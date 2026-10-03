/*****************************************************************************\
     Snes9x - Portable Super Nintendo Entertainment System (TM) emulator.
                This file is licensed under the Snes9x License.
   For further information, consult the LICENSE file in the root directory.
\*****************************************************************************/

#ifndef _CHEATS_H_
#define _CHEATS_H_

#include <stdint.h>

using bool8 = uint8_t;

struct SCheat
{
	uint32_t	address;
	uint8_t	byte;
	uint8_t	saved_byte;
	bool8	conditional;
	bool8	cond_true;
	uint8_t	cond_byte;
	bool8	enabled;
};

// CRTOS: no cheat groups (no C++ library; cheats are stubs, snes_stubs.cpp); the space of the
// std::vector stays, so the objects after Cheat keep their addresses
struct SCheatData
{
	uint32_t	groupPad[3];
	bool8	enabled;
	// CRTOS: no cheat search (cheats.cpp is left out): not its 890 KB of memory copies; the
	// watch display (off) keeps its array, one byte
	uint8_t	*RAM;
	uint8_t	*FillRAM;
	uint8_t	*SRAM;
	uint8_t	CWatchRAM[1];
};

struct Watch
{
	bool	on;
	int		size;
	int		format;
	uint32_t	address;
	char	buf[12];
	char	desc[32];
};

typedef enum
{
	S9X_LESS_THAN,
	S9X_GREATER_THAN,
	S9X_LESS_THAN_OR_EQUAL,
	S9X_GREATER_THAN_OR_EQUAL,
	S9X_EQUAL,
	S9X_NOT_EQUAL
}	S9xCheatComparisonType;

typedef enum
{
	S9X_8_BITS,
	S9X_16_BITS,
	S9X_24_BITS,
	S9X_32_BITS
}	S9xCheatDataSize;

extern SCheatData	Cheat;
extern Watch		watches[16];

void S9xDeleteCheats(void);
bool8 S9xLoadCheatFile(const char *filename);
void S9xUpdateCheatsInMemory(void);

void S9xInitCheatData (void);
void S9xInitWatchedAddress (void);
void S9xStartCheatSearch (SCheatData *);
void S9xSearchForChange (SCheatData *, S9xCheatComparisonType, S9xCheatDataSize, bool8, bool8);
void S9xSearchForValue (SCheatData *, S9xCheatComparisonType, S9xCheatDataSize, uint32_t, bool8, bool8);
void S9xSearchForAddress (SCheatData *, S9xCheatComparisonType, S9xCheatDataSize, uint32_t, bool8);
void S9xOutputCheatSearchResults (SCheatData *);

#endif
