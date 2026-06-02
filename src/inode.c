#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "inode.h"
#include "super.h"
#include "persist.h"
#include "user.h"
#include "globals.h"

/* ====================================================
 * HASH TABLE
 * ==================================================== */

static uint16_t hash_ino(uint16_t ino)
{
    return ino & (HASH_BUCKETS - 1);
}

void inode_hash_init(void)
{
    for (int i = 0; i < HASH_BUCKETS; i++) {
        g_inode_hash[i] = NULL;
    }
}

/* Allocate a new MInode struct from heap */
static MInode *mialloc(void)
{
    MInode *ip = (MInode *)malloc(sizeof(MInode));
    if (ip) {
        memset(ip, 0, sizeof(MInode));
    }
    return ip;
}

/* Insert MInode into hash table */
static void hash_insert(MInode *ip)
{
    uint16_t h = hash_ino(ip->mi_ino);
    ip->mi_next = g_inode_hash[h];
    ip->mi_prev = NULL;
    if (g_inode_hash[h]) {
        g_inode_hash[h]->mi_prev = ip;
    }
    g_inode_hash[h] = ip;
}

/* Remove MInode from hash table */
static void hash_remove(MInode *ip)
{
    uint16_t h = hash_ino(ip->mi_ino);
    if (ip->mi_prev) {
        ip->mi_prev->mi_next = ip->mi_next;
    } else {
        g_inode_hash[h] = ip->mi_next;
    }
    if (ip->mi_next) {
        ip->mi_next->mi_prev = ip->mi_prev;
    }
}

/* Find MInode in hash table by ino */
static MInode *hash_find(uint16_t ino)
{
    uint16_t h = hash_ino(ino);
    MInode *ip = g_inode_hash[h];
    while (ip) {
        if (ip->mi_ino == ino) return ip;
        ip = ip->mi_next;
    }
    return NULL;
}

/* ====================================================
 * DISK INODE READ / WRITE
 * ==================================================== */

static void read_disk_inode(uint16_t ino, DiskInode *dip)
{
    uint16_t blk = INODE_AREA_START + (ino - 1) / INODES_PER_BLOCK;
    uint16_t off = ((ino - 1) % INODES_PER_BLOCK) * DISK_INODE_SIZE;
    char buf[BLOCK_SIZE];
    bread(blk, buf);
    memcpy(dip, buf + off, DISK_INODE_SIZE);
}

static void write_disk_inode(uint16_t ino, const DiskInode *dip)
{
    uint16_t blk = INODE_AREA_START + (ino - 1) / INODES_PER_BLOCK;
    uint16_t off = ((ino - 1) % INODES_PER_BLOCK) * DISK_INODE_SIZE;
    char buf[BLOCK_SIZE];
    bread(blk, buf);
    memcpy(buf + off, dip, DISK_INODE_SIZE);
    bwrite(blk, buf);
}

/* ====================================================
 * CORE INODE OPERATIONS
 * ==================================================== */

uint16_t ialloc(void)
{
    uint16_t ino = sb_ialloc();
    if (ino == 0) return 0;

    /* Clear the disk inode */
    DiskInode di;
    memset(&di, 0, sizeof(di));
    write_disk_inode(ino, &di);
    return ino;
}

void ifree(uint16_t ino)
{
    /* First, free all blocks associated with this inode */
    MInode *ip = iget(ino);
    if (ip) {
        free_all_blocks(ip);
        iput(ip);
    }

    /* Return inode number to free pool */
    sb_ifree(ino);
}

MInode *iget(uint16_t ino)
{
    if (ino == 0 || ino > TOTAL_INODES) return NULL;

    /* Check if already in hash table */
    MInode *ip = hash_find(ino);
    if (ip) {
        ip->mi_count++;
        return ip;
    }

    /* Allocate new memory inode */
    ip = mialloc();
    if (!ip) return NULL;

    ip->mi_ino = ino;
    ip->mi_count = 1;
    ip->mi_flag = 0;

    /* Read from disk */
    read_disk_inode(ino, &ip->mi_dinode);

    /* Insert into hash */
    hash_insert(ip);

    return ip;
}

void iupdat(MInode *ip)
{
    if (ip->mi_flag & IUPD) {
        write_disk_inode(ip->mi_ino, &ip->mi_dinode);
        ip->mi_flag &= ~IUPD;
    }
}

void iput(MInode *ip)
{
    if (!ip) return;

    ip->mi_count--;
    if (ip->mi_count > 0) return;

    /* No more references: write back if dirty, then free */
    iupdat(ip);
    hash_remove(ip);
    free(ip);
}

void inode_flush_all(void)
{
    for (int i = 0; i < HASH_BUCKETS; i++) {
        MInode *ip = g_inode_hash[i];
        while (ip) {
            iupdat(ip);
            ip = ip->mi_next;
        }
    }
}

/* ====================================================
 * PERMISSION CHECK
 * ==================================================== */

int access_check(MInode *ip, uint16_t uid, int op)
{
    uint16_t mode = ip->mi_dinode.di_mode;
    uint16_t owner = ip->mi_dinode.di_uid;

    /* Root user (uid 0) always has access */
    if (uid == 0) return 1;

    int need_read  = (op == O_RDONLY || op == O_RDWR);
    int need_write = (op == O_WRONLY || op == O_RDWR);
    int need_exec  = (op == 0x04);

    if (owner == uid) {
        if (need_read  && !(mode & PERM_IRUSR)) return 0;
        if (need_write && !(mode & PERM_IWUSR)) return 0;
        if (need_exec  && !(mode & PERM_IXUSR)) return 0;
        return 1;
    }

    /* Group or other: simplified to "other" for this project */
    if (need_read  && !(mode & PERM_IROTH)) return 0;
    if (need_write && !(mode & PERM_IWOTH)) return 0;
    if (need_exec  && !(mode & PERM_IXOTH)) return 0;
    return 1;
}

/* ====================================================
 * DIRECTORY OPERATIONS
 * ==================================================== */

MInode *dir_lookup(MInode *dp, const char *name)
{
    if (!(dp->mi_dinode.di_mode & FT_DIR)) return NULL;

    uint32_t size = dp->mi_dinode.di_size;
    uint32_t pos = 0;
    char buf[BLOCK_SIZE];

    while (pos < size) {
        uint32_t logical_block = pos / BLOCK_SIZE;
        uint32_t offset_in_block = pos % BLOCK_SIZE;

        uint16_t phys = bmap(dp, logical_block, 0);
        if (phys == 0) {
            pos += BLOCK_SIZE - offset_in_block;
            continue;
        }

        bread(phys, buf);

        while (offset_in_block + DIR_ENTRY_SIZE <= BLOCK_SIZE && pos < size) {
            DirEntry *de = (DirEntry *)(buf + offset_in_block);

            if (de->de_ino != 0 &&
                strncmp(de->de_name, name, DIR_ENTRY_NAME_LEN) == 0) {
                return iget(de->de_ino);
            }

            offset_in_block += DIR_ENTRY_SIZE;
            pos += DIR_ENTRY_SIZE;
        }
    }

    return NULL;
}

int iname(MInode *dp)
{
    if (!(dp->mi_dinode.di_mode & FT_DIR)) return -1;

    uint32_t size = dp->mi_dinode.di_size;
    uint32_t pos = 0;
    char buf[BLOCK_SIZE];

    /* Search within existing directory blocks */
    while (pos < size) {
        uint32_t logical_block = pos / BLOCK_SIZE;
        uint32_t offset_in_block = pos % BLOCK_SIZE;

        uint16_t phys = bmap(dp, logical_block, 0);
        if (phys == 0) {
            pos += BLOCK_SIZE - offset_in_block;
            continue;
        }

        bread(phys, buf);

        while (offset_in_block + DIR_ENTRY_SIZE <= BLOCK_SIZE && pos < size) {
            DirEntry *de = (DirEntry *)(buf + offset_in_block);
            if (de->de_ino == 0) {
                return (int)pos;  /* return byte offset */
            }
            offset_in_block += DIR_ENTRY_SIZE;
            pos += DIR_ENTRY_SIZE;
        }
    }

    /* Need to expand directory: try to add a new entry at `size` position.
     * Check that we haven't exceeded DIR_MAX_ENTRIES. */
    if (size / DIR_ENTRY_SIZE >= DIR_MAX_ENTRIES) return -1;

    return (int)size;
}

/* ====================================================
 * PATH RESOLUTION
 * ==================================================== */

MInode *namei(const char *path)
{
    char basename[DIR_ENTRY_NAME_LEN + 1];
    MInode *dp = namei_parent(path, basename);
    if (!dp) return NULL;

    /* If basename is empty, the path itself is the target (e.g. "/" or ".") */
    if (basename[0] == '\0') {
        return dp;  /* dp is the target, already locked & referenced */
    }

    /* Look up the last component in the parent directory */
    MInode *target = dir_lookup(dp, basename);
    iput(dp);
    return target;
}

MInode *namei_parent(const char *path, char *basename)
{
    MInode *dp;

    /* Determine starting point */
    if (path[0] == '/') {
        dp = iget(ROOT_INO);
        if (!dp) return NULL;
        path++;  /* skip leading '/' */
    } else {
        if (g_current_user_idx < 0) {
            fprintf(stderr, "namei: no user logged in\n");
            return NULL;
        }
        User *u = &g_users[g_current_user_idx];
        dp = iget(u->u_cwd_ino);
        if (!dp) return NULL;
    }

    /* Handle empty path or root */
    if (*path == '\0') {
        /* Path is just "/" or "." — return dp as both parent and target.
         * Caller convention: if there's no last component, the caller
         * should use dp directly and set basename to "". */
        basename[0] = '\0';
        return dp;  /* dp is returned locked & referenced */
    }

    /* Tokenize path */
    char path_copy[MAX_PATH_LEN];
    strncpy(path_copy, path, MAX_PATH_LEN - 1);
    path_copy[MAX_PATH_LEN - 1] = '\0';

    char *components[64];
    int ncomp = 0;
    char *token = strtok(path_copy, "/");
    while (token && ncomp < 64) {
        components[ncomp++] = token;
        token = strtok(NULL, "/");
    }

    if (ncomp == 0) {
        basename[0] = '\0';
        return dp;
    }

    /* Traverse all but last component */
    for (int i = 0; i < ncomp - 1; i++) {
        MInode *nip = dir_lookup(dp, components[i]);
        if (!nip) {
            iput(dp);
            return NULL;
        }
        if (!(nip->mi_dinode.di_mode & FT_DIR)) {
            iput(nip);
            iput(dp);
            fprintf(stderr, "namei: '%s' is not a directory\n", components[i]);
            return NULL;
        }

        /* Check traversal permission on intermediate directory */
        {
            User *cu = get_current_user();
            if (cu && cu->u_uid != 0 && !access_check(nip, cu->u_uid, 0x04)) {
                iput(nip);
                iput(dp);
                fprintf(stderr, "namei: permission denied for '%s'\n", components[i]);
                return NULL;
            }
        }
        iput(dp);
        dp = nip;
    }

    /* Return the last component name */
    strncpy(basename, components[ncomp - 1], DIR_ENTRY_NAME_LEN);
    basename[DIR_ENTRY_NAME_LEN] = '\0';

    return dp;  /* dp = parent directory, locked & referenced */
}

/* ====================================================
 * BLOCK MAPPING (Mixed Index)
 * ==================================================== */

/* Get a block address from a single-indirect or double-indirect block.
 * If `alloc` is non-zero and the block doesn't exist, allocate it.
 * `pblk` is a pointer to the address-of-address: the slot that
 * holds the indirect block number (or 0). On allocation, *pblk is updated. */
static uint16_t get_indirect_block(uint16_t *pblk, uint32_t index, int alloc)
{
    /* If the indirect block itself doesn't exist */
    if (*pblk == 0) {
        if (!alloc) return 0;
        *pblk = sb_balloc();
        if (*pblk == 0) return 0;
        /* Zero the newly allocated indirect block */
        uint16_t zeros[256];
        memset(zeros, 0, sizeof(zeros));
        bwrite(*pblk, zeros);
    }

    /* Read the indirect block and get the entry */
    uint16_t table[ADDRS_PER_BLOCK];
    bread(*pblk, table);

    if (table[index] == 0 && alloc) {
        table[index] = sb_balloc();
        if (table[index] == 0) return 0;
        bwrite(*pblk, table);
    }

    return table[index];
}

uint16_t bmap(MInode *ip, uint32_t logical_block, int alloc)
{
    uint16_t *addr = ip->mi_dinode.di_addr;
    uint32_t single_start = DIRECT_BLOCKS;
    uint32_t double_start = single_start + ADDRS_PER_BLOCK;

    if (logical_block < DIRECT_BLOCKS) {
        /* Direct block */
        if (addr[logical_block] == 0 && alloc) {
            addr[logical_block] = sb_balloc();
            ip->mi_flag |= IUPD;
        }
        return addr[logical_block];
    }

    if (logical_block < double_start) {
        /* Single indirect through addr[INDIRECT_INDEX] */
        uint32_t idx = logical_block - single_start;
        uint16_t result = get_indirect_block(&addr[INDIRECT_INDEX], idx, alloc);
        if (alloc && result) ip->mi_flag |= IUPD;
        return result;
    }

    /* Double indirect through addr[DOUBLE_INDIRECT_INDEX] */
    uint32_t idx = logical_block - double_start;
    uint32_t outer = idx / ADDRS_PER_BLOCK;
    uint32_t inner = idx % ADDRS_PER_BLOCK;

    if (addr[DOUBLE_INDIRECT_INDEX] == 0) {
        if (!alloc) return 0;
        addr[DOUBLE_INDIRECT_INDEX] = sb_balloc();
        if (addr[DOUBLE_INDIRECT_INDEX] == 0) return 0;
        uint16_t zeros[256];
        memset(zeros, 0, sizeof(zeros));
        bwrite(addr[DOUBLE_INDIRECT_INDEX], zeros);
        ip->mi_flag |= IUPD;
    }

    /* Read outer indirect block */
    uint16_t outer_table[ADDRS_PER_BLOCK];
    bread(addr[DOUBLE_INDIRECT_INDEX], outer_table);

    uint16_t result = get_indirect_block(&outer_table[outer], inner, alloc);

    if (alloc && result) {
        /* Write back the updated outer table */
        bwrite(addr[DOUBLE_INDIRECT_INDEX], outer_table);
        ip->mi_flag |= IUPD;
    }

    return result;
}

/* Free a list of block addresses from an indirect block */
static void free_indirect_blocks(uint16_t blk, int level)
{
    if (blk == 0) return;

    uint16_t table[ADDRS_PER_BLOCK];
    bread(blk, table);

    if (level == 1) {
        /* Single indirect: entries are data blocks */
        for (unsigned int i = 0; i < ADDRS_PER_BLOCK; i++) {
            if (table[i]) sb_bfree(table[i]);
        }
    } else if (level == 2) {
        /* Double indirect: entries are single-indirect blocks */
        for (unsigned int i = 0; i < ADDRS_PER_BLOCK; i++) {
            if (table[i]) {
                free_indirect_blocks(table[i], 1);
                sb_bfree(table[i]);
            }
        }
    }

    sb_bfree(blk);
}

void free_all_blocks(MInode *ip)
{
    uint16_t *addr = ip->mi_dinode.di_addr;

    /* Free direct blocks */
    for (int i = 0; i < DIRECT_BLOCKS; i++) {
        if (addr[i]) {
            sb_bfree(addr[i]);
            addr[i] = 0;
        }
    }

    /* Free single indirect */
    if (addr[INDIRECT_INDEX]) {
        free_indirect_blocks(addr[INDIRECT_INDEX], 1);
        addr[INDIRECT_INDEX] = 0;
    }

    /* Free double indirect */
    if (addr[DOUBLE_INDIRECT_INDEX]) {
        free_indirect_blocks(addr[DOUBLE_INDIRECT_INDEX], 2);
        addr[DOUBLE_INDIRECT_INDEX] = 0;
    }

    ip->mi_dinode.di_size = 0;
    ip->mi_flag |= IUPD;
}
