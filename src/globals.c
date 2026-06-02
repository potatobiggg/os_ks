#include "globals.h"

/*
 * ============================================================
 * globals.c —— 全局变量定义（实际内存分配）
 * ============================================================
 *
 * 注意：全局变量只在 .c 文件中定义一次，.h 文件只做 extern 声明。
 * 这样链接器不会报重复定义错误。
 */

/* ===== 超级块：整个文件系统的"元数据大脑"，含空闲资源栈 ===== */
SuperBlock g_sb;

/* ===== 内存 inode 哈希表：HASH_BUCKETS 个桶，每个桶是一个 MInode 链表头 =====
 * 初始值为 NULL（全局/静态变量自动初始化为 0）
 * 所有对磁盘 inode 的访问都先查此哈希表，命中则直接返回缓存 */
MInode *g_inode_hash[HASH_BUCKETS];

/* ===== 用户表数组 + 登录状态 ===== */
User g_users[MAX_USERS];
int g_current_user_idx = -1;  /* -1 表示当前无用户登录 */
int g_user_count = 0;         /* 系统实际创建的用户数 */

/* ===== 宿主文件：系统启动时由 block_init() 以 "rb+" 打开 =====
 * 如果文件不存在，block_init 返回 -1，main.c 会调用 fs_format() 创建新文件 */
FILE *g_fs_file = NULL;        /* 宿主文件的 FILE* 句柄 */
const char *g_fs_path = NULL;  /* 宿主文件路径，block_init() 中赋值 */
