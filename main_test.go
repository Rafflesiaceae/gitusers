package main

import (
	"io/ioutil"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func temporaryDirectory(t *testing.T) string {
	t.Helper()

	directory, err := ioutil.TempDir("", "gitusers-test-")
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() {
		if err := os.RemoveAll(directory); err != nil {
			t.Error(err)
		}
	})

	return directory
}

func TestSSHCommandForUser(t *testing.T) {
	tests := []struct {
		name     string
		user     User
		expected string
	}{
		{
			name:     "private key",
			user:     User{Short: "work", PrivKey: "~/.ssh/work"},
			expected: `ssh -i ~/.ssh/work -o IdentitiesOnly=yes -o ControlPath="$HOME/.ssh/cm/work-%C"`,
		},
		{
			name:     "default key",
			user:     User{Short: "personal"},
			expected: `ssh -o ControlPath="$HOME/.ssh/cm/personal-%C"`,
		},
	}

	for _, test := range tests {
		t.Run(test.name, func(t *testing.T) {
			actual := sshCommandForUser(&test.user)
			if actual != test.expected {
				t.Fatalf("expected %q, got %q", test.expected, actual)
			}
		})
	}
}

func TestUpgradePathMatchesLegacySshCommand(t *testing.T) {
	tests := []struct {
		name    string
		user    User
		cfg     GitConfig
		matches bool
	}{
		{
			name:    "legacy with private key matches",
			user:    User{Short: "work", PrivKey: "~/.ssh/work"},
			cfg:     GitConfig{SshCommand: `ssh -i ~/.ssh/work -o IdentitiesOnly=yes`},
			matches: true,
		},
		{
			name:    "legacy without private key matches",
			user:    User{Short: "personal"},
			cfg:     GitConfig{SshCommand: "ssh"},
			matches: true,
		},
		{
			name:    "already-current value does not match",
			user:    User{Short: "work", PrivKey: "~/.ssh/work"},
			cfg:     GitConfig{SshCommand: sshCommandForUser(&User{Short: "work", PrivKey: "~/.ssh/work"})},
			matches: false,
		},
	}

	for _, test := range tests {
		t.Run(test.name, func(t *testing.T) {
			actual := upgradePaths[0].matches(&test.cfg, &test.user)
			if actual != test.matches {
				t.Fatalf("expected matches=%v, got %v", test.matches, actual)
			}
		})
	}
}

func TestTryUpgradeGitConfigRewritesLegacySshCommand(t *testing.T) {
	repoDir := temporaryDirectory(t)

	if _, _, errStr := run("git", "-C", repoDir, "init"); errStr != "" {
		t.Fatalf("git init failed: %s", errStr)
	}

	user := User{Short: "work", PrivKey: "~/.ssh/work"}
	legacySshCommand := `ssh -i ~/.ssh/work -o IdentitiesOnly=yes`

	if _, _, errStr := run("git", "-C", repoDir, "config", "core.sshCommand", legacySshCommand); errStr != "" {
		t.Fatalf("git config failed: %s", errStr)
	}

	originalWd, err := os.Getwd()
	if err != nil {
		t.Fatal(err)
	}
	if err := os.Chdir(repoDir); err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() {
		if err := os.Chdir(originalWd); err != nil {
			t.Error(err)
		}
	})

	cfg := &GitConfig{Source: "LOCAL", SshCommand: legacySshCommand}

	if upgraded := tryUpgradeGitConfig(cfg, &user); !upgraded {
		t.Fatal("expected tryUpgradeGitConfig to report a successful upgrade")
	}

	expected := sshCommandForUser(&user)
	if cfg.SshCommand != expected {
		t.Fatalf("expected cfg.SshCommand to be updated to %q, got %q", expected, cfg.SshCommand)
	}

	onDisk, _ := runCheck("git", "config", "core.sshCommand")
	if strings.TrimSpace(onDisk) != expected {
		t.Fatalf("expected on-disk core.sshCommand to be %q, got %q", expected, strings.TrimSpace(onDisk))
	}
}

func TestGetGitConfigDecodesQuotedControlPath(t *testing.T) {
	configPath := filepath.Join(temporaryDirectory(t), "config")
	contents := `[core]
	sshCommand = ssh -i ~/.ssh/work -o IdentitiesOnly=yes -o ControlPath=\"$HOME/.ssh/cm/work-%C\"
[user]
	name = Work User
	email = work@example.com
`
	if err := os.WriteFile(configPath, []byte(contents), 0600); err != nil {
		t.Fatal(err)
	}

	config, err := getGitConfig(configPath)
	if err != nil {
		t.Fatal(err)
	}
	if config == nil {
		t.Fatal("expected a parsed git config")
	}

	expected := `ssh -i ~/.ssh/work -o IdentitiesOnly=yes -o ControlPath="$HOME/.ssh/cm/work-%C"`
	if config.SshCommand != expected {
		t.Fatalf("expected %q, got %q", expected, config.SshCommand)
	}
}
