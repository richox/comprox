/*
 * Copyright (C) 2011-2012 by Zhang Li <RichSelian at gmail.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the project nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE PROJECT AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE PROJECT OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/time.h>
#include "cr-datablock.h"
#include "cr-filter.h"
#include "cr-dicpick.h"
#include "cr-diccode.h"

#if defined(_WIN32) || defined(_WIN64) /* windows ports */
#include <fcntl.h> /* for setmode() */
#endif

/* implement in
 *  src/roxmain/main.c
 *  src/rolzmain/main.c
 *  src/ropmain/main.c
 */
extern const char* cr_magic_header;
extern const char* cr_start_info;
extern const char* cr_usage_info;
extern int cr_process_arguments(int argc, char** argv);

/* implement in
 *  src/roxmain/cr-coder.c
 *  src/rolzmain/cr-coder.c
 *  src/ropmain/cr-coder.c
 */
void reset_models();
void lzencode(struct data_block_t* ib, struct data_block_t* ob, int print_information);
void lzdecode(struct data_block_t* ib, struct data_block_t* ob, int print_information);

/* main wrapper configuration */
uint32_t cr_split_size = 16 * 1048576; /* default block size = 16MB */
int cr_filt_enable = 0;
int cr_prec_enable = 0;

/* handle magic header */
static inline int write_magic(FILE* stream) {
    fwrite(cr_magic_header, 1, strlen(cr_magic_header), stream);
    return 0;
}
static inline int check_magic(FILE* stream) {
    char in_magic_header[256] = {0};

    fread(in_magic_header, 1, strlen(cr_magic_header), stream);
    if(memcmp(in_magic_header, cr_magic_header, strlen(cr_magic_header)) == 0) {
        return 1;
    }
    return 0;
}

/* swap block */
static inline void swap_xyblock(data_block_t** xb, data_block_t** yb) {
    data_block_t* tmpblock = *xb;
    *xb = *yb;
    *yb = tmpblock;
    return;
}

int cr_main(int argc, char** argv) {
    struct {
        uint32_t m_size;
        uint8_t  m_filt;
        uint8_t  m_prec;
    } __attribute__((packed)) block_header;

    const char* src_name = "<stdin>";
    const char* dst_name = "<stdout>";
    FILE* src_file;
    FILE* dst_file;
    data_block_t ib = INITIAL_BLOCK;
    data_block_t ob = INITIAL_BLOCK;
    data_block_t* xb;
    data_block_t* yb;
    uint32_t src_size;
    uint32_t dst_size;
    int filt = 0;
    int enc;

    data_block_t dic_xb = INITIAL_BLOCK;
    data_block_t dic_yb = INITIAL_BLOCK;
    int nword;

    struct timeval time_start;
    struct timeval time_end;
    double cost_time;

    gettimeofday(&time_start, NULL);
    src_file = stdin;
    dst_file = stdout;

#if defined(_WIN32) || defined(_WIN64)
    /* we need to set stdin/stdout to binary mode under windows */
    setmode(fileno(stdin), O_BINARY);
    setmode(fileno(stdout), O_BINARY);
#endif

    /* reset global models for compressing/decompressing */
    reset_models();

    /* process arguments */
    if((argc = cr_process_arguments(argc, argv)) == 0) {
        return -1;
    }

    /* start! */
    fprintf(stderr, "%s\n", cr_start_info);
    if(argc >=2 && argc <= 4 && strcmp(argv[1], "e") == 0) { /* encode */
        enc = 1;
        if(argc >= 3) src_name = argv[2], src_file = fopen(src_name, "rb");
        if(argc >= 4) dst_name = argv[3], dst_file = fopen(dst_name, "wb");
        if(src_file == stdin) { /* copy input data to temporary file, since stdin doesn't support rewind() */
            data_block_reserve(&ib, 1048576);
            src_file = tmpfile();
            while((ib.m_size = fread(ib.m_data, 1, ib.m_capacity, stdin)) > 0) {
                fwrite(ib.m_data, 1, ib.m_size, src_file);
            }
            data_block_destroy(&ib);
            rewind(src_file);
            ib = INITIAL_BLOCK;
        }

        if(src_file != NULL && dst_file != NULL) {
            write_magic(dst_file);
            fprintf(stderr, "compressing %s to %s, block_size = %uMB...\n", src_name, dst_name, cr_split_size / 1048576);

            /* build static dictionary */
            fprintf(stderr, "%s\n", "-> building static dictionary...");
            dicpick(src_file, &dic_xb);
            rewind(src_file);
            nword = dictionary_load((char*)dic_xb.m_data, 1);

            /* encode static dictionary */
            dic_lcp_encode(&dic_xb);
            lzencode(&dic_xb, &dic_yb, 0);
            reset_models();
            fprintf(stderr, "added %d words to dictionary, compressed size = %u bytes\n", nword, dic_yb.m_size);

            /* write static dictionary to dst_file */
            fwrite(&dic_yb.m_size, sizeof(dic_yb.m_size), 1, dst_file);
            fwrite( dic_yb.m_data, 1, dic_yb.m_size, dst_file);
            data_block_destroy(&dic_xb);
            data_block_destroy(&dic_yb);

            while(!ferror(src_file) && !ferror(dst_file) && !feof(src_file)) {
                xb = &ib;
                yb = &ob;
                data_block_resize(xb, cr_split_size);

                /* read blocks */
                xb->m_size = fread(xb->m_data, 1, cr_split_size, src_file);

                /* precompress with filters */
                if(cr_filt_enable) {
                    filt = filter_inplace(xb->m_data, xb->m_size, FILTER_ENC);
                }

                /* encode */
                data_block_resize(yb, 0);
                dictionary_encode(xb, yb);

                if(!cr_prec_enable) {
                    swap_xyblock(&xb, &yb);
                    data_block_resize(yb, 0);
                    lzencode(xb, yb, !ferror(stderr));
                }

                /* write blocks */
                if(yb->m_size > 0) {
                    block_header.m_size = yb->m_size;
                    block_header.m_filt = filt;
                    block_header.m_prec = cr_prec_enable;

                    fwrite(&block_header, sizeof(block_header), 1, dst_file);
                    fwrite(yb->m_data, 1, yb->m_size, dst_file);
                }
            }
            if(ferror(src_file) || ferror(dst_file)) {
                perror("ferror()");
                return -1;
            }
        } else {
            perror("fopen()");
            return -1;
        }
        src_size = ftell(src_file);
        dst_size = ftell(dst_file);
        fclose(src_file);
        fclose(dst_file);

    } else if(argc >= 2 && argc <= 4 && strcmp(argv[1], "d") == 0) { /* decode */
        enc = 0;
        if(argc >= 3) src_name = argv[2], src_file = fopen(src_name, "rb");
        if(argc >= 4) dst_name = argv[3], dst_file = fopen(dst_name, "wb");

        if(src_file == stdin) { /* copy input data to temporary file, since stdin doesn't support rewind() */
            data_block_reserve(&ib, 1048576);
            src_file = tmpfile();
            while((ib.m_size = fread(ib.m_data, 1, ib.m_capacity, stdin)) > 0) {
                fwrite(ib.m_data, 1, ib.m_size, src_file);
            }
            data_block_destroy(&ib);
            rewind(src_file);
            ib = INITIAL_BLOCK;
        }
        if(src_file != NULL && dst_file != NULL) {
            if(!check_magic(src_file)) {
                fprintf(stderr, "%s\n", "check_magic() failed.");
                fclose(src_file);
                fclose(dst_file);
                return -1;
            }
            fprintf(stderr, "decompressing %s to %s...\n", src_name, dst_name);

            /* decode static dictionary */
            fprintf(stderr, "%s\n", "-> decoding static dictionary...");

            /* read size of static dictionary from src_file */
            fread(&dic_yb.m_size, sizeof(dic_yb.m_size), 1, src_file);

            /* read static dictionary from src_file */
            data_block_resize(&dic_yb, dic_yb.m_size);
            fread(dic_yb.m_data, 1, dic_yb.m_size, src_file);

            /* decode static dictionary */
            lzdecode(&dic_yb, &dic_xb, 0);
            reset_models();
            dic_lcp_decode(&dic_xb);

            dictionary_load((char*)dic_xb.m_data, 0);
            data_block_destroy(&dic_xb);
            data_block_destroy(&dic_yb);

            while(!ferror(src_file) && !ferror(dst_file) && !feof(src_file)) {
                xb = &ib;
                yb = &ob;

                /* read blocks */
                if(fread(&block_header, sizeof(block_header), 1, src_file) != 1) {
                    break;
                }
                data_block_resize(yb, block_header.m_size);
                yb->m_size = fread(yb->m_data, 1, yb->m_size, src_file);

                /* decode */
                if(!block_header.m_prec) {
                    data_block_resize(xb, 0);
                    lzdecode(yb, xb, !ferror(stderr));
                    swap_xyblock(&xb, &yb);
                }
                data_block_resize(xb, 0);
                dictionary_decode(yb, xb, dst_file);

                /* precompress with filters */
                if(block_header.m_filt) {
                    filter_inplace(xb->m_data, xb->m_size, FILTER_DEC);
                }

                /* write blocks */
                if(xb->m_size > 0) {
                    fwrite(xb->m_data, 1, xb->m_size, dst_file);
                }
            }
            if(ferror(src_file) || ferror(dst_file)) {
                perror("ferror()");
                return -1;
            }
        } else {
            perror("fopen()");
            return -1;
        }
        src_size = ftell(src_file);
        dst_size = ftell(dst_file);
        fclose(src_file);
        fclose(dst_file);

    } else {
        /* bad argument! */
        fprintf(stderr, "%s\n", cr_usage_info);
        return -1;
    }

    data_block_destroy(&ib);
    data_block_destroy(&ob);

    gettimeofday(&time_end, NULL);
    cost_time = (time_end.tv_sec - time_start.tv_sec) + (time_end.tv_usec - time_start.tv_usec) / 1000000.0;

    fprintf(stderr, "%u bytes => %u bytes\n\n", src_size, dst_size);
    if(enc) {
        fprintf(stderr, "encode-speed:   %.3lf MB/s\n",  src_size / 1048576 / cost_time);
        fprintf(stderr, "cost-time:      %.3lf s\n",     cost_time);
        fprintf(stderr, "compress-ratio: %.3lf\n",       (double)dst_size / src_size);
        fprintf(stderr, "bpb:            %.3lf\n",       (double)dst_size / src_size * 8);
    } else {
        fprintf(stderr, "decode-speed:   %.3lf MB/s\n",  dst_size / 1048576 / cost_time);
        fprintf(stderr, "cost-time:      %.3lf s\n",     cost_time);
        fprintf(stderr, "compress-ratio: %.3lf\n",       (double)src_size / dst_size);
        fprintf(stderr, "bpb:            %.3lf\n",       (double)src_size / dst_size * 8);
    }
    return 0;
}
