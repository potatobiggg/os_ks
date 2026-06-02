#include <string.h>
#include "super.h"
#include "persist.h"
#include "globals.h"

/*
 * ============================================================
 * super.c —— 超级块管理（实现）
 * ============================================================
 *
 * 本文件实现了 super.h 中声明的所有函数。
 * 核心职责分两大部分：
 *
 * ---- 一、Inode 分配与回收 (内存栈 + 磁盘扫描) ----
 *
 *   内存栈（g_sb.s_free_inode[50]，LIFO）：
 *     分配：s_nfree-- → 弹出，O(1)
 *     释放：push 进栈，O(1)
 *
 *   栈空时的回填（scan_free_inodes）：
 *     遍历磁盘 inode 区，找 di_mode == 0 的
 *     一次最多收集 STACK_REFILL_COUNT(50) 个
 *
 *   栈满时的降级回收：
 *     释放时若栈满，只清零磁盘 inode（di_mode=0）
 *     下次栈空扫描时会被自动发现
 *
 * ---- 二、数据块分配与回收 (成组链接法 Group Linking) ----
 *
 *   每组 50 个空闲块号，超级块只存当前组。
 *
 *   正常分配：弹栈，O(1)
 *
 *   栈空恢复：
 *     刚弹出的 blkno 指向下一组 → 读 header 回填
 *     header[0] = 组内块数, header[1..50] = 块号列表
 *
 *   栈满释放：
 *     当前栈写入释放块作为新组链表头
 *     栈重置为该释放块（count=1）
 *
 *   优点：512 字节管理任意多空闲块，不需 FAT 表。
 *
 * 内部辅助函数（static，仅本文件可见）：
 *   read_inode_mode()  — 读取磁盘 inode 的 di_mode 字段
 *   zero_disk_inode()  — 清空磁盘 inode（标记空闲）
 *   scan_free_inodes() — 扫描磁盘区收集空闲 inode
 */

/* 每次磁盘扫描最多收集的空闲 inode 数 */
#define STACK_REFILL_COUNT 50

/**
 * read_inode_mode — 只读取磁盘 inode 的 di_mode 字段（2 字节）
 * @ino: inode 编号（1-based）
 *
 * 所在块 = INODE_AREA_START + (ino-1) / INODES_PER_BLOCK
 * 偏移量 = ((ino-1) % INODES_PER_BLOCK) × DISK_INODE_SIZE
 *
 * di_mode == 0 表示该 inode 空闲，可被分配。
 * 只读 2 字节，避免加载整个 32 字节的 DiskInode。
 *
 * 返回 di_mode 的值。
 */
static uint16_t read_inode_mode(uint16_t ino)
{
    /* 计算 inode 在磁盘 inode 区中的位置 */
    uint16_t blk = INODE_AREA_START + (ino - 1) / INODES_PER_BLOCK;
    uint16_t off = ((ino - 1) % INODES_PER_BLOCK) * DISK_INODE_SIZE;

    char buf[BLOCK_SIZE];
    bread(blk, buf);                        /* 读入所在块 */
    uint16_t mode;
    memcpy(&mode, buf + off, sizeof(uint16_t)); /* 取前 2 字节 */
    return mode;
}

/**
 * zero_disk_inode — 将磁盘 inode 全部清零（标记为空闲）
 * @ino: inode 编号
 *
 * 读整块 → 清零该 inode 的 32 字节区域 → 写回整块。
 * 操作过程保证同一块中的其他 inode 不受影响。
 * di_mode == 0 即空闲，scan_free_inodes() 会检测到。
 */
static void zero_disk_inode(uint16_t ino)
{
    uint16_t blk = INODE_AREA_START + (ino - 1) / INODES_PER_BLOCK;
    uint16_t off = ((ino - 1) % INODES_PER_BLOCK) * DISK_INODE_SIZE;

    char buf[BLOCK_SIZE];
    bread(blk, buf);                            /* 1. 读出整块 */
    memset(buf + off, 0, DISK_INODE_SIZE);      /* 2. 清零目标区域 */
    bwrite(blk, buf);                           /* 3. 写回整块 */
}

/**
 * scan_free_inodes — 扫描磁盘 inode 区，回填空闲栈
 *
 * 扫描范围：ROOT_INO ~ TOTAL_INODES
 * 判断空闲：di_mode == 0
 * 最多收集 STACK_REFILL_COUNT（50）个
 *
 * 从小编号开始扫描，保证小编号优先进栈。
 * 返回值：实际找到的空闲 inode 数量，0 = 磁盘 inode 区满。
 */
static int scan_free_inodes(void)
{
    int found = 0;
    /* 从小编号开始扫描，保证小编号 inode 优先被使用 */
    for (uint16_t ino = ROOT_INO; ino <= TOTAL_INODES && found < STACK_REFILL_COUNT; ino++) {
        if (read_inode_mode(ino) == 0) {
            g_sb.s_free_inode[found] = ino;   /* 填入栈 */
            found++;
        }
    }
    g_sb.s_nfree = found;   /* 更新栈顶指针 */
    return found;
}

/* ====================================================
 * Inode 分配与回收 —— 公开接口
 * ==================================================== */

/**
 * sb_ialloc — 分配一个空闲 inode 编号
 *
 * 1. 若栈空 → scan_free_inodes() 扫描磁盘回填
 * 2. 若回填后仍空 → 返回 0（磁盘 inode 区满）
 * 3. 弹栈：s_nfree--，取值，标记脏
 *
 * 返回：>= ROOT_INO 的 inode 编号，0 = 无空闲 inode
 */
uint16_t sb_ialloc(void)
{
    /* 第 1 步：栈空 → 扫描磁盘回填 */
    if (g_sb.s_nfree == 0) {
        if (scan_free_inodes() == 0)
            return 0;   /* 磁盘 inode 区满 */
    }

    /* 第 2 步：弹栈（LIFO） */
    g_sb.s_nfree--;
    uint16_t ino = g_sb.s_free_inode[g_sb.s_nfree];

    /* 第 3 步：标记超级块为脏，提醒退出时写回 */
    g_sb.s_fmod = 1;
    return ino;
}

/**
 * sb_ifree — 释放一个 inode 编号
 * @ino: 要释放的 inode 编号
 *
 * 1. 清零磁盘 inode（zero_disk_inode）
 * 2. 栈未满 → push 进栈
 * 3. 栈满 → 不做 push（磁盘已清零，下次 scan 能找到）
 * 4. 标记脏
 */
void sb_ifree(uint16_t ino)
{
    /* 第 1 步：磁盘 inode 清零（di_mode = 0） */
    zero_disk_inode(ino);

    /* 第 2 步：栈有空间 → push */
    if (g_sb.s_nfree < FREE_INODE_STACK_SIZE) {
        g_sb.s_free_inode[g_sb.s_nfree] = ino;
        g_sb.s_nfree++;
    }
    /* 第 3 步（隐式）：栈满不 push，磁盘已清零，scan 会发现 */

    /* 第 4 步：标记脏 */
    g_sb.s_fmod = 1;
}

/* ====================================================
 * 数据块分配与回收 —— 成组链接法（公开接口）
 * ==================================================== */

/**
 * sb_balloc — 分配一个空闲数据块编号
 *
 * 1. 栈空 → 返回 0（磁盘满）
 * 2. 弹栈取 blkno
 * 3. 若弹出后栈空 → blkno 指向下一组
 *    → 读 blkno 块的 header[0..50]
 *    → header[0] = count, header[1..50] = 块号列表
 *    → 回填栈
 * 4. count == 0 → 最后一组用尽，栈保持空
 * 5. 标记脏
 *
 * 返回：分配的块号，0 = 磁盘数据区满
 */
uint16_t sb_balloc(void)
{
    /* 第 1 步：栈空 → 磁盘满 */
    if (g_sb.s_nfree_block == 0)
        return 0;

    /* 第 2 步：弹栈 */
    g_sb.s_nfree_block--;
    uint16_t blkno = g_sb.s_free_block[g_sb.s_nfree_block];

    /* 第 3 步：若弹出后栈变空，恢复下一组 */
    if (g_sb.s_nfree_block == 0) {
        uint16_t header[256];           /* 整块 = 256 个 uint16_t */
        bread(blkno, header);

        uint16_t count = header[0];     /* 组内空闲块数量 */
        /* 合法性检查：count 必须在 [1, GROUP_SIZE] 范围内 */
        if (count > 0 && count <= GROUP_SIZE) {
            /* header[1..50] 回填栈 */
            for (uint16_t i = 0; i < count; i++) {
                g_sb.s_free_block[i] = header[i + 1];
            }
            g_sb.s_nfree_block = count;
        }
        /* count == 0：最后一组，栈保持空 */
    }

    /* 第 5 步：标记脏 */
    g_sb.s_fmod = 1;
    return blkno;
}

/**
 * sb_bfree — 释放一个数据块编号（成组链接法核心）
 * @blkno: 要释放的数据块编号
 *
 * 情况 A（栈未满，< 50）→ 直接 push
 *
 * 情况 B（栈满，== 50）→ dump 栈到 blkno 块形成新组：
 *   a) header[0] = 50（当前组块数）
 *   b) header[1..50] = 栈中的 50 个块号
 *   c) bwrite(blkno, header) 写入磁盘
 *   d) 栈重置为 [blkno]，s_nfree_block = 1
 *
 * 下次 sb_balloc 弹出 blkno 时会发现 header 并恢复栈，
 * 实现循环成组链接。
 */
void sb_bfree(uint16_t blkno)
{
    if (g_sb.s_nfree_block < FREE_BLOCK_STACK_SIZE) {
        /* 情况 A：栈有空间，直接 push */
        g_sb.s_free_block[g_sb.s_nfree_block] = blkno;
        g_sb.s_nfree_block++;
    } else {
        /* 情况 B：栈满 → dump 到 blkno 块形成新组 */
        uint16_t header[256];
        memset(header, 0, sizeof(header));

        header[0] = g_sb.s_nfree_block;     /* 组内块数 = 50 */

        /* 将当前栈中的 50 个块号复制到 header[1..50] */
        for (uint16_t i = 0; i < g_sb.s_nfree_block; i++) {
            header[i + 1] = g_sb.s_free_block[i];
        }

        /* 写入 blkno 盘块 */
        bwrite(blkno, header);

        /* 栈重置：只含 blkno 一个元素 */
        g_sb.s_nfree_block = 1;
        g_sb.s_free_block[0] = blkno;
    }

    /* 标记脏 */
    g_sb.s_fmod = 1;
}

/* ====================================================
 * 格式化辅助函数（仅 fs_format() 调用）
 * ==================================================== */

/**
 * sb_init_free_blocks — 初始化空闲块栈（格式化用）
 * @start: 起始块号（含）
 * @count: 块数量
 *
 * 把 [start, start+count-1] 范围内的所有块
 * 逐块调用 sb_bfree()，自动触发成组链接。
 *
 * 例如：sb_init_free_blocks(34, 512) 依次释放块 34~545
 */
void sb_init_free_blocks(uint16_t start, uint16_t count)
{
    uint16_t total = start + count;

    for (uint16_t blk = start; blk < total; blk++) {
        sb_bfree(blk);   /* 逐块 push，满了自动 dump 成新组 */
    }
}

/**
 * sb_init_free_inodes — 初始化空闲 inode 栈（格式化用）
 * @total: inode 总数（如 TOTAL_INODES = 512）
 *
 * 格式化时只使用 inode 1（根目录），所以要：
 *
 * 1. 栈指针 s_nfree 归零
 * 2. 将 inode 2 ~ total 的磁盘结构全部清零（di_mode = 0）
 *    （跳过 inode 1，它已经被根目录占用）
 * 3. 逆序 push（total → 2）进栈
 *
 * 为什么逆序 push？
 * 栈是 LIFO，最后 push 的最先弹出。逆序 push 使得
 * 小编号 inode 在栈顶，分配时优先得到小编号。
 */
void sb_init_free_inodes(uint16_t total)
{
    g_sb.s_nfree = 0;   /* 栈指针归零 */

    /* 第 1 步：清零 inode 2 ~ total */
    for (uint16_t ino = 2; ino <= total; ino++) {
        zero_disk_inode(ino);   /* 磁盘 inode 清零，跳过根 inode 1 */
    }

    /* 第 2 步：逆序 push（从 total 到 2）
     * 逆序保证分配时先取小编号 */
    for (uint16_t ino = total; ino >= 2; ino--) {
        if (g_sb.s_nfree >= FREE_INODE_STACK_SIZE)
            break;   /* 栈满（最多 50 个），停止 */

        g_sb.s_free_inode[g_sb.s_nfree] = ino;
        g_sb.s_nfree++;
    }
}
