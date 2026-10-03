/*
 * snes_fxjit_ops.h - what the entries of Snes9x's fx_OpcodeTable (core/fxinst.cpp: ALT mode << 8 |
 * opcode) do, for the Super FX recompiler (snes_fxjit.cpp). Generated from the handlers' names by
 * a script: K_PLOT and K_RPIX are the entries fx_readRegisterSpace() points at GSU.pfPlot/pfRpix.
 */
#ifndef SNES_FXJIT_OPS_H
#define SNES_FXJIT_OPS_H

enum fx_kind {
    K_STOP,
    K_NOP,
    K_CACHE,
    K_LSR,
    K_ROL,
    K_BRANCH,
    K_TO,
    K_WITH,
    K_STW,
    K_STB,
    K_LOOP,
    K_ALT,
    K_LDW,
    K_LDB,
    K_PLOT,
    K_SWAP,
    K_NOT,
    K_ADD,
    K_ADDI,
    K_ADC,
    K_ADCI,
    K_SUB,
    K_SUBI,
    K_SBC,
    K_CMP,
    K_MERGE,
    K_AND,
    K_ANDI,
    K_BIC,
    K_BICI,
    K_OR,
    K_ORI,
    K_XOR,
    K_XORI,
    K_MULT,
    K_MULTI,
    K_UMULT,
    K_UMULTI,
    K_SBK,
    K_LINK,
    K_SEX,
    K_ASR,
    K_DIV2,
    K_ROR,
    K_JMP,
    K_LOB,
    K_IBT,
    K_LMS,
    K_SMS,
    K_FROM,
    K_HIB,
    K_INC,
    K_DEC,
    K_IWT,
    K_LM,
    K_SM,
    K_COLOR,
    K_CMODE,
    K_GETC,
    K_RAMB,
    K_ROMB,
    K_FMULT,
    K_LMULT,
    K_GETB,
    K_GETBH,
    K_GETBL,
    K_GETBS,
    K_LJMP,
    K_RPIX,
    K_COUNT
};

/* branch conditions (FX_BRANCH) */
enum { B_BRA, B_BGE, B_BLT, B_BNE, B_BEQ, B_BPL, B_BMI, B_BCC, B_BCS, B_BVC, B_BVS };

struct fx_op {
    uint8_t kind, arg;          /* arg: register, immediate, ALT mode or branch condition */
};

static const struct fx_op FX_OPS[1024] = {
    {K_STOP, 0}, {K_NOP, 0}, {K_CACHE, 0}, {K_LSR, 0}, /* 000: stop nop cache lsr */
    {K_ROL, 0}, {K_BRANCH, 0}, {K_BRANCH, 1}, {K_BRANCH, 2}, /* 004: rol bra bge blt */
    {K_BRANCH, 3}, {K_BRANCH, 4}, {K_BRANCH, 5}, {K_BRANCH, 6}, /* 008: bne beq bpl bmi */
    {K_BRANCH, 7}, {K_BRANCH, 8}, {K_BRANCH, 9}, {K_BRANCH, 10}, /* 00c: bcc bcs bvc bvs */
    {K_TO, 0}, {K_TO, 1}, {K_TO, 2}, {K_TO, 3}, /* 010: to_r0 to_r1 to_r2 to_r3 */
    {K_TO, 4}, {K_TO, 5}, {K_TO, 6}, {K_TO, 7}, /* 014: to_r4 to_r5 to_r6 to_r7 */
    {K_TO, 8}, {K_TO, 9}, {K_TO, 10}, {K_TO, 11}, /* 018: to_r8 to_r9 to_r10 to_r11 */
    {K_TO, 12}, {K_TO, 13}, {K_TO, 14}, {K_TO, 15}, /* 01c: to_r12 to_r13 to_r14 to_r15 */
    {K_WITH, 0}, {K_WITH, 1}, {K_WITH, 2}, {K_WITH, 3}, /* 020: with_r0 with_r1 with_r2 with_r3 */
    {K_WITH, 4}, {K_WITH, 5}, {K_WITH, 6}, {K_WITH, 7}, /* 024: with_r4 with_r5 with_r6 with_r7 */
    {K_WITH, 8}, {K_WITH, 9}, {K_WITH, 10}, {K_WITH, 11}, /* 028: with_r8 with_r9 with_r10 with_r11 */
    {K_WITH, 12}, {K_WITH, 13}, {K_WITH, 14}, {K_WITH, 15}, /* 02c: with_r12 with_r13 with_r14 with_r15 */
    {K_STW, 0}, {K_STW, 1}, {K_STW, 2}, {K_STW, 3}, /* 030: stw_r0 stw_r1 stw_r2 stw_r3 */
    {K_STW, 4}, {K_STW, 5}, {K_STW, 6}, {K_STW, 7}, /* 034: stw_r4 stw_r5 stw_r6 stw_r7 */
    {K_STW, 8}, {K_STW, 9}, {K_STW, 10}, {K_STW, 11}, /* 038: stw_r8 stw_r9 stw_r10 stw_r11 */
    {K_LOOP, 0}, {K_ALT, 1}, {K_ALT, 2}, {K_ALT, 3}, /* 03c: loop alt1 alt2 alt3 */
    {K_LDW, 0}, {K_LDW, 1}, {K_LDW, 2}, {K_LDW, 3}, /* 040: ldw_r0 ldw_r1 ldw_r2 ldw_r3 */
    {K_LDW, 4}, {K_LDW, 5}, {K_LDW, 6}, {K_LDW, 7}, /* 044: ldw_r4 ldw_r5 ldw_r6 ldw_r7 */
    {K_LDW, 8}, {K_LDW, 9}, {K_LDW, 10}, {K_LDW, 11}, /* 048: ldw_r8 ldw_r9 ldw_r10 ldw_r11 */
    {K_PLOT, 0}, {K_SWAP, 0}, {K_COLOR, 0}, {K_NOT, 0}, /* 04c: plot_2bit swap color not */
    {K_ADD, 0}, {K_ADD, 1}, {K_ADD, 2}, {K_ADD, 3}, /* 050: add_r0 add_r1 add_r2 add_r3 */
    {K_ADD, 4}, {K_ADD, 5}, {K_ADD, 6}, {K_ADD, 7}, /* 054: add_r4 add_r5 add_r6 add_r7 */
    {K_ADD, 8}, {K_ADD, 9}, {K_ADD, 10}, {K_ADD, 11}, /* 058: add_r8 add_r9 add_r10 add_r11 */
    {K_ADD, 12}, {K_ADD, 13}, {K_ADD, 14}, {K_ADD, 15}, /* 05c: add_r12 add_r13 add_r14 add_r15 */
    {K_SUB, 0}, {K_SUB, 1}, {K_SUB, 2}, {K_SUB, 3}, /* 060: sub_r0 sub_r1 sub_r2 sub_r3 */
    {K_SUB, 4}, {K_SUB, 5}, {K_SUB, 6}, {K_SUB, 7}, /* 064: sub_r4 sub_r5 sub_r6 sub_r7 */
    {K_SUB, 8}, {K_SUB, 9}, {K_SUB, 10}, {K_SUB, 11}, /* 068: sub_r8 sub_r9 sub_r10 sub_r11 */
    {K_SUB, 12}, {K_SUB, 13}, {K_SUB, 14}, {K_SUB, 15}, /* 06c: sub_r12 sub_r13 sub_r14 sub_r15 */
    {K_MERGE, 0}, {K_AND, 1}, {K_AND, 2}, {K_AND, 3}, /* 070: merge and_r1 and_r2 and_r3 */
    {K_AND, 4}, {K_AND, 5}, {K_AND, 6}, {K_AND, 7}, /* 074: and_r4 and_r5 and_r6 and_r7 */
    {K_AND, 8}, {K_AND, 9}, {K_AND, 10}, {K_AND, 11}, /* 078: and_r8 and_r9 and_r10 and_r11 */
    {K_AND, 12}, {K_AND, 13}, {K_AND, 14}, {K_AND, 15}, /* 07c: and_r12 and_r13 and_r14 and_r15 */
    {K_MULT, 0}, {K_MULT, 1}, {K_MULT, 2}, {K_MULT, 3}, /* 080: mult_r0 mult_r1 mult_r2 mult_r3 */
    {K_MULT, 4}, {K_MULT, 5}, {K_MULT, 6}, {K_MULT, 7}, /* 084: mult_r4 mult_r5 mult_r6 mult_r7 */
    {K_MULT, 8}, {K_MULT, 9}, {K_MULT, 10}, {K_MULT, 11}, /* 088: mult_r8 mult_r9 mult_r10 mult_r11 */
    {K_MULT, 12}, {K_MULT, 13}, {K_MULT, 14}, {K_MULT, 15}, /* 08c: mult_r12 mult_r13 mult_r14 mult_r15 */
    {K_SBK, 0}, {K_LINK, 1}, {K_LINK, 2}, {K_LINK, 3}, /* 090: sbk link_i1 link_i2 link_i3 */
    {K_LINK, 4}, {K_SEX, 0}, {K_ASR, 0}, {K_ROR, 0}, /* 094: link_i4 sex asr ror */
    {K_JMP, 8}, {K_JMP, 9}, {K_JMP, 10}, {K_JMP, 11}, /* 098: jmp_r8 jmp_r9 jmp_r10 jmp_r11 */
    {K_JMP, 12}, {K_JMP, 13}, {K_LOB, 0}, {K_FMULT, 0}, /* 09c: jmp_r12 jmp_r13 lob fmult */
    {K_IBT, 0}, {K_IBT, 1}, {K_IBT, 2}, {K_IBT, 3}, /* 0a0: ibt_r0 ibt_r1 ibt_r2 ibt_r3 */
    {K_IBT, 4}, {K_IBT, 5}, {K_IBT, 6}, {K_IBT, 7}, /* 0a4: ibt_r4 ibt_r5 ibt_r6 ibt_r7 */
    {K_IBT, 8}, {K_IBT, 9}, {K_IBT, 10}, {K_IBT, 11}, /* 0a8: ibt_r8 ibt_r9 ibt_r10 ibt_r11 */
    {K_IBT, 12}, {K_IBT, 13}, {K_IBT, 14}, {K_IBT, 15}, /* 0ac: ibt_r12 ibt_r13 ibt_r14 ibt_r15 */
    {K_FROM, 0}, {K_FROM, 1}, {K_FROM, 2}, {K_FROM, 3}, /* 0b0: from_r0 from_r1 from_r2 from_r3 */
    {K_FROM, 4}, {K_FROM, 5}, {K_FROM, 6}, {K_FROM, 7}, /* 0b4: from_r4 from_r5 from_r6 from_r7 */
    {K_FROM, 8}, {K_FROM, 9}, {K_FROM, 10}, {K_FROM, 11}, /* 0b8: from_r8 from_r9 from_r10 from_r11 */
    {K_FROM, 12}, {K_FROM, 13}, {K_FROM, 14}, {K_FROM, 15}, /* 0bc: from_r12 from_r13 from_r14 from_r15 */
    {K_HIB, 0}, {K_OR, 1}, {K_OR, 2}, {K_OR, 3}, /* 0c0: hib or_r1 or_r2 or_r3 */
    {K_OR, 4}, {K_OR, 5}, {K_OR, 6}, {K_OR, 7}, /* 0c4: or_r4 or_r5 or_r6 or_r7 */
    {K_OR, 8}, {K_OR, 9}, {K_OR, 10}, {K_OR, 11}, /* 0c8: or_r8 or_r9 or_r10 or_r11 */
    {K_OR, 12}, {K_OR, 13}, {K_OR, 14}, {K_OR, 15}, /* 0cc: or_r12 or_r13 or_r14 or_r15 */
    {K_INC, 0}, {K_INC, 1}, {K_INC, 2}, {K_INC, 3}, /* 0d0: inc_r0 inc_r1 inc_r2 inc_r3 */
    {K_INC, 4}, {K_INC, 5}, {K_INC, 6}, {K_INC, 7}, /* 0d4: inc_r4 inc_r5 inc_r6 inc_r7 */
    {K_INC, 8}, {K_INC, 9}, {K_INC, 10}, {K_INC, 11}, /* 0d8: inc_r8 inc_r9 inc_r10 inc_r11 */
    {K_INC, 12}, {K_INC, 13}, {K_INC, 14}, {K_GETC, 0}, /* 0dc: inc_r12 inc_r13 inc_r14 getc */
    {K_DEC, 0}, {K_DEC, 1}, {K_DEC, 2}, {K_DEC, 3}, /* 0e0: dec_r0 dec_r1 dec_r2 dec_r3 */
    {K_DEC, 4}, {K_DEC, 5}, {K_DEC, 6}, {K_DEC, 7}, /* 0e4: dec_r4 dec_r5 dec_r6 dec_r7 */
    {K_DEC, 8}, {K_DEC, 9}, {K_DEC, 10}, {K_DEC, 11}, /* 0e8: dec_r8 dec_r9 dec_r10 dec_r11 */
    {K_DEC, 12}, {K_DEC, 13}, {K_DEC, 14}, {K_GETB, 0}, /* 0ec: dec_r12 dec_r13 dec_r14 getb */
    {K_IWT, 0}, {K_IWT, 1}, {K_IWT, 2}, {K_IWT, 3}, /* 0f0: iwt_r0 iwt_r1 iwt_r2 iwt_r3 */
    {K_IWT, 4}, {K_IWT, 5}, {K_IWT, 6}, {K_IWT, 7}, /* 0f4: iwt_r4 iwt_r5 iwt_r6 iwt_r7 */
    {K_IWT, 8}, {K_IWT, 9}, {K_IWT, 10}, {K_IWT, 11}, /* 0f8: iwt_r8 iwt_r9 iwt_r10 iwt_r11 */
    {K_IWT, 12}, {K_IWT, 13}, {K_IWT, 14}, {K_IWT, 15}, /* 0fc: iwt_r12 iwt_r13 iwt_r14 iwt_r15 */
    {K_STOP, 0}, {K_NOP, 0}, {K_CACHE, 0}, {K_LSR, 0}, /* 100: stop nop cache lsr */
    {K_ROL, 0}, {K_BRANCH, 0}, {K_BRANCH, 1}, {K_BRANCH, 2}, /* 104: rol bra bge blt */
    {K_BRANCH, 3}, {K_BRANCH, 4}, {K_BRANCH, 5}, {K_BRANCH, 6}, /* 108: bne beq bpl bmi */
    {K_BRANCH, 7}, {K_BRANCH, 8}, {K_BRANCH, 9}, {K_BRANCH, 10}, /* 10c: bcc bcs bvc bvs */
    {K_TO, 0}, {K_TO, 1}, {K_TO, 2}, {K_TO, 3}, /* 110: to_r0 to_r1 to_r2 to_r3 */
    {K_TO, 4}, {K_TO, 5}, {K_TO, 6}, {K_TO, 7}, /* 114: to_r4 to_r5 to_r6 to_r7 */
    {K_TO, 8}, {K_TO, 9}, {K_TO, 10}, {K_TO, 11}, /* 118: to_r8 to_r9 to_r10 to_r11 */
    {K_TO, 12}, {K_TO, 13}, {K_TO, 14}, {K_TO, 15}, /* 11c: to_r12 to_r13 to_r14 to_r15 */
    {K_WITH, 0}, {K_WITH, 1}, {K_WITH, 2}, {K_WITH, 3}, /* 120: with_r0 with_r1 with_r2 with_r3 */
    {K_WITH, 4}, {K_WITH, 5}, {K_WITH, 6}, {K_WITH, 7}, /* 124: with_r4 with_r5 with_r6 with_r7 */
    {K_WITH, 8}, {K_WITH, 9}, {K_WITH, 10}, {K_WITH, 11}, /* 128: with_r8 with_r9 with_r10 with_r11 */
    {K_WITH, 12}, {K_WITH, 13}, {K_WITH, 14}, {K_WITH, 15}, /* 12c: with_r12 with_r13 with_r14 with_r15 */
    {K_STB, 0}, {K_STB, 1}, {K_STB, 2}, {K_STB, 3}, /* 130: stb_r0 stb_r1 stb_r2 stb_r3 */
    {K_STB, 4}, {K_STB, 5}, {K_STB, 6}, {K_STB, 7}, /* 134: stb_r4 stb_r5 stb_r6 stb_r7 */
    {K_STB, 8}, {K_STB, 9}, {K_STB, 10}, {K_STB, 11}, /* 138: stb_r8 stb_r9 stb_r10 stb_r11 */
    {K_LOOP, 0}, {K_ALT, 1}, {K_ALT, 2}, {K_ALT, 3}, /* 13c: loop alt1 alt2 alt3 */
    {K_LDB, 0}, {K_LDB, 1}, {K_LDB, 2}, {K_LDB, 3}, /* 140: ldb_r0 ldb_r1 ldb_r2 ldb_r3 */
    {K_LDB, 4}, {K_LDB, 5}, {K_LDB, 6}, {K_LDB, 7}, /* 144: ldb_r4 ldb_r5 ldb_r6 ldb_r7 */
    {K_LDB, 8}, {K_LDB, 9}, {K_LDB, 10}, {K_LDB, 11}, /* 148: ldb_r8 ldb_r9 ldb_r10 ldb_r11 */
    {K_RPIX, 0}, {K_SWAP, 0}, {K_CMODE, 0}, {K_NOT, 0}, /* 14c: rpix_2bit swap cmode not */
    {K_ADC, 0}, {K_ADC, 1}, {K_ADC, 2}, {K_ADC, 3}, /* 150: adc_r0 adc_r1 adc_r2 adc_r3 */
    {K_ADC, 4}, {K_ADC, 5}, {K_ADC, 6}, {K_ADC, 7}, /* 154: adc_r4 adc_r5 adc_r6 adc_r7 */
    {K_ADC, 8}, {K_ADC, 9}, {K_ADC, 10}, {K_ADC, 11}, /* 158: adc_r8 adc_r9 adc_r10 adc_r11 */
    {K_ADC, 12}, {K_ADC, 13}, {K_ADC, 14}, {K_ADC, 15}, /* 15c: adc_r12 adc_r13 adc_r14 adc_r15 */
    {K_SBC, 0}, {K_SBC, 1}, {K_SBC, 2}, {K_SBC, 3}, /* 160: sbc_r0 sbc_r1 sbc_r2 sbc_r3 */
    {K_SBC, 4}, {K_SBC, 5}, {K_SBC, 6}, {K_SBC, 7}, /* 164: sbc_r4 sbc_r5 sbc_r6 sbc_r7 */
    {K_SBC, 8}, {K_SBC, 9}, {K_SBC, 10}, {K_SBC, 11}, /* 168: sbc_r8 sbc_r9 sbc_r10 sbc_r11 */
    {K_SBC, 12}, {K_SBC, 13}, {K_SBC, 14}, {K_SBC, 15}, /* 16c: sbc_r12 sbc_r13 sbc_r14 sbc_r15 */
    {K_MERGE, 0}, {K_BIC, 1}, {K_BIC, 2}, {K_BIC, 3}, /* 170: merge bic_r1 bic_r2 bic_r3 */
    {K_BIC, 4}, {K_BIC, 5}, {K_BIC, 6}, {K_BIC, 7}, /* 174: bic_r4 bic_r5 bic_r6 bic_r7 */
    {K_BIC, 8}, {K_BIC, 9}, {K_BIC, 10}, {K_BIC, 11}, /* 178: bic_r8 bic_r9 bic_r10 bic_r11 */
    {K_BIC, 12}, {K_BIC, 13}, {K_BIC, 14}, {K_BIC, 15}, /* 17c: bic_r12 bic_r13 bic_r14 bic_r15 */
    {K_UMULT, 0}, {K_UMULT, 1}, {K_UMULT, 2}, {K_UMULT, 3}, /* 180: umult_r0 umult_r1 umult_r2 umult_r3 */
    {K_UMULT, 4}, {K_UMULT, 5}, {K_UMULT, 6}, {K_UMULT, 7}, /* 184: umult_r4 umult_r5 umult_r6 umult_r7 */
    {K_UMULT, 8}, {K_UMULT, 9}, {K_UMULT, 10}, {K_UMULT, 11}, /* 188: umult_r8 umult_r9 umult_r10 umult_r11 */
    {K_UMULT, 12}, {K_UMULT, 13}, {K_UMULT, 14}, {K_UMULT, 15}, /* 18c: umult_r12 umult_r13 umult_r14 umult_r15 */
    {K_SBK, 0}, {K_LINK, 1}, {K_LINK, 2}, {K_LINK, 3}, /* 190: sbk link_i1 link_i2 link_i3 */
    {K_LINK, 4}, {K_SEX, 0}, {K_DIV2, 0}, {K_ROR, 0}, /* 194: link_i4 sex div2 ror */
    {K_LJMP, 8}, {K_LJMP, 9}, {K_LJMP, 10}, {K_LJMP, 11}, /* 198: ljmp_r8 ljmp_r9 ljmp_r10 ljmp_r11 */
    {K_LJMP, 12}, {K_LJMP, 13}, {K_LOB, 0}, {K_LMULT, 0}, /* 19c: ljmp_r12 ljmp_r13 lob lmult */
    {K_LMS, 0}, {K_LMS, 1}, {K_LMS, 2}, {K_LMS, 3}, /* 1a0: lms_r0 lms_r1 lms_r2 lms_r3 */
    {K_LMS, 4}, {K_LMS, 5}, {K_LMS, 6}, {K_LMS, 7}, /* 1a4: lms_r4 lms_r5 lms_r6 lms_r7 */
    {K_LMS, 8}, {K_LMS, 9}, {K_LMS, 10}, {K_LMS, 11}, /* 1a8: lms_r8 lms_r9 lms_r10 lms_r11 */
    {K_LMS, 12}, {K_LMS, 13}, {K_LMS, 14}, {K_LMS, 15}, /* 1ac: lms_r12 lms_r13 lms_r14 lms_r15 */
    {K_FROM, 0}, {K_FROM, 1}, {K_FROM, 2}, {K_FROM, 3}, /* 1b0: from_r0 from_r1 from_r2 from_r3 */
    {K_FROM, 4}, {K_FROM, 5}, {K_FROM, 6}, {K_FROM, 7}, /* 1b4: from_r4 from_r5 from_r6 from_r7 */
    {K_FROM, 8}, {K_FROM, 9}, {K_FROM, 10}, {K_FROM, 11}, /* 1b8: from_r8 from_r9 from_r10 from_r11 */
    {K_FROM, 12}, {K_FROM, 13}, {K_FROM, 14}, {K_FROM, 15}, /* 1bc: from_r12 from_r13 from_r14 from_r15 */
    {K_HIB, 0}, {K_XOR, 1}, {K_XOR, 2}, {K_XOR, 3}, /* 1c0: hib xor_r1 xor_r2 xor_r3 */
    {K_XOR, 4}, {K_XOR, 5}, {K_XOR, 6}, {K_XOR, 7}, /* 1c4: xor_r4 xor_r5 xor_r6 xor_r7 */
    {K_XOR, 8}, {K_XOR, 9}, {K_XOR, 10}, {K_XOR, 11}, /* 1c8: xor_r8 xor_r9 xor_r10 xor_r11 */
    {K_XOR, 12}, {K_XOR, 13}, {K_XOR, 14}, {K_XOR, 15}, /* 1cc: xor_r12 xor_r13 xor_r14 xor_r15 */
    {K_INC, 0}, {K_INC, 1}, {K_INC, 2}, {K_INC, 3}, /* 1d0: inc_r0 inc_r1 inc_r2 inc_r3 */
    {K_INC, 4}, {K_INC, 5}, {K_INC, 6}, {K_INC, 7}, /* 1d4: inc_r4 inc_r5 inc_r6 inc_r7 */
    {K_INC, 8}, {K_INC, 9}, {K_INC, 10}, {K_INC, 11}, /* 1d8: inc_r8 inc_r9 inc_r10 inc_r11 */
    {K_INC, 12}, {K_INC, 13}, {K_INC, 14}, {K_GETC, 0}, /* 1dc: inc_r12 inc_r13 inc_r14 getc */
    {K_DEC, 0}, {K_DEC, 1}, {K_DEC, 2}, {K_DEC, 3}, /* 1e0: dec_r0 dec_r1 dec_r2 dec_r3 */
    {K_DEC, 4}, {K_DEC, 5}, {K_DEC, 6}, {K_DEC, 7}, /* 1e4: dec_r4 dec_r5 dec_r6 dec_r7 */
    {K_DEC, 8}, {K_DEC, 9}, {K_DEC, 10}, {K_DEC, 11}, /* 1e8: dec_r8 dec_r9 dec_r10 dec_r11 */
    {K_DEC, 12}, {K_DEC, 13}, {K_DEC, 14}, {K_GETBH, 0}, /* 1ec: dec_r12 dec_r13 dec_r14 getbh */
    {K_LM, 0}, {K_LM, 1}, {K_LM, 2}, {K_LM, 3}, /* 1f0: lm_r0 lm_r1 lm_r2 lm_r3 */
    {K_LM, 4}, {K_LM, 5}, {K_LM, 6}, {K_LM, 7}, /* 1f4: lm_r4 lm_r5 lm_r6 lm_r7 */
    {K_LM, 8}, {K_LM, 9}, {K_LM, 10}, {K_LM, 11}, /* 1f8: lm_r8 lm_r9 lm_r10 lm_r11 */
    {K_LM, 12}, {K_LM, 13}, {K_LM, 14}, {K_LM, 15}, /* 1fc: lm_r12 lm_r13 lm_r14 lm_r15 */
    {K_STOP, 0}, {K_NOP, 0}, {K_CACHE, 0}, {K_LSR, 0}, /* 200: stop nop cache lsr */
    {K_ROL, 0}, {K_BRANCH, 0}, {K_BRANCH, 1}, {K_BRANCH, 2}, /* 204: rol bra bge blt */
    {K_BRANCH, 3}, {K_BRANCH, 4}, {K_BRANCH, 5}, {K_BRANCH, 6}, /* 208: bne beq bpl bmi */
    {K_BRANCH, 7}, {K_BRANCH, 8}, {K_BRANCH, 9}, {K_BRANCH, 10}, /* 20c: bcc bcs bvc bvs */
    {K_TO, 0}, {K_TO, 1}, {K_TO, 2}, {K_TO, 3}, /* 210: to_r0 to_r1 to_r2 to_r3 */
    {K_TO, 4}, {K_TO, 5}, {K_TO, 6}, {K_TO, 7}, /* 214: to_r4 to_r5 to_r6 to_r7 */
    {K_TO, 8}, {K_TO, 9}, {K_TO, 10}, {K_TO, 11}, /* 218: to_r8 to_r9 to_r10 to_r11 */
    {K_TO, 12}, {K_TO, 13}, {K_TO, 14}, {K_TO, 15}, /* 21c: to_r12 to_r13 to_r14 to_r15 */
    {K_WITH, 0}, {K_WITH, 1}, {K_WITH, 2}, {K_WITH, 3}, /* 220: with_r0 with_r1 with_r2 with_r3 */
    {K_WITH, 4}, {K_WITH, 5}, {K_WITH, 6}, {K_WITH, 7}, /* 224: with_r4 with_r5 with_r6 with_r7 */
    {K_WITH, 8}, {K_WITH, 9}, {K_WITH, 10}, {K_WITH, 11}, /* 228: with_r8 with_r9 with_r10 with_r11 */
    {K_WITH, 12}, {K_WITH, 13}, {K_WITH, 14}, {K_WITH, 15}, /* 22c: with_r12 with_r13 with_r14 with_r15 */
    {K_STW, 0}, {K_STW, 1}, {K_STW, 2}, {K_STW, 3}, /* 230: stw_r0 stw_r1 stw_r2 stw_r3 */
    {K_STW, 4}, {K_STW, 5}, {K_STW, 6}, {K_STW, 7}, /* 234: stw_r4 stw_r5 stw_r6 stw_r7 */
    {K_STW, 8}, {K_STW, 9}, {K_STW, 10}, {K_STW, 11}, /* 238: stw_r8 stw_r9 stw_r10 stw_r11 */
    {K_LOOP, 0}, {K_ALT, 1}, {K_ALT, 2}, {K_ALT, 3}, /* 23c: loop alt1 alt2 alt3 */
    {K_LDW, 0}, {K_LDW, 1}, {K_LDW, 2}, {K_LDW, 3}, /* 240: ldw_r0 ldw_r1 ldw_r2 ldw_r3 */
    {K_LDW, 4}, {K_LDW, 5}, {K_LDW, 6}, {K_LDW, 7}, /* 244: ldw_r4 ldw_r5 ldw_r6 ldw_r7 */
    {K_LDW, 8}, {K_LDW, 9}, {K_LDW, 10}, {K_LDW, 11}, /* 248: ldw_r8 ldw_r9 ldw_r10 ldw_r11 */
    {K_PLOT, 0}, {K_SWAP, 0}, {K_COLOR, 0}, {K_NOT, 0}, /* 24c: plot_2bit swap color not */
    {K_ADDI, 0}, {K_ADDI, 1}, {K_ADDI, 2}, {K_ADDI, 3}, /* 250: add_i0 add_i1 add_i2 add_i3 */
    {K_ADDI, 4}, {K_ADDI, 5}, {K_ADDI, 6}, {K_ADDI, 7}, /* 254: add_i4 add_i5 add_i6 add_i7 */
    {K_ADDI, 8}, {K_ADDI, 9}, {K_ADDI, 10}, {K_ADDI, 11}, /* 258: add_i8 add_i9 add_i10 add_i11 */
    {K_ADDI, 12}, {K_ADDI, 13}, {K_ADDI, 14}, {K_ADDI, 15}, /* 25c: add_i12 add_i13 add_i14 add_i15 */
    {K_SUBI, 0}, {K_SUBI, 1}, {K_SUBI, 2}, {K_SUBI, 3}, /* 260: sub_i0 sub_i1 sub_i2 sub_i3 */
    {K_SUBI, 4}, {K_SUBI, 5}, {K_SUBI, 6}, {K_SUBI, 7}, /* 264: sub_i4 sub_i5 sub_i6 sub_i7 */
    {K_SUBI, 8}, {K_SUBI, 9}, {K_SUBI, 10}, {K_SUBI, 11}, /* 268: sub_i8 sub_i9 sub_i10 sub_i11 */
    {K_SUBI, 12}, {K_SUBI, 13}, {K_SUBI, 14}, {K_SUBI, 15}, /* 26c: sub_i12 sub_i13 sub_i14 sub_i15 */
    {K_MERGE, 0}, {K_ANDI, 1}, {K_ANDI, 2}, {K_ANDI, 3}, /* 270: merge and_i1 and_i2 and_i3 */
    {K_ANDI, 4}, {K_ANDI, 5}, {K_ANDI, 6}, {K_ANDI, 7}, /* 274: and_i4 and_i5 and_i6 and_i7 */
    {K_ANDI, 8}, {K_ANDI, 9}, {K_ANDI, 10}, {K_ANDI, 11}, /* 278: and_i8 and_i9 and_i10 and_i11 */
    {K_ANDI, 12}, {K_ANDI, 13}, {K_ANDI, 14}, {K_ANDI, 15}, /* 27c: and_i12 and_i13 and_i14 and_i15 */
    {K_MULTI, 0}, {K_MULTI, 1}, {K_MULTI, 2}, {K_MULTI, 3}, /* 280: mult_i0 mult_i1 mult_i2 mult_i3 */
    {K_MULTI, 4}, {K_MULTI, 5}, {K_MULTI, 6}, {K_MULTI, 7}, /* 284: mult_i4 mult_i5 mult_i6 mult_i7 */
    {K_MULTI, 8}, {K_MULTI, 9}, {K_MULTI, 10}, {K_MULTI, 11}, /* 288: mult_i8 mult_i9 mult_i10 mult_i11 */
    {K_MULTI, 12}, {K_MULTI, 13}, {K_MULTI, 14}, {K_MULTI, 15}, /* 28c: mult_i12 mult_i13 mult_i14 mult_i15 */
    {K_SBK, 0}, {K_LINK, 1}, {K_LINK, 2}, {K_LINK, 3}, /* 290: sbk link_i1 link_i2 link_i3 */
    {K_LINK, 4}, {K_SEX, 0}, {K_ASR, 0}, {K_ROR, 0}, /* 294: link_i4 sex asr ror */
    {K_JMP, 8}, {K_JMP, 9}, {K_JMP, 10}, {K_JMP, 11}, /* 298: jmp_r8 jmp_r9 jmp_r10 jmp_r11 */
    {K_JMP, 12}, {K_JMP, 13}, {K_LOB, 0}, {K_FMULT, 0}, /* 29c: jmp_r12 jmp_r13 lob fmult */
    {K_SMS, 0}, {K_SMS, 1}, {K_SMS, 2}, {K_SMS, 3}, /* 2a0: sms_r0 sms_r1 sms_r2 sms_r3 */
    {K_SMS, 4}, {K_SMS, 5}, {K_SMS, 6}, {K_SMS, 7}, /* 2a4: sms_r4 sms_r5 sms_r6 sms_r7 */
    {K_SMS, 8}, {K_SMS, 9}, {K_SMS, 10}, {K_SMS, 11}, /* 2a8: sms_r8 sms_r9 sms_r10 sms_r11 */
    {K_SMS, 12}, {K_SMS, 13}, {K_SMS, 14}, {K_SMS, 15}, /* 2ac: sms_r12 sms_r13 sms_r14 sms_r15 */
    {K_FROM, 0}, {K_FROM, 1}, {K_FROM, 2}, {K_FROM, 3}, /* 2b0: from_r0 from_r1 from_r2 from_r3 */
    {K_FROM, 4}, {K_FROM, 5}, {K_FROM, 6}, {K_FROM, 7}, /* 2b4: from_r4 from_r5 from_r6 from_r7 */
    {K_FROM, 8}, {K_FROM, 9}, {K_FROM, 10}, {K_FROM, 11}, /* 2b8: from_r8 from_r9 from_r10 from_r11 */
    {K_FROM, 12}, {K_FROM, 13}, {K_FROM, 14}, {K_FROM, 15}, /* 2bc: from_r12 from_r13 from_r14 from_r15 */
    {K_HIB, 0}, {K_ORI, 1}, {K_ORI, 2}, {K_ORI, 3}, /* 2c0: hib or_i1 or_i2 or_i3 */
    {K_ORI, 4}, {K_ORI, 5}, {K_ORI, 6}, {K_ORI, 7}, /* 2c4: or_i4 or_i5 or_i6 or_i7 */
    {K_ORI, 8}, {K_ORI, 9}, {K_ORI, 10}, {K_ORI, 11}, /* 2c8: or_i8 or_i9 or_i10 or_i11 */
    {K_ORI, 12}, {K_ORI, 13}, {K_ORI, 14}, {K_ORI, 15}, /* 2cc: or_i12 or_i13 or_i14 or_i15 */
    {K_INC, 0}, {K_INC, 1}, {K_INC, 2}, {K_INC, 3}, /* 2d0: inc_r0 inc_r1 inc_r2 inc_r3 */
    {K_INC, 4}, {K_INC, 5}, {K_INC, 6}, {K_INC, 7}, /* 2d4: inc_r4 inc_r5 inc_r6 inc_r7 */
    {K_INC, 8}, {K_INC, 9}, {K_INC, 10}, {K_INC, 11}, /* 2d8: inc_r8 inc_r9 inc_r10 inc_r11 */
    {K_INC, 12}, {K_INC, 13}, {K_INC, 14}, {K_RAMB, 0}, /* 2dc: inc_r12 inc_r13 inc_r14 ramb */
    {K_DEC, 0}, {K_DEC, 1}, {K_DEC, 2}, {K_DEC, 3}, /* 2e0: dec_r0 dec_r1 dec_r2 dec_r3 */
    {K_DEC, 4}, {K_DEC, 5}, {K_DEC, 6}, {K_DEC, 7}, /* 2e4: dec_r4 dec_r5 dec_r6 dec_r7 */
    {K_DEC, 8}, {K_DEC, 9}, {K_DEC, 10}, {K_DEC, 11}, /* 2e8: dec_r8 dec_r9 dec_r10 dec_r11 */
    {K_DEC, 12}, {K_DEC, 13}, {K_DEC, 14}, {K_GETBL, 0}, /* 2ec: dec_r12 dec_r13 dec_r14 getbl */
    {K_SM, 0}, {K_SM, 1}, {K_SM, 2}, {K_SM, 3}, /* 2f0: sm_r0 sm_r1 sm_r2 sm_r3 */
    {K_SM, 4}, {K_SM, 5}, {K_SM, 6}, {K_SM, 7}, /* 2f4: sm_r4 sm_r5 sm_r6 sm_r7 */
    {K_SM, 8}, {K_SM, 9}, {K_SM, 10}, {K_SM, 11}, /* 2f8: sm_r8 sm_r9 sm_r10 sm_r11 */
    {K_SM, 12}, {K_SM, 13}, {K_SM, 14}, {K_SM, 15}, /* 2fc: sm_r12 sm_r13 sm_r14 sm_r15 */
    {K_STOP, 0}, {K_NOP, 0}, {K_CACHE, 0}, {K_LSR, 0}, /* 300: stop nop cache lsr */
    {K_ROL, 0}, {K_BRANCH, 0}, {K_BRANCH, 1}, {K_BRANCH, 2}, /* 304: rol bra bge blt */
    {K_BRANCH, 3}, {K_BRANCH, 4}, {K_BRANCH, 5}, {K_BRANCH, 6}, /* 308: bne beq bpl bmi */
    {K_BRANCH, 7}, {K_BRANCH, 8}, {K_BRANCH, 9}, {K_BRANCH, 10}, /* 30c: bcc bcs bvc bvs */
    {K_TO, 0}, {K_TO, 1}, {K_TO, 2}, {K_TO, 3}, /* 310: to_r0 to_r1 to_r2 to_r3 */
    {K_TO, 4}, {K_TO, 5}, {K_TO, 6}, {K_TO, 7}, /* 314: to_r4 to_r5 to_r6 to_r7 */
    {K_TO, 8}, {K_TO, 9}, {K_TO, 10}, {K_TO, 11}, /* 318: to_r8 to_r9 to_r10 to_r11 */
    {K_TO, 12}, {K_TO, 13}, {K_TO, 14}, {K_TO, 15}, /* 31c: to_r12 to_r13 to_r14 to_r15 */
    {K_WITH, 0}, {K_WITH, 1}, {K_WITH, 2}, {K_WITH, 3}, /* 320: with_r0 with_r1 with_r2 with_r3 */
    {K_WITH, 4}, {K_WITH, 5}, {K_WITH, 6}, {K_WITH, 7}, /* 324: with_r4 with_r5 with_r6 with_r7 */
    {K_WITH, 8}, {K_WITH, 9}, {K_WITH, 10}, {K_WITH, 11}, /* 328: with_r8 with_r9 with_r10 with_r11 */
    {K_WITH, 12}, {K_WITH, 13}, {K_WITH, 14}, {K_WITH, 15}, /* 32c: with_r12 with_r13 with_r14 with_r15 */
    {K_STB, 0}, {K_STB, 1}, {K_STB, 2}, {K_STB, 3}, /* 330: stb_r0 stb_r1 stb_r2 stb_r3 */
    {K_STB, 4}, {K_STB, 5}, {K_STB, 6}, {K_STB, 7}, /* 334: stb_r4 stb_r5 stb_r6 stb_r7 */
    {K_STB, 8}, {K_STB, 9}, {K_STB, 10}, {K_STB, 11}, /* 338: stb_r8 stb_r9 stb_r10 stb_r11 */
    {K_LOOP, 0}, {K_ALT, 1}, {K_ALT, 2}, {K_ALT, 3}, /* 33c: loop alt1 alt2 alt3 */
    {K_LDB, 0}, {K_LDB, 1}, {K_LDB, 2}, {K_LDB, 3}, /* 340: ldb_r0 ldb_r1 ldb_r2 ldb_r3 */
    {K_LDB, 4}, {K_LDB, 5}, {K_LDB, 6}, {K_LDB, 7}, /* 344: ldb_r4 ldb_r5 ldb_r6 ldb_r7 */
    {K_LDB, 8}, {K_LDB, 9}, {K_LDB, 10}, {K_LDB, 11}, /* 348: ldb_r8 ldb_r9 ldb_r10 ldb_r11 */
    {K_RPIX, 0}, {K_SWAP, 0}, {K_CMODE, 0}, {K_NOT, 0}, /* 34c: rpix_2bit swap cmode not */
    {K_ADCI, 0}, {K_ADCI, 1}, {K_ADCI, 2}, {K_ADCI, 3}, /* 350: adc_i0 adc_i1 adc_i2 adc_i3 */
    {K_ADCI, 4}, {K_ADCI, 5}, {K_ADCI, 6}, {K_ADCI, 7}, /* 354: adc_i4 adc_i5 adc_i6 adc_i7 */
    {K_ADCI, 8}, {K_ADCI, 9}, {K_ADCI, 10}, {K_ADCI, 11}, /* 358: adc_i8 adc_i9 adc_i10 adc_i11 */
    {K_ADCI, 12}, {K_ADCI, 13}, {K_ADCI, 14}, {K_ADCI, 15}, /* 35c: adc_i12 adc_i13 adc_i14 adc_i15 */
    {K_CMP, 0}, {K_CMP, 1}, {K_CMP, 2}, {K_CMP, 3}, /* 360: cmp_r0 cmp_r1 cmp_r2 cmp_r3 */
    {K_CMP, 4}, {K_CMP, 5}, {K_CMP, 6}, {K_CMP, 7}, /* 364: cmp_r4 cmp_r5 cmp_r6 cmp_r7 */
    {K_CMP, 8}, {K_CMP, 9}, {K_CMP, 10}, {K_CMP, 11}, /* 368: cmp_r8 cmp_r9 cmp_r10 cmp_r11 */
    {K_CMP, 12}, {K_CMP, 13}, {K_CMP, 14}, {K_CMP, 15}, /* 36c: cmp_r12 cmp_r13 cmp_r14 cmp_r15 */
    {K_MERGE, 0}, {K_BICI, 1}, {K_BICI, 2}, {K_BICI, 3}, /* 370: merge bic_i1 bic_i2 bic_i3 */
    {K_BICI, 4}, {K_BICI, 5}, {K_BICI, 6}, {K_BICI, 7}, /* 374: bic_i4 bic_i5 bic_i6 bic_i7 */
    {K_BICI, 8}, {K_BICI, 9}, {K_BICI, 10}, {K_BICI, 11}, /* 378: bic_i8 bic_i9 bic_i10 bic_i11 */
    {K_BICI, 12}, {K_BICI, 13}, {K_BICI, 14}, {K_BICI, 15}, /* 37c: bic_i12 bic_i13 bic_i14 bic_i15 */
    {K_UMULTI, 0}, {K_UMULTI, 1}, {K_UMULTI, 2}, {K_UMULTI, 3}, /* 380: umult_i0 umult_i1 umult_i2 umult_i3 */
    {K_UMULTI, 4}, {K_UMULTI, 5}, {K_UMULTI, 6}, {K_UMULTI, 7}, /* 384: umult_i4 umult_i5 umult_i6 umult_i7 */
    {K_UMULTI, 8}, {K_UMULTI, 9}, {K_UMULTI, 10}, {K_UMULTI, 11}, /* 388: umult_i8 umult_i9 umult_i10 umult_i11 */
    {K_UMULTI, 12}, {K_UMULTI, 13}, {K_UMULTI, 14}, {K_UMULTI, 15}, /* 38c: umult_i12 umult_i13 umult_i14 umult_i15 */
    {K_SBK, 0}, {K_LINK, 1}, {K_LINK, 2}, {K_LINK, 3}, /* 390: sbk link_i1 link_i2 link_i3 */
    {K_LINK, 4}, {K_SEX, 0}, {K_DIV2, 0}, {K_ROR, 0}, /* 394: link_i4 sex div2 ror */
    {K_LJMP, 8}, {K_LJMP, 9}, {K_LJMP, 10}, {K_LJMP, 11}, /* 398: ljmp_r8 ljmp_r9 ljmp_r10 ljmp_r11 */
    {K_LJMP, 12}, {K_LJMP, 13}, {K_LOB, 0}, {K_LMULT, 0}, /* 39c: ljmp_r12 ljmp_r13 lob lmult */
    {K_LMS, 0}, {K_LMS, 1}, {K_LMS, 2}, {K_LMS, 3}, /* 3a0: lms_r0 lms_r1 lms_r2 lms_r3 */
    {K_LMS, 4}, {K_LMS, 5}, {K_LMS, 6}, {K_LMS, 7}, /* 3a4: lms_r4 lms_r5 lms_r6 lms_r7 */
    {K_LMS, 8}, {K_LMS, 9}, {K_LMS, 10}, {K_LMS, 11}, /* 3a8: lms_r8 lms_r9 lms_r10 lms_r11 */
    {K_LMS, 12}, {K_LMS, 13}, {K_LMS, 14}, {K_LMS, 15}, /* 3ac: lms_r12 lms_r13 lms_r14 lms_r15 */
    {K_FROM, 0}, {K_FROM, 1}, {K_FROM, 2}, {K_FROM, 3}, /* 3b0: from_r0 from_r1 from_r2 from_r3 */
    {K_FROM, 4}, {K_FROM, 5}, {K_FROM, 6}, {K_FROM, 7}, /* 3b4: from_r4 from_r5 from_r6 from_r7 */
    {K_FROM, 8}, {K_FROM, 9}, {K_FROM, 10}, {K_FROM, 11}, /* 3b8: from_r8 from_r9 from_r10 from_r11 */
    {K_FROM, 12}, {K_FROM, 13}, {K_FROM, 14}, {K_FROM, 15}, /* 3bc: from_r12 from_r13 from_r14 from_r15 */
    {K_HIB, 0}, {K_XORI, 1}, {K_XORI, 2}, {K_XORI, 3}, /* 3c0: hib xor_i1 xor_i2 xor_i3 */
    {K_XORI, 4}, {K_XORI, 5}, {K_XORI, 6}, {K_XORI, 7}, /* 3c4: xor_i4 xor_i5 xor_i6 xor_i7 */
    {K_XORI, 8}, {K_XORI, 9}, {K_XORI, 10}, {K_XORI, 11}, /* 3c8: xor_i8 xor_i9 xor_i10 xor_i11 */
    {K_XORI, 12}, {K_XORI, 13}, {K_XORI, 14}, {K_XORI, 15}, /* 3cc: xor_i12 xor_i13 xor_i14 xor_i15 */
    {K_INC, 0}, {K_INC, 1}, {K_INC, 2}, {K_INC, 3}, /* 3d0: inc_r0 inc_r1 inc_r2 inc_r3 */
    {K_INC, 4}, {K_INC, 5}, {K_INC, 6}, {K_INC, 7}, /* 3d4: inc_r4 inc_r5 inc_r6 inc_r7 */
    {K_INC, 8}, {K_INC, 9}, {K_INC, 10}, {K_INC, 11}, /* 3d8: inc_r8 inc_r9 inc_r10 inc_r11 */
    {K_INC, 12}, {K_INC, 13}, {K_INC, 14}, {K_ROMB, 0}, /* 3dc: inc_r12 inc_r13 inc_r14 romb */
    {K_DEC, 0}, {K_DEC, 1}, {K_DEC, 2}, {K_DEC, 3}, /* 3e0: dec_r0 dec_r1 dec_r2 dec_r3 */
    {K_DEC, 4}, {K_DEC, 5}, {K_DEC, 6}, {K_DEC, 7}, /* 3e4: dec_r4 dec_r5 dec_r6 dec_r7 */
    {K_DEC, 8}, {K_DEC, 9}, {K_DEC, 10}, {K_DEC, 11}, /* 3e8: dec_r8 dec_r9 dec_r10 dec_r11 */
    {K_DEC, 12}, {K_DEC, 13}, {K_DEC, 14}, {K_GETBS, 0}, /* 3ec: dec_r12 dec_r13 dec_r14 getbs */
    {K_LM, 0}, {K_LM, 1}, {K_LM, 2}, {K_LM, 3}, /* 3f0: lm_r0 lm_r1 lm_r2 lm_r3 */
    {K_LM, 4}, {K_LM, 5}, {K_LM, 6}, {K_LM, 7}, /* 3f4: lm_r4 lm_r5 lm_r6 lm_r7 */
    {K_LM, 8}, {K_LM, 9}, {K_LM, 10}, {K_LM, 11}, /* 3f8: lm_r8 lm_r9 lm_r10 lm_r11 */
    {K_LM, 12}, {K_LM, 13}, {K_LM, 14}, {K_LM, 15}, /* 3fc: lm_r12 lm_r13 lm_r14 lm_r15 */
};

#endif
