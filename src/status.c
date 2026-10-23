#include "gitusers.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#define YELLOW "%{$fg[yellow]%}"
#define RESET "%{${reset_color}%}"
#define RED "%{$fg[red]%}"
#define MAGENTA "%{$fg[magenta]%}"

/* A stash list is the common repository's reflog. Read timestamps directly
 * instead of starting Git and formatting every stash's commit message. */
static unsigned stashes_today(const Repo *repo)
{
    time_t now = time(NULL);
    struct tm today;
    if (!localtime_r(&now, &today)) return 0;
    /* Reftable stores reflogs in tables rather than logs/refs. Keep the direct
     * file fast path for ordinary repositories and delegate this backend to Git. */
    char *table = join(repo->common, "reftable/tables.list");
    struct stat st;
    bool reftable = stat(table, &st) == 0;
    free(table);
    if (reftable) {
        char date[16];
        strftime(date, sizeof(date), "%Y-%m-%d", &today);
        char *args[] = {"git", "stash", "list", "--format=%gd", "--date=iso", NULL};
        char *output;
        if (run(args, &output)) { free(output); die("could not read stashes"); }
        unsigned count = 0;
        char *cursor = output, *line;
        while ((line = strtok_r(cursor, "\n", &cursor)))
            if (!strncmp(line, "stash@{", 7) && !strncmp(line + 7, date, 10)) ++count;
        free(output);
        return count;
    }
    char *path = join(repo->common, "logs/refs/stash");
    char *data = read_file(path, NULL);
    free(path);
    if (!data) {
        if (errno != ENOENT) die("reading stash reflog: %s", strerror(errno));
        return 0;
    }
    unsigned count = 0;
    char *cursor = data, *line;
    while ((line = strtok_r(cursor, "\n", &cursor))) {
        char *tab = strchr(line, '\t');
        if (!tab) continue;
        *tab = 0;
        char *zone = strrchr(line, ' ');
        if (!zone) continue;
        *zone++ = 0;
        char *stamp = strrchr(line, ' ');
        if (!stamp) continue;
        char *end;
        long long seconds = strtoll(stamp + 1, &end, 10);
        if (*end || strlen(zone) != 5 || (*zone != '+' && *zone != '-')) continue;
        unsigned hours, minutes;
        if (sscanf(zone + 1, "%2u%2u", &hours, &minutes) != 2 || hours > 23 || minutes > 59) continue;
        /* --date=iso uses the reflog entry's timezone, not the current zone. */
        long offset = (long)(hours * 60 + minutes) * 60;
        time_t date = (time_t)(seconds + (*zone == '-' ? -offset : offset));
        struct tm created;
        if (gmtime_r(&date, &created) && created.tm_year == today.tm_year && created.tm_yday == today.tm_yday)
            ++count;
    }
    free(data);
    return count;
}

static void superscript(unsigned number)
{
    static const char *digits[] = {"⁰", "¹", "²", "³", "⁴", "⁵", "⁶", "⁷", "⁸", "⁹"};
    char text[32];
    snprintf(text, sizeof(text), "%u", number);
    for (const char *p = text; *p; ++p) fputs(digits[*p - '0'], stdout);
}

void print_prompt(const Repo *repo, Identity identity)
{
    if (identity.state == NO_REPO) return;
    /* Porcelain v2 supplies branch, divergence and both index/worktree states
     * in one stable, NUL-delimited response. Untracked files are not displayed. */
    char *args[] = {"git", "--no-optional-locks", "status", "--porcelain=v2", "--branch", "--ahead-behind",
                    "-z", "--untracked-files=no", NULL};
    char *data;
    if (run(args, &data)) { free(data); die("could not read Git status"); }
    char *branch = NULL, *oid = NULL;
    unsigned ahead = 0, behind = 0, changed = 0, staged = 0;
    for (char *p = data; *p;) {
        char *next = p + strlen(p) + 1;
        if (!strncmp(p, "# branch.head ", 14)) branch = p + 14;
        else if (!strncmp(p, "# branch.oid ", 13)) oid = p + 13;
        else if (!strncmp(p, "# branch.ab ", 12)) sscanf(p + 12, "+%u -%u", &ahead, &behind);
        else if (*p == '1' || *p == '2') {
            if (strlen(p) < 4) die("malformed Git status record");
            if (p[2] != '.') ++staged;
            if (p[3] != '.') ++changed;
            /* Rename/copy records include a second NUL-delimited pathname. */
            if (*p == '2') next += strlen(next) + 1;
        } else if (*p == 'u') {
            /* git diff counts the tracked worktree modification in addition
             * to the unmerged entry; gitusers omits the conflict marker. */
            if (strlen(p) >= 4 && p[3] != 'D') ++changed;
        }
        p = next;
    }
    char *detached = NULL;
    if (branch && !strcmp(branch, "(detached)")) {
        char *short_args[] = {"git", "rev-parse", "--short", "HEAD", NULL};
        char *hash;
        if (run(short_args, &hash)) { free(hash); free(data); die("could not resolve HEAD"); }
        detached = format(":%s", trim(hash));
        free(hash);
        branch = detached;
    }
    if (!branch) branch = oid ? oid : "detached HEAD";
    unsigned stashes = stashes_today(repo);
    fputs(YELLOW "(" RESET, stdout);
    if (identity.state == EMPTY || identity.state == UNKNOWN) fputs(RED, stdout);
    fputs(identity.name, stdout);
    fputs(YELLOW ";" RESET MAGENTA, stdout);
    fputs(branch, stdout);
    fputs(RESET, stdout);
    if (repo->depth > 1) { fputs(YELLOW, stdout); superscript(repo->depth); fputs(RESET, stdout); }
    if (ahead || behind || stashes || changed || staged) fputc(' ', stdout);
    if (ahead) printf("↑%u", ahead);
    if (behind) printf("↓%u", behind);
    if (stashes) printf("≡%u", stashes);
    if (changed) printf("☇%u", changed);
    if (staged) fputs("★", stdout);
    fputs(YELLOW ")" RESET, stdout);
    free(detached);
    free(data);
}
