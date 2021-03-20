#include<stdio.h>
#include<stdlib.h>
#include<unistd.h>
#include<fcntl.h>
#include<string.h>
#include<wait.h>
int lenstr(char* str){
    int count=0;
    for(int i=0;;i++){
        if(str[i] == '\0') break;
        else count++;
    }
    return count;
}
int count(char* cmd, const char c){
    int cnt = 0;
    for(int i=0; i<lenstr(cmd); i++) if(cmd[i] == c) cnt++;
    return cnt;
}

char** split(char* str, const char* del, int equal){
    int words = count(str, del[0]);
    words += 1;
	if(!equal) words += 1;

    char** split = malloc(words * sizeof(char*));
    char* token = strtok(str, del);
    
    int i = 0;
    while(token != NULL) {
        split[i++] = token;
        token = strtok(NULL, del);           
    }

    split[words-1] = (char*)0;
    return split;
}
void dbg(char** arr, const char* stmt){
    fprintf(stderr, "%s: ", stmt);
    for(int i=0; ; i++){
        if(arr[i])
            fprintf(stderr, "%d: %s ", i, arr[i]);
        else break;
    }
    fprintf(stderr, "\n");
}
void construct(char** path, char* command){
    for(int i=0;;i++){
        if(!path[i]) break;
        char* to_add = (char*)malloc((2 + lenstr(path[i]) + lenstr(command) + 1) * sizeof(char));
        strcpy(to_add, "./");
        strcat(to_add, path[i]);
        strcat(to_add, "/");
        strcat(to_add, command);
		path[i] = to_add;   
	}
    return;
}
int executeCommand (char *cmd) {
    char* path = getenv("CS330_PATH");
    if(path == NULL) exit(-1);
    int wcmd = count(cmd, ' '), wpath = count(path, ':');

    char** command = split(cmd, " ", 0);
    char** path_to_exec = split(path, ":", 0);

    construct(path_to_exec, command[0]);

    //dbg(command, "from internal dbg : command"); 
    //dbg(path_to_exec, "path_to_exec");
    
    int i, status;
    pid_t pid, cpid;
    for(int i=0; i<=wpath; i++){
        pid = fork();
        if(pid < 0){
            perror("fork");
            exit(-1);
        }
        if(!pid){
            if(execv(path_to_exec[i], command))
                exit(-1);
        }
        cpid = wait(&status);
        if(WEXITSTATUS(status)) continue;
        else return 0;
    }
    printf("UNABLE TO EXECUTE\n");
    return -1;
}

int execute_in_parallel(char *infile, char *output){
	int len = lenstr(output) + 2;
	char outfile[len]; 
	strcpy(outfile, "./");
	strcat(outfile, output);

	// extracting commands
	int in_fd = open(infile, O_RDONLY);
	if(in_fd < 0) return -1;

	char buff[1024];
	int bytes = read(in_fd, buff, 1024);
	int ncom = count(buff, '\n');
	char** commands = split(buff, "\n", 1);

	// extracting paths
	char* path = getenv("CS330_PATH");
    int wpath = count(path, ':'); 

	int p[ncom][2];
	for(int i=0; i<ncom; i++){
		if(pipe(p[i]) < 0) exit(1);
		
		pid_t pid, cpid;
		pid = fork();
        if(pid < 0){
            perror("fork");
            exit(-1);
        }
		
        if(!pid){
			close(p[i][0]);
			dup2(p[i][1], 1);
			executeCommand(commands[i]);
            exit(-1);
        }
	}

	int out = open(outfile, O_WRONLY|O_CREAT, 0666);

	dup2(out, 1);
	for(int i=0; i<ncom; i++){
		close(p[i][1]);
		char str[4096];
		int lenbuff = read(p[i][0], str, 4096);

		if(lenbuff > 0){
			str[lenbuff] = '\0';
			printf("%s", str);
		}
	}
	return 0;
}

int main(int argc, char *argv[])
{
	return execute_in_parallel(argv[1], argv[2]);
}
