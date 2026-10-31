#include "gitusers.h"
#include "cJSON.h"
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

static void replace(char **target, const char *value)
{
    free(*target);
    *target = xstrdup(value);
}

static int compare_users(const void *left, const void *right)
{
    return strcmp(((const User *)left)->shortname, ((const User *)right)->shortname);
}

/* Keep string ownership in the user array so the JSON tree can be freed early. */
static char *json_string(cJSON *object, const char *key)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
    if (!item || cJSON_IsNull(item)) return xstrdup("");
    if (!cJSON_IsString(item)) die("user field '%s' must be a string", key);
    return xstrdup(item->valuestring);
}

Users load_users(const char *path)
{
    size_t length;
    char *data = read_file(path, &length);
    if (!data) die("%s: %s", path, strerror(errno));
    cJSON *json = cJSON_ParseWithLengthOpts(data, length + 1, NULL, true);
    if (!json || !cJSON_IsArray(json)) die("%s: expected a JSON array of users", path);
    Users users = {.count = (size_t)cJSON_GetArraySize(json)};
    users.items = xmalloc(users.count * sizeof(*users.items));
    size_t i = 0;
    cJSON *item;
    cJSON_ArrayForEach(item, json) {
        if (!cJSON_IsObject(item)) die("%s: each user must be an object", path);
        users.items[i++] = (User){json_string(item, "short"), json_string(item, "name"),
                                 json_string(item, "email"), json_string(item, "privkey")};
    }
    cJSON_Delete(json);
    free(data);
    qsort(users.items, users.count, sizeof(*users.items), compare_users);
    return users;
}

void free_users(Users *users)
{
    for (size_t i = 0; i < users->count; ++i) {
        free(users->items[i].shortname);
        free(users->items[i].name);
        free(users->items[i].email);
        free(users->items[i].key);
    }
    free(users->items);
}

const User *find_user(const Users *users, const char *query)
{
    for (size_t i = 0; i < users->count; ++i) {
        const User *user = &users->items[i];
        if (!strcmp(query, user->shortname) || !strcmp(query, user->name) || !strcmp(query, user->email))
            return user;
    }
    die("could not find a defined user matching '%s'", query);
}

/* Leave conventional paths unchanged for compatibility. Quote unusual paths
 * as one shell word, retaining tilde expansion for a leading ~/ component. */
static char *quote_key(const char *key)
{
    if (strspn(key, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_./~@:+-=") == strlen(key))
        return xstrdup(key);
    bool home = !strncmp(key, "~/", 2);
    const char *start = home ? key + 2 : key;
    char *result = xmalloc(strlen(start) * 4 + 16), *out = result;
    if (home) { memcpy(out, "\"$HOME\"/", 8); out += 8; }
    *out++ = '\'';
    for (const char *p = start; *p; ++p) {
        if (*p == '\'') { memcpy(out, "'\\''", 4); out += 4; }
        else *out++ = *p;
    }
    *out++ = '\'';
    *out = 0;
    return result;
}

char *ssh_command(const User *user, bool legacy)
{
    char *key = quote_key(user->key);
    char *base = *key ? format("ssh -i %s -o IdentitiesOnly=yes", key) : xstrdup("ssh");
    free(key);
    if (legacy) return base;
    /* The short name is inside double quotes in the command and is also
     * interpreted by OpenSSH's percent-token expansion. Escape both layers. */
    char *shortname = xmalloc(strlen(user->shortname) * 2 + 1), *out = shortname;
    for (const char *p = user->shortname; *p; ++p) {
        if (strchr("\\\"$`", *p)) *out++ = '\\';
        if (*p == '%') *out++ = '%';
        *out++ = *p;
    }
    *out = 0;
    char *result = format("%s -o ControlPath=\"$HOME/.ssh/cm/%s-%%C\"", base, shortname);
    free(shortname);
    free(base);
    return result;
}

static char *gitdir_at(const char *directory)
{
    char *path = join(directory, ".git");
    struct stat st;
    if (stat(path, &st) < 0) { free(path); return NULL; }
    if (!S_ISDIR(st.st_mode)) {
        char *data = read_file(path, NULL);
        free(path);
        if (!data) die("reading .git: %s", strerror(errno));
        char *line = trim(data);
        if (strncmp(line, "gitdir:", 7)) die("invalid .git file in %s", directory);
        path = join(directory, trim(line + 7));
        free(data);
    }
    char *resolved = realpath(path, NULL);
    if (!resolved) die("%s: %s", path, strerror(errno));
    free(path);
    /* A stray directory named .git is not a repository marker. In particular,
     * do not let empty ancestor directories affect prompt nesting depth. */
    char *head = join(resolved, "HEAD");
    bool valid = stat(head, &st) == 0;
    free(head);
    if (!valid) { free(resolved); return NULL; }
    return resolved;
}

Repo find_repo(void)
{
    Repo repo = {0};
    char *cwd = getcwd(NULL, 0);
    if (!cwd) die("getcwd: %s", strerror(errno));
    const char *env = getenv("GIT_DIR");
    if (env && *env) {
        repo.gitdir = realpath(env, NULL);
        if (!repo.gitdir) die("GIT_DIR: %s", strerror(errno));
    }
    /* stat one known path per ancestor; avoid listing entire directories.
     * Deduplicate resolved gitdirs when counting nested repositories. */
    char **seen = NULL;
    size_t count = 0;
    for (;;) {
        char *dir = gitdir_at(cwd);
        if (dir) {
            if (!repo.gitdir) repo.gitdir = xstrdup(dir);
            bool duplicate = false;
            for (size_t i = 0; i < count; ++i) if (!strcmp(seen[i], dir)) duplicate = true;
            if (duplicate) free(dir);
            else { seen = xrealloc(seen, (count + 1) * sizeof(*seen)); seen[count++] = dir; }
        }
        if (!strcmp(cwd, "/")) break;
        char *slash = strrchr(cwd, '/');
        if (slash == cwd) cwd[1] = 0;
        else *slash = 0;
    }
    free(cwd);
    repo.depth = count ? (unsigned)count : 1;
    for (size_t i = 0; i < count; ++i) free(seen[i]);
    free(seen);
    if (repo.gitdir) {
        char *path = join(repo.gitdir, "commondir");
        char *data = read_file(path, NULL);
        free(path);
        if (data) {
            path = join(repo.gitdir, trim(data));
            repo.common = realpath(path, NULL);
            if (!repo.common) die("%s: %s", path, strerror(errno));
            free(path);
            free(data);
        } else if (errno == ENOENT) repo.common = xstrdup(repo.gitdir);
        else die("reading commondir: %s", strerror(errno));
    }
    return repo;
}

void free_repo(Repo *repo)
{
    free(repo->gitdir);
    free(repo->common);
}

void free_config(Config *config)
{
    free(config->name);
    free(config->email);
    free(config->ssh);
    *config = (Config){0};
}

static void config_value(Config *cfg, const char *key, const char *value)
{
    if (!strcasecmp(key, "user.name")) { replace(&cfg->name, value); cfg->has_user = true; }
    else if (!strcasecmp(key, "user.email")) { replace(&cfg->email, value); cfg->has_user = true; }
    else if (!strcasecmp(key, "core.sshcommand")) replace(&cfg->ssh, value);
    else if (!strcasecmp(key, "extensions.worktreeconfig"))
        cfg->worktree_enabled = !strcasecmp(value, "true") || !strcasecmp(value, "yes") ||
                                !strcasecmp(value, "on") || !strcmp(value, "1") || !*value;
}

/* Decode the common single-line Git value grammar in place. Git itself handles
 * includes, continuations and unusual syntax through the fallback below. */
static bool decode_value(char *value)
{
    bool quoted = false;
    char *out = value, *last = value;
    for (char *p = value; *p; ++p) {
        if (*p == '"') { quoted = !quoted; continue; }
        if (!quoted && (*p == '#' || *p == ';')) break;
        char ch = *p;
        if (ch == '\\') {
            switch (*++p) {
            case 'n': ch = '\n'; break;
            case 't': ch = '\t'; break;
            case 'b': ch = '\b'; break;
            case '\\': case '"': ch = *p; break;
            default: return false;
            }
        }
        *out++ = ch;
        if (quoted || (ch != ' ' && ch != '\t' && ch != '\r')) last = out;
    }
    *last = 0;
    return !quoted;
}

static bool parse_config(Config *cfg, char *data)
{
    char section[128] = "";
    char *cursor = data, *line;
    while ((line = strtok_r(cursor, "\n", &cursor))) {
        line = trim(line);
        if (!*line || *line == '#' || *line == ';') continue;
        if (*line == '[') {
            char *end = strchr(line, ']');
            if (!end || strspn(end + 1, " \t\r") != strlen(end + 1)) return false;
            *end = 0;
            char *name = trim(line + 1);
            if (!strncasecmp(name, "include", 7)) return false;
            if (strlen(name) >= sizeof(section)) return false;
            strcpy(section, name);
            continue;
        }
        char *end = line;
        while (isalnum((unsigned char)*end) || *end == '-') ++end;
        if (end == line) return false;
        char *value = end;
        if (*value) {
            char delimiter = *value;
            *value++ = 0;
            value += strspn(value, " \t");
            if (delimiter != '=') {
                if (*value != '=') return false;
                ++value;
            }
            value += strspn(value, " \t");
        }
        if (!decode_value(value)) return false;
        char *key = format("%s.%s", section, line);
        config_value(cfg, key, value);
        free(key);
    }
    return true;
}

static void read_config(Config *cfg, const char *path, bool worktree)
{
    char *data = read_file(path, NULL);
    if (!data) {
        if (errno == ENOENT) return;
        die("%s: %s", path, strerror(errno));
    }
    /* Parse into a temporary object so a fallback never retains partial data. */
    Config part = {0};
    bool simple = parse_config(&part, data);
    free(data);
    if (!simple) {
        free_config(&part);
        char *args[] = {"git", "config", "--includes", "--null", "--file", (char *)path, "--list", NULL};
        if (run(args, &data)) die("could not read Git config %s", path);
        /* Git emits key LF value NUL, with a final extra NUL from run(). */
        for (char *p = data; *p;) {
            char *next = p + strlen(p) + 1;
            char *value = strchr(p, '\n');
            if (value) *value++ = 0;
            config_value(&part, p, value ? value : "");
            p = next;
        }
        free(data);
    }
    if (part.name) replace(&cfg->name, part.name);
    if (part.email) replace(&cfg->email, part.email);
    if (part.ssh) { replace(&cfg->ssh, part.ssh); cfg->worktree = worktree; }
    cfg->has_user |= part.has_user;
    cfg->worktree_enabled |= part.worktree_enabled;
    free_config(&part);
}

Config load_config(const Repo *repo, const char *home)
{
    Config cfg = {0};
    if (!repo->gitdir) return cfg;
    char *path = join(repo->common, "config");
    read_config(&cfg, path, false);
    free(path);
    if (cfg.worktree_enabled) {
        path = join(repo->gitdir, "config.worktree");
        read_config(&cfg, path, true);
        free(path);
    }
    /* Match gitusers' local-identity-first policy, including the SSH command
     * from the same scope instead of merging a global identity with local SSH. */
    if (!cfg.has_user) {
        free_config(&cfg);
        path = join(home, ".gitconfig");
        read_config(&cfg, path, false);
        free(path);
        cfg.global = true;
    }
    return cfg;
}

Identity identify(const Repo *repo, Config *cfg, const Users *users)
{
    if (!repo->gitdir) return (Identity){NO_REPO, ""};
    if (!cfg->has_user) return (Identity){EMPTY, "NONE"};
    const char *name = cfg->name ? cfg->name : "";
    const char *email = cfg->email ? cfg->email : "";
    const char *ssh = cfg->ssh ? cfg->ssh : "";
    for (size_t i = 0; i < users->count; ++i) {
        const User *user = &users->items[i];
        if (strcmp(name, user->name) || strcmp(email, user->email)) continue;
        char *expected = ssh_command(user, false);
        bool match = !strcmp(ssh, expected);
        if (!match) {
            char *legacy = ssh_command(user, true);
            /* Only rewrite an exact known legacy command; customized SSH
             * settings stay unknown and are never silently overwritten. */
            if (!strcmp(ssh, legacy)) {
                char *args[] = {"git", "config", cfg->global ? "--global" :
                    cfg->worktree ? "--worktree" : "--local", "core.sshCommand", expected, NULL};
                match = run(args, NULL) == 0;
                if (match) replace(&cfg->ssh, expected);
            }
            free(legacy);
        }
        free(expected);
        if (match) return (Identity){FOUND, user->shortname};
    }
    return (Identity){UNKNOWN, name};
}

void set_user(const User *user)
{
    char *ssh = ssh_command(user, false);
    char *keys[] = {"user.name", "user.email", "core.sshCommand"};
    char *values[] = {user->name, user->email, ssh};
    for (size_t i = 0; i < 3; ++i) {
        /* --worktree falls back to --local unless worktreeConfig is enabled;
         * replace-all avoids leaving a contradictory duplicate identity. */
        char *args[] = {"git", "config", "--worktree", "--replace-all", keys[i], values[i], NULL};
        if (run(args, NULL)) die("failed to set %s", keys[i]);
    }
    free(ssh);
}

void reset_user(void)
{
    char *scopes[] = {"--local", "--worktree"};
    char *keys[] = {"user.name", "user.email", "core.sshCommand"};
    /* Clear shared repository settings and current worktree overrides, leaving
     * global configuration and other worktrees' overrides untouched. Without
     * worktreeConfig, --worktree repeats the local cleanup harmlessly. */
    for (size_t i = 0; i < 2; ++i) {
        for (size_t j = 0; j < 3; ++j) {
            char *args[] = {"git", "config", scopes[i], "--unset-all", keys[j], NULL};
            int result = run(args, NULL);
            /* Git returns 5 when the key is absent, so reset is idempotent. */
            if (result != 0 && result != 5) die("failed to reset %s (%s)", keys[j], scopes[i]);
        }
    }
}
