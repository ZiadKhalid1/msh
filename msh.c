#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include <readline/history.h>
#include <readline/readline.h>

typedef struct {
  char name[NAME_MAX];
  char value[NAME_MAX];
} var_t;

typedef enum {
  START,
  IN_QUOTE,
  IN_WORD,
  IN_REDIRECT,
  DONE,
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

char *readline_input(int status);
void add_local_var(input_command_t args);
char *search_var(char *name);
input_command_t fsm_parser(char *buf);
input_command_t redirection(const input_command_t args);
int executor(input_command_t args);
void cleanup(input_command_t args, char *buf);

int _export(const input_command_t args);
int cd(const input_command_t args);
int pwd(const input_command_t args);
const command_t builtins[] = {{.command_name = "cd", .main = cd},
                              {.command_name = "pwd", .main = pwd},
                              {.command_name = "export", .main = _export}};

int main() {
  // Ignore SIGINT (Ctrl+C) globally; readline won't be interrupted
  signal(SIGINT, SIG_IGN);

  // Tell readline not to install its own signal handlers
  rl_catch_signals = 0;
  local_vars = calloc(256, sizeof(var_t));
  int status = 0;
  while (1) {
    char *buf = readline_input(status);
    input_command_t args = fsm_parser(buf);

    if (args.argc == 0) {
      cleanup(args, buf);
      continue;
    }

    if (strcmp(args.argv[0], "exit") == 0) {
      cleanup(args, buf);
      free(local_vars);
      rl_clear_history();
      return status;
    }

    status = executor(args);
    cleanup(args, buf);
  }
}

char *readline_input(int status) {
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

char* search_var(char *name) {
  for (int i = 0; i < local_vars_count; i++) {
    if (strcmp(name, local_vars[i].name) == 0) {
      return local_vars[i].value;
    }
  }
  char *value = getenv(name);
  if (value != NULL)
    return value;

  return NULL;
}

input_command_t fsm_parser(char *buf) {
  state_t s = START;
  char line[1024] = {0};
  int line_len = 0;
  int argv_capacity = 16;
  input_command_t args = {.argc = 0,
                          .argv = calloc(argv_capacity, sizeof(char *)),
                          .env = calloc(16, sizeof(var_t)),
                          .envc = 0,
                          .redirections = calloc(16, sizeof(redirect_t)),
                          .redirections_count = 0};

  while (*buf != '\0') {
    switch (s) {
    case START:
      if (isspace((unsigned char)*buf)) {
        buf++;
        break;
      }
      if (*buf == '"') {
        s = IN_QUOTE;
        buf++;
        break;
      }
      if (*buf == '<' || *buf == '>') {
        s = IN_REDIRECT;
        args.redirections[args.redirections_count].symbol = *buf++;
        break;
      }
      if (strncmp(buf, "2>", 2) == 0) {
        s = IN_REDIRECT;
        args.redirections[args.redirections_count].symbol = '2';
        buf += 2;
        break;
      }
      s = IN_WORD;
      break;

    case IN_QUOTE:
      while (*buf != '"' && *buf != '\0') {
        line[line_len++] = *buf++;
      }
      if (*buf == '"')
        buf++;
      line[line_len] = '\0';
      args.argv[args.argc++] = strdup(line);
      if (args.argc >= argv_capacity) {
        argv_capacity *= 2;
        args.argv = realloc(args.argv, argv_capacity * sizeof(char *));
      }
      line_len = 0;
      memset(line, 0, sizeof(line));
      s = START;
      break;

    case IN_WORD:
      while (*buf != '\0' && !isspace((unsigned char)*buf) && *buf != '<' &&
             *buf != '>' && *buf != '"') {
        if (*buf == '$') {
          buf++;
          char var_buf[128] = {0};
          int var_len = 0;
          while (isalnum((unsigned char)*buf) || *buf == '_') {
            var_buf[var_len++] = *buf++;
          }
          var_buf[var_len] = '\0';
          const char *value = search_var(var_buf);
          if (value) {
            for (size_t i = 0; value[i] != '\0'; i++) {
              line[line_len++] = value[i];
            }
          }
        } else {

          line[line_len++] = *buf++;
        }
      }
      line[line_len] = '\0';
      char *equal_sign = strchr(line, '=');
      if (equal_sign != NULL && args.argc == 0) {
        sscanf(line, "%[^=]=%s", args.env[args.envc].name,
               args.env[args.envc].value);
        args.envc++;
      } else {
        args.argv[args.argc++] = strdup(line);
        if (args.argc >= argv_capacity) {
          argv_capacity *= 2;
          args.argv = realloc(args.argv, argv_capacity * sizeof(char *));
        }
      }
      line_len = 0;
      memset(line, 0, sizeof(line));
      s = START;
      break;

    case IN_REDIRECT:
      while (isspace((unsigned char)*buf))
        buf++;
      size_t redirect_start = 0;
      while (*buf != '\0' && !isspace((unsigned char)*buf)) {
        line[redirect_start++] = *buf++;
      }
      line[redirect_start] = '\0';
      args.redirections[args.redirections_count].name = strdup(line);
      args.redirections_count++;
      line_len = 0;
      memset(line, 0, sizeof(line));
      s = START;
      break;
    case DONE:
      break;
    }
  }
  if (args.argc == 0) {
    add_local_var(args);
  }
  args.argv[args.argc] = NULL;
  return args;
}

void add_local_var(input_command_t args) {
  if (args.envc > 1)
    return;
  strcpy(local_vars[local_vars_count].name, args.env[0].name);
  strcpy(local_vars[local_vars_count].value, args.env[0].value);
  local_vars_count++;
}

int _export(const input_command_t args) {
  int error = 0;
  for (int i = 1; i < args.argc; i++) {
    if (sscanf(args.argv[i], "%[^=]=%s", args.env[i].name, args.env[i].value) ==
        2) {
      error = setenv(args.env[i].name, args.env[i].value, 1);
    }
  }
  for (int j = 1; j < args.argc; j++) {
    for (int i = 0; i < local_vars_count; i++) {
      if (strcmp(args.argv[j], local_vars[i].name) == 0) {
        error = setenv(local_vars[i].name, local_vars[i].value, 1);
      }
    }
  }
  return error;
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
        fprintf(stderr, "cannot access %s: %s\n", args.redirections[i].name,
                strerror(errno));
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
    fprintf(stderr, "%s: %s: %s\n", args.argv[0], args.argv[1],
            strerror(errno));
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

void cleanup(input_command_t args, char *buf) {
  // Free each argv element
  for (int i = 0; i < args.argc; i++) {
    free(args.argv[i]);
  }
  free(args.argv);
  // Free env and redirection arrays
  free(args.env);
  for (int i = 0; i < args.redirections_count; i++) {
    free(args.redirections[i].name);
  }
  free(args.redirections);
  // Free the input buffer from readline
  free(buf);
}
