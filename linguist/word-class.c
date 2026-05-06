#define _GNU_SOURCE //needed for %ms, a GNU feature
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <ctype.h> //find+ convert types

#define NUM_CLASSES 6
#define HASH_SIZE 4096 //4kb

//linked list queue of words
typedef struct QueueNode{
    char *word;
    struct QueueNode *next;
} QueueNode;
typedef struct Queue{
    QueueNode *head;
    QueueNode *tail;
    int done; //signal no more input
    pthread_mutex_t lock; //mutual exclusion
    pthread_cond_t cond; //thread wakeup on cond
}Queue;
//hashtable doesn't need lock, next for collision chain
typedef struct HashNode{
    char *word;
    unsigned int count; //word freq
    struct HashNode *next; //collsion chain
}HashNode;
typedef struct{
    char *filename;
} ReaderArg;
typedef struct{
    unsigned int class_id;
} CounterArg;

//global arr
Queue queues[NUM_CLASSES]; //queue for each class
HashNode *tables[NUM_CLASSES][HASH_SIZE]; //6 hashtables
unsigned int distinct_counts[NUM_CLASSES]; //unique words in each class

char *class_names[NUM_CLASSES]={
    "lawful-evil",
    "neutral-evil",
    "chaotic-evil",
    "lawful-good",
    "neutral-good",
    "chaotic-good"
};
char *output_files[NUM_CLASSES] = {
    "lawful-evil.cnt",
    "neutral-evil.cnt",
    "chaotic-evil.cnt",
    "lawful-good.cnt",
    "neutral-good.cnt",
    "chaotic-good.cnt"
};
unsigned int hash_word(char *word){
    //djb2 -> good starting seed (thank u daniel)
    unsigned int hash = 5381;
    while(*word != '\0'){
        //curr hash *2, plus ascii val
        hash = hash * 33 + (unsigned char) (*word);
        word++;
    }
    //div total to keep within table bounds
    return hash % HASH_SIZE;
}
// lowercase + remove trailing non alpha
void clean_word(char *word){
    for(int i=0; word[i]!='\0'; i++){
        word[i] = tolower((unsigned char)word[i]);
    }
    int len = strlen(word);
    while(len > 0 && !isalpha((unsigned char)word[len-1])){
        word[len-1] = '\0';
        len--;
    }
}
//ret class index, -1 for invalid
int classify_word(char *word){
    int len = strlen(word);
    if(len==0){return -1;}
    char last = word[len-1];
    if (last == 'o' || last == 'u') return 0; // lawful evil
    if (last == 'i') return 1;                // neutral evil
    if (last == 'a' || last == 'e') return 2; // chaotic evil

    if (last == 'p' || last == 'q' || last == 'r' ||
        last == 's' || last == 't' || last == 'v' ||
        last == 'w' || last == 'x' || last == 'y' ||
        last == 'z') return 3; // lawful good

    if (last == 'n') return 4; // neutral good

    if (last == 'b' || last == 'c' || last == 'd' ||
        last == 'f' || last == 'g' || last == 'h' ||
        last == 'j' || last == 'k' || last == 'l' ||
        last == 'm') return 5; // chaotic good

    return -1;
}
//adding word to queue for class
void enqueue(unsigned int class_id, char *word){
    QueueNode *node = malloc(sizeof(QueueNode));
    if(node==NULL){
        perror("malloc\n");
        free(word);
        exit(1);
    }
    node->word = word;
    node->next = NULL;
    pthread_mutex_lock(&queues[class_id].lock);
    if(queues[class_id].tail==NULL){
        queues[class_id].head = node;
        queues[class_id].tail = node;
    }
    else{
        queues[class_id].tail->next = node;
        queues[class_id].tail = node;
    }
    //signal to class queue wait cond that it's done
    pthread_cond_signal(&queues[class_id].cond);
    pthread_mutex_unlock(&queues[class_id].lock);
}
char *dequeue(unsigned int class_id){
    QueueNode *node = queues[class_id].head;
    if(node==NULL)return NULL;
    queues[class_id].head = node->next;
    if(queues[class_id].head==NULL){
        queues[class_id].tail =NULL;
    }
    char *word = node->word;
    free(node);
    return word;
}
// add word into hash table for that class
void add_to_table(unsigned int class_id, char *word) {
    unsigned int index = hash_word(word);
    HashNode *cur = tables[class_id][index];
    while (cur != NULL) {
        if (strcmp(cur->word, word) == 0) {
            cur->count++;
            free(word);
            return;
        }
        cur = cur->next;
    }
    HashNode *new_node = malloc(sizeof(HashNode));
    if (new_node == NULL) {
        perror("malloc");
        free(word);
        exit(1);
    }
    new_node->word = word;
    new_node->count = 1;
    new_node->next = tables[class_id][index];

    tables[class_id][index] = new_node;
    distinct_counts[class_id]++;
}
//reader thread opens 1 file, reads words, classifies, queues
void *reader_thread(void *arg){
    ReaderArg *rarg = (ReaderArg *)arg;
    FILE *fp = fopen(rarg->filename, "r");
    if(fp==NULL){
        perror(rarg->filename);
        return NULL;
    }
    char *word = NULL;
    //%m auto alloc mem, s reads string
    // -> read next word, memalloc, store ptr in word
    while(fscanf(fp, "%ms", &word)==1){
        clean_word(word);
        int class_id = classify_word(word);
        if(class_id >=0){
            enqueue(class_id, word);
        }
        else{
            free(word);
        }
        word = NULL;
    }
    fclose(fp);
    return NULL;
}

// write class's hash table into its output file+print
void write_output_file(unsigned int class_id) {
    FILE *fp = fopen(output_files[class_id], "w");
    if (fp == NULL) {
        perror(output_files[class_id]);
        return;
    }
    for (int i = 0; i < HASH_SIZE; i++) {
        HashNode *cur = tables[class_id][i];
        while (cur != NULL) {
            fprintf(fp, "%u %s\n", cur->count, cur->word);
            cur = cur->next;
        }
    }
    fclose(fp);
}
// each counter thread owns one queue + one hash table
void *counter_thread(void *arg) {
    //cast void back to CounterArg type
    CounterArg *carg = (CounterArg *)arg;
    //0 indexed, starting from lawful-evil
    unsigned int class_id = carg->class_id;
    //infinite loop until no more words
    while (1) {
        //lock class's queue before checking/ changing
        pthread_mutex_lock(&queues[class_id].lock);
        //count thread sleep while queue empty + reading not done
        //temp unlock mutex while sleeping
        while (queues[class_id].head == NULL && !queues[class_id].done) {
            pthread_cond_wait(&queues[class_id].cond, &queues[class_id].lock);
        }
        //IF queue empty & main says read done -> unlock + leave
        if (queues[class_id].head == NULL && queues[class_id].done) {
            pthread_mutex_unlock(&queues[class_id].lock);
            break;
        }
        //dequeue word then unlock
        char *word = dequeue(class_id);
        pthread_mutex_unlock(&queues[class_id].lock);
        //add word to class hashtable/ increase coutn
        if (word != NULL) {
            add_to_table(class_id, word);
        }
    }
    //write class result to .cnt file
    write_output_file(class_id);
    return NULL;
}

void free_hash_tables(void){
    for(int class_id=0; class_id < NUM_CLASSES; class_id++){
        for(int i=0; i< HASH_SIZE; i++){
            HashNode *cur = tables[class_id][i];
            while(cur != NULL){
                HashNode *next = cur->next;
                free(cur->word);
                free(cur);
                cur = next;
            }
        }
    }
}

int main(int argc, char* argv[]){
       if (argc < 2) {
        fprintf(stdout, "USAGE: ./word-class file1 [file2 ...]\n");
        exit(1);
    }

    pthread_t counter_threads[NUM_CLASSES];
    CounterArg counter_args[NUM_CLASSES];

    // init queues, hash tables, counter threads
    for (int i = 0; i < NUM_CLASSES; i++) {
        queues[i].head = NULL;
        queues[i].tail = NULL;
        queues[i].done = 0;
        pthread_mutex_init(&queues[i].lock, NULL);
        pthread_cond_init(&queues[i].cond, NULL);
        distinct_counts[i] = 0;
        for (int j = 0; j < HASH_SIZE; j++) {
            tables[i][j] = NULL;
        }
        counter_args[i].class_id = i;
        pthread_create(&counter_threads[i], NULL, counter_thread, &counter_args[i]);
    }
    int num_files = argc - 1;
    pthread_t *reader_threads = malloc(num_files * sizeof(pthread_t));
    ReaderArg *reader_args = malloc(num_files * sizeof(ReaderArg));
    if (reader_threads == NULL || reader_args == NULL) {
        perror("malloc");
        free(reader_threads);
        free(reader_args);
        exit(1);
    }
    // make one reader thread per file
    for (int i = 0; i < num_files; i++) {
        reader_args[i].filename = argv[i + 1];
        pthread_create(&reader_threads[i], NULL, reader_thread, &reader_args[i]);
    }
    // wait for readers to finish first
    for (int i = 0; i < num_files; i++) {
        pthread_join(reader_threads[i], NULL);
    }
    // tell all counter threads that no more words are coming
    for (int i = 0; i < NUM_CLASSES; i++) {
        pthread_mutex_lock(&queues[i].lock);
        queues[i].done = 1;
        pthread_cond_signal(&queues[i].cond);
        pthread_mutex_unlock(&queues[i].lock);
    }
    // wait for counters
    for (int i = 0; i < NUM_CLASSES; i++) {
        pthread_join(counter_threads[i], NULL);
    }
    // print final counts
    for (int i = 0; i < NUM_CLASSES; i++) {
        printf("%s: %u\n", class_names[i], distinct_counts[i]);
    }
    // destroy locks/ condition vars
    for (int i = 0; i < NUM_CLASSES; i++) {
        pthread_mutex_destroy(&queues[i].lock);
        pthread_cond_destroy(&queues[i].cond);
    }
    free_hash_tables();
    free(reader_threads);
    free(reader_args);
    return 0;
}