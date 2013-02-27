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
#include <string.h>
#include <stdint.h>
#include "cr-matcher.h"

const char* cr_magic_header = "\x1f\x9d\x01\x01::0.11.0-comprop";
const char* cr_start_info = (
        "============================================\n"
        " comprop: an lzp-ari compressor             \n"
        " written by Zhang Li <richselian@gmail.com> \n"
        "============================================\n");
const char* cr_usage_info = (
        "to compress:   comprop [SWITCH] e [input] [output]\n"
        "to decompress: comprop          d [input] [output]\n"
        "work with standard I/O streams if filenames are not given.\n"
        "\n"
        "optional SWITCH:\n"
        "   -b  set block size(MB), default = 16.\n"
        "   -p  work as a precompressor.\n"
        "   -F  use PE/ELF/BMP filter.\n"
        "   -q  quiet mode.\n"
        "\n"
        "example:\n"
        "   comprop -m16 -b100 e enwik8 enwik8.rox\n"
        "   comprop            d enwik8.rox enwik8\n");

/* implement in src/main.c */
extern uint32_t cr_split_size;
extern int cr_filt_enable;
extern int cr_prec_enable;
extern int cr_main(int argc, char** argv);

int main(int argc, char** argv) {
    return cr_main(argc, argv);
}

int cr_process_arguments(int argc, char** argv) {
    while(argc >= 2 && argv[1][0] == '-') {
        switch(argv[1][1]) {
            case 'b': /* set block size */
                if((cr_split_size = atoi(argv[1] + 2) * 1048576) <= 0) {
                    goto BadSwitch;
                }
                break;

            case 'p': /* no LZ-stage */
                if(argv[1][2] != 0) {
                    goto BadSwitch;
                }
                cr_prec_enable = 1;
                break;

            case 'F': /* use PE/ELF/BMP filter */
                if(argv[1][2] != 0) {
                    goto BadSwitch;
                }
                cr_filt_enable = 1;
                break;

            case 'q': /* quiet mode */
                if(argv[1][2] != 0) {
                    goto BadSwitch;
                }
                fclose(stderr);
                break;

BadSwitch:  default:
                fprintf(stderr, "invalid switch '%s'.\n", argv[1]);
                return 0;
        }
        memmove(argv + 1, argv + 2, (argc - 2) * sizeof(char*));
        argc--;
    }
    return argc;
}
