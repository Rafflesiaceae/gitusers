package main

import (
	"fmt"
	"os"
	"strings"
	"time"
)

type GitStatus struct {
	branch    string
	ahead     int
	behind    int
	staged    int
	conflicts int
	changed   int
	untracked int
	// number of stashes created today
	stashes int
}

func queryGitStashCountToday() (result int) {
	stashesRaw, _ := runCheck("git", "stash", "list", "--date=iso")
	if stashesRaw == "" {
		return 0
	}
	stashes := strings.Split(strings.TrimSpace(stashesRaw), "\n")
	today := time.Now().Format("2006-01-02")
	for _, s := range stashes { // filter for today's stashes
		// extracts date out of the git stash output
		// e.g.:
		// stash@{2021-05-28 12:02:24 +0200}: WIP on master: 153225e add README
		stashDate := s[7 : 7+10]
		if today == stashDate {
			result++
		}
	}
	return
}

func fetchGitStatus(prehash string) *GitStatus {
	var branch string
	{ // get branch
		retCode, branchRaw, _ := run("git", "symbolic-ref", "HEAD")
		if retCode == 0 {
			branch = branchRaw[11:]
			branch = strings.TrimSpace(branch)
		}

	}

	var changedFiles []byte
	var stagedFiles []byte
	{
		_, res, err := run("git", "diff", "--name-status")
		if strings.Contains(err, "fatal") {
			os.Exit(0)
		}

		changedFiles = make([]byte, 0)
		for _, line := range strings.Split(res, "\n") {
			if len(line) > 0 {
				changedFiles = append(changedFiles, line[0])
			}
		}

		res, _ = runCheck("git", "diff", "--staged", "--name-status")

		stagedFiles = make([]byte, 0)
		for _, line := range strings.Split(res, "\n") {
			if len(line) > 0 {
				stagedFiles = append(stagedFiles, line[0])
			}
		}
	}

	nbChanged := len(changedFiles) - strings.Count(string(changedFiles), "U")
	nbU := strings.Count(string(stagedFiles), "U")
	nbStaged := len(stagedFiles) - nbU
	conflicts := nbU
	changed := nbChanged

	var nbUntracked int
	{
		res, _ := runCheck("git", "status", "--porcelain")
		for _, status := range strings.Split(res, "\n") {
			if strings.HasPrefix(status, "??") {
				nbUntracked++
			}
		}
	}

	ahead := 0
	behind := 0

	if branch == "" {
		res, _ := runCheck("git", "rev-parse", "--short", "HEAD")
		branch = prehash + res[:len(res)-1]
	} else {
		_, remoteNameRaw, _ := run("git", "config", fmt.Sprintf("branch.%s.remote", branch))
		remoteName := strings.TrimSpace(remoteNameRaw)
		if remoteName != "" {
			mergeNameRaw, _ := runCheck("git", "config", fmt.Sprintf("branch.%s.merge", branch))
			mergeName := strings.TrimSpace(mergeNameRaw)
			var remoteRef string
			if remoteName == "." {
				remoteRef = mergeName
			} else {
				remoteRef = fmt.Sprintf("refs/remotes/%s/%s", remoteName, mergeName[11:])
			}

			retCode, revList, _ := run("git", "rev-list", "--left-right", fmt.Sprintf("%s...HEAD", remoteRef))
			if retCode != 0 {
				revList, _ = runCheck("git", "rev-list", "--left-right", fmt.Sprintf("%s...HEAD", mergeName))
			}

			if len(revList) > 0 {
				behead := strings.Split(strings.TrimSpace(revList), "\n")
				ahead = 0
				for _, v := range behead {
					if len(v) < 1 {
						continue
					}
					if v[0] == '>' {
						ahead++
					}
				}

				behind = len(behead) - ahead
			}
		}
	}

	return &GitStatus{
		ahead:     ahead,
		behind:    behind,
		branch:    branch,
		changed:   changed,
		conflicts: conflicts,
		staged:    nbStaged,
		untracked: nbUntracked,
		stashes:   queryGitStashCountToday(),
	}
}
