"""Exercise the CLI against isolated real Git repositories and temporary homes."""

import datetime
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


BINARY = str(Path(sys.argv.pop(1)).resolve())
REFERENCE = os.environ.get("GITUSERS_REFERENCE")
RED = "%{$fg[red]%}"
YELLOW = "%{$fg[yellow]%}"
RESET = "%{${reset_color}%}"
MAGENTA = "%{$fg[magenta]%}"


class Integration(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="gitcusers-test-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.home = self.root / "home"
        (self.home / ".config").mkdir(parents=True)
        # Clear inherited Git overrides to protect the caller's configuration.
        self.env = {k: v for k, v in os.environ.items() if not k.startswith("GIT_")}
        self.env.update(HOME=str(self.home), XDG_CONFIG_HOME=str(self.home / ".config"),
                        GIT_CONFIG_NOSYSTEM="1", GIT_CONFIG_GLOBAL=str(self.home / ".gitconfig"),
                        LC_ALL="C", TZ="UTC", GIT_TERMINAL_PROMPT="0")
        self.users = [dict(short="work", name="Work User", email="work@example.com", privkey="~/.ssh/work"),
                      dict(short="personal", name="Personal User", email="me@example.com", privkey="")]
        self.write_users()
        (self.home / ".gitconfig").write_text("")
        self.repo = self.root / "repo"
        self.repo.mkdir()
        self.git("init", "-b", "main")

    def write_users(self):
        (self.home / ".config/gitusers.json").write_text(json.dumps(self.users))

    def git(self, *args, cwd=None, ok=True, env=None):
        result = subprocess.run(["git", *args], cwd=cwd or self.repo, env=env or self.env,
                                capture_output=True, text=True)
        if ok:
            self.assertEqual(result.returncode, 0, result.stderr)
        return result

    def cli(self, *args, cwd=None, code=0, binary=BINARY):
        result = subprocess.run([binary, *args], cwd=cwd or self.repo, env=self.env,
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, code, result.stderr)
        return result.stdout

    def ssh(self, user="work"):
        key = " -i ~/.ssh/work -o IdentitiesOnly=yes" if user == "work" else ""
        return f'ssh{key} -o ControlPath="$HOME/.ssh/cm/{user}-%C"'

    def configure(self, user="work", legacy=False, global_=False):
        definition = next(u for u in self.users if u["short"] == user)
        scope = ["--global"] if global_ else ["--local"]
        for key in ("name", "email"):
            self.git("config", *scope, f"user.{key}", definition[key])
        command = self.ssh(user).split(" -o ControlPath=")[0] if legacy else self.ssh(user)
        self.git("config", *scope, "core.sshCommand", command)

    def commit(self, message="initial"):
        self.git("add", "-A")
        self.git("commit", "--allow-empty", "-m", message)

    def prompt(self, branch="main", post="", identity="work", depth=""):
        return (f"{YELLOW}({RESET}{identity}{YELLOW};{RESET}{MAGENTA}{branch}{RESET}"
                f"{depth}{post}{YELLOW}){RESET}")

    def test_help_and_outside_repository(self):
        (self.home / ".config/gitusers.json").unlink()
        self.assertIn("Usage:", self.cli("-h"))
        self.assertEqual(self.cli("-p", cwd=self.root), "")
        self.assertEqual(self.cli("-c", cwd=self.root), "")
        self.cli("-g", cwd=self.root, code=1)
        self.cli("--invalid", code=2)

    def test_sorted_list_summary_and_aliases(self):
        self.assertEqual(self.cli("-l", cwd=self.root),
                         "{personal Personal User me@example.com }\n{work Work User work@example.com ~/.ssh/work}\n")
        self.assertEqual(self.cli(), "List of available Users:\n      personal (Personal User)\n          work (Work User)\n")
        for alias in ("work", "Work User", "work@example.com"):
            self.assertEqual(self.cli("-sshc", alias), self.ssh() + "\n")
            self.cli(alias)
            self.assertEqual(self.cli("-g"), "work\n")
            self.assertEqual(self.cli("-c"), "work")
        self.assertIn("        → work (Work User)", self.cli())
        self.assertEqual(self.git("config", "--get", "core.sshCommand").stdout.strip(), self.ssh())
        self.cli("missing-user", code=1)

    def test_empty_unknown_and_custom_ssh(self):
        self.assertEqual(self.cli("-c"), RED + "NONE")
        self.cli("-g", code=1)
        self.configure()
        self.git("config", "core.sshCommand", "ssh -F custom.conf")
        self.assertEqual(self.cli("-c"), RED + "Work User")
        self.cli("-g", code=1)
        self.assertEqual(self.git("config", "core.sshCommand").stdout.strip(), "ssh -F custom.conf")

    def test_local_and_global_legacy_upgrades(self):
        for global_ in (False, True):
            for user in ("work", "personal"):
                with self.subTest(global_=global_, user=user):
                    if global_:
                        self.git("config", "--remove-section", "user", ok=False)
                        self.git("config", "--unset", "core.sshCommand", ok=False)
                    self.configure(user, legacy=True, global_=global_)
                    self.assertEqual(self.cli("-g"), user + "\n")
                    scope = ["--global"] if global_ else ["--local"]
                    self.assertEqual(self.git("config", *scope, "core.sshCommand").stdout.strip(), self.ssh(user))

    def test_git_quoting_comments_and_additional_user_keys(self):
        self.cli("work")
        config = self.repo / ".git/config"
        with config.open("a") as stream:
            stream.write('[User]\n Name = "Work User" # comment\n Email = work@example.com\n signingKey = ignored\n')
        self.assertEqual(self.cli("-g"), "work\n")
        self.users[0]["name"] = 'Work "Quoted"\tUser'
        self.write_users()
        self.cli("work")
        self.assertEqual(self.cli("-g"), "work\n")

    def test_git_includes_and_continuations(self):
        self.configure()
        config = self.repo / ".git/config"
        included = self.root / "included.config"
        included.write_bytes(config.read_bytes())
        config.write_text('[include]\n path = ' + str(included) + '\n')
        self.assertEqual(self.cli("-g"), "work\n")
        # An includeIf must be evaluated in the discovered repository context.
        config.write_text(f'[includeIf "gitdir:{self.repo}/.git"]\n path = {included}\n')
        self.assertEqual(self.cli("-g"), "work\n")
        with config.open("a") as stream:
            stream.write('[user]\n name = Work \\\nUser\n')
        self.assertEqual(self.cli("-g"), "work\n")

    def test_bad_json_and_invalid_git_config(self):
        path = self.home / ".config/gitusers.json"
        for data in ("[", "{}", '[{"name":12}]', '[] trailing'):
            path.write_text(data)
            self.cli("-l", code=1)
        self.write_users()
        (self.repo / ".git/config").write_text('[user]\n name = "unterminated\n')
        self.cli("-g", code=1)

    def test_prompt_tracked_states_rename_and_untracked(self):
        self.cli("work")
        file = self.repo / "tracked"
        file.write_text("original\n")
        self.commit()
        self.assertEqual(self.cli("-p"), self.prompt())
        (self.repo / "untracked").write_text("ignored\n")
        self.assertEqual(self.cli("-p"), self.prompt())
        file.write_text("changed\n")
        self.assertEqual(self.cli("-p"), self.prompt(post=" ☇1"))
        self.git("add", "tracked")
        self.assertEqual(self.cli("-p"), self.prompt(post=" ★"))
        file.write_text("changed again\n")
        self.assertEqual(self.cli("-p"), self.prompt(post=" ☇1★"))
        self.git("reset", "--hard", "HEAD")
        self.git("mv", "tracked", "new\nname")
        self.assertEqual(self.cli("-p"), self.prompt(post=" ★"))

    def test_unborn_detached_and_subdirectory(self):
        self.cli("work")
        self.assertEqual(self.cli("-p"), self.prompt())
        self.commit()
        nested = self.repo / "a/b/c"
        nested.mkdir(parents=True)
        self.assertEqual(self.cli("-g", cwd=nested), "work\n")
        self.git("checkout", "--detach")
        short = self.git("rev-parse", "--short", "HEAD").stdout.strip()
        self.assertEqual(self.cli("-p", cwd=nested), self.prompt(branch=":" + short))

    def test_stashes_today_only(self):
        self.cli("work")
        file = self.repo / "tracked"
        file.write_text("initial\n")
        self.commit()
        for days_ago in (1, 0):
            file.write_text(str(days_ago))
            date = datetime.datetime.now(datetime.timezone.utc) - datetime.timedelta(days=days_ago)
            env = dict(self.env, GIT_COMMITTER_DATE=date.isoformat())
            self.git("stash", "push", env=env)
        self.assertEqual(self.cli("-p"), self.prompt(post=" ≡1"))

    def test_upstream_divergence(self):
        self.cli("work")
        self.commit()
        self.git("branch", "upstream")
        self.git("branch", "--set-upstream-to", "upstream")
        self.git("config", "status.aheadBehind", "false")
        self.commit("ahead")
        self.assertEqual(self.cli("-p"), self.prompt(post=" ↑1"))
        self.git("checkout", "upstream")
        self.commit("behind")
        self.git("checkout", "main")
        self.assertEqual(self.cli("-p"), self.prompt(post=" ↑1↓1"))

    def test_reftable_stashes(self):
        result = self.git("refs", "migrate", "--ref-format=reftable", ok=False)
        if result.returncode:
            self.skipTest("Git lacks reftable migration support")
        self.cli("work")
        file = self.repo / "tracked"
        file.write_text("initial\n")
        self.commit()
        self.assertEqual(self.cli("-p"), self.prompt())
        file.write_text("stash me\n")
        self.git("stash", "push")
        self.assertEqual(self.cli("-p"), self.prompt(post=" ≡1"))

    def test_conflicted_prompt(self):
        self.cli("work")
        file = self.repo / "tracked"
        file.write_text("base\n")
        self.commit()
        self.git("branch", "other")
        file.write_text("main\n")
        self.commit("main change")
        self.git("checkout", "other")
        file.write_text("other\n")
        self.commit("other change")
        self.git("checkout", "main")
        self.git("merge", "other", ok=False)
        self.assertEqual(self.cli("-p"), self.prompt(post=" ☇1"))

    def test_linked_worktree_and_absolute_gitfile(self):
        self.cli("work")
        self.commit()
        linked = self.root / "linked"
        self.git("worktree", "add", "-b", "linked", str(linked))
        self.assertTrue((linked / ".git").read_text().startswith("gitdir: /"))
        self.assertEqual(self.cli("-g", cwd=linked), "work\n")
        self.assertEqual(self.cli("-p", cwd=linked), self.prompt(branch="linked"))
        self.git("config", "extensions.worktreeConfig", "true")
        self.git("config", "--worktree", "user.name", "Personal User", cwd=linked)
        self.git("config", "--worktree", "user.email", "me@example.com", cwd=linked)
        self.git("config", "--worktree", "core.sshCommand", self.ssh("personal"), cwd=linked)
        self.assertEqual(self.cli("-g", cwd=linked), "personal\n")
        self.assertEqual(self.cli("-g"), "work\n")
        self.cli("work", cwd=linked)
        self.assertEqual(self.cli("-g", cwd=linked), "work\n")
        self.assertEqual(self.cli("-g"), "work\n")

    def test_relative_submodule_gitfile_and_depth(self):
        self.cli("work")
        self.commit()
        parent = self.root / "parent"
        self.git("init", "-b", "main", str(parent))
        self.git("config", "user.name", "Parent", cwd=parent)
        self.git("config", "user.email", "parent@example.com", cwd=parent)
        self.git("-c", "protocol.file.allow=always", "submodule", "add", str(self.repo), "child", cwd=parent)
        child = parent / "child"
        self.cli("work", cwd=child)
        self.assertEqual(self.cli("-g", cwd=child), "work\n")
        self.assertEqual(self.cli("-p", cwd=child), self.prompt(depth=YELLOW + "²" + RESET))

    def test_clone_local_and_authentication_environment(self):
        self.cli("work")
        self.commit()
        destination = self.root / "cloned"
        self.cli("work", "clone", str(self.repo), str(destination), cwd=self.root)
        self.assertTrue((destination / ".git").is_dir())
        # Fake Git observes the exact argv and environment without using SSH.
        bindir = self.root / "bin"
        bindir.mkdir()
        fake = bindir / "git"
        fake.write_text('#!/usr/bin/env python3\nimport json, os, sys\n'
                        'print(json.dumps([sys.argv[1:], os.environ["GIT_SSH_COMMAND"], os.environ["SENTINEL"]]))\n')
        fake.chmod(0o755)
        self.env["PATH"] = str(bindir) + os.pathsep + self.env["PATH"]
        self.env["SENTINEL"] = "preserved"
        value = json.loads(self.cli("work", "clone", "ssh://example/repo", "target", "--depth=1", cwd=self.root))
        self.assertEqual(value, [["clone", "ssh://example/repo", "target", "--depth=1"], self.ssh(), "preserved"])

    def test_editor_without_config_and_exit_status(self):
        path = self.home / ".config/gitusers.json"
        path.unlink()
        editor = self.root / "editor"
        editor.write_text('#!/bin/sh\nprintf "%s" "$1"\nexit 7\n')
        editor.chmod(0o755)
        self.env["EDITOR"] = str(editor)
        self.assertEqual(self.cli("-e", code=7), str(path))

    def test_ssh_shell_quoting(self):
        key = "~/key with 'quotes' and $(touch injected)"
        self.users[0]["privkey"] = key
        self.users[0]["short"] = 'cash$money`id`"%'
        self.write_users()
        command = self.cli("-sshc", "Work User").strip()
        # Replace only the ssh executable to observe shell argument expansion.
        script = 'ssh() { printf "%s\\n" "$@"; }; ' + command
        output = subprocess.check_output(["/bin/sh", "-c", script], env=self.env, cwd=self.root, text=True)
        self.assertEqual(output.splitlines()[1], str(self.home) + "/" + key[2:])
        self.assertIn('cash$money`id`"%%-%C', output)
        self.assertFalse((self.root / "injected").exists())

    @unittest.skipUnless(REFERENCE, "set GITUSERS_REFERENCE to compare with the Go CLI")
    def test_reference_compatibility(self):
        for user in ("work", "personal"):
            self.cli(user)
            self.commit(user)
            for args in ((), ("-l",), ("-c",), ("-g",), ("-p",), ("-sshc", user)):
                with self.subTest(user=user, args=args):
                    self.assertEqual(self.cli(*args), self.cli(*args, binary=REFERENCE))
        file = self.repo / "tracked"
        file.write_text("initial\n")
        self.commit()
        for staged in (False, True):
            file.write_text("modified\n")
            if staged:
                self.git("add", "tracked")
            self.assertEqual(self.cli("-p"), self.cli("-p", binary=REFERENCE))
        self.git("reset", "--hard", "HEAD")
        file.write_text("stash\n")
        self.git("stash", "push")
        self.assertEqual(self.cli("-p"), self.cli("-p", binary=REFERENCE))
        self.git("checkout", "--detach")
        self.assertEqual(self.cli("-p"), self.cli("-p", binary=REFERENCE))


if __name__ == "__main__":
    unittest.main()
