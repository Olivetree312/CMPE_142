#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h> //for uint16_t
#include <unistd.h> //read() function is system call, not in stdlib
/*
argc = arg counter, *argv = char ptr
reads key from command line, reads input from file
key interpreted as hex, but input = hex val of integer value of ascii byte
read(0, char *key, 4) -> read key
long lsb = strtol temp[1]-> get LSB in decimal first
xor lsb and output 8 bits: (lsb XOR 0x41) & 0xFF 
rotate N to left with: (value << shift) | (value >> (bits - shift))
multiply N * 257 & keep only lower 16 bits
repeat with argc # of input bytes
*/
static unsigned char output;
static int bytesEncoded=0;
static unsigned char byteVal;
static char *key;
//input length/ chars read for N -> NOT bit-width of N
static int exit_code;
static uint16_t N; //keeps N 16-bit

void encrypt();
void prep();

int main(int argc, char *argv[]) {
    //printf("Command to RUN: %s ", argv[0]);
    //missing key -> exit
    if(argc!=2){
        exit_code = 1;
        //printf("\nExpected Exit Code: %d\n", exit_code);
        printf("USAGE: ./sea_enc KEY_IN_HEX\n");
        exit(1);
    }
    key = argv[1];
    N = strtol(key, NULL, 16);
    //printf("%s", key);
    //fprintf(stderr, "\nExpected Exit Code: %d\n", exit_code);
    //loop runs until all bytes processed
    while(read(0, &byteVal, 1)>0){
        //gets int val of input (not hex)
        bytesEncoded++;
        encrypt();
        prep();
        //newline every 40 bytes encrypted
        if(bytesEncoded%40==0){
            printf("\n");
        }
        //exit loop if newline, but newline needs to be encrypted
        //if(byteVal=='\n')
            //break;
    }
    //new line after last byte UNLESS already newline after 40th byte
    if(bytesEncoded%40!=0)
        printf("\n");
    return 0;
}

//XOR & output lower 2 nibbles
void encrypt(){
    //get only lower 8 bits
    output = byteVal ^ (N & 0xFF);
    //upper case output
    printf("%02X", (unsigned)output);
}

//rotate left 1 bit & multiply N by 257
void prep(){
    //wrap around by shifting right the bit rotated out, then ORing
    //masking 16 bits with 0xFFFF
    //remember to always mask to get bits after operations
    N = ((N << 1)&0xFFFF) | ((N >> 15) & 0xFFFF);
    N = (N* 257) & 0xFFFF;
}