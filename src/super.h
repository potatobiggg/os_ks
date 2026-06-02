#ifndef SUPER_H
#define SUPER_H

#include <stdint.h>

/*
 * ============================================================
 * super.h / super.c —— 超级块管理（inode & 数据块的分配与回收）
 * ============================================================
 *
 * 作用：本模块是文件系统的"资源管家"，管理两种核心资源：
 *   1. inode 编号  —— 文件/目录的唯一标识
 *   2. 数据块编号   —— 存放文件内容和目录项
 *
 * ---- inode 分配策略 (内存栈 + 磁盘扫描) ----
 *
 *   内存栈（g_sb.s_free_inode[50]，LIFO）：
 *     - 正常分配：s_nfree-- → 弹出 s_free_inode[s_nfree]，O(1)
 *     - 正常释放：push 进栈，O(1)
 *
 *   栈空时的回填（scan_free_inodes）：
 *     - 遍历磁盘 inode 区，找 di_mode == 0 的
 *     - 一次最多收集 50 个回填
 *
 *   栈满时的降级回收：
 *     - 如果释放时栈已满，只清零磁盘 inode (di_mode=0)
 *     - 下次栈空扫描时会被自动发现
 *
 * ---- 数据块分配策略 (成组链接法 Group Linking) ----
 *
 *   采用传统 UNIX 成组链接法，每组最多 50 个空闲块号：
 *
 *   正常分配：弹出栈顶，O(1)
 *
 *   栈空恢复：
 *     - 刚弹出的 blkno 本身是下一组空闲块的"链表头"
 *     - 读该块的 header[0..50] 回填栈
 *       header[0] = 组内块数, header[1..50] = 块号列表
 *
 *   栈满释放：
 *     - 当前 50 个块号写入被释放的块作为新组链表头
 *     - 栈重置为只含这一个被释放块
 *
 *   优点：超级块只占 512 字节，就能管理任意多的空闲块，
 *         不需要 FAT 表那样占用额外空间。
 *
 * ---- 辅助函数 (仅供格式化时使用) ----
 *   sb_init_free_inodes()  — 把所有 inode 推入空闲栈
 *   sb_init_free_blocks()  — 把所有数据块推入空闲栈
 */

/* ===================== inode 分配 ===================== */

/**
 * sb_ialloc — 分配一个空闲 inode 编号
 *
 * 1. 栈空 → scan_free_inodes() 扫描磁盘回填
 * 2. 仍空 → 返回 0（磁盘 inode 区满）
 * 3. s_nfree-- → 弹出 s_free_inode[s_nfree]
 * 4. s_fmod = 1（标记超级块脏）
 *
 * 返回：>= ROOT_INO 的 inode 编号，0 表示无空闲 inode
 */
uint16_t sb_ialloc(void);

/**
 * sb_ifree — 释放一个 inode 编号
 * @ino: 要释放的 inode 编号
 *
 * 1. zero_disk_inode(ino) — 磁盘 inode 全部清零
 * 2. 栈未满 → push
 * 3. 栈满 → 仅第1步（磁盘已清零，scan 能找到）
 */
void sb_ifree(uint16_t ino);

/* ===================== 数据块分配（成组链接法）===================== */

/**
 * sb_balloc — 分配一个空闲数据块编号
 *
 * 1. 栈空 → 返回 0（磁盘满）
 * 2. 弹出栈顶块号 blkno
 * 3. 若弹出后栈空 → blkno 指向下一组
 *    → 读 header 回填栈
 *    → header[0]=count 为 0 则最后一组用尽
 *
 * 返回：块号，0 表示磁盘满
 */
uint16_t sb_balloc(void);

/**
 * sb_bfree — 释放一个数据块编号（成组链接法核心）
 * @blkno: 要释放的数据块编号
 *
 * 情况 A（栈未满 < 50）：直接 push
 * 情况 B（栈满）：
 *   a) 当前栈 50 个块号写入 blkno 块作为新组链表头
 *   b) 栈重置为 [blkno]（count=1）
 */
void sb_bfree(uint16_t blkno);

/* ===================== 格式化辅助 ===================== */

/**
 * sb_init_free_blocks — 初始化空闲块栈（格式化用）
 * @start: 起始块号
 * @count: 块数量
 *
 * 把 [start, start+count-1] 区间所有块推入空闲栈。
 * 内部逐块调用 sb_bfree()，自动触发成组链接。
 */
void sb_init_free_blocks(uint16_t start, uint16_t count);

/**
 * sb_init_free_inodes — 初始化空闲 inode 栈（格式化用）
 * @total: inode 总数（如 512）
 *
 * 1. 栈清零
 * 2. 清零 inode 2~total 的磁盘结构（跳过根目录 inode 1）
 * 3. 逆序 push（total→2），LIFO 保证先分配小编号
 */
void sb_init_free_inodes(uint16_t total);

#endif /* SUPER_H */
