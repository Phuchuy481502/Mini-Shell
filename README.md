# Mini-Cell
A simple Unix shell in C — fork, exec, pipe, I/O redirect, background jobs

## Features

- Run external commands (`ls`, `grep`, etc.)
- Built-in `cd`, `exit`, `history`, `help`
- Pipe: `ls | grep .c`
- I/O redirect: `cmd > file`, `cmd < file`
- Background jobs: `cmd &`

## Build

```bash
gcc -Wall -o myshell shell.c
./myshell
```

Requires GCC on Linux/macOS or WSL on Windows.
## Demo
![Screenshot](Screenshot%202026-03-09%20113020.png)
