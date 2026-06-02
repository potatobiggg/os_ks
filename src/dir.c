#include <stdio.h>
#include <string.h>
#include "dir.h"
#include "inode.h"
#include "user.h"
#include "super.h"
#include "persist.h"
#include "globals.h"

int dir_add_entry(MInode *dp, const char *name, uint16_t ino)
{
    if (!(dp->mi_dinode.di_mode & FT_DIR)) return -1;

    int offset = iname(dp);
    if (offset < 0) {
        fprintf(stderr, "dir_add_entry: directory full or error\n");
        return -1;
    }

    /* Determine which logical block holds this offset */
    uint32_t logical_block = offset / BLOCK_SIZE;
    uint32_t off_in_block  = offset % BLOCK_SIZE;

    uint16_t phys = bmap(dp, logical_block, 1);
    if (phys == 0) return -1;

    char buf[BLOCK_SIZE];
    bread(phys, buf);

    DirEntry *de = (DirEntry *)(buf + off_in_block);
    memset(de, 0, DIR_ENTRY_SIZE);
    strncpy(de->de_name, name, DIR_ENTRY_NAME_LEN);
    de->de_name[DIR_ENTRY_NAME_LEN - 1] = '\0';
    de->de_ino = ino;

    bwrite(phys, buf);

    /* Update directory size if we extended */
    if ((uint32_t)(offset + DIR_ENTRY_SIZE) > dp->mi_dinode.di_size) {
        dp->mi_dinode.di_size = offset + DIR_ENTRY_SIZE;
    }
    dp->mi_flag |= IUPD;

    return 0;
}

int dir_remove_entry(MInode *dp, const char *name)
{
    if (!(dp->mi_dinode.di_mode & FT_DIR)) return -1;

    uint32_t size = dp->mi_dinode.di_size;
    uint32_t pos = 0;
    char buf[BLOCK_SIZE];

    while (pos < size) {
        uint32_t logical_block = pos / BLOCK_SIZE;
        uint32_t off_in_block  = pos % BLOCK_SIZE;

        uint16_t phys = bmap(dp, logical_block, 0);
        if (phys == 0) {
            pos += BLOCK_SIZE - off_in_block;
            continue;
        }

        bread(phys, buf);

        while (off_in_block + DIR_ENTRY_SIZE <= BLOCK_SIZE && pos < size) {
            DirEntry *de = (DirEntry *)(buf + off_in_block);

            if (de->de_ino != 0 &&
                strncmp(de->de_name, name, DIR_ENTRY_NAME_LEN) == 0) {
                de->de_ino = 0;
                memset(de->de_name, 0, DIR_ENTRY_NAME_LEN);
                bwrite(phys, buf);
                dp->mi_flag |= IUPD;
                return 0;
            }

            off_in_block += DIR_ENTRY_SIZE;
            pos += DIR_ENTRY_SIZE;
        }
    }

    return -1;
}

int dir_is_empty(MInode *dp)
{
    if (!(dp->mi_dinode.di_mode & FT_DIR)) return -1;

    uint32_t size = dp->mi_dinode.di_size;
    uint32_t pos = 0;
    char buf[BLOCK_SIZE];
    int count = 0;

    while (pos < size) {
        uint32_t logical_block = pos / BLOCK_SIZE;
        uint32_t off_in_block  = pos % BLOCK_SIZE;

        uint16_t phys = bmap(dp, logical_block, 0);
        if (phys == 0) {
            pos += BLOCK_SIZE - off_in_block;
            continue;
        }

        bread(phys, buf);

        while (off_in_block + DIR_ENTRY_SIZE <= BLOCK_SIZE && pos < size) {
            DirEntry *de = (DirEntry *)(buf + off_in_block);
            if (de->de_ino != 0) {
                /* Skip "." and ".." */
                if (strcmp(de->de_name, ".") != 0 &&
                    strcmp(de->de_name, "..") != 0) {
                    return 0;  /* not empty */
                }
            }
            off_in_block += DIR_ENTRY_SIZE;
            pos += DIR_ENTRY_SIZE;
            count++;
        }
    }

    return 1;  /* empty */
}

int fs_mkdir(const char *path)
{
    User *u = get_current_user();
    if (!u) { fprintf(stderr, "mkdir: not logged in\n"); return -1; }

    char basename[DIR_ENTRY_NAME_LEN + 1];
    MInode *dp = namei_parent(path, basename);
    if (!dp) { fprintf(stderr, "mkdir: invalid parent path\n"); return -1; }
    if (basename[0] == '\0') {
        iput(dp);
        fprintf(stderr, "mkdir: missing directory name\n");
        return -1;
    }

    /* Check if already exists */
    MInode *existing = dir_lookup(dp, basename);
    if (existing) {
        iput(existing); iput(dp);
        fprintf(stderr, "mkdir: '%s' already exists\n", basename);
        return -1;
    }

    /* Allocate a new inode */
    uint16_t new_ino = ialloc();
    if (new_ino == 0) { iput(dp); return -1; }

    /* Allocate a data block for the directory entries */
    uint16_t dir_blk = sb_balloc();
    if (dir_blk == 0) { ifree(new_ino); iput(dp); return -1; }

    /* Initialize the new directory inode */
    MInode *nip = iget(new_ino);
    if (!nip) { sb_bfree(dir_blk); ifree(new_ino); iput(dp); return -1; }

    nip->mi_dinode.di_mode  = DEFAULT_DIR_MODE;
    nip->mi_dinode.di_nlink = 2;
    nip->mi_dinode.di_uid   = u->u_uid;
    nip->mi_dinode.di_gid   = u->u_gid;
    nip->mi_dinode.di_size  = 2 * DIR_ENTRY_SIZE;
    nip->mi_dinode.di_addr[0] = dir_blk;
    nip->mi_flag |= IUPD;

    /* Create "." and ".." entries */
    DirEntry dir_buf[DIR_ENTRIES_PER_BLOCK];
    memset(dir_buf, 0, sizeof(dir_buf));
    strncpy(dir_buf[0].de_name, ".", DIR_ENTRY_NAME_LEN);
    dir_buf[0].de_ino = new_ino;
    strncpy(dir_buf[1].de_name, "..", DIR_ENTRY_NAME_LEN);
    dir_buf[1].de_ino = dp->mi_ino;
    bwrite(dir_blk, dir_buf);

    /* Add entry in parent directory */
    dir_add_entry(dp, basename, new_ino);
    dp->mi_dinode.di_nlink++;
    dp->mi_flag |= IUPD;

    iput(nip);
    iput(dp);

    printf("mkdir: created directory '%s'\n", basename);
    return 0;
}

int fs_chdir(const char *path)
{
    User *u = get_current_user();
    if (!u) { fprintf(stderr, "chdir: not logged in\n"); return -1; }

    MInode *ip = namei(path);
    if (!ip) { fprintf(stderr, "chdir: '%s' not found\n", path); return -1; }

    if (!(ip->mi_dinode.di_mode & FT_DIR)) {
        iput(ip);
        fprintf(stderr, "chdir: '%s' is not a directory\n", path);
        return -1;
    }

    /* Check execute (traverse) permission */
    if (!access_check(ip, u->u_uid, 0x04)) {
        iput(ip);
        fprintf(stderr, "chdir: permission denied for '%s'\n", path);
        return -1;
    }

    /* Update current directory */
    u->u_cwd_ino = ip->mi_ino;

    /* Build new path string */
    if (path[0] == '/') {
        strncpy(u->u_cwd_path, path, MAX_PATH_LEN - 1);
        u->u_cwd_path[MAX_PATH_LEN - 1] = '\0';
    } else {
        if (strcmp(path, "..") == 0) {
            /* Go up one level: strip last component from cwd_path */
            char *last = strrchr(u->u_cwd_path, '/');
            if (last && last != u->u_cwd_path) {
                *last = '\0';
            } else if (last) {
                *(last + 1) = '\0';  /* keep root "/" */
            }
        } else if (strcmp(path, ".") == 0) {
            /* No change */
        } else {
            /* Append to current path */
            if (strcmp(u->u_cwd_path, "/") != 0) {
                strncat(u->u_cwd_path, "/", MAX_PATH_LEN - strlen(u->u_cwd_path) - 1);
            }
            strncat(u->u_cwd_path, path, MAX_PATH_LEN - strlen(u->u_cwd_path) - 1);
        }
    }

    iput(ip);
    printf("chdir: current directory is '%s'\n", u->u_cwd_path);
    return 0;
}

int fs_dir(const char *path)
{
    User *u = get_current_user();
    if (!u) { fprintf(stderr, "dir: not logged in\n"); return -1; }

    const char *target = path ? path : u->u_cwd_path;

    MInode *ip;
    if (target[0] == '\0') {
        ip = iget(u->u_cwd_ino);
    } else {
        ip = namei(target);
    }

    if (!ip) { fprintf(stderr, "dir: '%s' not found\n", target); return -1; }
    if (!(ip->mi_dinode.di_mode & FT_DIR)) {
        iput(ip);
        fprintf(stderr, "dir: '%s' is not a directory\n", target);
        return -1;
    }

    /* Check read permission on the directory */
    if (!access_check(ip, u->u_uid, O_RDONLY)) {
        iput(ip);
        fprintf(stderr, "dir: permission denied for '%s'\n", target);
        return -1;
    }

    printf("\nDirectory: %s\n", target);
    printf("%-16s %-8s %-8s %-10s\n", "Name", "Inode", "Size", "Type");
    printf("--------------------------------------------------\n");

    uint32_t size = ip->mi_dinode.di_size;
    uint32_t pos = 0;
    char buf[BLOCK_SIZE];

    while (pos < size) {
        uint32_t logical_block = pos / BLOCK_SIZE;
        uint32_t off_in_block  = pos % BLOCK_SIZE;

        uint16_t phys = bmap(ip, logical_block, 0);
        if (phys == 0) {
            pos += BLOCK_SIZE - off_in_block;
            continue;
        }

        bread(phys, buf);

        while (off_in_block + DIR_ENTRY_SIZE <= BLOCK_SIZE && pos < size) {
            DirEntry *de = (DirEntry *)(buf + off_in_block);
            if (de->de_ino != 0) {
                MInode *tip = iget(de->de_ino);
                const char *type = (tip->mi_dinode.di_mode & FT_DIR) ? "DIR" : "FILE";
                printf("%-16s %-8u %-8u %-10s\n",
                       de->de_name, de->de_ino,
                       tip->mi_dinode.di_size, type);
                iput(tip);
            }
            off_in_block += DIR_ENTRY_SIZE;
            pos += DIR_ENTRY_SIZE;
        }
    }

    iput(ip);
    return 0;
}
