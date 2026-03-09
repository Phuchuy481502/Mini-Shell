
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <fcntl.h>
#include <errno.h>

#define MAX_INPUT    1024   
#define MAX_ARGS       64  
#define MAX_HISTORY    20  
#define PROMPT      "myshell> "

static char history[MAX_HISTORY][MAX_INPUT];
static int  history_count = 0;

/*Save history*/
void add_history(const char *cmd) {
    if (strlen(cmd) == 0) return;
    if (history_count == MAX_HISTORY) {
        for (int i = 0; i < MAX_HISTORY - 1; i++)
            strcpy(history[i], history[i + 1]);
        history_count--;
    }
    strncpy(history[history_count++], cmd, MAX_INPUT - 1);
}

/*Print history*/
void print_history(void) {
    for (int i = 0; i < history_count; i++)
        printf("  %3d  %s\n", i + 1, history[i]);
}

/*Parse args*/
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

/*Handle redirect*/
void handle_redirect(char *args[], int *argc) {
    for (int i = 0; i < *argc; i++) {

        /*Command > file */
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

        /*Command < file */
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

/*Run piped*/
void run_piped(char *left[], char *right[]) {
    int pipefd[2];

    if (pipe(pipefd) < 0) { perror("pipe"); return; }

    pid_t pid1 = fork();
    if (pid1 == 0) {
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

    close(pipefd[0]);
    close(pipefd[1]);
    waitpid(pid1, NULL, 0);
    waitpid(pid2, NULL, 0);
}

/*Execute*/
void execute(char *args[], int argc, int background) {
    pid_t pid = fork();

    if (pid < 0) {
        perror("fork");
        return;
    }

    if (pid == 0) {
        handle_redirect(args, &argc); 
        execvp(args[0], args);
        fprintf(stderr, "%s: command not found\n", args[0]);
        exit(EXIT_FAILURE);
    }

    if (background) {
        printf("[background] PID %d\n", pid);
    } else {
        waitpid(pid, NULL, 0);
    }
}

/*Main*/
int main(void) {
    char  input[MAX_INPUT];
    char *args[MAX_ARGS];
    char *left[MAX_ARGS], *right[MAX_ARGS];

    printf("=== myshell — type 'help' for commands ===\n");

    while (1) {

        /*Print prompt*/
        printf(PROMPT);
        fflush(stdout);

        /*Read input*/
        if (fgets(input, sizeof(input), stdin) == NULL) {
            printf("\nGoodbye!\n");
            break;
        }

        input[strcspn(input, "\n")] = '\0';

        if (strlen(input) == 0) continue;   /* empty line */

        add_history(input);

        /*Built-in: exit*/
        if (strcmp(input, "exit") == 0 ||
            strcmp(input, "quit") == 0) {
            printf("Goodbye!\n");
            break;
        }

        /*Built-in: help*/
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

        /*Built-in: history*/
        if (strcmp(input, "history") == 0) {
            print_history();
            continue;
        }

        /*Detect pipe operator*/
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

        /*Parse normal command*/
        strncpy(input_copy, input, MAX_INPUT - 1);
        int argc = parse_args(input_copy, args);

        if (argc == 0) continue;

        /*Detect background operator*/
        int background = 0;
        if (argc > 0 && strcmp(args[argc - 1], "&") == 0) {
            background = 1;
            args[--argc] = NULL;       /* remove "&" from args   */
        }

        /*Built-in: cd*/
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

        /*Execute external command*/
        execute(args, argc, background);

        while (waitpid(-1, NULL, WNOHANG) > 0);
    }

    return 0;
}
