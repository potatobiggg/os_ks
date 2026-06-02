#ifndef USER_H
#define USER_H

#include <stdint.h>
#include "types.h"

/* Log in a user by name and password. Returns user index (>=0) or -1 on failure. */
int fs_login(const char *username, const char *password);

/* Log out the current user. Returns 0 on success. */
int fs_logout(void);

/* Get the currently logged-in user, or NULL. */
User *get_current_user(void);

/* Get the OpenFile for a user's fd. Returns NULL if invalid. */
OpenFile *user_get_file(int fd);

/* Allocate a free fd for the current user. Returns fd or -1. */
int user_alloc_fd(void);

/* Free a file descriptor for the current user. */
void user_free_fd(int fd);

/* Initialize the user subsystem. */
void user_init(void);

#endif /* USER_H */
