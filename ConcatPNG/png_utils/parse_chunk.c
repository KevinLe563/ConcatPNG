#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include "crc.h"

char *parse_chunk(FILE *file, int *length, char *type)
{
    fread(length, 4, 1, file);
    *length = ntohl(*length);

    // printf("length: %d\n", *length);

    char *data = malloc(*length * sizeof(char));
    unsigned int *png_crc = malloc(sizeof(unsigned int));

    // Read type and data
    fread(type, 1, 4, file);
    fread(data, 1, *length, file);
    fread(png_crc, 4, 1, file);
    *png_crc = ntohl(*png_crc);

    char *newBuffer = (char *)malloc(*length + 4);
    for (int i = 0; i < 4; i++)
    {
        newBuffer[i] = type[i];
    }
    for (int i = 0; i < *length; i++)
    {
        newBuffer[i + 4] = data[i];
    }
    unsigned long computed_crc = crc(newBuffer, *length + 4);
    // printf("crc: %lx (computed)\n", computed_crc);

    if (computed_crc != *png_crc)
    {
        printf("%s chunk CRC error: computed %lx, expected %x\n", type, computed_crc, *png_crc);
        free(data);
        free(png_crc);
        free(newBuffer);
        fclose(file);
        exit(-1);
    }

    free(newBuffer);
    free(png_crc);

    return data;
}