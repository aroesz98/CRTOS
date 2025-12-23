// MEMSET implementation ARM optimized.
// This implementation breaks down set value into several blocks: 32, 8, 4, and 1 byte.
// Author: Arkadiusz Szlanta

.syntax unified
.arch   armv7-m

.text
.global     memset_optimized
.type       memset_optimized, %function
.align      4

memset_optimized:
    cbz     r2, stop                  // Nothing to copy.
    push    {r0, r3, r4}
    // Build 32-bit value with byte repeated 4 times
    // First, mask r1 to ensure we only have the byte value
    and     r1, r1, #0xFF             // Keep only lower 8 bits
    // Now build the pattern: byte in all 4 positions
    orr     r3, r1, r1, lsl #8        // r3 = 0x0000BABA
    orr     r3, r3, r3, lsl #16       // r3 = 0xBABABABA
    mov     r4, r3
    cmp     r2, #31
    bhi     set32
    cmp     r2, #7
    bhi     set8
    cmp     r2, #3
    bhi     set4
    b       set1

set32:
    strd    r3, r4, [r0], #8
    strd    r3, r4, [r0], #8
    strd    r3, r4, [r0], #8
    strd    r3, r4, [r0], #8
    subs    r2, r2, #32
    cmp     r2, #31
    bhi     set32
    cbz     r2, stop
    cmp     r2, #7
    bhi     set8
    cmp     r2, #3
    bhi     set4
    b       set1

set8:
    strd    r3, r4, [r0], #8
    subs    r2, r2, #8
    cmp     r2, #7
    bhi     set8
    cbz     r2, stop
    cmp     r2, #3
    bhi     set4
    b       set1

set4:
    str     r3, [r0], #4
    subs    r2, r2, #4
    cmp     r2, #3
    bhi     set4
    cbz     r2, stop
    b       set1

set1:
    strb    r3, [r0], #1
    subs    r2, r2, #1
    cbz     r2, stop
    b       set1

stop:
    pop     {r0, r3, r4}
    bx      lr
.size   memset_optimized, .-memset_optimized

