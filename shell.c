#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define SHELL_PROMPT "myshell> "
#define MAX_TOKENS 128
#define READ_END 0
#define WRITE_END 1

/*
 * A small helper structure to hold parsed command state.
 */
struct command {
    char **argv1;
    int argc1;
    char **argv2;
    int argc2;
    char *outfile;
    bool background;
    bool has_pipe;
};

static void sigchld_handler(int signo)
{
    (void)signo;
    int saved_errno = errno;
    while (waitpid(-1, NULL, WNOHANG) > 0) {
        ;
    }
    errno = saved_errno;
}

static void install_sigchld_handler(void)
{
    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_handler = sigchld_handler;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_RESTART | SA_NOCLDSTOP;

    if (sigaction(SIGCHLD, &action, NULL) == -1) {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }
}

static char *str_duplicate(const char *text)
{
    char *copy = strdup(text);
    if (!copy) {
        perror("strdup");
        exit(EXIT_FAILURE);
    }
    return copy;
}

static char **build_token_array(char **tokens, int count)
{
    char **array = malloc((count + 1) * sizeof(char *));
    if (!array) {
        perror("malloc");
        exit(EXIT_FAILURE);
    }
    for (int i = 0; i < count; ++i) {
        array[i] = tokens[i];
    }
    array[count] = NULL;
    return array;
}

static char **tokenize_line(const char *line, int *out_count)
{
    char *copy = str_duplicate(line);
    char *cursor = copy;
    char *tokens[MAX_TOKENS];
    int count = 0;

    while (*cursor != '\0') {
        while (*cursor != '\0' && (*cursor == ' ' || *cursor == '\t' || *cursor == '\n')) {
            cursor++;
        }
        if (*cursor == '\0') {
            break;
        }

        if (*cursor == '>' || *cursor == '|' || *cursor == '&') {
            if (count >= MAX_TOKENS - 1) {
                fprintf(stderr, "Too many tokens\n");
                break;
            }
            tokens[count++] = str_duplicate((char[]){*cursor, '\0'});
            cursor++;
            continue;
        }

        char *start = cursor;
        while (*cursor != '\0' && *cursor != ' ' && *cursor != '\t' && *cursor != '\n' && *cursor != '>' && *cursor != '|' && *cursor != '&') {
            cursor++;
        }

        size_t len = (size_t)(cursor - start);
        char *token = malloc(len + 1);
        if (!token) {
            perror("malloc");
            exit(EXIT_FAILURE);
        }
        memcpy(token, start, len);
        token[len] = '\0';
        tokens[count++] = token;
    }

    free(copy);
    *out_count = count;
    return build_token_array(tokens, count);
}

static void free_argv(char **argv)
{
    if (!argv) {
        return;
    }
    for (char **p = argv; *p != NULL; ++p) {
        free(*p);
    }
    free(argv);
}

static void free_command(struct command *cmd)
{
    if (!cmd) {
        return;
    }
    free_argv(cmd->argv1);
    free_argv(cmd->argv2);
    free(cmd->outfile);
}

static void free_token_list(char **tokens, int count)
{
    if (!tokens) {
        return;
    }
    for (int i = 0; i < count; ++i) {
        free(tokens[i]);
    }
}

static bool parse_tokens(char **tokens, int count, struct command *cmd)
{
    cmd->argv1 = NULL;
    cmd->argc1 = 0;
    cmd->argv2 = NULL;
    cmd->argc2 = 0;
    cmd->outfile = NULL;
    cmd->background = false;
    cmd->has_pipe = false;

    if (count == 0) {
        return true;
    }

    if (strcmp(tokens[count - 1], "&") == 0) {
        cmd->background = true;
        free(tokens[count - 1]);
        count -= 1;
    }

    int pipe_index = -1;
    for (int i = 0; i < count; ++i) {
        if (strcmp(tokens[i], "|") == 0) {
            pipe_index = i;
            break;
        }
    }

    if (pipe_index >= 0) {
        cmd->has_pipe = true;
        if (pipe_index == 0 || pipe_index == count - 1) {
            fprintf(stderr, "Syntax error: invalid pipe placement\n");
            free_token_list(tokens, count);
            return false;
        }
    }

    int redir_pos = -1;
    for (int i = 0; i < count; ++i) {
        if (strcmp(tokens[i], ">") == 0) {
            redir_pos = i;
            break;
        }
    }

    if (redir_pos >= 0) {
        if (redir_pos == count - 1) {
            fprintf(stderr, "Syntax error: missing output file after '>'\n");
            free_token_list(tokens, count);
            return false;
        }
        cmd->outfile = str_duplicate(tokens[redir_pos + 1]);
        free(tokens[redir_pos]);
        free(tokens[redir_pos + 1]);
        for (int i = redir_pos; i < count - 2; ++i) {
            tokens[i] = tokens[i + 2];
        }
        count -= 2;
        if (pipe_index > redir_pos) {
            pipe_index -= 2;
        }
    }

    if (cmd->has_pipe) {
        int left_count = pipe_index;
        int right_count = count - pipe_index - 1;
        if (left_count <= 0 || right_count <= 0) {
            fprintf(stderr, "Syntax error: invalid pipe operands\n");
            free_token_list(tokens, count);
            return false;
        }
        cmd->argv1 = build_token_array(tokens, left_count);
        cmd->argc1 = left_count;
        cmd->argv2 = build_token_array(tokens + pipe_index + 1, right_count);
        cmd->argc2 = right_count;
        free(tokens[pipe_index]);
    } else {
        cmd->argv1 = build_token_array(tokens, count);
        cmd->argc1 = count;
    }

    return true;
}

static bool execute_builtin(struct command *cmd)
{
    if (cmd->argc1 == 0 || !cmd->argv1) {
        return false;
    }

    if (strcmp(cmd->argv1[0], "exit") == 0) {
        exit(EXIT_SUCCESS);
    }

    if (strcmp(cmd->argv1[0], "cd") == 0) {
        const char *target = cmd->argv1[1];
        if (!target) {
            target = getenv("HOME");
            if (!target) {
                target = "/";
            }
        }
        if (chdir(target) == -1) {
            perror("cd");
        }
        return true;
    }

    return false;
}

static void redirect_output(const char *outfile)
{
    int fd = open(outfile, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd == -1) {
        perror("open");
        exit(EXIT_FAILURE);
    }
    if (dup2(fd, STDOUT_FILENO) == -1) {
        perror("dup2");
        close(fd);
        exit(EXIT_FAILURE);
    }
    close(fd);
}

static void execute_simple_command(struct command *cmd)
{
    pid_t pid = fork();
    if (pid == -1) {
        perror("fork");
        return;
    }

    if (pid == 0) {
        if (cmd->outfile) {
            redirect_output(cmd->outfile);
        }
        execvp(cmd->argv1[0], cmd->argv1);
        perror(cmd->argv1[0]);
        _exit(EXIT_FAILURE);
    }

    if (!cmd->background) {
        int status;
        if (waitpid(pid, &status, 0) == -1) {
            perror("waitpid");
        }
    } else {
        printf("[background] pid=%d\n", pid);
    }
}

static void execute_piped_command(struct command *cmd)
{
    int pipe_fds[2];
    if (pipe(pipe_fds) == -1) {
        perror("pipe");
        return;
    }

    pid_t left_pid = fork();
    if (left_pid == -1) {
        perror("fork");
        close(pipe_fds[READ_END]);
        close(pipe_fds[WRITE_END]);
        return;
    }

    if (left_pid == 0) {
        if (dup2(pipe_fds[WRITE_END], STDOUT_FILENO) == -1) {
            perror("dup2");
            _exit(EXIT_FAILURE);
        }
        close(pipe_fds[READ_END]);
        close(pipe_fds[WRITE_END]);
        execvp(cmd->argv1[0], cmd->argv1);
        perror(cmd->argv1[0]);
        _exit(EXIT_FAILURE);
    }

    pid_t right_pid = fork();
    if (right_pid == -1) {
        perror("fork");
        close(pipe_fds[READ_END]);
        close(pipe_fds[WRITE_END]);
        return;
    }

    if (right_pid == 0) {
        if (dup2(pipe_fds[READ_END], STDIN_FILENO) == -1) {
            perror("dup2");
            _exit(EXIT_FAILURE);
        }
        close(pipe_fds[WRITE_END]);
        close(pipe_fds[READ_END]);
        if (cmd->outfile) {
            redirect_output(cmd->outfile);
        }
        execvp(cmd->argv2[0], cmd->argv2);
        perror(cmd->argv2[0]);
        _exit(EXIT_FAILURE);
    }

    close(pipe_fds[READ_END]);
    close(pipe_fds[WRITE_END]);

    if (!cmd->background) {
        if (waitpid(left_pid, NULL, 0) == -1) {
            perror("waitpid");
        }
        if (waitpid(right_pid, NULL, 0) == -1) {
            perror("waitpid");
        }
    } else {
        printf("[background] pid=%d,%d\n", left_pid, right_pid);
    }
}

int main(void)
{
    install_sigchld_handler();

    char *line = NULL;
    size_t line_size = 0;

    while (true) {
        printf("%s", SHELL_PROMPT);
        fflush(stdout);

        ssize_t bytes_read = getline(&line, &line_size, stdin);
        if (bytes_read == -1) {
            if (feof(stdin)) {
                putchar('\n');
                break;
            }
            perror("getline");
            continue;
        }

        if (bytes_read > 0 && line[bytes_read - 1] == '\n') {
            line[bytes_read - 1] = '\0';
        }

        int token_count = 0;
        char **tokens = tokenize_line(line, &token_count);
        if (token_count == 0) {
            free(tokens);
            continue;
        }

        struct command cmd;
        bool valid = parse_tokens(tokens, token_count, &cmd);
        if (!valid) {
            free(tokens);
            continue;
        }
        free(tokens);

        if (cmd.argc1 == 0) {
            free_command(&cmd);
            continue;
        }

        if (execute_builtin(&cmd)) {
            free_command(&cmd);
            continue;
        }

        if (cmd.has_pipe) {
            execute_piped_command(&cmd);
        } else {
            execute_simple_command(&cmd);
        }

        free_command(&cmd);
    }

    free(line);
    return EXIT_SUCCESS;
}
