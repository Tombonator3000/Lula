#include "lula_resources.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>
#endif

static int fail(LulaError *error, const char *format, ...)
{
    if (error != NULL) {
        va_list args;
        va_start(args, format);
        (void)vsnprintf(error->message, sizeof error->message, format, args);
        va_end(args);
    }
    return 0;
}

static uint16_t le16(const unsigned char *p)
{
    return (uint16_t)((uint16_t)p[0] | (uint16_t)((uint16_t)p[1] << 8));
}

static uint32_t le32(const unsigned char *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void clear_error(LulaError *error)
{
    if (error != NULL) error->message[0] = '\0';
}

int lula_blob_read(const char *path, LulaBlob *out, LulaError *error)
{
    FILE *input;
    long end;
    unsigned char *data;
    size_t size;
    if (out == NULL) return fail(error, "Missing blob output");
    memset(out, 0, sizeof *out);
    clear_error(error);
    if (path == NULL) return fail(error, "Missing input path");
    input = fopen(path, "rb");
    if (input == NULL) return fail(error, "Cannot open input: %s", strerror(errno));
    if (fseek(input, 0, SEEK_END) != 0 || (end = ftell(input)) < 0
            || fseek(input, 0, SEEK_SET) != 0) {
        (void)fclose(input);
        return fail(error, "Input is not a seekable, size-readable file");
    }
    if ((unsigned long)end > (unsigned long)LULA_MAX_FILE_BYTES) {
        (void)fclose(input);
        return fail(error, "Input exceeds the 256 MiB file limit");
    }
    size = (size_t)end;
    data = (unsigned char *)malloc(size != 0 ? size : 1);
    if (data == NULL) {
        (void)fclose(input);
        return fail(error, "Cannot allocate input buffer");
    }
    if (fread(data, 1, size, input) != size || fgetc(input) != EOF || ferror(input)) {
        free(data);
        (void)fclose(input);
        return fail(error, "Input changed size or could not be read completely");
    }
    if (fclose(input) != 0) {
        free(data);
        return fail(error, "Cannot close input file");
    }
    out->data = data;
    out->size = size;
    return 1;
}

void lula_blob_free(LulaBlob *blob)
{
    if (blob != NULL) { free(blob->data); memset(blob, 0, sizeof *blob); }
}

/* C11's x flag creates exclusively, without a check-then-open race.
 * Legacy Windows CRTs need their explicit exclusive-descriptor operation. */
static FILE *open_new(const char *path, LulaError *error)
{
    FILE *output;
    if (path == NULL) { (void)fail(error, "Missing output path"); return NULL; }
#ifdef _WIN32
    {
        int descriptor = _open(path, _O_WRONLY | _O_CREAT | _O_EXCL | _O_BINARY,
                               _S_IREAD | _S_IWRITE);
        if (descriptor < 0) output = NULL;
        else {
            output = _fdopen(descriptor, "wb");
            if (output == NULL) {
                int saved_error = errno;
                (void)_close(descriptor);
                (void)remove(path);
                errno = saved_error;
            }
        }
    }
#else
    output = fopen(path, "wbx");
#endif
    if (output == NULL)
        (void)fail(error, "Cannot create new output (existing paths are preserved): %s",
                   strerror(errno));
    return output;
}

static int close_output(FILE *output, const char *path, int okay, LulaError *error)
{
    if (fclose(output) != 0) okay = 0;
    if (!okay) {
        (void)remove(path); /* Only a file successfully created by this operation. */
        return fail(error, "Output write failed; incomplete output removal attempted");
    }
    return 1;
}

int lula_blob_write_new(const char *path, const void *data, size_t size,
                        LulaError *error)
{
    FILE *output;
    clear_error(error);
    if ((data == NULL && size != 0) || size > LULA_MAX_FILE_BYTES)
        return fail(error, "Invalid output buffer or excessive payload size");
    output = open_new(path, error);
    if (output == NULL) return 0;
    return close_output(output, path, size == 0 || fwrite(data, 1, size, output) == size,
                        error);
}

int lula_ngs_parse(const void *input, size_t size, LulaNgs *out, LulaError *error)
{
    const unsigned char *data = (const unsigned char *)input;
    size_t table_size, table_start, cursor, i;
    uint16_t count;
    LulaNgsEntry *entries;
    if (out == NULL) return fail(error, "Missing NGS output");
    memset(out, 0, sizeof *out);
    clear_error(error);
    if (data == NULL || size < 8 || size > LULA_MAX_FILE_BYTES
            || memcmp(data, "NGS\0", 4) != 0)
        return fail(error, "Expected an NGS header within the 256 MiB limit");
    count = le16(data + 6);
    table_size = (size_t)count * 4;
    if (table_size > size - 8) return fail(error, "NGS index extends before header");
    table_start = size - table_size;
    entries = count != 0 ? (LulaNgsEntry *)calloc(count, sizeof *entries) : NULL;
    if (count != 0 && entries == NULL) return fail(error, "Cannot allocate NGS index");
    cursor = 8;
    for (i = 0; i < count; ++i) {
        uint32_t offset = le32(data + table_start + i * 4);
        uint32_t payload_size;
        if (cursor > table_start || table_start - cursor < 6
                || (size_t)offset != cursor + 6) {
            free(entries);
            return fail(error, "NGS entry %lu: inconsistent payload offset", (unsigned long)i);
        }
        payload_size = le32(data + cursor);
        if ((size_t)offset > table_start || (size_t)payload_size > table_start - offset) {
            free(entries);
            return fail(error, "NGS entry %lu: truncated payload", (unsigned long)i);
        }
        entries[i].offset = offset;
        entries[i].size = payload_size;
        entries[i].type_word = le16(data + cursor + 4);
        cursor = (size_t)offset + payload_size;
    }
    if (cursor != table_start) {
        free(entries);
        return fail(error, "Unexpected bytes between NGS payloads and index");
    }
    out->data = data;
    out->size = size;
    out->version_word = le16(data + 4);
    out->count = count;
    out->entries = entries;
    return 1;
}

void lula_ngs_close(LulaNgs *archive)
{
    if (archive != NULL) { free(archive->entries); memset(archive, 0, sizeof *archive); }
}

int lula_ngs_payload(const LulaNgs *archive, size_t index,
                     const unsigned char **data, size_t *size,
                     uint16_t *type_word, LulaError *error)
{
    const LulaNgsEntry *entry;
    clear_error(error);
    if (data != NULL) *data = NULL;
    if (size != NULL) *size = 0;
    if (type_word != NULL) *type_word = 0;
    if (archive == NULL || archive->data == NULL || archive->entries == NULL
            || index >= archive->count || data == NULL || size == NULL)
        return fail(error, "NGS payload index is out of range or archive is unavailable");
    entry = &archive->entries[index];
    if ((size_t)entry->offset > archive->size
            || (size_t)entry->size > archive->size - entry->offset)
        return fail(error, "NGS payload range is invalid");
    *data = archive->data + entry->offset;
    *size = entry->size;
    if (type_word != NULL) *type_word = entry->type_word;
    return 1;
}

int lula_tbf_decode(const void *input, size_t size, LulaImage *out, LulaError *error)
{
    const unsigned char *data = (const unsigned char *)input;
    uint16_t width, height, mode;
    size_t pixel_count, bytes, cursor, produced, i;
    uint16_t *pixels;
    if (out == NULL) return fail(error, "Missing image output");
    memset(out, 0, sizeof *out);
    clear_error(error);
    if (data == NULL || size < 16 || size > LULA_MAX_FILE_BYTES
            || memcmp(data, "TBF\0", 4) != 0)
        return fail(error, "Expected a TBF header within the file-size limit");
    width = le16(data + 12); height = le16(data + 14); mode = le16(data + 6);
    if (width == 0 || height == 0
            || (size_t)width > LULA_MAX_IMAGE_PIXELS / height)
        return fail(error, "Unsupported TBF dimensions (limit: 16777216 pixels)");
    pixel_count = (size_t)width * height;
    bytes = pixel_count * sizeof(uint16_t);
    if (le16(data + 4) != 16 || (size_t)le32(data + 8) != bytes)
        return fail(error, "Unverified TBF version or inconsistent raw size");
    if (mode != 0 && mode != 2) return fail(error, "TBF mode %u is unverified", (unsigned)mode);
    if (mode == 0 && size - 16 != bytes)
        return fail(error, "Raw TBF has unexpected bytes or truncated pixels");
    pixels = (uint16_t *)malloc(bytes);
    if (pixels == NULL) return fail(error, "Cannot allocate RGB565 pixels");
    if (mode == 0) {
        for (i = 0; i < pixel_count; ++i) pixels[i] = le16(data + 16 + i * 2);
    } else {
        cursor = 16; produced = 0;
        while (cursor < size) {
            uint16_t code;
            size_t count;
            if (size - cursor < 2) {
                free(pixels); return fail(error, "Truncated RLE command");
            }
            code = le16(data + cursor); cursor += 2;
            count = code >= 0xf000u ? 65536u - (size_t)code : code;
            if (count > pixel_count - produced) {
                free(pixels); return fail(error, "RLE command expands beyond image dimensions");
            }
            if (code >= 0xf000u) {
                if (count > (size - cursor) / 2) {
                    free(pixels); return fail(error, "Truncated RLE literal");
                }
                for (i = 0; i < count; ++i) pixels[produced++] = le16(data + cursor + i * 2);
                cursor += count * 2;
            } else {
                uint16_t word;
                if (size - cursor < 2) {
                    free(pixels); return fail(error, "Truncated RLE repeated pixel");
                }
                word = le16(data + cursor); cursor += 2;
                for (i = 0; i < count; ++i) pixels[produced++] = word;
            }
        }
        if (produced != pixel_count) {
            free(pixels); return fail(error, "RLE decoded size differs from image dimensions");
        }
    }
    out->width = width; out->height = height;
    out->version_word = 16; out->mode = mode;
    out->pixel_count = pixel_count; out->pixels = pixels;
    return 1;
}

void lula_image_free(LulaImage *image)
{
    if (image != NULL) { free(image->pixels); memset(image, 0, sizeof *image); }
}

int lula_ppm_write_new(const char *path, const LulaImage *image, LulaError *error)
{
    FILE *output;
    unsigned char rgb[4096 * 3];
    size_t offset = 0;
    int okay;
    clear_error(error);
    if (image == NULL || image->pixels == NULL || image->width == 0 || image->height == 0
            || (size_t)image->width > LULA_MAX_IMAGE_PIXELS / image->height
            || image->pixel_count != (size_t)image->width * image->height)
        return fail(error, "Invalid RGB565 image output");
    output = open_new(path, error);
    if (output == NULL) return 0;
    okay = fprintf(output, "P6\n%u %u\n255\n", (unsigned)image->width,
                    (unsigned)image->height) > 0;
    while (okay && offset < image->pixel_count) {
        size_t count = image->pixel_count - offset;
        size_t i;
        if (count > 4096) count = 4096;
        for (i = 0; i < count; ++i) {
            unsigned word = image->pixels[offset + i];
            unsigned r = (word >> 11) & 31u, g = (word >> 5) & 63u, b = word & 31u;
            rgb[i * 3] = (unsigned char)((r << 3) | (r >> 2));
            rgb[i * 3 + 1] = (unsigned char)((g << 2) | (g >> 4));
            rgb[i * 3 + 2] = (unsigned char)((b << 3) | (b >> 2));
        }
        okay = fwrite(rgb, 3, count, output) == count;
        offset += count;
    }
    return close_output(output, path, okay, error);
}
