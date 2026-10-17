/*****************************************************************************\
     Snes9x - Portable Super Nintendo Entertainment System (TM) emulator.
                This file is licensed under the Snes9x License.
   For further information, consult the LICENSE file in the root directory.
\*****************************************************************************/

#define _TILEIMPL_CPP_
#include "tileimpl.h"

namespace TileImpl {

	template<class MATH, class BPSTART>
	void Normal1x1Base<MATH, BPSTART>::Draw(int N, int M, uint32 Offset, uint32 OffsetInLine, uint8 Pix, uint8 Z1, uint8 Z2)
	{
		(void) OffsetInLine;
		if (Z1 > GFX.DB[Offset + N] && (M))
		{
			GFX.S[Offset + N] = MATH::Calc(GFX.ScreenColors[Pix], GFX.SubScreen[Offset + N], GFX.SubZBuffer[Offset + N]);
			GFX.DB[Offset + N] = (depth8)Z2; // CRTOS: depth8
		}
	}

	// CRTOS: Draw() of pixels N..N+3, whose depths are all 0 (so the test passes)
	template<class MATH, class BPSTART>
	void Normal1x1Base<MATH, BPSTART>::DrawUncovered4(int N, uint32 Offset, uint8 Pix, uint8 Z2)
	{
		uint16	*s = GFX.S + Offset + N, *sub = GFX.SubScreen + Offset + N;
		depth8	*sd = GFX.SubZBuffer + Offset + N;
		uint16	c = GFX.ScreenColors[Pix];
		for (int i = 0; i < 4; i++)
			s[i] = MATH::Calc(c, sub[i], sd[i]);
		uint32	z = Z2 * 0x01010101u; // (one store: memset() of 4 bytes was a call)
		memcpy(GFX.DB + Offset + N, &z, 4);
	}


	// normal width
	template struct Renderers<DrawTile16, Normal1x1>;
	template struct Renderers<DrawClippedTile16, Normal1x1>;
	template struct Renderers<DrawMosaicPixel16, Normal1x1>;
	template struct Renderers<DrawBackdrop16, Normal1x1>;
	template struct Renderers<DrawMode7MosaicBG1, Normal1x1>;
	template struct Renderers<DrawMode7BG1, Normal1x1>;
	template struct Renderers<DrawMode7MosaicBG2, Normal1x1>;
	template struct Renderers<DrawMode7BG2, Normal1x1>;

} // namespace TileImpl
