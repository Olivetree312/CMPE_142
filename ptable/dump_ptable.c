#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include "ptable.h"

int main(int argc, char **argv) {
    if (argc != 2) {
        printf("USAGE: %s ptable_file\n", argv[0]);
        exit(1);
    }
    int fd = open(argv[1], O_RDONLY);
    if (fd == -1) {
        perror(argv[1]);
        exit(2);
    }

    struct stat st;
    if (fstat(fd, &st) == -1) {
        perror(argv[1]);
        exit(2);
    }

    if (st.st_size != PTABLE_SIZE) {
        fprintf(stderr, "BAD FILE SIZE %ld should be %d\n", st.st_size, PTABLE_SIZE);
    }

    struct ptable_s *hdr = mmap(NULL, PTABLE_SIZE, PROT_READ, MAP_SHARED, fd, 0);
    if (hdr == MAP_FAILED) {
        perror(argv[1]);
        exit(2);
    }

    printf("Magic 0x%x\n", hdr->magic);
    printf("First entry offset %d\n", hdr->first_entry);
    printf("First free offset %d\n", hdr->first_free);

    uint32_t offset = sizeof(*hdr);
    while (offset < PTABLE_SIZE) {
        struct ptable_entry_s *pe = offset_to_entry(hdr, offset);
        printf("------------------\n");
        printf("    offset = %d\n", offset);
        printf("    magic = 0x%04hx", pe->magic);
        if (pe->magic == PTABLE_ENTRY_MAGIC_FREE) printf(" (FREE)");
        else if (pe->magic == PTABLE_ENTRY_MAGIC_DATA) printf(" (DATA)");
        else printf(" (UNKNOWN)");
        printf("\n");
        printf("    len = %d\n", pe->len);
        printf("    next = %d\n", pe->next);
        if (pe->magic == PTABLE_ENTRY_MAGIC_DATA) {
            char key_buf[PTABLE_MAX_KEY_LEN + 1];
            memcpy(key_buf, pe->key, pe->key_len);
            key_buf[pe->key_len] = '\0';
            printf("    key_len = %d\n", pe->key_len);
            printf("    data_len = %d\n", pe->data_len);
            printf("    key = \"%s\"\n", key_buf);
        }
        if (pe->len == 0) {
            printf("stopping due to a zero length entry\n");
            break;
        }
        offset += pe->len;
    }
    exit(0);
}