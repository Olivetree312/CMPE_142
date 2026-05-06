#pragma once
#include <stdint.h>

// Magic numbers (stored in native byte order for simplicity)
#define PTABLE_MAGIC 0x0C3142
#define PTABLE_ENTRY_MAGIC_FREE 0xDEAD
#define PTABLE_ENTRY_MAGIC_DATA 0xDA7A

#define PTABLE_SIZE (64 * 1024)
#define PTABLE_MAX_KEY_LEN 255
#define PTABLE_MAX_DATA_LEN (32 * 1024)

#pragma pack(1)
struct ptable_s {
    uint32_t magic;         /* PTABLE_MAGIC */
    uint16_t first_entry;   /* offset to first data entry, or 0 if none */
    uint16_t first_free;    /* offset to first free entry */
};

struct ptable_entry_s {
    uint16_t magic;     /* PTABLE_ENTRY_MAGIC_FREE or PTABLE_ENTRY_MAGIC_DATA */
    uint16_t len;       /* total length of the entry including this header */
    uint16_t next;      /* offset to the next entry, or 0 if end of list */
    uint16_t data_len;  /* length of data (for DATA entries) */
    uint8_t key_len;    /* length of the key (not including null terminator) */
    char key[0];        /* key followed by data */
};
#pragma pack()

#define ENTRY_HEADER_SIZE (sizeof(struct ptable_entry_s))

static inline struct ptable_entry_s *offset_to_entry(struct ptable_s *hdr, uint16_t offset) {
    if (offset == 0) return NULL;
    return (struct ptable_entry_s *)((char *)hdr + offset);
}

static inline uint16_t entry_to_offset(struct ptable_s *hdr, struct ptable_entry_s *entry) {
    return (uint16_t)((char *)entry - (char *)hdr);
}