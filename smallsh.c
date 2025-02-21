// Henry Kanaskie

#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

// swapping $$ for pid
void swap_pid(char* input_string, int id, int i){
	// int to string
	char id_str[20];
	sprintf(id_str, "%d", id);

	int id_len = strlen(id_str);
	int str_len = strlen(input_string);
	// the new size is going to be the result of adding the length of the id - 2 ($$)
	int shift = id_len - 2;
	int j;
	// shift the string to make room
	for(j = str_len; j >= i+2; j--){
		input_string[j + shift] = input_string[j];
	}
	// input id into the string
	for(j = 0; j < id_len; j++){
		input_string[i + j] = id_str[j];
	}
}

// scans the input for $$
void scans(char* input){
	int i;
	for(i = 0; i < strlen(input); i++){
		if(i < (strlen(input) -1) && input[i] == '$'){
			// only swap if the next one is also a $
			if(input[i+1] == '$'){
				int id = getpid();
				swap_pid(input, id, i);
				// increment so that it skips to the next letter after the double $
				i += 1;
			}
		}
	}
}

// exit function
void exit_shell(int child_processes[], int child_index){
	int i;
	int pid;
	// go through the child process array and kill each active process
	for(i = 0; i < child_index; i++){
		pid = child_processes[i];
		kill(pid, SIGKILL);
	}
	exit(0);
}

// changing directory
void change_directory(char* args[]){
	// if no arg, changes to HOME environment variable
	if(args[1] == NULL){
		char* home = getenv("HOME");
		chdir(home);
	}else{
		// else change to path
		char path[strlen(args[1])];
		strcpy(path, args[1]);
		// error if path doesn't exist
		if(chdir(path) == -1){
			perror("");
		}
		
	}
}

// status command
void check_status(int* last_status){
	// checks the last status of foreground processes and prints the status
	if (WIFEXITED(*last_status)) {
		printf("exit value %d\n", WEXITSTATUS(*last_status));
	}else if(WIFSIGNALED(*last_status)){
		printf("terminated by signal %d\n", WTERMSIG(*last_status));
	}
}

// checks for terminated background processes
void check_background(int * num_forks, int* child_index, int child_processes[]){
	int status;
	pid_t terminated_pid;
	// check for any terminated process
	while((terminated_pid = waitpid(-1, &status, WNOHANG)) > 0){
		// print how the background process ended
		// don't update *last_status because it is for foreground processes
		if (WIFEXITED(status)) {
			status = WEXITSTATUS(status);
			printf("Background process %d exited with status %d\n", terminated_pid, status);
		}else if(WIFSIGNALED(status)){
			status = WTERMSIG(status);
			printf("Background process %d terminated by signal %d\n", terminated_pid, status);
		}
		int i, j;
		// clean up the child processes array so that it doesn't continue to hold onto a terminated process
		for(i = 0; i < *child_index; i++){
			if(child_processes[i] == terminated_pid){
				// keeps track of number of forks in case of forkbombs
				(*num_forks)--;
				for(j=i; j< *child_index -1; j++){
					child_processes[j] = child_processes[j+1];
				}
				(*child_index)--;
				break;
			}
		}
	}
}

// parsing the input 
void parse_input(char* args[], char** inputfile, char** outputfile, char* input_copy, int* i){
	*inputfile = NULL;   // Ensure they start as NULL
    *outputfile = NULL;
	char* save;
	// tokenize arguments to use in execvp()
	char* command = strtok_r(input_copy, " ", &save);
	int j = 0;
	while(command != NULL){
		// check for input output redirection
		if(strcmp(command, "<") == 0){
			// if so skip it and add the next argument to the args array 
			command = strtok_r(NULL, " ", &save);
			*inputfile = command;
		}else if(strcmp(command, ">") == 0){
			command = strtok_r(NULL, " ", &save);
			*outputfile = command;
		}else{
			args[j] = command;
			j++;
		}
		command = strtok_r(NULL, " ", &save);
	}
	// null terminated args array
	args[j] = NULL;
	// i is tracking how many args there are
	(*i) = j;
}

// output handling funciton
void output_handle(char** outputfile, char** inputfile){
	if (*inputfile){
		// sets input file descriptor to the inputfile, read only
			int in_fd = open(*inputfile, O_RDONLY);
			// error handling
			if(in_fd == -1){
				perror("");
				exit(1);
			}
			// changes stdin to the file
			dup2(in_fd, STDIN_FILENO);
			close(in_fd);
		}
	// same as above but for output file
	if (*outputfile){
		// sets file discriptor to the output file, write only, creates if doesn't exist, truncates if it does.
		int out_fd = open(*outputfile, O_WRONLY | O_CREAT | O_TRUNC, 0600);
		if(out_fd == -1){
			printf("Error opening outputfile %s\n", outputfile);
			exit(1);
		}
		// changes stdout to ouputfile
		dup2(out_fd, STDOUT_FILENO);
		close(out_fd);
	}
}

// background input output handling
void background_output_handle(char** outputfile, char** inputfile){
	// if no input file, /dev/null is the file descriptor
	if(!(*inputfile)){
		int in_dev = open("/dev/null", O_RDONLY);
		if(in_dev == -1){
			perror("Error opening devnull\n");
			exit(1);
		}
		dup2(in_dev, STDIN_FILENO);
		close(in_dev);
	}
	// same as above but for output file
	if(!(*outputfile)){
		int out_dev = open("/dev/null", O_WRONLY);
		if(out_dev == -1){
			perror("Error opening devnull\n");
			exit(1);
		}
		dup2(out_dev, STDOUT_FILENO);
		close(out_dev);
	}
}


// cleans up terminated processes
void clean_up (char* args[], int child_processes[], int* child_index, int* status, int pid, int* num_forks, int* last_status){
	int i, j;
	// goes through child processes and cleans up the array to keep track of forks and processes
	for(i = 0; i < (*child_index); i++){
		if(child_processes[i] == pid){
			(*num_forks)--;
			for(j=i; j< (*child_index) -1; j++){
				child_processes[j] = child_processes[j+1];
			}
			(*child_index)--;
		}
	}
	// update last status because it is only for foreground processes
	*last_status = *status;
	// print if terminated by signal
	if(WIFSIGNALED(*status)){
		printf("process %d terminated with signal %d\n", pid, *last_status);
	}	
}

// creates a child process
void create_child(char* args[], int i, int child_processes[], char** inputfile, char** outputfile, int* num_forks, int* child_index, int* status, int* last_status, int tstp){
	pid_t pid;
	// make a child process
	pid = fork();
	(*num_forks)++;
	// if child
	if(pid == 0){
		struct sigaction sa_child_int = {0};
		// if "&" at the end and not in foreground mode
		if((strcmp(args[i-1], "&") == 0) && tstp == 0){
			// sigint handler, if background, ignore sigint
			sa_child_int.sa_handler = SIG_IGN;
			background_output_handle(outputfile, inputfile);
			args[i-1] = NULL;
		}else{
			// if in foreground mode ignore it
			if((strcmp(args[i-1], "&") == 0)){
				args[i-1] = NULL;
			}
			// default sigint response if foreground process
			sa_child_int.sa_handler = SIG_DFL;
		}
		// applies the signal handler
		sigaction(SIGINT, &sa_child_int, NULL);

		// output handling
		output_handle(outputfile, inputfile);

		// run execvp but if it has an error print it
		// execvp uses the args in the array to run
		if(execvp(args[0], args) == -1){
			perror("");
			// keep track of status
			*status = 1;
			exit(1);
		}
	}else{
		// parent
		// add to child processes array with pid
		child_processes[*child_index] = pid;
		(*child_index)++;
		// if successfull background process, print the pid and use WNOHANG
		if((strcmp(args[i-1], "&") == 0) && tstp == 0){
			printf("background pid is %d\n", pid);
			// sends to background because of WNOHANG
			waitpid(pid, status, WNOHANG);
		}else{
			// else not in background
			if((strcmp(args[i-1], "&") == 0)){
				args[i-1] = NULL;
			}
			// wait for it to terminate and then clean up
			pid_t terminated_pid = waitpid(pid, status, 0);
			clean_up(args, child_processes, child_index, status, terminated_pid, num_forks, last_status);
		}
	}
}

// global boolean for SIGTSTP
int tstp = 0;
// swaps the boolean and enters/exits into foreground mode
void catch_TSTP(int signal_number){
	if(tstp == 1){
		write(STDOUT_FILENO, "\n--exited foreground only--\n:", 29);
		tstp = 0;
	}else if(tstp ==0){
		write(STDOUT_FILENO, "\n--foreground only--\n:", 22);
		tstp =1;
	}
}




int main() {
	// just incase there is a suprise bomb
	int max_forks = 25;

	int max_input_size = 2049;
	int max_args = 512;
	int child_processes[max_forks];
	int num_forks = 0;
	int child_index = 0;
	int status = 0;
	int last_status = 0;


	char user_input[max_input_size];
	char* args[max_args];

	// input output files 
	char* outputfile = NULL;
	char* inputfile = NULL;

	struct sigaction sa_int = {0};
	struct sigaction sa_tstp = {0};
	
	// signal handlers SIGINT and SIGTSTP
	sigfillset(&sa_int.sa_mask);
	sa_int.sa_handler = SIG_IGN;
	sa_int.sa_flags = SA_RESTART;
	sigaction(SIGINT, &sa_int, NULL);

	sigfillset(&sa_tstp.sa_mask);
	sa_tstp.sa_handler = catch_TSTP;
	sa_tstp.sa_flags = SA_RESTART;
	sigaction(SIGTSTP, &sa_tstp, NULL);

	while(1){
		// check for terminated background processes
		check_background(&num_forks, &child_index, child_processes);

		printf(":");
		fflush(stdout);
		// get user input
		fgets(user_input, max_input_size, stdin);
		
		char input[strlen(user_input) + 1];
		user_input[strcspn(user_input, "\n")] = '\0';  // Remove newline from input
		// copy input not mess with user input when modifying
		strcpy(input, user_input);
		// ignore comments and empty lines
		if(input[0] == '#'){
		// if there is an input:
		}else if(strlen(input) > 0){
			// scan for "$$"
			scans(input);
			// copy again to modify safely
			char input_copy[strlen(input)+1];
			strcpy(input_copy, input);
			int i = 0;
			// parse input
			parse_input(args, &inputfile, &outputfile, input_copy, &i);

			if (strcmp(args[0],"exit") == 0){
				// exit
				exit_shell(child_processes, child_index);
			}else if(strcmp(args[0], "cd") == 0){
				// cd
				change_directory(args);
			}else if(strcmp(args[0], "status") == 0){
				// status
				check_status(&last_status);
			}else{
				// any other command
				create_child(args, i, child_processes, &inputfile, &outputfile, &num_forks, &child_index, &status, &last_status, tstp);
			}
			
		}
		
	}
}
