#ifndef DIR_H
#define DIR_H

#include <stdint.h>
#include "types.h"

/* Create a directory at `path`. */
int fs_mkdir(const char *path);

/* Change current working directory to `path`. */
int fs_chdir(const char *path);

/* List directory contents of `path` (or current dir if NULL). */
int fs_dir(const char *path);

/* Add a directory entry (name, ino) to directory inode `dp`. */
int dir_add_entry(MInode *dp, const char *name, uint16_t ino);

/* Remove a directory entry by name from directory inode `dp`. */
int dir_remove_entry(MInode *dp, const char *name);

/* Check if a directory is empty (only has "." and ".." entries). */
int dir_is_empty(MInode *dp);

#endif /* DIR_H */
