/*
 * snes_stubs.cpp - what the Snes9x core refers to but this program leaves out, to keep only
 * the core's minimal set of files (core/):
 *
 *   - the cartridge coprocessors but DSP-1 and the Super FX: DSP-2..4, SA-1, C4, OBC1, SETA,
 *     SPC7110, S-DD1, S-RTC, BS-X, MSU-1. A game that needs one is not started (snes_core.cpp
 *     tells from the core's Settings after loading), so these are never called with a game
 *     running;
 *   - cheats, movies, save states of the sound processor, screenshots.
 */
#include "snes9x.h"
#include "memmap.h"
#include "apu/bapu/snes/snes.hpp"
#include "bsx.h"
#include "c4.h"
#include "cheats.h"
#include "dsp.h"
#include "fxemu.h"
#include "movie.h"
#include "msu1.h"
#include "obc1.h"
#include "sa1.h"
#include "screenshot.h"
#include "sdd1.h"
#include "sdd1emu.h"
#include "seta.h"
#include "sha256.h"
#include "snapshot.h"
#include "spc7110.h"
#include "srtc.h"

/* ---- coprocessors ------------------------------------------------------------------------ */

void DSP3_Reset(void) {}
uint8 DSP2GetByte(uint16) { return 0; }
void DSP2SetByte(uint8, uint16) {}
uint8 DSP3GetByte(uint16) { return 0; }
void DSP3SetByte(uint8, uint16) {}
uint8 DSP4GetByte(uint16) { return 0; }
void DSP4SetByte(uint8, uint16) {}

uint8 S9xGetST010(uint32) { return 0; }
void S9xSetST010(uint32, uint8) {}
uint8 S9xGetST011(uint32) { return 0; }
void S9xSetST011(uint32, uint8) {}
uint8 S9xGetST018(uint32) { return 0; }
void S9xSetST018(uint8, uint32) {}
uint8 S9xGetSetaDSP(uint32) { return 0; }
void S9xSetSetaDSP(uint8, uint32) {}
uint8 (*GetSETA)(uint32) = S9xGetSetaDSP;
void (*SetSETA)(uint32, uint8) = S9xSetST010;

void S9xSetOBC1(uint8, uint16) {}
uint8 S9xGetOBC1(uint16) { return 0; }
void S9xResetOBC1(void) {}
uint8 *S9xGetBasePointerOBC1(uint16) { return NULL; }
uint8 *S9xGetMemPointerOBC1(uint16) { return NULL; }

void S9xInitSRTC(void) {}
void S9xResetSRTC(void) {}
void S9xSetSRTC(uint8, uint16) {}
uint8 S9xGetSRTC(uint16) { return 0; }

uint8 S9xGetBSX(uint32) { return 0; }
void S9xSetBSX(uint8, uint32) {}
uint8 S9xGetBSXPPU(uint16) { return 0; }
void S9xSetBSXPPU(uint8, uint16) {}
uint8 *S9xGetBasePointerBSX(uint32) { return NULL; }
void S9xInitBSX(void) {}
void S9xResetBSX(void) {}

uint8 S9xGetSA1(uint32) { return 0; }
void S9xSetSA1(uint8, uint32) {}
void S9xSA1Init(void) {}
void S9xSA1MainLoop(void) {}

void S9xResetMSU(void) {}
void S9xMSU1Init(void) {}
void S9xMSU1DeInit(void) {}
bool S9xMSU1ROMExists(void) { return false; }
void S9xMSU1Generate(size_t) {}
uint8 S9xMSU1ReadPort(uint8) { return 0; }
void S9xMSU1WritePort(uint8, uint8) {}
void S9xMSU1SetOutput(Resampler *) {}

void S9xSetSDD1MemoryMap(uint32, uint32) {}
void S9xResetSDD1(void) {}
void SDD1_decompress(uint8 *, uint8 *, int) {}

void S9xInitSPC7110(void) {}
void S9xResetSPC7110(void) {}
void S9xSetSPC7110(uint8, uint16) {}
uint8 S9xGetSPC7110(uint16) { return 0; }
uint8 S9xGetSPC7110Byte(uint32) { return 0; }
uint8 *S9xGetBasePointerSPC7110(uint32) { return NULL; }

void S9xInitC4(void) {}
void S9xSetC4(uint8, uint16) {}
uint8 S9xGetC4(uint16) { return 0; }
uint8 *S9xGetBasePointerC4(uint16) { return NULL; }
uint8 *S9xGetMemPointerC4(uint16) { return NULL; }

/* ---- cheats, movies, snapshots, screenshots ---------------------------------------------- */

void S9xInitCheatData(void) {}
void S9xDeleteCheats(void) {}
bool8 S9xLoadCheatFile(const char *) { return FALSE; }
void S9xUpdateCheatsInMemory(void) {}

void S9xMovieStop(bool8) {}
void S9xMovieUpdate(bool) {}
bool8 S9xMovieActive(void) { return FALSE; }
uint16 MovieGetJoypad(int) { return 0; }
bool MovieGetMouse(int, uint8 *) { return false; }
bool MovieGetScope(int, uint8 *) { return false; }
bool MovieGetJustifier(int, uint8 *) { return false; }

void S9xResetSaveTimer(bool8) {}
bool8 S9xDoScreenshot(int, int) { return FALSE; }

/* the ROM's SHA-256 is only shown by front ends (and used by BS-X): not worked out */
void sha256sum(unsigned char *, unsigned int, unsigned char *hash)
{
    memset(hash, 0, 32);
}

namespace SNES
{
void SMP::load_state(uint8 **) {}
void SMP::save_state(uint8 **) {}
void SMP::save_spc(uint8 *) {}
} // namespace SNES


