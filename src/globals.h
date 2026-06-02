#ifndef GLOBALS_H
#define GLOBALS_H

#include <stdio.h>
#include "types.h"

/*
 * ============================================================
 * globals.h / globals.c --- 全局变量模块
 * ============================================================
 *
 * 作用：定义整个 VFS 系统中所有模块共享的全局变量。
 * 所有全局变量在 globals.c 中定义（分配实际内存），
 * 在此头文件中以 extern 方式声明，供其他 .c 文件引用。
 *
 * 包含的全局变量：
 *   - g_sb：           内存中的超级块（含有空闲 inode / block 栈）
 *   - g_inode_hash[]： 内存 inode 哈希表（磁盘 inode 的缓存）
 *   - g_users[]：      用户表，每个用户私有一张打开文件表
 *   - g_fs_file：      宿主文件的 FILE* 指针，所有磁盘读写依赖它
 *   - g_fs_path：      宿主文件的完整路径
 */

/* ===== 超级块（内存态）：存放空闲 inode 栈 + 空闲 block 栈 ===== */
extern SuperBlock g_sb;

/* ===== 内存 inode 哈希表：各桶链表头指针数组 ===== */
extern MInode *g_inode_hash[HASH_BUCKETS];

/* ===== 用户表：系统最多管理 MAX_USERS 个用户 ===== */
extern User g_users[MAX_USERS];
extern int g_current_user_idx;  /* 当前登录用户在 g_users[] 中的索引，-1 = 无人登录 */
extern int g_user_count;        /* 系统实际已创建的用户个数 */

/* ===== 宿主文件指针：读写磁盘块都通过 C 标准库 fread/fwrite 操作此文件 ===== */
extern FILE *g_fs_file;
/* ===== 宿主文件路径：如 "filesystem.dat" ===== */
extern const char *g_fs_path;

/* ===== 根目录 inode 编号固定为 1 ===== */
#define ROOT_INO 1

#endif /* GLOBALS_H */
