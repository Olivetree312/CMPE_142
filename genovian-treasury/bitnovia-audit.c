#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/*#include <fcntl.h> //file control
#include <unistd.h> //sys call wrappers: fork, read, write
#include <sys/mman.h> //mmap 
#include <sys/stat.h> //file status & metadata
#include <assert.h> //checks if a condition is true; if false, it prints an error message to stderr
*/
#include <pthread.h>
//going to use 3 struct types: Citizen, ThreadArg, Result -> arrays
//processing: individual locks for citizens = fine grain
// citizen id = arr index+1
typedef struct Citizen {
    //balance + lock
    long balance;
    pthread_mutex_t lock; 
} Citizen;
//global array of citizens, each w lock
static unsigned int N;
static Citizen *citizens = NULL;

//thread comm: threads take 1 arg -> bundle into struct
typedef struct{
    char *filename;
} ThreadArg;
//final output: sorting result by balance
//copy arr index & balance to sort citizens wo mutex
typedef struct{
    //balance + id
    unsigned int id;
    long balance;
} Result;

//each thread: open file, read file, locks 2 citizens, update balance, unlock
void *process_file(void *arg){
    //cast void ptr back to struct
    ThreadArg *targ = (ThreadArg *)arg;
    FILE *fp = fopen(targ->filename, "r");
    //cont even if file error
    if(fp==NULL){
        perror(targ->filename);
        return NULL;
    }
    long amount;
    //index is 1 less than id
    unsigned int from_id, to_id;
    unsigned int from_index, to_index;
    unsigned int first, second;
    
    while(fscanf(fp, "%ld %u %u", &amount, &from_id, &to_id)==3){
        //array index needs -1 since ids start at 1
        from_index= from_id -1;
        to_index = to_id -1;
        //selects smaller id to go first 
        //locking multiple resources needs global lock ordering
        first = from_index < to_index ? from_index : to_index;
        second = from_index < to_index ? to_index : from_index;
        //locking citzens in order = safety to prevent deadlock in case same pairs updated
        //first citizen gets exclusive access to file first
        //have to make sure no "complementary pieces locking together"
        pthread_mutex_lock(&citizens[first].lock);
        pthread_mutex_lock(&citizens[second].lock);
        citizens[from_index].balance -= amount;
        citizens[to_index].balance += amount;
        //unlocking citizens in reverse order (close inner then outer)
        pthread_mutex_unlock(&citizens[second].lock);
        pthread_mutex_unlock(&citizens[first].lock);
    }
    fclose(fp);
    //always return null
    return NULL;
}
//higher balance, then smaller id
// 1=rb first, -1=ra first, 0= equal
//to be used with qsort
int compare_results(const void *a, const void *b){
    const Result *ra = (const Result *) a;
    const Result *rb = (const Result *) b;
    //rb before ra if greater
    if(ra->balance < rb->balance) return 1;
    if(ra->balance > rb->balance) return -1;
    if(ra->id < rb->id) return -1;
    if(ra->id > rb->id) return 1;
    return 0;
}

int main(int argc, char **argv) {
//error handling
    if(argc<3){
        fprintf(stdout, "USAGE: ./bitnovia-audit NUMBER_OF_RESIDENTS transfer_file ...\n");
        exit(1);
    }
    N = (unsigned int) atoi(argv[1]);
    if(N < 1 || N > 100000){
        fprintf(stderr, "Invalid number of residents\n");
        exit(2);
    }
//init Citizens arr
    citizens = malloc(N*sizeof(Citizen));
    if(citizens == NULL){
        fprintf(stderr, "malloc\n");
        exit(1);
    }
    for(int i=0; i<N; i++){
        citizens[i].balance = 1000;
        //eacch citizen has mutex obj
        pthread_mutex_init(&citizens[i].lock, NULL);
    }
//making thread & arg obj for each file
    int num_files = argc-2;
    //remember structs & pthread is their own type --> unique size
    pthread_t *threads = malloc(num_files * sizeof(pthread_t));
    ThreadArg *args = malloc(num_files * sizeof(ThreadArg));
    //whoop gotta free mem when anything goes wrong
    if(threads == NULL || args == NULL){
        perror("malloc\n");
        free(citizens);
        free(threads);
        free(args);
        exit(1);
    }
    //populating args w filenames -> matching to thread
    //start thread -> run process_file w data in args
    for(int i=0; i<num_files; i++){
        args[i].filename = argv[i+2];
        pthread_create(&threads[i], NULL, process_file, &args[i]);
    }
    //wait for all threads to finish before printing ouptut
    for(int i=0; i<num_files; i++){
        pthread_join(threads[i], NULL);
    }
//making result arr w id = index+1 for citizens
    Result *results = malloc(N * sizeof(Result));
    if (results == NULL) {
        perror("malloc\n");
        free(threads);
        free(args);
        free(citizens);
        return 1;
    }
    for (unsigned int i = 0; i < N; i++) {
        results[i].id = i + 1;
        results[i].balance = citizens[i].balance;
    }

//sorting Result arr w qsort
    qsort(results, N, sizeof(Result), compare_results);
//print top 3
    for (unsigned int i = 0; i < 3 && i < N; i++) {
        printf("%u %ld\n", results[i].id, results[i].balance);
    }
//print bottom 3 (the pOORs)
//corner case: the top richest are also the poorest
    unsigned int start = (N >= 3) ? N - 3 : 0;
    for (unsigned int i = start; i < N; i++) {
        printf("%u %ld\n", results[i].id, results[i].balance);
    }
//destroy lock for each citizen
    for (unsigned int i = 0; i < N; i++) {
        pthread_mutex_destroy(&citizens[i].lock);
    }

    free(results);
    free(threads);
    free(args);
    free(citizens);

    return 0;
}