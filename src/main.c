#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "persist.h"
#include "format.h"
#include "inode.h"
#include "file.h"
#include "dir.h"
#include "user.h"
#include "globals.h"

/*
 * ============================================================
 * main.c —— VFS 程序入口 & 命令行交互循环（REPL）
 * ============================================================
 *
 * 本文件是整个程序的入口和用户交互界面。
 *
 * 三大职责：
 *
 * 1. 启动初始化
 *    — 解析命令行参数获取宿主文件路径
 *    — 尝试打开已有文件系统，失败则自动格式化
 *    — 加载超级块、初始化 inode 哈希表、初始化用户
 *
 * 2. 命令循环（REPL = Read-Eval-Print Loop）
 *    — 读取用户输入 → 解析命令名和参数 → 分发到对应模块
 *    — 支持的命令：login / logout / create / open / close /
 *      read / write / seek / delete / mkdir / chdir / dir /
 *      format / help / exit
 *
 * 3. 安全退出
 *    — flush 所有脏 inode → 保存超级块 → 关闭宿主文件
 *
 * 内部辅助函数（static）：
 *   show_banner() — 打印欢迎横幅
 *   show_help()   — 打印命令列表
 *   next_arg()    — 从输入行切分下一个参数
 *   parse_mode()  — "r"/"w"/"rw" → 打开标志位
 *   parse_fd()    — 字符串 → fd 编号（带校验）
 */

/* ====================================================
 * 辅助函数
 * ==================================================== */

/**
 * show_banner — 打印程序启动时的欢迎横幅
 *
 * 显示项目名称和课程说明，仅启动时调用一次。
 */
static void show_banner(void)
{
    printf("\n");
    printf("============================================\n");
    printf("  VFS - UNIX-like Virtual File System\n");
    printf("  OS Course Design Project\n");
    printf("============================================\n");
    printf("\n");
}

/**
 * show_help — 打印所有可用命令的帮助信息
 *
 * 用户输入 "help" 时调用。
 * 每条命令包含命令名、参数格式和功能简述。
 */
static void show_help(void)
{
    printf("\nAvailable commands:\n");
    printf("  login   <username> <pw>   - Log in (usr1-usr8, default pw: 123)\n");
    printf("  logout                    - Log out current user\n");
    printf("  create  <path> [r|rw|rwx] - Create a new file\n");
    printf("  open    <path> <mode>     - Open a file (r/w/a/rw)\n");
    printf("  close   <fd>              - Close a file\n");
    printf("  read    <fd> <size>       - Read from a file\n");
    printf("  write   <fd> <text>       - Write to a file\n");
    printf("  seek    <fd> <offset>     - Seek to position in file\n");
    printf("  delete  <path>            - Delete a file\n");
    printf("  mkdir   <path>            - Create a directory\n");
    printf("  chdir   <path>            - Change directory\n");
    printf("  dir     [path]            - List directory contents\n");
    printf("  format                    - Format the file system\n");
    printf("  help                      - Show this help\n");
    printf("  exit                      - Save and exit\n");
    printf("\n");
}

/**
 * next_arg — 从输入字符串中切分下一个空格分隔的参数
 * @s: 指向字符串指针的指针（会被修改，指向剩余部分）
 *
 * 工作流程：
 *   1. 跳过前导空格/Tab
 *   2. 标记 token 起始位置
 *   3. 扫描到空格/Tab/字符串尾
 *   4. 分隔处写 '\0'（原地截断），*s 后移
 *
 * 返回值：token 起始指针，无更多 token 返回 NULL。
 *
 * ⚠ 注意：此函数会修改原始字符串（插入 '\0'），不可用于 const 串！
 */
static char *next_arg(char **s)
{
    /* 跳过所有前导空白字符 */
    while (**s == ' ' || **s == '\t') (*s)++;
    /* 已到字符串末尾 → 没有更多参数 */
    if (**s == '\0') return NULL;

    /* 记录当前参数起始位置 */
    char *start = *s;
    /* 扫描到下一个空白或字符串结尾 */
    while (**s && **s != ' ' && **s != '\t') (*s)++;
    if (**s) {
        /* 在分隔符位置写入 '\0' 截断，*s 跳过分隔符 */
        **s = '\0';
        (*s)++;
    }
    return start;
}

/**
 * parse_mode — 将打开模式字符串解析为标志位
 * @s: 模式字符串，如 "r", "w", "rw", "a"
 *
 * 映射关系：
 *   "r" / "read"   → O_RDONLY           （只读）
 *   "w" / "write"  → O_WRONLY | O_TRUNC （只写 + 截断）
 *   "a" / "append" → O_WRONLY | O_APPEND（只写 + 追加）
 *   "rw" / "wr"    → O_RDWR             （读写）
 *
 * 返回：标志位组合，0 表示无法识别（调用者默认用 O_RDONLY）
 */
static uint16_t parse_mode(const char *s)
{
    if (!s) return 0;
    if (strcmp(s, "r") == 0 || strcmp(s, "read") == 0)   return O_RDONLY;
    if (strcmp(s, "w") == 0 || strcmp(s, "write") == 0)  return O_WRONLY | O_TRUNC;
    if (strcmp(s, "a") == 0 || strcmp(s, "append") == 0) return O_WRONLY | O_APPEND;
    if (strcmp(s, "rw") == 0 || strcmp(s, "wr") == 0)    return O_RDWR;
    return 0;
}

/**
 * parse_fd — 将文件描述符字符串解析为整数
 * @s: fd 字符串，如 "0", "1", "2"
 *
 * 三步校验：
 *   1. 空字符串检查
 *   2. 逐字符验证全部是数字 '0'~'9'
 *   3. 范围检查：必须 < MAX_OPEN_FILES
 *
 * 返回：有效的 fd 编号（>= 0），-1 表示无效
 */
static int parse_fd(const char *s)
{
    if (!s || *s == '\0') return -1;         /* 空字符串 */

    /* 第 1 步：验证所有字符都是数字 */
    for (const char *p = s; *p; p++) {
        if (*p < '0' || *p > '9') return -1; /* 非数字 */
    }

    /* 第 2 步：字符串转整数 */
    int fd = 0;
    for (const char *p = s; *p; p++) {
        fd = fd * 10 + (*p - '0');
    }

    /* 第 3 步：范围检查 */
    if (fd >= MAX_OPEN_FILES) return -1;
    return fd;
}

/* ====================================================
 * 主函数
 * ==================================================== */

/**
 * main — VFS 虚拟文件系统主入口
 * @argc: 命令行参数个数
 * @argv: 命令行参数数组
 *
 * 用法：./vfs.exe [host_file_path]
 * 不指定路径则默认 "filesystem.dat"
 *
 * 三大阶段：
 *
 * ——— 阶段 I：启动初始化 ———
 *   尝试打开已有文件系统，不存在则自动格式化。
 *   加载超级块、初始化 inode 哈希、初始化用户表。
 *
 * ——— 阶段 II：主命令循环（REPL） ———
 *   无限循环读取用户输入行，解析命令并分发：
 *
 *   exit  / quit  → 保存并退出
 *   help          → 打印帮助
 *   format        → 格式化（⚠ 清除所有数据）
 *   login         → fs_login(username, password)
 *   logout        → fs_logout()
 *   create        → fs_create(path, mode)
 *   open          → fs_open(path, flags)
 *   close         → fs_close(fd)
 *   read          → fs_read(fd, buf, size)
 *   write         → fs_write(fd, text, len)
 *   seek          → fs_lseek(fd, offset, 0)
 *   delete / rm   → fs_delete(path)
 *   mkdir         → fs_mkdir(path)
 *   chdir / cd    → fs_chdir(path)
 *   dir / ls      → fs_dir(path)  [path 可省略，用当前目录]
 *
 * ——— 阶段 III：安全退出 ———
 *   1. inode_flush_all() — 所有脏 inode 写回
 *   2. sb_save()         — 超级块写回块 0
 *   3. fs_shutdown()     — 关闭宿主文件
 *
 * 返回 0 正常退出，1 格式化失败
 */
int main(int argc, char *argv[])
{
    const char *fs_path;

    /* ——— 解析宿主文件路径 ——— */
    if (argc > 1) {
        fs_path = argv[1];               /* 使用命令行指定路径 */
    } else {
        fs_path = "filesystem.dat";      /* 默认路径 */
    }

    /* ——— 阶段 I：启动初始化 ——— */
    show_banner();
    printf("File system: %s\n", fs_path);

    /*
     * 尝试打开已有文件系统：
     *
     * block_init(fs_path)：
     *   成功（返回 0）  → 宿主文件已存在
     *                    → sb_load() 加载超级块
     *                    → inode_hash_init() 初始化内存哈希
     *                    → user_init() 初始化用户表
     *
     *   失败（返回 -1） → 宿主文件不存在
     *                    → 设置 g_fs_path（format 需要）
     *                    → inode_hash_init()
     *                    → fs_format() 格式化新系统
     *                    → user_init()
     */
    if (block_init(fs_path) == 0) {
        /* 加载已有文件系统 */
        printf("Loaded existing file system.\n");
        sb_load();                   /* 从块 0 读超级块到 g_sb */
        inode_hash_init();           /* 初始化 inode 哈希表（空） */
        user_init();                 /* 初始化 8 个内置用户 */
    } else {
        /* 首次启动：格式化新文件系统 */
        printf("File system not found. Starting fresh.\n");
        g_fs_path = fs_path;         /* ⚠ 必须在 fs_format 之前赋值 */
        inode_hash_init();           /* 初始化 inode 哈希表（空） */
        if (fs_format() != 0) {
            /* 格式化失败（磁盘空间/权限等）→ 退出 */
            fprintf(stderr, "Format failed!\n");
            return 1;
        }
        user_init();                 /* 格式化后初始化用户 */
    }

    /* 操作提示 */
    printf("Type 'help' for available commands.\n");
    printf("(You must 'login' first before using file operations.)\n\n");

    /* ——— 阶段 II：主命令循环（REPL） ——— */
    char line[1024];  /* 输入缓冲区，一次一行 */
    for (;;) {
        /* 显示 UNIX 风格提示符 */
        printf("$ ");
        fflush(stdout);              /* 确保提示符立即可见 */

        /* 读取一行输入，EOF 则退出循环 */
        if (!fgets(line, sizeof(line), stdin))
            break;

        /* 去掉末尾的换行符 '\n' */
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n')
            line[len - 1] = '\0';
        /* 纯空行 → 跳过 */
        if (len == 0) continue;

        /* 跳过行首空白 */
        char *s = line;
        while (*s == ' ' || *s == '\t') s++;
        /* 整行都是空白 → 跳过 */
        if (*s == '\0') continue;

        /* 提取命令名（第一个空格分隔的 token） */
        char *cmd = next_arg(&s);

        /* ================================================
         * 命令分发
         * ================================================ */

        /* —— exit / quit：保存并退出 —— */
        if (strcmp(cmd, "exit") == 0 || strcmp(cmd, "quit") == 0) {
            printf("Saving and exiting...\n");
            break;                   /* 跳出循环 → 阶段 III */
        }

        /* —— help：打印帮助列表 —— */
        else if (strcmp(cmd, "help") == 0) {
            show_help();
        }

        /* —— format：格式化文件系统（⚠ 清除所有数据！） —— */
        else if (strcmp(cmd, "format") == 0) {
            printf("Formatting file system...\n");
            inode_flush_all();       /* 所有脏 inode 写回磁盘 */
            fs_shutdown();           /* 关闭宿主文件 */
            if (fs_format() != 0) {
                fprintf(stderr, "Format failed!\n");
            } else {
                user_init();         /* 重新初始化用户表 */
                printf("Format complete. All data erased.\n");
            }
        }

        /* —— login：用户登录 —— */
        else if (strcmp(cmd, "login") == 0) {
            char *username = next_arg(&s);
            char *password = next_arg(&s);
            if (!username) {
                fprintf(stderr, "Usage: login <username> <password>\n");
            } else {
                if (!password) password = "";  /* 密码可选 */
                fs_login(username, password);
            }
        }

        /* —— logout：用户登出 —— */
        else if (strcmp(cmd, "logout") == 0) {
            fs_logout();
        }

        /* —— create：创建新文件 ——
         * 模式可选：r（只读）、rw（读写）、rwx（读写执行）
         * 默认 = DEFAULT_FILE_MODE（rw-r--r--）
         * 简化：含 'w' 加写权限，含 'x' 加执行权限 */
        else if (strcmp(cmd, "create") == 0) {
            char *path = next_arg(&s);
            if (!path) {
                fprintf(stderr, "Usage: create <path> [r|rw|rwx]\n");
            } else {
                char *mode_str = next_arg(&s);
                uint16_t mode = DEFAULT_FILE_MODE;
                if (mode_str) {
                    /* 检测权限字母 */
                    if (strchr(mode_str, 'w')) mode |= PERM_IWUSR;
                    if (strchr(mode_str, 'x')) mode |= PERM_IXUSR;
                }
                fs_create(path, mode);
            }
        }

        /* —— open：打开文件 ——
         * 模式：r（只读）、w（只写+截断）、a（追加）、rw（读写）
         * 默认 O_RDONLY */
        else if (strcmp(cmd, "open") == 0) {
            char *path = next_arg(&s);
            char *mode_str = next_arg(&s);
            if (!path) {
                fprintf(stderr, "Usage: open <path> <r|w|rw>\n");
            } else {
                uint16_t flags = parse_mode(mode_str);
                if (flags == 0) flags = O_RDONLY;  /* 无法识别 → 默认只读 */
                fs_open(path, flags);
            }
        }

        /* —— close：关闭文件描述符 —— */
        else if (strcmp(cmd, "close") == 0) {
            char *fd_str = next_arg(&s);
            int fd = parse_fd(fd_str);
            if (fd < 0) {
                fprintf(stderr, "Usage: close <fd>\n");
            } else {
                fs_close(fd);
            }
        }

        /* —— read：读取文件内容 ——
         * 分配 size+1 字节缓冲区，读完后补 '\0' 以便 %s 输出 */
        else if (strcmp(cmd, "read") == 0) {
            char *fd_str = next_arg(&s);
            char *size_str = next_arg(&s);
            int fd = parse_fd(fd_str);
            if (fd < 0 || !size_str) {
                fprintf(stderr, "Usage: read <fd> <size>\n");
            } else {
                uint32_t size = (uint32_t)atoi(size_str);
                char *buf = (char *)malloc(size + 1);  /* +1 给 '\0' */
                if (buf) {
                    int n = fs_read(fd, buf, size);
                    if (n > 0) {
                        buf[n] = '\0';               /* 截断用于打印 */
                        printf("Read %d bytes: \"%s\"\n", n, buf);
                    } else {
                        printf("Read 0 bytes.\n");
                    }
                    free(buf);                       /* 释放临时缓冲区 */
                }
            }
        }

        /* —— write：写入文件内容 ——
         * 命令名和 fd 之后的剩余部分全部作为写入文本 */
        else if (strcmp(cmd, "write") == 0) {
            char *fd_str = next_arg(&s);
            int fd = parse_fd(fd_str);
            if (fd < 0) {
                fprintf(stderr, "Usage: write <fd> <text>\n");
            } else {
                char *text = s;  /* 剩余部分 = 写入内容 */
                if (!text || *text == '\0') {
                    fprintf(stderr, "Usage: write <fd> <text>\n");
                } else {
                    fs_write(fd, text, (uint32_t)strlen(text));
                }
            }
        }

        /* —— seek：移动文件读写指针 ——
         * 相当于 Unix lseek(fd, offset, SEEK_SET) */
        else if (strcmp(cmd, "seek") == 0) {
            char *fd_str = next_arg(&s);
            char *off_str = next_arg(&s);
            int fd = parse_fd(fd_str);
            if (fd < 0 || !off_str) {
                fprintf(stderr, "Usage: seek <fd> <offset>\n");
            } else {
                fs_lseek(fd, atoi(off_str), 0);  /* whence = SEEK_SET */
            }
        }

        /* —— delete / rm：删除文件 —— */
        else if (strcmp(cmd, "delete") == 0 || strcmp(cmd, "rm") == 0) {
            char *path = next_arg(&s);
            if (!path) {
                fprintf(stderr, "Usage: delete <path>\n");
            } else {
                fs_delete(path);
            }
        }

        /* —— mkdir：创建目录 —— */
        else if (strcmp(cmd, "mkdir") == 0) {
            char *path = next_arg(&s);
            if (!path) {
                fprintf(stderr, "Usage: mkdir <path>\n");
            } else {
                fs_mkdir(path);
            }
        }

        /* —— chdir / cd：切换当前工作目录 —— */
        else if (strcmp(cmd, "chdir") == 0 || strcmp(cmd, "cd") == 0) {
            char *path = next_arg(&s);
            if (!path) {
                fprintf(stderr, "Usage: chdir <path>\n");
            } else {
                fs_chdir(path);
            }
        }

        /* —— dir / ls：列出目录内容 ——
         * path 可为 NULL，fs_dir 内部用当前目录 */
        else if (strcmp(cmd, "dir") == 0 || strcmp(cmd, "ls") == 0) {
            char *path = next_arg(&s);
            fs_dir(path);  /* NULL → 当前目录 */
        }

        /* —— 未知命令 —— */
        else {
            printf("Unknown command: %s (type 'help' for help)\n", cmd);
        }
    }

    /* ——— 阶段 III：安全退出 ———
     *
     * 按顺序执行：
     *   1. inode_flush_all() — 将所有脏 inode 写回磁盘
     *   2. sb_save()         — 将超级块（含空闲栈）写回块 0
     *   3. fs_shutdown()     — 关闭宿主文件句柄
     *
     * 顺序很重要：先文件数据 → 后元数据（超级块） → 最后关闭文件 */
    inode_flush_all();
    sb_save();
    fs_shutdown();
    printf("Goodbye.\n");
    return 0;
}
