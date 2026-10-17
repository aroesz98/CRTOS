/*
 * deflate.h - a small, fast DEFLATE compressor for vncd's zlib encoding (deflate.c).
 *
 * One zlib stream per viewer: the first rectangle starts it with the zlib header, every
 * rectangle is one block with the fixed Huffman codes, ended by a sync flush (an empty stored
 * block), so the viewer's inflater returns all of it at once. The stream never ends (no
 * checksum).
 */
#ifndef VNCD_DEFLATE_H
#define VNCD_DEFLATE_H

#include <stddef.h>
#include <stdint.h>

#define ZDEF_HASH_BITS 12

struct zdef {
    uint16_t head[1u << ZDEF_HASH_BITS]; /* hash of 4 bytes -> their last position (8 KB: stays in
                                          * the data cache; a larger table cost a miss a pixel) */
    int started;                         /* the zlib header went out */
    uint8_t *out;                        /* the compressed rectangle */
    size_t cap, len;
    uint64_t acc;                        /* bits not written yet */
    unsigned nbits;
};

/* The output buffer for rectangles of up to @max_in bytes; 0 or -1 (no memory) */
int zdef_init(struct zdef *z, uint8_t *out, size_t max_in);
/* Space the output buffer needs for rectangles of up to @max_in bytes */
size_t zdef_bound(size_t max_in);
/* A new stream (a new viewer) */
void zdef_reset(struct zdef *z);
/* @len bytes of whole pixels of @unit bytes (1, 2 or 4), rows of @row bytes, compressed into
 * z->out; their length */
size_t zdef_rect(struct zdef *z, const uint8_t *in, size_t len, unsigned unit, size_t row);

#endif
