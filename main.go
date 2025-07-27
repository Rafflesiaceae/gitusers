package main

// @TODO support list all users with author syntax, e.g.: "Author Name <email@address.com>"
// @TODO change remote urls to user urls, according to some funky scheme e.g. github.com => github-author-name.com

import (
	"bufio"
	"encoding/json"
	"fmt"
	"log"
	"os"
	"os/exec"
	"os/user"
	"path"
	"path/filepath"
	"sort"
	"strconv"
	"strings"
)

const (
	// SSHWrapper a wrapper around ssh that uses the env var SSH_IDENTITY_FILE for the -i param, needed for `clone`
	SSHWrapper = "ssh-i-from-env"
	// SSHWrapperInstruction a text that's printed in case the wrapper is missing
	SSHWrapperInstruction = `the wrapper: 'ssh-i-from-env' is missing!

create it according to the following template and add it to your path:

#!/bin/bash
ssh -i "$SSH_IDENTITY_FILE" $*
`
)

type GitConfig struct {
	User
	// either LOCAL or GLOBAL
	Source     string
	SshCommand string
}

type User struct {
	Short   string `json:"short"`
	Name    string `json:"name"`
	Email   string `json:"email"`
	PrivKey string `json:"privkey"`
}
type Users []User

type UserStatusFlag int

const (
	UserStatusEmpty UserStatusFlag = iota
	UserStatusFound
	UserStatusUnknown
	UserStatusNoGitDir
)

type UserStatus struct {
	status UserStatusFlag
	name   string
	short  string
}

var gitusersCfgPath string

func getDefinedGitUsers(path string) (resultPtr *Users, err error) {
	var result Users
	gitusersCfgPath = path

	contents, err := os.ReadFile(path)
	if err != nil {
		return nil, err
	}

	err = json.Unmarshal(contents, &result)
	if err != nil {
		return nil, err
	}

	sort.Slice(result, func(i, j int) bool {
		return result[i].Short < result[j].Short
	})

	return &result, nil
}

// try to load git user from given path
func getGitConfig(fpath string) (result *GitConfig, err error) {
	result = &GitConfig{}
	rawGitConfig, err := os.ReadFile(fpath)
	if err != nil {
		return nil, err
	}

	splitEquals := func(line string) (lhs string, rhs string, err error) {
		words := strings.SplitN(line, "=", 2)
		if len(words) != 2 {
			return "", "", fmt.Errorf("failed to split '%s' into 2 words through '='", line)
		}

		lhs = strings.TrimSpace(words[0])
		rhs = strings.TrimSpace(words[1])
		return lhs, rhs, nil

	}

	var userLines = make([]string, 0)
	var sshCommand = ""
	{ // iterate over lines of local git config
		foundUserBlock := false
		scanner := bufio.NewScanner(strings.NewReader(string(rawGitConfig)))
		for scanner.Scan() {
			text := scanner.Text()
			stext := strings.TrimSpace(text)
			if strings.HasPrefix(stext, ";") { // skip commments
				continue
			}

			if strings.Contains(text, `sshCommand =`) {
				_, rhs, err := splitEquals(text)
				if err != nil {
					return nil, err
				}

				sshCommand = rhs
				continue
			}

			if foundUserBlock {
				if strings.HasPrefix(text, "[") {
					break
				} else {
					userLines = append(userLines, text)
				}
			} else {
				if text == "[user]" {
					foundUserBlock = true
				}
			}
		}
		if err := scanner.Err(); err != nil {
			return nil, err
		}
	}

	if len(userLines) == 0 {
		return nil, nil
	}

	result.SshCommand = sshCommand

	for _, line := range userLines {
		lhs, rhs, err := splitEquals(line)
		if err != nil {
			return nil, err
		}

		switch lhs {
		case "name":
			result.Name = rhs
			result.Short = rhs
		case "email":
			result.Email = rhs
		default:
			return nil, fmt.Errorf("unsupported user key %s in line: %s", lhs, line)
		}
	}

	return result, nil
}

func exitError() {
	fmt.Printf("%%{$fg[red]%%}%s%%{${reset_color}%%}", "!")
	os.Exit(1)
}

func main() {
	var definedUsers *Users
	var homeDir string

	{ // load definedUsers
		user, err := user.Current()
		check(err)
		homeDir = user.HomeDir

		definedUsers, err = getDefinedGitUsers(path.Join(homeDir, ".config", "gitusers.json"))
		check(err)
	}

	var assertGitDir func() error
	var gitDir string
	{ // walk upwards finding .git dir
		wd, err := os.Getwd()
		check(err)

		originalWd := wd

		var prepath string

	walkUpwards:
		for ; prepath != wd; wd = filepath.Dir(wd) {
			prepath = wd

			files, err := os.ReadDir(wd)
			check(err)

			for _, f := range files {
				if f.Name() == ".git" {
					gitDir = path.Join(wd, f.Name())

					// check if file is dir
					fi, err := os.Stat(gitDir)
					check(err)

					if !fi.IsDir() { // isFile
						// assume its a submodule and follow the contents written therein
						gitDirLinkRaw, err := os.ReadFile(gitDir)
						check(err)

						gitDirLink := strings.TrimLeft(strings.TrimSpace(string(gitDirLinkRaw)), "gitdir: ")
						gitDirParent := filepath.Dir(gitDir)
						gitDir = filepath.Clean(path.Join(gitDirParent, string(gitDirLink)))
					}

					break walkUpwards
				}
			}
		}

		assertGitDir = func() error {
			if gitDir == "" {
				return fmt.Errorf("could not find .gitdir in %s", originalWd)
			}
			return nil
		}
	}

	var cfg *GitConfig
	if gitDir != "" { // load current from either local or global git config
		var err error
		// try local first
		cfg, err = getGitConfig(path.Join(gitDir, "config"))
		if err != nil {
			exitError()
		}

		if cfg != nil {
			cfg.Source = "LOCAL"
		} else { // couldn't find local
			// @TODO support other possible gitconfig paths
			cfg, err = getGitConfig(path.Join(homeDir, ".gitconfig"))
			check(err)

			if cfg != nil {
				cfg.Source = "GLOBAL"
			}
		}
	}

	expectedSshCommand := func(user *User) string {
		if user.PrivKey != "" {
			return fmt.Sprintf(`ssh -i %s -o IdentitiesOnly=yes`, user.PrivKey)
		} else {
			return fmt.Sprintf(`ssh`)
		}
	}

	queryUserStatus := func() UserStatus {
		err := assertGitDir()
		if err != nil {
			return UserStatus{status: UserStatusNoGitDir}
		}

		if cfg == nil {
			return UserStatus{status: UserStatusEmpty}
		}

		// check if we know the current user
		for _, defUser := range *definedUsers {
			if cfg.Name == defUser.Name &&
				cfg.Email == defUser.Email &&
				cfg.SshCommand == expectedSshCommand(&defUser) {
				return UserStatus{status: UserStatusFound, name: defUser.Short, short: defUser.Short}
			}
		}

		// so we don't know the current user
		return UserStatus{status: UserStatusUnknown, name: cfg.Short, short: cfg.Short}
	}

	// @TODO CLI autocompl
	{ // check or set user
		args := os.Args[1:]

		if len(args) == 0 { // summary
			userStatus := queryUserStatus()

			listDefinedUserShorts := ""
			for _, u := range *definedUsers {
				short := u.Short
				if (userStatus.status == UserStatusFound) && (u.Short == userStatus.short) {
					short = "→ " + short
				}
				listDefinedUserShorts += fmt.Sprintf("%14s (%s)\n", short, u.Name)
			}
			fmt.Printf("List of available Users:\n%s", listDefinedUserShorts)
		} else if len(args) == 1 && args[0] == "-c" { // check
			userStatus := queryUserStatus()
			switch userStatus.status {
			case UserStatusEmpty:
				fmt.Printf("%%{$fg[red]%%}%s", "NONE")
				os.Exit(0)
			case UserStatusFound:
				fmt.Print(userStatus.name)
				os.Exit(0)
			case UserStatusUnknown:
				fmt.Printf("%%{$fg[red]%%}%s", userStatus.name)
			}
			os.Exit(0)
		} else if len(args) == 1 &&
			!strings.HasPrefix(args[0], "-") {

			err := assertGitDir()
			check(err)

			setUser := args[0]
			for _, defUser := range *definedUsers { // set
				if defUser.Short == setUser ||
					defUser.Name == setUser ||
					defUser.Email == setUser {

					ret, _, serr := runEnv("git", []string{"config", "user.name", defUser.Name}, []string{})
					if ret != 0 {
						panic(serr)
					}

					ret, _, serr = runEnv("git", []string{"config", "user.email", defUser.Email}, []string{})
					if ret != 0 {
						panic(serr)
					}

					ret, _, serr = runEnv("git", []string{"config", "core.sshCommand", expectedSshCommand(&defUser)}, []string{})
					if ret != 0 {
						panic(serr)
					}

					os.Exit(0)
				}
			}

			// we could not match setUser with anything
			listDefinedUsers := ""
			for _, u := range *definedUsers {
				listDefinedUsers += fmt.Sprintf("%v\n", u)
			}
			log.Fatalf("could not find a defined user matching '%s', defined users:\n\n%s", setUser, listDefinedUsers)
		} else if len(args) == 1 && args[0] == "-h" { // help
			println(`Usage:
  gitusers <user-short>

Options:
  -h      Show this help
  -c      Check current set user
  -e      Open 'gitusers.json' config in your $EDITOR
  -l      List all known Users (w. user-short)
  -g      Return current user
  -p      Print the zsh RPROMPT`)
			os.Exit(0)
		} else if len(args) == 1 && args[0] == "-l" { // list
			err := assertGitDir()
			check(err)

			for _, user := range *definedUsers {
				fmt.Printf("%v\n", user)
			}
		} else if len(args) == 1 && args[0] == "-e" { // edit
			editor := os.Getenv("EDITOR")
			if editor == "" { // default
				editor = "vi"
			}

			cmd := exec.Command(editor, gitusersCfgPath)

			cmd.Stdout = os.Stdout
			cmd.Stderr = os.Stderr

			err := cmd.Run()
			check(err)
		} else if len(args) == 1 && args[0] == "-g" { // get
			userStatus := queryUserStatus()
			switch userStatus.status {
			case UserStatusFound:
				fmt.Println(userStatus.name)
				os.Exit(0)
			default:
				os.Exit(1)
			}
			os.Exit(0)
		} else if len(args) == 1 && args[0] == "-p" { // prompt
			outCloseParen := "%{$fg[yellow]%})%{${reset_color}%}"
			userStatus := queryUserStatus()
			if userStatus.status == UserStatusNoGitDir {
				os.Exit(0)
			}
			fmt.Print("%{$fg[yellow]%}(%{${reset_color}%}")
			switch userStatus.status {
			case UserStatusEmpty:
				fmt.Printf("%%{$fg[red]%%}%s", "NONE")
			case UserStatusFound:
				fmt.Print(userStatus.name)
			case UserStatusUnknown:
				fmt.Printf("%%{$fg[red]%%}%s", userStatus.name)
			}

			fmt.Print("%{$fg[yellow]%};%{${reset_color}%}")
			gitStatus := fetchGitStatus(":")
			if gitStatus == nil {
				fmt.Print("%{$fg[red]%}detached HEAD" + outCloseParen)
				os.Exit(0)
			}
			fmt.Print("%{$fg[magenta]%}" + gitStatus.branch + "%{${reset_color}%}")
			if gitStatus.depth > 1 {
				fmt.Print("%{$fg[yellow]%}" + DigitsToSuperscript(strconv.Itoa(gitStatus.depth)) + "%{${reset_color}%}")
			}

			{ // post
				var post string
				{ // collect post
					if gitStatus.ahead > 0 {
						post += "↑" + strconv.Itoa(gitStatus.ahead)
					}
					if gitStatus.behind > 0 {
						post += "↓" + strconv.Itoa(gitStatus.behind)
					}
					if gitStatus.stashes > 0 {
						post += "≡" + strconv.Itoa(gitStatus.stashes)
					}
					if gitStatus.changed > 0 {
						post += "☇" + strconv.Itoa(gitStatus.changed)
					}
					if gitStatus.staged > 0 {
						post += "★"
					}
				}

				if len(post) > 0 {
					fmt.Print(" " + post)
				}
			}

			fmt.Print(outCloseParen)
			os.Exit(0)
		} else if len(args) >= 3 && args[1] == "clone" { // <user> clone ...
			user := args[0]
			src := args[2]
			restargs := args[3:]
			for _, defUser := range *definedUsers { // set
				if defUser.Short == user ||
					defUser.Name == user ||
					defUser.Email == user {

					_, err := exec.LookPath(SSHWrapper)
					if err != nil {
						log.Fatalf(SSHWrapperInstruction)
					}

					cloneArgs := []string{fmt.Sprintf("GIT_SSH=%s", "ssh-i-from-env")}
					if defUser.PrivKey != "" {
						cloneArgs = append(cloneArgs, fmt.Sprintf("SSH_IDENTITY_FILE=%s", defUser.PrivKey))
					}

					ret, _, serr := runEnv("git", append([]string{"clone", src}, restargs...), cloneArgs)
					if ret != 0 {
						panic(serr)
					}

					os.Exit(0)
				}
			}

			log.Fatalf("could not find a defined user matching %s, defined users: %v", user, definedUsers)
		} else {
			panic("unsupported argument")
		}
	}
}
