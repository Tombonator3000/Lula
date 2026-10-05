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
            if (code == 0x8000u) {
                free(pixels); return fail(error, "INT16_MIN RLE command is unsafe in the game decoder");
            }
            count = code > 0x8000u ? 65536u - (size_t)code : code;
            if (count > pixel_count - produced) {
                free(pixels); return fail(error, "RLE command expands beyond image dimensions");
            }
            if (code > 0x8000u) {
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

static void store16(unsigned char *p, uint16_t value)
{
    p[0] = (unsigned char)value;
    p[1] = (unsigned char)(value >> 8);
}

static void store32(unsigned char *p, uint32_t value)
{
    p[0] = (unsigned char)value;
    p[1] = (unsigned char)(value >> 8);
    p[2] = (unsigned char)(value >> 16);
    p[3] = (unsigned char)(value >> 24);
}

/* The real reader uses signed16-bit commands (0x43fa48..0x43fa57).
 * Newly written literals stay <=4096 words and repeats <=32767 words. */
static int taf_rle_decode(const unsigned char *data, size_t size, uint16_t *pixels,
                           size_t pixel_count, LulaError *error)
{
    size_t cursor = 0, produced = 0;
    while (cursor < size) {
        uint16_t code;
        size_t count, i;
        if (size - cursor < 2) return fail(error, "TAF: truncated RLE command");
        code = le16(data + cursor); cursor += 2;
        if (code == 0x8000u)
            return fail(error, "TAF: INT16_MIN RLE command is unsafe in the game decoder");
        count = code > 0x8000u ? 65536u - (size_t)code : code;
        if (count > pixel_count - produced)
            return fail(error, "TAF: RLE command exceeds frame dimensions");
        if (code > 0x8000u) {
            if (count > (size - cursor) / 2)
                return fail(error, "TAF: truncated literal run");
            for (i = 0; i < count; ++i)
                pixels[produced++] = le16(data + cursor + i * 2);
            cursor += count * 2;
        } else {
            uint16_t pixel;
            if (size - cursor < 2) return fail(error, "TAF: truncated repeated pixel");
            pixel = le16(data + cursor); cursor += 2;
            for (i = 0; i < count; ++i) pixels[produced++] = pixel;
        }
    }
    if (produced != pixel_count)
        return fail(error, "TAF: decoded pixel count differs from dimensions");
    return 1;
}

static int taf_rle_encode(const LulaImage *image, LulaBlob *out, LulaError *error)
{
    size_t cursor = 0, written = 0;
    unsigned char *data;
    if (image->pixel_count > LULA_MAX_IMAGE_PIXELS)
        return fail(error, "TAF: image exceeds pixel limit");
    data = (unsigned char *)malloc(image->pixel_count != 0 ? image->pixel_count * 4 : 1);
    if (data == NULL) return fail(error, "Cannot allocate TAF encoded frame");
    while (cursor < image->pixel_count) {
        size_t end = cursor + 1, start, i;
        while (end < image->pixel_count && image->pixels[end] == image->pixels[cursor]
                && end - cursor < 32767)
            ++end;
        if (end - cursor >= 2) {
            store16(data + written, (uint16_t)(end - cursor));
            store16(data + written + 2, image->pixels[cursor]);
            written += 4;
            cursor = end;
            continue;
        }
        start = cursor++;
        while (cursor < image->pixel_count && cursor - start < 4096) {
            if (cursor + 1 < image->pixel_count
                    && image->pixels[cursor] == image->pixels[cursor + 1])
                break;
            ++cursor;
        }
        store16(data + written, (uint16_t)(65536u - (cursor - start)));
        written += 2;
        for (i = start; i < cursor; ++i) {
            store16(data + written, image->pixels[i]);
            written += 2;
        }
    }
    out->data = data;
    out->size = written;
    return 1;
}

void lula_taf_close(LulaTaf *animation)
{
    if (animation != NULL) { free(animation->frames); memset(animation, 0, sizeof *animation); }
}

int lula_taf_decode_frame(const LulaTaf *animation, size_t index,
                          LulaImage *out, LulaError *error)
{
    const LulaTafFrame *frame;
    size_t count;
    uint16_t *pixels;
    if (out == NULL) return fail(error, "Missing frame image output");
    memset(out, 0, sizeof *out);
    clear_error(error);
    if (animation == NULL || animation->data == NULL || animation->frames == NULL
            || index >= animation->count)
        return fail(error, "TAF frame index is out of range");
    frame = &animation->frames[index];
    if ((size_t)frame->offset > animation->size
            || (size_t)frame->size > animation->size - frame->offset)
        return fail(error, "TAF frame has an invalid byte range");
    if (frame->empty_sentinel) {
        if (frame->width != 0 || frame->height != 0 || frame->size != 14)
            return fail(error, "TAF empty sentinel is inconsistent");
        out->version_word = animation->version_word;
        out->mode = frame->mode;
        return 1;
    }
    if (frame->width == 0 || frame->height == 0
            || (size_t)frame->width > LULA_MAX_IMAGE_PIXELS / frame->height
            || frame->mode != 2 || frame->size < 15
            || frame->payload_offset != frame->offset + 15)
        return fail(error, "TAF frame dimensions or compression are unsupported");
    count = (size_t)frame->width * frame->height;
    pixels = (uint16_t *)malloc(count * sizeof *pixels);
    if (pixels == NULL) return fail(error, "Cannot allocate TAF frame pixels");
    if (!taf_rle_decode(animation->data + frame->payload_offset, frame->size - 15,
                        pixels, count, error)) {
        free(pixels);
        return 0;
    }
    out->width = frame->width; out->height = frame->height;
    out->pixel_count = count; out->pixels = pixels;
    out->version_word = animation->version_word; out->mode = frame->mode;
    return 1;
}

int lula_taf_parse(const void *input, size_t size, LulaTaf *out, LulaError *error)
{
    const unsigned char *data = (const unsigned char *)input;
    LulaTaf animation = {0};
    size_t cursor, total = 0, i, trailer_size;
    if (out == NULL) return fail(error, "Missing TAF output");
    memset(out, 0, sizeof *out);
    clear_error(error);
    if (data == NULL || size < 786 || size > LULA_MAX_FILE_BYTES
            || memcmp(data, "TAF\0", 4) != 0 || le16(data + 4) != 16)
        return fail(error, "Expected TAF version16 within the file-size limit");
    animation.data = data; animation.size = size;
    animation.version_word = le16(data + 4); animation.count = le16(data + 6);
    animation.decoded_bytes = le32(data + 8);
    animation.first_frame_offset = le32(data + 780);
    animation.trailer_table_count = le16(data + 784);
    if (animation.count == 0 || animation.first_frame_offset < 786
            || (size_t)animation.first_frame_offset > size
            || (animation.trailer_table_count != 1 && animation.trailer_table_count != 2))
        return fail(error, "TAF frame start/count or trailer-table count is unsupported");
    animation.frames = (LulaTafFrame *)calloc(animation.count, sizeof *animation.frames);
    if (animation.frames == NULL) return fail(error, "Cannot allocate TAF frame index");
    cursor = animation.first_frame_offset;
    for (i = 0; i < animation.count; ++i) {
        LulaTafFrame *frame = &animation.frames[i];
        LulaImage image = {0};
        uint32_t next;
        if (cursor > size || size - cursor < 14) {
            (void)fail(error, "TAF frame %lu has a truncated header", (unsigned long)i);
            goto rejected;
        }
        frame->offset = (uint32_t)cursor;
        frame->mode = le16(data + cursor);
        frame->width = le16(data + cursor + 2); frame->height = le16(data + cursor + 4);
        next = le32(data + cursor + 6);
        frame->payload_offset = le32(data + cursor + 10);
        frame->empty_sentinel = frame->width == 0 && frame->height == 0
            && next == cursor + 15 && frame->payload_offset == cursor + 15;
        if (frame->empty_sentinel) {
            if (i + 1 != animation.count || frame->mode != 2) {
                (void)fail(error, "TAF empty sentinel must be the terminal mode2 frame");
                goto rejected;
            }
            frame->size = 14;
        } else {
            if ((size_t)next < cursor + 15 || (size_t)next > size
                    || frame->payload_offset != cursor + 15) {
                (void)fail(error, "TAF frame %lu has inconsistent offsets", (unsigned long)i);
                goto rejected;
            }
            frame->size = next - frame->offset;
            frame->flag_byte = data[cursor + 14];
        }
        if (!lula_taf_decode_frame(&animation, i, &image, error)) goto rejected;
        if (image.pixel_count > (UINT32_MAX - total) / 2) {
            lula_image_free(&image);
            (void)fail(error, "TAF decoded byte sum overflows header");
            goto rejected;
        }
        total += image.pixel_count * 2;
        lula_image_free(&image);
        cursor += frame->size;
    }
    animation.trailer_offset = cursor;
    trailer_size = (size_t)animation.count * 4 * animation.trailer_table_count;
    if (total != animation.decoded_bytes || cursor > size || size - cursor != trailer_size) {
        (void)fail(error, "TAF decoded byte sum or trailer size is inconsistent");
        goto rejected;
    }
    if (animation.trailer_table_count == 2) {
        for (i = 0; i < animation.count; ++i) {
            if (le32(data + cursor + (size_t)animation.count * 4 + i * 4)
                    != animation.frames[i].offset) {
                (void)fail(error, "TAF trailing frame index is inconsistent");
                goto rejected;
            }
        }
    }
    *out = animation;
    return 1;
rejected:
    lula_taf_close(&animation);
    return 0;
}

int lula_taf_replace_frame(const LulaTaf *animation, size_t index,
                           const LulaImage *image, LulaBlob *out, LulaError *error)
{
    LulaTaf verified = {0}, check = {0};
    LulaImage old = {0};
    LulaBlob encoded = {0};
    unsigned char *data = NULL;
    size_t i, size, cursor, trailer_size;
    int okay = 0;
    if (out == NULL) return fail(error, "Missing TAF replacement output");
    memset(out, 0, sizeof *out);
    clear_error(error);
    if (animation == NULL || image == NULL
            || !lula_taf_parse(animation->data, animation->size, &verified, error))
        return fail(error, "TAF template is missing or unsupported");
    if (!lula_taf_decode_frame(&verified, index, &old, error)) goto done;
    if (image->width != old.width || image->height != old.height
            || image->pixel_count != old.pixel_count
            || (image->pixel_count != 0 && image->pixels == NULL)) {
        (void)fail(error, "TAF replacement must retain the frame dimensions");
        goto done;
    }
    if (image->pixel_count == 0
            || memcmp(image->pixels, old.pixels, image->pixel_count * sizeof(uint16_t)) == 0) {
        data = (unsigned char *)malloc(verified.size);
        if (data == NULL) { (void)fail(error, "Cannot allocate unchanged TAF copy"); goto done; }
        memcpy(data, verified.data, verified.size);
        out->data = data; out->size = verified.size; data = NULL;
        okay = 1;
        goto done;
    }
    if (!taf_rle_encode(image, &encoded, error)) goto done;
    trailer_size = verified.size - verified.trailer_offset;
    size = verified.size - verified.frames[index].size;
    if (size > LULA_MAX_FILE_BYTES - 15 || encoded.size > LULA_MAX_FILE_BYTES - size - 15) {
        (void)fail(error, "Edited TAF exceeds the file-size limit");
        goto done;
    }
    size += 15 + encoded.size;
    data = (unsigned char *)malloc(size);
    if (data == NULL) { (void)fail(error, "Cannot allocate edited TAF"); goto done; }
    memcpy(data, verified.data, verified.first_frame_offset);
    cursor = verified.first_frame_offset;
    for (i = 0; i < verified.count; ++i) {
        const LulaTafFrame *frame = &verified.frames[i];
        size_t frame_size = i == index ? 15 + encoded.size : frame->size;
        if (i == index) {
            memcpy(data + cursor, verified.data + frame->offset, 15);
            memcpy(data + cursor + 15, encoded.data, encoded.size);
        } else memcpy(data + cursor, verified.data + frame->offset, frame_size);
        /* Empty BUTCH sentinel is14 bytes, but its offsets point to+15. */
        store32(data + cursor + 6, (uint32_t)(cursor + (frame->empty_sentinel ? 15 : frame_size)));
        store32(data + cursor + 10, (uint32_t)(cursor + 15));
        if (verified.trailer_table_count == 2) {
            size_t index_start = size - (size_t)verified.count * 4;
            store32(data + index_start + i * 4, (uint32_t)cursor);
        }
        cursor += frame_size;
    }
    memcpy(data + cursor, verified.data + verified.trailer_offset,
            (size_t)verified.count * 4);
    if (cursor + trailer_size != size || !lula_taf_parse(data, size, &check, error)) goto done;
    out->data = data; out->size = size; data = NULL;
    okay = 1;
done:
    free(data);
    lula_blob_free(&encoded);
    lula_image_free(&old);
    lula_taf_close(&check);
    lula_taf_close(&verified);
    return okay;
}

static int ppm_space(unsigned char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
}

static int ppm_number(const unsigned char *data, size_t size, size_t *cursor,
                       unsigned *value, LulaError *error)
{
    unsigned n = 0;
    size_t first;
    for (;;) {
        while (*cursor < size && ppm_space(data[*cursor])) ++*cursor;
        if (*cursor >= size || data[*cursor] != '#') break;
        while (*cursor < size && data[*cursor] != '\n' && data[*cursor] != '\r') ++*cursor;
    }
    first = *cursor;
    while (*cursor < size && data[*cursor] >= '0' && data[*cursor] <= '9') {
        unsigned digit = data[(*cursor)++] - '0';
        if (n > (65535u - digit) / 10u) return fail(error, "PPM numeric field exceeds65535");
        n = n * 10u + digit;
    }
    if (*cursor == first || *cursor >= size || !ppm_space(data[*cursor]))
        return fail(error, "PPM header contains an invalid numeric field");
    *value = n;
    return 1;
}

int lula_ppm_decode(const void *input, size_t size, LulaImage *out, LulaError *error)
{
    const unsigned char *data = (const unsigned char *)input;
    size_t cursor = 2, count, i;
    unsigned width, height, maxval;
    uint16_t *pixels;
    if (out == NULL) return fail(error, "Missing PPM image output");
    memset(out, 0, sizeof *out);
    clear_error(error);
    if (data == NULL || size < 3 || size > LULA_MAX_FILE_BYTES
            || data[0] != 'P' || data[1] != '6' || !ppm_space(data[2]))
        return fail(error, "Expected binary P6 PPM within the file-size limit");
    if (!ppm_number(data, size, &cursor, &width, error)
            || !ppm_number(data, size, &cursor, &height, error)
            || !ppm_number(data, size, &cursor, &maxval, error)) return 0;
    if (width == 0 || height == 0 || (size_t)width > LULA_MAX_IMAGE_PIXELS / height || maxval != 255)
        return fail(error, "PPM requires nonempty dimensions and maxval255");
    /* Consume one separator, treating CRLF as one line-ending. Never skip
     * raster bytes merely because their RGB value looks like whitespace. */
    if (data[cursor++] == '\r' && cursor < size && data[cursor] == '\n') ++cursor;
    count = (size_t)width * height;
    if (size - cursor != count * 3) return fail(error, "PPM raster length differs from dimensions");
    pixels = (uint16_t *)malloc(count * sizeof *pixels);
    if (pixels == NULL) return fail(error, "Cannot allocate PPM RGB565 pixels");
    for (i = 0; i < count; ++i) {
        unsigned r = data[cursor + i * 3], g = data[cursor + i * 3 + 1], b = data[cursor + i * 3 + 2];
        pixels[i] = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
    }
    out->width = (uint16_t)width; out->height = (uint16_t)height;
    out->pixel_count = count; out->pixels = pixels; out->version_word = 16; out->mode = 2;
    return 1;
}
