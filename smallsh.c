/* 
 * smallsh
 *
 * CS344 - Mark Miller
 *
 * outside references: 
 *  https://brennan.io/2015/01/16/write-a-shell-in-c/
 *  (for a basic understanding of shell functionality)
*/

#include <sys/wait.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <signal.h>
#include <fcntl.h>

// to keep track of background processes
pid_t pidArray[5];
int pidPos = 0;
int backStatus;

int foreStatus;

// ignoring this signal, ran out of time to make it work :(
static void ignoreSig(){
  signal(SIGTSTP, ignoreSig);
  return;
}

char *comment_line = "#";

// to hold parsed command
struct userCommand{
  char *command;
  char *arguments[512];
  char *in_file;
  char *out_file;
  bool backProc;
  int  argNum;
};

// built-in functions
int exitShell(struct userCommand cmd);
int cdShell(struct userCommand cmd);
int statusShell(struct userCommand cmd);

char *builtin_commands[] = {
  "exit",
  "cd",
  "status"
};

int (*builtin_func[]) (struct userCommand) = {
  &exitShell,
  &cdShell,
  &statusShell
};

int num_builtins(){
  return sizeof(builtin_commands) / sizeof(char*);
}

// exits smallsh
int exitShell(struct userCommand cmd){
  return 0;
}

// changes working directory of smallsh
int cdShell(struct userCommand cmd){
  // if no argument provided
  if (cmd.arguments[0] == NULL){
    if (chdir(getenv("HOME")) != 0){
      perror("smallsh");
    }
  } else {
    if (chdir(cmd.arguments[0]) != 0){
      perror("smallsh");
    }
  }
  return 1;
}

// prints out either the exit status or the terminating signal of last foreground process
int statusShell(struct userCommand cmd){
  if (WIFEXITED(foreStatus)){
    printf("exit value %d\n", WIFEXITED(foreStatus));
  } else {
    printf("terminated by signal %d\n", WTERMSIG(foreStatus));
  }
  return 1;
}

// start new process
int startProcess(struct userCommand cmd){
  pid_t pid, wpid;
  int saved_stdin = dup(0);
  int saved_stdout = dup(1);

  // input redirection
  if (cmd.in_file){
    int sourceFD = open(cmd.in_file, O_RDONLY);
    if (sourceFD == -1){
      perror("smallsh: badfile");
      return 1;
    }
    int resultIn = dup2(sourceFD, 0);
    if (resultIn == -1){
      perror("source dup2()");
      return 1;
    }
  }
  // output redirection
  if (cmd.out_file){
    int targetFD = open(cmd.out_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (targetFD == -1){
      perror("smallsh: badfile");
      return 1;
    }
    int resultOut = dup2(targetFD, 1);
    if (resultOut == -1){
      perror("target dup2()");
      return 1;
    }
  }
  
  // fork new process
  pid = fork();
  if (pid == -1){
    perror("fork() failed");
    exit(EXIT_FAILURE);
  } else if (pid == 0){
		// child process
    // build the array with command and arguments
    // tot_args + 2 (i.e. command and null)
    char *cmdArray[cmd.argNum + 2];
    cmdArray[0] = cmd.command;
    for (int i = 0; i < cmd.argNum; i++){
      cmdArray[i + 1] = cmd.arguments[i];
    }
    cmdArray[(cmd.argNum + 2) - 1] = NULL;
    // new program, only returns here on error
		if (execvp(cmdArray[0], cmdArray) == -1){
			perror("smallsh: badfile");
		}
		exit(EXIT_FAILURE);
	} else {
    // parent process
    // restore fds
    dup2(saved_stdin, 0);
    dup2(saved_stdout, 1);
    // if background command, add pid to array, advance immediately WNOHANG, else wait
    if (cmd.backProc){
      printf("background pid is %d\n", pid);
      pidArray[pidPos] = pid;
      pidPos++;
      wpid = waitpid(pid, &backStatus, WNOHANG);
    } else {
      wpid = waitpid(pid, &foreStatus, 0);
    }
	}
	return 1;
}

// check for built-ins and execute commands accordingly
int executeCommand(struct userCommand cmd){
  // check for blank or comment line
  if (cmd.command == NULL || !strcmp(comment_line, cmd.command) || cmd.command[0] == '#'){
    return 1;
  } else {
  // check if built-in function or start new process
    for (int i = 0; i < num_builtins(); i++){
      if (strcmp(cmd.command, builtin_commands[i]) == 0){
        return (*builtin_func[i]) (cmd);
      }
    }
    return startProcess(cmd);
  }
}

// parse input line & return struct
struct userCommand structParser(char *line){
  char *input_file = "<";
  char *output_file = ">";
  char *background = "&";

  struct userCommand newCommand;
  newCommand.backProc = 0;
  newCommand.in_file = NULL;
  newCommand.out_file = NULL;

  // get first word and set as command
  char *token = strtok(line, " ");
  newCommand.command = token;

  // get next word
  token = strtok(NULL, " ");
  int tot_args = 0;

  while (token != NULL){
    if (!strcmp(token, input_file)){
      token = strtok(NULL, " ");
      newCommand.in_file = token;
      token = strtok(NULL, " ");
      continue;
    }
    if (!strcmp(token, output_file)){
      token = strtok(NULL, " ");
      newCommand.out_file = token;
      token = strtok(NULL, " ");
      continue;
    }
    if (!strcmp(token, background)){
      newCommand.backProc = 1;
      token = strtok(NULL, " ");
      if (token != NULL){
        newCommand.backProc = 0;
        newCommand.arguments[tot_args] = background;
        tot_args++;
        continue;
      } else {
        continue;
      }
    }
    newCommand.arguments[tot_args] = token;
    tot_args++;
    token = strtok(NULL, " ");
  }
  newCommand.argNum = tot_args;

  return newCommand;
}

// read user input
char *readInput(void){
	int bufsize = 2048;
	int position = 0;
	char *readBuffer = malloc(sizeof(char) * bufsize);
	// store character as an int since eof is an int
	int ch;
  // for $$ expansion
  int count$ = 0;

	if (!readBuffer){
		fprintf(stderr, "smallsh: allocation error\n");
		exit(EXIT_FAILURE);
	}

	// read characters to buffer until eof or max size is reached
  while (1){
    ch = getchar();
    if (ch == EOF || ch == '\n'){
      readBuffer[position] = '\0';
      return readBuffer;
    } else if (ch == '$'){
      count$++;
      if (count$ == 2){
        // $$ expansion
        count$ = 0;
        position = position - 1;
        char pidString[8];
        pid_t smallshPid = getpid();
        sprintf(pidString, "%d", smallshPid);
        int pos$ = 0;
        char num = pidString[pos$];
        while (num != '\0'){
          readBuffer[position] = num;
          pos$++;
          position++;
          num = pidString[pos$];
        }
        position = position - 1;
      } else {
        readBuffer[position] = ch;
      }
    } else if (ch == ' '){
      count$ = 0;
      readBuffer[position] = ch;
    } else {
      readBuffer[position] = ch;
    }
    position++;
    if (position >= bufsize){
      fprintf(stderr, "smallsh: command length maximum exceeded\n");
      exit(EXIT_FAILURE);
    }
  }
}

// main loop function
void shell_loop(void){
  char *line;
  struct userCommand command_struct;
  // with status set as 1, infinite loop
  int status;

  do {
    // background processes check
    for (int i = 0; i < 5; i++){
      if (pidArray[i] == '\0'){
        continue;
      }
      if (waitpid(pidArray[i], &backStatus, WNOHANG) == 0){
          continue;
      } else {
        if (WIFEXITED(backStatus)){
          printf("background pid %d is done: exit value %d\n", pidArray[i], WIFEXITED(backStatus));
        } else {
          printf("background pid %d is done: terminated by signal %d\n", pidArray[i], WTERMSIG(backStatus));
        }
        fflush(stdout);
        pidArray[i] = '\0';
      }
    }
    printf(": ");
    fflush(stdout);
    line = readInput();
    command_struct = structParser(line);
    status = executeCommand(command_struct);
    free(line);
  } while (status);
}

int main(void){
  // signal handling
  signal(SIGTSTP, ignoreSig);
  shell_loop();
  return EXIT_SUCCESS;
}
