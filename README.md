
# Custom POSIX Shell

A simple POSIX-compatible command-line shell written in C.

## Features

- `myshell>` prompt
- built-in `cd` and `exit`
- background execution with `&`
- output redirection using `>`
- piping using `|`

## Requirements

- POSIX-compatible environment (WSL, Linux, macOS)
- GCC or another POSIX C compiler
- `make` for easy build

## Build

```bash
make
Or directly:
gcc -Wall -Wextra -std=c99 -pedantic -o shell shell.c
Examples:
# run an external command
myshell> ls -l

# change directory
myshell> cd /tmp

# exit the shell
myshell> exit

# background execution
myshell> sleep 5 &

# output redirection
myshell> echo hello > out.txt

# piping
myshell> ls -l | grep .c
Notes
shell is a simple demonstration shell and does not implement all shell features.
Use WSL if you are on Windows, since this project depends on POSIX APIs like fork(), execvp(), and pipe().
