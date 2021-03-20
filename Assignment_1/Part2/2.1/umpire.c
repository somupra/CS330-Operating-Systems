#include<stdio.h>
#include<stdlib.h>
#include<unistd.h>
#include<fcntl.h>
#include<string.h>
#include<wait.h>
# define PLAYERS 2
# define ROUNDS 10

int lenstr(char* str){
    int count=0;
    for(int i=0;;i++){
        if(str[i] == '\0') break;
        else count++;
    }
    return count;
}
void cpystr(char* str1, char*str2){
	for(int i=0; i<lenstr(str2); i++){
		str1[i] = str2[i];
	}
	return;
}

void catstr(char* str1, char* str2){
	int idx = lenstr(str1);
	for(int i=0; i<lenstr(str2); i++){
		str1[idx++] = str2[i];
	}
	str1[idx] = '\0';
	return;
}


int play(int a, int b){
	// fprintf(stderr, "Moves: %d %d\n", a, b);
	if(a == b) return 0;
	if(a == 0){
		if(b == 1) return -1;
		if(b == 2) return 1;
	}else if(a == 1){
		if(b == 0) return 1;
		if(b == 2) return -1;
	}else{
		if(b == 0) return -1;
		if(b == 1) return 1;
	}
}
int main(int argc, char* argv[]) {
	
	char* path[2];
	for(int i=0; i<2; i++){
		path[i] = (char*) malloc(lenstr(argv[i+1])+2);
		cpystr(path[i], "./");
		catstr(path[i], argv[i+1]);
	}
	char** executable[2];
	for(int p=0; p<PLAYERS; p++){
		executable[p] = (char**)malloc(2*sizeof(char*));
		executable[p][0] = malloc(lenstr(argv[1]) * sizeof(char)); cpystr(executable[p][0], argv[p+1]);
		executable[p][1] = NULL;
	}
	

	int scores[PLAYERS] = {0};
	int pipes[PLAYERS][2][2];

	for(int i=0; i<2; i++){
		pipe(pipes[0][i]);
	}
	pid_t pid, cpid;
	pid = fork();
	if(pid < 0){perror("fork"); exit(-1);}
	
	// create first child process to play
	if(!pid){
		close(pipes[0][0][1]);
		dup2(pipes[0][0][0], 0); // stdin is closed for child, it will use read end of giving pipe
		dup2(pipes[0][1][1], 1); // stdout is closed for child, it will use write end of taking pipe
		
		if (execv(path[0], executable[0])) {
			fprintf(stderr, "couldn't execute player\n");
		};

		exit(-1);
	}
	if(pid){
		for(int i=0; i<2; i++){
			pipe(pipes[1][i]);
		}
		// parent process: create second child to play
		cpid = fork();
		if(cpid < 0){perror("fork"); exit(-1);}
		if(!cpid){
			close(pipes[1][0][1]);
			dup2(pipes[1][0][0], 0); // stdin is closed for child, it will use read end of giving pipe
			dup2(pipes[1][1][1], 1); // stdout is closed for child, it will use write end of taking pipe

			if (execv(path[1], executable[1])) {
				fprintf(stderr, "couldn't execute player\n");
			};

			exit(-1);
		}
	}
	
	for(int p=0; p<PLAYERS; p++){
		close(pipes[p][0][0]);	//close the read end of the input pipe
		close(pipes[p][1][1]);	//close the write end of the output pipe
	}
	
	
	for(int r=0; r < ROUNDS; r++){
		// for each round, give input to the child
		// parent should give input to the child to start the game
		// fprintf(stderr, "Starting round #%d\n", r+1);
		char begin[] = {'G', 'O', '\0'};
		int p1_move, p2_move;
		if(write(pipes[0][0][1], "GO", 3) == 3){
			// fprintf(stderr, "wrote successfully\n");
			char buff[1];
			int r_size = read(pipes[0][1][0], buff, 1);
			if(r_size == 1){
				p1_move = (int)buff[0] - (int)'0';
			}else{
				printf("Couldn't read, buff size: %d\n", r_size);
			};
		}
		if(write(pipes[1][0][1], "GO", 3) == 3){
			// fprintf(stderr, "wrote successfully\n");
			char buff[1];
			int r_size = read(pipes[1][1][0], buff, 1);
			if(r_size == 1){
				p2_move =  (int)buff[0] - (int)'0';
			}else{
				printf("Couldn't read, buff size: %d\n", r_size);
			};
		}
		int res = play(p1_move, p2_move);
		if(res == 0) continue;
		if(res > 0) scores[0]++;
		else scores[1]++;
	}
	for(int p=0; p<PLAYERS; p++){
		close(pipes[p][0][1]);	//close the write end of the input pipe
	}
	printf("%d %d", scores[0], scores[1]);
	return 0;
}
