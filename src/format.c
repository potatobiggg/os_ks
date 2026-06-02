#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "format.h"
#include "persist.h"
#include "super.h"
#include "globals.h"

/*
 * ============================================================
 * format.c —— 文件系统格式化（实现）
 * ============================================================
 *
 * 格式化是文件系统的"出厂初始化"操作。
 * 本文件只有一个公开函数 fs_format() 和一个内部辅助函数。
 *
 * 格式化后的磁盘状态：
 *   块 0：超 级 块
 *   块 INODE_AREA_START ~ ... ：inode 区
 *   块 DATA_AREA_START ~ ...  ：数据区（全部空闲）
 *   inode 1：根目录（已占用），其余全部空闲
 *   根目录包含 "."（ino=1）和 ".."（ino=1）两项
 */

/**
 * write_disk_inode — 将 DiskInode 结构写入磁盘 inode 区
 * @ino: inode 编号（1-based，1 = 根目录）
 * @dip: 指向 DiskInode 的指针（32 字节）
 *
 * 计算位置：
 *   所在块 = INODE_AREA_START + (ino-1) / INODES_PER_BLOCK
 *   块内偏移 = ((ino-1) % INODES_PER_BLOCK) × DISK_INODE_SIZE
 *
 * 先读整块 → 覆盖目标 inode → 写回整块
 *（保证同块其他 inode 不受影响）
 */
static void write_disk_inode(uint16_t ino, const DiskInode *dip)
{
    /* 计算 inode 在磁盘 inode 区中的物理位置 */
    uint16_t blk = INODE_AREA_START + (ino - 1) / INODES_PER_BLOCK;
    uint16_t off = ((ino - 1) % INODES_PER_BLOCK) * DISK_INODE_SIZE;

    char buf[BLOCK_SIZE];
    bread(blk, buf);
    memcpy(buf + off, dip, DISK_INODE_SIZE);
    bwrite(blk, buf);
}

/**
 * fs_format — 格式化（初始化）文件系统
 *
 * 执行严格的 10 步流程：
 *
 * 第 1 步：以 "wb+" 模式创建/覆盖宿主文件
 * 第 2 步：TOTAL_BLOCKS × 512 字节写零
 * 第 3 步：构造超级块（区域位置、大小）
 * 第 4 步：初始化空闲 inode 栈
 * 第 5 步：初始化空闲数据块栈（成组链接法）
 * 第 6 步：根目录占用 inode 1（不通过 sb_ialloc）
 * 第 7 步：为根目录分配一个数据块
 * 第 8 步：构造根目录 DiskInode
 * 第 9 步：写入 "." 和 ".." 两个目录项
 * 第 10 步：超级块写回磁盘
 *
 * 返回 0 成功，-1 失败（无法创建文件或无空闲块）
 */
int fs_format(void)
{
    char zero[BLOCK_SIZE];

    /* ──── 第 1 步：创建/覆盖宿主文件 ────
     * "wb+" = 读+写 + 不存在则创建 + 存在则截断为零 */
    g_fs_file = fopen(g_fs_path, "wb+");
    if (!g_fs_file) {
        fprintf(stderr, "format: cannot create %s\n", g_fs_path);
        return -1;
    }

    /* ──── 第 2 步：所有磁盘块写零 ────
     * 逐块写入 512 字节全零，确保无残留脏数据 */
    memset(zero, 0, BLOCK_SIZE);
    for (uint16_t i = 0; i < TOTAL_BLOCKS; i++) {
        fseek(g_fs_file, (long)i * BLOCK_SIZE, SEEK_SET);
        fwrite(zero, 1, BLOCK_SIZE, g_fs_file);
    }
    fflush(g_fs_file);

    /* ──── 第 3 步：构造超级块 ────
     * 设置各区域的起止块号和大小 */
    memset(&g_sb, 0, sizeof(g_sb));
    g_sb.s_inode_area_start = INODE_AREA_START;       /* inode 区起始块号 */
    g_sb.s_inode_area_size  = INODE_AREA_BLOCKS;      /* inode 区块数 */
    g_sb.s_data_area_start  = DATA_AREA_START;        /* 数据区起始块号 */
    g_sb.s_data_area_size   = DATA_AREA_BLOCKS;       /* 数据区块数 */
    g_sb.s_total_blocks     = TOTAL_BLOCKS;           /* 总块数 */
    g_sb.s_fmod             = 0;                      /* 干净（未修改） */

    /* ──── 第 4 步：初始化空闲 inode 栈 ────
     * 把 inode 2 ~ TOTAL_INODES 推入空闲栈 */
    sb_init_free_inodes(TOTAL_INODES);

    /* ──── 第 5 步：初始化空闲数据块栈 ────
     * 逐块调用 sb_bfree()，自动触发成组链接 */
    sb_init_free_blocks(DATA_AREA_START, DATA_AREA_BLOCKS);

    /* ──── 第 6 步：根目录专用 inode 1 ────
     * 不通过 sb_ialloc 分配，避免被普通文件占用 */

    /* ──── 第 7 步：为根目录分配数据块 ──── */
    uint16_t root_blk = sb_balloc();
    if (root_blk == 0) {
        fprintf(stderr, "format: no free block for root directory\n");
        fclose(g_fs_file);
        g_fs_file = NULL;
        return -1;
    }

    /* ──── 第 8 步：构造根目录 DiskInode ────
     *
     * 字段含义：
     *   di_mode  = FT_DIR | 权限      → 目录 + rwxr-xr-x
     *   di_nlink = 2                 → "." 和 ".." 两个链接
     *   di_uid / di_gid = 0         → root 用户
     *   di_size  = 2 × 16 = 32 字节 → 仅 . 和 .. 两个目录项
     *   di_addr[0] = root_blk       → 数据所在块号
     *   di_addr[1..9] = 0           → 暂未使用（留给间接索引） */
    DiskInode root_di;
    memset(&root_di, 0, sizeof(root_di));
    root_di.di_mode  = DEFAULT_DIR_MODE;
    root_di.di_nlink = 2;                   /* "." 和 ".." */
    root_di.di_uid   = 0;
    root_di.di_gid   = 0;
    root_di.di_size  = 2 * DIR_ENTRY_SIZE;  /* 32 字节 */
    root_di.di_addr[0] = root_blk;
    /* addr[1..9] 已由 memset 初始化为 0 */

    write_disk_inode(ROOT_INO, &root_di);

    /* ──── 第 9 步：初始化根目录数据块 ────
     *
     * 目录内容 = DirEntry 数组，每项 16 字节：
     *   [0] "."  → inode 1（指向自身）
     *   [1] ".." → inode 1（根目录的父目录就是自己）
     * 剩余 de_ino==0 表示空闲目录项 */
    DirEntry dir_block[DIR_ENTRIES_PER_BLOCK];
    memset(dir_block, 0, sizeof(dir_block));

    /* 目录项 0："." — 当前目录 */
    strncpy(dir_block[0].de_name, ".", DIR_ENTRY_NAME_LEN);
    dir_block[0].de_ino = ROOT_INO;

    /* 目录项 1：".." — 父目录（根目录的父目录是自身） */
    strncpy(dir_block[1].de_name, "..", DIR_ENTRY_NAME_LEN);
    dir_block[1].de_ino = ROOT_INO;

    bwrite(root_blk, dir_block);

    /* ──── 第 10 步：超级块写回磁盘 ────
     * 空闲 inode 栈和空闲 block 栈都已正确填充 */
    sb_save();

    printf("File system formatted: %u blocks, %u inodes\n",
           TOTAL_BLOCKS, TOTAL_INODES);
    printf("Data area: blocks %u-%u (%u blocks)\n",
           DATA_AREA_START, DATA_AREA_START + DATA_AREA_BLOCKS - 1,
           DATA_AREA_BLOCKS);

    return 0;
}
