#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include <pthread.h>
#include <arpa/inet.h>
#include "../png_utils/parse_chunk.h"
#include "../png_utils/zutil.h"

#define ECE252_HEADER "X-Ece252-Fragment: "
char IMG_URL[60];
#define DUM_URL "https://example.com/"

#define BUF_SIZE 1048576 /* 1024*1024 = 1M */
#define BUF_INC 524288   /* 1024*512  = 0.5M */

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

#define max(a, b)                   \
    (                               \
        {                           \
            __typeof__(a) _a = (a); \
            __typeof__(b) _b = (b); \
            _a > _b ? _a : _b;      \
        })

typedef struct recv_buf
{
    char *buf;       /* memory to hold a copy of received data */
    size_t size;     /* size of valid data in buf in bytes*/
    size_t max_size; /* max capacity of buf in bytes*/
    int seq;         /* sequence number */
} RECV_BUF;

size_t write_callback(char *p_recv, size_t size, size_t nmemb, void *p_userdata)
{
    size_t realsize = size * nmemb;
    RECV_BUF *p = (RECV_BUF *)p_userdata;

    if (p->size + realsize + 1 > p->max_size)
    { /* hope this rarely happens */
        /* received data is not 0 terminated, add one byte for terminating 0 */
        size_t new_size = p->max_size + max(BUF_INC, realsize + 1);
        char *q = realloc(p->buf, new_size);
        if (q == NULL)
        {
            perror("realloc"); /* out of memory */
            return -1;
        }
        p->buf = q;
        p->max_size = new_size;
    }

    memcpy(p->buf + p->size, p_recv, realsize); /*copy data from libcurl*/
    p->size += realsize;
    p->buf[p->size] = 0;

    return realsize;
}

int recv_buf_init(RECV_BUF *ptr, size_t max_size)
{
    void *p = NULL;

    if (ptr == NULL)
    {
        return 1;
    }

    p = malloc(max_size);
    if (p == NULL)
    {
        return 2;
    }

    ptr->buf = p;
    ptr->size = 0;
    ptr->max_size = max_size;
    return 0;
}

int recv_buf_cleanup(RECV_BUF *ptr)
{
    if (ptr == NULL)
    {
        return 1;
    }

    free(ptr->buf);
    ptr->size = 0;
    ptr->max_size = 0;
    return 0;
}

size_t header_cb_curl(char *p_recv, size_t size, size_t nmemb, void *userdata)
{
    int realsize = size * nmemb;
    RECV_BUF *p = userdata;

    if (realsize > strlen(ECE252_HEADER) &&
        strncmp(p_recv, ECE252_HEADER, strlen(ECE252_HEADER)) == 0)
    {
        /* extract img sequence number */
        p->seq = atoi(p_recv + strlen(ECE252_HEADER));
    }
    return realsize;
}

/**
 * @brief output data in memory to a file
 * @param path const char *, output file path
 * @param in  void *, input data to be written to the file
 * @param len size_t, length of the input data in bytes
 */

int write_file(const char *path, const void *in, size_t len)
{
    FILE *fp = NULL;

    if (path == NULL)
    {
        fprintf(stderr, "write_file: file name is null!\n");
        return -1;
    }

    if (in == NULL)
    {
        fprintf(stderr, "write_file: input data is null!\n");
        return -1;
    }

    fp = fopen(path, "wb");
    if (fp == NULL)
    {
        perror("fopen");
        return -2;
    }

    if (fwrite(in, 1, len, fp) != len)
    {
        fprintf(stderr, "write_file: imcomplete write!\n");
        return -3;
    }
    return fclose(fp);
}

typedef struct
{
    char *url;
    unsigned char **all_inf_data;
    int *all_inf_len_array;
    long unsigned int *all_inf_len;
    int *uniq_chunks;
} thread_args_type;

void *fetch_png(void *argument)
{
    CURL *curl_handle;
    CURLcode res;

    thread_args_type *thread_args = (thread_args_type *)argument;
    char *url = thread_args->url;
    unsigned char **all_inf_data = thread_args->all_inf_data;
    int *all_inf_len_array = thread_args->all_inf_len_array;
    long unsigned int *all_inf_len = thread_args->all_inf_len;
    int *uniq_chunks = thread_args->uniq_chunks;

    RECV_BUF recv_buf;
    // printf("Initing a new recv buf!\n");

    curl_handle = curl_easy_init();
    if (!curl_handle)
    {
        printf("Failed to initialize curl. Exiting.\n");
        return NULL;
    }

    curl_easy_setopt(curl_handle, CURLOPT_URL, url);

    /* register write call back function to process received data */
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, write_callback);
    /* user defined data structure passed to the call back function */
    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)&recv_buf);

    /* register header call back function to process received header data */
    curl_easy_setopt(curl_handle, CURLOPT_HEADERFUNCTION, header_cb_curl);
    /* user defined data structure passed to the call back function */
    curl_easy_setopt(curl_handle, CURLOPT_HEADERDATA, (void *)&recv_buf);

    while (*uniq_chunks < 50)
    {

        recv_buf_init(&recv_buf, BUF_SIZE);
        // Send request
        res = curl_easy_perform(curl_handle);

        if (res != CURLE_OK)
        {
            fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
        }

        FILE *file = fmemopen(recv_buf.buf, recv_buf.size, "rb");

        // parse 3 chunks in final_data
        char bytes[8];
        fread(bytes, 1, 8, file);

        // Check if valid PNG file
        if (!(bytes[1] == 80 && bytes[2] == 78 && bytes[3] == 71))
        {
            printf("Not a PNG file\n");
            exit(-1);
        }

        int length;
        char type[5];
        type[4] = '\0';
        unsigned char *data;

        // Read IHDR chunk
        data = parse_chunk(file, &length, type);

        int width = data[3] | data[2] << 8 | data[1] << 16 | data[0] << 24;
        int height = data[7] | data[6] << 8 | data[5] << 16 | data[4] << 24;

        free(data);

        // Read IDAT chunk
        data = parse_chunk(file, &length, type);

        // Inflate the pixel data
        long unsigned int inflated_len = height * (width * 4 + 1);
        char *inflated = malloc(inflated_len * sizeof(char));
        int res = mem_inf(inflated, &inflated_len, data, (long unsigned int)length);
        if (res != 0)
        {
            printf("Error occurred while inflating with code %d", res);

            free(inflated);
            free(data);
            exit(-1);
        }

        int seq = recv_buf.seq;

        // New image!
        // critical section
        // We don't want multiple threads to read/write to
        pthread_mutex_lock(&mutex);
        if (all_inf_data[seq] == 0)
        {
            all_inf_data[seq] = malloc(inflated_len * sizeof(unsigned char));
            memcpy(all_inf_data[seq], inflated, inflated_len);
            all_inf_len_array[seq] = inflated_len;
            (*uniq_chunks)++;
            *all_inf_len += inflated_len;
            printf("Found %d, %d/50\n", seq, *uniq_chunks);
        }
        pthread_mutex_unlock(&mutex);

        // Free old datsa and set new data
        free(inflated);
        free(data);
        fclose(file);

        // clear recv_buf
        recv_buf_cleanup(&recv_buf);
    }
    curl_easy_cleanup(curl_handle);
    return NULL;
}

int main(int argc, char **argv)
{
    // filter command line args
    int c;
    int thread_num = 1;
    int image_num = 1;

    while ((c = getopt(argc, argv, "t:n:")) != -1)
    {
        switch (c)
        {
        case 't':
            thread_num = strtoul(optarg, NULL, 10);
            if (thread_num <= 0)
            {
                return -1;
            }
            break;
        case 'n':
            image_num = strtoul(optarg, NULL, 10);
            if (image_num <= 0 || image_num > 3)
            {
                printf("Option -n can be only be 1, 2 or 3.");
                return -1;
            }
            break;
        default:
            return -1;
        }
    }

#ifdef DEBUG_1
    fprintf(stderr, "%s: URL is %s\n", argv[0], url);
#endif /* DEBUG_1 */
    curl_global_init(CURL_GLOBAL_DEFAULT);

    unsigned char *all_inf_data[50];
    int all_inf_len_array[50];
    long unsigned int *all_inf_len = malloc(sizeof(long unsigned int));
    *all_inf_len = 0;
    int *uniq_chunks = malloc(sizeof(int));
    *uniq_chunks = 0;
    memset(all_inf_data, 0, sizeof(all_inf_data));

    pthread_t *threads = malloc(thread_num * sizeof(pthread_t));
    thread_args_type thread_args[thread_num];

    for (int i = 0; i < thread_num; i++)
    {
        sprintf(IMG_URL, "http://ece252-%d.uwaterloo.ca:2520/image?img=%d", (i % 3) + 1, image_num);
        thread_args[i].url = IMG_URL;
        thread_args[i].all_inf_data = all_inf_data;
        thread_args[i].all_inf_len_array = all_inf_len_array;
        thread_args[i].all_inf_len = all_inf_len;
        thread_args[i].uniq_chunks = uniq_chunks;

        pthread_create(threads + i, NULL, fetch_png, thread_args + i);
    }

    for (int i = 0; i < thread_num; i++)
    {
        pthread_join(threads[i], NULL);
    }

    pthread_mutex_destroy(&mutex);
    free(threads);
    free(uniq_chunks);

    printf("Found all 50!\n");

    // concat all 50 pieces of png together
    unsigned char *all_inf = malloc(*all_inf_len * sizeof(unsigned char));
    unsigned char *all_def = malloc(*all_inf_len * sizeof(unsigned char));
    all_inf[0] = '\0';
    int cursor = 0;
    for (int i = 0; i < 50; i++)
    {
        for (int j = 0; j < all_inf_len_array[i]; j++)
        {
            all_inf[cursor + j] = all_inf_data[i][j];
        }
        cursor += all_inf_len_array[i];
        // printf("%s\n", all_inf_data[i]);
        // printf("%s\n", all_inf);
    }

    int result = mem_def(all_def, all_inf_len, all_inf, *all_inf_len, Z_DEFAULT_COMPRESSION);
    if (result != 0)
    {
        printf("Error occurred while deflating with code %d", result);

        free(all_inf);
        free(all_def);
        for (int i = 0; i < 50; i++)
        {
            free(all_inf_data[i]);
        }
        exit(-1);
    }

    for (int i = 0; i < 50; i++)
    {
        free(all_inf_data[i]);
    }
    free(all_inf);

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
        ihdr_crc_buffer[4 + i] = (400 >> (3 - i) * 8) & 0xff; // height
        ihdr_crc_buffer[8 + i] = (300 >> (3 - i) * 8) & 0xff; // width
    }

    fwrite(ihdr_crc_buffer, 1, 17, f);

    unsigned int ihdr_crc = htonl(crc(ihdr_crc_buffer, 17));
    fwrite(&ihdr_crc, 4, 1, f);

    // Write the IDAT chunk
    int net_fdl = htonl(*all_inf_len);
    fwrite(&net_fdl, 4, 1, f);

    unsigned char *idat_crc_buffer = malloc((*all_inf_len + 4) * sizeof(unsigned char));
    idat_crc_buffer[0] = 'I';
    idat_crc_buffer[1] = 'D';
    idat_crc_buffer[2] = 'A';
    idat_crc_buffer[3] = 'T';
    for (int i = 0; i < *all_inf_len; i++)
    {
        idat_crc_buffer[4 + i] = all_def[i];
    }
    fwrite(idat_crc_buffer, 1, *all_inf_len + 4, f);

    unsigned int idat_crc = htonl(crc(idat_crc_buffer, *all_inf_len + 4));
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

    free(all_def);
    free(all_inf_len);

    curl_global_cleanup();
    return 0;
}
