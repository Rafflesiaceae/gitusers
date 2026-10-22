#include "gitcusers.h"
#include <errno.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void usage(void)
{
    puts("Usage:\n"
         "  gitcusers [<user-short>|<name>|<email>]\n"
         "  gitcusers <user> clone <url> [git-clone-options...]\n\n"
         "Options:\n"
         "  -h             Show this help\n"
         "  -c             Check current user (zsh colors for unknown users)\n"
         "  -e             Open ~/.config/gitusers.json in $EDITOR\n"
         "  -l             List known users\n"
         "  -g             Print current known user; exit 1 if none matches\n"
         "  -p             Print the zsh RPROMPT\n"
         "  -sshc <user>   Print the user's SSH command");
}

/* Prefer HOME to avoid NSS lookups on every prompt invocation. */
static char *home_directory(void)
{
    const char *home = getenv("HOME");
    if (home && *home) return xstrdup(home);
    struct passwd *pw = getpwuid(getuid());
    if (!pw) die("cannot determine home directory; set HOME");
    return xstrdup(pw->pw_dir);
}

int main(int argc, char **argv)
{
    if (argc == 2 && !strcmp(argv[1], "-h")) { usage(); return 0; }
    const char *option = argc > 1 ? argv[1] : "";
    bool summary = argc == 1;
    bool check = argc == 2 && !strcmp(option, "-c");
    bool get = argc == 2 && !strcmp(option, "-g");
    bool prompt = argc == 2 && !strcmp(option, "-p");
    bool list = argc == 2 && !strcmp(option, "-l");
    bool edit = argc == 2 && !strcmp(option, "-e");
    bool ssh = argc == 3 && !strcmp(option, "-sshc");
    bool clone = argc >= 4 && !strcmp(argv[2], "clone");
    bool set = argc == 2 && option[0] != '-';
    if (!(summary || check || get || prompt || list || edit || ssh || clone || set)) {
        fputs("gitcusers: unsupported arguments; use -h for help\n", stderr);
        return 2;
    }
    Repo repo = {0};
    if (summary || check || get || prompt || set) repo = find_repo();
    /* The outside-repository prompt is intentionally silent and needs no JSON. */
    if (!repo.gitdir && (prompt || check || get)) { free_repo(&repo); return get ? 1 : 0; }
    if (set && !repo.gitdir) die("could not find a Git repository");
    char *home = home_directory();
    char *path = join(home, ".config/gitusers.json");
    if (edit) {
        const char *editor = getenv("EDITOR");
        if (!editor || !*editor) editor = "vi";
        /* Match the original executable-only EDITOR convention, with no shell. */
        char *args[] = {(char *)editor, path, NULL};
        int result = run(args, NULL);
        free(path); free(home);
        return result;
    }
    Users users = load_users(path);
    int result = 0;
    if (list) {
        for (size_t i = 0; i < users.count; ++i) {
            const User *u = &users.items[i];
            printf("{%s %s %s %s}\n", u->shortname, u->name, u->email, u->key);
        }
    } else if (ssh || set || clone) {
        const User *user = find_user(&users, ssh ? argv[2] : option);
        if (ssh) {
            char *command = ssh_command(user, false);
            puts(command);
            free(command);
        } else if (set) set_user(user);
        else {
            /* Pass authentication through Git's supported command override;
             * posix_spawn inherits the complete environment, including PATH. */
            char *command = ssh_command(user, false);
            if (setenv("GIT_SSH_COMMAND", command, 1) < 0) die("setenv: %s", strerror(errno));
            free(command);
            char **args = xmalloc((size_t)argc * sizeof(*args));
            args[0] = "git";
            for (int i = 2; i < argc; ++i) args[i - 1] = argv[i];
            args[argc - 1] = NULL;
            result = run(args, NULL);
            free(args);
        }
    } else {
        Config cfg = load_config(&repo, home);
        Identity identity = identify(&repo, &cfg, &users);
        if (summary) {
            puts("List of available Users:");
            for (size_t i = 0; i < users.count; ++i) {
                const User *user = &users.items[i];
                bool current = identity.state == FOUND && !strcmp(identity.name, user->shortname);
                /* Go's width is measured in codepoints; count UTF-8 leading
                 * bytes to retain alignment without locale initialization. */
                size_t width = current ? 2 : 0;
                for (const unsigned char *p = (const unsigned char *)user->shortname; *p; ++p)
                    if ((*p & 0xc0) != 0x80) ++width;
                printf("%*s%s%s (%s)\n", width < 14 ? (int)(14 - width) : 0, "",
                       current ? "→ " : "", user->shortname, user->name);
            }
        } else if (prompt) print_prompt(&repo, identity);
        else if (get) {
            if (identity.state == FOUND) puts(identity.name);
            else result = 1;
        } else if (check && identity.state != NO_REPO) {
            if (identity.state != FOUND) fputs("%{$fg[red]%}", stdout);
            fputs(identity.name, stdout);
        }
        free_config(&cfg);
    }
    free_users(&users);
    free_repo(&repo);
    free(path);
    free(home);
    return result;
}
