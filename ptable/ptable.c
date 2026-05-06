#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include "ptable.h"

#define FILE_SIZE 65536
#define KEY_SIZE 255
#define DATA_SIZE 32768

static void *table;

static void init_table(void) {
//casts mmap table as ptable structure
    struct ptable_s *h = (struct ptable_s*) table;
    //does header contain magic num/ is valid ptable bc persistance = old contents there
    if (h->magic == PTABLE_MAGIC) {
        return;
    }
    //if not valid ptable yet -> clear mapped file contents & set header
    memset(table, 0, FILE_SIZE);
    h->magic = PTABLE_MAGIC;
    h->first_entry = 0;
    //settign next free entry = size of rest of file
    h->first_free = sizeof(struct ptable_s);
    struct ptable_entry_s *free_ent = offset_to_entry(h, h->first_free);
    free_ent->magic = PTABLE_ENTRY_MAGIC_FREE;
    free_ent->len = FILE_SIZE - sizeof(struct ptable_s);
    free_ent->next = 0;
    free_ent->data_len = 0;
    free_ent->key_len = 0;
}
//checks if entry key matches 
static int key_matches(struct ptable_entry_s *e, char *KEY) {
    size_t klen = strlen(KEY);
    //is data entry?: will match magic_data
    if (e->magic != PTABLE_ENTRY_MAGIC_DATA) return 0;
    //is same length?
    if (e->key_len != klen) return 0;
    //cmp actual bytes in key & KEY
    return memcmp(e->key, KEY, klen) == 0;
}
//lookin for key by searching linkd list of data
static struct ptable_entry_s *find_data_entry(char *KEY, uint16_t *prev_off_out) {
    //ptr to beg of table & offsets
    struct ptable_s *h = (struct ptable_s*)table;
    uint16_t prev = 0;
    uint16_t cur = h->first_entry;
    //end of list when cur offset=0
    while (cur != 0) {
        //need to convert offset to entry ptr each time (pt of ptable.h)
        struct ptable_entry_s *e = offset_to_entry(h, cur);
        //stroe prev node offset + return entry ptr
        if (key_matches(e, KEY)) {
            if (prev_off_out) *prev_off_out = prev;
            return e;
        }
        prev = cur;
        cur = e->next;
    }
    //no prev off if not found & set to nul
    if (prev_off_out) *prev_off_out = 0;
    return NULL;
}
//merge adjacent blocks w next = offset
static void coalesce_free_list(void){
    struct ptable_s *h = (struct ptable_s*)table;
    //traverse list & merge when offset+len = next
    uint16_t cur_off = h->first_free;
    while (cur_off != 0) {
        struct ptable_entry_s *cur = offset_to_entry(h, cur_off);
        uint16_t next_off = cur->next;
        //no next entry to merge
        if (next_off == 0) break;
        //ptr to next entry
        struct ptable_entry_s *next = offset_to_entry(h, next_off);
        if ((uint16_t)(cur_off + cur->len) == next_off) {
            //combine lengths + next = skip one ovr
            cur->len += next->len;
            cur->next = next->next;
        } else {
            cur_off = next_off;
        }
    }
}
static void insert_free_entry(uint16_t off, uint16_t len){
    struct ptable_s *h = (struct ptable_s*)table;
    //turning offset we want to insert at into entry ptr
    struct ptable_entry_s *new_free = offset_to_entry(h, off);
    //blank FREE entry of len size
    new_free->magic = PTABLE_ENTRY_MAGIC_FREE;
    new_free->len = len;
    new_free->data_len = 0;
    new_free->key_len = 0;
    new_free->next = 0;
    //if inserting at first free entry pos/ header, aka first_free==0 or < first_free
    if (h->first_free == 0 || off < h->first_free) {
        //this free block pts to old first_free so it is head
        new_free->next = h->first_free;
        //header first free is this free block
        h->first_free = off;
        //try to coalesce blocks
        coalesce_free_list();
        return;
    }
    //traverse from head of free list: get off -> make ptr
    uint16_t prev_off = h->first_free;
    struct ptable_entry_s *prev = offset_to_entry(h, prev_off);
    //incrementing prev off while !EOF free list & before this free block
    while (prev->next != 0 && prev->next < off) {
        prev_off = prev->next;
        prev = offset_to_entry(h, prev_off);
    }
    //this free block's next is prev->next, prev has reached off
    //this block placed at off prev's position & prev->next = off
    //prev -off-> this block -> (old next)
    new_free->next = prev->next;
    prev->next = off;
    coalesce_free_list();
}
//find free block big enough for new entry of need_len size
static uint16_t alloc_block(uint16_t need_len){
    struct ptable_s *h = (struct ptable_s*)table;
    uint16_t prev_off = 0;
    //iterator
    uint16_t cur_off = h->first_free;
    while (cur_off != 0) {
        struct ptable_entry_s *cur = offset_to_entry(h, cur_off);
        //if found block big enough
        if (cur->len >= need_len) {
            //find extra len
            uint16_t remaining = cur->len - need_len;
            //only split if leftover space large enough to hold another free block header
            if (remaining >= ENTRY_HEADER_SIZE) {
                //offset of extra block = curr_off+length needed
                uint16_t new_free_off = cur_off + need_len;
                //get new_free_off ptr from int new_free_off & initialize
                struct ptable_entry_s *new_free = offset_to_entry(h, new_free_off);
                new_free->magic = PTABLE_ENTRY_MAGIC_FREE;
                new_free->len = remaining;
                new_free->next = cur->next;
                new_free->data_len = 0;
                new_free->key_len = 0;
                //when first block found is big enough, set new_free_off as header
                if (prev_off == 0) {
                    h->first_free = new_free_off;
                } else {
                //
                    offset_to_entry(h, prev_off)->next = new_free_off;
                }
                return cur_off;
            } else {
                if (prev_off == 0) {
                    h->first_free = cur->next;
                } else {
                    offset_to_entry(h, prev_off)->next = cur->next;
                }
                return cur_off;
            }
        }
        prev_off = cur_off;
        cur_off = cur->next;
    }
    return 0;
}

void listKey(char* STR){
    struct ptable_s *h = (struct ptable_s *)table;
    //offset to first entry
    uint16_t cur = h->first_entry;
    while (cur != 0) {
        //ptr to first entry
        struct ptable_entry_s *e = offset_to_entry(h, cur);
        //if entry is a data entry, not free entry
        if (e->magic == PTABLE_ENTRY_MAGIC_DATA) {
            //if str found in key of e, print
            if (strstr(e->key, STR) != NULL) {
                //for unformatted binary data from mem -> writing len # of bytes
                fwrite(e->key, 1, e->key_len, stdout);
                fputc('\n', stdout);
            }
        }
        //EOF will be when next=0
        cur = e->next;
    }
}
//find key, unlink, into free list
void deleteKey(char* KEY){
    //find table entry ptr & prev key's offset
    //pt to tabl eheader
    struct ptable_s *h = (struct ptable_s*) table;
    uint16_t prev_off =0;
    struct ptable_entry_s *e = find_data_entry(KEY, &prev_off);
    if(e==NULL){
        fprintf(stderr, "KEY not found\n");
        exit(1);
    }
    //offset of entry ptr for free list to store/ that we're deleting
    //aka starting pt of free block
    uint16_t off = entry_to_offset(h, e);
    //delete curr header's first entry (e)/ skip over e
    if(prev_off == 0){//header is offset 0
        //first entry doesn't live at zero, use prev_off instead of off
        h->first_entry = e->next;
    }
    else{
        //prev skips over e, pts to e next
        offset_to_entry(h, prev_off)->next = e->next;
    }
    //curr entry has been deleted, insert free entry of e len
    insert_free_entry(off, e->len);
}
//key from cmd line, then data from stdin
//delete if existing, calc entry size, alloc, populate elem
void addKey(char* KEY, unsigned char* data, uint16_t data_len){
    struct ptable_s *h = (struct ptable_s *)table;
    //is existing entry? -> no duplicates in table
    struct ptable_entry_s *old = find_data_entry(KEY, NULL);
    if (old != NULL) {
        deleteKey(KEY);
    }
    //length needs to include struct ptable_s size, elem lengths vary
    uint16_t need_len = ENTRY_HEADER_SIZE + (uint16_t)strlen(KEY) + 1 + data_len;
    uint16_t off = alloc_block(need_len);
    //offset can nver be 0 since header lives there
    if (off == 0) {
        fprintf(stderr, "data too large\n");
        exit(3);
    }
    //convert offset of alloc space to entry ptr
    struct ptable_entry_s *e = offset_to_entry(h, off);
    e->magic = PTABLE_ENTRY_MAGIC_DATA;
    e->len = need_len;
    e->data_len = data_len;
    e->key_len = (uint8_t)strlen(KEY);
    e->next = 0;
    //copying KEY to e->key of key_len (doesn't include \0)
    memcpy(e->key, KEY, e->key_len);
    //last char of key is null terminator
    e->key[e->key_len] = '\0';
    memcpy(e->key + e->key_len + 1, data, data_len);
    //new entry placed as first
    if(h->first_entry==0){
        h->first_entry=off;
    }
    else{//iterate to end, then append
        uint16_t curr_off = h->first_entry;
        struct ptable_entry_s *cur = offset_to_entry(h, curr_off);
        while(cur->next !=0){
            curr_off= cur->next;
            cur = offset_to_entry(h, curr_off);
        }
        cur->next = off;
    }
}
//find key, print data
void printKey(char* KEY){
    struct ptable_s *h = (struct ptable_s*)table;
    struct ptable_entry_s *e = find_data_entry(KEY, NULL);
    if (e == NULL) {
        fprintf(stderr, "KEY not found\n");
        exit(1);
    }
    unsigned char *data = (unsigned char *)(e->key + e->key_len + 1);
    //write data to stdout
    fwrite(data, 1, e->data_len, stdout);
}

int main(int argc, char **argv) {
    if(argc!=4){
        fprintf(stdout, "Usage: ./ptable FILENAME COMMAND [ARGS]\nCommands:\n  l SUBSTRING - List all keys containing SUBSTRING\n  a KEY       - Add KEY with data read from stdin\n  p KEY       - Print the data for KEY to stdout\n  d KEY       - Delete KEY from the table\n");
        exit(1);
    }
    char COMMAND = argv[2][0];
    char* ARGS = argv[3];
    //if file no exist, will be created with required mode args 0644
    int fd = open(argv[1], O_RDWR | O_CREAT, 0644);
        if(fd<0){
        fprintf(stderr, "File error\n");
        exit(1);
    }
    if(ftruncate(fd, FILE_SIZE) < 0){
        perror("ftruncate error");
        close(fd);
        exit(1);
    }
    //kernel chooses start address, read/ write, updates visible on file
    table = mmap(NULL, 65536, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (table == MAP_FAILED) {
        perror("MAP_FAILED"); 
        close(fd);
        exit(1);
    }
    //resizes file, fills spaces w zero
    //cmd line error checking
    else if(strlen(ARGS)>255){
        if(COMMAND=='l'||(COMMAND!='l'&&strlen(ARGS==0))){
            fprintf(stderr, "KEY too large\n");
            munmap(table, FILE_SIZE);
            close(fd);
            exit(2);
        }
    }

    init_table();
    if(COMMAND=='l'){
        listKey(ARGS);
    }
    else if (COMMAND == 'a') {
        //buffer of max data size
        unsigned char data[DATA_SIZE];
        //total bytes read
        size_t total = 0;
        //bytes each read returns
        ssize_t n;
        //reads into next index in data buffer
        //binary data chunk by chunk
        while ((n = read(0, data + total, DATA_SIZE - total)) > 0) {
            total += n;
            if (total > DATA_SIZE) {
                fprintf(stderr, "data too large\n");
                munmap(table, FILE_SIZE);
                close(fd);
                exit(3);
            }
            //at exact data size -> error if more input
            if (total == DATA_SIZE) {
                char extra;
                if (read(0, &extra, 1) > 0) {
                    fprintf(stderr, "data too large\n");
                    munmap(table, FILE_SIZE);
                    close(fd);
                    exit(3);
                }
                //EXACTLy 32KB allowed so break & addKey
                break;
            }
        }
        addKey(ARGS, data, (uint16_t)total);
    }
    else if(COMMAND=='p'){
        printKey(ARGS);
    }
    else if(COMMAND=='d'){
        deleteKey(ARGS);
    }

    else{
        fprintf(stdout, "Usage: ./ptable FILENAME COMMAND [ARGS]\nCommands:\n  l SUBSTRING - List all keys containing SUBSTRING\n  a KEY       - Add KEY with data read from stdin\n  p KEY       - Print the data for KEY to stdout\n  d KEY       - Delete KEY from the table\n");
        munmap(table, FILE_SIZE);
        close(fd);
        exit(1);
    }
    munmap(table, FILE_SIZE);
    close(fd);
    return 0;
}