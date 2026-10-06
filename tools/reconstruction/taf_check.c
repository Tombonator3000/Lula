/* Check public C TAF APIs against a concatenated RGB565 oracle produced by
 * tools/assets.py. Run through verify_taf.py; no original assets are changed. */
#include "lula_resources.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Every RGB565 word survives bit-replicated RGB888 and high-bit import. */
static int check_ppm_quantization(void)
{
    static const char header[] = "P6\n256 256\n255\n";
    size_t head = sizeof header - 1, size = head + 65536u * 3, i;
    unsigned char *ppm = (unsigned char *)malloc(size);
    LulaImage image = {0};
    LulaError error = {{0}};
    int okay = 0;
    if (ppm == NULL) return 0;
    memcpy(ppm, header, head);
    for (i = 0; i < 65536; ++i) {
        unsigned r = (unsigned)(i >> 11), g = (unsigned)((i >> 5) & 63), b = (unsigned)(i & 31);
        ppm[head + i * 3] = (unsigned char)((r << 3) | (r >> 2));
        ppm[head + i * 3 + 1] = (unsigned char)((g << 2) | (g >> 4));
        ppm[head + i * 3 + 2] = (unsigned char)((b << 3) | (b >> 2));
    }
    if (!lula_ppm_decode(ppm, size, &image, &error)) goto done;
    for (i = 0; i < 65536; ++i)
        if (image.pixels[i] != i) goto done;
    okay = 1;
done:
    free(ppm);
    lula_image_free(&image);
    if (!okay) (void)fprintf(stderr, "PPM RGB565 quantization failed: %s\n", error.message);
    return okay;
}

int main(int argc, char **argv)
{
    LulaBlob source = {0}, expected = {0}, replaced = {0};
    LulaTaf animation = {0};
    LulaImage image = {0}, invalid = {0};
    LulaError error = {{0}};
    size_t i, j, offset = 0;
    int okay = 0;
    if (argc != 3) {
        (void)fprintf(stderr, "Usage: lula-taf-check INPUT.taf EXPECTED.rgb565\n");
        return 2;
    }
    if (!check_ppm_quantization()) return 1;
    if (!lula_blob_read(argv[1], &source, &error)
            || !lula_blob_read(argv[2], &expected, &error)
            || !lula_taf_parse(source.data, source.size, &animation, &error)) goto done;
    if (lula_taf_decode_frame(&animation, animation.count, &image, &error)
            || lula_taf_replace_frame(&animation, animation.count, &invalid, &replaced, &error)) {
        (void)fprintf(stderr, "Out-of-range frame index accepted\n");
        goto done;
    }
    for (i = 0; i < animation.count; ++i) {
        if (!lula_taf_decode_frame(&animation, i, &image, &error)) goto done;
        if (image.pixel_count * 2 > expected.size - offset) {
            (void)fprintf(stderr, "Expected pixels are truncated\n");
            goto done;
        }
        for (j = 0; j < image.pixel_count; ++j) {
            uint16_t word = (uint16_t)(expected.data[offset + 2 * j]
                | (uint16_t)((uint16_t)expected.data[offset + 2 * j + 1] << 8));
            if (image.pixels[j] != word) {
                (void)fprintf(stderr, "Pixels differ in frame%lu word%lu\n",
                               (unsigned long)i, (unsigned long)j);
                goto done;
            }
        }
        offset += image.pixel_count * 2;
        invalid = image;
        invalid.width ^= 1;
        if (lula_taf_replace_frame(&animation, i, &invalid, &replaced, &error)) {
            (void)fprintf(stderr, "Changed frame dimensions accepted\n");
            goto done;
        }
        if (!lula_taf_replace_frame(&animation, i, &image, &replaced, &error)) goto done;
        if (replaced.size != source.size || memcmp(replaced.data, source.data, source.size) != 0) {
            (void)fprintf(stderr, "Unchanged frame replacement changed archive bytes\n");
            goto done;
        }
        lula_image_free(&image);
        lula_blob_free(&replaced);
    }
    if (offset != expected.size) {
        (void)fprintf(stderr, "Unexpected trailing oracle pixels\n");
        goto done;
    }
    (void)printf("%u frames\n", (unsigned)animation.count);
    okay = 1;
done:
    if (!okay && error.message[0] != '\0') (void)fprintf(stderr, "%s\n", error.message);
    lula_image_free(&image);
    lula_blob_free(&replaced);
    lula_taf_close(&animation);
    lula_blob_free(&source);
    lula_blob_free(&expected);
    return okay ? 0 : 1;
}
