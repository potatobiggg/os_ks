#ifndef FILE_H
#define FILE_H

#include <stdint.h>

/* Create a new regular file at `path` with given mode. Returns 0 on success. */
int fs_create(const char *path, uint16_t mode);

/* Open a file at `path` with `flags`. Returns fd (>=0) or -1 on error. */
int fs_open(const char *path, uint16_t flags);

/* Close an open file by fd. Returns 0 on success, -1 on error. */
int fs_close(int fd);

/* Read up to `count` bytes from file at current offset. Returns bytes read. */
int fs_read(int fd, char *buf, uint32_t count);

/* Write up to `count` bytes to file at current offset. Returns bytes written. */
int fs_write(int fd, const char *buf, uint32_t count);

/* Reposition file offset. `whence`: 0=SET, 1=CUR, 2=END. */
int fs_lseek(int fd, int32_t offset, int whence);

/* Delete a file at `path`. Returns 0 on success. */
int fs_delete(const char *path);

#endif /* FILE_H */
