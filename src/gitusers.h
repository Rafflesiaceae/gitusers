#ifndef GITUSERS_H
#define GITUSERS_H

#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#include <stdbool.h>
#include <stddef.h>

typedef struct {
    char *shortname, *name, *email, *key;
} User;

typedef struct {
    User *items;
    size_t count;
} Users;

typedef struct {
    char *gitdir, *common;
    unsigned depth;
} Repo;

typedef struct {
    char *name, *email, *ssh;
    bool has_user, global, worktree, worktree_enabled;
} Config;

typedef enum { EMPTY, FOUND, UNKNOWN, NO_REPO } State;
typedef struct {
    State state;
    const char *name;
} Identity;

_Noreturn void die(const char *format, ...);
void *xmalloc(size_t size);
void *xrealloc(void *ptr, size_t size);
char *xstrdup(const char *text);
char *format(const char *fmt, ...);
char *join(const char *base, const char *path);
char *trim(char *text);
char *read_file(const char *path, size_t *length);
int run(char *const argv[], char **output);
Users load_users(const char *path);
void free_users(Users *users);
const User *find_user(const Users *users, const char *query);
char *ssh_command(const User *user, bool legacy);
Repo find_repo(void);
void free_repo(Repo *repo);
Config load_config(const Repo *repo, const char *home);
void free_config(Config *config);
Identity identify(const Repo *repo, Config *config, const Users *users);
void set_user(const User *user);
void print_prompt(const Repo *repo, Identity identity);

#endif
