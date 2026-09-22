package main

import (
	"io/ioutil"
	"os"
	"path/filepath"
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
