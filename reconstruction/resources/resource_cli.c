#include "lula_resources.h"

#include <stdio.h>
#include <string.h>

static void usage(void)
{
    (void)fprintf(stderr,
        "Lula reconstructed resource reader (NGS; TBF; TAF)\n"
        "Usage:\n"
        "  lula-resource-cli inspect-ngs INPUT\n"
        "  lula-resource-cli extract-ngs INPUT INDEX OUTPUT\n"
        "  lula-resource-cli decode-tbf INPUT OUTPUT.ppm\n"
        "  lula-resource-cli decode-ngs INPUT INDEX OUTPUT.ppm\n"
        "  lula-resource-cli inspect-taf INPUT\n"
        "  lula-resource-cli decode-taf INPUT INDEX OUTPUT.ppm\n"
        "  lula-resource-cli import-taf INPUT INDEX EDIT.ppm OUTPUT.taf\n"
        "INDEX is zero-based decimal. Outputs must not exist.\n");
}

static int parse_index(const char *text, size_t *index)
{
    size_t value = 0;
    const unsigned char *p = (const unsigned char *)text;
    if (p == NULL || *p == '\0') return 0;
    while (*p != '\0') {
        unsigned digit;
        if (*p < '0' || *p > '9') return 0;
        digit = (unsigned)(*p - '0');
        if (value > (65535u - digit) / 10u) return 0;
        value = value * 10u + digit;
        ++p;
    }
    *index = value;
    return 1;
}

static int inspect(const LulaNgs *archive)
{
    size_t i;
    if (printf("{\"format\":\"NGS\",\"version_word\":%u,\"count\":%u,\"size\":%lu,\"entries\":[",
               (unsigned)archive->version_word, (unsigned)archive->count,
               (unsigned long)archive->size) < 0)
        return 0;
    for (i = 0; i < archive->count; ++i) {
        const LulaNgsEntry *entry = &archive->entries[i];
        if (printf("%s{\"index\":%lu,\"offset\":%lu,\"size\":%lu,\"type_word\":%u}",
                   i != 0 ? "," : "", (unsigned long)i, (unsigned long)entry->offset,
                   (unsigned long)entry->size, (unsigned)entry->type_word) < 0)
            return 0;
    }
    return printf("]}\n") >= 0 && fflush(stdout) == 0;
}

static int inspect_taf(const LulaTaf *animation)
{
    size_t i;
    if (printf("{\"format\":\"TAF\",\"version_word\":%u,\"count\":%u,\"size\":%lu,"
               "\"decoded_bytes\":%lu,\"trailer_table_count\":%u,\"frames\":[",
               (unsigned)animation->version_word, (unsigned)animation->count,
               (unsigned long)animation->size, (unsigned long)animation->decoded_bytes,
               (unsigned)animation->trailer_table_count) < 0) return 0;
    for (i = 0; i < animation->count; ++i) {
        const LulaTafFrame *frame = &animation->frames[i];
        if (printf("%s{\"index\":%lu,\"offset\":%lu,\"size\":%lu,\"payload_offset\":%lu,"
                   "\"width\":%u,\"height\":%u,\"mode\":%u,\"flag_byte\":%u,\"empty_sentinel\":%s}",
                   i != 0 ? "," : "", (unsigned long)i, (unsigned long)frame->offset,
                   (unsigned long)frame->size, (unsigned long)frame->payload_offset,
                   (unsigned)frame->width, (unsigned)frame->height,
                   (unsigned)frame->mode, (unsigned)frame->flag_byte,
                   frame->empty_sentinel ? "true" : "false") < 0) return 0;
    }
    return printf("]}\n") >= 0 && fflush(stdout) == 0;
}

int main(int argc, char **argv)
{
    LulaBlob blob = {0};
    LulaNgs archive = {0};
    LulaTaf animation = {0};
    LulaBlob edit = {0}, replaced = {0};
    LulaImage image = {0};
    LulaError error = {{0}};
    const unsigned char *payload;
    size_t payload_size, index = 0;
    int okay = 0, command;

    if (argc == 3 && strcmp(argv[1], "inspect-ngs") == 0) command = 1;
    else if (argc == 5 && strcmp(argv[1], "extract-ngs") == 0) command = 2;
    else if (argc == 4 && strcmp(argv[1], "decode-tbf") == 0) command = 3;
    else if (argc == 5 && strcmp(argv[1], "decode-ngs") == 0) command = 4;
    else if (argc == 3 && strcmp(argv[1], "inspect-taf") == 0) command = 5;
    else if (argc == 5 && strcmp(argv[1], "decode-taf") == 0) command = 6;
    else if (argc == 6 && strcmp(argv[1], "import-taf") == 0) command = 7;
    else { usage(); return 2; }
    if ((command == 2 || command == 4 || command == 6 || command == 7)
            && !parse_index(argv[3], &index)) {
        (void)fprintf(stderr, "INDEX must contain decimal digits in the range 0..65535\n");
        return 2;
    }
    if (!lula_blob_read(argv[2], &blob, &error)) goto done;
    payload = blob.data;
    payload_size = blob.size;
    if (command >= 5) {
        if (!lula_taf_parse(blob.data, blob.size, &animation, &error)) goto done;
        if (command == 5) {
            okay = inspect_taf(&animation);
            if (!okay) (void)snprintf(error.message, sizeof error.message, "Cannot write TAF metadata");
        } else if (command == 6) {
            if (!lula_taf_decode_frame(&animation, index, &image, &error)) goto done;
            if (image.pixel_count == 0) {
                (void)snprintf(error.message, sizeof error.message,
                    "Empty TAF sentinel has no image; preserve it through archive operations");
                goto done;
            }
            okay = lula_ppm_write_new(argv[4], &image, &error);
        } else {
            if (index >= animation.count) {
                (void)snprintf(error.message, sizeof error.message, "TAF frame index is out of range");
                goto done;
            }
            if (animation.frames[index].empty_sentinel) {
                (void)snprintf(error.message, sizeof error.message,
                    "Empty TAF sentinel has no image to import; archive operations preserve it");
                goto done;
            }
            if (!lula_blob_read(argv[4], &edit, &error)
                    || !lula_ppm_decode(edit.data, edit.size, &image, &error)
                    || !lula_taf_replace_frame(&animation, index, &image, &replaced, &error)) goto done;
            okay = lula_blob_write_new(argv[5], replaced.data, replaced.size, &error);
        }
        goto done;
    }
    if (command != 3) {
        if (!lula_ngs_parse(blob.data, blob.size, &archive, &error)) goto done;
        if (command == 1) {
            okay = inspect(&archive);
            if (!okay) (void)snprintf(error.message, sizeof error.message, "Cannot write NGS metadata");
            goto done;
        }
        if (!lula_ngs_payload(&archive, index, &payload, &payload_size, NULL, &error)) goto done;
        if (command == 2) {
            okay = lula_blob_write_new(argv[4], payload, payload_size, &error);
            goto done;
        }
    }
    if (!lula_tbf_decode(payload, payload_size, &image, &error)) goto done;
    okay = lula_ppm_write_new(argv[command == 3 ? 3 : 4], &image, &error);
done:
    if (!okay) (void)fprintf(stderr, "Resource error: %s\n", error.message);
    lula_image_free(&image);
    lula_ngs_close(&archive);
    lula_taf_close(&animation);
    lula_blob_free(&edit);
    lula_blob_free(&replaced);
    lula_blob_free(&blob);
    return okay ? 0 : 1;
}
