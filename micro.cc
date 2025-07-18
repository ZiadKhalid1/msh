#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
extern char **environ;

char* allocate_buf() {
  char *buf = (char *)malloc(sizeof(char) * BUFSIZ);
  if (buf == NULL) {
    perror("Unable to allocate buffer");
    exit(1);
  }
  return buf;
}

typedef struct {
  char name[NAME_MAX];
  char value[NAME_MAX];
}var_t;

typedef enum {
  start,
  name,
  success,
}state;

typedef struct {
  char symbol;
  char* name;
}redirect;

typedef struct {
  int argc;
  char **argv;
  var_t* env;
  int envc;
  redirect* actions;
  int numOfRedirection;
} input_command_t;


typedef struct {
  const char* command_name;
  int (*main) (input_command_t args);
} command_t;

var_t* arr_local_vars = (var_t*)calloc(256, sizeof(var_t));
int arr_local_vars_count = 0;


void add_local_var(input_command_t args) {
   if (args.envc > 0){
    strcpy(arr_local_vars[arr_local_vars_count].name, args.env[0].name);
    strcpy(arr_local_vars[arr_local_vars_count].value, args.env[0].value);
  }
  arr_local_vars_count = arr_local_vars_count + args.envc;
}

int Export(const input_command_t args) {
  int error = 0;
  for (int j = 1; j < args.argc; j++) {
    for (int i = 0; i < arr_local_vars_count; i++) {
      if (strcmp(args.argv[j], arr_local_vars[i].name) == 0) {
        error = setenv(arr_local_vars[i].name, arr_local_vars[i].value,1);
      }
    }
  }
  for (int i = 1; i < args.argc; i++) {
    if (sscanf(args.argv[i], "%[^=]=%s", args.env[i].name, args.env[i].value) == 2) {
      error = setenv(args.env[i].name, args.env[i].value,1);
    }
  }
  return error;
}


void substituteToken(char* &token) {
  if (token == NULL) return;
  int substituted = 0;
  for (int k = 0; k < strlen(token); k++) {
    if (token[k] == '$') {
      for (int j = 0; j < arr_local_vars_count; j++) {
        if (strcmp(token+ k+1, arr_local_vars[j].name) == 0) {
          substituted = 1;
          strcpy(token+k, arr_local_vars[j].value);
        }
      }
      if (!substituted) {
        strcpy(token+k, "");
      }
    }
  }
}
// void substitute(input_command_t &args) {
//   for (int i = 0; i < args.argc; i++) {
//     substituteToken(args.argv[i]);
//   }
// }

void fsm_redirection(char* &token, input_command_t &args) {
  state s = start;
  while (1) {
    switch (s) {
      case start:
        if ( strstr(token,"2>") != NULL && (*(token+2) != ' ' && *(token+2) != '\0')) {
          args.actions[args.numOfRedirection].symbol = *token;
          args.actions[args.numOfRedirection].name = token+2;
          s = success;
        } //for stderr "2>"
        else if (((strstr(token,">") != NULL || strstr(token,"<") != NULL) && *(token+1) != '\0' && *(token+1) != '>')) {
          args.actions[args.numOfRedirection].symbol = *token;
          args.actions[args.numOfRedirection].name = token+1;
          s = success;
        }
        else if (*token == '>' || *token == '<' || strncmp(token, "2>",2) == 0) {
          args.actions[args.numOfRedirection].symbol = *token;
          s = name;
        }
        break;
      case name:
        token = strtok(NULL, " \t\r\n\a");
        if (token == NULL) {
          return;
        }
        substituteToken(token);
        args.actions[args.numOfRedirection].name = token;
        s = success;
        break;
      case success:
        args.numOfRedirection++;
        token = strtok(NULL, " \t\r\n\a");
        if (token == NULL) {
          return;
        }
        substituteToken(token);
        if (token == NULL) {break;}
        if ( strstr(token,"2>") != NULL && (*(token+2) != ' ' && *(token+2) != '\0')) {
          s = start;
        }
        else if (((strstr(token,">") != NULL || strstr(token,"<") != NULL) && *(token+1) != '\0' && *(token+1) != '>')) {
          s = start;
        }
        else if (*token == '>' || *token == '<' || strncmp(token, "2>",2) == 0) {
          args.actions[args.numOfRedirection].symbol = *token;
          s = name;
        }
        else {
          return;
        }
        break;
    }
  }
}


input_command_t parse_command(char *buf) {
  input_command_t args = {.argc = 0,
    .argv = (char **)malloc(BUFSIZ * sizeof(char *)),
    .env = (var_t*)calloc(256, sizeof(var_t)),
    .envc = 0,
    .actions = (redirect*)calloc(256, sizeof(redirect)),
    .numOfRedirection = 0};
  char *token = strtok(buf, " \t\r\n\a");
  while (token != NULL && strchr(token, '=') != NULL){
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
    substituteToken(token);
    if ((*token == '>' || *token == '<' || *token == '2' )) {
      fsm_redirection(token, args);
    }
    if (token == NULL) {
      continue;
    }
    args.argv[args.argc] = (char*)calloc(1024, sizeof(char));
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
  while (i < args.numOfRedirection) {
    if ('>' == args.actions[i].symbol) {
      fd = open(args.actions[i].name, O_WRONLY | O_CREAT | O_TRUNC, 0644);
      if (fd < 0) {
        fprintf(stderr, "%s: %s\n",args.actions[i].name, strerror(errno));
        exit(-1);
      }
      dup2(fd,1);
      close(fd);
    }
    else if ('<' == args.actions[i].symbol) {
      fd = open(args.actions[i].name, O_RDONLY);
      if (fd < 0) {
        fprintf(stderr, "cannot access %s: %s\n",args.actions[i].name, strerror(errno));
        //fflush(stderr);
        exit(-1);
      }
      dup2(fd,0);
      close(fd);
    }
    else if ('2' == args.actions[i].symbol) {
      fd = open(args.actions[i].name, O_WRONLY | O_CREAT | O_TRUNC, 0644);
      if (fd < 0) {
        exit(-1);
      }
      dup2(fd,2);
      close(fd);
    }
    i++;
  }
  return args;
}

int echo(const input_command_t args) {
  char** argv = args.argv;
  while (*++argv) {
    printf("%s", *argv);
    if (argv[1])
      printf(" ");
  }
  printf("\n");
  return 0;
}

int pwd(const input_command_t) {
  char buf[PATH_MAX];
  printf("%s\n", getcwd(buf, PATH_MAX));
  return 0;
}

int cd(const input_command_t args) {
  if (args.argv[1] == NULL) {
    chdir("/home/ziad");
    return 0;
  }
  if (chdir(args.argv[1])) {
    fprintf(stderr, "%s: %s: %s\n",args.argv[0],args.argv[1], strerror(errno));
    fflush(stderr);
    return -1;
  }
  return 0;
}



const command_t builtins[] = {
  {.command_name = "cd", .main = cd},
  {.command_name = "pwd", .main = pwd},
  {.command_name = "export", .main = Export}
};



void readline(char* buf, size_t bufSize, int status) {
  if (isatty(STDIN_FILENO)) {
    printf("picoShell> ");
    fflush(stdout);
  }
  if (getline(&buf, &bufSize, stdin) == -1) {
    if (feof(stdin)) {
      exit(status); // We received an EOF
    }
    perror("readline");
    exit(status);
  }
}

int executor(input_command_t args) {
  int i = 0;
  int size = sizeof(builtins)/sizeof(command_t);
  for (; i < size; i++) {
    if (strcmp(args.argv[0],builtins[i].command_name) == 0)
      break;
  }
  if (i < size) {
    return builtins[i].main(args);
  }
  //to support local variables

  pid_t status;
  pid_t pid = fork();
  if (pid == -1) {
    //error in fork
    perror("fork");
    exit(EXIT_FAILURE);
  }
  if (pid == 0) {
    for (int j = 0; j < args.envc; j++) {
      setenv(args.env[j].name, args.env[j].value, 1);
    }
    args = redirection(args);
    execvp(args.argv[0], args.argv);
    fprintf(stderr, "%s: command not found\n", args.argv[0]);
    exit(1);
  }
  //wait child
  waitpid(pid,&status,0);
  return WEXITSTATUS(status);
}
int main(int argc, char *argv[]) {
  char *buf = allocate_buf();
  int status = 0;
  while (true){
    constexpr size_t bufSize = BUFSIZ;
    readline(buf, bufSize,status);
    input_command_t args = parse_command(buf);
    if (args.argc == 0)
      continue;
   // args = redirection(args);
    if(strcmp(args.argv[0],"exit") == 0){
          printf("Good Bye\n");
          return status;
      }

    status = executor(args);
    free(args.argv);
  };
}





