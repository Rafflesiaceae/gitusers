# gitusers
`gitusers` is a small helper to set up and maintain different git credentials specific to different repositories/remotes.

## Build

Requires Meson 1.1 or newer, Ninja, and `musl-gcc`. Git is needed at runtime for configuration writes, cloning, and repository status. Python 3 is used only by the integration tests.

```sh
./build.sh
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
| `gitusers` | List identities, marking the current match with an arrow |
| `gitusers work` | Set the current repository's name, email, and SSH command |
| `gitusers -l` | List all user records |
| `gitusers -g` | Print the matching short name and a newline; exit 1 for no match |
| `gitusers -c` | Print the short name, or the unknown name / `NONE` with zsh red markup |
| `gitusers -p` | Print the zsh right prompt without a trailing newline |
| `gitusers -sshc work` | Print the SSH command without writing Git configuration |
| `gitusers -e` | Open the JSON file in `$EDITOR`, defaulting to `vi` |
| `gitusers work clone <url> [options...]` | Clone using the selected identity for SSH authentication |
| `gitusers -h` | Show help |

For example, a work identity generates:

```sh
ssh -i ~/.ssh/work -o IdentitiesOnly=yes -o ControlPath="$HOME/.ssh/cm/work-%C"
```

The short name keeps each identity's multiplexing socket separate. For SSH multiplexing, create `~/.ssh/cm` and configure `ControlMaster` and `ControlPersist` in your SSH configuration. An empty private key omits `-i` and `IdentitiesOnly`, retaining the per-identity control path. Paths containing shell metacharacters are quoted.

Like gitusers, identity detection prefers a repository identity and falls back to `~/.gitconfig` if no repository identity is set. Name, email, and SSH command must match a defined user. Status commands silently upgrade an exact legacy SSH command missing the control path, writing to the local, worktree, or global scope it came from. Custom commands are left alone. Git includes and conditional includes are supported through a Git subprocess fallback.

The prompt includes the identity, branch or abbreviated detached commit, repository nesting depth, ahead/behind counts, today's stashes, tracked worktree changes, and a staged marker. Untracked files do not contribute to the displayed counts. Submodule gitfiles and linked worktrees are supported, including worktree-specific identity settings.

Help, listing, editing, SSH command printing, and cloning work outside a repository. Outside-repository `-p` and `-c` produce no output, and `-g` exits 1. `$EDITOR` is an executable name or path, following the original program's convention. Clone authentication uses `GIT_SSH_COMMAND` directly and needs no `ssh-i-from-env` wrapper. Cloning does not configure the resulting repository's identity; run `gitusers work` there to do that.
