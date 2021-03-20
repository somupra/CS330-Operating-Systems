#include<stdio.h>
#include<stdlib.h>
#include<unistd.h>
#include <string.h>
void hello(){
    fprintf(stderr, "hello\n");
}
int executeCommand (char *cmd) {
    pid_t pid;
    pid = fork();
    if(pid < 0){
         perror("fork");
         exit(-1);
    }
        
    if(!pid){ 
        // hello();
        char *env = (char*) malloc(512*sizeof(char));
        char *tok;
        char *tmp = (char*) malloc(128*sizeof(char));
        char *arg[7];
        tok = strtok (cmd , " ");
        int i = 0;
        while (tok != NULL)
        {
            arg[i] = tok;
            i++;
            tok = strtok (NULL, " ");
        }
        
        // hello();

        // fprintf(stderr, "i is %d\n", i);
        char** argv = (char**)malloc(sizeof(char*)*(i+1));
        
        // hello();
        for (int j = 0; j<i; j++){
            argv[j] = arg[j];
        }
        argv[i] = (char*)NULL;

        // fprintf(stderr, "%s\n", argv[0]);
        // hello();
        env = getenv("CS330_PATH");
        // printf("%s\n", env);
        if (env == NULL){
            // printf ("UNABLE TO EXECUTE\n");
            return -1;
        }

        // hello();
    
        tok = strtok (env,":");
        while (tok != NULL)
        {
            strcpy (tmp,tok);
            strcat(tmp,"/");
            strcat(tmp, arg[0]);

            // fprintf(stderr, "%s\n", tmp);
            if(execv(tmp, argv)){
                fprintf(stderr, "fail for %s\n", argv[0]);
                tok = strtok (NULL, ":");
            }
        }
        printf("UNABLE TO EXECUTE\n");
        return -1;
    }
    return 0;
}

int main (int argc, char *argv[]) {
    return executeCommand(argv[1]);
}