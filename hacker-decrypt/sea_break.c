#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <stdint.h> //for uint16_t
#include <unistd.h> //read() function is system call, not in stdlib
static int encryptedIndex=0; //hexchars
static int currIndex=0; //decrypted bytes from taking 2 hex at a time
static int rc;
//used to iterate through bytes in encrypted
static unsigned char byteVal;

FILE *ptr;
static unsigned char *encrypted, *unencrypted;
//encrypted hex chars must be interpreted in 2's to make bytes
static uint16_t key; //tracks curr 16 bit key

//converts hex to decimal to get bytes
static int hexval(unsigned char c){
    if(c >='0' && c <= '9')
        return (c-'0');
    if(c>='A'&& c <= 'F')
        return 10+(c-'A');
    return -1;
}

static void getKey(){
    key = ((key << 1)&0xFFFF) | ((key >> 15) & 0xFFFF);
    key = (key* 257) & 0xFFFF;
}
static void unencrypt(){
    //get only lower 8 bits
    int hexpos = currIndex*2;
    //getting byte val from 2 hex char
    int upper=hexval(encrypted[hexpos]);
    int lower=hexval(encrypted[hexpos+1]);
    if(upper<0||lower<0)
        byteVal=0;
    //shift upper byte up and make room for lower byte
    else
    byteVal = (unsigned char)((upper << 4)| lower);
    //unencrypting algorithm, XORing 1 byte at a time
    byteVal = (unsigned char)(byteVal ^ (unsigned char)(key & 0xFF));
    //get plaintext ascii of byte
    unencrypted[currIndex] = byteVal;
    //prep key for next state
    getKey();
    currIndex++;
}
//according to rules for UTF-8
static int validate(){
    //just in case unencrypted isn't even long enough to have heart\n
    if(currIndex <4)
        return 1;
    //return 0 if has heart + \n
    //heart is for some reason 3 bytes
    if(unencrypted[currIndex-4]==0xE2 &&
        unencrypted[currIndex-3]==0x99 &&
        unencrypted[currIndex-2] ==0xA5 &&
        unencrypted[currIndex-1]==0x0A){ 
            return 0;       
    }
    return 1;
}

static int valid_utf8(const unsigned char *s, size_t n){
    size_t i=0;
    while(i < n){
        unsigned char c = s[i];
        //is ASCII?
        if(c <=0x7F){
            i++;
            continue;
        }
        //2-byte
        if((c&0xE0)==0xC0){
            if(i+1>=n)
                return 0;
            unsigned char c1 = s[i+1];
            if((c1&0xC0)!= 0x80)
                return 0;
            if(c < 0xC2)
                return 0;
            i +=2;
            continue;   
        }

        // 3-byte: 1110xxxx 10xxxxxx 10xxxxxx
        if ((c & 0xF0) == 0xE0) {
            if (i + 2 >= n) return 0;
            unsigned char c1 = s[i+1], c2 = s[i+2];
            if ((c1 & 0xC0) != 0x80 || (c2 & 0xC0) != 0x80) return 0;

            // overlong check for E0
            if (c == 0xE0 && c1 < 0xA0) return 0;
            // U+D800 to U+DFFF invalid in UTF-8
            if (c == 0xED && c1 >= 0xA0) return 0;

            i += 3;
            continue;
        }

        // 4-byte: 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
        if ((c & 0xF8) == 0xF0) {
            if (i + 3 >= n) return 0;
            unsigned char c1 = s[i+1], c2 = s[i+2], c3 = s[i+3];
            if ((c1 & 0xC0) != 0x80 || (c2 & 0xC0) != 0x80 || (c3 & 0xC0) != 0x80) return 0;
            // overlong check for F0
            if (c == 0xF0 && c1 < 0x90) return 0;
            // max U+10FFFF
            if (c > 0xF4) return 0;
            if (c == 0xF4 && c1 > 0x8F) return 0;
            i += 4;
            continue;
        }

        return 0;
    }
    return 1;
}

int main(int argc, char *argv[]) {
    //printf("Command to RUN: %s ", argv[0]);
    //missing key -> exit
    if(argc!=2){
        //printf("\nExpected Exit Code: %d\n", exit_code);
        printf("USAGE: ./sea_break FILE_TO_BREAK\n");
        exit(1);
    }
    //read binary so filename read exactly as is
    ptr = fopen(argv[1], "rb");
    if(ptr==NULL){
        printf("Error: Could not open file.\n");
        exit(1);
    }
    //allocate 900KB of space to encrypted input
    encrypted = (unsigned char*)malloc(900*1024);
    if(!encrypted)
        exit(1);
    int ch;
    while((ch = fgetc(ptr))!= EOF){
        encrypted[encryptedIndex++] = (unsigned char)ch;
    }
    //close ur files!!
    fclose(ptr);
    //trim trailing newline before checking even bytes
    while(encryptedIndex >0 && (encrypted[encryptedIndex-1] == '\n')){
        encryptedIndex--;
    }
    //arr of pids like they do in the textbook
    pid_t pids[8];    
    //must be even # of char for there to be complete bytes
    if(encryptedIndex%2!=0){
        printf("Could not break\n");
        exit(1);
    }
    unencrypted = (unsigned char*)malloc(encryptedIndex/2);
    if(!unencrypted){
        free(encrypted);
        exit(1);
    }
    //call child processes & test keys
    for(int i=0; i<8; i++){
        rc = fork();
        //fork failed
        if(rc<0){
            free(encrypted);
            free(unencrypted);
            exit(1);
        }
        uint16_t start=0;
        uint16_t end=0;
        //is child -> get start and end keys
        if(rc==0){
            start = (uint16_t)(i*0x2000);
            end = (uint16_t)(start + 0x1FFF);
                    int found=0;
            //iterate through keys
            for(int k = start; k <= end; k++){
                key = (uint16_t)k;
                currIndex=0;
                //unencrypting input file 2 nibbles/ 1 byte at a time
                for(int b=0; b < encryptedIndex/2; b++){
                    unencrypt();
                }
                //child does the actual work
                if(validate()==0 && valid_utf8(unencrypted, (size_t)(encryptedIndex/2))){
                    //key is starting key, k is current candidate key
                    printf("%04X ", k);
                    //last 10 bytes before heart\n = 0xE299A50A
                    int len = encryptedIndex/2;
                    int before = len-4;
                    int start10 = before -10;
                    //just in case less than 10 bytes
                    if(start10 < 0)
                        start10 = 0;
                    for(int j=start10; j< before; j++){
                        putchar(unencrypted[j]);
                    }
                    //heart is a bit tricky to putchar since it's 3 bytes
                    fwrite("\xE2\x99\xA5\n", 1,4, stdout);
                    found = 1;
                }
            }
            free(encrypted);
            free(unencrypted);
            //after child is fully done testing all in range keys
            exit(found ? 0:1);
        }
        //if parent-> save pid that child returns
        pids[i] = rc;
    }
    //parent keeps going since we only made child exit
    //gotta wait
    /*
    waitpid(int pid, *status, int options)
        waits for specific child process pid, stores child exit status, nonblocking option=0
        WIFEXITED(status) = 1 if child called exit/ terminated normally
        WEXITSTATUS(status) returns lower 8 bits (0-255) of child exit value 
        status argument populated by wait <- info about HOW child terminated
    */
   //iterate through pids in arr and check fate of children
   int status, any_found = 0;
   for(int i=0; i<8; i++){
        waitpid(pids[i], &status, 0);
        //wifexited=1 if child exited normally
        //wexitstatus gets exit value of child main = 0 if codebreak
        if(WIFEXITED(status) && WEXITSTATUS(status)==0)
            any_found=1;
   }
   if(!any_found){
        free(encrypted);
        free(unencrypted);
        printf("Could not break\n");
        return 1;
   }
   free(encrypted);
   free(unencrypted);
    return 0;
}

