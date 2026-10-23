#include "gitusers.h"
#include <errno.h>
#include <fcntl.h>
#include <spawn.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

_Noreturn void die(const char *fmt, ...)
{
    va_list args;
    fputs("gitusers: ", stderr);
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fputc('\n', stderr);
    exit(1);
}

void *xmalloc(size_t size)
{
    void *ptr = malloc(size ? size : 1);
    if (!ptr) die("out of memory");
    return ptr;
}

void *xrealloc(void *ptr, size_t size)
{
    void *next = realloc(ptr, size ? size : 1);
    if (!next) die("out of memory");
    return next;
}

char *xstrdup(const char *text)
{
    char *copy = strdup(text);
    if (!copy) die("out of memory");
    return copy;
}

char *format(const char *fmt, ...)
{
    va_list args, copy;
    va_start(args, fmt);
    va_copy(copy, args);
    int length = vsnprintf(NULL, 0, fmt, copy);
    va_end(copy);
    if (length < 0) die("formatting failed");
    char *result = xmalloc((size_t)length + 1);
    vsnprintf(result, (size_t)length + 1, fmt, args);
    va_end(args);
    return result;
}

char *join(const char *base, const char *path)
{
    return path[0] == '/' ? xstrdup(path) : format("%s/%s", base, path);
}

char *trim(char *text)
{
    text += strspn(text, " \t\r\n");
    size_t length = strlen(text);
    while (length && strchr(" \t\r\n", text[length - 1])) text[--length] = 0;
    return text;
}

/* Read once into a growing buffer; also works for procfs and small text files. */
static char *read_fd(int fd, size_t *length)
{
    size_t used = 0, capacity = 4096;
    char *data = xmalloc(capacity + 1);
    for (;;) {
        if (used == capacity) data = xrealloc(data, (capacity *= 2) + 1);
        ssize_t got = read(fd, data + used, capacity - used);
        if (got < 0 && errno == EINTR) continue;
        if (got < 0) { free(data); return NULL; }
        if (!got) break;
        used += (size_t)got;
    }
    data[used] = 0;
    if (length) *length = used;
    return data;
}

char *read_file(const char *path, size_t *length)
{
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return NULL;
    char *result = read_fd(fd, length);
    int saved = errno;
    close(fd);
    errno = saved;
    return result;
}

/* posix_spawn avoids copying the parent address space. Stderr stays visible,
 * and captured stdout is drained before waiting to avoid a full-pipe deadlock. */
int run(char *const argv[], char **output)
{
    int pipefd[2] = {-1, -1};
    posix_spawn_file_actions_t actions;
    int error = posix_spawn_file_actions_init(&actions);
    if (error) die("spawn setup: %s", strerror(error));
    if (output) {
        if (pipe(pipefd) < 0) die("pipe: %s", strerror(errno));
        if ((error = posix_spawn_file_actions_adddup2(&actions, pipefd[1], STDOUT_FILENO)) ||
            (error = posix_spawn_file_actions_addclose(&actions, pipefd[0])) ||
            (error = posix_spawn_file_actions_addclose(&actions, pipefd[1])))
            die("spawn setup: %s", strerror(error));
    }
    pid_t child;
    error = posix_spawnp(&child, argv[0], &actions, NULL, argv, environ);
    posix_spawn_file_actions_destroy(&actions);
    if (output) close(pipefd[1]);
    if (error) {
        if (output) { close(pipefd[0]); *output = xstrdup(""); }
        fprintf(stderr, "gitusers: %s: %s\n", argv[0], strerror(error));
        return 127;
    }
    if (output) {
        *output = read_fd(pipefd[0], NULL);
        close(pipefd[0]);
        if (!*output) die("reading command output: %s", strerror(errno));
    }
    int status;
    while (waitpid(child, &status, 0) < 0) {
        if (errno != EINTR) die("waitpid: %s", strerror(errno));
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
}
