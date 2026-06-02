#ifndef CONFIG_H
#define CONFIG_H

/* ===== Disk Layout ===== */
#define BLOCK_SIZE          512
#define SUPER_BLOCK_NO      1
#define INODE_AREA_START    2
#define INODE_AREA_BLOCKS   32
#define DATA_AREA_START     34
#define DATA_AREA_BLOCKS    512
#define TOTAL_BLOCKS        546

/* ===== Inode ===== */
#define INODES_PER_BLOCK    (BLOCK_SIZE / DISK_INODE_SIZE)
#define TOTAL_INODES        (INODE_AREA_BLOCKS * INODES_PER_BLOCK)  /* 512 */
#define DISK_INODE_SIZE     32
#define DIRECT_BLOCKS       8       /* addr[0..7] */
#define INDIRECT_INDEX      8       /* addr[8] single indirect */
#define DOUBLE_INDIRECT_INDEX 9     /* addr[9] double indirect */
#define ADDRS_PER_BLOCK     (BLOCK_SIZE / sizeof(uint16_t))  /* 256 */

/* Indirect block ranges */
#define SMALL_FILE_BLOCKS   DIRECT_BLOCKS
#define SINGLE_INDIRECT_BLOCKS (ADDRS_PER_BLOCK)
#define DOUBLE_INDIRECT_BLOCKS (ADDRS_PER_BLOCK * ADDRS_PER_BLOCK)

/* ===== Group Linking ===== */
#define GROUP_SIZE          50
#define FREE_BLOCK_STACK_SIZE 50
#define FREE_INODE_STACK_SIZE 50

/* ===== Directory ===== */
#define DIR_ENTRY_NAME_LEN  14
#define DIR_ENTRY_SIZE      16      /* 14 name + 2 inode */
#define DIR_MAX_ENTRIES     128
#define DIR_ENTRIES_PER_BLOCK (BLOCK_SIZE / DIR_ENTRY_SIZE)  /* 32 */

/* ===== Users ===== */
#define MAX_USERS           10
#define MAX_OPEN_FILES      20
#define USRNAME_LEN         8
#define PASSWORD_LEN        12

/* ===== Inode Cache ===== */
#define HASH_BUCKETS        128

/* ===== Path ===== */
#define MAX_PATH_LEN        256
#define MAX_FILENAME_LEN    14

/* ===== System Limits ===== */
#define SYS_OPEN_MAX        40

#endif /* CONFIG_H */
