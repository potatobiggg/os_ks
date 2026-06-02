#include <stdio.h>
#include <string.h>
#include "user.h"
#include "inode.h"
#include "dir.h"
#include "super.h"
#include "persist.h"
#include "globals.h"

void user_init(void)
{
    g_user_count = 0;
    g_current_user_idx = -1;

    /* Create 8 default users: usr1 ~ usr8, each with password "123" */
    for (int i = 0; i < 8; i++) {
        memset(&g_users[i], 0, sizeof(User));
        snprintf(g_users[i].u_name, USRNAME_LEN, "usr%d", i + 1);
        snprintf(g_users[i].u_password, PASSWORD_LEN, "123");
        g_users[i].u_uid  = i + 1;
        g_users[i].u_gid  = 1;
        g_users[i].u_utype = 0;
        g_users[i].u_home_ino = 0;     /* not created yet */
        g_users[i].u_cwd_ino = ROOT_INO;
        strncpy(g_users[i].u_cwd_path, "/", MAX_PATH_LEN);
        g_users[i].u_logged_in = 0;
        g_user_count++;
    }
}

int fs_login(const char *username, const char *password)
{
    if (g_current_user_idx >= 0) {
        fprintf(stderr, "login: user '%s' is already logged in. Use logout first.\n",
                g_users[g_current_user_idx].u_name);
        return -1;
    }

    for (int i = 0; i < g_user_count; i++) {
        if (strcmp(g_users[i].u_name, username) == 0) {
            if (g_users[i].u_logged_in) {
                fprintf(stderr, "login: user '%s' is already logged in.\n", username);
                return -1;
            }

            /* Check password */
            if (strcmp(g_users[i].u_password, password) != 0) {
                fprintf(stderr, "login: incorrect password for '%s'\n", username);
                return -1;
            }

            /* Auto-create home directory if not yet created */
            if (g_users[i].u_home_ino == 0) {
                char home_path[MAX_PATH_LEN];
                snprintf(home_path, MAX_PATH_LEN, "/%s", username);

                /* Check if home dir already exists on disk (from previous session) */
                MInode *existing = namei(home_path);
                if (existing) {
                    /* Already exists: just record its inode number */
                    g_users[i].u_home_ino = existing->mi_ino;
                    iput(existing);
                } else {
                    /* Create home directory with restricted permissions (0700) */
                    uint16_t home_ino = ialloc();
                    if (home_ino == 0) {
                        fprintf(stderr, "login: failed to allocate inode for home dir\n");
                        return -1;
                    }

                    uint16_t dir_blk = sb_balloc();
                    if (dir_blk == 0) {
                        ifree(home_ino);
                        fprintf(stderr, "login: failed to allocate block for home dir\n");
                        return -1;
                    }

                    /* Set up home directory inode (owner-only: rwx------) */
                    MInode *hip = iget(home_ino);
                    hip->mi_dinode.di_mode  = FT_DIR | PERM_IRUSR | PERM_IWUSR | PERM_IXUSR;
                    hip->mi_dinode.di_nlink = 2;
                    hip->mi_dinode.di_uid   = g_users[i].u_uid;
                    hip->mi_dinode.di_gid   = g_users[i].u_gid;
                    hip->mi_dinode.di_size  = 2 * DIR_ENTRY_SIZE;
                    hip->mi_dinode.di_addr[0] = dir_blk;
                    hip->mi_flag |= IUPD;

                    /* Create "." and ".." entries */
                    DirEntry dir_buf[DIR_ENTRIES_PER_BLOCK];
                    memset(dir_buf, 0, sizeof(dir_buf));
                    strncpy(dir_buf[0].de_name, ".", DIR_ENTRY_NAME_LEN);
                    dir_buf[0].de_ino = home_ino;
                    strncpy(dir_buf[1].de_name, "..", DIR_ENTRY_NAME_LEN);
                    dir_buf[1].de_ino = ROOT_INO;
                    bwrite(dir_blk, dir_buf);

                    iput(hip);

                    /* Add entry in root directory */
                    MInode *root = iget(ROOT_INO);
                    dir_add_entry(root, username, home_ino);
                    root->mi_dinode.di_nlink++;
                    root->mi_flag |= IUPD;
                    iput(root);

                    g_users[i].u_home_ino = home_ino;
                    printf("login: created home directory '/%s'\n", username);
                }
            }

            /* Set current directory to home */
            g_users[i].u_cwd_ino = g_users[i].u_home_ino;
            snprintf(g_users[i].u_cwd_path, MAX_PATH_LEN, "/%s", username);

            g_users[i].u_logged_in = 1;
            g_current_user_idx = i;
            printf("Welcome, %s! Current directory: %s\n",
                   g_users[i].u_name, g_users[i].u_cwd_path);
            return i;
        }
    }

    fprintf(stderr, "login: user '%s' not found\n", username);
    return -1;
}

int fs_logout(void)
{
    if (g_current_user_idx < 0) {
        fprintf(stderr, "logout: no user is logged in\n");
        return -1;
    }

    User *u = &g_users[g_current_user_idx];

    /* Close all open files */
    for (int fd = 0; fd < MAX_OPEN_FILES; fd++) {
        if (u->u_ofile[fd].o_inode) {
            u->u_ofile[fd].o_inode->mi_count--;
            u->u_ofile[fd].o_inode = NULL;
            u->u_ofile[fd].o_count = 0;
        }
    }

    printf("Goodbye, %s!\n", u->u_name);
    u->u_logged_in = 0;
    g_current_user_idx = -1;
    return 0;
}

User *get_current_user(void)
{
    if (g_current_user_idx < 0) return NULL;
    return &g_users[g_current_user_idx];
}

OpenFile *user_get_file(int fd)
{
    User *u = get_current_user();
    if (!u || fd < 0 || fd >= MAX_OPEN_FILES) return NULL;
    if (u->u_ofile[fd].o_inode == NULL) return NULL;
    return &u->u_ofile[fd];
}

int user_alloc_fd(void)
{
    User *u = get_current_user();
    if (!u) return -1;

    for (int fd = 0; fd < MAX_OPEN_FILES; fd++) {
        if (u->u_ofile[fd].o_inode == NULL) {
            return fd;
        }
    }
    return -1;
}

void user_free_fd(int fd)
{
    User *u = get_current_user();
    if (!u || fd < 0 || fd >= MAX_OPEN_FILES) return;
    memset(&u->u_ofile[fd], 0, sizeof(OpenFile));
}
