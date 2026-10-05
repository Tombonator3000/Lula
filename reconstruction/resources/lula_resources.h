#ifndef LULA_RESOURCES_H
#define LULA_RESOURCES_H

/* Independently reconstructed, portable C11 resource reader.
 * NGS indexing, TBF modes 0/2 and same-size TAF frame editing are supported.
 * No gameplay is included.
 * Initialize output structs to zero; free/close them before reuse.
 * All operations return 1 on success and 0 on error; error may be NULL.
 */
#include <stddef.h>
#include <stdint.h>

#define LULA_MAX_FILE_BYTES ((size_t)268435456u)
#define LULA_MAX_IMAGE_PIXELS ((size_t)16777216u)

typedef struct { char message[256]; } LulaError;
typedef struct { unsigned char *data; size_t size; } LulaBlob;
typedef struct {
    uint32_t offset;
    uint32_t size;
    uint16_t type_word;
} LulaNgsEntry;
typedef struct {
    const unsigned char *data; /* Borrowed: keep input bytes alive until close. */
    size_t size;
    uint16_t version_word;
    uint16_t count;
    LulaNgsEntry *entries;     /* Owned index; freed by lula_ngs_close. */
} LulaNgs;
typedef struct {
    uint16_t width, height;
    uint16_t version_word, mode;
    size_t pixel_count;
    uint16_t *pixels;          /* Owned, host-endian RGB565 words. */
} LulaImage;
typedef struct {
    uint32_t offset, size, payload_offset;
    uint16_t width, height, mode;
    unsigned char flag_byte, empty_sentinel;
} LulaTafFrame;
typedef struct {
    const unsigned char *data; /* Borrowed; keep input alive until close. */
    size_t size, trailer_offset;
    uint32_t decoded_bytes, first_frame_offset;
    uint16_t version_word, count, trailer_table_count;
    LulaTafFrame *frames;      /* Owned frame index. */
} LulaTaf;

int lula_blob_read(const char *path, LulaBlob *out, LulaError *error);
void lula_blob_free(LulaBlob *blob);
int lula_blob_write_new(const char *path, const void *data, size_t size,
                        LulaError *error);

int lula_ngs_parse(const void *data, size_t size, LulaNgs *out, LulaError *error);
void lula_ngs_close(LulaNgs *archive);
int lula_ngs_payload(const LulaNgs *archive, size_t index,
                     const unsigned char **data, size_t *size,
                     uint16_t *type_word, LulaError *error);

int lula_tbf_decode(const void *data, size_t size, LulaImage *out,
                     LulaError *error);
void lula_image_free(LulaImage *image);
/* P6 PPM, bit-replicated RGB565 -> RGB888, C11 exclusive output creation. */
int lula_ppm_write_new(const char *path, const LulaImage *image, LulaError *error);
/* P6 with maxval255 -> RGB565 by keeping the high5/6/5 bits. */
int lula_ppm_decode(const void *data, size_t size, LulaImage *out, LulaError *error);

/* Verified TAF version16, frame mode2, header table-count1/2. Metadata and
 * opaque placement bytes remain unchanged. The terminal14-byte0x0 frame
 * can decode/replace as an empty LulaImage; PPM rejects empty images. */
int lula_taf_parse(const void *data, size_t size, LulaTaf *out, LulaError *error);
void lula_taf_close(LulaTaf *animation);
int lula_taf_decode_frame(const LulaTaf *animation, size_t index,
                          LulaImage *out, LulaError *error);
/* Same dimensions only. Unchanged pixels copy the entire original exactly.
 * Changed pixels update frame offsets and the optional trailing index. */
int lula_taf_replace_frame(const LulaTaf *animation, size_t index,
                           const LulaImage *image, LulaBlob *out, LulaError *error);

#endif
