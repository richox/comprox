/*
 * Copyright (C) 2012 by Xie Mingjue <jeremyxie357 at gmail.com>
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

#define IMAGE_DOS_SIGNATURE 0x5A4D      // MZ
#define IMAGE_NT_SIGNATURE  0x00004550  // PE\0\0
#define DOS_LFA_NEW_OFFSET  0x3C

// Machine
#define IMAGE_FILE_MACHINE_I386     0x14c
// Flag
#define IMAGE_FILE_EXECUTABLE_IMAGE 0x0002

typedef struct {
    uint32_t    Signature;
    uint16_t    Machine;
    uint16_t    NumberOfSections;
    uint32_t    TimeDateStamp;
    uint32_t    PointerToSymbolTable;
    uint32_t    NumberOfSymbols;
    uint16_t    SizeOfOptionalHeader;
    uint16_t    Characteristics;
} CoffFileHeader;

typedef struct {
    char        Name[8];
    uint32_t    VirtualSize;
    uint32_t    VirtualAddress;
    uint32_t    SizeOfRawData;
    uint32_t    PointerToRawData;
    uint32_t    PointerToRelocations;
    uint32_t    PointerToLinenumbers;
    uint16_t    NumberOfRelocations;
    uint16_t    NumberOfLinenumbers;
    uint32_t    Characteristics;
} SectionHeader;

typedef struct {
    int         is_valid;
    uint32_t    size_est;
    uint32_t    size_hdr;
} analysis_result;

static inline uint32_t min(uint32_t lhs, uint32_t rhs) {
    return lhs < rhs ? lhs : rhs;
}

// `base' points to file header
static analysis_result pe_i386_analyze(uint8_t* buf, uint32_t hdr_off, uint32_t len) {
    static const int MaxImageSize = 1 << 28;
    uint32_t i;

    analysis_result result = { 0, 0, 0 };
    CoffFileHeader* header = (CoffFileHeader*) (buf + hdr_off);

    if(hdr_off + len < sizeof(CoffFileHeader))
        return result;

    if(header->Machine != IMAGE_FILE_MACHINE_I386 && header->Characteristics & IMAGE_FILE_EXECUTABLE_IMAGE)
        return result;

    uint32_t sec_num = header->NumberOfSections;
    uint32_t sec_tbl_off = sizeof(CoffFileHeader) + header->SizeOfOptionalHeader;

    result.size_hdr = sec_tbl_off + sec_num * sizeof(SectionHeader);
    result.size_est = result.size_hdr;

    if(hdr_off + len < result.size_hdr)
        return result;

    SectionHeader* sec_table = (SectionHeader*)(buf + hdr_off + sec_tbl_off);
    for(i = 0; i < sec_num; ++i)
        result.size_est += sec_table[i].SizeOfRawData;

    if(result.size_est > MaxImageSize)
        return result;

    result.is_valid = 1;
    return result;
}

static inline uint32_t pe_check_magic(uint8_t* buf, uint32_t len) {
    uint32_t hdr_off;

    if(len < DOS_LFA_NEW_OFFSET + 4)
        return 0;

    if(*(uint16_t*) buf != IMAGE_DOS_SIGNATURE)
        return 0;

    hdr_off = *(uint32_t*)(buf + DOS_LFA_NEW_OFFSET);

    if(hdr_off >= len)
        return 0;

    if(*(uint32_t*)(buf + hdr_off) != IMAGE_NT_SIGNATURE)
        return 0;

    return hdr_off;
}

uint32_t pe_i386_transform(uint8_t* buf, uint32_t len, int en_de) {
    static int      flag = 0;
    static uint32_t curr = 0;
    static uint32_t imsz = 0;   // [hdr_end, est_end]

    uint8_t*    start   = buf;
    uint32_t    size    = min(imsz - curr, len);
    uint32_t    ret     = size;

    if(!flag) {
        curr = 0;

        uint32_t hdr_off = pe_check_magic(buf, len);
        if (!hdr_off)
            return 0;

        analysis_result analysis = pe_i386_analyze(buf, hdr_off, len);
        if(!analysis.is_valid)
            return 0;

        start   = buf + analysis.size_hdr;
        imsz    = analysis.size_est - analysis.size_hdr;
        size    = min(imsz, len - analysis.size_hdr);
        ret     = size + analysis.size_hdr;
    }

    i386_e8e9(start, size, en_de, curr, imsz);
    curr += size;
    flag = curr < imsz;

    return ret;
}
