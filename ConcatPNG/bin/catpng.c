#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include "png_utils.h"
#include "png_util/zutil.h"

int main(int argc, char *argv[])
{
    if (argc < 3)
    {
        printf("Need to provide at least two images to concatenate.\n");
        exit(-1);
    }

    // Running variables for final PNG
    int final_height = 0, final_width = 0;
    unsigned char *final_data = malloc(sizeof(unsigned char));
    long unsigned int *final_data_len = malloc(sizeof(long unsigned int));
    *final_data_len = 0;

    // Read all the PNGs to concatenate
    for (int i = 1; i < argc; i++)
    {
        FILE *file = fopen(argv[i], "rb");
        if (file == NULL)
        {
            printf("Could not open file %s.\n", argv[i]);
            exit(-1);
        }

        char *bytes = malloc(8 * sizeof(char));
        fread(bytes, 1, 8, file);

        // Check if valid PNG file
        if (!(bytes[1] == 80 && bytes[2] == 78 && bytes[3] == 71))
        {
            printf("%s: Not a PNG file\n", argv[i]);
            exit(-1);
        }

        free(bytes);

        int *length = malloc(sizeof(int));
        char *type = malloc(5 * sizeof(char));
        type[4] = '\0';
        unsigned char *data;

        // Read IHDR chunk
        data = parse_chunk(file, length, type);

        int width = data[3] | data[2] << 8 | data[1] << 16 | data[0] << 24;
        int height = data[7] | data[6] << 8 | data[5] << 16 | data[4] << 24;

        if (final_width == 0)
        {
            final_width = width;
        }
        final_height += height;

        free(data);

        // Read IDAT chunk
        data = parse_chunk(file, length, type);

        // Inflate the pixel data
        long unsigned int *inflated_len = malloc(sizeof(long unsigned int));
        *inflated_len = height * (width * 4 + 1);
        char *inflated = malloc(*inflated_len * sizeof(char));
        int res = mem_inf(inflated, inflated_len, data, (long unsigned int)*length);
        if (res != 0)
        {
            printf("Error occurred while inflating %s with code %d", argv[i], res);

            free(inflated_len);
            free(inflated);
            free(length);
            free(type);
            free(final_data);
            free(final_data_len);
            exit(-1);
        }

        // Calculate new size of final image data
        char *new_final = malloc((*final_data_len + *inflated_len) * sizeof(char));

        // Copy into a new file
        for (int i = 0; i < *final_data_len; i++)
        {
            new_final[i] = final_data[i];
        }
        for (int i = 0; i < *inflated_len; i++)
        {
            new_final[i + *final_data_len] = inflated[i];
        }

        // Free old data and set new data
        free(final_data);
        *final_data_len += *inflated_len;
        final_data = new_final;
        free(inflated_len);
        free(inflated);
        free(data);

        // Read IEND chunk
        data = parse_chunk(file, length, type);
        free(data);

        free(length);
        free(type);
        fclose(file);
    }

    // Compress the final data
    unsigned char *final_data_def = malloc(*final_data_len * sizeof(unsigned char));
    int result = mem_def(final_data_def, final_data_len, final_data, *final_data_len, Z_DEFAULT_COMPRESSION);
    if (result != 0)
    {
        printf("Error occurred while deflating with code %d", result);

        free(final_data_def);
        free(final_data);
        free(final_data_len);
        exit(-1);
    }

    free(final_data);

    // Create a new png file or overwrite if it already exists
    FILE *f = fopen("all.png", "wb");

    // Write the 8 header bytes
    unsigned char header_bytes[8] = {0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a};
    fwrite(header_bytes, 1, 8, f);

    // Write the IHDR chunk
    int ihdr_length = htonl(13);
    fwrite(&ihdr_length, 4, 1, f);

    unsigned char ihdr_crc_buffer[17] = {
        'I', 'H', 'D', 'R', // type
        'x', 'x', 'x', 'x', // width
        'x', 'x', 'x', 'x', // height
        8, 6, 0, 0, 0       // other options
    };

    for (int i = 0; i < 4; i++)
    {
        // Write bytes in big-endian
        ihdr_crc_buffer[4 + i] = (final_width >> (3 - i) * 8) & 0xff;
        ihdr_crc_buffer[8 + i] = (final_height >> (3 - i) * 8) & 0xff;
    }

    fwrite(ihdr_crc_buffer, 1, 17, f);

    unsigned int ihdr_crc = htonl(crc(ihdr_crc_buffer, 17));
    fwrite(&ihdr_crc, 4, 1, f);

    // Write the IDAT chunk
    int net_fdl = htonl(*final_data_len);
    fwrite(&net_fdl, 4, 1, f);

    unsigned char *idat_crc_buffer = malloc((*final_data_len + 4) * sizeof(unsigned char));
    idat_crc_buffer[0] = 'I';
    idat_crc_buffer[1] = 'D';
    idat_crc_buffer[2] = 'A';
    idat_crc_buffer[3] = 'T';
    for (int i = 0; i < *final_data_len; i++)
    {
        idat_crc_buffer[4 + i] = final_data_def[i];
    }
    fwrite(idat_crc_buffer, 1, *final_data_len + 4, f);

    unsigned int idat_crc = htonl(crc(idat_crc_buffer, *final_data_len + 4));
    fwrite(&idat_crc, 4, 1, f);

    free(idat_crc_buffer);

    // Write the IEND chunk
    int iend_length = 0;
    fwrite(&iend_length, 4, 1, f);

    unsigned char iend_buffer[8] = {
        'I', 'E', 'N', 'D',    // type
        0xae, 0x42, 0x60, 0x82 // crc (always the same, since there is no data)
    };
    fwrite(iend_buffer, 1, 8, f);

    fclose(f);

    free(final_data_def);
    free(final_data_len);

    return 0;
}