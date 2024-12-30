// Memcpy implementation ARM optimized.
// This implementation breaks down copying into several blocks: 128, 32, 8, 4, and 1 byte.
// Author: Arkadiusz Szlanta

.syntax unified
.arch	armv7-m

.text
.global     memcpy_optimized
.type       memcpy_optimized, %function
.align      4

memcpy_optimized:
    cmp     r2, #0                  // Nothing to copy. Don't play with me.
    beq     stop

    push    {r1, r3, r4}
    cmp     r2, #127
	bhi		copy128
	cmp     r2, #31
	bhi		copy32
    cmp     r2, #7
	bhi		copy8
    cmp     r2, #3
    bhi     copy4
    bls     copybytes

copy128:
    ldrd    r3, r4, [r1]
    strd    r3, r4, [r0]
    ldrd    r3, r4, [r1, #8]
    strd    r3, r4, [r0, #8]
    ldrd    r3, r4, [r1, #16]
    strd    r3, r4, [r0, #16]
    ldrd    r3, r4, [r1, #24]
    strd    r3, r4, [r0, #24]
    ldrd    r3, r4, [r1, #32]
    strd    r3, r4, [r0, #32]
    ldrd    r3, r4, [r1, #40]
    strd    r3, r4, [r0, #40]
    ldrd    r3, r4, [r1, #48]
    strd    r3, r4, [r0, #48]
    ldrd    r3, r4, [r1, #56]
    strd    r3, r4, [r0, #56]
    ldrd    r3, r4, [r1, #64]
    strd    r3, r4, [r0, #64]
    ldrd    r3, r4, [r1, #72]
    strd    r3, r4, [r0, #72]
    ldrd    r3, r4, [r1, #80]
    strd    r3, r4, [r0, #80]
    ldrd    r3, r4, [r1, #88]
    strd    r3, r4, [r0, #88]
    ldrd    r3, r4, [r1, #96]
    strd    r3, r4, [r0, #96]
    ldrd    r3, r4, [r1, #104]
    strd    r3, r4, [r0, #104]
    ldrd    r3, r4, [r1, #112]
    strd    r3, r4, [r0, #112]
    ldrd    r3, r4, [r1, #120]
    strd    r3, r4, [r0, #120]
    add     r1, r1, #128
    add     r0, r0, #128
	subs    r2, r2, #128
    cmp     r2, #127
	bhi     copy128
    cmp     r2, #0
    beq     stop
	cmp     r2, #31
    bhi     copy32
    cmp     r2, #7
    bhi     copy8
    cmp     r2, #3
    bhi     copy4
    b       copybytes

copy32:
    ldr     r3, [r1], #4
	str     r3, [r0], #4
    ldr     r3, [r1], #4
	str     r3, [r0], #4
    ldr     r3, [r1], #4
	str     r3, [r0], #4
    ldr     r3, [r1], #4
	str     r3, [r0], #4
    ldr     r3, [r1], #4
	str     r3, [r0], #4
    ldr     r3, [r1], #4
	str     r3, [r0], #4
    ldr     r3, [r1], #4
	str     r3, [r0], #4
    ldr     r3, [r1], #4
	str     r3, [r0], #4
	subs    r2, r2, #32
    cmp     r2, #31
	bhi     copy32
    cmp     r2, #0
    beq     stop
    cmp     r2, #7
    bhi     copy8
    cmp     r2, #3
    bhi     copy4
    b       copybytes

copy8:
    ldr     r3, [r1], #4
	str     r3, [r0], #4
    ldr     r3, [r1], #4
	str     r3, [r0], #4
	subs    r2, r2, #8
    cmp     r2, #7
    bhi     copy8
    cmp     r2, #0
    beq     stop
    cmp     r2, #3
    bhi     copy4
    b       copybytes

copy4:
	ldr     r3, [r1], #4
	str     r3, [r0], #4
	subs    r2, r2, #4
    cmp     r2, #3
    bhi     copy4
    cmp     r2, #0
    beq     stop
	b       copybytes

copybytes:
    ldrb    r3, [r1], #1
	strb    r3, [r0], #1
	subs    r2, r2, #1
    cmp     r2, #0
	bne     copybytes
    b       stop

stop:
    pop     {r1, r3, r4}

    // Return address of destination buffer
    mov     r0, r1
    bx      lr

.size	memcpy_optimized, . - memcpy_optimized

