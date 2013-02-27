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
#include "cr-filter.h"
#include "filter_x86opcode.h"
#include "filter_bmp.h"

int filter_inplace(unsigned char* buf, uint32_t len, int en_de) {
    typedef uint32_t (*filter_subproc)(uint8_t* buf, uint32_t len, int en_de);

    filter_subproc subprocs[] = {
        pe_i386_transform,
        elf_i386_transform,
        bmp_transform,
    };
    static filter_subproc lastproc = 0;

    int filt = 0;
    int pos;
    int i;
    int filtsize;

    fprintf(stderr, "%s\n", "-> running filters...");

    for(pos = 0; pos < len; pos++) {
        if(lastproc) {
            filtsize = lastproc(buf + pos, len - pos, en_de); /* use last matched filter first */
            if(filtsize == 0) {
                lastproc = 0;
            } else {
                filt = 1;
                pos += filtsize - 1;
                continue;
            }
        }

        for(i = 0; i < sizeof(subprocs) / sizeof(*subprocs); i++) {
            filtsize = subprocs[i](buf + pos, len - pos, en_de);
            if(filtsize > 0) {
                filt = 1;
                lastproc = subprocs[i];
                pos += filtsize - 1;
                break;
            }
        }
    }
    return filt;
}
