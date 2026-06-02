#ifndef TYPES_H
#define TYPES_H

#include <stdint.h>
#include "config.h"

/* ===== Disk Inode (exactly 32 bytes) ===== */
typedef struct {
    uint16_t di_mode;           /* file type + access permissions */
    uint16_t di_nlink;          /* link count */
    uint16_t di_uid;            /* owner user ID */
    uint16_t di_gid;            /* owner group ID */
    uint32_t di_size;           /* file size in bytes */
    uint16_t di_addr[10];       /* addr[0..7] direct, addr[8] single-indirect, addr[9] double-indirect */
} DiskInode;

/* ===== Super Block (512 bytes) ===== */
typedef struct {
    /* free inode management */
    uint16_t s_nfree;               /* count of free inode numbers in stack */
    uint16_t s_free_inode[50];      /* free inode number stack */

    /* free block management (group linking) */
    uint16_t s_nfree_block;         /* count of free block numbers in stack */
    uint16_t s_free_block[50];      /* free block number stack */

    /* modification flag */
    uint16_t s_fmod;                /* 0=clean, 1=dirty */

    /* area descriptors */
    uint16_t s_inode_area_start;
    uint16_t s_inode_area_size;
    uint16_t s_data_area_start;
    uint16_t s_data_area_size;
    uint16_t s_total_blocks;

    /* padding to fill 512 bytes */
    uint16_t s_pad[148];   /* padding to 512 bytes */
} SuperBlock;

/* ===== Directory Entry (16 bytes) ===== */
typedef struct {
    char     de_name[DIR_ENTRY_NAME_LEN];  /* null-terminated file name */
    uint16_t de_ino;                        /* inode number (0 = empty) */
} DirEntry;

/* ===== Memory Inode ===== */
typedef struct Minode {
    DiskInode   mi_dinode;        /* the on-disk inode data */
    uint32_t    mi_ino;           /* this inode's number (1-based) */
    uint16_t    mi_count;         /* reference count */
    uint16_t    mi_flag;          /* flags: ILOCKED, IUPD, IACC, IMOUNT */
    struct Minode *mi_next;       /* Hash chain forward */
    struct Minode *mi_prev;       /* Hash chain backward */
} MInode;

/* mi_flag bits */
#define ILOCKED  0x01
#define IUPD     0x02
#define IACC     0x04
#define IMOUNT   0x08

/* ===== Open File ===== */
typedef struct {
    uint16_t o_flag;            /* O_RDONLY, O_WRONLY, O_RDWR */
    uint16_t o_count;           /* reference count */
    MInode  *o_inode;           /* pointer to memory inode */
    uint32_t o_offset;          /* current read/write position */
} OpenFile;

/* open modes */
#define O_RDONLY    0x01
#define O_WRONLY    0x02
#define O_RDWR      0x03    /* O_RDONLY | O_WRONLY */
#define O_CREAT     0x08
#define O_TRUNC     0x10
#define O_APPEND    0x20

/* ===== User ===== */
typedef struct {
    char     u_name[USRNAME_LEN];                  /* login name */
    char     u_password[PASSWORD_LEN];             /* login password */
    uint16_t u_uid;                                /* user ID */
    uint16_t u_gid;                                /* group ID */
    uint16_t u_utype;                              /* user type */
    uint16_t u_home_ino;                           /* home directory inode (0 = not created yet) */
    OpenFile u_ofile[MAX_OPEN_FILES];              /* per-user open file table */
    uint16_t u_cwd_ino;                            /* current working directory inode */
    char     u_cwd_path[MAX_PATH_LEN];             /* current working directory path string */
    int      u_logged_in;                          /* 1=logged in, 0=not */
} User;

/* ===== File Types (di_mode high bits) ===== */
#define FT_REGULAR  0x8000
#define FT_DIR      0x4000
#define FT_BLKDEV   0x2000
#define FT_CHARDEV  0x1000

/* ===== Permission Bits (lower 9 bits of di_mode) ===== */
#define PERM_IRUSR  0x0100  /* owner read */
#define PERM_IWUSR  0x0080  /* owner write */
#define PERM_IXUSR  0x0040  /* owner execute */
#define PERM_IRGRP  0x0020  /* group read */
#define PERM_IWGRP  0x0010  /* group write */
#define PERM_IXGRP  0x0008  /* group execute */
#define PERM_IROTH  0x0004  /* other read */
#define PERM_IWOTH  0x0002  /* other write */
#define PERM_IXOTH  0x0001  /* other execute */

/* Default directory permissions: rwxr-xr-x */
#define DEFAULT_DIR_MODE  (FT_DIR | PERM_IRUSR | PERM_IWUSR | PERM_IXUSR | \
                           PERM_IRGRP | PERM_IXGRP | PERM_IROTH | PERM_IXOTH)

/* Default file permissions: rw-r--r-- */
#define DEFAULT_FILE_MODE (FT_REGULAR | PERM_IRUSR | PERM_IWUSR | \
                           PERM_IRGRP | PERM_IROTH)

/* ===== Compile-time assertions ===== */
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
_Static_assert(sizeof(DiskInode) == DISK_INODE_SIZE,
               "DiskInode must be exactly 32 bytes");
_Static_assert(sizeof(SuperBlock) == BLOCK_SIZE,
               "SuperBlock must be exactly 512 bytes");
_Static_assert(sizeof(DirEntry) == DIR_ENTRY_SIZE,
               "DirEntry must be exactly 16 bytes");
#endif

#endif /* TYPES_H */
