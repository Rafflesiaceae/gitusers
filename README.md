# About
`gitusers` is a small helper to set up and maintain different git credentials specific to different repositories/remotes

# Usage
set up a local config file in `~/.config/gitusers.json` in the following structure:

```
[
    {
        "short": "foo",
        "name": "Foobar",
        "email": "foobar@foo.bar",
        "privkey": "~/.ssh/foobar"
    }
]
```

calling `gitusers` will check if the local git config matches a known user-short, in case of a match, it prints the user-short back, in case of either a misconfiguration or an unknown user, it will report an error in zsh-colors (argless usage is for embedding in your prompt [currently only zsh])

calling `gitusers <user-short>` will configure the current git repository with the given user config

calling `gitusers -l` will list all known users in your `~/.config/gitusers.json`

calling `gitusers <user-short> clone <url>` will clone an upstream URL with the given user config

# Design
the first idea was to use `gitusers` to change all remote urls according to a schema
for each specific user in `~/.ssh/config`, e.g. for github we would make another
section with a differently named host (.e.g `github_otheruser`), set the section up
to use our specific privkey and then use that host in the desired git remote

however, this sadly doesn't cover submodules in a reasonable way, as submodules
won't follow local naming schemes

thus the current way is to set the `core.sshCommand` via git config and
override the ssh command to include the specific privkey see <https://superuser.com/a/912281>, e.g.
```
git config core.sshCommand "ssh -i ~/.ssh/id_rsa_example -F /dev/null"
```
