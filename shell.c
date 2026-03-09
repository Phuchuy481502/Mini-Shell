/*
 * ============================================================
 *  simple_shell.c — A minimal Unix shell for C beginners
 *  Features: command execution, cd, exit, pipes, I/O redirect,
 *            background jobs, command history
 *  Compile : gcc -Wall -o myshell shell.c
 *  Run     : ./myshell
 * ============================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <fcntl.h>
#include <errno.h>

/* ── Constants ─────────────────────────────────────────────── */
#define MAX_INPUT    1024   /* max chars per input line          */
#define MAX_ARGS       64   /* max number of arguments           */
#define MAX_HISTORY    20   /* number of history entries kept    */
#define PROMPT      "myshell> "

/* ── History storage ────────────────────────────────────────── */
static char history[MAX_HISTORY][MAX_INPUT];
static int  history_count = 0;

/* ============================================================
 *  add_history — save a command to history ring buffer
 * ============================================================ */
void add_history(const char *cmd) {
    if (strlen(cmd) == 0) return;
    /* shift entries when buffer is full */
    if (history_count == MAX_HISTORY) {
        for (int i = 0; i < MAX_HISTORY - 1; i++)
            strcpy(history[i], history[i + 1]);
        history_count--;
    }
    strncpy(history[history_count++], cmd, MAX_INPUT - 1);
}

/* ============================================================
 *  print_history — print all saved commands
 * ============================================================ */
void print_history(void) {
    for (int i = 0; i < history_count; i++)
        printf("  %3d  %s\n", i + 1, history[i]);
}

/* ============================================================
 *  parse_args — split a string into an argv[] array
 *  Returns number of tokens found.
 * ============================================================ */
int parse_args(char *line, char *args[]) {
    int argc = 0;
    char *token = strtok(line, " \t\n");
    while (token != NULL && argc < MAX_ARGS - 1) {
        args[argc++] = token;
        token = strtok(NULL, " \t\n");
    }
    args[argc] = NULL;   /* execvp requires NULL terminator */
    return argc;
}

/* ============================================================
 *  handle_redirect — set up I/O redirection before exec()
 *  Scans args[] for > (stdout) and < (stdin) operators,
 *  opens the file, calls dup2(), then removes the operator
 *  and filename from args[] so execvp doesn't see them.
 * ============================================================ */
void handle_redirect(char *args[], int *argc) {
    for (int i = 0; i < *argc; i++) {

        /* Output redirect:  command > file */
        if (strcmp(args[i], ">") == 0 && args[i + 1]) {
            int fd = open(args[i + 1],
                          O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd < 0) { perror("open"); exit(EXIT_FAILURE); }
            dup2(fd, STDOUT_FILENO);   /* replace stdout with file */
            close(fd);
            /* remove "> filename" from args */
            for (int j = i; j < *argc - 2; j++)
                args[j] = args[j + 2];
            *argc -= 2;
            args[*argc] = NULL;
            i--;
        }

        /* Input redirect:   command < file */
        else if (strcmp(args[i], "<") == 0 && args[i + 1]) {
            int fd = open(args[i + 1], O_RDONLY);
            if (fd < 0) { perror("open"); exit(EXIT_FAILURE); }
            dup2(fd, STDIN_FILENO);    /* replace stdin with file */
            close(fd);
            for (int j = i; j < *argc - 2; j++)
                args[j] = args[j + 2];
            *argc -= 2;
            args[*argc] = NULL;
            i--;
        }
    }
}

/* ============================================================
 *  run_piped — execute "cmd1 | cmd2"
 *  Creates one anonymous pipe, forks twice:
 *    child1 → writes to pipe write-end (its stdout)
 *    child2 → reads from pipe read-end (its stdin)
 * ============================================================ */
void run_piped(char *left[], char *right[]) {
    int pipefd[2];

    if (pipe(pipefd) < 0) { perror("pipe"); return; }

    pid_t pid1 = fork();
    if (pid1 == 0) {
        /* left side: stdout → pipe write end */
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[0]);
        close(pipefd[1]);
        execvp(left[0], left);
        perror(left[0]);
        exit(EXIT_FAILURE);
    }

    pid_t pid2 = fork();
    if (pid2 == 0) {
        /* right side: stdin ← pipe read end */
        dup2(pipefd[0], STDIN_FILENO);
        close(pipefd[1]);
        close(pipefd[0]);
        execvp(right[0], right);
        perror(right[0]);
        exit(EXIT_FAILURE);
    }

    /* parent closes both ends and waits for both children */
    close(pipefd[0]);
    close(pipefd[1]);
    waitpid(pid1, NULL, 0);
    waitpid(pid2, NULL, 0);
}

/* ============================================================
 *  execute — run a single command (with optional background)
 * ============================================================ */
void execute(char *args[], int argc, int background) {
    pid_t pid = fork();

    if (pid < 0) {
        perror("fork");
        return;
    }

    if (pid == 0) {
        /* ── child process ── */
        handle_redirect(args, &argc);   /* set up any redirection */
        execvp(args[0], args);
        /* execvp only returns on error */
        fprintf(stderr, "%s: command not found\n", args[0]);
        exit(EXIT_FAILURE);
    }

    /* ── parent process ── */
    if (background) {
        printf("[background] PID %d\n", pid);
        /* don't wait — let child run concurrently */
    } else {
        waitpid(pid, NULL, 0);
    }
}

/* ============================================================
 *  main — the REPL loop
 *  Read → Evaluate → Print → Loop
 * ============================================================ */
int main(void) {
    char  input[MAX_INPUT];
    char *args[MAX_ARGS];
    char *left[MAX_ARGS], *right[MAX_ARGS];

    printf("=== myshell — type 'help' for commands ===\n");

    while (1) {

        /* ── 1. Print prompt ── */
        printf(PROMPT);
        fflush(stdout);

        /* ── 2. Read input ── */
        if (fgets(input, sizeof(input), stdin) == NULL) {
            /* Ctrl+D (EOF) */
            printf("\nGoodbye!\n");
            break;
        }

        /* Strip trailing newline for history storage */
        input[strcspn(input, "\n")] = '\0';

        if (strlen(input) == 0) continue;   /* empty line */

        add_history(input);

        /* ── 3. Built-in: exit ── */
        if (strcmp(input, "exit") == 0 ||
            strcmp(input, "quit") == 0) {
            printf("Goodbye!\n");
            break;
        }

        /* ── 4. Built-in: help ── */
        if (strcmp(input, "help") == 0) {
            printf("\n  Built-in commands:\n");
            printf("    cd <dir>     Change directory\n");
            printf("    history      Show command history\n");
            printf("    help         Show this message\n");
            printf("    exit / quit  Exit the shell\n\n");
            printf("  Features:\n");
            printf("    cmd > file   Redirect stdout to file\n");
            printf("    cmd < file   Redirect stdin from file\n");
            printf("    cmd1 | cmd2  Pipe output to next command\n");
            printf("    cmd &        Run command in background\n\n");
            continue;
        }

        /* ── 5. Built-in: history ── */
        if (strcmp(input, "history") == 0) {
            print_history();
            continue;
        }

        /* ── 6. Detect pipe operator ── */
        char input_copy[MAX_INPUT];
        strncpy(input_copy, input, MAX_INPUT - 1);

        char *pipe_pos = strchr(input_copy, '|');
        if (pipe_pos) {
            *pipe_pos = '\0';          /* split into two halves */
            char *left_str  = input_copy;
            char *right_str = pipe_pos + 1;

            int la = parse_args(left_str,  left);
            int ra = parse_args(right_str, right);

            if (la > 0 && ra > 0)
                run_piped(left, right);
            continue;
        }

        /* ── 7. Parse normal command ── */
        /* work on a fresh copy since parse_args uses strtok */
        strncpy(input_copy, input, MAX_INPUT - 1);
        int argc = parse_args(input_copy, args);

        if (argc == 0) continue;

        /* ── 8. Detect background operator ── */
        int background = 0;
        if (argc > 0 && strcmp(args[argc - 1], "&") == 0) {
            background = 1;
            args[--argc] = NULL;       /* remove "&" from args   */
        }

        /* ── 9. Built-in: cd ── */
        if (strcmp(args[0], "cd") == 0) {
            if (args[1] == NULL) {
                /* cd with no arg → go to HOME */
                const char *home = getenv("HOME");
                if (home) chdir(home);
                else fprintf(stderr, "cd: HOME not set\n");
            } else if (chdir(args[1]) != 0) {
                perror("cd");
            }
            continue;
        }

        /* ── 10. Execute external command ── */
        execute(args, argc, background);

        /* Reap any finished background children (non-blocking) */
        while (waitpid(-1, NULL, WNOHANG) > 0);
    }

    return 0;
}
