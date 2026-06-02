#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "persist.h"
#include "globals.h"

/*
 * ============================================================
 * persist.c —— 持久化 / 块 I/O 层（实现）
 * ============================================================
 *
 * 本文件实现了 persist.h 中声明的所有函数。
 * 核心思路：把文件系统视为逻辑块数组，通过 fseek + fread/fwrite
 * 实现随机块访问。每块固定 512 字节（BLOCK_SIZE），偏移 = 块号 × 512。
 *
 * 磁盘布局（逻辑视图）：
 *   块 0           → 超级块（super block）
 *   块 INODE_AREA_START ~ +size-1  → inode 区
 *   块 DATA_AREA_START ~ +size-1    → 数据区
 */

/**
 * block_init — 以读写模式打开宿主文件
 * @host_path: 宿主文件路径，如 "filesystem.dat"
 *
 * "rb+" 模式不会截断文件，保留已有内容。
 * 如果宿主文件还不存在，fopen 返回 NULL，本函数返回 -1，
 * 由 main.c 调用 fs_format() 创建新文件。
 */
int block_init(const char *host_path)
{
    g_fs_path = host_path;

    /* 尝试以 "rb+" 模式打开已有的宿主文件 */
    g_fs_file = fopen(host_path, "rb+");
    if (!g_fs_file) {
        /* 文件不存在 → 返回 -1，main.c 将调用 fs_format() 创建新文件 */
        return -1;
    }
    return 0;
}

/**
 * bread — 从宿主文件读取第 blkno 号块（Block Read）
 * @blkno: 要读取的块号（0 ~ TOTAL_BLOCKS-1）
 * @buf:   存放读取内容的缓冲区，至少 512 字节
 *
 * 偏移量 = blkno × 512，一次读取 512 字节。
 * 越界或读不到完整 512 字节时，buf 填充全零，返回 -1。
 * 返回值：0 成功，-1 失败。
 */
int bread(uint16_t blkno, void *buf)
{
    if (!g_fs_file) return -1;           /* 宿主文件未打开 */
    if (blkno >= TOTAL_BLOCKS) {
        /* 块号越界 → 返回全零缓冲区 */
        memset(buf, 0, BLOCK_SIZE);
        return -1;
    }
    fseek(g_fs_file, (long)blkno * BLOCK_SIZE, SEEK_SET);
    size_t n = fread(buf, 1, BLOCK_SIZE, g_fs_file);
    if (n != BLOCK_SIZE) {
        /* 实际读到的字节数不对 → 返回全零缓冲区 */
        memset(buf, 0, BLOCK_SIZE);
        return -1;
    }
    return 0;
}

/**
 * bwrite — 向宿主文件写入第 blkno 号块（Block Write）
 * @blkno: 要写入的块号（0 ~ TOTAL_BLOCKS-1）
 * @buf:   存放写入内容的缓冲区，至少 512 字节
 *
 * 偏移量 = blkno × 512，一次写入 512 字节。
 * 写入后立即 fflush()，强制将 C 库缓冲区刷新到 OS，
 * 减少意外断电丢数据的风险。
 * 返回值：0 成功，-1 失败。
 */
int bwrite(uint16_t blkno, const void *buf)
{
    if (!g_fs_file) return -1;
    if (blkno >= TOTAL_BLOCKS) return -1;
    fseek(g_fs_file, (long)blkno * BLOCK_SIZE, SEEK_SET);
    size_t n = fwrite(buf, 1, BLOCK_SIZE, g_fs_file);
    if (n != BLOCK_SIZE) return -1;
    fflush(g_fs_file);  /* 强制刷新到 OS 缓冲区 */
    return 0;
}

/**
 * sb_load — 从块 0（超级块位置）读超级块到内存 g_sb
 *
 * 调用 bread 读取第 SUPER_BLOCK_NO 号块（即块 0），
 * 覆盖 g_sb。启动时调用一次。
 */
void sb_load(void)
{
    bread(SUPER_BLOCK_NO, &g_sb);
}

/**
 * sb_save — 将内存中的 g_sb 写回块 0
 *
 * 写之前先把 s_fmod（脏标记）清零，表示超级块是"干净"的。
 * 退出时调用，确保空闲栈、区域信息等元数据持久化。
 */
void sb_save(void)
{
    g_sb.s_fmod = 0;                    /* 清脏标记 */
    bwrite(SUPER_BLOCK_NO, &g_sb);      /* 写回块 0 */
}

/**
 * fs_shutdown — 安全关闭文件系统
 *
 * 先 fflush 刷新 C 库缓冲区到 OS，
 * 再 fclose 关闭文件句柄释放系统资源，
 * 最后 g_fs_file = NULL 防止悬空指针。
 */
void fs_shutdown(void)
{
    if (g_fs_file) {
        fflush(g_fs_file);              /* 刷新未写入的缓冲数据 */
        fclose(g_fs_file);              /* 关闭文件 */
        g_fs_file = NULL;               /* 防止悬空指针 */
    }
}
