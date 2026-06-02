#ifndef PERSIST_H
#define PERSIST_H

#include <stdint.h>

/*
 * ============================================================
 * persist.h / persist.c —— 持久化 / 块 I/O 层
 * ============================================================
 *
 * 作用：封装所有底层磁盘读写的细节。上层代码只需要调用
 * bread() / bwrite() 来读写逻辑块，完全不需要关心底层
 * 是 fopen / fread / fwrite 如何实现的。
 *
 * 磁盘模型：
 *   整个文件系统存储在宿主文件（如 filesystem.dat）中。
 *   宿主文件被划分为 TOTAL_BLOCKS 个块，每块 512 字节。
 *   块号从 0 开始：块 0 是超级块，块 1~N 是 inode 区，
 *   再之后是数据区。
 *
 * 主要接口：
 *   - block_init(path)：打开宿主文件，失败则返回 -1（触发格式化）
 *   - bread(blkno, buf)：读取第 blkno 号块（512 字节）
 *   - bwrite(blkno, buf)：写入第 blkno 号块（512 字节）
 *   - sb_load()：从块 0 读取超级块到 g_sb
 *   - sb_save()：将 g_sb 写回到块 0
 *   - fs_shutdown()：关闭宿主文件，刷新缓冲区
 */

/**
 * block_init - 初始化块 I/O 层，打开宿主文件
 * @host_path: 宿主文件的路径，如 "filesystem.dat"
 *
 * 用 "rb+" 模式打开（读写已有文件，不截断）。
 * 如果文件不存在则返回 -1，由 main.c 调用 fs_format() 创建新文件。
 * 返回值：0 表示成功，-1 表示文件不存在
 */
int block_init(const char *host_path);

/**
 * bread - 读取一个磁盘块到内存缓冲区（Block Read）
 * @blkno: 要读取的块号（0 ~ TOTAL_BLOCKS-1）
 * @buf:   存放读取内容的缓冲区，至少 512 字节
 *
 * 偏移量 = blkno * 512，一次读取 512 字节。
 * 越界或读不到完整 512 字节时，buf 会被填充为全零。
 * 返回值：0 表示成功，-1 表示失败
 */
int bread(uint16_t blkno, void *buf);

/**
 * bwrite - 将内存缓冲区写入一个磁盘块（Block Write）
 * @blkno: 要写入的块号（0 ~ TOTAL_BLOCKS-1）
 * @buf:   存放写入内容的缓冲区，至少 512 字节
 *
 * 偏移量 = blkno * 512，一次写入 512 字节。
 * 写入后立即 fflush()，确保数据持久化到磁盘。
 * 返回值：0 表示成功，-1 表示失败
 */
int bwrite(uint16_t blkno, const void *buf);

/**
 * sb_load - 从块 0（超级块位置）读取超级块到内存全局变量 g_sb
 */
void sb_load(void);

/**
 * sb_save - 将内存中的 g_sb 写回块 0
 *
 * 写之前先把 s_fmod（修改标记）清零，表示超级块是"干净"的。
 */
void sb_save(void);

/**
 * fs_shutdown - 安全关闭文件系统
 *
 * 先 fflush 刷新 C 库缓冲区，再 fclose 关闭文件句柄，
 * 最后置 g_fs_file = NULL 防止悬空指针。
 */
void fs_shutdown(void);

#endif /* PERSIST_H */
