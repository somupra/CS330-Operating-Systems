#include<stdio.h>
#include<stdlib.h>
#include<unistd.h>
#include<fcntl.h>
#include<string.h>
#include<wait.h>
#include "gameUtils.h"
#define ROCK        0 
#define PAPER       1 
#define SCISSORS    2 
#define STDIN 		0
#define STDOUT 		1
#define STDERR		2
int play(int a, int b){
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
int getWalkOver(int numPlayers); // Returns a number between [1, numPlayers]

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

int** getpairs(int* elig, int n, int bye);

int main(int argc, char *argv[])
{
	int fd = open("debug.txt", O_WRONLY | O_CREAT, 0666);
	char* input;
	int rounds;
	if(argc > 2){
		rounds = atoi(argv[2]);
		input = argv[3];
	}else{
		rounds = 10;
		input = argv[1];
	}
	//extracting players
	int in_fd = open(input, O_RDONLY);
	if(in_fd < 0) exit(-1);

	char buff[20000];
	int bytes = read(in_fd, buff, 20000);
	int ncom = count(buff, '\n');
	char** players = split(buff, "\n", 1);


	int n = atoi(players[0]);
	int fp=1;
	for(int i=0; i<n; i++){
		if(fp){
			printf("p%d", i);
			fp=0;
		}else{
			printf(" p%d", i);
		}
	}
	printf("\n");
	
	int ip_pipes[n][2], op_pipes[n][2];
	// create input and output pipes
	for(int i=0; i<n; i++){
		pipe(ip_pipes[i]);
		pipe(op_pipes[i]);
	}

	pid_t pid;
	for(int p=0; p<n; p++){
		pid = fork();
		if(pid < 0){perror("fork"); exit(-1);}
		
		// create pth child process to play
		if(!pid){
			// at this moment this child is connected to all the n pipes, close its connection to all but the pth pipe
			for(int i=0; i<n; i++) if(i != p) close(ip_pipes[i][0]), close(op_pipes[i][1]), close(ip_pipes[i][1]), close(op_pipes[i][0]);

			// after this remove its connection to the write end of the input pipe and the read end of the output pipe
			close(ip_pipes[p][1]), close(op_pipes[p][0]);

			// now dup the ip_pipe and op_pipe to stdin and stdout
			dup2(ip_pipes[p][0], STDIN); // stdin is closed for child, it will use read end of input pipe
			dup2(op_pipes[p][1], STDOUT); // stdout is closed for child, it will use write end of output pipe
			
			// get the executable path (append null to the array just)
			char** executable = (char**)malloc(2*sizeof(char*));
			executable[0] = (char*)malloc(lenstr(players[p+1])*sizeof(char));
			cpystr(executable[0], players[p+1]);
			executable[1] = NULL;
			
			if (execv(players[p+1], executable)) {
				fprintf(stderr, "Cannot execute player\n");
			};

			exit(-1);
		}
	}

	// at this moment all children got their pipes, now we need to start the tournament and have the matches
	// but we need to close the pipes for parent also
	for(int i=0; i<n; i++) close(ip_pipes[i][0]), close(op_pipes[i][1]);

	
	int rem = n;
	int elig[n]; for(int i=0; i<n; i++) elig[i] = 1;

	while(rem > 1){
		int bye = (rem%2) ? getWalkOver(rem) : 0; bye--;
		int playing[rem]; 
		int idx=0;
		for(int i=0; i<n; i++){
			if(elig[i]){
				playing[idx] = i;
				idx++;
			}
		}
		
		int** pairs = getpairs(playing, rem, bye);
		
		int scores[rem/2][2];
		for(int i=0; i<rem/2; i++) scores[i][0] = 0, scores[i][1] = 0;

		for(int r=0; r<rounds; r++){
			// for each round do a match for all the pairings
			for(int p=0; p<rem/2; p++){
				int p1_move, p2_move;
				if(write(ip_pipes[pairs[p][0]][1], "GO", 3) == 3){
					char buff[1];
					int r_size = read(op_pipes[pairs[p][0]][0], buff, 1);
					if(r_size == 1){
						p1_move = (int)buff[0] - (int)'0';
					}else{
						fprintf(stderr, "Couldn't read, buff size: %d\n", r_size);
					};
				}
				if(write(ip_pipes[pairs[p][1]][1], "GO", 3) == 3){
					// dprintf(DEBUG, "wrote successfully\n");
					char buff[1];
					int r_size = read(op_pipes[pairs[p][1]][0], buff, 1);
					if(r_size == 1){
						p2_move =  (int)buff[0] - (int)'0';
					}else{
						fprintf(stderr, "Couldn't read, buff size: %d\n", r_size);
					};
				}
				int res = play(p1_move, p2_move);
				if(res > 0) scores[p][0]++;
				else if(res < 0) scores[p][1]++;
			}
		}

		// all the rounds have been completed, now eliminate players with less score
		for(int i=0; i<rem/2; i++){
			int loser = (scores[i][0] < scores[i][1]) ? pairs[i][0] : pairs[i][1];
			
			// kick out the loser from the tournament
			close(ip_pipes[loser][1]);
			close(op_pipes[loser][0]);

			// mark him as not eligible
			elig[loser] = 0;
			rem --;
		}
		
		// print the candidates for the next round and free pairs
		int countelig=0;
		int first=1;
		for(int i=0; i<n; i++){
			if(elig[i]){
				if(first){
					printf("p%d", i);
					first=0;
				}
				else printf(" p%d", i);
				countelig++;
			}
		}
		if(countelig > 1)printf("\n");
		free(pairs);
	}
	
	return 0;
}
int** getpairs(int* elig, int n, int bye){
	
	int** pairs = (int**) malloc((int)(n/2) * sizeof(int*));
	int size = n;
	if(bye >= 0) size--;
	int play[size];
	int idx = 0;
	for(int i=0; i<bye; i++){
		play[idx++] = elig[i];
	}
	for(int i=bye+1; i<n; i++){
		play[idx++] = elig[i];
	}
	
	int p=0;
	for(int i=0; i<n/2; i++){
		pairs[i] = (int*)malloc(2 * sizeof(int));
		pairs[i][0] = play[p];
		pairs[i][1] = play[p+1];
		p+=2;
	}
	
	return pairs;
}
