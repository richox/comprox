/*
 * Copyright (C) 2011-2012 by Wang Bo <viper.serv at gmail.com>
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
#include "filter_x86opcode.h"

typedef uint32_t      elf32_addr_t;
typedef uint16_t      elf32_half_t;
typedef uint32_t      elf32_off_t;
typedef int32_t       elf32_sword_t;
typedef uint32_t      elf32_word_t;
typedef unsigned char elf32_uchar_t;

#define EI_NIDENT     16

typedef struct elf32_header_s {
    elf32_uchar_t  e_ident[EI_NIDENT];
    elf32_half_t   e_type;
    elf32_half_t   e_machine;
    elf32_word_t   e_version;
    elf32_addr_t   e_entry;
    elf32_off_t    e_phoff;
    elf32_off_t    e_shoff;
    elf32_word_t   e_flags;
    elf32_half_t   e_ehsize;
    elf32_half_t   e_phentsize;
    elf32_half_t   e_phnum;
    elf32_half_t   e_shentsize;
    elf32_half_t   e_shnum;
    elf32_half_t   e_shstrndx;
} elf32_header_t;

#define EI_MAGIC   0x464C457F      /* 0x7F, "ELF" */
#define EM_I386    3

typedef struct elf32_section_header_s {
    elf32_word_t  sh_name;
    elf32_word_t  sh_type;
    elf32_word_t  sh_flags;
    elf32_addr_t  sh_addr;
    elf32_off_t   sh_offset;
    elf32_word_t  sh_size;
    elf32_word_t  sh_link;
    elf32_word_t  sh_info;
    elf32_word_t  sh_addralign;
    elf32_word_t  sh_entsize;
} elf32_section_header_t;

typedef struct elf32_program_header_s {
    elf32_word_t  p_type;
    elf32_off_t   p_offset;
    elf32_addr_t  p_vaddr;
    elf32_addr_t  p_paddr;
    elf32_word_t  p_filesz;
    elf32_word_t  p_memsz;
    elf32_word_t  p_flags;
    elf32_word_t  p_align;
} elf32_program_header_t;

#define PT_NULL     0
#define PT_LOAD     1
#define PT_DYNAMIC  2
#define PT_INTERP   3
#define PT_NOTE     4
#define PT_SHLIB    5
#define PT_PHDR     6

#define PF_X        (1<<0)
#define PF_W        (1<<1)
#define PF_R        (1<<2)

typedef struct elf32_analysis_s {
    int      ana_legal;
    uint32_t ana_ps_begin;
    uint32_t ana_ps_size_est;
} elf32_analysis_t;

static inline uint32_t min(uint32_t l, uint32_t r) {
    return l < r ? l : r;
}

elf32_analysis_t elf32_i386_analyze(uint8_t *off, uint32_t limit) {
    elf32_header_t *header = (elf32_header_t *) off;
    elf32_analysis_t analysis = { 1, 0, 0 };

    if (limit < sizeof(elf32_header_t)) {
        analysis.ana_legal = 0;
        return analysis;
    }
    if (*(uint32_t*)header != EI_MAGIC || header->e_machine != EM_I386) {
        analysis.ana_legal = 0;
        return analysis;
    }

    analysis.ana_ps_begin = sizeof(elf32_header_t);
    analysis.ana_ps_size_est = header->e_shoff - sizeof(elf32_header_t);

    if (header->e_shoff < sizeof(elf32_header_t))
        analysis.ana_legal = 0;

    if (analysis.ana_ps_size_est >= (1<<30))
        analysis.ana_legal = 0;

    return analysis;
}

uint32_t elf_i386_transform(uint8_t *buf, uint32_t len, int en_de) {
    static int      flag  = 0;
    static uint32_t curr  = 0;
    static uint32_t imsz  = 0;

    uint32_t size = min(imsz - curr, len);
    uint32_t ret = size;
    uint8_t* start = buf;

    if (!flag) {
        elf32_analysis_t analysis = elf32_i386_analyze(buf, len);
        if (!analysis.ana_legal) {
            return 0;
        }
        imsz = analysis.ana_ps_size_est - analysis.ana_ps_begin;
        start = buf + analysis.ana_ps_begin;
        size = min(imsz, len);
        ret = size;
    }

    i386_e8e9(start, size, en_de, curr, imsz);
    curr += size;
    flag = (curr < imsz);

    return ret;
}
