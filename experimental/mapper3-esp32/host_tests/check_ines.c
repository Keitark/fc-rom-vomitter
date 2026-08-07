#include <stdio.h>
#include <stdlib.h>

#include "ines.h"

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: check_ines ROM.nes\n");
        return 2;
    }
    FILE *file = NULL;
#ifdef _MSC_VER
    (void)fopen_s(&file, argv[1], "rb");
#else
    file = fopen(argv[1], "rb");
#endif
    if (file == NULL || fseek(file, 0, SEEK_END) != 0) {
        perror(argv[1]);
        if (file != NULL) {
            fclose(file);
        }
        return 2;
    }
    const long file_length = ftell(file);
    if (file_length < 0 || fseek(file, 0, SEEK_SET) != 0) {
        perror(argv[1]);
        fclose(file);
        return 2;
    }
    uint8_t *rom = malloc((size_t)file_length);
    nescart_image_t *image = calloc(1, sizeof(*image));
    if (rom == NULL || image == NULL ||
        fread(rom, 1, (size_t)file_length, file) != (size_t)file_length) {
        fprintf(stderr, "could not read ROM\n");
        free(rom);
        free(image);
        fclose(file);
        return 2;
    }
    fclose(file);

    char error[160];
    const int result = ines_normalize(
        rom, (size_t)file_length, image, error, sizeof(error));
    free(rom);
    if (result != 0) {
        fprintf(stderr, "rejected: %s\n", error);
        free(image);
        return 1;
    }
    printf("accepted mapper=%u chr_banks=%u mirroring=%s crc32=%08x\n",
           (unsigned)image->mapper, (unsigned)image->chr_banks,
           image->mirroring == NESCART_MIRROR_VERTICAL
               ? "vertical" : "horizontal",
           (unsigned)image->crc32);
    free(image);
    return 0;
}
