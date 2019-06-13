#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <pwd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>

#define MAX_PATH_LENGTH 1024
#define MAX_USERNAME_LENGTH 512
#define HISTORY_FILE_NAME ".smsh_history"
#define HISTORY_FILE_NAME_LENGTH 13
#define INPUT_BUFFER_SIZE 16
#define BYPASS_DELIMITER '"'


int can_overwritten = 1;

char* ltrim(char* str) {
    if (str == NULL) return str;

    while (*str == ' ') str++;

    return str;
}

char* rtrim(char* str) {
    if (str == NULL) return str;

    size_t end = strlen(str) - 1;

    while (str[end] == ' ' && end >= 0) str[end--] = '\0';

    return str;
}

char* trim(char* str) {
    return rtrim(ltrim(str));
}

char* get_token(char* str, const char* delimiter) {
    static char* s_str;
    const char* s_delimiter;

    int is_bypass = 0;

    if (str == NULL) str = s_str;
    else s_str = str;

    if (*s_str == '\0') return NULL;

    while (*s_str) {
        s_delimiter = delimiter;

        if (*s_str == BYPASS_DELIMITER) {
            is_bypass = !is_bypass;

            s_str++;
            continue;
        }

        if (is_bypass) {
            s_str++;
            continue;
        }

        while (*s_delimiter) {
            if (*s_str == *s_delimiter) {
                *s_str = '\0';
                s_str++;

                return str;
            }

            s_delimiter++;
        }

        s_str++;
    }

    return str;
}

char** tokenize(char* command, const char* delimiter, int* token_count) {
    int i, found, is_end_delimiter;
    char* token;
    char** argv;

    i = 0;
    is_end_delimiter = 0;
    *token_count = 0;
    while (command[i] != '\0') {
        if (command[i] == *delimiter) {
            (*token_count)++;
            is_end_delimiter = 1;
        } else if (command[i] != ' ') {
            is_end_delimiter = 0;
        }

        i++;
    }

    if (*token_count > 0 && is_end_delimiter == 0) {
        (*token_count)++;
    }

    argv = (char**) malloc(sizeof(char*) * (*token_count + 1));

    found = 0;
    argv[found++] = trim(get_token(command, delimiter));
    while ((token = get_token(NULL, delimiter)) != NULL) {
        if (*token == BYPASS_DELIMITER) {
            *token = '\0';
            token++;
            argv[found] = trim(token);

            while (*token != BYPASS_DELIMITER && token++ != '\0');

            *token = '\0';
        } else {
            argv[found] = trim(token);
        }

        found++;
    }

    return argv;
}

const char* get_home_path() {
    static char* home_path = NULL;

    if (home_path == NULL && (home_path = getenv("HOME")) == NULL) {
        home_path = getpwuid(getuid())->pw_dir;
    }

    return home_path;
}

const char* get_history_file_path() {
    static char* path = NULL;

    if (path == NULL) {
        const char* home = get_home_path();
        size_t home_path_length = strlen(home);
        char* history_file_path = malloc(home_path_length + HISTORY_FILE_NAME_LENGTH + 2);

        strcpy(history_file_path, home);
        history_file_path[home_path_length] = '/';
        history_file_path[home_path_length + 1] = '\0';
        strcat(history_file_path, HISTORY_FILE_NAME);

        path = history_file_path;
    }

    return path;
}

const char* get_history_command() {
    static char* command = NULL;

    if (command == NULL) {
        const char* history_file_path = get_history_file_path();
        size_t command_length = 4 + strlen(history_file_path) + 1;
        char* print_history_command = malloc(command_length);

        strcpy(print_history_command, "cat ");
        strcat(print_history_command, history_file_path);
        print_history_command[command_length - 1] = '\0';

        command = print_history_command;
    }

    return command;
}

void do_change_directory(const char* command) {
    int argc;
    char** argv;
    char argv_token[strlen(command) + 1];

    strcpy(argv_token, command);

    argv = tokenize(argv_token, " ", &argc);

    if (argc > 1 && strcmp(argv[0], "cd") == 0) {
        if (argv[1][0] == '~') {
            chdir(get_home_path());
            argv[1][0] = '.';
        }
        chdir(argv[1]);
    }

    free(argv);
}

void do_set_config(const char* command) {
    int argc;
    char** argv;
    char argv_data[strlen(command) + 1];

    strcpy(argv_data, command);

    argv = tokenize(argv_data, " ", &argc);

    if (argc < 3) {
        printf("set -o noclobber | set +o noclobber\n");
        free(argv);
        return;
    }

    if (strcmp(argv[1], "-o") == 0 && strcmp(argv[2], "noclobber") == 0) {
        can_overwritten = 0;
    } else if (strcmp(argv[1], "+o") == 0 && strcmp(argv[2], "noclobber") == 0) {
        can_overwritten = 1;
    }

    free(argv);
}

pid_t exec(const char* command, int input_fd, int output_fd) {
    if (strncmp(command, "cd", 2) == 0) {
        do_change_directory(command);
        return 0;
    } else if (strcmp("exit", command) == 0) {
        exit(0);
    } else if (strcmp("clear", command) == 0) {
        printf("\033[H\033[J");
        return 0;
    } else if (strcmp("history", command) == 0) {
        return exec(get_history_command(), input_fd, output_fd);
    } else if (strncmp(command, "set", 3) == 0) {
        do_set_config(command);
        return 0;
    }

    pid_t pid;

    pid = fork();
    if (pid == 0) {
        int argc, i;
        size_t command_length = strlen(command);
        char** temp_argv;
        char argv_token[command_length + 1];

        memcpy(argv_token, command, sizeof(char) * (command_length + 1));

        temp_argv = tokenize(argv_token, " ", &argc);

        char* argv[argc + 1];

        if (argc == 0) {
            argv[0] = temp_argv[0];
        }

        for (i = 0; i < argc; i++) {
            argv[i] = temp_argv[i];
        }

        free(temp_argv);

        if (argc == 0) {
            argv[1] = (char*) 0;
        } else {
            argv[argc] = (char*) 0;
        }

        if (input_fd != STDIN_FILENO) {
            dup2(input_fd, STDIN_FILENO);
        }

        if (output_fd != STDOUT_FILENO) {
            dup2(output_fd, STDOUT_FILENO);
        }

        if (execvp(argv[0], argv) == -1) {
            perror("execute fail");
        }

        exit(EXIT_FAILURE);
    } else if (pid < 0) {
        perror("fork fail");
    }

    if (input_fd != STDIN_FILENO) {
        close(input_fd);
    }

    if (output_fd != STDOUT_FILENO) {
        close(output_fd);
    }

    return pid;
}

int run(char* command, int is_background) {
    int input_fd, output_fd, token_count, status;
    char** tokens;
    pid_t pid;

    input_fd = STDIN_FILENO;
    status = 0;
    tokens = tokenize(command, "<", &token_count);

    if (token_count > 1) {
        if (token_count > 2) {
            perror("not support multiple input redirection");
            return -1;
        }

        input_fd = open(tokens[1], O_RDONLY);
    }

    output_fd = STDOUT_FILENO;
    tokens = tokenize(command, ">", &token_count);

    if (token_count > 1) {
        if (tokens[1][0] == '\0') {
            output_fd = open(trim(tokens[1] + 1), O_WRONLY | O_CREAT | O_APPEND, 0644);
        } else if (tokens[1][0] == '|') {
            output_fd = creat(trim(tokens[1] + 1), 0644);
        } else {
            struct stat sts;

            if (can_overwritten == 0 && (stat(tokens[1], &sts)) >= 0) {
                printf("-smsh: %s: connat overwrite exist file\n", tokens[1]);
                return -1;
            } else {
                output_fd = creat(tokens[1], 0644);
            }
        }
    }

    pid = exec(command, input_fd, output_fd);

    if (input_fd != STDIN_FILENO) {
        close(input_fd);
    }

    if (output_fd != STDOUT_FILENO) {
        close(output_fd);
    }

    if (pid > 0 && is_background == 0) {
        waitpid(pid, &status, 0);
    }

    return status;
}

int run_pipe(char* command) {
    int i, status, token_count;
    char** tokens;
    pid_t pid;
    int pipe_fd[2];
    int last_pipe_fd[2];

    last_pipe_fd[0] = STDIN_FILENO;
    last_pipe_fd[1] = STDOUT_FILENO;

    tokens = tokenize(command, "|", &token_count);

    if (token_count < 2) {
        return 0;
    }

    for (i = 0; i < token_count; i++) {
        size_t length = strlen(tokens[i]);

        if (tokens[i][length - 1] == '>') {
            tokens[i][length] = '|';
            i++;
            last_pipe_fd[0] = STDIN_FILENO;
            last_pipe_fd[1] = STDOUT_FILENO;
            continue;
        }

        pipe(pipe_fd);

        if (i == 0) {
            pid = exec(tokens[i], STDIN_FILENO, pipe_fd[1]);
        } else if (i == token_count - 1) {
            pid = exec(tokens[i], last_pipe_fd[0], STDOUT_FILENO);
        } else {
            pid = exec(tokens[i], last_pipe_fd[0], pipe_fd[1]);
        }

        last_pipe_fd[0] = pipe_fd[0];
        last_pipe_fd[1] = pipe_fd[1];

        waitpid(pid, &status, 0);
    }

    if (last_pipe_fd[0] == STDIN_FILENO || last_pipe_fd[1] == STDOUT_FILENO) {
        return 0;
    }

    return 1;
}

void run_in_order(char* input) {
    if (input == NULL) return;

    int i, is_background, token_count;
    char** tokens;

    tokens = tokenize(trim(input), "&", &token_count);

    for (i = 0; i < token_count; i++) {
        if (tokens[i] == NULL || tokens[i][0] == '\0') {
            continue;
        }

        is_background = 0;

        if (token_count == 1 || (token_count > 1 && tokens[i + 1] != NULL && tokens[i + 1][0] != '\0')) {
            is_background = 1;
        }

        if (run_pipe(tokens[i]) == 0 && run(tokens[i], is_background) != 0) {
            return;
        }
    }

    if (token_count == 0) {
        if (run_pipe(tokens[0]) == 0) {
            run(tokens[0], 0);
        }
    }
}

void run_multiple_statement(char* input) {
    if (input == NULL) return;

    int i, token_count;
    char** tokens;

    tokens = tokenize(trim(input), ";", &token_count);

    if (token_count == 0) {
        run_in_order(tokens[0]);
    }

    for (i = 0; i < token_count; i++) {
        run_in_order(tokens[i]);
    }
}

void print_cursor() {
    char path[MAX_PATH_LENGTH];
    char username[MAX_USERNAME_LENGTH];

    getcwd(path, MAX_PATH_LENGTH);
    getlogin_r(username, MAX_USERNAME_LENGTH);

    printf("smsh:%s %s$ ", path, username);
}

int main(int argc, char** argv) {
    char input_buffer[INPUT_BUFFER_SIZE];
    char* result;
    size_t result_size, input_length;
    char* input;
    char* temp;
    int is_end_line, history_fd;

    input = NULL;

    setbuf(stdout, NULL);

    history_fd = open(get_history_file_path(), O_WRONLY | O_CREAT | O_APPEND, 0644);

    print_cursor();
    while (1) {
        result = fgets(input_buffer, INPUT_BUFFER_SIZE, stdin);

        if (result == NULL) continue;

        is_end_line = 0;
        result_size = strlen(result);
        if (result[result_size - 1] == '\n') {
            result[result_size - 1] = '\0';
            result_size -= 1;
            is_end_line = 1;
        }

        if (input == NULL) {
            input = malloc(sizeof(char) * (result_size + 1));

            strcpy(input, result);
        } else {
            input_length = strlen(input);

            temp = malloc((sizeof(char) * (input_length + result_size + 1)));
            temp[0] = '\0';

            strcat(temp, input);
            strcat(temp, result);

            free(input);

            input = temp;
            input[input_length + result_size] = '\0';
        }

        if (is_end_line) {
            if (strcmp(input, "exit") == 0) {
                exit(0);
            }

            run_multiple_statement(input);

            write(history_fd, input, strlen(input));
            write(history_fd, "\n", 1);

            free(input);
            input = NULL;

            print_cursor();
        }
    }
}
