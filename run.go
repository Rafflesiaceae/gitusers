package main

import (
	"bytes"
	"log"
	"os"
	"os/exec"
)

func run(cmdArg string, args ...string) (retCode int, outStr string, errStr string) {
	cmd := exec.Command(cmdArg, args...)

	var stdoutBuf, stderrBuf bytes.Buffer
	cmd.Stdout = &stdoutBuf
	cmd.Stderr = &stderrBuf

	err := cmd.Run()
	outStr, errStr = string(stdoutBuf.Bytes()), string(stderrBuf.Bytes())
	if err != nil {
		if exitError, ok := err.(*exec.ExitError); ok {
			retCode = exitError.ExitCode()
			return
		}
	}

	return
}

func runCheck(cmdArg string, args ...string) (outStr string, errStr string) {
	retCode, outStr, errStr := run(cmdArg, args...)
	if retCode != 0 {
		log.Fatalf("%s %v failed with retCode %d", cmdArg, args, retCode)
	}
	return outStr, errStr
}

func runEnv(cmdArg string, args []string, env []string) (retCode int, outStr string, errStr string) {
	cmd := exec.Command(cmdArg, args...)
	if len(env) != 0 {
		cmd.Env = os.Environ()
		for k, v := range env {
			cmd.Env[k] = v
		}
	}

	var stdoutBuf, stderrBuf bytes.Buffer
	cmd.Stdout = &stdoutBuf
	cmd.Stderr = &stderrBuf

	err := cmd.Run()
	outStr, errStr = string(stdoutBuf.Bytes()), string(stderrBuf.Bytes())
	if err != nil {
		if exitError, ok := err.(*exec.ExitError); ok {
			retCode = exitError.ExitCode()
			return
		}
	}

	return
}
