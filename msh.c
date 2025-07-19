#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <readline/readline.h>
#include <readline/history.h>
extern char **environ;

#define NAME_MAX 255

char *allocate_buf() {
    char *buf = (char *) malloc(sizeof(char) * BUFSIZ);
    return buf;
}

typedef struct {
    char name[NAME_MAX];
    char value[NAME_MAX];
} var_t;

typedef enum {
    start,
    name,
    success,
} state_t;

typedef struct {
    char symbol;
    char *name;
} redirect_t;

typedef struct {
    int argc;
    char **argv;
    var_t *env;
    int envc;
    redirect_t *redirections;
    int redirections_count;
} input_command_t;

typedef struct {
    const char *command_name;
    int (*main)(input_command_t args);
} command_t;

var_t *local_vars = NULL;
int local_vars_count = 0;

void env_substitute(char **token);
void fsm_redirection(char **token, input_command_t *args);

void add_local_var(input_command_t args) {
    if (args.envc > 0) {
        strcpy(local_vars[local_vars_count].name, args.env[0].name);
        strcpy(local_vars[local_vars_count].value, args.env[0].value);
    }
    local_vars_count = local_vars_count + args.envc;
}

int Export(const input_command_t args) {
    int error = 0;
    for (int j = 1; j < args.argc; j++) {
        for (int i = 0; i < local_vars_count; i++) {
            if (strcmp(args.argv[j], local_vars[i].name) == 0) {
                error = setenv(local_vars[i].name, local_vars[i].value, 1);
            }
        }
    }
    for (int i = 1; i < args.argc; i++) {
        if (sscanf(args.argv[i], "%[^=]=%s", args.env[i].name, args.env[i].value) == 2) {
            error = setenv(args.env[i].name, args.env[i].value, 1);
        }
    }
    return error;
}

void env_substitute(char **token) {
    if (*token == NULL) return;
    int substituted = 0;
    for (int k = 0; k < strlen(*token); k++) {
        if ((*token)[k] == '$') {
            for (int j = 0; j < local_vars_count; j++) {
                if (strcmp(*token + k + 1, local_vars[j].name) == 0) {
                    substituted = 1;
                    strcpy(*token + k, local_vars[j].value);
                }
            }
            if (!substituted) {
                strcpy(*token + k, "");
            }
        }
    }
}

void fsm_redirection(char **token, input_command_t *args) {
    state_t s = start;
    while (1) {
        switch (s) {
            case start:
                if (strstr(*token, "2>") != NULL && (*((*token) + 2) != ' ' && *((*token) + 2) != '\0')) {
                    args->redirections[args->redirections_count].symbol = **token;
                    args->redirections[args->redirections_count].name = *token + 2;
                    s = success;
                } else if (((strstr(*token, ">") != NULL || strstr(*token, "<") != NULL) && *((*token) + 1) != '\0' && *((*token) + 1) != '>')) {
                    args->redirections[args->redirections_count].symbol = **token;
                    args->redirections[args->redirections_count].name = *token + 1;
                    s = success;
                } else if (**token == '>' || **token == '<' || strncmp(*token, "2>", 2) == 0) {
                    args->redirections[args->redirections_count].symbol = **token;
                    s = name;
                }
                break;
            case name:
                *token = strtok(NULL, " \t\r\n\a");
                if (*token == NULL) {
                    return;
                }
                env_substitute(token);
                args->redirections[args->redirections_count].name = *token;
                s = success;
                break;
            case success:
                args->redirections_count++;
                *token = strtok(NULL, " \t\r\n\a");
                if (*token == NULL) {
                    return;
                }
                env_substitute(token);
                if (*token == NULL) break;
                if (strstr(*token, "2>") != NULL && (*((*token) + 2) != ' ' && *((*token) + 2) != '\0')) {
                    s = start;
                } else if (((strstr(*token, ">") != NULL || strstr(*token, "<") != NULL) && *((*token) + 1) != '\0' && *((*token) + 1) != '>')) {
                    s = start;
                } else if (**token == '>' || **token == '<' || strncmp(*token, "2>", 2) == 0) {
                    args->redirections[args->redirections_count].symbol = **token;
                    s = name;
                } else {
                    return;
                }
                break;
        }
    }
}

input_command_t parse_command(char *buf) {
    input_command_t args = {
        .argc = 0,
        .argv = (char **) malloc(BUFSIZ * sizeof(char *)),
        .env = (var_t *) calloc(256, sizeof(var_t)),
        .envc = 0,
        .redirections = (redirect_t *) calloc(256, sizeof(redirect_t)),
        .redirections_count = 0
    };
    char *token = strtok(buf, " \t\r\n\a");
    while (token != NULL && strchr(token, '=') != NULL) {
        sscanf(token, "%[^=]=%s", args.env[args.envc].name, args.env[args.envc].value);
        args.envc++;
        token = strtok(NULL, " \t\r\n\a");
    }
    if (token == NULL) {
        add_local_var(args);
    }
    while (token != NULL) {
        if (args.argc >= BUFSIZ - 1) {
            exit(-1);
        }
        env_substitute(&token);
        if ((*token == '>' || *token == '<' || *token == '2')) {
            fsm_redirection(&token, &args);
        }
        if (token == NULL) {
            continue;
        }
        args.argv[args.argc] = (char *) calloc(1024, sizeof(char));
        strcpy(args.argv[args.argc], token);
        args.argc++;
        token = strtok(NULL, " \t\r\n\a");
    }
    args.argv[args.argc] = NULL;
    return args;
}

input_command_t redirection(const input_command_t args) {
    int i = 0;
    int fd;
    while (i < args.redirections_count) {
        if ('>' == args.redirections[i].symbol) {
            fd = open(args.redirections[i].name, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd < 0) {
                fprintf(stderr, "%s: %s\n", args.redirections[i].name, strerror(errno));
                exit(-1);
            }
            dup2(fd, 1);
            close(fd);
        } else if ('<' == args.redirections[i].symbol) {
            fd = open(args.redirections[i].name, O_RDONLY);
            if (fd < 0) {
                fprintf(stderr, "cannot access %s: %s\n", args.redirections[i].name, strerror(errno));
                exit(-1);
            }
            dup2(fd, 0);
            close(fd);
        } else if ('2' == args.redirections[i].symbol) {
            fd = open(args.redirections[i].name, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd < 0) {
                exit(-1);
            }
            dup2(fd, 2);
            close(fd);
        }
        i++;
    }
    return args;
}

int echo(const input_command_t args) {
    char **argv = args.argv;
    while (*++argv) {
        printf("%s", *argv);
        if (argv[1])
            printf(" ");
    }
    printf("\n");
    return 0;
}

int pwd(const input_command_t args) {
    char buf[PATH_MAX];
    printf("%s\n", getcwd(buf, PATH_MAX));
    return 0;
}


int cd(const input_command_t args) {
    char path[PATH_MAX];

    if (args.argv[1] == NULL) {
        if (chdir(getenv("HOME")) == 0) {
            setenv("PWD", getenv("HOME"), 1);
            return 0;
        } else {
            perror("cd");
            return -1;
        }
    }

    if (chdir(args.argv[1]) != 0) {
        fprintf(stderr, "%s: %s: %s\n", args.argv[0], args.argv[1], strerror(errno));
        fflush(stderr);
        return -1;
    }

    // After successful chdir, update PWD
    if (getcwd(path, sizeof(path)) != NULL) {
        setenv("PWD", path, 1);
    } else {
        perror("getcwd");
    }

    return 0;
}

const command_t builtins[] = {
    {.command_name = "cd", .main = cd},
    {.command_name = "pwd", .main = pwd},
    {.command_name = "export", .main = Export}
};

char* readline_input(int status) {
    const char *username = getenv("USER");
       if (username == NULL)
           username = "unknown";

       const char *pwd = getenv("PWD");
          if (pwd == NULL)
              pwd = "unknown";

       char prompt[1024];
       snprintf(prompt, sizeof(prompt), "%s:%s> ", username, pwd);

       char *input = readline(prompt);
       if (input == NULL) {
           printf("\n");
           exit(status);
       }

       if (*input) {
           add_history(input);
       }
       return input;
}

int executor(input_command_t args) {
    int i = 0;
    const int size = sizeof(builtins) / sizeof(command_t);
    for (; i < size; i++) {
        if (strcmp(args.argv[0], builtins[i].command_name) == 0)
            break;
    }
    if (i < size) {
        return builtins[i].main(args);
    }
    pid_t status;
    const pid_t pid = fork();
    if (pid == -1) {
        perror("fork");
        exit(EXIT_FAILURE);
    }
    if (pid == 0) {
        for (int j = 0; j < args.envc; j++) {
            setenv(args.env[j].name, args.env[j].value, 1);
        }
        input_command_t redirected_args = redirection(args);
        execvp(redirected_args.argv[0], redirected_args.argv);
        fprintf(stderr, "%s: command not found\n", redirected_args.argv[0]);
        exit(1);
    }
    waitpid(pid, &status, 0);
    return WEXITSTATUS(status);
}
void cleanup(input_command_t args, char *buf);

void cleanup(input_command_t args, char *buf) {
    // Free each argv element
    for (int i = 0; i < args.argc; i++) {
        free(args.argv[i]);
    }
    free(args.argv);

    // Free env and redirection arrays
    free(args.env);
    free(args.redirections);

    // Free the input buffer from readline
    free(buf);

}
int main(int argc, char *argv[]) {
    local_vars = calloc(256, sizeof(var_t));
    int status = 0;
    while (1) {
        char *buf = readline_input(status);
        input_command_t args = parse_command(buf);

        if (args.argc == 0) {
            free(buf);
            continue;
        }

        if (strcmp(args.argv[0], "exit") == 0) {
            printf("Good Bye\n");
            cleanup(args, buf);
            // Free global local variables storage
            free(local_vars);
            // Clear readline history
            rl_clear_history();
            return status;
        }

        status = executor(args);
        cleanup(args, buf);
    }
}
