# gitcusers

A C implementation of [gitusers](https://github.com/Rafflesiaceae/gitusers), built with Meson, cJSON, and musl. It selects repository identities and prints the same zsh prompt format as the Go program.

## Build

Requires Meson 1.1 or newer, Ninja, and `musl-gcc`. Git is needed at runtime for configuration writes, cloning, and repository status. Python 3 is used only by the integration tests.

```sh
meson setup build --native-file musl.ini
meson compile -C build
meson test -C build --print-errorlogs
./build/gitcusers -h
```

The musl configuration produces a fully static executable, with release optimization, link-time optimization, and removal of unused functions. cJSON 1.7.19 is vendored and compiled with musl, so building needs neither network access nor a system cJSON installation. Third-party licensing is in `vendor/cjson/LICENSE`.

To install, set the desired prefix during setup and run `meson install -C build`. Meson strips the installed executable. For a build without tests, add `-Dtests=false` during setup.

A normal libc build is also supported for development and sanitizers:

```sh
meson setup build-debug -Dbuildtype=debug -Db_lto=false -Db_sanitize=address,undefined
meson compile -C build-debug
meson test -C build-debug --print-errorlogs
```

## Configuration

Uses the existing `~/.config/gitusers.json` file:

```json
[
    {
        "short": "work",
        "name": "Work User",
        "email": "work@example.com",
        "privkey": "~/.ssh/work"
    },
    {
        "short": "personal",
        "name": "Personal User",
        "email": "me@example.com",
        "privkey": ""
    }
]
```

`HOME` determines the home directory; the account database is used if `HOME` is unset. Users are sorted by `short`. Selection accepts the short name, full name, or email address.

## Usage

| Command | Result |
| --- | --- |
| `gitcusers` | List identities, marking the current match with an arrow |
| `gitcusers work` | Set the current repository's name, email, and SSH command |
| `gitcusers -l` | List all user records |
| `gitcusers -g` | Print the matching short name and a newline; exit 1 for no match |
| `gitcusers -c` | Print the short name, or the unknown name / `NONE` with zsh red markup |
| `gitcusers -p` | Print the zsh right prompt without a trailing newline |
| `gitcusers -sshc work` | Print the SSH command without writing Git configuration |
| `gitcusers -e` | Open the JSON file in `$EDITOR`, defaulting to `vi` |
| `gitcusers work clone <url> [options...]` | Clone using the selected identity for SSH authentication |
| `gitcusers -h` | Show help |

For example, a work identity generates:

```sh
ssh -i ~/.ssh/work -o IdentitiesOnly=yes -o ControlPath="$HOME/.ssh/cm/work-%C"
```

The short name keeps each identity's multiplexing socket separate. For SSH multiplexing, create `~/.ssh/cm` and configure `ControlMaster` and `ControlPersist` in your SSH configuration. An empty private key omits `-i` and `IdentitiesOnly`, retaining the per-identity control path. Paths containing shell metacharacters are quoted.

Like gitusers, identity detection prefers a repository identity and falls back to `~/.gitconfig` if no repository identity is set. Name, email, and SSH command must match a defined user. Status commands silently upgrade an exact legacy SSH command missing the control path, writing to the local, worktree, or global scope it came from. Custom commands are left alone. Git includes and conditional includes are supported through a Git subprocess fallback.

The prompt includes the identity, branch or abbreviated detached commit, repository nesting depth, ahead/behind counts, today's stashes, tracked worktree changes, and a staged marker. Untracked files do not contribute to the displayed counts. Submodule gitfiles and linked worktrees are supported, including worktree-specific identity settings.

Help, listing, editing, SSH command printing, and cloning work outside a repository. Outside-repository `-p` and `-c` produce no output, and `-g` exits 1. `$EDITOR` is an executable name or path, following the original program's convention. Clone authentication uses `GIT_SSH_COMMAND` directly and needs no `ssh-i-from-env` wrapper. Cloning does not configure the resulting repository's identity; run `gitcusers work` there to do that.

## Performance

Identity checks read the JSON and Git configuration directly, without starting Git in the ordinary case. Repository discovery probes `.git` with `stat` rather than enumerating directories. Help and outside-repository prompt checks avoid loading the JSON file.

Prompt rendering combines branch, divergence, staged, and worktree state into one `git status --porcelain=v2` call. It skips untracked-file enumeration and optional index writes. Stash dates are read directly from the common repository's reflog; reftable repositories use a Git fallback. Detached HEAD needs one additional Git call to obtain Git's configured, unambiguous abbreviated commit ID.

Example measurements on a small clean repository, with one configured identity, 20 warmups and 200 runs per command using `hyperfine --shell=none`:

| Operation | C / static musl | Go reference | Speedup |
| --- | ---: | ---: | ---: |
| `-c` | 0.57 ms | 3.69 ms | 6.5x |
| `-p` | 4.60 ms | 18.80 ms | 4.1x |

The unstripped C executable was about 123 KiB versus 4.4 MiB for Go. These are warm-run measurements on one machine; repository size, Git configuration, filesystem, and system load affect timings. The reference was built from the original Go sources, with only home-directory selection adjusted to use the isolated test home.

## Verification

The integration suite creates temporary homes and real Git repositories to cover identity aliases, quoting, local/global SSH upgrades, custom SSH preservation, includes, prompt states, conflicts, divergence, stash dates, reftable, submodules, worktrees, cloning, and editor exit status. It does not modify your own Git configuration.

An optional compatibility test compares output with a Go reference executable that uses `HOME` for its configuration directory:

```sh
GITUSERS_REFERENCE=/path/to/test-reference meson test -C build --print-errorlogs
```

Without this variable the compatibility test is skipped. The reftable test is skipped if the installed Git lacks migration support.
