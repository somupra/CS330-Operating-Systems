#include<stdio.h>
#include<stdlib.h>
#include<unistd.h>

int play(char a, char b){
	// fprintf(stderr, "Moves: %d %d\n", a, b);
	if(a == b) return 0;
    switch(a){
        case '0': if(b == '1') return -1;
		          if(b == '2') return 1;
                  break;
        case '1': if(b == '0') return 1;
		          if(b == '2') return -1;
                  break;
        case '2': if(b == '0') return -1;
		          if(b == '1') return 1;
                  break;
    }
}
int main(int argc, char* argv[]) 
 {
    int pid, input[2][2], output[2][2];
    char moves[2][10];
    
    // Child for PLAYER 1
    char* p[2];
    int pos;
    for(int i=1; i<=2; i++){
        pos = 0;
        for (int k = 0; argv[i][k]!='\0'; k++){
            if (k == '/')
                pos = k+1;
        }
        p[i-1] = argv[i]+pos;
    }
    
    if(pipe(input[0]) < 0 || pipe(output[0])){
        perror("pipe");
        exit(-1);
    }

    pid = fork();   
    if(pid < 0){
        perror("fork");
        exit(-1);
    }  


    if(!pid){ 
        close(output[0][0]); // close read end for output in child 
        close(input[0][1]); // close write end for input in child
        close(0);
        dup(input[0][0]);

        close(1); 
        dup(output[0][1]); 


        if (execl(argv[1], p[0], NULL)){
            perror("execl");
            exit(-1);
        }
    }else{
        close(output[0][1]);    
        close(input[0][0]);   
    }

    
    for (int i = 0; i<10; i++){
        if (write(input[0][1], "GO\0", 3) == 3){
            char str[1];
            int ncr = read(output[0][0], str, 1);
            if(ncr < 0){
                fprintf(stderr, "bad reading...\n");
                exit(-1);
            }
            moves[0][i] = str[0];
        }
    }
    // printf("moves pa: %s\n", moves[0]);

    close(output[0][0]); // close the remaining ends of pipe
    close(output[0][1]);

    // Create child process for PLAYER 2
    if(pipe(input[1]) < 0 || pipe(output[1]) < 0){
        perror("pipe");
        exit(-1);
    }

    pid = fork();   
    if(pid < 0){
        perror("fork");
        exit(-1);
    }  


    if(!pid){ // Child 
        close(output[1][0]);      // Close the read end in child for writing output to pipe
        close(input[1][1]);      // Close the write end in child for taking input GO
        
        close(0);
        dup(input[1][0]); 

        close(1);
        dup(output[1][1]); 

        if (execl(argv[2], p[1], NULL)){
            perror("execl");
            exit(-1);
        }
    }

    close(output[1][1]);    // Close the write end in the parent for taking output from child
    close(input[1][0]);    // Close the read end in the parent for giving input
    for (int i = 0; i<10; i++){
        if (write(input[1][1], "GO\0", 3) == 3){
            char str[1];
            int ncr = read(output[1][0], str, 1);
            if(ncr == 1) moves[1][i] = str[0];
            else{
                fprintf(stderr, "bad reading...\n");
                exit(-1);
            }
        }
    }
    // printf("moves pb: %s\n", moves[1]);
    close(output[1][0]);                                //close remaining ends of both pipes
    close(output[1][1]);
    

    int s1 = 0, s2 = 0;
    for (int i = 0; i<10; i++){
        int res = play(moves[0][i], moves[1][i]);
        if(res == 0) continue;
        if(res > 0) s1++;
        else s2++;
    }
    printf("%d %d", s1, s2);
    return 0;
}