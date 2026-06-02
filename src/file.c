#include <stdio.h>
#include <string.h>
#include "file.h"
#include "inode.h"
#include "dir.h"
#include "user.h"
#include "super.h"
#include "persist.h"
#include "globals.h"

int fs_create(const char *path, uint16_t mode)
{
    User *u = get_current_user();
    if (!u) { fprintf(stderr, "create: not logged in\n"); return -1; }

    if (mode == 0) mode = DEFAULT_FILE_MODE;

    char basename[DIR_ENTRY_NAME_LEN + 1];
    MInode *dp = namei_parent(path, basename);
    if (!dp) { fprintf(stderr, "create: invalid parent path\n"); return -1; }
    if (basename[0] == '\0') {
        iput(dp);
        fprintf(stderr, "create: invalid filename\n");
        return -1;
    }

    /* Check if already exists */
    if (dir_lookup(dp, basename)) {
        iput(dp);
        fprintf(stderr, "create: '%s' already exists\n", basename);
        return -1;
    }

    /* Allocate new inode */
    uint16_t new_ino = ialloc();
    if (new_ino == 0) { iput(dp); return -1; }

    /* Set up the inode */
    MInode *nip = iget(new_ino);
    if (!nip) { ifree(new_ino); iput(dp); return -1; }

    nip->mi_dinode.di_mode  = mode;
    nip->mi_dinode.di_nlink = 1;
    nip->mi_dinode.di_uid   = u->u_uid;
    nip->mi_dinode.di_gid   = u->u_gid;
    nip->mi_dinode.di_size  = 0;
    nip->mi_flag |= IUPD;

    /* Add entry in parent directory */
    dir_add_entry(dp, basename, new_ino);

    iput(nip);
    iput(dp);

    printf("create: created file '%s' (inode %u)\n", basename, new_ino);
    return 0;
}

int fs_open(const char *path, uint16_t flags)
{
    User *u = get_current_user();
    if (!u) { fprintf(stderr, "open: not logged in\n"); return -1; }

    if (flags == 0) flags = O_RDONLY;

    MInode *ip = namei(path);
    if (!ip) {
        /* File doesn't exist: create if O_CREAT */
        if (flags & O_CREAT) {
            if (fs_create(path, DEFAULT_FILE_MODE) != 0) return -1;
            ip = namei(path);
            if (!ip) return -1;
        } else {
            fprintf(stderr, "open: '%s' not found\n", path);
            return -1;
        }
    }

    /* Check if it's a directory */
    if (ip->mi_dinode.di_mode & FT_DIR) {
        iput(ip);
        fprintf(stderr, "open: '%s' is a directory\n", path);
        return -1;
    }

    /* Permission check */
    if (!access_check(ip, u->u_uid, flags & (O_RDONLY | O_WRONLY))) {
        iput(ip);
        fprintf(stderr, "open: permission denied\n");
        return -1;
    }

    /* Allocate fd */
    int fd = user_alloc_fd();
    if (fd < 0) { iput(ip); fprintf(stderr, "open: too many open files\n"); return -1; }

    /* Set up open file */
    OpenFile *of = &u->u_ofile[fd];
    of->o_flag   = flags;
    of->o_count  = 1;
    of->o_inode  = ip;

    if (flags & O_TRUNC) {
        free_all_blocks(ip);
    }
    if (flags & O_APPEND) {
        of->o_offset = ip->mi_dinode.di_size;
    } else {
        of->o_offset = 0;
    }

    printf("open: fd=%d, file='%s', size=%u bytes\n",
           fd, path, ip->mi_dinode.di_size);
    return fd;
}

int fs_close(int fd)
{
    User *u = get_current_user();
    if (!u) { fprintf(stderr, "close: not logged in\n"); return -1; }

    OpenFile *of = user_get_file(fd);
    if (!of) { fprintf(stderr, "close: invalid fd %d\n", fd); return -1; }

    /* Write back inode if dirty */
    if (of->o_inode) {
        iupdat(of->o_inode);
        iput(of->o_inode);
        of->o_inode = NULL;
    }

    user_free_fd(fd);
    printf("close: fd=%d closed\n", fd);
    return 0;
}

int fs_read(int fd, char *buf, uint32_t count)
{
    User *u = get_current_user();
    if (!u) { fprintf(stderr, "read: not logged in\n"); return -1; }

    OpenFile *of = user_get_file(fd);
    if (!of) { fprintf(stderr, "read: invalid fd %d\n", fd); return -1; }
    if (!(of->o_flag & O_RDONLY)) {
        fprintf(stderr, "read: file not opened for reading\n");
        return -1;
    }

    MInode *ip = of->o_inode;
    uint32_t offset = of->o_offset;
    uint32_t file_size = ip->mi_dinode.di_size;

    if (offset >= file_size) return 0;
    if (count > file_size - offset) count = file_size - offset;

    uint32_t remaining = count;
    uint32_t bytes_done = 0;
    char blkbuf[BLOCK_SIZE];

    while (remaining > 0) {
        uint32_t logical_block = offset / BLOCK_SIZE;
        uint32_t block_offset  = offset % BLOCK_SIZE;

        uint16_t phys = bmap(ip, logical_block, 0);
        if (phys == 0) break;

        bread(phys, blkbuf);

        uint32_t chunk = BLOCK_SIZE - block_offset;
        if (chunk > remaining) chunk = remaining;

        memcpy(buf + bytes_done, blkbuf + block_offset, chunk);

        offset += chunk;
        bytes_done += chunk;
        remaining -= chunk;
    }

    of->o_offset = offset;
    return (int)bytes_done;
}

int fs_write(int fd, const char *buf, uint32_t count)
{
    User *u = get_current_user();
    if (!u) { fprintf(stderr, "write: not logged in\n"); return -1; }

    OpenFile *of = user_get_file(fd);
    if (!of) { fprintf(stderr, "write: invalid fd %d\n", fd); return -1; }
    if (!(of->o_flag & O_WRONLY)) {
        fprintf(stderr, "write: file not opened for writing\n");
        return -1;
    }

    MInode *ip = of->o_inode;
    uint32_t offset = of->o_offset;
    uint32_t remaining = count;
    uint32_t bytes_done = 0;
    char blkbuf[BLOCK_SIZE];

    while (remaining > 0) {
        uint32_t logical_block = offset / BLOCK_SIZE;
        uint32_t block_offset  = offset % BLOCK_SIZE;

        uint16_t phys = bmap(ip, logical_block, 1);
        if (phys == 0) {
            fprintf(stderr, "write: disk full\n");
            break;
        }

        /* Read-modify-write for partial blocks */
        uint32_t chunk = BLOCK_SIZE - block_offset;
        if (chunk > remaining) chunk = remaining;

        if (chunk < BLOCK_SIZE) {
            bread(phys, blkbuf);
        }
        memcpy(blkbuf + block_offset, buf + bytes_done, chunk);
        bwrite(phys, blkbuf);

        offset += chunk;
        bytes_done += chunk;
        remaining -= chunk;
    }

    /* Update file size */
    if (offset > ip->mi_dinode.di_size) {
        ip->mi_dinode.di_size = offset;
    }

    of->o_offset = offset;
    ip->mi_flag |= IUPD;

    printf("write: wrote %u bytes\n", bytes_done);
    return (int)bytes_done;
}

int fs_lseek(int fd, int32_t offset, int whence)
{
    User *u = get_current_user();
    if (!u) { fprintf(stderr, "lseek: not logged in\n"); return -1; }

    OpenFile *of = user_get_file(fd);
    if (!of) { fprintf(stderr, "lseek: invalid fd %d\n", fd); return -1; }

    MInode *ip = of->o_inode;
    uint32_t new_offset;

    switch (whence) {
    case 0: /* SEEK_SET */
        new_offset = (uint32_t)offset;
        break;
    case 1: /* SEEK_CUR */
        new_offset = of->o_offset + offset;
        break;
    case 2: /* SEEK_END */
        new_offset = ip->mi_dinode.di_size + offset;
        break;
    default:
        return -1;
    }

    of->o_offset = new_offset;
    return (int)new_offset;
}

int fs_delete(const char *path)
{
    User *u = get_current_user();
    if (!u) { fprintf(stderr, "delete: not logged in\n"); return -1; }

    char basename[DIR_ENTRY_NAME_LEN + 1];
    MInode *dp = namei_parent(path, basename);
    if (!dp) { fprintf(stderr, "delete: invalid path\n"); return -1; }
    if (basename[0] == '\0') {
        iput(dp);
        fprintf(stderr, "delete: cannot delete root directory\n");
        return -1;
    }

    MInode *ip = dir_lookup(dp, basename);
    if (!ip) { iput(dp); fprintf(stderr, "delete: '%s' not found\n", basename); return -1; }

    /* Check if it's a non-empty directory */
    if (ip->mi_dinode.di_mode & FT_DIR) {
        if (!dir_is_empty(ip)) {
            iput(ip); iput(dp);
            fprintf(stderr, "delete: directory '%s' is not empty\n", basename);
            return -1;
        }
    }

    /* Permission check */
    if (!access_check(ip, u->u_uid, O_WRONLY)) {
        iput(ip); iput(dp);
        fprintf(stderr, "delete: permission denied\n");
        return -1;
    }

    /* Check if file is currently open (mi_count > iget's initial ref) */
    if (ip->mi_count > 1) {
        fprintf(stderr, "delete: '%s' is currently open\n", basename);
        /* Still allow deletion but warn */
    }

    /* Remove directory entry */
    dir_remove_entry(dp, basename);
    dp->mi_flag |= IUPD;

    /* Free inode (this also frees all blocks) */
    uint16_t ino = ip->mi_ino;
    iput(ip);
    ifree(ino);

    iput(dp);
    printf("delete: '%s' deleted\n", basename);
    return 0;
}
