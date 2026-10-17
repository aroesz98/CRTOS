/*
 * snes_input.cpp - the controller ports of the SNES for the Snes9x core, in place of its
 * controls.cpp (3700 lines of mice, light guns, multitaps and key mapping): two standard
 * joypads whose buttons the program sets with snes_input_set().
 *
 * A joypad is a 16-bit shift register: latched by writing 1 then 0 to $4016, read one bit a
 * time from $4016 / $4017 (B, Y, Select, Start, up, down, left, right, A, X, L, R, then 4
 * zero bits, then ones), or all at once by the automatic read into $4218-$421F.
 */
#include "snes9x.h"
#include "memmap.h"
#include "controls.h"
#include "ppu.h"
#include "snes_core.h"

static uint16 s_buttons[2];     /* as the program last set them */
static uint16 s_latched[2];     /* as the console latched them */
static uint8 s_read_idx[2];     /* the next bit a serial read gives */
static bool8 s_latch;           /* $4016 bit 0 */

void snes_input_set(int port, uint16_t buttons)
{
    if (port >= 0 && port < 2)
        s_buttons[port] = buttons & 0xfff0;
}

void S9xSetJoypadLatch(bool latch)
{
    if (latch && !s_latch)
    {
        for (int n = 0; n < 2; n++)
        {
            s_read_idx[n] = 0;
            s_latched[n] = s_buttons[n];
        }
    }
    s_latch = latch;
}

uint8 S9xReadJOYSERn(int n)
{
    if (n > 1)
        n -= 0x4016;
    uint8 bits = (OpenBus & ~3) | ((n == 1) ? 0x1c : 0);
    if (s_latch)
        return bits | ((s_buttons[n] & 0x8000) ? 1 : 0);
    if (s_read_idx[n] >= 16)
    {
        if (s_read_idx[n] < 255)
            s_read_idx[n]++;
        return bits | 1; /* past the 16 bits: ones */
    }
    return bits | ((s_latched[n] & (0x8000 >> s_read_idx[n]++)) ? 1 : 0);
}

void S9xDoAutoJoypad(void)
{
    S9xSetJoypadLatch(1);
    S9xSetJoypadLatch(0);
    for (int n = 0; n < 2; n++)
    {
        s_read_idx[n] = 16;
        WRITE_WORD(Memory.FillRAM + 0x4218 + n * 2, s_latched[n]);
        WRITE_WORD(Memory.FillRAM + 0x421c + n * 2, 0);
    }
}

void S9xControlEOF(void)
{
    PPU.GunVLatch = 1000; /* (no light gun: never latch) */
    PPU.GunHLatch = 0;
}

void S9xControlsSoftReset(void)
{
    s_read_idx[0] = s_read_idx[1] = 0;
    s_latch = FALSE;
}

void S9xControlsReset(void)
{
    S9xControlsSoftReset();
}

bool S9xVerifyControllers(void)
{
    return false; /* (two joypads: nothing to change) */
}

void S9xGetController(int port, enum controllers *controller, int8 *id1, int8 *id2, int8 *id3, int8 *id4)
{
    *controller = (port == 0 || port == 1) ? CTL_JOYPAD : CTL_NONE;
    *id1 = (int8)((port == 0 || port == 1) ? port : -1);
    *id2 = *id3 = *id4 = -1;
}
