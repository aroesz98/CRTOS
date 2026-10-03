/*****************************************************************************\
     Snes9x - Portable Super Nintendo Entertainment System (TM) emulator.
                This file is licensed under the Snes9x License.
   For further information, consult the LICENSE file in the root directory.
\*****************************************************************************/

#pragma once

#ifdef __cplusplus

enum s9x_getdirtype
{
	DEFAULT_DIR = 0,
	HOME_DIR,
	ROMFILENAME_DIR,
	ROM_DIR,
	SRAM_DIR,
	SNAPSHOT_DIR,
	SCREENSHOT_DIR,
	SPC_DIR,
	CHEAT_DIR,
	PATCH_DIR,
	BIOS_DIR,
	LOG_DIR,
	SAT_DIR,
	LAST_DIR
};

// CRTOS: file names as C strings (no C++ library). S9xGetFilename's result lives until its
// next call; the path helpers (SplitPath, makepath, S9xBasename) are left out.
#define S9X_PATH_MAX 256

const char *S9xGetFilename (const char *ext, enum s9x_getdirtype dirtype);
const char *S9xGetFilename (const char *filename, const char *ext, enum s9x_getdirtype dirtype);
const char *S9xGetDirectory (enum s9x_getdirtype);
const char *S9xGetFilenameInc (const char *, enum s9x_getdirtype);
#endif
