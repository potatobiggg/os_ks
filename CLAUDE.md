# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is a **UNIX-like Virtual File System (VFS)** implemented in C as an OS course design project. It simulates a real file system persisted in a single `.dat` host file, using on-disk structures modeled after UNIX System V (super block, inodes, mixed indexing, group-linked free block lists).

## Build & Run

```bash
# Build
make                    # gcc -Wall -Wextra -std=c11 -g -O0
# or directly:
gcc -std=c11 -g -O0 -o vfs.exe src/*.c

# Run (auto-creates filesystem.dat if not present)
./vfs.exe filesystem.dat

# Build with AddressSanitizer for debugging
make debug

# Clean build artifacts
make clean
```

## Architecture

### Disk Layout (512-byte blocks)

| Block | Purpose |
|-------|---------|
| 0 | Unused |
| 1 | Super Block |
| 2-33 | Inode Area (32 blocks, 16 inodes/block = 512 inodes) |
| 34-545 | Data Area (512 blocks) |

Total: 546 blocks.

### Key Data Structures (`src/types.h`)

- **DiskInode** (32 bytes): mode, nlink, uid, gid, size, addr[10] — 8 direct + 1 single-indirect + 1 double-indirect
- **SuperBlock** (512 bytes): free inode/block stacks, area descriptors, dirty flag
- **DirEntry** (16 bytes): 14-byte name + 2-byte inode number
- **MInode**: in-memory inode with hash table for caching (ILOCKED, IUPD, IACC flags)
- **OpenFile**: per-user open file table with fd, flags, offset, inode pointer
- **User**: 10 users (8 active: usr1-usr8), password "123", per-user open file table

### Free Space Management

- **Inodes**: Stack-based (50-entry in super block). When empty, scans disk for free inodes (di_mode == 0).
- **Blocks**: Group-linked list (50 blocks per group). When the super block stack fills, it dumps the stack into a freed block as a new group header.

### Source Modules

| File | Responsibility |
|------|---------------|
| `main.c` | CLI command loop, argument parsing, dispatch |
| `persist.c` | Block I/O (`bread`/`bwrite`), super block load/save |
| `format.c` | File system initialization (create .dat, zero blocks, root dir) |
| `super.c` | Inode/block allocation (`sb_ialloc`, `sb_balloc`, etc.) |
| `inode.c` | Hash table, iget/iput, name resolution, bmap mixed indexing, permission checks |
| `file.c` | fs_create/open/close/read/write/lseek/delete |
| `dir.c` | fs_mkdir/chdir/dir, directory entry add/remove |
| `user.c` | Login/logout, user table init, fd allocation |
| `globals.c` | Global variable definitions |

### Path Resolution Flow

`namei(path)` → `namei_parent(path, basename)` → iterates components via `dir_lookup()` starting from root (absolute) or cwd (relative) → returns locked MInode.

### Command Set

`login`, `logout`, `create`, `open`, `close`, `read`, `write`, `seek`, `delete`/`rm`, `mkdir`, `chdir`/`cd`, `dir`/`ls`, `format`, `help`, `exit`/`quit`

## Testing

- `验证指南.md` — Detailed verification guide with 12 test scenarios (all commands, multi-user isolation, persistence, nested dirs)
- `测试指南.md` — Quick test scenarios for login, file I/O, directories, user isolation, deletion protection, persistence, formatting

Input can be piped for automated testing: `./vfs.exe test.dat < test_commands.txt`

## Conventions

- All strings are null-terminated. Filenames limited to 14 characters.
- Paths max 256 characters. Max 64 path components.
- Directories limited to 128 entries.
- User login is required before file operations.
- Default users: usr1-usr8, password "123" for all.
