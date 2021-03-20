#include<stdio.h>
#include<stdlib.h>
#include<unistd.h>
#include<string.h>
#include<wait.h>

int count(char* cmd, const char c){
    int cnt = 0;
    for(int i=0; i<strlen(cmd); i++) if(cmd[i] == c) cnt++;
    return cnt;
}

char** split(char* str, const char* del){
    int words = count(str, del[0]);
    words += 2;

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
            fprintf(stderr, "%s ", arr[i]);
        else break;
    }
    fprintf(stderr, "\n");
}
void construct(char** path, char* command){
    for(int i=0;;i++){
        if(!path[i]) break;
        char* to_add = (char*)malloc((2 + strlen(path[i]) + strlen(command) + 1) * sizeof(char));
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

    char** command = split(cmd, " ");
    char** path_to_exec = split(path, ":");

    construct(path_to_exec, command[0]);

    // dbg(command, "from internal dbg : command"); 
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

int main (int argc, char *argv[]) {
	return executeCommand(argv[1]);
}
