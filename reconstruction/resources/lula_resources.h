#ifndef LULA_RESOURCES_H
#define LULA_RESOURCES_H

/* Independently reconstructed, portable C11 resource reader.
 * Only NGS indexing and TBF modes 0/2 are verified. No gameplay is included.
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

#endif
