#include "lula_resources.h"

#include <stdio.h>
#include <string.h>

static void usage(void)
{
    (void)fprintf(stderr,
        "Lula reconstructed resource reader (NGS; TBF modes 0/2)\n"
        "Usage:\n"
        "  lula-resource-cli inspect-ngs INPUT\n"
        "  lula-resource-cli extract-ngs INPUT INDEX OUTPUT\n"
        "  lula-resource-cli decode-tbf INPUT OUTPUT.ppm\n"
        "  lula-resource-cli decode-ngs INPUT INDEX OUTPUT.ppm\n"
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

int main(int argc, char **argv)
{
    LulaBlob blob = {0};
    LulaNgs archive = {0};
    LulaImage image = {0};
    LulaError error = {{0}};
    const unsigned char *payload;
    size_t payload_size, index = 0;
    int okay = 0, command;

    if (argc == 3 && strcmp(argv[1], "inspect-ngs") == 0) command = 1;
    else if (argc == 5 && strcmp(argv[1], "extract-ngs") == 0) command = 2;
    else if (argc == 4 && strcmp(argv[1], "decode-tbf") == 0) command = 3;
    else if (argc == 5 && strcmp(argv[1], "decode-ngs") == 0) command = 4;
    else { usage(); return 2; }
    if ((command == 2 || command == 4) && !parse_index(argv[3], &index)) {
        (void)fprintf(stderr, "INDEX must contain decimal digits in the range 0..65535\n");
        return 2;
    }
    if (!lula_blob_read(argv[2], &blob, &error)) goto done;
    payload = blob.data;
    payload_size = blob.size;
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
    lula_blob_free(&blob);
    return okay ? 0 : 1;
}
