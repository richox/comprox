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
#include "filter_bmp.h"

#define BMP_MAGIC 0x4d42

typedef struct bmp_header_t {
    uint16_t m_magic;
    uint32_t m_file_size;
    uint16_t m_reserv1;
    uint16_t m_reserv2;
    uint32_t m_image_offset;

    uint32_t m_info_size;
    uint32_t m_width;
    uint32_t m_height;
    uint16_t m_planes;
    uint16_t m_bits_per_pixel;
    uint32_t m_compression;
    uint32_t m_image_size;
    uint32_t m_hresolution;
    uint32_t m_vresolution;
    uint32_t m_colors;
    uint32_t m_colors_important;
} __attribute__((packed)) bmp_header_t;

static uint32_t min(uint32_t l, uint32_t r) {
    return l < r ? l : r;
}

static uint32_t bmp_transform_column_rgb_delta(uint8_t* buf, uint32_t len, int width, int row_size, int bpp, int en_de) {
    int trans_column = len / row_size;
    int x;
    int y;

    if(en_de == 0) { /* encode */
        for(y = 0; y < trans_column; y++) { /* color transform: R-=G, B-=G */
            for(x = 0; x < width; x++) {
                if(bpp == 24) {
                    buf[y * row_size + x * 3 + 0] -= buf[y * row_size + x * 3 + 1];
                    buf[y * row_size + x * 3 + 2] -= buf[y * row_size + x * 3 + 1];
                } else if(bpp == 32) {
                    buf[y * row_size + x * 4 + 0] -= buf[y * row_size + x * 4 + 1];
                    buf[y * row_size + x * 4 + 2] -= buf[y * row_size + x * 4 + 1];
                }
            }
        }

        for(y = 0; y < trans_column; y++) { /* delta transform row */
            for(x = width - 1; x > 0; x--) {
                if(bpp == 24) {
                    buf[y * row_size + x * 3 + 0] -= buf[y * row_size + (x - 1) * 3 + 0];
                    buf[y * row_size + x * 3 + 1] -= buf[y * row_size + (x - 1) * 3 + 1];
                    buf[y * row_size + x * 3 + 2] -= buf[y * row_size + (x - 1) * 3 + 2];
                } else if(bpp == 32) {
                    buf[y * row_size + x * 4 + 0] -= buf[y * row_size + (x - 1) * 4 + 0];
                    buf[y * row_size + x * 4 + 1] -= buf[y * row_size + (x - 1) * 4 + 1];
                    buf[y * row_size + x * 4 + 2] -= buf[y * row_size + (x - 1) * 4 + 2];
                    buf[y * row_size + x * 4 + 3] -= buf[y * row_size + (x - 1) * 4 + 3];
                }
            }
        }
        for(y = trans_column - 1; y > 0; y--) { /* delta transform column */
            for(x = 0; x < width; x++) {
                if(bpp == 24) {
                    buf[y * row_size + x * 3 + 0] -= buf[(y - 1) * row_size + x * 3 + 0];
                    buf[y * row_size + x * 3 + 1] -= buf[(y - 1) * row_size + x * 3 + 1];
                    buf[y * row_size + x * 3 + 2] -= buf[(y - 1) * row_size + x * 3 + 2];
                } else if(bpp == 32) {
                    buf[y * row_size + x * 4 + 0] -= buf[(y - 1) * row_size + x * 4 + 0];
                    buf[y * row_size + x * 4 + 1] -= buf[(y - 1) * row_size + x * 4 + 1];
                    buf[y * row_size + x * 4 + 2] -= buf[(y - 1) * row_size + x * 4 + 2];
                    buf[y * row_size + x * 4 + 3] -= buf[(y - 1) * row_size + x * 4 + 3];
                }
            }
        }

    } else { /* decode */
        for(y = 0; y < trans_column; y++) { /* delta transform row */
            for(x = 1; x < width; x++) {
                if(bpp == 24) {
                    buf[y * row_size + x * 3 + 0] += buf[y * row_size + (x - 1) * 3 + 0];
                    buf[y * row_size + x * 3 + 1] += buf[y * row_size + (x - 1) * 3 + 1];
                    buf[y * row_size + x * 3 + 2] += buf[y * row_size + (x - 1) * 3 + 2];
                } else if(bpp == 32) {
                    buf[y * row_size + x * 4 + 0] += buf[y * row_size + (x - 1) * 4 + 0];
                    buf[y * row_size + x * 4 + 1] += buf[y * row_size + (x - 1) * 4 + 1];
                    buf[y * row_size + x * 4 + 2] += buf[y * row_size + (x - 1) * 4 + 2];
                    buf[y * row_size + x * 4 + 3] += buf[y * row_size + (x - 1) * 4 + 3];
                }
            }
        }
        for(y = 1; y < trans_column; y++) { /* delta transform column */
            for(x = 0; x < width; x++) {
                if(bpp == 24) {
                    buf[y * row_size + x * 3 + 0] += buf[(y - 1) * row_size + x * 3 + 0];
                    buf[y * row_size + x * 3 + 1] += buf[(y - 1) * row_size + x * 3 + 1];
                    buf[y * row_size + x * 3 + 2] += buf[(y - 1) * row_size + x * 3 + 2];
                } else if(bpp == 32) {
                    buf[y * row_size + x * 4 + 0] += buf[(y - 1) * row_size + x * 4 + 0];
                    buf[y * row_size + x * 4 + 1] += buf[(y - 1) * row_size + x * 4 + 1];
                    buf[y * row_size + x * 4 + 2] += buf[(y - 1) * row_size + x * 4 + 2];
                    buf[y * row_size + x * 4 + 3] += buf[(y - 1) * row_size + x * 4 + 3];
                }
            }
        }

        for(y = 0; y < trans_column; y++) { /* color transform */
            for(x = 0; x < width; x++) {
                if(bpp == 24) {
                    buf[y * row_size + x * 3 + 0] += buf[y * row_size + x * 3 + 1];
                    buf[y * row_size + x * 3 + 2] += buf[y * row_size + x * 3 + 1];
                } else if(bpp == 32) {
                    buf[y * row_size + x * 4 + 0] += buf[y * row_size + x * 4 + 1];
                    buf[y * row_size + x * 4 + 2] += buf[y * row_size + x * 4 + 1];
                }
            }
        }
    }
    return row_size * trans_column;
}

uint32_t bmp_transform(uint8_t* buf, uint32_t len, int en_de) {
    static int flag = 0;
    static int curr = 0;
    static int size = 0;
    static int row_size = 0;
    static int bpp = 0;
    static int width = 0;
    static int height = 0;
    static int skip_size = 0;

    bmp_header_t* header = (bmp_header_t*)buf;
    int trans_size;
    uint8_t* start = buf;

    if(!flag) {
        if(len < sizeof(*header) /* check bitmap header */
                || header->m_magic != BMP_MAGIC
                || header->m_planes != 1
                || header->m_compression != 0
                || (header->m_image_size != 0 && header->m_image_offset + header->m_image_size != header->m_file_size)
                || (header->m_bits_per_pixel != 24 && header->m_bits_per_pixel != 32)) {
            return 0;
        }
        width = abs(header->m_width);
        height = abs(header->m_height);
        row_size = (header->m_bits_per_pixel * width + 31) / 32 * 4;
        bpp = header->m_bits_per_pixel;

        if(width < 4 || height < 4 || width >= (1 << 20) || height >= (1 << 20)) {
            return 0;
        }

        start = buf + header->m_image_offset;
        curr = header->m_image_offset;
        size = height * row_size;
        skip_size = 0;
        flag = 1;
        return header->m_image_offset;
    }

    while(skip_size > 0) { /* skip broken rows */
        trans_size = min(skip_size, len);
        curr += trans_size;
        skip_size -= trans_size;
        return trans_size;
    }
    trans_size = bmp_transform_column_rgb_delta(start, min(len, size - curr), width, row_size, bpp, en_de);
    curr += trans_size;

    if(curr < size) { /* transform not done -- the last row is broken */
        skip_size = min(row_size, size - curr);
    } else {
        flag = 0;
    }
    return trans_size;
}
