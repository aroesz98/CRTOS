// MEMFILL32 implementation ARM optimized for 32-bit XRGB8888 framebuffer fills.
// Fills memory with 32-bit value (4 bytes) using unrolled stores for throughput.
// Author: Arkadiusz Szlanta / Optimized for Window Manager

.syntax unified
.arch   armv7-m
.thumb

.text
.global     memfill32
.thumb_func
.type       memfill32, %function
.align      2

// void memfill32(uint32_t *dest, uint32_t value, size_t count)
// r0 = dest (pointer to uint32_t array)
// r1 = value (32-bit color value to fill)
// r2 = count (number of 32-bit words to fill)

memfill32:
    cmp     r2, #0
    beq     .Lexit                    // Nothing to fill

    push    {r4-r7, lr}
    
    // Copy value to r3-r7 for unrolled writes
    mov     r3, r1
    mov     r4, r1
    mov     r5, r1
    mov     r6, r1
    mov     r7, r1

    // Check if we have at least 8 words
    cmp     r2, #8
    blo     .Lsmall

.Lfill8:
    // Fill 8 words (32 bytes) at a time using str
    str     r1, [r0, #0]
    str     r3, [r0, #4]
    str     r4, [r0, #8]
    str     r5, [r0, #12]
    str     r6, [r0, #16]
    str     r7, [r0, #20]
    str     r1, [r0, #24]
    str     r3, [r0, #28]
    add     r0, r0, #32
    subs    r2, r2, #8
    cmp     r2, #8
    bhs     .Lfill8

.Lsmall:
    // Handle remaining 0-7 words
    cmp     r2, #0
    beq     .Ldone

.Lfill1:
    str     r1, [r0], #4
    subs    r2, r2, #1
    bne     .Lfill1

.Ldone:
    pop     {r4-r7, pc}

.Lexit:
    bx      lr

.size   memfill32, .-memfill32
