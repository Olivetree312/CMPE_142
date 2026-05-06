#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>

void usage(){
    printf("USAGE: ./converse DELIMITER program1 [args...] DELIMITER program2 [args...]\n");
    exit(1);
}

int main(int argc, char* argv[]){
    //not <7 because arguments not required
    if(argc < 5) usage();
    //validate & get args
    char* ptr, *prog1, *prog2;
    const char* delim = argv[1];
    int delim2=3;
    //find second delim
    while(strcmp(argv[delim2], delim)!=0){
        delim2++;
        ptr = argv[delim2];
        if(ptr==NULL) usage();
    }
    argv[delim2] = NULL;
    //parse program1 string
    prog1 = argv[2];
    //ARGS SHOULD INLUDE PROGRAM NAMES
    char **args1 = &argv[2];
    
    prog2 = argv[++delim2];
    //parse program2 string
    char **args2 = &argv[delim2];

    //create pipes with keys
    int pipe12[2], pipe21[2]; //0 is read, 1 is write
    //generates file descriptors to populate arr with
    if(pipe(pipe21)==-1){
        perror("pipe 21 creation failed");
        exit(1);
    } //prog2 stdout
    if(pipe(pipe12)==-1){
        perror("pipe 12 creation failed");
        exit(1);
    } //prog1 stdout
    int pid1 = fork();
    //make child execute prog1 -> read from prog2 stdout
    if(pid1<0){
        perror("fork failed");
        exit(1);
    }
    if(pid1==0){
        //close write to prog2 stdout
        close(pipe21[1]);
        //close read from prog1 
        close(pipe12[0]);
        //write to new prog1 stdout
        dup2(pipe12[1], 1);
        //read from new prog2 stdout
        dup2(pipe21[0], 0);
        execvp(prog1, args1);
        //only reaches here if failed
        perror("prog1 failed");
        exit(1);
    }
    int pid2 = fork();
    if(pid2==0){
        close(pipe12[1]);
        close(pipe21[0]);
        dup2(pipe21[1], 1);
        dup2(pipe12[0], 0);
        execvp(prog2, args2);
        perror("prog2 failed");
        exit(1);
    }
    // parent gotta close all pipe ends so EOF weRKs
    close(pipe12[0]); close(pipe12[1]);
    close(pipe21[0]); close(pipe21[1]);

    int status1, status2;
    //don't use WNOHANG, which returns immediately
    waitpid(pid1, &status1, 0);
    waitpid(pid2, &status2, 0);
    //status variable is packed bitfield, NOT actual exit val
    int first = WIFEXITED(status1) ? WEXITSTATUS(status1) : 1;
    int second = WIFEXITED(status2) ? WEXITSTATUS(status2) :1;
    return first > second ? first : second;
}