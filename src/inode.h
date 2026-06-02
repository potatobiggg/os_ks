#ifndef INODE_H
#define INODE_H

#include <stdint.h>
#include "types.h"

/* ----- Hash Table & Cache ----- */
void inode_hash_init(void);
void inode_flush_all(void);

/* ----- Core Inode Operations ----- */

/* Allocate a new disk inode (calls sb_ialloc, initializes it). Returns ino or 0. */
uint16_t ialloc(void);

/* Free a disk inode (calls sb_ifree). */
void ifree(uint16_t ino);

/* Get a locked, referenced memory inode by number. Returns NULL if not found. */
MInode *iget(uint16_t ino);

/* Release a memory inode (decrement ref count, write back if dirty). */
void iput(MInode *ip);

/* Write back a dirty memory inode to disk. */
void iupdat(MInode *ip);

/* ----- Path Resolution ----- */

/* Resolve a pathname to a locked, referenced MInode.
 * Returns NULL if any component is not found. */
MInode *namei(const char *path);

/* Resolve the parent directory of `path`. Returns locked parent MInode.
 * On return, `basename` is filled with the last component name.
 * Returns NULL if the parent path is invalid. */
MInode *namei_parent(const char *path, char *basename);

/* Search directory `dp` for entry with `name`. Returns locked MInode or NULL. */
MInode *dir_lookup(MInode *dp, const char *name);

/* Find an empty directory entry in directory `dp`.
 * Returns byte offset of the empty slot, or -1 if full. */
int iname(MInode *dp);

/* ----- Permission Check ----- */

/* Check if user `uid` has permission `op` on inode `ip`.
 * op: O_RDONLY, O_WRONLY, or 0x04 for execute.
 * Returns 1 if permitted, 0 if denied. */
int access_check(MInode *ip, uint16_t uid, int op);

/* ----- Block Mapping (Mixed Index) ----- */

/* Map a logical block number within a file to a physical block number.
 * If `alloc` is non-zero and the block doesn't exist, allocate it.
 * Returns physical block number, or 0 if not found (or alloc failed). */
uint16_t bmap(MInode *ip, uint32_t logical_block, int alloc);

/* Free all data blocks (including indirect blocks) associated with an inode. */
void free_all_blocks(MInode *ip);

#endif /* INODE_H */
