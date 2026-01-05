/*
 * TFTLIB_8BIT.cpp
 *
 *  Created on: Jan 10, 2022
 *  Updated on: Feb 20, 2022
 *      Author: asz
 */

#include "TFTLIB_8BIT.hpp"
using namespace std;

extern "C" void delay(uint32_t);
extern "C" uint32_t GetTick(void);

/* Optimized ARM assembly memory functions */
extern "C" void *memset_optimized(void *dest, int value, size_t count);
extern "C" void *memcpy_optimized(void *dest, const void *src, size_t count);
extern "C" void memfill32(uint32_t *dest, uint32_t value, size_t count);

extern char *itoa(int num, char *str, int base);

/***************************************************************************************
** Function name:           TFTLIB_8BIT
** Description:             TFTLIB_8BIT Constructor
***************************************************************************************/
TFTLIB_8BIT::TFTLIB_8BIT() : palette()
{
}

/***************************************************************************************
** Function name:           ~TFTLIB_8BIT
** Description:             TFTLIB_8BIT Destructor
***************************************************************************************/
TFTLIB_8BIT::~TFTLIB_8BIT()
{
}

/***************************************************************************************
** Function name:           setFramebuffer
** Description:             Set framebuffer pointer for direct pixel writes (32-bit XRGB8888)
***************************************************************************************/
void TFTLIB_8BIT::setFramebuffer(uint32_t *fb, int32_t width, int32_t height)
{
	_framebuffer = fb;
	_display_width = width;
	_display_height = height;
	_width = width;
	_height = height;
	_useFramebuffer = (fb != nullptr);
}

/***************************************************************************************
** Function name:           setWindow
** Description:             Set display window for drawing operations (stub in framebuffer mode)
***************************************************************************************/
void TFTLIB_8BIT::setWindow(uint_fast32_t x0, uint_fast32_t y0, uint_fast32_t x1, uint_fast32_t y1)
{
#ifdef TFTLIB_FRAMEBUFFER_ONLY
	(void)x0; (void)y0; (void)x1; (void)y1;
	// In framebuffer mode, no hardware window setup needed
#else
	// Hardware window setup would go here
	writeCommand8(CASET);
	writeSmallData32((x0 << 16) | x1);
	writeCommand8(RASET);
	writeSmallData32((y0 << 16) | y1);
	writeCommand8(RAMWR);
#endif
}

/***************************************************************************************
** Function name:           readWindow
** Description:             Set display window for reading (stub in framebuffer mode)
***************************************************************************************/
void TFTLIB_8BIT::readWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
#ifdef TFTLIB_FRAMEBUFFER_ONLY
	(void)x0; (void)y0; (void)x1; (void)y1;
	// In framebuffer mode, no hardware window setup needed
#else
	// Hardware read window setup would go here
	writeCommand8(CASET);
	writeSmallData32((x0 << 16) | x1);
	writeCommand8(RASET);
	writeSmallData32((y0 << 16) | y1);
	writeCommand8(RAMRD);
#endif
}

/***************************************************************************************
** Function name:           pushPixels8
** Description:             Push 8-bit pixels (stub in framebuffer mode)
***************************************************************************************/
void TFTLIB_8BIT::pushPixels8(const void* data_in, uint32_t len)
{
	(void)data_in; (void)len;
	// Stub - not used in framebuffer mode
}

/***************************************************************************************
** Function name:           pushPixels16
** Description:             Push 16-bit pixels (stub in framebuffer mode)
***************************************************************************************/
void TFTLIB_8BIT::pushPixels16(const void* data_in, uint32_t len)
{
	(void)data_in; (void)len;
	// Stub - not used in framebuffer mode
}

#ifndef TFTLIB_FRAMEBUFFER_ONLY
inline __attribute__((always_inline)) void TFTLIB_8BIT::write8(uint_fast32_t data)
{
	data <<= 16;

	WR_PORT->DR_CLEAR = (1 << WR_PIN);
	asm("NOP");
	asm("NOP");
	asm("NOP");
	asm("NOP");
	_PARALLEL_PORT->DR = (data & 0x00FF0000);
	asm("NOP");
	asm("NOP");
	asm("NOP");
	asm("NOP");
	asm("NOP");
	asm("NOP");
	asm("NOP");
	asm("NOP");
	WR_PORT->DR_SET = (1 << WR_PIN);
	asm("NOP");
	asm("NOP");
	asm("NOP");
	asm("NOP");
	asm("NOP");
	asm("NOP");
	asm("NOP");
	asm("NOP");
}
inline __attribute__((always_inline)) void TFTLIB_8BIT::write16(uint_fast32_t data)
{
	data <<= 16;

	WR_PORT->DR_CLEAR = 1 << WR_PIN;
	asm("NOP");
	asm("NOP");
	asm("NOP");
	asm("NOP");
	_PARALLEL_PORT->DR = ((data & 0xFF000000) >> 8);
	asm("NOP");
	asm("NOP");
	asm("NOP");
	asm("NOP");
	asm("NOP");
	asm("NOP");
	asm("NOP");
	asm("NOP");
	WR_PORT->DR_SET = 1 << WR_PIN;
	asm("NOP");
	asm("NOP");
	asm("NOP");
	asm("NOP");
	asm("NOP");
	asm("NOP");
	asm("NOP");
	asm("NOP");

	WR_PORT->DR_CLEAR = 1 << WR_PIN;
	asm("NOP");
	asm("NOP");
	asm("NOP");
	asm("NOP");
	_PARALLEL_PORT->DR = (data & 0x00FF0000);
	asm("NOP");
	asm("NOP");
	asm("NOP");
	asm("NOP");
	asm("NOP");
	asm("NOP");
	asm("NOP");
	asm("NOP");
	WR_PORT->DR_SET = 1 << WR_PIN;
	asm("NOP");
	asm("NOP");
	asm("NOP");
	asm("NOP");
	asm("NOP");
	asm("NOP");
	asm("NOP");
	asm("NOP");
}
#else
// Framebuffer-only mode - stub out hardware functions
inline void TFTLIB_8BIT::write8(uint_fast32_t) { }
inline void TFTLIB_8BIT::write16(uint_fast32_t) { }
#endif

int16_t TFTLIB_8BIT::width(void)
{
	return _width;
	;
}

int16_t TFTLIB_8BIT::height(void)
{
	return _height;
}

#ifndef TFTLIB_FRAMEBUFFER_ONLY
uint16_t TFTLIB_8BIT::readID(void)
{
	uint8_t data1, data2;
	uint16_t id = 0xFFFF;

	switch (_type)
	{
	case NT35510_PARALLEL:
		writeCommand16(0xDB00);
		// PIN_INPUT(_PARALLEL_PORT, 0x00FF);
		DC_H();
		CS_L();
		readByte();
		data1 = readByte();
		CS_H();
		// PIN_OUTPUT(_PARALLEL_PORT, 0x00FF);

		writeCommand16(0xDC00);
		// PIN_INPUT(_PARALLEL_PORT, 0x00FF);
		DC_H();
		CS_L();
		readByte();
		data2 = readByte();
		CS_H();
		// PIN_OUTPUT(_PARALLEL_PORT, 0x00FF);

		id = data1 << 8 | data2;
		break;

	case ILI9327_PARALLEL:
		writeCommand8(0xEF);

		// PIN_INPUT(_PARALLEL_PORT, 0x00FF);

		CS_L();
		DC_H();
		readByte();
		readByte();
		readByte();
		data1 = readByte();
		data2 = readByte();
		CS_H();
		// PIN_OUTPUT(_PARALLEL_PORT, 0x00FF);

		id = data1 << 8 | data2;
		break;

	case ILI9341_PARALLEL:
		writeCommand8(0xDB);

		// PIN_INPUT(_PARALLEL_PORT, 0x00FF);
		DC_H();
		CS_L();
		data1 = readByte();
		CS_H();
		// PIN_OUTPUT(_PARALLEL_PORT, 0x00FF);

		writeCommand8(0xDC);

		// PIN_INPUT(_PARALLEL_PORT, 0x00FF);
		DC_H();
		CS_L();
		data2 = readByte();
		CS_H();
		// PIN_OUTPUT(_PARALLEL_PORT, 0x00FF);

		id = data1 << 8 | data2;
		break;
	}

	return id;
}

/***************************************************************************************
** Function name:           init
** Description:             Init display with selected driver
***************************************************************************************/
void TFTLIB_8BIT::init(void)
{
	_display_width = 272;
	_display_height = 480;

	_tx0 = _tx1 = _ty0 = _ty1 = 0xFFFFFFFF;

	if (_type == NT35510_PARALLEL)
	{
		setFreeFont(&SegoeScript16pt7b);
	}
	else
	{
		setFreeFont(&SegoeScript8pt7b);
	}

	setRotation(3);
	fillScreen(palette.BLACK);
	setCursor(0, 0);

	textfont = 1;
	textsize = 1;
	textcolor = palette.WHITE;	 // White
	textbgcolor = palette.BLACK; // Black
	padX = 0;					 // No padding
	isDigits = false;			 // No bounding box adjustment
	textwrapX = true;			 // Wrap text at end of line when using print stream
	textwrapY = true;			 // Wrap text at bottom of screen when using print stream
	textdatum = TL_DATUM;		 // Top Left text alignment is default
	_utf8 = true;
}

/***************************************************************************************
** Function name:           readByte
** Description:             Read 1 byte of data from display;
***************************************************************************************/
uint8_t TFTLIB_8BIT::readByte(void)
{
	uint32_t b = 0;

	RD_STROBE();

	//	b = (_PARALLEL_PORT->IDR & 0x000000FF);
	//	b = (_PARALLEL_PORT->IDR & 0x000000FF);
	//	b = (_PARALLEL_PORT->IDR & 0x000000FF);
	//	b = (_PARALLEL_PORT->IDR & 0x000000FF);

	RD_IDLE();

	return (b & 0xFF);
}

/***************************************************************************************
** Function name:           writeCommand8
** Description:             Writing command to parallel display
***************************************************************************************/
void TFTLIB_8BIT::writeCommand8(uint8_t cmmd)
{
	CS_L();
	DC_L();

	write8(cmmd);

	CS_H();
}

void TFTLIB_8BIT::writeCommand16(uint16_t cmmd)
{
	CS_L();
	DC_L();

	write16(cmmd);

	CS_H();
}

/***************************************************************************************
** Function name:           writeData8
** Description:             Write 8bit data to parallel display
***************************************************************************************/
void TFTLIB_8BIT::writeData8(uint8_t *data, uint32_t len)
{
	CS_L();
	DC_H();

	while (len > 31)
	{
		write8(*data++);
		write8(*data++);
		write8(*data++);
		write8(*data++);
		write8(*data++);
		write8(*data++);
		write8(*data++);
		write8(*data++);
		write8(*data++);
		write8(*data++);
		write8(*data++);
		write8(*data++);
		write8(*data++);
		write8(*data++);
		write8(*data++);
		write8(*data++);
		write8(*data++);
		write8(*data++);
		write8(*data++);
		write8(*data++);
		write8(*data++);
		write8(*data++);
		write8(*data++);
		write8(*data++);
		write8(*data++);
		write8(*data++);
		write8(*data++);
		write8(*data++);
		write8(*data++);
		write8(*data++);
		write8(*data++);
		write8(*data++);
		len -= 32;
	}

	while (len > 7)
	{
		write8(*data++);
		write8(*data++);
		write8(*data++);
		write8(*data++);
		write8(*data++);
		write8(*data++);
		write8(*data++);
		write8(*data++);
		len -= 8;
	}

	while (len-- > 0)
		write8(*data++);

	CS_H();
}
uint32_t SWAP(uint32_t i)
{
	return (((i >> 24) & 0xFF) | ((i << 8) & 0xFF0000) | ((i >> 8) & 0xFF00) | ((i << 24) & 0xFF000000));
}

/***************************************************************************************
** Function name:           writeData16
** Description:             Write 16bit data to parallel display
***************************************************************************************/
void TFTLIB_8BIT::writeData16(uint16_t *data, uint32_t len)
{
	DC_H();
	CS_L();

	while (len > 31)
	{
		write16(*data++);
		write16(*data++);
		write16(*data++);
		write16(*data++);
		write16(*data++);
		write16(*data++);
		write16(*data++);
		write16(*data++);
		write16(*data++);
		write16(*data++);
		write16(*data++);
		write16(*data++);
		write16(*data++);
		write16(*data++);
		write16(*data++);
		write16(*data++);
		write16(*data++);
		write16(*data++);
		write16(*data++);
		write16(*data++);
		write16(*data++);
		write16(*data++);
		write16(*data++);
		write16(*data++);
		write16(*data++);
		write16(*data++);
		write16(*data++);
		write16(*data++);
		write16(*data++);
		write16(*data++);
		write16(*data++);
		write16(*data++);
		len -= 32;
	}

	while (len > 7)
	{
		write16(*data++);
		write16(*data++);
		write16(*data++);
		write16(*data++);
		write16(*data++);
		write16(*data++);
		write16(*data++);
		write16(*data++);
		len -= 8;
	}

	while (len > 0)
	{
		write16(*data++);
		len--;
	}

	CS_H();
}

#else
// Framebuffer-only mode - stub out hardware-specific functions
uint16_t TFTLIB_8BIT::readID(void) { return 0; }
void TFTLIB_8BIT::init(void) 
{
    // Initialize text rendering settings for framebuffer mode
    // Use TomThumb - tiny 5x6 pixel font, perfect for compact UI
    setFreeFont(&TomThumb);
    
    textfont = 1;
    textsize = 1;
    textcolor = 0x00FFFFFF;   // White
    textbgcolor = 0x00FFFFFF; // Same as fg = transparent
    padX = 0;
    isDigits = false;
    textwrapX = true;
    textwrapY = true;
    textdatum = TL_DATUM;
    _utf8 = true;
}
uint8_t TFTLIB_8BIT::readByte(void) { return 0; }
void TFTLIB_8BIT::writeCommand8(uint8_t) { }
void TFTLIB_8BIT::writeCommand16(uint16_t) { }
void TFTLIB_8BIT::writeData8(uint8_t*, uint32_t) { }
void TFTLIB_8BIT::writeData16(uint16_t*, uint32_t) { }
#endif // TFTLIB_FRAMEBUFFER_ONLY for readID/init/readByte/writeCommand/writeData

void TFTLIB_8BIT::drawSmoothRoundRect(int32_t x, int32_t y, int32_t r, int32_t ir, int32_t w, int32_t h, uint32_t fg_color, uint32_t bg_color, uint8_t quadrants)
{
	if (r < ir)
		swap_coord(r, ir); // Required that r > ir
	if (r <= 0 || ir < 0)
		return; // Invalid

	w -= 2 * r;
	h -= 2 * r;

	if (w < 0)
		w = 0;
	if (h < 0)
		h = 0;

	x += r;
	y += r;

	uint16_t t = r - ir + 1;
	int32_t xs = 0;
	int32_t cx = 0;

	int32_t r2 = r * r; // Outer arc radius^2
	r++;
	int32_t r1 = r * r; // Outer AA zone radius^2

	int32_t r3 = ir * ir; // Inner arc radius^2
	ir--;
	int32_t r4 = ir * ir; // Inner AA zone radius^2

	// float irf = ir;
	// float rf  = r;
	uint8_t alpha = 0;

	// Scan top left quadrant x y r ir fg_color  bg_color
	for (int32_t cy = r - 1; cy > 0; cy--)
	{
		int32_t len = 0;  // Pixel run length
		int32_t lxst = 0; // Left side run x start
		int32_t rxst = 0; // Right side run x start
		int32_t dy2 = (r - cy) * (r - cy);

		// Find and track arc zone start point
		while ((r - xs) * (r - xs) + dy2 >= r1)
			xs++;

		for (cx = xs; cx < r; cx++)
		{
			// Calculate radius^2
			int32_t hyp = (r - cx) * (r - cx) + dy2;

			// If in outer zone calculate alpha
			if (hyp > r2)
			{
				alpha = ~sqrt_fraction(hyp);
				// alpha = (uint8_t)((rf - sqrtf(hyp)) * 255); // Outer AA zone
			}
			// If within arc fill zone, get line lengths for each quadrant
			else if (hyp >= r3)
			{
				rxst = cx; // Right side start
				len++;	   // Line segment length
				continue;  // Next x
			}
			else
			{
				if (hyp <= r4)
					break; // Skip inner pixels
				// alpha = (uint8_t)((sqrtf(hyp) - irf) * 255); // Inner AA zone
				alpha = sqrt_fraction(hyp);
			}

			if (alpha < 16)
				continue; // Skip low alpha pixels

			// If background is read it must be done in each quadrant
			uint16_t pcol = alphaBlend(alpha, fg_color, bg_color);
			if (quadrants & 0x8)
				drawPixel(x + cx - r, y - cy + r + h, pcol); // BL
			if (quadrants & 0x1)
				drawPixel(x + cx - r, y + cy - r, pcol); // TL
			if (quadrants & 0x2)
				drawPixel(x - cx + r + w, y + cy - r, pcol); // TR
			if (quadrants & 0x4)
				drawPixel(x - cx + r + w, y - cy + r + h, pcol); // BR
		}
		// Fill arc inner zone in each quadrant
		lxst = rxst - len + 1; // Calculate line segment start for left side
		if (quadrants & 0x8)
			drawFastHLine(x + lxst - r, y - cy + r + h, len, fg_color); // BL
		if (quadrants & 0x1)
			drawFastHLine(x + lxst - r, y + cy - r, len, fg_color); // TL
		if (quadrants & 0x2)
			drawFastHLine(x - rxst + r + w, y + cy - r, len, fg_color); // TR
		if (quadrants & 0x4)
			drawFastHLine(x - rxst + r + w, y - cy + r + h, len, fg_color); // BR
	}

	// Draw sides
	if ((quadrants & 0xC) == 0xC)
		fillRect(x, y + r - t + h, w + 1, t, fg_color); // Bottom
	if ((quadrants & 0x9) == 0x9)
		fillRect(x - r + 1, y, t, h + 1, fg_color); // Left
	if ((quadrants & 0x3) == 0x3)
		fillRect(x, y - r + 1, w + 1, t, fg_color); // Top
	if ((quadrants & 0x6) == 0x6)
		fillRect(x + r - t + w, y, t, h + 1, fg_color); // Right
}

void TFTLIB_8BIT::drawSmoothCircle(int32_t x, int32_t y, int32_t r, uint32_t fg_color, uint32_t bg_color)
{
	drawSmoothRoundRect(x - r, y - r, r, r - 1, 0, 0, fg_color, bg_color);
}

#ifndef TFTLIB_FRAMEBUFFER_ONLY
/***************************************************************************************
** Function name:           writeSmallData8
** Description:             Write 8bit data via GPIO Port
***************************************************************************************/
inline void TFTLIB_8BIT::writeSmallData8(uint8_t data)
{
	DC_H();
	CS_L();

	write8(data);

	CS_H();
}

/***************************************************************************************
** Function name:           writeSmallData16
** Description:             Write 16bit data via GPIO Port
***************************************************************************************/
inline void TFTLIB_8BIT::writeSmallData16(uint16_t data)
{
	DC_H();
	CS_L();

	write16(data);

	CS_H();
}

/***************************************************************************************
** Function name:           writeSmallData32
** Description:             Write 32bit data via GPIO Port
***************************************************************************************/
inline void TFTLIB_8BIT::writeSmallData32(uint32_t data)
{
	DC_H();
	CS_L();

	write8(data >> 24);
	write8(data >> 16);
	write8(data >> 8);
	write8(data);

	CS_H();
}

#else
// Framebuffer-only mode - stub out writeSmallData functions
inline void TFTLIB_8BIT::writeSmallData8(uint8_t) { }
inline void TFTLIB_8BIT::writeSmallData16(uint16_t) { }
inline void TFTLIB_8BIT::writeSmallData32(uint32_t) { }
#endif // TFTLIB_FRAMEBUFFER_ONLY for writeSmallData

/***************************************************************************************
** Function name:           setRotation
** Description:             Set the rotation direction of the display
***************************************************************************************/
void TFTLIB_8BIT::setRotation(uint8_t m)
{
	m = m % 4;
	_rotation = m;

#ifdef TFTLIB_FRAMEBUFFER_ONLY
	// In framebuffer mode, just set dimensions based on rotation
	switch (m)
	{
	case 0:
	case 2:
		_width = _display_width;
		_height = _display_height;
		break;
	case 1:
	case 3:
		_width = _display_height;
		_height = _display_width;
		break;
	}
#else
	if (_type == ILI9327_PARALLEL)
	{
		writeCommand8(MADCTL);
		switch (m)
		{
		case 0:
			writeSmallData8(MADCTL_MX | MADCTL_BGR);
			_width = _display_width;
			_height = _display_height;
			break;
		case 1:
			writeSmallData8(MADCTL_MV | MADCTL_BGR);
			_width = _display_height;
			_height = _display_width;
			break;
		case 2:
			writeSmallData8(MADCTL_MY | MADCTL_BGR);
			_width = _display_width;
			_height = _display_height;
			break;
		case 3:
			writeSmallData8(MADCTL_MX | MADCTL_MY | MADCTL_MV | MADCTL_BGR);
			_width = _display_height;
			_height = _display_width;
			break;
		default:
			break;
		}
	}

	else if (_type == ILI9341_PARALLEL)
	{
		writeCommand8(MADCTL);
		switch (m)
		{
		case 0:
			writeSmallData8(MADCTL_MX | MADCTL_BGR);
			_width = _display_width;
			_height = _display_height;
			break;
		case 1:
			writeSmallData8(MADCTL_MV | MADCTL_BGR);
			_width = _display_height;
			_height = _display_width;
			break;
		case 2:
			writeSmallData8(MADCTL_MY | MADCTL_BGR);
			_width = _display_width;
			_height = _display_height;
			break;
		case 3:
			writeSmallData8(MADCTL_MX | MADCTL_MY | MADCTL_MV | MADCTL_BGR);
			_width = _display_height;
			_height = _display_width;
			break;
		default:
			break;
		}
	}

	else if (_type == NT35510_PARALLEL)
	{
		writeCommand16(0x3600);
		switch (m)
		{
		case 0:
			writeSmallData16(MADCTL_RGB);
			_width = _display_width;
			_height = _display_height;
			break;

		case 1:
			writeSmallData16(MADCTL_MX | MADCTL_MV | MADCTL_RGB);
			_width = _display_height;
			_height = _display_width;
			break;

		case 2:
			writeSmallData16(MADCTL_MX | MADCTL_MY | MADCTL_RGB);
			_width = _display_width;
			_height = _display_height;
			break;

		case 3:
			writeSmallData16(MADCTL_MV | MADCTL_MY | MADCTL_RGB);
			_width = _display_height;
			_height = _display_width;
			break;

		default:
			break;
		}
	}
#endif // TFTLIB_FRAMEBUFFER_ONLY
}

/***************************************************************************************
** Function name:           color565
** Description:             Convert value RGB888 to RGB565
***************************************************************************************/
uint32_t TFTLIB_8BIT::color565(uint8_t r, uint8_t g, uint8_t b)
{
	uint32_t color = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
	return color;
}

/***************************************************************************************
** Function name:           color16to8
** Description:             Convert 16bit palette to 8bit palette
***************************************************************************************/
uint32_t TFTLIB_8BIT::color16to8(uint16_t c)
{
	return ((c & 0xE000) >> 8) | ((c & 0x0700) >> 6) | ((c & 0x0018) >> 3);
}

uint32_t TFTLIB_8BIT::color16to24(uint16_t color565)
{
	uint8_t r = (color565 >> 8) & 0xF8;
	r |= (r >> 5);
	uint8_t g = (color565 >> 3) & 0xFC;
	g |= (g >> 6);
	uint8_t b = (color565 << 3) & 0xF8;
	b |= (b >> 5);

	return ((uint32_t)r << 16) | ((uint32_t)g << 8) | ((uint32_t)b << 0);
}

/***************************************************************************************
** Function name:           color8to16
** Description:             Convert 8bit palette to 16bit palette
***************************************************************************************/
uint32_t TFTLIB_8BIT::color8to16(uint8_t color)
{
	uint8_t blue[] = {0, 11, 21, 31}; // blue 2 to 5 bit colour lookup table
	uint32_t color16 = 0;

	//        =====Green=====     ===============Red==============
	color16 = (color & 0x1C) << 6 | (color & 0xC0) << 5 | (color & 0xE0) << 8;
	//        =====Green=====    =======Blue======
	color16 |= (color & 0x1C) << 3 | blue[color & 0x03];

	return color16;
}

/***************************************************************************************
** Function name:           alphaBlend
** Description:             Mix fgc & bgc with selected alpha channel(255 = full bgc)
***************************************************************************************/
// uint16_t TFTLIB_8BIT::alphaBlend(uint8_t alpha, uint16_t fgc, uint16_t bgc)
//{
//	uint16_t fgR = ((fgc >> 10) & 0x3E) + 1;
//	uint16_t fgG = ((fgc >>  4) & 0x7E) + 1;
//	uint16_t fgB = ((fgc <<  1) & 0x3E) + 1;
//
//	uint16_t bgR = ((bgc >> 10) & 0x3E) + 1;
//	uint16_t bgG = ((bgc >>  4) & 0x7E) + 1;
//	uint16_t bgB = ((bgc <<  1) & 0x3E) + 1;
//
//	uint16_t r = (((fgR * alpha) + (bgR * (255 - alpha))) >> 9);
//	uint16_t g = (((fgG * alpha) + (bgG * (255 - alpha))) >> 9);
//	uint16_t b = (((fgB * alpha) + (bgB * (255 - alpha))) >> 9);
//
//	return (r << 11) | (g << 5) | (b << 0);
// }

uint32_t TFTLIB_8BIT::alphaBlend(uint8_t alpha, uint32_t fgc, uint32_t bgc)
{
	uint16_t fgR = ((fgc >> 15) & 0x1FE) + 1;
	uint16_t fgG = ((fgc >> 7) & 0x1FE) + 1;
	uint16_t fgB = ((fgc << 1) & 0x1FE) + 1;

	uint16_t bgR = ((bgc >> 15) & 0x1FE) + 1;
	uint16_t bgG = ((bgc >> 7) & 0x1FE) + 1;
	uint16_t bgB = ((bgc << 1) & 0x1FE) + 1;

	uint16_t r = (((fgR * alpha) + (bgR * (255 - alpha))) >> 9);
	uint16_t g = (((fgG * alpha) + (bgG * (255 - alpha))) >> 9);
	uint16_t b = (((fgB * alpha) + (bgB * (255 - alpha))) >> 9);

	return (r << 16) | (g << 8) | (b << 0);
}

// pushPixels8 and pushPixels16 are defined earlier in the file as stubs for framebuffer mode

#ifndef TFTLIB_FRAMEBUFFER_ONLY
/***************************************************************************************
** Function name:           pushBlock24
** Description:             Push block of data
***************************************************************************************/
inline void TFTLIB_8BIT::pushBlock(uint_fast32_t color, uint_fast32_t len = 1)
{
	DC_H();
	CS_L();

	while (len > 31)
	{
		write16(color);
		write16(color);
		write16(color);
		write16(color);
		write16(color);
		write16(color);
		write16(color);
		write16(color);
		write16(color);
		write16(color);
		write16(color);
		write16(color);
		write16(color);
		write16(color);
		write16(color);
		write16(color);
		write16(color);
		write16(color);
		write16(color);
		write16(color);
		write16(color);
		write16(color);
		write16(color);
		write16(color);
		write16(color);
		write16(color);
		write16(color);
		write16(color);
		write16(color);
		write16(color);
		write16(color);
		write16(color);
		len -= 32;
	}

	while (len > 7)
	{
		write16(color);
		write16(color);
		write16(color);
		write16(color);
		write16(color);
		write16(color);
		write16(color);
		write16(color);
		len -= 8;
	}

	while (len > 0)
	{
		write16(color);
		len--;
	}

	CS_H();
}
#else
// Framebuffer-only mode - stub
inline void TFTLIB_8BIT::pushBlock(uint_fast32_t color, uint_fast32_t len = 1) { (void)color; (void)len; }
#endif

/***************************************************************************************
** Function name:           fillScreen
** Description:             Fast color fillig screen function
***************************************************************************************/
void TFTLIB_8BIT::fillScreen(uint32_t color)
{
	if (_useFramebuffer && _framebuffer) {
		int32_t len = _width * _height;
		uint32_t *dst = _framebuffer;
		
		// Unrolled 32x loop for maximum performance
		while (len > 31) {
			dst[0] = color; dst[1] = color; dst[2] = color; dst[3] = color;
			dst[4] = color; dst[5] = color; dst[6] = color; dst[7] = color;
			dst[8] = color; dst[9] = color; dst[10] = color; dst[11] = color;
			dst[12] = color; dst[13] = color; dst[14] = color; dst[15] = color;
			dst[16] = color; dst[17] = color; dst[18] = color; dst[19] = color;
			dst[20] = color; dst[21] = color; dst[22] = color; dst[23] = color;
			dst[24] = color; dst[25] = color; dst[26] = color; dst[27] = color;
			dst[28] = color; dst[29] = color; dst[30] = color; dst[31] = color;
			dst += 32;
			len -= 32;
		}
		
		// Unrolled 8x loop
		while (len > 7) {
			dst[0] = color; dst[1] = color; dst[2] = color; dst[3] = color;
			dst[4] = color; dst[5] = color; dst[6] = color; dst[7] = color;
			dst += 8;
			len -= 8;
		}
		
		// Remaining pixels
		while (len > 0) {
			*dst++ = color;
			len--;
		}
	} else {
		setWindow(0, 0, _width - 1, _height - 1);
		pushBlock(color, _width * _height);
	}
}

/***************************************************************************************
** Function name:           readPixel (PARALLEL DISPLAY ONLY!)
** Description:             Read single pixel at coords x&y
***************************************************************************************/
uint32_t TFTLIB_8BIT::readPixel(int32_t x0, int32_t y0)
{
	if (_useFramebuffer && _framebuffer) {
		if (x0 < 0 || x0 >= _width || y0 < 0 || y0 >= _height)
			return 0;
		return _framebuffer[y0 * _width + x0];
	}

#ifndef TFTLIB_FRAMEBUFFER_ONLY
	uint8_t data[3];

	readWindow(x0, y0, x0, y0);

	DC_H();
	CS_L();

	// Set masked pins D0- D7 to input
	// PIN_INPUT(_PARALLEL_PORT, 0x00FF);

	readByte();
	data[0] = readByte();
	data[1] = readByte();
	data[2] = readByte();

	CS_H();

	// Set masked pins D0- D7 to output
	// PIN_OUTPUT(_PARALLEL_PORT, 0x00FF);

	return (((data[0] & 0xF8) << 8) | ((data[1] & 0xFC) << 3) | (data[2] >> 3));
#else
	return 0;
#endif
}

/***************************************************************************************
** Function name:           drawPixel
** Description:             Draw single pixel at coords x&y
***************************************************************************************/
void TFTLIB_8BIT::drawPixel(int32_t x, int32_t y, uint32_t color)
{
	if (x < 0)
		x = 0;
	if (y < 0)
		y = 0;
	if (x >= _width)
		x = _width - 1;
	if (y >= _height)
		y = _height - 1;

	if (_useFramebuffer && _framebuffer) {
		_framebuffer[y * _width + x] = color;
	} else {
		setWindow(x, y, x, y);
		pushBlock(color, 1);
	}
}

/***************************************************************************************
** Function name:           drawPixel (alpha blended)
** Description:             Draw a pixel blended with the screen or bg pixel colour
***************************************************************************************/
uint16_t TFTLIB_8BIT::drawPixel(int32_t x, int32_t y, uint32_t color, uint8_t alpha, uint32_t bg_color)
{
	if (bg_color == 0x00FFFFFF)
		bg_color = readPixel(x, y);
	color = alphaBlend(alpha, color, bg_color);
	drawPixel(x, y, color);
	return color;
}

/***************************************************************************************
** Function name:           drawPixelAlpha
** Description:             Draw single pixel with alpha channel (optimized)
***************************************************************************************/
void TFTLIB_8BIT::drawPixelAlpha(int16_t x, int16_t y, uint32_t color, uint8_t alpha)
{
	if (x < 0 || x >= _width || y < 0 || y >= _height)
		return;

	/* Fast path for framebuffer mode */
	if (_useFramebuffer && _framebuffer) {
		uint32_t *dst = &_framebuffer[y * _width + x];
		uint32_t bgc = *dst;
		
		uint32_t fgR = (color >> 16) & 0xFF;
		uint32_t fgG = (color >> 8) & 0xFF;
		uint32_t fgB = color & 0xFF;
		uint32_t bgR = (bgc >> 16) & 0xFF;
		uint32_t bgG = (bgc >> 8) & 0xFF;
		uint32_t bgB = bgc & 0xFF;
		uint32_t invAlpha = 255 - alpha;
		
		uint32_t r = (fgR * alpha + bgR * invAlpha) >> 8;
		uint32_t g = (fgG * alpha + bgG * invAlpha) >> 8;
		uint32_t b = (fgB * alpha + bgB * invAlpha) >> 8;
		
		*dst = 0xFF000000 | (r << 16) | (g << 8) | b;
	} else {
		uint16_t px = readPixel(x, y);
		drawPixel(x, y, color, alphaBlend(alpha, color, px));
	}
}

/***************************************************************************************
** Function name:           drawPixelAlpha
** Description:             Draw filled rectangle with vertical colour gradient
***************************************************************************************/
void TFTLIB_8BIT::fillRectVGradient(int16_t x, int16_t y, int16_t w, int16_t h, uint32_t color1, uint32_t color2)
{
	setWindow(x, y, x + w - 1, y + h - 1);

	float delta = -255.0 / h;
	float alpha = 255.0;
	uint32_t color = color1;

	while (h--)
	{
		pushBlock(color, w);
		alpha += delta;
		color = alphaBlend((uint8_t)alpha, color1, color2);
	}
}

/***************************************************************************************
** Function name:           drawPixelAlpha
** Description:             Draw filled rectangle with horizontal colour gradient
***************************************************************************************/
void TFTLIB_8BIT::fillRectHGradient(int16_t x, int16_t y, int16_t w, int16_t h, uint32_t color1, uint32_t color2)
{
	float delta = -255.0 / w;
	float alpha = 255.0;
	uint32_t color = color1;

	while (w--)
	{
		drawFastVLine(x++, y, h, color);
		alpha += delta;
		color = alphaBlend((uint8_t)alpha, color1, color2);
	}
}

/***************************************************************************************
** Function name:           drawSpot - maths intensive, so for small filled circles
** Description:             Draw an anti-aliased filled circle at ax,ay with radius r
***************************************************************************************/
void TFTLIB_8BIT::drawSpot(float ax, float ay, float r, uint32_t color)
{
	// Filled circle can be created by the wide line function with zero line length
	drawWedgeLine(ax, ay, ax, ay, r, r, color, 0x00FFFFFF);
}

/***************************************************************************************
** Function name:           drawFastHLine
** Description:             Fast drawing Horizontal Line
***************************************************************************************/
void TFTLIB_8BIT::drawFastHLine(int_fast32_t x, int_fast32_t y, int_fast32_t w, uint_fast32_t color)
{
	if (x < 0)
	{
		w += x;
		x = 0;
	}
	if ((x + w) > _width)
		w = _width - x;
	if (w < 1 || y < 0 || y >= _height)
		return;

	if (_useFramebuffer && _framebuffer) {
		uint32_t *dst = &_framebuffer[y * _width + x];
		int_fast32_t len = w;
		
		// Unrolled 32x loop
		while (len > 31) {
			dst[0] = color; dst[1] = color; dst[2] = color; dst[3] = color;
			dst[4] = color; dst[5] = color; dst[6] = color; dst[7] = color;
			dst[8] = color; dst[9] = color; dst[10] = color; dst[11] = color;
			dst[12] = color; dst[13] = color; dst[14] = color; dst[15] = color;
			dst[16] = color; dst[17] = color; dst[18] = color; dst[19] = color;
			dst[20] = color; dst[21] = color; dst[22] = color; dst[23] = color;
			dst[24] = color; dst[25] = color; dst[26] = color; dst[27] = color;
			dst[28] = color; dst[29] = color; dst[30] = color; dst[31] = color;
			dst += 32;
			len -= 32;
		}
		
		// Unrolled 8x loop
		while (len > 7) {
			dst[0] = color; dst[1] = color; dst[2] = color; dst[3] = color;
			dst[4] = color; dst[5] = color; dst[6] = color; dst[7] = color;
			dst += 8;
			len -= 8;
		}
		
		// Remaining pixels
		while (len > 0) {
			*dst++ = color;
			len--;
		}
	} else {
		setWindow(x, y, x + w - 1, y);
		pushBlock(color, w);
	}
}

/***************************************************************************************
** Function name:           drawHLineAlpha
** Description:             Fast drawing Horizontal Line with alpha blending
***************************************************************************************/
void TFTLIB_8BIT::drawHLineAlpha(int32_t x, int32_t y, int32_t w, uint32_t color, uint8_t alpha)
{
	if (y < 0 || y >= _height) return;
	if (x < 0)
	{
		w += x;
		x = 0;
	}
	if ((x + w) > _width)
		w = _width - x;
	if (w < 1)
		return;

	/* Fast path for framebuffer mode - avoid per-pixel function calls */
	if (_useFramebuffer && _framebuffer) {
		uint32_t *dst = &_framebuffer[y * _width + x];
		
		/* Pre-extract foreground color components */
		uint32_t fgR = (color >> 16) & 0xFF;
		uint32_t fgG = (color >> 8) & 0xFF;
		uint32_t fgB = color & 0xFF;
		uint32_t invAlpha = 255 - alpha;
		
		/* Process pixels in a tight loop */
		for (int32_t i = 0; i < w; i++) {
			uint32_t bgc = dst[i];
			uint32_t bgR = (bgc >> 16) & 0xFF;
			uint32_t bgG = (bgc >> 8) & 0xFF;
			uint32_t bgB = bgc & 0xFF;
			
			uint32_t r = (fgR * alpha + bgR * invAlpha) >> 8;
			uint32_t g = (fgG * alpha + bgG * invAlpha) >> 8;
			uint32_t b = (fgB * alpha + bgB * invAlpha) >> 8;
			
			dst[i] = 0xFF000000 | (r << 16) | (g << 8) | b;
		}
	} else {
		/* Fallback for non-framebuffer mode */
		for (int32_t i = 0; i < w; i++) {
			drawPixelAlpha(x + i, y, color, alpha);
		}
	}
}

/***************************************************************************************
** Function name:           drawFastVLine
** Description:             Drawing Vertical Line
***************************************************************************************/
void TFTLIB_8BIT::drawFastVLine(int_fast32_t x, int_fast32_t y, int_fast32_t h, uint_fast32_t color)
{
	if (y < 0)
	{
		h += y;
		y = 0;
	}
	if ((y + h) > _height)
		h = _height - y;
	if (h < 1 || x < 0 || x >= _width)
		return;

	if (_useFramebuffer && _framebuffer) {
		uint32_t *dst = &_framebuffer[y * _width + x];
		for (int_fast32_t i = 0; i < h; i++) {
			*dst = color;
			dst += _width;
		}
	} else {
		setWindow(x, y, x, y + h - 1);
		pushBlock(color, h);
	}
}

/***************************************************************************************
** Function name:           drawVLineAlpha
** Description:             Drawing Vertical Line with alpha blending (optimized)
***************************************************************************************/
void TFTLIB_8BIT::drawVLineAlpha(int32_t x, int32_t y, int32_t h, uint32_t color, uint8_t alpha)
{
	if (x < 0 || x >= _width) return;
	if (y < 0)
	{
		h += y;
		y = 0;
	}
	if ((y + h) > _height)
		h = _height - y;
	if (h < 1)
		return;

	/* Fast path for framebuffer mode */
	if (_useFramebuffer && _framebuffer) {
		uint32_t *dst = &_framebuffer[y * _width + x];
		
		uint32_t fgR = (color >> 16) & 0xFF;
		uint32_t fgG = (color >> 8) & 0xFF;
		uint32_t fgB = color & 0xFF;
		uint32_t invAlpha = 255 - alpha;
		
		for (int32_t i = 0; i < h; i++) {
			uint32_t bgc = *dst;
			uint32_t bgR = (bgc >> 16) & 0xFF;
			uint32_t bgG = (bgc >> 8) & 0xFF;
			uint32_t bgB = bgc & 0xFF;
			
			uint32_t r = (fgR * alpha + bgR * invAlpha) >> 8;
			uint32_t g = (fgG * alpha + bgG * invAlpha) >> 8;
			uint32_t b = (fgB * alpha + bgB * invAlpha) >> 8;
			
			*dst = 0xFF000000 | (r << 16) | (g << 8) | b;
			dst += _width;
		}
	} else {
		for (int32_t i = 0; i < h; i++) {
			drawPixelAlpha(x, y + i, color, alpha);
		}
	}
}

/***************************************************************************************
** Function name:           drawCircleHelper
** Description:             Support function for drawRoundRect()
***************************************************************************************/
inline void TFTLIB_8BIT::drawCircleHelper(int32_t x0, int32_t y0, int32_t rr, uint8_t cornername, uint32_t color)
{
	if (rr <= 0)
		return;
	int32_t f = 1 - rr;
	int32_t ddF_x = 1;
	int32_t ddF_y = -2 * rr;
	int32_t xe = 0;
	int32_t xs = 0;
	int32_t len = 0;

	while (xe < rr--)
	{
		while (f < 0)
		{
			++xe;
			f += (ddF_x += 2);
		}

		f += (ddF_y += 2);

		if (xe - xs == 1)
		{
			if (cornername & 0x1)
			{ // left top
				drawPixel(x0 - xe, y0 - rr, color);
				drawPixel(x0 - rr, y0 - xe, color);
			}

			if (cornername & 0x2)
			{ // right top
				drawPixel(x0 + rr, y0 - xe, color);
				drawPixel(x0 + xs + 1, y0 - rr, color);
			}

			if (cornername & 0x4)
			{ // right bottom
				drawPixel(x0 + xs + 1, y0 + rr, color);
				drawPixel(x0 + rr, y0 + xs + 1, color);
			}

			if (cornername & 0x8)
			{ // left bottom
				drawPixel(x0 - rr, y0 + xs + 1, color);
				drawPixel(x0 - xe, y0 + rr, color);
			}
		}

		else
		{
			len = xe - xs++;
			if (cornername & 0x1)
			{ // left top
				drawFastHLine(x0 - xe, y0 - rr, len, color);
				drawFastVLine(x0 - rr, y0 - xe, len, color);
			}

			if (cornername & 0x2)
			{ // right top
				drawFastVLine(x0 + rr, y0 - xe, len, color);
				drawFastHLine(x0 + xs, y0 - rr, len, color);
			}

			if (cornername & 0x4)
			{ // right bottom
				drawFastHLine(x0 + xs, y0 + rr, len, color);
				drawFastVLine(x0 + rr, y0 + xs, len, color);
			}

			if (cornername & 0x8)
			{ // left bottom
				drawFastVLine(x0 - rr, y0 + xs, len, color);
				drawFastHLine(x0 - xe, y0 + rr, len, color);
			}
		}
		xs = xe;
	}
}

/***************************************************************************************
** Function name:           wideWedgeDistance
** Description:             Support function for drawWedgeLine
***************************************************************************************/
inline float TFTLIB_8BIT::wedgeLineDistance(float xpax, float ypay, float bax, float bay, float dr)
{
	float h = fmaxf(fminf((xpax * bax + ypay * bay) / (bax * bax + bay * bay), 1.0f), 0.0f);
	float dx = xpax - bax * h, dy = ypay - bay * h;
	return sqrtf(dx * dx + dy * dy) + h * dr;
}

/***************************************************************************************
** Function name:           fillCircleHelper
** Description:             Support function for fillRoundRect()
***************************************************************************************/
inline void TFTLIB_8BIT::fillCircleHelper(int32_t x0, int32_t y0, int32_t r, uint8_t cornername, int32_t delta, uint32_t color)
{
	int32_t f = 1 - r;
	int32_t ddF_x = 1;
	int32_t ddF_y = -r - r;
	int32_t y = 0;

	delta++;

	while (y < r)
	{
		if (f >= 0)
		{
			if (cornername & 0x1)
				drawFastHLine(x0 - y, y0 + r, y + y + delta, color);
			ddF_y += 2;
			f += ddF_y;
			if (cornername & 0x2)
				drawFastHLine(x0 - y, y0 - r, y + y + delta, color);
			r--;
		}

		y++;
		if (cornername & 0x1)
			drawFastHLine(x0 - r, y0 + y, r + r + delta, color);

		ddF_x += 2;
		f += ddF_x;
		if (cornername & 0x2)
			drawFastHLine(x0 - r, y0 - y, r + r + delta, color);
	}
}

/***************************************************************************************
** Function name:           fillCircleHelperAA
** Description:             Support function for fillRoundRectAA()
***************************************************************************************/
inline void TFTLIB_8BIT::fillCircleHelperAA(int32_t x0, int32_t y0, int32_t r, uint8_t cornername, int32_t delta, uint32_t color)
{
	int32_t f = 1 - r;
	int32_t ddF_x = 1;
	int32_t ddF_y = -r - r;
	int32_t y = 0;

	delta++;

	while (y < r)
	{
		if (f >= 0)
		{
			if (cornername & 0x1)
				drawWideLine(x0 - y, y0 + r, x0 + y + delta, y0 + r, 1, color);
			ddF_y += 2;
			f += ddF_y;
			if (cornername & 0x2)
				drawWideLine(x0 - y, y0 - r, x0 + y + delta, y0 - r, 1, color);
			r--;
		}

		y++;
		if (cornername & 0x1)
			drawWideLine(x0 - r, y0 + y, x0 + r + delta, y0 + y, 1, color);

		ddF_x += 2;
		f += ddF_x;
		if (cornername & 0x2)
			drawWideLine(x0 - r, y0 - y, x0 + r + delta, y0 - y, 1, color);
	}
}

/***************************************************************************************
** Function name:           fillAlphaCircleHelper
** Description:             Support function for fillAlphaRoundRect()
***************************************************************************************/
inline void TFTLIB_8BIT::fillAlphaCircleHelper(int32_t x0, int32_t y0, int32_t r, uint8_t cornername, int32_t delta, uint32_t color, uint8_t alpha)
{
	int32_t f = 1 - r;
	int32_t ddF_x = 1;
	int32_t ddF_y = -r - r;
	int32_t y = 0;

	delta++;

	while (y < r)
	{
		if (f >= 0)
		{
			if (cornername & 0x1)
				drawHLineAlpha(x0 - y, y0 + r, y + y + delta, color, alpha);
			ddF_y += 2;
			f += ddF_y;
			if (cornername & 0x2)
				drawHLineAlpha(x0 - y, y0 - r, y + y + delta, color, alpha);
			r--;
		}

		y++;
		if (cornername & 0x1)
			drawHLineAlpha(x0 - r, y0 + y, r + r + delta, color, alpha);

		ddF_x += 2;
		f += ddF_x;
		if (cornername & 0x2)
			drawHLineAlpha(x0 - r, y0 - y, r + r + delta, color, alpha);
	}
}

/***************************************************************************************
** Function name:           drawLine
** Description:             Draw a line with single color
***************************************************************************************/
void TFTLIB_8BIT::drawLine(int_fast32_t x0, int_fast32_t y0, int_fast32_t x1, int_fast32_t y1, uint_fast32_t color)
{
	bool steep = abs(y1 - y0) > abs(x1 - x0);
	if (steep)
	{
		swap_coord(x0, y0);
		swap_coord(x1, y1);
	}

	if (x0 > x1)
	{
		swap_coord(x0, x1);
		swap_coord(y0, y1);
	}

	int_fast32_t dx = x1 - x0, dy = abs(y1 - y0);

	int_fast32_t err = dx >> 1, ystep = -1, xs = x0, dlen = 0;

	if (y0 < y1)
		ystep = 1;

	// Split into steep and not steep for FastH/V separation
	if (steep)
	{
		for (; x0 <= x1; x0++)
		{
			dlen++;
			err -= dy;
			if (err < 0)
			{
				if (dlen == 1)
					drawPixel(y0, xs, color);
				else
					drawFastVLine(y0, xs, dlen, color);
				dlen = 0;
				y0 += ystep;
				xs = x0 + 1;
				err += dx;
			}
		}
		if (dlen)
			drawFastVLine(y0, xs, dlen, color);
	}
	else
	{
		for (; x0 <= x1; x0++)
		{
			dlen++;
			err -= dy;
			if (err < 0)
			{
				if (dlen == 1)
					drawPixel(xs, y0, color);
				else
					drawFastHLine(xs, y0, dlen, color);
				dlen = 0;
				y0 += ystep;
				xs = x0 + 1;
				err += dx;
			}
		}
		if (dlen)
			drawFastHLine(xs, y0, dlen, color);
	}
}

/***************************************************************************************
** Function name:           drawWideLine
** Description:             Draw anti-aliased line with single color
***************************************************************************************/
void TFTLIB_8BIT::drawWideLine(float ax, float ay, float bx, float by, float wd, uint16_t fg_color, uint16_t bg_color)
{
	drawWedgeLine(ax, ay, bx, by, wd / 2.0, wd / 2.0, fg_color, bg_color);
}

/***************************************************************************************
** Function name:           drawWedgeLine
** Description:             Draw anti-aliased line with single color
***************************************************************************************/
void TFTLIB_8BIT::drawWedgeLine(float ax, float ay, float bx, float by, float ar, float br, uint32_t fg_color, uint32_t bg_color)
{
	if ((abs(ax - bx) < 0.01f) && (abs(ay - by) < 0.01f))
		bx += 0.01f; // Avoid divide by zero

	// Find line bounding box
	int32_t x0 = (int32_t)floorf(fminf(ax - ar, bx - br));
	int32_t x1 = (int32_t)ceilf(fmaxf(ax + ar, bx + br));
	int32_t y0 = (int32_t)floorf(fminf(ay - ar, by - br));
	int32_t y1 = (int32_t)ceilf(fmaxf(ay + ar, by + br));

	// Establish x start and y start
	int32_t ys = ay;
	if ((ax - ar) > (bx - br))
		ys = by;

	float rdt = ar - br; // Radius delta
	float alpha = 1.0f;
	ar += 0.5;

	uint16_t bg = bg_color;
	float xpax, ypay, bax = bx - ax, bay = by - ay;

	int32_t xs = x0;
	// Scan bounding box from ys down, calculate pixel intensity from distance to line
	for (int32_t yp = ys; yp <= y1; yp++)
	{
		bool swin = true;  // Flag to start new window area
		bool endX = false; // Flag to skip pixels
		ypay = yp - ay;
		for (int32_t xp = xs; xp <= x1; xp++)
		{
			if (endX)
				if (alpha <= LoAlphaTheshold)
					break; // Skip right side
			xpax = xp - ax;
			alpha = ar - wedgeLineDistance(xpax, ypay, bax, bay, rdt);
			if (alpha <= LoAlphaTheshold)
				continue;
			// Track edge to minimise calculations
			if (!endX)
			{
				endX = true;
				xs = xp;
			}
			if (alpha > HiAlphaTheshold)
			{
				if (swin)
				{
					setWindow(xp, yp, width() - 1, yp);
					swin = false;
				}

				pushBlock(fg_color);
				continue;
			}
			// Blend color with background and plot
			if (bg_color == 0x00FFFFFF)
			{
				// bg = readPixel(xp, yp); swin = true;
			}

			if (swin)
			{
				setWindow(xp, yp, width() - 1, yp);
				swin = false;
			}

			pushBlock(alphaBlend((uint8_t)(alpha * PixelAlphaGain), fg_color, bg));
		}
	}

	// Reset x start to left side of box
	xs = x0;
	// Scan bounding box from ys-1 up, calculate pixel intensity from distance to line
	for (int32_t yp = ys - 1; yp >= y0; yp--)
	{
		bool swin = true;  // Flag to start new window area
		bool endX = false; // Flag to skip pixels
		ypay = yp - ay;
		for (int32_t xp = xs; xp <= x1; xp++)
		{
			if (endX)
				if (alpha <= LoAlphaTheshold)
					break; // Skip right side of drawn line
			xpax = xp - ax;
			alpha = ar - wedgeLineDistance(xpax, ypay, bax, bay, rdt);
			if (alpha <= LoAlphaTheshold)
				continue;
			// Track line boundary
			if (!endX)
			{
				endX = true;
				xs = xp;
			}
			if (alpha > HiAlphaTheshold)
			{
				if (swin)
				{
					setWindow(xp, yp, width() - 1, yp);
					swin = false;
				}
				pushBlock(fg_color);
				continue;
			}
			// Blend color with background and plot
			if (bg_color == 0x00FFFFFF)
			{
				// bg = readPixel(xp, yp); swin = true;
			}
			if (swin)
			{
				setWindow(xp, yp, width() - 1, yp);
				swin = false;
			}
			pushBlock(alphaBlend((uint8_t)(alpha * PixelAlphaGain), fg_color, bg));
		}
	}
}

/***************************************************************************************
** Function name:           drawSmoothArc
** Description:             Draw a smooth arc clockwise from 6 o'clock
***************************************************************************************/
void TFTLIB_8BIT::drawSmoothArc(int32_t x, int32_t y, int32_t r, int32_t ir, int32_t startAngle, int32_t endAngle, uint32_t fg_color, uint32_t bg_color, bool roundEnds)
{
	if (endAngle != startAngle && (startAngle != 0 || endAngle != 360))
	{
		float sx = -sinf(startAngle * deg2rad);
		float sy = +cosf(startAngle * deg2rad);
		float ex = -sinf(endAngle * deg2rad);
		float ey = +cosf(endAngle * deg2rad);

		if (roundEnds)
		{ // Round ends
			sx = sx * (r + ir) / 2.0 + x;
			sy = sy * (r + ir) / 2.0 + y;
			drawSpot(sx, sy, (r - ir) / 2.0, fg_color);

			ex = ex * (r + ir) / 2.0 + x;
			ey = ey * (r + ir) / 2.0 + y;
			drawSpot(ex, ey, (r - ir) / 2.0, fg_color);
		}
		else
		{ // Square ends
			float asx = sx * ir + x;
			float asy = sy * ir + y;
			float aex = sx * r + x;
			float aey = sy * r + y;
			drawWedgeLine(asx, asy, aex, aey, 0.3, 0.3, fg_color, bg_color);

			asx = ex * ir + x;
			asy = ey * ir + y;
			aex = ex * r + x;
			aey = ey * r + y;
			drawWedgeLine(asx, asy, aex, aey, 0.3, 0.3, fg_color, bg_color);
		}

		// Draw arc
		drawArc(x, y, r, ir, startAngle, endAngle, fg_color, bg_color);
	}
	else // Draw full 360
	{
		drawArc(x, y, r, ir, 0, 360, fg_color, bg_color);
	}
}

/***************************************************************************************
** Function name:           sqrt_fraction (private function)
** Description:             Smooth graphics support function for alpha derivation
***************************************************************************************/
// Compute the fixed point square root of an integer and
// return the 8 MS bits of fractional part.
// Quicker than sqrt() for processors that do not have and FPU (e.g. RP2040)
inline uint8_t TFTLIB_8BIT::sqrt_fraction(uint32_t num)
{
	if (num > (0x40000000))
		return 0;
	uint32_t bsh = 0x00004000;
	uint32_t fpr = 0;
	uint32_t osh = 0;

	// Auto adjust from U8:8 up to U15:16
	while (num > bsh)
	{
		bsh <<= 2;
		osh++;
	}

	do
	{
		uint32_t bod = bsh + fpr;
		if (num >= bod)
		{
			num -= bod;
			fpr = bsh + bod;
		}
		num <<= 1;
	} while (bsh >>= 1);

	return fpr >> osh;
}

/***************************************************************************************
** Function name:           drawArc
** Description:             Draw an arc clockwise from 6 o'clock position
***************************************************************************************/
void TFTLIB_8BIT::drawArc(int32_t x, int32_t y, int32_t r, int32_t ir,
						  int32_t startAngle, int32_t endAngle,
						  uint32_t fg_color, uint32_t bg_color,
						  bool smooth)
{
	if (endAngle < startAngle)
	{
		// Arc sweeps through 6 o'clock so draw in two parts
		drawArc(x, y, r, ir, startAngle, 360, fg_color, bg_color, smooth);
		startAngle = 0;
	}

	if (startAngle == endAngle)
		return;
	if (r < ir)
		swap_coord(r, ir); // Required that r > ir
	if (r <= 0 || ir < 0)
		return; // Invalid r, ir can be zero (circle sector)
	if (startAngle < 0)
		startAngle = 0;
	if (endAngle > 360)
		endAngle = 360;

	int32_t xs = 0;	   // x start position for quadrant scan
	uint8_t alpha = 0; // alpha value for blending pixels

	uint32_t r2 = r * r; // Outer arc radius^2
	if (smooth)
		r++;			   // Outer AA zone radius
	uint32_t r1 = r * r;   // Outer AA radius^2
	int16_t w = r - ir;	   // Width of arc (r - ir + 1)
	uint32_t r3 = ir * ir; // Inner arc radius^2
	if (smooth)
		ir--;			   // Inner AA zone radius
	uint32_t r4 = ir * ir; // Inner AA radius^2

	// Fixed point U16.16 slope table for arc start/end in each quadrant
	uint32_t startSlope[4] = {0, 0, 0xFFFFFFFF, 0};
	uint32_t endSlope[4] = {0, 0xFFFFFFFF, 0, 0};

	// Ensure maximum U16.16 slope of arc ends is ~ 0x8000 0000
	constexpr float minDivisor = 1.0f / 0x8000;

	// Fill in start slope table and empty quadrants
	float fabscos = fabsf(cosf(startAngle * deg2rad));
	float fabssin = fabsf(sinf(startAngle * deg2rad));

	// U16.16 slope of arc start
	uint32_t slope = (fabscos / (fabssin + minDivisor)) * (float)(1 << 16);

	// Update slope table, add slope for arc start
	if (startAngle < 90)
	{
		startSlope[0] = slope;
	}
	else if (startAngle < 180)
	{
		startSlope[1] = slope;
	}
	else if (startAngle < 270)
	{
		startSlope[1] = 0xFFFFFFFF;
		startSlope[2] = slope;
	}
	else
	{
		startSlope[1] = 0xFFFFFFFF;
		startSlope[2] = 0;
		startSlope[3] = slope;
	}

	// Fill in end slope table and empty quadrants
	fabscos = fabsf(cosf(endAngle * deg2rad));
	fabssin = fabsf(sinf(endAngle * deg2rad));

	// U16.16 slope of arc end
	slope = (uint32_t)((fabscos / (fabssin + minDivisor)) * (float)(1 << 16));

	// Work out which quadrants will need to be drawn and add slope for arc end
	if (endAngle < 90)
	{
		endSlope[0] = slope;
		endSlope[1] = 0;
		endSlope[2] = 0xFFFFFFFF;
	}
	else if (endAngle < 180)
	{
		endSlope[1] = slope;
		endSlope[2] = 0xFFFFFFFF;
	}
	else if (endAngle < 270)
	{
		endSlope[2] = slope;
	}
	else
	{
		endSlope[3] = slope;
	}

	// Scan quadrant
	for (int32_t cy = r - 1; cy > 0; cy--)
	{
		uint32_t len[4] = {0, 0, 0, 0};	   // Pixel run length
		int32_t xst[4] = {-1, -1, -1, -1}; // Pixel run x start
		uint32_t dy2 = (r - cy) * (r - cy);

		// Find and track arc zone start point
		while ((r - xs) * (r - xs) + dy2 >= r1)
			xs++;

		for (int32_t cx = xs; cx < r; cx++)
		{
			// Calculate radius^2
			uint32_t hyp = (r - cx) * (r - cx) + dy2;

			// If in outer zone calculate alpha
			if (hyp > r2)
			{
				// alpha = (uint8_t)((rf - sqrtf(hyp)) * 255);
				alpha = ~sqrt_fraction(hyp); // Outer AA zone
			}
			// If within arc fill zone, get line start and lengths for each quadrant
			else if (hyp >= r3)
			{
				// Calculate U16.16 slope
				slope = ((r - cy) << 16) / (r - cx);
				if (slope <= startSlope[0] && slope >= endSlope[0])
				{				 // slope hi -> lo
					xst[0] = cx; // Bottom left line end
					len[0]++;
				}
				if (slope >= startSlope[1] && slope <= endSlope[1])
				{				 // slope lo -> hi
					xst[1] = cx; // Top left line end
					len[1]++;
				}
				if (slope <= startSlope[2] && slope >= endSlope[2])
				{				 // slope hi -> lo
					xst[2] = cx; // Bottom right line start
					len[2]++;
				}
				if (slope >= startSlope[3] && slope <= endSlope[3])
				{				 // slope lo -> hi
					xst[3] = cx; // Top right line start
					len[3]++;
				}
				continue; // Next x
			}
			else
			{
				if (hyp <= r4)
					break; // Skip inner pixels
				// alpha = (uint8_t)((sqrtf(hyp) - irf) * 255.0);
				alpha = sqrt_fraction(hyp); // Inner AA zone
			}

			if (alpha < 16)
				continue; // Skip low alpha pixels

			// If background is read it must be done in each quadrant
			uint16_t pcol = alphaBlend(alpha, fg_color, bg_color);
			// Check if an AA pixels need to be drawn
			slope = ((r - cy) << 16) / (r - cx);
			if (slope <= startSlope[0] && slope >= endSlope[0]) // BL
				drawPixel(x + cx - r, y - cy + r, pcol);
			if (slope >= startSlope[1] && slope <= endSlope[1]) // TL
				drawPixel(x + cx - r, y + cy - r, pcol);
			if (slope <= startSlope[2] && slope >= endSlope[2]) // TR
				drawPixel(x - cx + r, y + cy - r, pcol);
			if (slope >= startSlope[3] && slope <= endSlope[3]) // BR
				drawPixel(x - cx + r, y - cy + r, pcol);
		}
		// Add line in inner zone
		if (len[0])
			drawFastHLine(x + xst[0] - len[0] + 1 - r, y - cy + r, len[0], fg_color); // BL
		if (len[1])
			drawFastHLine(x + xst[1] - len[1] + 1 - r, y + cy - r, len[1], fg_color); // TL
		if (len[2])
			drawFastHLine(x - xst[2] + r, y + cy - r, len[2], fg_color); // TR
		if (len[3])
			drawFastHLine(x - xst[3] + r, y - cy + r, len[3], fg_color); // BR
	}

	// Fill in centre lines
	if (startAngle == 0 || endAngle == 360)
		drawFastVLine(x, y + r - w, w, fg_color); // Bottom
	if (startAngle <= 90 && endAngle >= 90)
		drawFastHLine(x - r + 1, y, w, fg_color); // Left
	if (startAngle <= 180 && endAngle >= 180)
		drawFastVLine(x, y - r + 1, w, fg_color); // Top
	if (startAngle <= 270 && endAngle >= 270)
		drawFastHLine(x + r - w, y, w, fg_color); // Right
}

/***************************************************************************************
** Function name:           fillSmoothCircle
** Description:             Draw a filled anti-aliased circle
***************************************************************************************/
void TFTLIB_8BIT::fillSmoothCircle(int32_t x, int32_t y, int32_t r, uint32_t color, uint32_t bg_color)
{
	if (r <= 0)
		return;

	drawFastHLine(x - r, y, 2 * r + 1, color);
	int32_t xs = 1;
	int32_t cx = 0;

	int32_t r1 = r * r;
	r++;
	int32_t r2 = r * r;

	for (int32_t cy = r - 1; cy > 0; cy--)
	{
		int32_t dy2 = (r - cy) * (r - cy);
		for (cx = xs; cx < r; cx++)
		{
			int32_t hyp2 = (r - cx) * (r - cx) + dy2;
			if (hyp2 <= r1)
				break;
			if (hyp2 >= r2)
				continue;
			float alphaf = (float)r - sqrtf(hyp2);
			if (alphaf > HiAlphaTheshold)
				break;
			xs = cx;
			if (alphaf < LoAlphaTheshold)
				continue;
			uint8_t alpha = alphaf * 255;

			if (bg_color == 0x00FFFFFF)
			{
				drawPixel(x + cx - r, y + cy - r, color, alpha, bg_color);
				drawPixel(x - cx + r, y + cy - r, color, alpha, bg_color);
				drawPixel(x - cx + r, y - cy + r, color, alpha, bg_color);
				drawPixel(x + cx - r, y - cy + r, color, alpha, bg_color);
			}

			else
			{
				uint16_t pcol = drawPixel(x + cx - r, y + cy - r, color, alpha, bg_color);
				drawPixel(x - cx + r, y + cy - r, pcol);
				drawPixel(x - cx + r, y - cy + r, pcol);
				drawPixel(x + cx - r, y - cy + r, pcol);
			}
		}
		drawFastHLine(x + cx - r, y + cy - r, 2 * (r - cx) + 1, color);
		drawFastHLine(x + cx - r, y - cy + r, 2 * (r - cx) + 1, color);
	}
}

/***************************************************************************************
** Function name:           fillSmoothRoundRect
** Description:             Draw a filled anti-aliased rounded corner rectangle
***************************************************************************************/
void TFTLIB_8BIT::fillSmoothRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint32_t color, uint32_t bg_color)
{
	int32_t xs = 0;
	int32_t cx = 0;

	y += r;
	h -= 2 * r;

	fillRect(x, y, w, h, color);

	h--;
	x += r;
	w -= 2 * r + 1;

	int32_t r1 = r * r;

	r++;

	int32_t r2 = r * r;

	for (int32_t cy = r - 1; cy > 0; cy--)
	{
		int32_t dy2 = (r - cy) * (r - cy);
		for (cx = xs; cx < r; cx++)
		{
			int32_t hyp2 = (r - cx) * (r - cx) + dy2;

			if (hyp2 <= r1)
				break;
			if (hyp2 >= r2)
				continue;

			float alphaf = (float)r - sqrtf(hyp2);

			if (alphaf > HiAlphaTheshold)
				break;

			xs = cx;

			if (alphaf < LoAlphaTheshold)
				continue;

			uint8_t alpha = alphaf * 255;

			drawPixel(x + cx - r, y + cy - r, color, alpha, bg_color);
			drawPixel(x - cx + r + w, y + cy - r, color, alpha, bg_color);
			drawPixel(x - cx + r + w, y - cy + r + h, color, alpha, bg_color);
			drawPixel(x + cx - r, y - cy + r + h, color, alpha, bg_color);
		}

		drawFastHLine(x + cx - r, y + cy - r, 2 * (r - cx) + 1 + w, color);
		drawFastHLine(x + cx - r, y - cy + r + h, 2 * (r - cx) + 1 + w, color);
	}
}

/***************************************************************************************
** Function name:           drawTriangle
** Description:             Draw a triangle with single color
***************************************************************************************/
void TFTLIB_8BIT::drawTriangle(int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t x3, int32_t y3, uint32_t color)
{
	drawLine(x1, y1, x2, y2, color);
	drawLine(x2, y2, x3, y3, color);
	drawLine(x3, y3, x1, y1, color);
}

/***************************************************************************************
** Function name:           drawTriangleAA
** Description:             Draw anti-aliased triangle with single color and specified thickness
***************************************************************************************/
void TFTLIB_8BIT::drawTriangleAA(int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t x3, int32_t y3, int32_t thickness, uint32_t color)
{
	drawWideLine(x1, y1, x2, y2, thickness, color);
	drawWideLine(x2, y2, x3, y3, thickness, color);
	drawWideLine(x3, y3, x1, y1, thickness, color);
}

/***************************************************************************************
** Function name:           drawRect
** Description:             Draw a rectangle with single color
***************************************************************************************/
void TFTLIB_8BIT::drawRect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color)
{
	if (x < 0)
	{
		w += x;
		x = 0;
	}
	if (y < 0)
	{
		h += y;
		y = 0;
	}
	if ((x + w) > _width)
		w = _width - x;
	if ((y + h) > _height)
		h = _height - y;

	drawFastHLine(x, y, w, color);
	drawFastVLine(x, y, h, color);
	drawFastVLine(x + w, y, h, color);
	drawFastHLine(x, y + h, w, color);
}

/***************************************************************************************
** Function name:           drawRectAA
** Description:             Draw anti-aliased rectangle with single color
***************************************************************************************/
void TFTLIB_8BIT::drawRectAA(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color)
{
	if (x < 0)
	{
		w += x;
		x = 0;
	}
	if (y < 0)
	{
		h += y;
		y = 0;
	}
	if ((x + w) > _width)
		w = _width - x;
	if ((y + h) > _height)
		h = _height - y;

	drawWideLine(x, y, x + w, y, 1, color);
	drawWideLine(x, y, x, y + h, 1, color);
	drawWideLine(x + w, y, x + w, y + h, 1, color);
	drawWideLine(x, y + h, x + w, y + h, 1, color);
}

/***************************************************************************************
** Function name:           drawRoundRect
** Description:             Draw a rectangle with rounded corners
***************************************************************************************/
void TFTLIB_8BIT::drawRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint32_t color)
{
	if (x < 0)
	{
		w += x;
		x = 0;
	}
	if (y < 0)
	{
		h += y;
		y = 0;
	}
	if ((x + w) > _width)
		w = _width - x;
	if ((y + h) > _height)
		h = _height - y;

	drawFastHLine(x + r, y, w - r - r, color);		   // Top
	drawFastHLine(x + r, y + h - 1, w - r - r, color); // Bottom
	drawFastVLine(x, y + r, h - r - r, color);		   // Left
	drawFastVLine(x + w - 1, y + r, h - r - r, color); // Right

	// draw four corners
	drawCircleHelper(x + r, y + r, r, 1, color);
	drawCircleHelper(x + w - r - 1, y + r, r, 2, color);
	drawCircleHelper(x + w - r - 1, y + h - r - 1, r, 4, color);
	drawCircleHelper(x + r, y + h - r - 1, r, 8, color);
}

/***************************************************************************************
** Function name:           drawCircle
** Description:             Draw a circle with single color
***************************************************************************************/
void TFTLIB_8BIT::drawCircle(int32_t x0, int32_t y0, int32_t r, uint32_t color)
{
	int32_t f = 1 - r;
	int32_t ddF_y = -2 * r;
	int32_t ddF_x = 1;
	int32_t xs = -1;
	int32_t xe = 0;
	int32_t len = 0;

	bool first = true;
	do
	{
		while (f < 0)
		{
			++xe;
			f += (ddF_x += 2);
		}
		f += (ddF_y += 2);

		if (xe - xs > 1)
		{
			if (first)
			{
				len = 2 * (xe - xs) - 1;
				drawFastHLine(x0 - xe, y0 + r, len, color);
				drawFastHLine(x0 - xe, y0 - r, len, color);
				drawFastVLine(x0 + r, y0 - xe, len, color);
				drawFastVLine(x0 - r, y0 - xe, len, color);
				first = false;
			}
			else
			{
				len = xe - xs++;
				drawFastHLine(x0 - xe, y0 + r, len, color);
				drawFastHLine(x0 - xe, y0 - r, len, color);
				drawFastHLine(x0 + xs, y0 - r, len, color);
				drawFastHLine(x0 + xs, y0 + r, len, color);

				drawFastVLine(x0 + r, y0 + xs, len, color);
				drawFastVLine(x0 + r, y0 - xe, len, color);
				drawFastVLine(x0 - r, y0 - xe, len, color);
				drawFastVLine(x0 - r, y0 + xs, len, color);
			}
		}
		else
		{
			++xs;
			drawPixel(x0 - xe, y0 + r, color);
			drawPixel(x0 - xe, y0 - r, color);
			drawPixel(x0 + xs, y0 - r, color);
			drawPixel(x0 + xs, y0 + r, color);

			drawPixel(x0 + r, y0 + xs, color);
			drawPixel(x0 + r, y0 - xe, color);
			drawPixel(x0 - r, y0 - xe, color);
			drawPixel(x0 - r, y0 + xs, color);
		}
		xs = xe;
	} while (xe < --r);
}

/***************************************************************************************
** Function name:           drawEllipse
** Description:             Draw an ellipse with single color
***************************************************************************************/
void TFTLIB_8BIT::drawEllipse(int16_t x0, int16_t y0, int32_t rx, int32_t ry, uint32_t color)
{
	if (x0 - rx < 0 || x0 + rx > _width || y0 - ry < 0 || y0 + ry > _height || rx < 2 || ry < 2)
		return;
	int32_t x, y;
	int32_t rx2 = rx * rx;
	int32_t ry2 = ry * ry;
	int32_t fx2 = 4 * rx2;
	int32_t fy2 = 4 * ry2;
	int32_t s;

	for (x = 0, y = ry, s = 2 * ry2 + rx2 * (1 - 2 * ry); ry2 * x <= rx2 * y; x++)
	{
		drawPixel(x0 + x, y0 + y, color);
		drawPixel(x0 - x, y0 + y, color);
		drawPixel(x0 - x, y0 - y, color);
		drawPixel(x0 + x, y0 - y, color);
		if (s >= 0)
		{
			s += fx2 * (1 - y);
			y--;
		}
		s += ry2 * ((4 * x) + 6);
	}

	for (x = rx, y = 0, s = 2 * rx2 + ry2 * (1 - 2 * rx); rx2 * y <= ry2 * x; y++)
	{
		drawPixel(x0 + x, y0 + y, color);
		drawPixel(x0 - x, y0 + y, color);
		drawPixel(x0 - x, y0 - y, color);
		drawPixel(x0 + x, y0 - y, color);
		if (s >= 0)
		{
			s += fy2 * (1 - x);
			x--;
		}
		s += rx2 * ((4 * y) + 6);
	}
}

/***************************************************************************************
** Function name:           fillTriangle
** Description:             Draw filled triangle with fixed color
***************************************************************************************/
void TFTLIB_8BIT::fillTriangle(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2, int32_t y2, uint32_t color)
{
	int32_t a, b, y, last;

	if (y0 > y1)
	{
		swap_coord(y0, y1);
		swap_coord(x0, x1);
	}

	if (y1 > y2)
	{
		swap_coord(y2, y1);
		swap_coord(x2, x1);
	}

	if (y0 > y1)
	{
		swap_coord(y0, y1);
		swap_coord(x0, x1);
	}

	if (y0 == y2)
	{
		a = b = x0;
		if (x1 < a)
			a = x1;
		else if (x1 > b)
			b = x1;

		if (x2 < a)
			a = x2;
		else if (x2 > b)
			b = x2;
		drawFastHLine(a, y0, b - a + 1, color);
		return;
	}

	int32_t
		dx01 = x1 - x0,
		dy01 = y1 - y0,
		dx02 = x2 - x0,
		dy02 = y2 - y0,
		dx12 = x2 - x1,
		dy12 = y2 - y1,
		sa = 0,
		sb = 0;

	if (y1 == y2)
		last = y1;
	else
		last = y1 - 1;

	for (y = y0; y <= last; y++)
	{
		a = x0 + sa / dy01;
		b = x0 + sb / dy02;
		sa += dx01;
		sb += dx02;

		if (a > b)
			swap_coord(a, b);
		drawFastHLine(a, y, b - a + 1, color);
	}

	sa = dx12 * (y - y1);
	sb = dx02 * (y - y0);

	for (; y <= y2; y++)
	{
		a = x1 + sa / dy12;
		b = x0 + sb / dy02;
		sa += dx12;
		sb += dx02;

		if (a > b)
			swap_coord(a, b);
		drawFastHLine(a, y, b - a + 1, color);
	}
}

/***************************************************************************************
** Function name:           fillTriangleAA
** Description:             Draw anti-aliased filled triangle with fixed color
***************************************************************************************/
void TFTLIB_8BIT::fillTriangleAA(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2, int32_t y2, uint32_t color)
{
	int32_t a, b, y, last;

	if (y0 > y1)
	{
		swap_coord(y0, y1);
		swap_coord(x0, x1);
	}

	if (y1 > y2)
	{
		swap_coord(y2, y1);
		swap_coord(x2, x1);
	}

	if (y0 > y1)
	{
		swap_coord(y0, y1);
		swap_coord(x0, x1);
	}

	if (y0 == y2)
	{
		a = b = x0;
		if (x1 < a)
			a = x1;
		else if (x1 > b)
			b = x1;
		if (x2 < a)
			a = x2;
		else if (x2 > b)
			b = x2;
		drawWideLine(a, y0, b + 1, y0, 1, color);
		return;
	}

	int32_t
		dx01 = x1 - x0,
		dy01 = y1 - y0,
		dx02 = x2 - x0,
		dy02 = y2 - y0,
		dx12 = x2 - x1,
		dy12 = y2 - y1,
		sa = 0,
		sb = 0;

	if (y1 == y2)
		last = y1;
	else
		last = y1 - 1;

	for (y = y0; y <= last; y++)
	{
		a = x0 + sa / dy01;
		b = x0 + sb / dy02;
		sa += dx01;
		sb += dx02;

		if (a > b)
			swap_coord(a, b);
		drawWideLine(a, y, b + 1, y, 1, color);
	}

	sa = dx12 * (y - y1);
	sb = dx02 * (y - y0);

	for (; y <= y2; y++)
	{
		a = x1 + sa / dy12;
		b = x0 + sb / dy02;
		sa += dx12;
		sb += dx02;

		if (a > b)
			swap_coord(a, b);
		drawWideLine(a, y, b + 1, y, 1, color);
	}
}

/***************************************************************************************
** Function name:           fillTriangleAlpha
** Description:             Draw filled triangle with alpha channel
***************************************************************************************/
void TFTLIB_8BIT::fillTriangleAlpha(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2, int32_t y2, uint32_t color, uint8_t alpha)
{
	int32_t a, b, y, last;

	if (y0 > y1)
	{
		swap_coord(y0, y1);
		swap_coord(x0, x1);
	}

	if (y1 > y2)
	{
		swap_coord(y2, y1);
		swap_coord(x2, x1);
	}

	if (y0 > y1)
	{
		swap_coord(y0, y1);
		swap_coord(x0, x1);
	}

	if (y0 == y2)
	{
		a = b = x0;
		if (x1 < a)
			a = x1;
		else if (x1 > b)
			b = x1;
		if (x2 < a)
			a = x2;
		else if (x2 > b)
			b = x2;
		drawHLineAlpha(a, y0, b - a, color, alpha);
		return;
	}

	int32_t
		dx01 = x1 - x0,
		dy01 = y1 - y0,
		dx02 = x2 - x0,
		dy02 = y2 - y0,
		dx12 = x2 - x1,
		dy12 = y2 - y1,
		sa = 0,
		sb = 0;

	if (y1 == y2)
		last = y1;
	else
		last = y1 - 1;

	for (y = y0; y <= last; y++)
	{
		a = x0 + sa / dy01;
		b = x0 + sb / dy02;
		sa += dx01;
		sb += dx02;

		if (a > b)
			swap_coord(a, b);
		drawHLineAlpha(a, y, b - a, color, alpha);
	}

	sa = dx12 * (y - y1);
	sb = dx02 * (y - y0);
	for (; y <= y2; y++)
	{
		a = x1 + sa / dy12;
		b = x0 + sb / dy02;
		sa += dx12;
		sb += dx02;

		if (a > b)
			swap_coord(a, b);
		drawHLineAlpha(a, y, b - a, color, alpha);
	}
}

/***************************************************************************************
** Function name:           fillRect
** Description:             Draw a filled rectangle with fixed color
**                          Optimized with memfill32 ARM assembly for Cortex-M7
***************************************************************************************/
void TFTLIB_8BIT::fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color)
{
	if (x < 0)
	{
		w += x;
		x = 0;
	}
	if (y < 0)
	{
		h += y;
		y = 0;
	}
	if ((x + w) > _width)
		w = _width - x;
	if ((y + h) > _height)
		h = _height - y;
	if (w < 1 || h < 1)
		return;

	if (_useFramebuffer && _framebuffer) {
		/* Use optimized ARM assembly memfill32 for 32-bit color fills */
		if (w == _width) {
			/* Full-width fill - single call for entire block */
			memfill32(&_framebuffer[y * _width], color, w * h);
		} else {
			/* Partial width - one fill per row */
			for (int32_t row = 0; row < h; row++) {
				memfill32(&_framebuffer[(y + row) * _width + x], color, w);
			}
		}
	} else {
		setWindow(x, y, x + w - 1, y + h - 1);
		pushBlock(color, w * h);
	}
}

/***************************************************************************************
** Function name:           fillRectAA
** Description:             Draw anti-aliased filled rectangle with fixed color
***************************************************************************************/
void TFTLIB_8BIT::fillRectAA(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color)
{
	setWindow(x, y, x + w - 1, y + h - 1);
	pushBlock(color, w * h);

	drawWideLine(x, y, x + w - 1, y, 1, color);
	drawWideLine(x, y + h - 1, x + w - 1, y + h - 1, 1, color);

	drawWideLine(x, y, x, y + h - 1, 1, color);
	drawWideLine(x + w, y, x + w, y + h - 1, 1, color);
}

/***************************************************************************************
** Function name:           fillRectAlpha
** Description:             Draw a filled rectangle with alpha channel (optimized)
***************************************************************************************/
void TFTLIB_8BIT::fillRectAlpha(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color, uint8_t alpha)
{
	/* Clipping */
	if (x < 0) { w += x; x = 0; }
	if (y < 0) { h += y; y = 0; }
	if (x + w > _width) w = _width - x;
	if (y + h > _height) h = _height - y;
	if (w < 1 || h < 1) return;
	
	/* Fast path for framebuffer mode */
	if (_useFramebuffer && _framebuffer) {
		/* Pre-extract foreground color components */
		uint32_t fgR = (color >> 16) & 0xFF;
		uint32_t fgG = (color >> 8) & 0xFF;
		uint32_t fgB = color & 0xFF;
		uint32_t invAlpha = 255 - alpha;
		
		for (int32_t row = 0; row < h; row++) {
			uint32_t *dst = &_framebuffer[(y + row) * _width + x];
			for (int32_t col = 0; col < w; col++) {
				uint32_t bgc = dst[col];
				uint32_t bgR = (bgc >> 16) & 0xFF;
				uint32_t bgG = (bgc >> 8) & 0xFF;
				uint32_t bgB = bgc & 0xFF;
				
				uint32_t r = (fgR * alpha + bgR * invAlpha) >> 8;
				uint32_t g = (fgG * alpha + bgG * invAlpha) >> 8;
				uint32_t b = (fgB * alpha + bgB * invAlpha) >> 8;
				
				dst[col] = 0xFF000000 | (r << 16) | (g << 8) | b;
			}
		}
	} else {
		/* Fallback */
		for (int32_t row = 0; row < h; row++) {
			drawHLineAlpha(x, y + row, w, color, alpha);
		}
	}
}

/***************************************************************************************
** Function name:           fillRoundRect
** Description:             Draw a filled rectangle with rounded corners & single color
***************************************************************************************/
void TFTLIB_8BIT::fillRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint32_t color)
{
	fillRect(x, y + r, w, h - r - r, color);

	fillCircleHelper(x + r, y + h - r - 1, r, 1, w - r - r - 1, color);
	fillCircleHelper(x + r, y + r, r, 2, w - r - r - 1, color);
}

/***************************************************************************************
** Function name:           fillRoundRectAA
** Description:             Draw a filled rectangle with rounded corners & single color
***************************************************************************************/
void TFTLIB_8BIT::fillRoundRectAA(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint32_t color)
{
	fillRectAA(x, y + r, w, h - r - r, color);

	fillCircleHelperAA(x + r, y + h - r - 1, r, 1, w - r - r - 1, color);
	fillCircleHelperAA(x + r, y + r, r, 2, w - r - r - 1, color);
}

/***************************************************************************************
** Function name:           fillAlphaRoundRect
** Description:             Draw filled rectangle with rounded corners & alpha channel
***************************************************************************************/
void TFTLIB_8BIT::fillAlphaRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint32_t color, uint8_t alpha)
{
	fillRectAlpha(x, y + r, w, h - r - r, color, alpha);

	fillAlphaCircleHelper(x + r, y + h - r - 1, r, 1, w - r - r - 1, color, alpha);
	fillAlphaCircleHelper(x + r, y + r, r, 2, w - r - r - 1, color, alpha);
}

/***************************************************************************************
** Function name:           fillCircle
** Description:             Draw a filled circle with single color
***************************************************************************************/
void TFTLIB_8BIT::fillCircle(int_fast32_t x0, int_fast32_t y0, int_fast32_t r, uint_fast32_t color)
{
	int_fast32_t x = 0;
	int_fast32_t dx = 1;
	int_fast32_t dy = r + r;
	int_fast32_t p = -(r >> 1);

	drawFastHLine(x0 - r, y0, dy + 1, color);

	while (x < r)
	{
		if (p >= 0)
		{
			drawFastHLine(x0 - x + 1, y0 + r, dx - 1, color);
			drawFastHLine(x0 - x + 1, y0 - r, dx - 1, color);
			dy -= 2;
			p -= dy;
			r--;
		}

		dx += 2;
		p += dx;
		x++;

		drawFastHLine(x0 - r, y0 + x, dy + 1, color);
		drawFastHLine(x0 - r, y0 - x, dy + 1, color);
	}
}

/***************************************************************************************
** Function name:           fillCircleAA
** Description:             Draw anti-aliased filled circle with fixed color
***************************************************************************************/
void TFTLIB_8BIT::fillCircleAA(float x, float y, float r, uint32_t color)
{
	drawWedgeLine(x, y, x, y, r, r, color, 0xFFFF);
}

/***************************************************************************************
** Function name:           fillCircleAlpha
** Description:             Draw filled circle with alpha channel
***************************************************************************************/
void TFTLIB_8BIT::fillCircleAlpha(int32_t x0, int32_t y0, int32_t r, uint32_t color, uint8_t alpha)
{
	int32_t x = 0;
	int32_t dx = 1;
	int32_t dy = r + r;
	int32_t p = -(r >> 1);

	drawHLineAlpha(x0 - r, y0, dy + 1, color, alpha);

	while (x < r)
	{
		if (p >= 0)
		{
			drawHLineAlpha(x0 - x + 1, y0 + r, dx - 1, color, alpha);
			drawHLineAlpha(x0 - x + 1, y0 - r, dx - 1, color, alpha);
			dy -= 2;
			p -= dy;
			r--;
		}

		dx += 2;
		p += dx;
		x++;

		drawHLineAlpha(x0 - r, y0 + x, dy + 1, color, alpha);
		drawHLineAlpha(x0 - r, y0 - x, dy + 1, color, alpha);
	}
}

/***************************************************************************************
** Function name:           fillEllipse
** Description:             Draw a filled ellipse with single color
***************************************************************************************/
void TFTLIB_8BIT::fillEllipse(int16_t x0, int16_t y0, int32_t rx, int32_t ry, uint32_t color)
{
	int32_t x, y;
	int32_t rx2 = rx * rx;
	int32_t ry2 = ry * ry;
	int32_t fx2 = 4 * rx2;
	int32_t fy2 = 4 * ry2;
	int32_t s;

	for (x = 0, y = ry, s = 2 * ry2 + rx2 * (1 - 2 * ry); ry2 * x <= rx2 * y; x++)
	{
		drawFastHLine(x0 - x, y0 - y, x + x + 1, color);
		drawFastHLine(x0 - x, y0 + y, x + x + 1, color);

		if (s >= 0)
		{
			s += fx2 * (1 - y);
			y--;
		}
		s += ry2 * ((4 * x) + 6);
	}

	for (x = rx, y = 0, s = 2 * rx2 + ry2 * (1 - 2 * rx); rx2 * y <= ry2 * x; y++)
	{
		drawFastHLine(x0 - x, y0 - y, x + x + 1, color);
		drawFastHLine(x0 - x, y0 + y, x + x + 1, color);

		if (s >= 0)
		{
			s += fy2 * (1 - x);
			x--;
		}
		s += rx2 * ((4 * y) + 6);
	}
}

/***************************************************************************************
** Function name:           drawImage
** Description:             Draw image at coords x&y
***************************************************************************************/
void TFTLIB_8BIT::drawImage(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t *data)
{
	setWindow(x, y, x + w - 1, y + h - 1);
	writeData16(data, w * h);
}

/***************************************************************************************
** Function name:           pushImage
** Description:             plot 16 bit colour sprite or image onto TFT
***************************************************************************************/
void TFTLIB_8BIT::pushImage(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t *data)
{
	setWindow(x, y, x + w - 1, y + h - 1);
	pushPixels16(data, w * h);
}

/***************************************************************************************
** Function name:           pushImage
** Description:             plot 8 bit or 4 bit or 1 bit image or sprite using a line buffer
***************************************************************************************/
void TFTLIB_8BIT::pushImage(int32_t x, int32_t y, int32_t w, int32_t h, uint8_t *data, bool bpp8, uint16_t *cmap)
{
	setWindow(x, y, x + w - 1, y + h - 1); // Sets CS low and sent RAMWR

	// Line buffer makes plotting faster
	uint16_t lineBuf[w];

	if (bpp8)
	{
		uint8_t blue[] = {0, 11, 21, 31}; // blue 2 to 5 bit colour lookup table

		_lastColor = -1; // Set to illegal value

		// Used to store last shifted colour
		uint8_t msbColor = 0;
		uint8_t lsbColor = 0;

		data += x + y * w;
		while (h--)
		{
			uint32_t len = w;
			uint8_t *ptr = (uint8_t *)data;
			uint8_t *linePtr = (uint8_t *)lineBuf;

			while (len--)
			{
				uint32_t color = *ptr++;

				// Shifts are slow so check if colour has changed first
				if (color != _lastColor)
				{
					//          =====Green=====     ===============Red==============
					msbColor = (color & 0x1C) >> 2 | (color & 0xC0) >> 3 | (color & 0xE0);
					//          =====Green=====    =======Blue======
					lsbColor = (color & 0x1C) << 3 | blue[color & 0x03];
					_lastColor = color;
				}

				*linePtr++ = msbColor;
				*linePtr++ = lsbColor;
			}

			pushPixels8(lineBuf, w);

			data += w;
		}
	}
	else if (cmap != nullptr) // Must be 4bpp
	{
		w = (w + 1) & 0xFFFE;			   // if this is a sprite, w will already be even; this does no harm.
		bool splitFirst = (x & 0x01) != 0; // split first means we have to push a single px from the left of the sprite / image

		if (splitFirst)
		{
			data += ((x - 1 + y * w) >> 1);
		}
		else
		{
			data += ((x + y * w) >> 1);
		}

		while (h--)
		{
			uint32_t len = w;
			uint8_t *ptr = (uint8_t *)data;
			uint16_t *linePtr = lineBuf;
			uint8_t colors; // two colors in one byte
			uint16_t index;

			if (splitFirst)
			{
				colors = *ptr;
				index = (colors & 0x0F);
				*linePtr++ = cmap[index];
				len--;
				ptr++;
			}

			while (len--)
			{
				colors = *ptr;
				index = ((colors & 0xF0) >> 4) & 0x0F;
				*linePtr++ = cmap[index];

				if (len--)
				{
					index = colors & 0x0F;
					*linePtr++ = cmap[index];
				}
				else
				{
					break; // nothing to do here
				}

				ptr++;
			}

			pushPixels8(lineBuf, w);
			data += (w >> 1);
		}
	}
	else // Must be 1bpp
	{
		uint8_t *ptr = (uint8_t *)data;
		uint32_t ww = (w + 7) >> 3; // Width of source image line in bytes
		for (int32_t yp = y; yp < y + h; yp++)
		{
			uint8_t *linePtr = (uint8_t *)lineBuf;
			for (int32_t xp = x; xp < x + w; xp++)
			{
				uint16_t col = (*(ptr + (xp >> 3)) & (0x80 >> (xp & 0x7)));
				if (col)
				{
					*linePtr++ = bitmap_fg >> 8;
					*linePtr++ = (uint8_t)bitmap_fg;
				}
				else
				{
					*linePtr++ = bitmap_bg >> 8;
					*linePtr++ = (uint8_t)bitmap_bg;
				}
			}
			ptr += ww;
			pushPixels8(lineBuf, w);
		}
	}
}

void TFTLIB_8BIT::pushImage(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t *data, uint16_t transp)
{

	data += x + y * w;

	uint16_t lineBuf[w];

	transp = transp >> 8 | transp << 8;

	while (h--)
	{
		int32_t len = w;
		uint16_t *ptr = (uint16_t *)data;
		int32_t px = x, sx = x;
		bool move = true;

		uint16_t np = 0;

		while (len--)
		{
			uint32_t color = *ptr;
			if (transp != color)
			{
				if (move)
				{
					move = false;
					sx = px;
				}
				lineBuf[np] = color;
				np++;
			}
			else
			{
				move = true;
				if (np)
				{
					setWindow(sx, y, sx + np - 1, y);
					pushPixels8(lineBuf, np);
					np = 0;
				}
			}
			px++;
			ptr++;
		}
		if (np)
		{
			setWindow(sx, y, sx + np - 1, y);
			pushPixels8(lineBuf, np);
		}

		y++;
		data += w;
	}
}

/***************************************************************************************
** Function name:           pushImage
** Description:             plot 8 or 4 or 1 bit image or sprite with a transparent colour
***************************************************************************************/
void TFTLIB_8BIT::pushImage(int32_t x, int32_t y, int32_t w, int32_t h, uint8_t *data, uint8_t transp, bool bpp8, uint16_t *cmap)
{
	// Line buffer makes plotting faster
	uint16_t lineBuf[w];

	if (bpp8)
	{ // 8 bits per pixel

		data += x + y * w;

		uint8_t blue[] = {0, 11, 21, 31}; // blue 2 to 5 bit colour lookup table

		_lastColor = -1; // Set to illegal value

		// Used to store last shifted colour
		uint8_t msbColor = 0;
		uint8_t lsbColor = 0;

		while (h--)
		{
			int32_t len = w;
			uint8_t *ptr = data;
			uint8_t *linePtr = (uint8_t *)lineBuf;

			int32_t px = x, sx = x;
			bool move = true;
			uint16_t np = 0;

			while (len--)
			{
				if (transp != *ptr)
				{
					if (move)
					{
						move = false;
						sx = px;
					}
					uint8_t color = *ptr;

					// Shifts are slow so check if colour has changed first
					if (color != _lastColor)
					{
						//          =====Green=====     ===============Red==============
						msbColor = (color & 0x1C) >> 2 | (color & 0xC0) >> 3 | (color & 0xE0);
						//          =====Green=====    =======Blue======
						lsbColor = (color & 0x1C) << 3 | blue[color & 0x03];
						_lastColor = color;
					}
					*linePtr++ = msbColor;
					*linePtr++ = lsbColor;
					np++;
				}
				else
				{
					move = true;
					if (np)
					{
						setWindow(sx, y, sx + np - 1, y);
						pushPixels8(lineBuf, np);
						linePtr = (uint8_t *)lineBuf;
						np = 0;
					}
				}
				px++;
				ptr++;
			}

			if (np)
			{
				setWindow(sx, y, sx + np - 1, y);
				pushPixels8(lineBuf, np);
			}
			y++;
			data += w;
		}
	}
	else if (cmap != nullptr) // 4bpp with color map
	{
		w = (w + 1) & 0xFFFE; // here we try to recreate iwidth from dwidth.
		bool splitFirst = ((x & 0x01) != 0);
		if (splitFirst)
		{
			data += ((x - 1 + y * w) >> 1);
		}
		else
		{
			data += ((x + y * w) >> 1);
		}

		while (h--)
		{
			uint32_t len = w;
			uint8_t *ptr = data;

			int32_t px = x, sx = x;
			bool move = true;
			uint16_t np = 0;

			uint8_t index; // index into cmap.

			if (splitFirst)
			{
				index = (*ptr & 0x0F); // odd = bits 3 .. 0
				if (index != transp)
				{
					move = false;
					sx = px;
					lineBuf[np] = cmap[index];
					np++;
				}
				px++;
				ptr++;
				len--;
			}

			while (len--)
			{
				uint8_t color = *ptr;

				// find the actual color you care about.  There will be two pixels here!
				// but we may only want one at the end of the row
				uint16_t index = ((color & 0xF0) >> 4) & 0x0F; // high bits are the even numbers
				if (index != transp)
				{
					if (move)
					{
						move = false;
						sx = px;
					}
					lineBuf[np] = cmap[index];
					np++; // added a pixel
				}
				else
				{
					move = true;
					if (np)
					{
						setWindow(sx, y, sx + np - 1, y);
						pushPixels8(lineBuf, np);
						np = 0;
					}
				}
				px++;

				if (len--)
				{
					index = color & 0x0F; // the odd number is 3 .. 0
					if (index != transp)
					{
						if (move)
						{
							move = false;
							sx = px;
						}
						lineBuf[np] = cmap[index];
						np++;
					}
					else
					{
						move = true;
						if (np)
						{
							setWindow(sx, y, sx + np - 1, y);
							pushPixels8(lineBuf, np);
							np = 0;
						}
					}
					px++;
				}
				else
				{
					break; // we are done with this row.
				}
				ptr++; // we only increment ptr once in the loop (deliberate)
			}

			if (np)
			{
				setWindow(sx, y, sx + np - 1, y);
				writeData16((uint16_t *)lineBuf, np);
				np = 0;
			}
			data += (w >> 1);
			y++;
		}
	}
	else
	{ // 1 bit per pixel

		uint32_t ww = (w + 7) >> 3; // Width of source image line in bytes
		uint16_t np = 0;

		for (int32_t yp = y; yp < y + h; yp++)
		{
			int32_t px = x, sx = x;
			bool move = true;
			for (int32_t xp = x; xp < x + w; xp++)
			{
				if (data[(xp >> 3)] & (0x80 >> (xp & 0x7)))
				{
					if (move)
					{
						move = false;
						sx = px;
					}
					np++;
				}
				else
				{
					move = true;
					if (np)
					{
						setWindow(sx, y, sx + np - 1, y);
						writeData16((uint16_t *)bitmap_fg, np);
						np = 0;
					}
				}
				px++;
			}
			if (np)
			{
				setWindow(sx, y, sx + np - 1, y);
				writeData16((uint16_t *)bitmap_fg, np);
				np = 0;
			}
			y++;
			data += ww;
		}
	}
}

/***************************************************************************************
** Function name:           drawBitmap
** Description:             Draw bitmap from array with fixed color
***************************************************************************************/
void TFTLIB_8BIT::drawBitmap(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t *bitmap, uint32_t color)
{
	int32_t i, j, byteWidth = (w + 7) / 8;

	for (j = 0; j < h; j++)
	{
		for (i = 0; i < w; i++)
		{
			if (*(bitmap + j * byteWidth + i / 8) & (128 >> (i & 7)))
			{
				drawPixel(x + i, y + j, color);
			}
		}
	}
}

/***************************************************************************************
** Function name:           drawBitmap
** Description:             Draw an image stored in an array on the TFT
***************************************************************************************/
void TFTLIB_8BIT::drawBitmap(int16_t x, int16_t y, int16_t w, int16_t h, const uint8_t *bitmap, uint32_t color)
{
	int32_t i, j, byteWidth = (w + 7) / 8;

	for (j = 0; j < h; j++)
	{
		for (i = 0; i < w; i++)
		{
			if (*(bitmap + j * byteWidth + i / 8) & (128 >> (i & 7)))
			{
				drawPixel(x + i, y + j, color);
			}
		}
	}
}

/***************************************************************************************
** Function name:           drawBitmap
** Description:             Draw an image stored in an array on the TFT
***************************************************************************************/
void TFTLIB_8BIT::drawBitmap(int16_t x, int16_t y, int16_t w, int16_t h, const uint8_t *bitmap, uint32_t fgcolor, uint32_t bgcolor)
{
	int32_t i, j, byteWidth = (w + 7) / 8;

	for (j = 0; j < h; j++)
	{
		for (i = 0; i < w; i++)
		{
			if (*(bitmap + j * byteWidth + i / 8) & (128 >> (i & 7)))
				drawPixel(x + i, y + j, fgcolor);
			else
				drawPixel(x + i, y + j, bgcolor);
		}
	}
}

/***************************************************************************************
** Function name:           drawXBitmap
** Description:             Draw an image stored in an XBM array onto the TFT
***************************************************************************************/
void TFTLIB_8BIT::drawXBitmap(int16_t x, int16_t y, const uint8_t *bitmap, int16_t w, int16_t h, uint32_t color)
{
	int32_t i, j, byteWidth = (w + 7) / 8;

	for (j = 0; j < h; j++)
	{
		for (i = 0; i < w; i++)
		{
			if (*(bitmap + j * byteWidth + i / 8) & (1 << (i & 7)))
			{
				drawPixel(x + i, y + j, color);
			}
		}
	}
}

/***************************************************************************************
** Function name:           drawXBitmap
** Description:             Draw an XBM image with foreground and background colors
***************************************************************************************/
void TFTLIB_8BIT::drawXBitmap(int16_t x, int16_t y, const uint8_t *bitmap, int16_t w, int16_t h, uint32_t color, uint32_t bgcolor)
{
	int32_t i, j, byteWidth = (w + 7) / 8;

	for (j = 0; j < h; j++)
	{
		for (i = 0; i < w; i++)
		{
			if (*(bitmap + j * byteWidth + i / 8) & (1 << (i & 7)))
				drawPixel(x + i, y + j, color);
			else
				drawPixel(x + i, y + j, bgcolor);
		}
	}
}

/***************************************************************************************
** Function name:           decodeUTF8
** Description:             Serial UTF-8 decoder with fall-back to extended ASCII
*************************************************************************************x*/
uint16_t TFTLIB_8BIT::decodeUTF8(uint8_t c)
{
	if (!_utf8)
		return c;

	// 7 bit Unicode Code Point
	if ((c & 0x80) == 0x00)
	{
		decoderState = 0;
		return c;
	}

	if (decoderState == 0)
	{
		// 11 bit Unicode Code Point
		if ((c & 0xE0) == 0xC0)
		{
			decoderBuffer = ((c & 0x1F) << 6);
			decoderState = 1;
			return 0;
		}
		// 16 bit Unicode Code Point
		if ((c & 0xF0) == 0xE0)
		{
			decoderBuffer = ((c & 0x0F) << 12);
			decoderState = 2;
			return 0;
		}
	}

	else
	{
		if (decoderState == 2)
		{
			decoderBuffer |= ((c & 0x3F) << 6);
			decoderState--;
			return 0;
		}

		else
		{
			decoderBuffer |= (c & 0x3F);
			decoderState = 0;
			return decoderBuffer;
		}
	}

	decoderState = 0;
	return c;
}

/***************************************************************************************
** Function name:           decodeUTF8
** Description:             Line buffer UTF-8 decoder with fall-back to extended ASCII
*************************************************************************************x*/
uint16_t TFTLIB_8BIT::decodeUTF8(uint8_t *buf, uint16_t *index, uint16_t remaining)
{
	uint16_t c = buf[(*index)++];
	// Serial.print("Byte from string = 0x"); Serial.println(c, HEX);

	// 7 bit Unicode
	if ((c & 0x80) == 0x00)
		return c;

	// 11 bit Unicode
	if (((c & 0xE0) == 0xC0) && (remaining > 1))
		return ((c & 0x1F) << 6) | (buf[(*index)++] & 0x3F);

	// 16 bit Unicode
	if (((c & 0xF0) == 0xE0) && (remaining > 2))
	{
		c = ((c & 0x0F) << 12) | ((buf[(*index)++] & 0x3F) << 6);
		return c | ((buf[(*index)++] & 0x3F));
	}

	return c; // fall-back to extended ASCII
}

int16_t TFTLIB_8BIT::textWidth(const char *string)
{
	return textWidth(string, textfont);
}

int16_t TFTLIB_8BIT::textWidth(const char *string, uint8_t font)
{
	int32_t str_width = 0;
	uint16_t uniCode = 0;

	if (gfxFont)
	{ // New font
		while (*string)
		{
			uniCode = decodeUTF8(*string++);
			if ((uniCode >= gfxFont->first) && (uniCode <= gfxFont->last))
			{
				uniCode -= gfxFont->first;
				GFXglyph *glyph = &gfxFont->glyph[uniCode];
				// If this is not the  last character or is a digit then use xAdvance
				if (*string || isDigits)
					str_width += glyph->xAdvance;
				// Else use the offset plus width since this can be bigger than xAdvance
				else
					str_width += (glyph->xOffset + glyph->width);
			}
		}
	}
	else
	{
		while (*string++)
			str_width += 6;
	}
	isDigits = false;
	return str_width * textsize;
}

/***************************************************************************************
** Function name:           getTextDatum
** Description:             Return the text datum value (as used by setTextDatum())
***************************************************************************************/
uint8_t TFTLIB_8BIT::getTextDatum(void)
{
	return textdatum;
}

/***************************************************************************************
** Function name:           getCursor
** Description:             Get the text cursor x & y position
***************************************************************************************/
void TFTLIB_8BIT::getCursor(int16_t *x, int16_t *y)
{
	*x = cursor_x;
	*y = cursor_y;
}

/***************************************************************************************
** Function name:           setTextPadding
** Description:             Define padding width (aids erasing old text and numbers)
***************************************************************************************/
uint16_t TFTLIB_8BIT::getTextPadding(void)
{
	return padX;
}

/***************************************************************************************
** Function name:           setCursor
** Description:             Set the text cursor x,y position
***************************************************************************************/
void TFTLIB_8BIT::setCursor(int16_t x, int16_t y)
{
	cursor_x = x;
	cursor_y = y + gfxFont->yAdvance;
}

/***************************************************************************************
** Function name:           setTextSize
** Description:             Set the text size multiplier
***************************************************************************************/
void TFTLIB_8BIT::setTextSize(uint8_t s)
{
	if (s > 7)
		s = 7;					// Limit the maximum size multiplier so byte variables can be used for rendering
	textsize = (s > 0) ? s : 1; // Don't allow font size 0
}

/***************************************************************************************
** Function name:           setFreeFont
** Descriptions:            Sets the GFX free font to use
***************************************************************************************/

void TFTLIB_8BIT::setFreeFont(const GFXfont *f)
{
	textfont = 1;
	gfxFont = (GFXfont *)f;

	glyph_ab = 0;
	glyph_bb = 0;
	uint16_t numChars = gfxFont->last - gfxFont->first;

	// Find the biggest above and below baseline offsets
	for (uint8_t c = 0; c < numChars; c++)
	{
		GFXglyph *glyph1 = &(gfxFont->glyph[c]);
		int8_t ab = -(glyph1->yOffset);
		if (ab > glyph_ab)
			glyph_ab = ab;
		int8_t bb = glyph1->height - ab;
		if (bb > glyph_bb)
			glyph_bb = bb;
	}
	setTextDatum(TL_DATUM);
	setCursor(0, 0);
}

/***************************************************************************************
** Function name:           setFont
** Description:             Set font by predefined ID for easier userspace access
***************************************************************************************/
void TFTLIB_8BIT::setFont(uint8_t fontId)
{
	switch (fontId)
	{
		case 0:  setFreeFont(&TomThumb); break;           // Tiny 5x6 pixel font (default)
		case 1:  setFreeFont(&Picopixel); break;          // Small 3x5 pixel font
		case 2:  setFreeFont(&FreeSans9pt7b); break;      // Clean sans-serif
		case 3:  setFreeFont(&FreeSans12pt7b); break;     // Larger sans-serif
		case 4:  setFreeFont(&FreeSansBold9pt7b); break;  // Bold sans-serif
		case 5:  setFreeFont(&FreeSansBold12pt7b); break; // Larger bold sans
		case 6:  setFreeFont(&FreeMono9pt7b); break;      // Monospace
		case 7:  setFreeFont(&FreeMono12pt7b); break;     // Larger monospace
		case 8:  setFreeFont(&Orbitron_Light_24); break;  // Futuristic display font
		case 9:  setFreeFont(&ArthurGothic8pt7b); break;  // Arthur Gothic 8pt
		case 10: setFreeFont(&ArthurGothic10pt7b); break; // Arthur Gothic 10pt
		case 11: setFreeFont(&ArthurGothic12pt7b); break; // Arthur Gothic 12pt
		default: setFreeFont(&TomThumb); break;           // Fallback to default
	}
}

/***************************************************************************************
** Function name:           setTextWrap
** Description:             Define if text should wrap at end of line
***************************************************************************************/
void TFTLIB_8BIT::setTextWrap(bool wrapX, bool wrapY)
{
	textwrapX = wrapX;
	textwrapY = wrapY;
}

/***************************************************************************************
** Function name:           setTextColor
** Description:             Set the font foreground and background colour
***************************************************************************************/
void TFTLIB_8BIT::setTextColor(uint32_t c, uint32_t b)
{
	textcolor = c;
	textbgcolor = b;
}

/***************************************************************************************
** Function name:           setTextDatum
** Description:             Set the text position reference datum
***************************************************************************************/
void TFTLIB_8BIT::setTextDatum(uint8_t d)
{
	textdatum = d;
}

/***************************************************************************************
** Function name:           setTextPadding
** Description:             Define padding width (aids erasing old text and numbers)
***************************************************************************************/
void TFTLIB_8BIT::setTextPadding(uint16_t x_width)
{
	padX = x_width;
}

int16_t TFTLIB_8BIT::fontHeight(int16_t font)
{
	return (gfxFont->yAdvance * textsize);
}

int16_t TFTLIB_8BIT::fontHeight(void)
{
	return fontHeight(textfont);
}

size_t TFTLIB_8BIT::write(uint8_t utf8)
{
	uint16_t uniCode = decodeUTF8(utf8);

	if (!uniCode)
		return 1;
	if (utf8 == '\r')
		return 1;

	if (uniCode == '\n')
		uniCode += 22; // Make it a valid space character to stop errors
	else if (uniCode < 32)
		return 1;

	uint16_t cheight = 0;

	cheight = cheight * textsize;

	if (utf8 == '\n')
	{
		cursor_x = 0;
		cursor_y += (int16_t)textsize * (uint8_t)(gfxFont->yAdvance);
	}

	else
	{
		if (uniCode > (gfxFont->last))
			return 1;
		if (uniCode < (gfxFont->first))
			return 1;

		uint16_t c2 = uniCode - (gfxFont->first);
		GFXglyph *glyph = &(((GFXglyph *)gfxFont->glyph)[c2]);
		uint8_t w = glyph->width,

				h = (glyph->height);

		if ((w > 0) && (h > 0))
		{ // Is there an associated bitmap?
			int16_t xo = (int8_t)(glyph->xOffset);
			if (textwrapX && ((cursor_x + textsize * (xo + w)) > width()))
			{
				// Drawing character would go off right edge; wrap to new line
				cursor_x = 0;
				cursor_y += (int16_t)textsize * (uint8_t)(gfxFont->yAdvance);
			}

			if (textwrapY && (cursor_y >= (int32_t)height()))
				cursor_y = 0;
			drawChar(cursor_x, cursor_y, uniCode, textcolor, textbgcolor, textsize);
		}
		cursor_x += (glyph->xAdvance) * (int16_t)textsize;
	}

	return 1;
}

/***************************************************************************************
** Function name:           drawChar
** Description:             draw a Unicode glyph onto the screen
***************************************************************************************/
// Any UTF-8 decoding must be done before calling drawChar()
int16_t TFTLIB_8BIT::drawChar(uint16_t uniCode, int32_t x, int32_t y)
{
	if (!uniCode)
		return 0;

	drawChar(x, y, uniCode, textcolor, textbgcolor, textsize);
	if ((uniCode >= gfxFont->first) && (uniCode <= gfxFont->last))
	{
		uint16_t c2 = uniCode - gfxFont->first;
		GFXglyph *glyph = &(gfxFont->glyph[c2]);
		return (glyph->xAdvance * textsize);
	}
	else
		return 0;

	if (((uniCode < 32) || (uniCode > 127)))
		return 0;

	int32_t width = 0;
	int32_t height = 0;
	uint32_t flash_address = 0;
	uniCode -= 32;
	int32_t w = width;
	int32_t pY = y;
	uint8_t line = 0;

	w *= height; // Now w is total number of pixels in the character
	if ((textsize != 1) || (textcolor == textbgcolor))
	{
		if (textcolor != textbgcolor)
			fillRect(x, pY, width * textsize, textsize * height, textbgcolor);
		int32_t px = 0, py = pY;		  // To hold character block start and end column and row values
		int32_t pc = 0;					  // Pixel count
		uint8_t np = textsize * textsize; // Number of pixels in a drawn pixel

		uint8_t tnp = 0;		   // Temporary copy of np for while loop
		uint8_t ts = textsize - 1; // Temporary copy of textsize
		// 16 bit pixel count so maximum font size is equivalent to 180x180 pixels in area
		// w is total number of pixels to plot to fill character block
		while (pc < w)
		{
			line = *((uint8_t *)flash_address);
			flash_address++;
			if (line & 0x80)
			{
				line &= 0x7F;
				line++;
				if (ts)
				{
					px = x + textsize * (pc % width); // Keep these px and py calculations outside the loop as they are slow
					py = y + textsize * (pc / width);
				}
				else
				{
					px = x + pc % width; // Keep these px and py calculations outside the loop as they are slow
					py = y + pc / width;
				}
				while (line--)
				{		  // In this case the while(line--) is faster
					pc++; // This is faster than putting pc+=line before while()?
					setWindow(px, py, px + ts, py + ts);

					if (ts)
					{
						tnp = np;
						while (tnp--)
						{
							write16(textcolor);
						}
					}
					else
						write16(textcolor);

					px += textsize;

					if (px >= (x + width * textsize))
					{
						px = x;
						py += textsize;
					}
				}
			}
			else
			{
				line++;
				pc += line;
			}
		}
	}
	else
	{
		// Text colour != background && textsize = 1 and character is within screen area
		// so use faster drawing of characters and background using block write
		if ((x >= 0) && (x + width <= _width) && (y >= 0) && (y + height <= _height))
		{
			setWindow(x, y, x + width - 1, y + height - 1);

			// Maximum font size is equivalent to 180x180 pixels in area
			while (w > 0)
			{
				line = *((uint8_t *)flash_address++); // 8 bytes smaller when incrementing here
				if (line & 0x80)
				{
					line &= 0x7F;
					line++;
					w -= line;
					pushBlock(textcolor, line);
				}
				else
				{
					line++;
					w -= line;
					pushBlock(textbgcolor, line);
				}
			}
		}
		else
		{
			int32_t px = x, py = y; // To hold character block start and end column and row values
			int32_t pc = 0;			// Pixel count
			int32_t pl = 0;			// Pixel line length
			uint32_t pcol = 0;		// Pixel color

			while (pc < w)
			{
				line = *((uint8_t *)flash_address);
				flash_address++;
				if (line & 0x80)
				{
					pcol = textcolor;
					line &= 0x7F;
				}
				else
					pcol = textbgcolor;
				line++;
				px = x + pc % width;
				py = y + pc / width;

				pl = 0;
				pc += line;
				while (line--)
				{ // In this case the while(line--) is faster
					pl++;
					if ((px + pl) >= (x + width))
					{
						drawFastHLine(px, py, pl, pcol);
						pl = 0;
						px = x;
						py++;
					}
				}
				if (pl)
					drawFastHLine(px, py, pl, pcol);
			}
		}
	}
	// End of RLE font rendering
	return width * textsize; // x +
}

void TFTLIB_8BIT::drawChar(int32_t x, int32_t y, uint16_t c, uint32_t color, uint32_t bg, uint8_t size)
{
	if ((x >= _width) ||			// Clip right
		(y >= _height) ||			// Clip bottom
		((x + 6 * size - 1) < 0) || // Clip left
		((y + 8 * size - 1) < 0))	// Clip top
		return;

	if (c < 32)
		return;
	if ((c >= (gfxFont->first)) && (c <= (gfxFont->last)))
	{
		c -= (gfxFont->first);
		GFXglyph *glyph = &(((GFXglyph *)gfxFont->glyph)[c]);
		uint8_t *bitmap = (uint8_t *)(gfxFont->bitmap);

		uint32_t bo = glyph->bitmapOffset;
		uint8_t w = glyph->width,
				h = glyph->height;

		int8_t xo = glyph->xOffset,
			   yo = glyph->yOffset;
		uint8_t xx, yy, bits = 0, bit = 0;
		int16_t xo16 = 0, yo16 = 0;

		if (bg != color && bg != 0xFFFFFFFF)
			fillRect(x + xo, y + yo, w, h, bg);

		if (size > 1)
		{
			xo16 = xo;
			yo16 = yo;
		}

		// GFXFF rendering speed up
		uint16_t hpc = 0; // Horizontal foreground pixel count
		for (yy = 0; yy < h; yy++)
		{
			for (xx = 0; xx < w; xx++)
			{
				if (bit == 0)
				{
					bits = bitmap[bo++];
					bit = 0x80;
				}

				if (bits & bit)
					hpc++;
				else
				{
					if (hpc)
					{
						if (size == 1)
							drawFastHLine(x + xo + xx - hpc, y + yo + yy, hpc, color);
						else
							fillRect(x + (xo16 + xx - hpc) * size, y + (yo16 + yy) * size, size * hpc, size, color);
						hpc = 0;
					}
				}
				bit >>= 1;
			}

			// Draw pixels for this line as we are about to increment yy
			if (hpc)
			{
				if (size == 1)
					drawFastHLine(x + xo + xx - hpc, y + yo + yy, hpc, color);
				else
					fillRect(x + (xo16 + xx - hpc) * size, y + (yo16 + yy) * size, size * hpc, size, color);
				hpc = 0;
			}
		}
	}
}

int16_t TFTLIB_8BIT::drawChar(uint16_t uniCode, int32_t x, int32_t y, float scale)
{
	uint8_t tempTextsize = textsize;
	textsize = (uint8_t)(textsize * scale);
	if (textsize < 1) textsize = 1; // Minimum size is 1
	int16_t result = drawChar(uniCode, x, y);
	textsize = tempTextsize;
	return result;
}

/***************************************************************************************
** Function name:           drawString (with or without user defined font)
** Description :            draw string with padding if it is defined
***************************************************************************************/
int16_t TFTLIB_8BIT::drawString(const String &string, int32_t poX, int32_t poY)
{
	int16_t len = string.length() + 2;
	char buffer[len];
	string.toCharArray(buffer, len);
	return drawString(buffer, poX, poY);
}

int16_t TFTLIB_8BIT::drawString(const String &string, int32_t poX, int32_t poY, float scale)
{
	int16_t len = string.length() + 2;
	char buffer[len];
	string.toCharArray(buffer, len);
	return drawString(buffer, poX, poY, scale);
}

int16_t TFTLIB_8BIT::drawString(const char *string, int32_t poX, int32_t poY)
{
	int16_t sumX = 0;
	uint8_t padding = 1, baseline = 0;
	uint16_t cwidth = textWidth(string, textfont); // Find the pixel width of the string in the font
	uint16_t cheight = 8 * textsize;

	cheight = glyph_ab * textsize;
	poY += cheight; // Adjust for baseline datum of free fonts
	baseline = cheight;
	padding = 101; // Different padding method used for Free Fonts

	// We need to make an adjustment for the bottom of the string (eg 'y' character)
	if ((textdatum == BL_DATUM) || (textdatum == BC_DATUM) || (textdatum == BR_DATUM))
	{
		cheight += glyph_bb * textsize;
	}

	baseline = gfxFont->yAdvance;
	cheight = fontHeight();

	if (textdatum || padX)
	{
		switch (textdatum)
		{
		case TC_DATUM:
			poX -= cwidth / 2;
			padding += 1;
			break;

		case TR_DATUM:
			poX -= cwidth;
			padding += 2;
			break;

		case ML_DATUM:
			poY -= cheight / 2;
			break;

		case MC_DATUM:
			poX -= cwidth / 2;
			poY -= cheight / 2;
			padding += 1;
			break;

		case MR_DATUM:
			poX -= cwidth;
			poY -= cheight / 2;
			padding += 2;
			break;

		case BL_DATUM:
			poY -= cheight;
			break;

		case BC_DATUM:
			poX -= cwidth / 2;
			poY -= cheight;
			padding += 1;
			break;

		case BR_DATUM:
			poX -= cwidth;
			poY -= cheight;
			padding += 2;
			break;

		case L_BASELINE:
			poY -= baseline;
			break;

		case C_BASELINE:
			poX -= cwidth / 2;
			poY -= baseline;
			padding += 1;
			break;

		case R_BASELINE:
			poX -= cwidth;
			poY -= baseline;
			padding += 2;
			break;
		}
		// Check coordinates are OK, adjust if not
		if (poX < 0)
			poX = 0;
		if (poX + cwidth > width())
			poX = width() - cwidth;
		if (poY < 0)
			poY = 0;
		if (poY + cheight - baseline > height())
			poY = height() - cheight;
	}

	int8_t xo = 0;

	if (textcolor != textbgcolor)
	{
		cheight = (glyph_ab + glyph_bb) * textsize;
		// Get the offset for the first character only to allow for negative offsets
		uint16_t c2 = 0;
		uint16_t len = strlen(string);
		uint16_t n = 0;

		while (n < len && c2 == 0)
			c2 = decodeUTF8((uint8_t *)string, &n, len - n);

		if ((c2 >= gfxFont->first) && (c2 <= gfxFont->last))
		{
			c2 -= gfxFont->first;
			GFXglyph *glyph = &(gfxFont->glyph[c2]);
			xo = (glyph->xOffset * textsize);
			// Adjust for negative xOffset
			if (xo > 0)
				xo = 0;
			else
				cwidth -= xo;
			// Add 1 pixel of padding all round
			// cheight +=2;
			// fillRect(poX+xo-1, poY - 1 - glyph_ab * textsize, cwidth+2, cheight, textbgcolor);
			fillRect(poX + xo, poY - glyph_ab * textsize, cwidth, cheight, textbgcolor);
		}
		padding -= 100;
	}

	uint16_t len = strlen(string);
	uint16_t n = 0;

	while (n < len)
	{
		uint16_t uniCode = decodeUTF8((uint8_t *)string, &n, len - n);
		sumX += drawChar(uniCode, poX + sumX, poY);
	}

	if ((padX > cwidth) && (textcolor != textbgcolor))
	{
		int16_t padXc = poX + cwidth + xo;
		poX += xo; // Adjust for negative offset start character
		poY -= glyph_ab * textsize;
		sumX += poX;

		switch (padding)
		{
		case 1:
			fillRect(padXc, poY, padX - cwidth, cheight, textbgcolor);
			break;

		case 2:
			fillRect(padXc, poY, (padX - cwidth) >> 1, cheight, textbgcolor);
			padXc = (padX - cwidth) >> 1;
			if (padXc > poX)
				padXc = poX;
			fillRect(poX - padXc, poY, (padX - cwidth) >> 1, cheight, textbgcolor);
			break;

		case 3:
			if (padXc > padX)
				padXc = padX;
			fillRect(poX + cwidth - padXc, poY, padXc - cwidth, cheight, textbgcolor);
			break;
		}
	}
	return sumX;
}

int16_t TFTLIB_8BIT::drawString(const char *string, int32_t poX, int32_t poY, float scale)
{
	uint8_t tempTextsize = textsize;
	textsize = (uint8_t)(textsize * scale);
	if (textsize < 1) textsize = 1; // Minimum size is 1
	int16_t result = drawString(string, poX, poY);
	textsize = tempTextsize;
	return result;
}

/***************************************************************************************
** Function name:           drawCentreString (deprecated, use setTextDatum())
** Descriptions:            draw string centred on dX
***************************************************************************************/
int16_t TFTLIB_8BIT::drawCentreString(const String &string, int32_t dX, int32_t poY)
{
	int16_t len = string.length() + 2;
	char buffer[len];
	string.toCharArray(buffer, len);
	return drawCentreString(buffer, dX, poY);
}

int16_t TFTLIB_8BIT::drawCentreString(const char *string, int32_t dX, int32_t poY)
{
	uint8_t tempdatum = textdatum;
	int32_t sumX = 0;
	textdatum = MC_DATUM;
	sumX = drawString(string, dX, poY);
	textdatum = tempdatum;
	return sumX;
}

int16_t TFTLIB_8BIT::drawCentreString(const char *string, int32_t dX, int32_t poY, float scale)
{
	uint8_t tempdatum = textdatum;
	int32_t sumX = 0;
	textdatum = MC_DATUM;
	sumX = drawString(string, dX, poY, scale);
	textdatum = tempdatum;
	return sumX;
}

int16_t TFTLIB_8BIT::drawCentreString(const String &string, int32_t dX, int32_t poY, float scale)
{
	int16_t len = string.length() + 2;
	char buffer[len];
	string.toCharArray(buffer, len);
	return drawCentreString(buffer, dX, poY, scale);
}

/***************************************************************************************
** Function name:           drawRightString
** Descriptions:            draw string right justified to dX
***************************************************************************************/
int16_t TFTLIB_8BIT::drawRightString(const String &string, int32_t dX, int32_t poY)
{
	int16_t len = string.length() + 2;
	char buffer[len];
	string.toCharArray(buffer, len);
	return drawRightString(buffer, dX, poY);
}

int16_t TFTLIB_8BIT::drawRightString(const char *string, int32_t dX, int32_t poY)
{
	uint8_t tempdatum = textdatum;
	int16_t sumX = 0;
	textdatum = TR_DATUM;
	sumX = drawString(string, dX, poY);
	textdatum = tempdatum;
	return sumX;
}

int16_t TFTLIB_8BIT::drawRightString(const char *string, int32_t dX, int32_t poY, float scale)
{
	uint8_t tempdatum = textdatum;
	int16_t sumX = 0;
	textdatum = TR_DATUM;
	sumX = drawString(string, dX, poY, scale);
	textdatum = tempdatum;
	return sumX;
}

int16_t TFTLIB_8BIT::drawRightString(const String &string, int32_t dX, int32_t poY, float scale)
{
	int16_t len = string.length() + 2;
	char buffer[len];
	string.toCharArray(buffer, len);
	return drawRightString(buffer, dX, poY, scale);
}

/***************************************************************************************
** Function name:           drawNumber
** Description:             draw a long integer
***************************************************************************************/
int16_t TFTLIB_8BIT::drawNumber(long long_num, int32_t poX, int32_t poY)
{
	isDigits = true; // Eliminate jiggle in monospaced fonts
	char str[12];
	itoa(long_num, str, 10);
	return drawString(str, poX, poY);
}

int16_t TFTLIB_8BIT::drawNumber(long long_num, int32_t poX, int32_t poY, float scale)
{
	isDigits = true; // Eliminate jiggle in monospaced fonts
	char str[12];
	itoa(long_num, str, 10);
	return drawString(str, poX, poY, scale);
}

/***************************************************************************************
** Function name:           drawFloat
** Descriptions:            drawFloat, prints 7 non zero digits maximum
***************************************************************************************/
// Assemble and print a string, this permits alignment relative to a datum
// looks complicated but much more compact and actually faster than using print class
int16_t TFTLIB_8BIT::drawFloat(float floatNumber, uint8_t dp, int32_t poX, int32_t poY)
{
	isDigits = true;
	char str[14];		  // Array to contain decimal string
	uint8_t ptr = 0;	  // Initialise pointer for array
	int8_t digits = 1;	  // Count the digits to avoid array overflow
	float rounding = 0.5; // Round up down delta

	if (dp > 7)
		dp = 7; // Limit the size of decimal portion

	// Adjust the rounding value
	for (uint8_t i = 0; i < dp; ++i)
		rounding /= 10.0;

	if (floatNumber < -rounding)
	{								// add sign, avoid adding - sign to 0.0!
		str[ptr++] = '-';			// Negative number
		str[ptr] = 0;				// Put a null in the array as a precaution
		digits = 0;					// Set digits to 0 to compensate so pointer value can be used later
		floatNumber = -floatNumber; // Make positive
	}

	floatNumber += rounding; // Round up or down

	if (floatNumber >= 2147483647)
	{
		strcpy(str, "...");
		return drawString(str, poX, poY);
	}
	// No chance of overflow from here on

	// Get integer part
	uint32_t temp = (uint32_t)floatNumber;

	// Put integer part into array
	itoa(temp, str + ptr, 10);

	// Find out where the null is to get the digit count loaded
	while ((uint8_t)str[ptr] != 0)
		ptr++;	   // Move the pointer along
	digits += ptr; // Count the digits

	str[ptr++] = '.'; // Add decimal point
	str[ptr] = '0';	  // Add a dummy zero
	str[ptr + 1] = 0; // Add a null but don't increment pointer so it can be overwritten

	// Get the decimal portion
	floatNumber = floatNumber - temp;

	// Get decimal digits one by one and put in array
	// Limit digit count so we don't get a false sense of resolution
	uint8_t i = 0;
	while ((i < dp) && (digits < 9))
	{ // while (i < dp) for no limit but array size must be increased
		i++;
		floatNumber *= 10;	// for the next decimal
		temp = floatNumber; // get the decimal
		itoa(temp, str + ptr, 10);
		ptr++;
		digits++;			 // Increment pointer and digits count
		floatNumber -= temp; // Remove that digit
	}

	// Finally we can plot the string and return pixel length
	return drawString(str, poX, poY);
}

/***************************************************************************************************************************
** 												Test functions for display benchmark
****************************************************************************************************************************/
uint32_t TFTLIB_8BIT::testFillScreen()
{
	unsigned long start = GetTick();
	fillScreen(palette.BLACK);
	fillScreen(palette.RED);
	fillScreen(palette.GREEN);
	fillScreen(palette.BLUE);
	fillScreen(palette.BLACK);
	return ((GetTick() - start) / 5);
}

uint32_t TFTLIB_8BIT::testText()
{
	fillScreen(palette.BLACK);
	setFreeFont(&FreeSerif9pt7b);
	unsigned long start = GetTick();
	setTextColor(palette.WHITE);
	setTextSize(1);
	setTextDatum(TL_DATUM);
	drawString("Hello World!", 0, 0);

	setTextColor(palette.YELLOW);
	setTextSize(2);
	drawFloat(1234.56, 2, 0, 16);

	setCursor(0, 48);
	setTextColor(palette.RED);
	setTextSize(3);
	println(0xDEADBEEF, HEX);

	setCursor(0, 96);
	setTextColor(palette.GREEN);
	setTextSize(5);
	println("Groop");

	setCursor(0, 176);
	setTextSize(2);
	println("I implore thee,");

	setCursor(0, 192);
	setTextSize(1);
	println("my foonting turlingdromes.");
	println("And hooptiously drangle me");
	println("with crinkly bindlewurdles,");
	println("Or I will rend thee");
	println("in the gobberwarts");
	println("with my blurglecruncheon,");
	println("see if I don't!");
	setFreeFont(&SegoeScript8pt7b);
	return (GetTick() - start);
}

uint32_t TFTLIB_8BIT::testLines(uint32_t color)
{
	unsigned long start, t;
	int x1, y1, x2, y2,
		w = _width,
		h = _height;

	fillScreen(palette.BLACK);

	x1 = y1 = 0;
	y2 = h - 1;
	start = GetTick();
	for (x2 = 0; x2 < w; x2 += 6)
		drawLine(x1, y1, x2, y2, color);
	x2 = w - 1;
	for (y2 = 0; y2 < h; y2 += 6)
		drawLine(x1, y1, x2, y2, color);
	t = (GetTick() - start); // fillScreen doesn't count against timing

	fillScreen(palette.BLACK);

	x1 = w - 1;
	y1 = 0;
	y2 = h - 1;
	start = GetTick();
	for (x2 = 0; x2 < w; x2 += 6)
		drawLine(x1, y1, x2, y2, color);
	x2 = 0;
	for (y2 = 0; y2 < h; y2 += 6)
		drawLine(x1, y1, x2, y2, color);
	t += (GetTick() - start);

	fillScreen(palette.BLACK);

	x1 = 0;
	y1 = h - 1;
	y2 = 0;
	start = GetTick();
	for (x2 = 0; x2 < w; x2 += 6)
		drawLine(x1, y1, x2, y2, color);
	x2 = w - 1;
	for (y2 = 0; y2 < h; y2 += 6)
		drawLine(x1, y1, x2, y2, color);
	t += (GetTick() - start);

	fillScreen(palette.BLACK);

	x1 = w - 1;
	y1 = h - 1;
	y2 = 0;
	start = GetTick();
	for (x2 = 0; x2 < w; x2 += 6)
		drawLine(x1, y1, x2, y2, color);
	x2 = 0;
	for (y2 = 0; y2 < h; y2 += 6)
		drawLine(x1, y1, x2, y2, color);

	return ((GetTick() - start) / 4);
}

uint32_t TFTLIB_8BIT::testFastLines(uint32_t color1, uint32_t color2)
{
	unsigned long start;
	int x = 0, y = 0, w = _width, h = _height;

	fillScreen(palette.BLACK);
	start = GetTick();
	for (y = 0; y < h; y += 5)
		drawFastHLine(0, y, w, color1);
	for (x = 0; x < w; x += 5)
		drawFastVLine(x, 0, h, color2);

	return (GetTick() - start);
}

uint32_t TFTLIB_8BIT::testRects(uint32_t color)
{
	unsigned long start;
	int n, i, i2,
		cx = _width / 2,
		cy = _height / 2;

	fillScreen(palette.BLACK);
	n = min(_width, _height);
	start = GetTick();
	for (i = 2; i < n; i += 10)
	{
		i2 = i / 2;
		drawRect(cx - i2, cy - i2, i, i, color);
	}

	return (GetTick() - start);
}

uint32_t TFTLIB_8BIT::testFilledRects(uint32_t color1, uint32_t color2)
{
	unsigned long start, t = 0;
	int n, i, i2,
		cx = _width / 2 - 1,
		cy = _height / 2 - 1;

	fillScreen(palette.BLACK);
	n = min(_width, _height);
	for (i = n - 1; i > 0; i -= 6)
	{
		i2 = i / 2;
		start = GetTick();
		fillRect(cx - i2, cy - i2, i, i, color1);
		t += (GetTick() - start);
		// Outlines are not included in timing results
		drawRect(cx - i2, cy - i2, i, i, color2);
	}
	return t;
}

uint32_t TFTLIB_8BIT::testFilledCircles(uint8_t radius, uint32_t color)
{
	unsigned long start;
	int x, y, w = _width, h = _height, r2 = radius * 2;

	fillScreen(palette.BLACK);
	start = GetTick();
	for (x = radius; x < w; x += r2)
	{
		for (y = radius; y < h; y += r2)
		{
			fillCircle(x, y, radius, color);
		}
	}
	return (GetTick() - start);
}

uint32_t TFTLIB_8BIT::testCircles(uint8_t radius, uint32_t color)
{
	unsigned long start;
	int x, y, r2 = radius * 2,
			  w = _width + radius,
			  h = _height + radius;

	// Screen is not cleared for this one -- this is
	// intentional and does not affect the reported time.
	start = GetTick();
	for (x = 0; x < w; x += r2)
	{
		for (y = 0; y < h; y += r2)
		{
			drawCircle(x, y, radius, color);
		}
	}

	return (GetTick() - start);
}

uint32_t TFTLIB_8BIT::testTriangles()
{
	unsigned long start;
	int n, i, cx = _width / 2 - 1,
			  cy = _height / 2 - 1;

	fillScreen(palette.BLACK);
	n = min(cx, cy);
	start = GetTick();

	for (i = 0; i < n; i += 5)
	{
		drawTriangle(
			cx, cy - i,		// peak
			cx - i, cy + i, // bottom left
			cx + i, cy + i, // bottom right
			i);
	}

	return (GetTick() - start);
}

uint32_t TFTLIB_8BIT::testFilledTriangles()
{
	unsigned long start, t = 0;
	int i, cx = _width / 2 - 1,
		   cy = _height / 2 - 1;

	fillScreen(palette.BLACK);
	start = GetTick();
	for (i = min(cx, cy); i > 10; i -= 5)
	{
		start = GetTick();
		fillTriangle(cx, cy - i, cx - i, cy + i, cx + i, cy + i, i);
		t += (GetTick() - start);
		drawTriangle(cx, cy - i, cx - i, cy + i, cx + i, cy + i, i);
	}

	return t;
}

uint32_t TFTLIB_8BIT::testRoundRects()
{
	uint32_t start;
	int w, i, i2,
		cx = width() / 2 - 1,
		cy = height() / 2 - 1;

	fillScreen(palette.BLACK);
	w = min(width(), height());
	start = GetTick();

	for (i = 0; i < w; i += 6)
	{
		i2 = i / 2;
		drawRoundRect(cx - i2, cy - i2, i, i, i / 8, color565(i, 0, 0));
	}

	return (GetTick() - start);
}

uint32_t TFTLIB_8BIT::testFilledRoundRects()
{
	uint32_t start;
	int i, i2,
		cx = width() / 2 - 1,
		cy = height() / 2 - 1;

	fillScreen(palette.BLACK);
	start = GetTick();

	for (i = min(width(), height()); i > 20; i -= 6)
	{
		i2 = i / 2;
		fillRoundRect(cx - i2, cy - i2, i, i, i / 8, color565(0, i, 0));
	}

	return (GetTick() - start);
}

/***************************************************************************************
** Function name:           setPivot
** Description:             Set the pivot point on the TFT
*************************************************************************************x*/
void TFTLIB_8BIT::setPivot(int16_t x, int16_t y)
{
	_xPivot = x;
	_yPivot = y;
}

/***************************************************************************************
** Function name:           getPivotX
** Description:             Get the x pivot position
***************************************************************************************/
int16_t TFTLIB_8BIT::getPivotX(void)
{
	return _xPivot;
}

/***************************************************************************************
** Function name:           getPivotY
** Description:             Get the y pivot position
***************************************************************************************/
int16_t TFTLIB_8BIT::getPivotY(void)
{
	return _yPivot;
}

/***************************************************************************************
** Function name:           setViewport
** Description:             Set the clipping region for the TFT screen
***************************************************************************************/
void TFTLIB_8BIT::setViewport(int32_t x, int32_t y, int32_t w, int32_t h, bool vpDatum)
{
	// Viewport metrics (not clipped)
	_xDatum = x;  // Datum x position in screen coordinates
	_yDatum = y;  // Datum y position in screen coordinates
	_xWidth = w;  // Viewport width
	_yHeight = h; // Viewport height

	// Full size default viewport
	_vpDatum = false; // Datum is at top left corner of screen (true = top left of viewport)
	_vpX = 0;		  // Viewport top left corner x coordinate
	_vpY = 0;		  // Viewport top left corner y coordinate
	_vpW = width();	  // Equivalent of TFT width  (Nb: viewport right edge coord + 1)
	_vpH = height();  // Equivalent of TFT height (Nb: viewport bottom edge coord + 1)

	// Clip viewport to screen area
	if (x < 0)
	{
		w += x;
		x = 0;
	}
	if (y < 0)
	{
		h += y;
		y = 0;
	}
	if ((x + w) > width())
	{
		w = width() - x;
	}
	if ((y + h) > height())
	{
		h = height() - y;
	}

	// Check if viewport is entirely out of bounds
	if (w < 1 || h < 1)
	{
		// Set default values and Out of Bounds flag in case of error
		_xDatum = 0;
		_yDatum = 0;
		_xWidth = width();
		_yHeight = height();
		return;
	}

	if (!vpDatum)
	{
		_xDatum = 0; // Reset to top left of screen if not using a viewport datum
		_yDatum = 0;
		_xWidth = width();
		_yHeight = height();
	}

	// Store the clipped screen viewport metrics and datum position
	_vpX = x;
	_vpY = y;
	_vpW = x + w;
	_vpH = y + h;
	_vpDatum = vpDatum;
}

/***************************************************************************************
** Function name:           resetViewport
** Description:             Reset viewport to whole TFT screen, datum at 0,0
***************************************************************************************/
void TFTLIB_8BIT::resetViewport(void)
{
	// Reset viewport to the whole screen (or sprite) area
	_vpDatum = false;
	_xDatum = 0;
	_yDatum = 0;
	_vpX = 0;
	_vpY = 0;
	_vpW = width();
	_vpH = height();
	_xWidth = width();
	_yHeight = height();
}

ButtonWidget::ButtonWidget(TFTLIB_8BIT *disp)
{
	_tft = disp;
	_xd = 0;
	_yd = 0;
	_textdatum = MC_DATUM;
	_label[9] = '\0';
	_currstate = false;
	_laststate = false;
	_inverted = false;
}

void ButtonWidget::setPressAction(actionCallback action)
{
	pressAction = action;
}

void ButtonWidget::setReleaseAction(actionCallback action)
{
	releaseAction = action;
}

// Classic initButton() function: pass center & size
void ButtonWidget::initButton(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t outline, uint16_t fill, uint16_t textcolor, char *label, uint8_t textsize)
{
	// Tweak arguments and pass to the newer initButtonUL() function...
	initButtonUL(x - (w / 2), y - (h / 2), w, h, outline, fill, textcolor, label, textsize);
}

// Newer function instead accepts upper-left corner & size
void ButtonWidget::initButtonUL(int16_t x1, int16_t y1, uint16_t w, uint16_t h, uint16_t outline, uint16_t fill, uint16_t textcolor, char *label, uint8_t textsize)
{
	_x1 = x1;
	_y1 = y1;
	_w = w;
	_h = h;
	_outlinecolor = outline;
	_outlinewidth = 2;
	_fillcolor = fill;
	_textcolor = textcolor;
	_textsize = textsize;
	strncpy(_label, label, 9);
	_pressTime = 0xFFFFFFFF;
	_releaseTime = 0xFFFFFFFF;
}

// Adjust text datum and x, y deltas
void ButtonWidget::setLabelDatum(int16_t x_delta, int16_t y_delta, uint8_t datum)
{
	_xd = x_delta;
	_yd = y_delta;
	_textdatum = datum;
}

void ButtonWidget::drawButton(bool inverted, String long_name)
{
	uint16_t fill, outline, text;

	_inverted = inverted;

	if (!inverted)
	{
		fill = _fillcolor;
		outline = _outlinecolor;
		text = _textcolor;
	}
	else
	{
		fill = _textcolor;
		outline = _outlinecolor;
		text = _fillcolor;
	}

	uint8_t r = min(_w, _h) / 4; // Corner radius
	_tft->fillRoundRect(_x1, _y1, _w, _h, r, fill);
	_tft->drawRoundRect(_x1, _y1, _w, _h, r, outline);

	if (_tft->textfont == 255)
	{
		_tft->setCursor(_x1 + (_w / 8),
						_y1 + (_h / 4));
		_tft->setTextColor(text);
		_tft->setTextSize(_textsize);
		_tft->print(_label);
	}
	else
	{
		_tft->setTextColor(text, fill);
		_tft->setTextSize(_textsize);

		uint8_t tempdatum = _tft->getTextDatum();
		_tft->setTextDatum(_textdatum);
		uint16_t tempPadding = _tft->getTextPadding();
		_tft->setTextPadding(0);

		if (long_name == "")
			_tft->drawString(_label, _x1 + (_w / 2) + _xd, _y1 + (_h / 2) - 4 + _yd);
		else
			_tft->drawString(long_name, _x1 + (_w / 2) + _xd, _y1 + (_h / 2) - 4 + _yd);

		_tft->setTextDatum(tempdatum);
		_tft->setTextPadding(tempPadding);
	}
}

void ButtonWidget::drawSmoothButton(bool inverted, int16_t outlinewidth, uint32_t bgcolor, String long_name)
{
	uint16_t fill, outline, text;
	if (bgcolor != 0x00FFFFFF)
		_bgcolor = bgcolor;
	if (outlinewidth >= 0)
		_outlinewidth = outlinewidth;
	_inverted = inverted;

	if (!inverted)
	{
		fill = _fillcolor;
		outline = _outlinecolor;
		text = _textcolor;
	}
	else
	{
		fill = _textcolor;
		outline = _outlinecolor;
		text = _fillcolor;
	}

	uint8_t r = min(_w, _h) / 4; // Corner radius
	if (outlinewidth > 0)
		_tft->fillSmoothRoundRect(_x1, _y1, _w, _h, r, outline, _bgcolor);
	_tft->fillSmoothRoundRect(_x1 + _outlinewidth, _y1 + _outlinewidth, _w - (2 * _outlinewidth), _h - (2 * _outlinewidth), r - _outlinewidth, fill, outline);

	if (_tft->textfont == 255)
	{
		_tft->setCursor(_x1 + (_w / 8),
						_y1 + (_h / 4));
		_tft->setTextColor(text);
		_tft->setTextSize(_textsize);
		_tft->print(_label);
	}
	else
	{
		_tft->setTextColor(text, fill);
		_tft->setTextSize(_textsize);

		uint8_t tempdatum = _tft->getTextDatum();
		_tft->setTextDatum(_textdatum);
		uint16_t tempPadding = _tft->getTextPadding();
		_tft->setTextPadding(0);

		if (long_name == "")
			_tft->drawString(_label, _x1 + (_w / 2) + _xd, _y1 + (_h / 2) - 4 + _yd);
		else
			_tft->drawString(long_name, _x1 + (_w / 2) + _xd, _y1 + (_h / 2) - 4 + _yd);

		_tft->setTextDatum(tempdatum);
		_tft->setTextPadding(tempPadding);
	}
}

bool ButtonWidget::contains(int16_t x, int16_t y)
{
	return ((x >= _x1) && (x < (_x1 + _w)) &&
			(y >= _y1) && (y < (_y1 + _h)));
}

void ButtonWidget::press(bool p)
{
	_laststate = _currstate;
	_currstate = p;
}

bool ButtonWidget::isPressed() { return _currstate; }
bool ButtonWidget::justPressed() { return (_currstate && !_laststate); }
bool ButtonWidget::justReleased() { return (!_currstate && _laststate); }

/***************************************************************************************
** Function name:           TFT_eSprite
** Description:             Class constructor
***************************************************************************************/
TFT_eSprite::TFT_eSprite(TFTLIB_8BIT *tft)
{
	_tft = tft; // Pointer to tft class so we can call member functions

	_iwidth = 0; // Initialise width and height to 0 (it does not exist yet)
	_iheight = 0;
	_bpp = 16;

	_created = false;

	_xs = 0; // window bounds for pushColor
	_ys = 0;
	_xe = 0;
	_ye = 0;

	_xptr = 0; // pushColor coordinate
	_yptr = 0;

	_colorMap = nullptr;
}

/***************************************************************************************
** Function name:           createSprite
** Description:             Create a sprite (bitmap) of defined width and height
***************************************************************************************/
// cast returned value to (uint8_t*) for 8 bit or (uint16_t*) for 16 bit colours
void *TFT_eSprite::createSprite(int16_t w, int16_t h, uint8_t frames)
{

	if (_created)
		return _img8_1;

	if (w < 1 || h < 1)
		return nullptr;

	_iwidth = _dwidth = _bitwidth = w;
	_iheight = _dheight = h;

	cursor_x = 0;
	cursor_y = 0;

	// Default scroll rectangle and gap fill colour
	_sx = 0;
	_sy = 0;
	_sw = w;
	_sh = h;
	_scolor = palette.BLACK;

	_img8 = (uint8_t *)callocSprite(w, h, frames);
	_img8_1 = _img8;
	_img8_2 = _img8;
	_img = (uint16_t *)_img8;
	_img4 = _img8;

	if ((_bpp == 16) && (frames > 1))
	{
		_img8_2 = _img8 + (w * h * 2 + 1);
	}

	if ((_bpp == 8) && (frames > 1))
	{
		_img8_2 = _img8 + (w * h + 1);
	}

	if ((_bpp == 4) && (_colorMap == nullptr))
		createPalette(default_4bit_palette);

	// This is to make it clear what pointer size is expected to be used
	// but casting in the user sketch is needed due to the use of void*
	if ((_bpp == 1) && (frames > 1))
	{
		w = (w + 7) & 0xFFF8;
		_img8_2 = _img8 + ((w >> 3) * h + 1);
	}

	if (_img8)
	{
		_created = true;
		_rotation = 0;
		setViewport(0, 0, _dwidth, _dheight);
		setPivot(_iwidth / 2, _iheight / 2);
		return _img8_1;
	}

	return nullptr;
}

/***************************************************************************************
** Function name:           getPointer
** Description:             Returns pointer to start of sprite memory area
***************************************************************************************/
void *TFT_eSprite::getPointer(void)
{
	if (!_created)
		return nullptr;
	return _img8_1;
}

/***************************************************************************************
** Function name:           created
** Description:             Returns true if sprite has been created
***************************************************************************************/
bool TFT_eSprite::created(void)
{
	return _created;
}

/***************************************************************************************
** Function name:           ~TFT_eSprite
** Description:             Class destructor
***************************************************************************************/
TFT_eSprite::~TFT_eSprite(void)
{
	deleteSprite();
}

/***************************************************************************************
** Function name:           callocSprite
** Description:             Allocate a memory area for the Sprite and return pointer
***************************************************************************************/
void *TFT_eSprite::callocSprite(int16_t w, int16_t h, uint8_t frames)
{
	// Add one extra "off screen" pixel to point out-of-bounds setWindow() coordinates
	// this means push/writeColor functions do not need additional bounds checks and
	// hence will run faster in normal circumstances.
	uint8_t *ptr8 = nullptr;

	if (frames > 2)
		frames = 2; // Currently restricted to 2 frame buffers
	if (frames < 1)
		frames = 1;

	if (_bpp == 16)
	{
		ptr8 = (uint8_t *)calloc(frames * w * h + frames, sizeof(uint16_t));
	}

	else if (_bpp == 8)
	{
		ptr8 = (uint8_t *)calloc(frames * w * h + frames, sizeof(uint8_t));
	}

	else if (_bpp == 4)
	{
		w = (w + 1) & 0xFFFE; // width needs to be multiple of 2, with an extra "off screen" pixel
		_iwidth = w;
		ptr8 = (uint8_t *)calloc(((frames * w * h) >> 1) + frames, sizeof(uint8_t));
	}

	else // Must be 1 bpp
	{
		//_dwidth   Display width+height in pixels always in rotation 0 orientation
		//_dheight  Not swapped for sprite rotations
		// Note: for 1bpp _iwidth and _iheight are swapped during Sprite rotations

		w = (w + 7) & 0xFFF8; // width should be the multiple of 8 bits to be compatible with epdpaint
		_iwidth = w;		  // _iwidth is rounded up to be multiple of 8, so might not be = _dwidth
		_bitwidth = w;		  // _bitwidth will not be rotated whereas _iwidth may be

		ptr8 = (uint8_t *)calloc(frames * (w >> 3) * h + frames, sizeof(uint8_t));
	}

	return ptr8;
}

/***************************************************************************************
** Function name:           createPalette (from RAM array)
** Description:             Set a palette for a 4-bit per pixel sprite
***************************************************************************************/
void TFT_eSprite::createPalette(uint16_t colorMap[], uint8_t colors)
{
	if (_colorMap != nullptr)
	{
		free(_colorMap);
	}

	if (colorMap == nullptr)
	{
		// Create a color map using the default FLASH map
		createPalette(default_4bit_palette);
		return;
	}

	// Allocate and clear memory for 16 color map
	_colorMap = (uint16_t *)calloc(16, sizeof(uint16_t));

	if (colors > 16)
		colors = 16;

	// Copy map colors
	for (uint8_t i = 0; i < colors; i++)
	{
		_colorMap[i] = colorMap[i];
	}
}

/***************************************************************************************
** Function name:           createPalette (from FLASH array)
** Description:             Set a palette for a 4-bit per pixel sprite
***************************************************************************************/
void TFT_eSprite::createPalette(const uint16_t colorMap[], uint8_t colors)
{
	if (colorMap == nullptr)
	{
		// Create a color map using the default FLASH map
		colorMap = default_4bit_palette;
	}

	// Allocate and clear memory for 16 color map
	_colorMap = (uint16_t *)calloc(16, sizeof(uint16_t));

	if (colors > 16)
		colors = 16;

	// Copy map colors
	for (uint8_t i = 0; i < colors; i++)
	{
		_colorMap[i] = *(uint32_t *)(colorMap++);
	}
}

/***************************************************************************************
** Function name:           frameBuffer
** Description:             For 1 bpp Sprites, select the frame used for graphics
***************************************************************************************/
// Frames are numbered 1 and 2
void *TFT_eSprite::frameBuffer(int8_t f)
{
	if (!_created)
		return nullptr;

	if (f == 2)
		_img8 = _img8_2;
	else
		_img8 = _img8_1;

	if (_bpp == 16)
		_img = (uint16_t *)_img8;

	if (_bpp == 4)
		_img4 = _img8;

	return _img8;
}

/***************************************************************************************
** Function name:           setColorDepth
** Description:             Set bits per pixel for colour (1, 8 or 16)
***************************************************************************************/
void *TFT_eSprite::setColorDepth(int8_t b)
{
	// Do not re-create the sprite if the colour depth does not change
	if (_bpp == b)
		return _img8_1;

	// Validate the new colour depth
	if (b > 8)
		_bpp = 16; // Bytes per pixel
	else if (b > 4)
		_bpp = 8;
	else if (b > 1)
		_bpp = 4;
	else
		_bpp = 1;

	// Can't change an existing sprite's colour depth so delete it
	if (_created)
		free(_img8_1);

	// If it existed, re-create the sprite with the new colour depth
	if (_created)
	{
		_created = false;
		return createSprite(_dwidth, _dheight);
	}

	return nullptr;
}

/***************************************************************************************
** Function name:           getColorDepth
** Description:             Get bits per pixel for colour (1, 8 or 16)
***************************************************************************************/
int8_t TFT_eSprite::getColorDepth(void)
{
	if (_created)
		return _bpp;
	else
		return 0;
}

/***************************************************************************************
** Function name:           setBitmapColor
** Description:             Set the 1bpp foreground foreground and background colour
***************************************************************************************/
void TFT_eSprite::setBitmapColor(uint16_t c, uint16_t b)
{
	if (c == b)
		b = ~c;
	_tft->bitmap_fg = c;
	_tft->bitmap_bg = b;
}

/***************************************************************************************
** Function name:           setPaletteColor
** Description:             Set the 4bpp palette color at the given index
***************************************************************************************/
void TFT_eSprite::setPaletteColor(uint8_t index, uint16_t color)
{
	if (_colorMap == nullptr || index > 15)
		return; // out of bounds

	_colorMap[index] = color;
}

/***************************************************************************************
** Function name:           getPaletteColor
** Description:             Return the palette color at 4bpp index, or 0 on error.
***************************************************************************************/
uint16_t TFT_eSprite::getPaletteColor(uint8_t index)
{
	if (_colorMap == nullptr || index > 15)
		return 0; // out of bounds

	return _colorMap[index];
}

/***************************************************************************************
** Function name:           deleteSprite
** Description:             Delete the sprite to free up memory (RAM)
***************************************************************************************/
void TFT_eSprite::deleteSprite(void)
{
	if (_colorMap != nullptr)
	{
		free(_colorMap);
		_colorMap = nullptr;
	}

	if (_created)
	{
		free(_img8_1);
		_img8 = nullptr;
		_created = false;
	}
}

/***************************************************************************************
** Function name:           pushRotated - Fast fixed point integer maths version
** Description:             Push rotated Sprite to TFT screen
***************************************************************************************/
#define FP_SCALE 10
bool TFT_eSprite::pushRotated(int16_t angle, uint32_t transp)
{
	if (!_created)
		return false;

	// Bounding box parameters
	int16_t min_x;
	int16_t min_y;
	int16_t max_x;
	int16_t max_y;

	// Get the bounding box of this rotated source Sprite relative to Sprite pivot
	if (!getRotatedBounds(angle, &min_x, &min_y, &max_x, &max_y))
		return false;

	uint16_t sline_buffer[max_x - min_x + 1];

	int32_t xt = min_x - _tft->_xPivot;
	int32_t yt = min_y - _tft->_yPivot;
	uint32_t xe = _dwidth << FP_SCALE;
	uint32_t ye = _dheight << FP_SCALE;
	uint16_t tpcolor = (uint16_t)transp;

	if (transp != 0x00FFFFFF)
	{
		if (_bpp == 4)
			tpcolor = _colorMap[transp & 0x0F];
		tpcolor = tpcolor >> 8 | tpcolor << 8; // Working with swapped color bytes
	}

	// Scan destination bounding box and fetch transformed pixels from source Sprite
	for (int32_t y = min_y; y <= max_y; y++, yt++)
	{
		int32_t x = min_x;
		uint32_t xs = (_cosra * xt - (_sinra * yt - (_xPivot << FP_SCALE)) + (1 << (FP_SCALE - 1)));
		uint32_t ys = (_sinra * xt + (_cosra * yt + (_yPivot << FP_SCALE)) + (1 << (FP_SCALE - 1)));

		while ((xs >= xe || ys >= ye) && x < max_x)
		{
			x++;
			xs += _cosra;
			ys += _sinra;
		}
		if (x == max_x)
			continue;

		uint32_t pixel_count = 0;
		do
		{
			uint32_t rp;
			int32_t xp = xs >> FP_SCALE;
			int32_t yp = ys >> FP_SCALE;
			if (_bpp == 16)
			{
				rp = _img[xp + yp * _iwidth];
			}
			else
			{
				rp = readPixel(xp, yp);
				rp = (uint16_t)(rp >> 8 | rp << 8);
			}
			if (transp != 0x00FFFFFF && tpcolor == rp)
			{
				if (pixel_count)
				{
					// TFT window is already clipped, so this is faster than pushImage()
					_tft->setWindow(x - pixel_count, y, x - 1, y);
					_tft->pushPixels16(sline_buffer, pixel_count);
					pixel_count = 0;
				}
			}
			else
			{
				sline_buffer[pixel_count++] = rp;
			}
		} while (++x < max_x && (xs += _cosra) < xe && (ys += _sinra) < ye);
		if (pixel_count)
		{
			// TFT window is already clipped, so this is faster than pushImage()
			_tft->setWindow(x - pixel_count, y, x - 1, y);
			_tft->pushPixels16(sline_buffer, pixel_count);
		}
	}

	return true;
}

/***************************************************************************************
** Function name:           pushRotated - Fast fixed point integer maths version
** Description:             Push a rotated copy of the Sprite to another Sprite
***************************************************************************************/
// Not compatible with 4bpp
bool TFT_eSprite::pushRotated(TFT_eSprite *spr, int16_t angle, uint32_t transp)
{
	if (!_created || _bpp == 4)
		return false; // Check this Sprite is created
	if (!spr->_created || spr->_bpp == 4)
		return false; // Ckeck destination Sprite is created

	// Bounding box parameters
	int16_t min_x;
	int16_t min_y;
	int16_t max_x;
	int16_t max_y;

	// Get the bounding box of this rotated source Sprite
	if (!getRotatedBounds(spr, angle, &min_x, &min_y, &max_x, &max_y))
		return false;

	uint16_t sline_buffer[max_x - min_x + 1];

	int32_t xt = min_x - spr->_xPivot;
	int32_t yt = min_y - spr->_yPivot;
	uint32_t xe = _dwidth << FP_SCALE;
	uint32_t ye = _dheight << FP_SCALE;
	uint16_t tpcolor = (uint16_t)transp;

	if (transp != 0x00FFFFFF)
	{
		if (_bpp == 4)
			tpcolor = _colorMap[transp & 0x0F];
		tpcolor = tpcolor >> 8 | tpcolor << 8; // Working with swapped color bytes
	}

	// Scan destination bounding box and fetch transformed pixels from source Sprite
	for (int32_t y = min_y; y <= max_y; y++, yt++)
	{
		int32_t x = min_x;
		uint32_t xs = (_cosra * xt - (_sinra * yt - (_xPivot << FP_SCALE)) + (1 << (FP_SCALE - 1)));
		uint32_t ys = (_sinra * xt + (_cosra * yt + (_yPivot << FP_SCALE)) + (1 << (FP_SCALE - 1)));

		while ((xs >= xe || ys >= ye) && x < max_x)
		{
			x++;
			xs += _cosra;
			ys += _sinra;
		}
		if (x == max_x)
			continue;

		uint32_t pixel_count = 0;
		do
		{
			uint32_t rp;
			int32_t xp = xs >> FP_SCALE;
			int32_t yp = ys >> FP_SCALE;
			if (_bpp == 16)
				rp = _img[xp + yp * _iwidth];
			else
			{
				rp = readPixel(xp, yp);
				rp = (uint16_t)(rp >> 8 | rp << 8);
			}
			if (transp != 0x00FFFFFF && tpcolor == rp)
			{
				if (pixel_count)
				{
					spr->pushImage(x - pixel_count, y, pixel_count, 1, sline_buffer);
					pixel_count = 0;
				}
			}
			else
			{
				sline_buffer[pixel_count++] = rp;
			}
		} while (++x < max_x && (xs += _cosra) < xe && (ys += _sinra) < ye);
		if (pixel_count)
			spr->pushImage(x - pixel_count, y, pixel_count, 1, sline_buffer);
	}
	return true;
}

/***************************************************************************************
** Function name:           getRotatedBounds
** Description:             Get TFT bounding box of a rotated Sprite wrt pivot
***************************************************************************************/
bool TFT_eSprite::getRotatedBounds(int16_t angle, int16_t *min_x, int16_t *min_y,
								   int16_t *max_x, int16_t *max_y)
{
	// Get the bounding box of this rotated source Sprite relative to Sprite pivot
	getRotatedBounds(angle, width(), height(), _xPivot, _yPivot, min_x, min_y, max_x, max_y);

	// Move bounding box so source Sprite pivot coincides with TFT pivot
	*min_x += _tft->_xPivot;
	*max_x += _tft->_xPivot;
	*min_y += _tft->_yPivot;
	*max_y += _tft->_yPivot;

	// Return if bounding box is outside of TFT viewport
	if (*min_x > _tft->_vpW)
		return false;
	if (*min_y > _tft->_vpH)
		return false;
	if (*max_x < _tft->_vpX)
		return false;
	if (*max_y < _tft->_vpY)
		return false;

	// Clip bounding box to be within TFT viewport
	if (*min_x < _tft->_vpX)
		*min_x = _tft->_vpX;
	if (*min_y < _tft->_vpY)
		*min_y = _tft->_vpY;
	if (*max_x > _tft->_vpW)
		*max_x = _tft->_vpW;
	if (*max_y > _tft->_vpH)
		*max_y = _tft->_vpH;

	return true;
}

/***************************************************************************************
** Function name:           getRotatedBounds
** Description:             Get destination Sprite bounding box of a rotated Sprite wrt pivot
***************************************************************************************/
bool TFT_eSprite::getRotatedBounds(TFT_eSprite *spr, int16_t angle, int16_t *min_x, int16_t *min_y,
								   int16_t *max_x, int16_t *max_y)
{
	// Get the bounding box of this rotated source Sprite relative to Sprite pivot
	getRotatedBounds(angle, width(), height(), _xPivot, _yPivot, min_x, min_y, max_x, max_y);

	// Move bounding box so source Sprite pivot coincides with destination Sprite pivot
	*min_x += spr->_xPivot;
	*max_x += spr->_xPivot;
	*min_y += spr->_yPivot;
	*max_y += spr->_yPivot;

	// Test only to show bounding box
	// spr->fillSprite(TFT_BLACK);
	// spr->drawRect(min_x, min_y, max_x - min_x + 1, max_y - min_y + 1, TFT_GREEN);

	// Return if bounding box is completely outside of destination Sprite
	if (*min_x > spr->width())
		return true;
	if (*min_y > spr->height())
		return true;
	if (*max_x < 0)
		return true;
	if (*max_y < 0)
		return true;

	// Clip bounding box to Sprite boundaries
	// Clipping to a viewport will be done by destination Sprite pushImage function
	if (*min_x < 0)
		min_x = 0;
	if (*min_y < 0)
		min_y = 0;
	if (*max_x > spr->width())
		*max_x = spr->width();
	if (*max_y > spr->height())
		*max_y = spr->height();

	return true;
}

/***************************************************************************************
** Function name:           rotatedBounds
** Description:             Get bounding box of a rotated Sprite wrt pivot
***************************************************************************************/
void TFT_eSprite::getRotatedBounds(int16_t angle, int16_t w, int16_t h, int16_t xp, int16_t yp,
								   int16_t *min_x, int16_t *min_y, int16_t *max_x, int16_t *max_y)
{
	// Trig values for the rotation
	float radAngle = -angle * 0.0174532925; // Convert degrees to radians
	float sina = sin(radAngle);
	float cosa = cos(radAngle);

	w -= xp; // w is now right edge coordinate relative to xp
	h -= yp; // h is now bottom edge coordinate relative to yp

	// Calculate new corner coordinates
	int16_t x0 = -xp * cosa - yp * sina;
	int16_t y0 = xp * sina - yp * cosa;

	int16_t x1 = w * cosa - yp * sina;
	int16_t y1 = -w * sina - yp * cosa;

	int16_t x2 = h * sina + w * cosa;
	int16_t y2 = h * cosa - w * sina;

	int16_t x3 = h * sina - xp * cosa;
	int16_t y3 = h * cosa + xp * sina;

	// Find bounding box extremes, enlarge box to accomodate rounding errors
	*min_x = x0 - 2;
	if (x1 < *min_x)
		*min_x = x1 - 2;
	if (x2 < *min_x)
		*min_x = x2 - 2;
	if (x3 < *min_x)
		*min_x = x3 - 2;

	*max_x = x0 + 2;
	if (x1 > *max_x)
		*max_x = x1 + 2;
	if (x2 > *max_x)
		*max_x = x2 + 2;
	if (x3 > *max_x)
		*max_x = x3 + 2;

	*min_y = y0 - 2;
	if (y1 < *min_y)
		*min_y = y1 - 2;
	if (y2 < *min_y)
		*min_y = y2 - 2;
	if (y3 < *min_y)
		*min_y = y3 - 2;

	*max_y = y0 + 2;
	if (y1 > *max_y)
		*max_y = y1 + 2;
	if (y2 > *max_y)
		*max_y = y2 + 2;
	if (y3 > *max_y)
		*max_y = y3 + 2;

	_sinra = round(sina * (1 << FP_SCALE));
	_cosra = round(cosa * (1 << FP_SCALE));
}

/***************************************************************************************
** Function name:           pushSprite
** Description:             Push the sprite to the TFT at x, y
***************************************************************************************/
void TFT_eSprite::pushSprite(int32_t x, int32_t y)
{
	if (!_created)
		return;

	if (_bpp == 16)
	{
		_tft->pushImage(x, y, _dwidth, _dheight, _img);
	}
	else if (_bpp == 4)
	{
		_tft->pushImage(x, y, _dwidth, _dheight, _img4, false, _colorMap);
	}
	else
		_tft->pushImage(x, y, _dwidth, _dheight, _img8, (bool)(_bpp == 8));
}

/***************************************************************************************
** Function name:           pushSprite
** Description:             Push the sprite to the TFT at x, y with transparent colour
***************************************************************************************/
void TFT_eSprite::pushSprite(int32_t x, int32_t y, uint16_t transp)
{
	if (!_created)
		return;

	if (_bpp == 16)
	{
		_tft->pushImage(x, y, _dwidth, _dheight, _img, transp);
	}
	else if (_bpp == 8)
	{
		transp = (uint8_t)((transp & 0xE000) >> 8 | (transp & 0x0700) >> 6 | (transp & 0x0018) >> 3);
		_tft->pushImage(x, y, _dwidth, _dheight, _img8, (uint8_t)transp, (bool)true);
	}
	else if (_bpp == 4)
	{
		_tft->pushImage(x, y, _dwidth, _dheight, _img4, (uint8_t)(transp & 0x0F), false, _colorMap);
	}
	else
		_tft->pushImage(x, y, _dwidth, _dheight, _img8, 0, (bool)false);
}

/***************************************************************************************
** Function name:           pushToSprite
** Description:             Push the sprite to another sprite at x, y
***************************************************************************************/
// Note: The following sprite to sprite colour depths are currently supported:
//    Source    Destination
//    16bpp  -> 16bpp
//    16bpp  ->  8bpp
//     8bpp  ->  8bpp
//     4bpp  ->  4bpp (note: color translation depends on the 2 sprites palette colors)
//     1bpp  ->  1bpp (note: color translation depends on the 2 sprites bitmap colors)

bool TFT_eSprite::pushToSprite(TFT_eSprite *dspr, int32_t x, int32_t y)
{
	if (!_created)
		return false;
	if (!dspr->created())
		return false;

	// Check destination sprite compatibility
	int8_t ds_bpp = dspr->getColorDepth();
	if (_bpp == 16 && ds_bpp != 16 && ds_bpp != 8)
		return false;
	if (_bpp == 8 && ds_bpp != 8)
		return false;
	if (_bpp == 4 && ds_bpp != 4)
		return false;
	if (_bpp == 1 && ds_bpp != 1)
		return false;

	dspr->pushImage(x, y, _dwidth, _dheight, _img, _bpp);

	return true;
}

/***************************************************************************************
** Function name:           pushToSprite
** Description:             Push the sprite to another sprite at x, y with transparent colour
***************************************************************************************/
// Note: The following sprite to sprite colour depths are currently supported:
//    Source    Destination
//    16bpp  -> 16bpp
//    16bpp  ->  8bpp
//     8bpp  ->  8bpp
//     1bpp  ->  1bpp

bool TFT_eSprite::pushToSprite(TFT_eSprite *dspr, int32_t x, int32_t y, uint16_t transp)
{
	if (!_created || !dspr->_created)
		return false; // Check Sprites exist

	// Check destination sprite compatibility
	int8_t ds_bpp = dspr->getColorDepth();
	if (_bpp == 16 && ds_bpp != 16 && ds_bpp != 8)
		return false;
	if (_bpp == 8 && ds_bpp != 8)
		return false;
	if (_bpp == 4 || ds_bpp == 4)
		return false;
	if (_bpp == 1 && ds_bpp != 1)
		return false;

	uint16_t sline_buffer[width()];

	transp = transp >> 8 | transp << 8;

	// Scan destination bounding box and fetch transformed pixels from source Sprite
	for (int32_t ys = 0; ys < height(); ys++)
	{
		int32_t ox = x;
		uint32_t pixel_count = 0;

		for (int32_t xs = 0; xs < width(); xs++)
		{
			uint16_t rp = 0;
			if (_bpp == 16)
				rp = _img[xs + ys * width()];
			else
			{
				rp = readPixel(xs, ys);
				rp = rp >> 8 | rp << 8;
			}
			// dspr->drawPixel(xs, ys, rp);

			if (transp == rp)
			{
				if (pixel_count)
				{
					dspr->pushImage(ox, y, pixel_count, 1, sline_buffer, _bpp);
					ox += pixel_count;
					pixel_count = 0;
				}
				ox++;
			}
			else
			{
				sline_buffer[pixel_count++] = rp;
			}
		}
		if (pixel_count)
			dspr->pushImage(ox, y, pixel_count, 1, sline_buffer);
		y++;
	}
	return true;
}

/***************************************************************************************
** Function name:           pushSprite
** Description:             Push a cropped sprite to the TFT at tx, ty
***************************************************************************************/
bool TFT_eSprite::pushSprite(int32_t tx, int32_t ty, int32_t sx, int32_t sy, int32_t sw, int32_t sh)
{
	if (!_created)
		return false;

	// Perform window boundary checks and crop if needed
	setWindow(sx, sy, sx + sw - 1, sy + sh - 1);

	/* These global variables are now populated for the sprite
	_xs = x start coordinate
	_ys = y start coordinate
	_xe = x end coordinate (inclusive)
	_ye = y end coordinate (inclusive)
	*/

	// Calculate new sprite window bounding box width and height
	sw = _xe - _xs + 1;
	sh = _ye - _ys + 1;

	if (_ys >= _iheight)
		return false;

	if (_bpp == 16)
	{
		// Check if a faster block copy to screen is possible
		if (sx == 0 && sw == _dwidth)
			_tft->pushImage(tx, ty, sw, sh, _img + _iwidth * _ys);
		else // Render line by line
			while (sh--)
				_tft->pushImage(tx, ty++, sw, 1, _img + _xs + _iwidth * _ys++);
	}
	else if (_bpp == 8)
	{
		// Check if a faster block copy to screen is possible
		if (sx == 0 && sw == _dwidth)
			_tft->pushImage(tx, ty, sw, sh, _img8 + _iwidth * _ys, (bool)true);
		else // Render line by line
			while (sh--)
				_tft->pushImage(tx, ty++, sw, 1, _img8 + _xs + _iwidth * _ys++, (bool)true);
	}
	else if (_bpp == 4)
	{
		// Check if a faster block copy to screen is possible
		if (sx == 0 && sw == _dwidth)
			_tft->pushImage(tx, ty, sw, sh, _img4 + (_iwidth >> 1) * _ys, false, _colorMap);
		else // Render line by line
		{
			int32_t ds = _xs & 1; // Odd x start pixel

			int32_t de = 0; // Odd x end pixel
			if ((sw > ds) && (_xe & 1))
				de = 1;

			uint32_t dm = 0; // Midsection pixel count
			if (sw > (ds + de))
				dm = sw - ds - de;
			sw--;

			uint32_t yp = (_xs + ds + _iwidth * _ys) >> 1;

			while (sh--)
			{
				if (ds)
					_tft->drawPixel(tx, ty, readPixel(_xs, _ys));
				if (dm)
					_tft->pushImage(tx + ds, ty, dm, 1, _img4 + yp, false, _colorMap);
				if (de)
					_tft->drawPixel(tx + sw, ty, readPixel(_xe, _ys));
				_ys++;
				ty++;
				yp += (_iwidth >> 1);
			}
		}
	}
	else // 1bpp
	{
		// Check if a faster block copy to screen is possible
		if (sx == 0 && sw == _dwidth)
			_tft->pushImage(tx, ty, sw, sh, _img8 + (_bitwidth >> 3) * _ys, (bool)false);
		else // Render line by line
		{

			while (sh--)
			{
				_tft->pushImage(tx, ty++, sw, 1, _img8 + (_bitwidth >> 3) * _ys++, (bool)false);
			}
		}
	}

	return true;
}

/***************************************************************************************
** Function name:           readPixelValue
** Description:             Read the color map index of a pixel at defined coordinates
***************************************************************************************/
uint16_t TFT_eSprite::readPixelValue(int32_t x, int32_t y)
{
	if (!_created)
		return 0xFF;

	x += _xDatum;
	y += _yDatum;

	// Range checking
	if ((x < _vpX) || (y < _vpY) || (x >= _vpW) || (y >= _vpH))
		return 0xFF;

	if (_bpp == 16)
	{
		// Return the pixel colour
		return readPixel(x - _xDatum, y - _yDatum);
	}

	if (_bpp == 8)
	{
		// Return the pixel byte value
		return _img8[x + y * _iwidth];
	}

	if (_bpp == 4)
	{
		if (x >= _dwidth)
			return 0xFF;
		if ((x & 0x01) == 0)
			return _img4[((x + y * _iwidth) >> 1)] >> 4; // even index = bits 7 .. 4
		else
			return _img4[((x + y * _iwidth) >> 1)] & 0x0F; // odd index = bits 3 .. 0.
	}

	if (_bpp == 1)
	{
		// Note: _dwidth and _dheight bounds not checked (rounded up -iwidth and _iheight used)
		if (_rotation == 1)
		{
			uint16_t tx = x;
			x = _dheight - y - 1;
			y = tx;
		}
		else if (_rotation == 2)
		{
			x = _dwidth - x - 1;
			y = _dheight - y - 1;
		}
		else if (_rotation == 3)
		{
			uint16_t tx = x;
			x = y;
			y = _dwidth - tx - 1;
		}
		// Return 1 or 0
		return (_img8[(x + y * _bitwidth) >> 3] >> (7 - (x & 0x7))) & 0x01;
	}

	return 0;
}

/***************************************************************************************
** Function name:           readPixel
** Description:             Read 565 colour of a pixel at defined coordinates
***************************************************************************************/
uint32_t TFT_eSprite::readPixel(int32_t x, int32_t y)
{
	if (!_created)
		return 0xFFFF;

	x += _xDatum;
	y += _yDatum;

	// Range checking
	if ((x < _vpX) || (y < _vpY) || (x >= _vpW) || (y >= _vpH))
		return 0xFFFF;

	if (_bpp == 16)
	{
		uint16_t color = _img[x + y * _iwidth];
		return (color >> 8) | (color << 8);
	}

	if (_bpp == 8)
	{
		uint16_t color = _img8[x + y * _iwidth];
		if (color != 0)
		{
			uint8_t blue[] = {0, 11, 21, 31};
			color = (color & 0xE0) << 8 | (color & 0xC0) << 5 | (color & 0x1C) << 6 | (color & 0x1C) << 3 | blue[color & 0x03];
		}
		return color;
	}

	if (_bpp == 4)
	{
		if (x >= _dwidth)
			return 0xFFFF;
		uint16_t color;
		if ((x & 0x01) == 0)
			color = _colorMap[_img4[((x + y * _iwidth) >> 1)] >> 4]; // even index = bits 7 .. 4
		else
			color = _colorMap[_img4[((x + y * _iwidth) >> 1)] & 0x0F]; // odd index = bits 3 .. 0.
		return color;
	}

	// Note: Must be 1bpp
	// _dwidth and _dheight bounds not checked (rounded up -iwidth and _iheight used)
	if (_rotation == 1)
	{
		uint16_t tx = x;
		x = _dheight - y - 1;
		y = tx;
	}
	else if (_rotation == 2)
	{
		x = _dwidth - x - 1;
		y = _dheight - y - 1;
	}
	else if (_rotation == 3)
	{
		uint16_t tx = x;
		x = y;
		y = _dwidth - tx - 1;
	}

	uint16_t color = (_img8[(x + y * _bitwidth) >> 3] << (x & 0x7)) & 0x80;

	if (color)
		return _tft->bitmap_fg;
	else
		return _tft->bitmap_bg;
}

/***************************************************************************************
** Function name:           pushImage
** Description:             push image into a defined area of a sprite
***************************************************************************************/
void TFT_eSprite::pushImage(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t *data, uint8_t sbpp)
{
	if (data == nullptr || !_created)
		return;

	PI_CLIP;

	if (_bpp == 16) // Plot a 16 bpp image into a 16 bpp Sprite
	{
		// Pointer within original image
		uint8_t *ptro = (uint8_t *)data + ((dx + dy * w) << 1);
		// Pointer within sprite image
		uint8_t *ptrs = (uint8_t *)_img + ((x + y * _iwidth) << 1);

		while (dh--)
		{
			memcpy(ptrs, ptro, dw << 1);
			ptro += w << 1;
			ptrs += _iwidth << 1;
		}
	}
	else if (_bpp == 8 && sbpp == 8) // Plot a 8 bpp image into a 8 bpp Sprite
	{
		// Pointer within original image
		uint8_t *ptro = (uint8_t *)data + (dx + dy * w);
		// Pointer within sprite image
		uint8_t *ptrs = (uint8_t *)_img + (x + y * _iwidth);

		while (dh--)
		{
			memcpy(ptrs, ptro, dw);
			ptro += w;
			ptrs += _iwidth;
		}
	}
	else if (_bpp == 8) // Plot a 16 bpp image into a 8 bpp Sprite
	{
		uint16_t lastColor = 0;
		uint8_t color8 = 0;
		for (int32_t yp = dy; yp < dy + dh; yp++)
		{
			int32_t xyw = x + y * _iwidth;
			int32_t dxypw = dx + yp * w;
			for (int32_t xp = dx; xp < dx + dw; xp++)
			{
				uint16_t color = data[dxypw++];
				if (color != lastColor)
				{
					// When data source is a sprite, the bytes are already swapped
					color8 = (uint8_t)((color & 0xE0) | (color & 0x07) << 2 | (color & 0x1800) >> 11);
				}
				lastColor = color;
				_img8[xyw++] = color8;
			}
			y++;
		}
	}
	else if (_bpp == 4)
	{
		// The image is assumed to be 4 bit, where each byte corresponds to two pixels.
		// much faster when aligned to a byte boundary, because the alternative is slower, requiring
		// tedious bit operations.

		int sWidth = (_iwidth >> 1);
		uint8_t *ptr = (uint8_t *)data;

		if ((x & 0x01) == 0 && (dx & 0x01) == 0 && (dw & 0x01) == 0)
		{
			x = (x >> 1) + y * sWidth;
			dw = (dw >> 1);
			dx = (dx >> 1) + dy * (w >> 1);
			while (dh--)
			{
				memcpy(_img4 + x, ptr + dx, dw);
				dx += (w >> 1);
				x += sWidth;
			}
		}
		else // not optimized
		{
			for (int32_t yp = dy; yp < dy + dh; yp++)
			{
				int32_t ox = x;
				for (int32_t xp = dx; xp < dx + dw; xp++)
				{
					uint32_t color;
					if ((xp & 0x01) == 0)
						color = (ptr[((xp + yp * w) >> 1)] & 0xF0) >> 4; // even index = bits 7 .. 4
					else
						color = ptr[((xp - 1 + yp * w) >> 1)] & 0x0F; // odd index = bits 3 .. 0.
					drawPixel(ox, y, color);
					ox++;
				}
				y++;
			}
		}
	}

	else // 1bpp
	{
		// Plot a 1bpp image into a 1bpp Sprite
		uint32_t ww = (w + 7) >> 3; // Width of source image line in bytes
		uint8_t *ptr = (uint8_t *)data;
		for (int32_t yp = dy; yp < dy + dh; yp++)
		{
			uint32_t yw = yp * ww; // Byte starting the line containing source pixel
			int32_t ox = x;
			for (int32_t xp = dx; xp < dx + dw; xp++)
			{
				uint16_t readPixel = (ptr[(xp >> 3) + yw] & (0x80 >> (xp & 0x7)));
				drawPixel(ox++, y, readPixel);
			}
			y++;
		}
	}
}

/***************************************************************************************
** Function name:           pushImage
** Description:             push 565 colour FLASH (PROGMEM) image into a defined area
***************************************************************************************/
void TFT_eSprite::pushImage(int32_t x, int32_t y, int32_t w, int32_t h, const uint16_t *data)
{
	pushImage(x, y, w, h, (uint16_t *)data);
	// Partitioned memory FLASH processor
	/*if (data == nullptr || !_created) return;

	PI_CLIP;

	if (_bpp == 16) // Plot a 16 bpp image into a 16 bpp Sprite
	{
	  for (int32_t yp = dy; yp < dy + dh; yp++)
	  {
		int32_t ox = x;
		for (int32_t xp = dx; xp < dx + dw; xp++)
		{
		  uint16_t color = *(uint32_t*)(data + xp + yp * w);
		  _img[ox + y * _iwidth] = color;
		  ox++;
		}
		y++;
	  }
	}

	else if (_bpp == 8) // Plot a 16 bpp image into a 8 bpp Sprite
	{
	  for (int32_t yp = dy; yp < dy + dh; yp++)
	  {
		int32_t ox = x;
		for (int32_t xp = dx; xp < dx + dw; xp++)
		{
		  uint16_t color = *(uint32_t*)(data + xp + yp * w);
		  _img8[ox + y * _iwidth] = (uint8_t)((color & 0xE000)>>8 | (color & 0x0700)>>6 | (color & 0x0018)>>3);
		  ox++;
		}
		y++;
	  }
	}

	else if (_bpp == 4)
	{
	  return;
	}

	else // Plot a 1bpp image into a 1bpp Sprite
	{
	  x-= _xDatum;   // Remove offsets, drawPixel will add
	  y-= _yDatum;
	  uint16_t bsw =  (w+7) >> 3; // Width in bytes of source image line
	  uint8_t *ptr = ((uint8_t*)data) + dy * bsw;

	  while (dh--) {
		int32_t odx = dx;
		int32_t ox  = x;
		while (odx < dx + dw) {
		  uint8_t pbyte = *(uint8_t*)(ptr + (odx>>3));
		  uint8_t mask = 0x80 >> (odx & 7);
		  while (mask) {
			uint8_t p = pbyte & mask;
			mask = mask >> 1;
			drawPixel(ox++, y, p);
			odx++;
		  }
		}
		ptr += bsw;
		y++;
	  }
	}*/
}

/***************************************************************************************
** Function name:           setWindow
** Description:             Set the bounds of a window in the sprite
***************************************************************************************/
// Intentionally not constrained to viewport area, does not manage 1bpp rotations
void TFT_eSprite::setWindow(int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
	if (x0 > x1)
		swap_coord(x0, x1);
	if (y0 > y1)
		swap_coord(y0, y1);

	int32_t w = width();
	int32_t h = height();

	if ((x0 >= w) || (x1 < 0) || (y0 >= h) || (y1 < 0))
	{ // Point to that extra "off screen" pixel
		_xs = 0;
		_ys = _dheight;
		_xe = 0;
		_ye = _dheight;
	}
	else
	{
		if (x0 < 0)
			x0 = 0;
		if (x1 >= w)
			x1 = w - 1;
		if (y0 < 0)
			y0 = 0;
		if (y1 >= h)
			y1 = h - 1;

		_xs = x0;
		_ys = y0;
		_xe = x1;
		_ye = y1;
	}

	_xptr = _xs;
	_yptr = _ys;
}

/***************************************************************************************
** Function name:           pushColor
** Description:             Send a new pixel to the set window
***************************************************************************************/
void TFT_eSprite::pushColor(uint16_t color)
{
	if (!_created)
		return;

	// Write the colour to RAM in set window
	if (_bpp == 16)
		_img[_xptr + _yptr * _iwidth] = (uint16_t)(color >> 8) | (color << 8);

	else if (_bpp == 8)
		_img8[_xptr + _yptr * _iwidth] = (uint8_t)((color & 0xE000) >> 8 | (color & 0x0700) >> 6 | (color & 0x0018) >> 3);

	else if (_bpp == 4)
	{
		uint8_t c = (uint8_t)color & 0x0F;
		if ((_xptr & 0x01) == 0)
		{
			_img4[(_xptr + _yptr * _iwidth) >> 1] = (c << 4) | (_img4[(_xptr + _yptr * _iwidth) >> 1] & 0x0F); // new color is in bits 7 .. 4
		}
		else
		{
			_img4[(_xptr + _yptr * _iwidth) >> 1] = (_img4[(_xptr + _yptr * _iwidth) >> 1] & 0xF0) | c; // new color is the low bits
		}
	}

	else
		drawPixel(_xptr, _yptr, color);

	// Increment x
	_xptr++;

	// Wrap on x and y to start, increment y if needed
	if (_xptr > _xe)
	{
		_xptr = _xs;
		_yptr++;
		if (_yptr > _ye)
			_yptr = _ys;
	}
}

/***************************************************************************************
** Function name:           pushColor
** Description:             Send a "len" new pixels to the set window
***************************************************************************************/
void TFT_eSprite::pushColor(uint16_t color, uint32_t len)
{
	if (!_created)
		return;

	uint16_t pixelColor;

	if (_bpp == 16)
		pixelColor = (uint16_t)(color >> 8) | (color << 8);

	else if (_bpp == 8)
		pixelColor = (color & 0xE000) >> 8 | (color & 0x0700) >> 6 | (color & 0x0018) >> 3;

	else
		pixelColor = (uint16_t)color; // for 1bpp or 4bpp

	while (len--)
		writeColor(pixelColor);
}

/***************************************************************************************
** Function name:           writeColor
** Description:             Write a pixel with pre-formatted colour to the set window
***************************************************************************************/
void TFT_eSprite::writeColor(uint16_t color)
{
	if (!_created)
		return;

	// Write 16 bit RGB 565 encoded colour to RAM
	if (_bpp == 16)
		_img[_xptr + _yptr * _iwidth] = color;

	// Write 8 bit RGB 332 encoded colour to RAM
	else if (_bpp == 8)
		_img8[_xptr + _yptr * _iwidth] = (uint8_t)color;

	else if (_bpp == 4)
	{
		uint8_t c = (uint8_t)color & 0x0F;
		if ((_xptr & 0x01) == 0)
			_img4[(_xptr + _yptr * _iwidth) >> 1] = (c << 4) | (_img4[(_xptr + _yptr * _iwidth) >> 1] & 0x0F); // new color is in bits 7 .. 4
		else
			_img4[(_xptr + _yptr * _iwidth) >> 1] = (_img4[(_xptr + _yptr * _iwidth) >> 1] & 0xF0) | c; // new color is the low bits (x is odd)
	}

	else
		drawPixel(_xptr, _yptr, color);

	// Increment x
	_xptr++;

	// Wrap on x and y to start, increment y if needed
	if (_xptr > _xe)
	{
		_xptr = _xs;
		_yptr++;
		if (_yptr > _ye)
			_yptr = _ys;
	}
}

/***************************************************************************************
** Function name:           setScrollRect
** Description:             Set scroll area within the sprite and the gap fill colour
***************************************************************************************/
// Intentionally not constrained to viewport area
void TFT_eSprite::setScrollRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color)
{
	if ((x >= _iwidth) || (y >= _iheight) || !_created)
		return;

	if (x < 0)
	{
		w += x;
		x = 0;
	}
	if (y < 0)
	{
		h += y;
		y = 0;
	}

	if ((x + w) > _iwidth)
		w = _iwidth - x;
	if ((y + h) > _iheight)
		h = _iheight - y;

	if (w < 1 || h < 1)
		return;

	_sx = x;
	_sy = y;
	_sw = w;
	_sh = h;

	_scolor = color;
}

/***************************************************************************************
** Function name:           scroll
** Description:             Scroll dx,dy pixels, positive right,down, negative left,up
***************************************************************************************/
void TFT_eSprite::scroll(int16_t dx, int16_t dy)
{
	if ((uint32_t)abs(dx) >= _sw || (uint32_t)abs(dy) >= _sh)
	{
		fillRect(_sx, _sy, _sw, _sh, _scolor);
		return;
	}

	// Fetch the scroll area width and height set by setScrollRect()
	uint32_t w = _sw - abs(dx); // line width to copy
	uint32_t h = _sh - abs(dy); // lines to copy
	int32_t iw = _iwidth;		// rounded up width of sprite

	// Fetch the x,y origin set by setScrollRect()
	uint32_t tx = _sx; // to x
	uint32_t fx = _sx; // from x
	uint32_t ty = _sy; // to y
	uint32_t fy = _sy; // from y

	// Adjust for x delta
	if (dx <= 0)
		fx -= dx;
	else
		tx += dx;

	// Adjust for y delta
	if (dy <= 0)
		fy -= dy;
	else
	{					   // Scrolling down so start copy from bottom
		ty = ty + _sh - 1; // "To" pointer
		iw = -iw;		   // Pointer moves backwards
		fy = ty - dy;	   // "From" pointer
	}

	// Calculate "from y" and "to y" pointers in RAM
	uint32_t fyp = fx + fy * _iwidth;
	uint32_t typ = tx + ty * _iwidth;

	// Now move the pixels in RAM
	if (_bpp == 16)
	{
		while (h--)
		{ // move pixel lines (to, from, byte count)
			memmove(_img + typ, _img + fyp, w << 1);
			typ += iw;
			fyp += iw;
		}
	}
	else if (_bpp == 8)
	{
		while (h--)
		{ // move pixel lines (to, from, byte count)
			memmove(_img8 + typ, _img8 + fyp, w);
			typ += iw;
			fyp += iw;
		}
	}
	else if (_bpp == 4)
	{
		// could optimize for scrolling by even # pixels using memove (later)
		if (dx > 0)
		{
			tx += w;
			fx += w;
		} // Start from right edge
		while (h--)
		{ // move pixels one by one
			for (uint16_t xp = 0; xp < w; xp++)
			{
				if (dx <= 0)
					drawPixel(tx + xp, ty, readPixelValue(fx + xp, fy));
				if (dx > 0)
					drawPixel(tx - xp, ty, readPixelValue(fx - xp, fy));
			}
			if (dy <= 0)
			{
				ty++;
				fy++;
			}
			else
			{
				ty--;
				fy--;
			}
		}
	}
	else if (_bpp == 1)
	{
		if (dx > 0)
		{
			tx += w;
			fx += w;
		} // Start from right edge
		while (h--)
		{ // move pixels one by one
			for (uint16_t xp = 0; xp < w; xp++)
			{
				if (dx <= 0)
					drawPixel(tx + xp, ty, readPixelValue(fx + xp, fy));
				if (dx > 0)
					drawPixel(tx - xp, ty, readPixelValue(fx - xp, fy));
			}
			if (dy <= 0)
			{
				ty++;
				fy++;
			}
			else
			{
				ty--;
				fy--;
			}
		}
	}
	else
		return; // Not 1, 4, 8 or 16 bpp

	// Fill the gap left by the scrolling
	if (dx > 0)
		fillRect(_sx, _sy, dx, _sh, _scolor);
	if (dx < 0)
		fillRect(_sx + _sw + dx, _sy, -dx, _sh, _scolor);
	if (dy > 0)
		fillRect(_sx, _sy, _sw, dy, _scolor);
	if (dy < 0)
		fillRect(_sx, _sy + _sh + dy, _sw, -dy, _scolor);
}

/***************************************************************************************
** Function name:           fillSprite
** Description:             Fill the whole sprite with defined colour
***************************************************************************************/
void TFT_eSprite::fillSprite(uint32_t color)
{
	if (!_created)
		return;

	// Use memset if possible as it is super fast
	if (_xDatum == 0 && _yDatum == 0 && _xWidth == width())
	{
		if (_bpp == 16)
		{
			if ((uint8_t)color == (uint8_t)(color >> 8))
			{
				memset(_img, (uint8_t)color, _iwidth * _yHeight * 2);
			}
			else
				fillRect(_vpX, _vpY, _xWidth, _yHeight, color);
		}
		else if (_bpp == 8)
		{
			color = (color & 0xE000) >> 8 | (color & 0x0700) >> 6 | (color & 0x0018) >> 3;
			memset(_img8, (uint8_t)color, _iwidth * _yHeight);
		}
		else if (_bpp == 4)
		{
			uint8_t c = ((color & 0x0F) | (((color & 0x0F) << 4) & 0xF0));
			memset(_img4, c, (_iwidth * _yHeight) >> 1);
		}
		else if (_bpp == 1)
		{
			if (color)
				memset(_img8, 0xFF, (_bitwidth >> 3) * _dheight + 1);
			else
				memset(_img8, 0x00, (_bitwidth >> 3) * _dheight + 1);
		}
	}
	else
		fillRect(_vpX - _xDatum, _vpY - _yDatum, _xWidth, _yHeight, color);
}

/***************************************************************************************
** Function name:           width
** Description:             Return the width of sprite
***************************************************************************************/
// Return the size of the sprite
int16_t TFT_eSprite::width(void)
{
	if (!_created)
		return 0;

	if (_bpp > 1)
	{
		if (_vpDatum)
			return _xWidth;
		return _dwidth;
	}

	if (_rotation & 1)
	{
		if (_vpDatum)
			return _xWidth;
		return _dheight;
	}

	if (_vpDatum)
		return _xWidth;
	return _dwidth;
}

/***************************************************************************************
** Function name:           height
** Description:             Return the height of sprite
***************************************************************************************/
int16_t TFT_eSprite::height(void)
{
	if (!_created)
		return 0;

	if (_bpp > 1)
	{
		if (_vpDatum)
			return _yHeight;
		return _dheight;
	}

	if (_rotation & 1)
	{
		if (_vpDatum)
			return _yHeight;
		return _dwidth;
	}

	if (_vpDatum)
		return _yHeight;
	return _dheight;
}

/***************************************************************************************
** Function name:           setRotation
** Description:             Rotate coordinate frame for 1bpp sprite
***************************************************************************************/
// Does nothing for 4, 8 and 16 bpp sprites.
void TFT_eSprite::setRotation(uint8_t r)
{
	if (_bpp != 1)
		return;

	_rotation = r;

	if (_rotation & 1)
	{
		resetViewport();
	}
	else
	{
		resetViewport();
	}
}

/***************************************************************************************
** Function name:           getRotation
** Description:             Get rotation for 1bpp sprite
***************************************************************************************/
uint8_t TFT_eSprite::getRotation(void)
{
	return _rotation;
}

/***************************************************************************************
** Function name:           drawPixel
** Description:             push a single pixel at an arbitrary position
***************************************************************************************/
void TFT_eSprite::drawPixel(int32_t x, int32_t y, uint32_t color)
{
	if (!_created)
		return;

	x += _xDatum;
	y += _yDatum;

	// Range checking
	if ((x < _vpX) || (y < _vpY) || (x >= _vpW) || (y >= _vpH))
		return;

	if (_bpp == 16)
	{
		color = (color >> 8) | (color << 8);
		_img[x + y * _iwidth] = (uint16_t)color;
	}
	else if (_bpp == 8)
	{
		_img8[x + y * _iwidth] = (uint8_t)((color & 0xE000) >> 8 | (color & 0x0700) >> 6 | (color & 0x0018) >> 3);
	}
	else if (_bpp == 4)
	{
		uint8_t c = color & 0x0F;
		int index = (x + y * _iwidth) >> 1;
		;
		if ((x & 0x01) == 0)
		{
			_img4[index] = (uint8_t)((c << 4) | (_img4[index] & 0x0F));
		}
		else
		{
			_img4[index] = (uint8_t)(c | (_img4[index] & 0xF0));
		}
	}
	else // 1 bpp
	{
		if (_rotation == 1)
		{
			uint16_t tx = x;
			x = _dwidth - y - 1;
			y = tx;
		}
		else if (_rotation == 2)
		{
			x = _dwidth - x - 1;
			y = _dheight - y - 1;
		}
		else if (_rotation == 3)
		{
			uint16_t tx = x;
			x = y;
			y = _dheight - tx - 1;
		}

		if (color)
			_img8[(x + y * _bitwidth) >> 3] |= (0x80 >> (x & 0x7));
		else
			_img8[(x + y * _bitwidth) >> 3] &= ~(0x80 >> (x & 0x7));
	}
}

/***************************************************************************************
** Function name:           drawLine
** Description:             draw a line between 2 arbitrary points
***************************************************************************************/
void TFT_eSprite::drawLine(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t color)
{
	if (!_created)
		return;

	//_xDatum and _yDatum Not added here, it is added by drawPixel & drawFastxLine

	bool steep = abs(y1 - y0) > abs(x1 - x0);
	if (steep)
	{
		swap_coord(x0, y0);
		swap_coord(x1, y1);
	}

	if (x0 > x1)
	{
		swap_coord(x0, x1);
		swap_coord(y0, y1);
	}

	int32_t dx = x1 - x0, dy = abs(y1 - y0);
	;

	int32_t err = dx >> 1, ystep = -1, xs = x0, dlen = 0;

	if (y0 < y1)
		ystep = 1;

	// Split into steep and not steep for FastH/V separation
	if (steep)
	{
		for (; x0 <= x1; x0++)
		{
			dlen++;
			err -= dy;
			if (err < 0)
			{
				err += dx;
				if (dlen == 1)
					drawPixel(y0, xs, color);
				else
					drawFastVLine(y0, xs, dlen, color);
				dlen = 0;
				y0 += ystep;
				xs = x0 + 1;
			}
		}
		if (dlen)
			drawFastVLine(y0, xs, dlen, color);
	}
	else
	{
		for (; x0 <= x1; x0++)
		{
			dlen++;
			err -= dy;
			if (err < 0)
			{
				err += dx;
				if (dlen == 1)
					drawPixel(xs, y0, color);
				else
					drawFastHLine(xs, y0, dlen, color);
				dlen = 0;
				y0 += ystep;
				xs = x0 + 1;
			}
		}
		if (dlen)
			drawFastHLine(xs, y0, dlen, color);
	}
}

/***************************************************************************************
** Function name:           drawFastVLine
** Description:             draw a vertical line
***************************************************************************************/
void TFT_eSprite::drawFastVLine(int32_t x, int32_t y, int32_t h, uint32_t color)
{
	if (!_created)
		return;

	x += _xDatum;
	y += _yDatum;

	// Clipping
	if ((x < _vpX) || (x >= _vpW) || (y >= _vpH))
		return;

	if (y < _vpY)
	{
		h += y - _vpY;
		y = _vpY;
	}

	if ((y + h) > _vpH)
		h = _vpH - y;

	if (h < 1)
		return;

	if (_bpp == 16)
	{
		color = (color >> 8) | (color << 8);
		int32_t yp = x + _iwidth * y;
		while (h--)
		{
			_img[yp] = (uint16_t)color;
			yp += _iwidth;
		}
	}
	else if (_bpp == 8)
	{
		color = (color & 0xE000) >> 8 | (color & 0x0700) >> 6 | (color & 0x0018) >> 3;
		while (h--)
			_img8[x + _iwidth * y++] = (uint8_t)color;
	}
	else if (_bpp == 4)
	{
		if ((x & 0x01) == 0)
		{
			uint8_t c = (uint8_t)(color & 0xF) << 4;
			while (h--)
			{
				_img4[(x + _iwidth * y) >> 1] = (uint8_t)(c | (_img4[(x + _iwidth * y) >> 1] & 0x0F));
				y++;
			}
		}
		else
		{
			uint8_t c = (uint8_t)color & 0xF;
			while (h--)
			{
				_img4[(x + _iwidth * y) >> 1] = (uint8_t)(c | (_img4[(x + _iwidth * y) >> 1] & 0xF0)); // x is odd; new color goes into the low bits.
				y++;
			}
		}
	}
	else
	{
		x -= _xDatum; // Remove any offset as it will be added by drawPixel
		y -= _yDatum;
		while (h--)
		{
			drawPixel(x, y++, color);
		}
	}
}

/***************************************************************************************
** Function name:           drawFastHLine
** Description:             draw a horizontal line
***************************************************************************************/
void TFT_eSprite::drawFastHLine(int32_t x, int32_t y, int32_t w, uint32_t color)
{
	if (!_created)
		return;

	x += _xDatum;
	y += _yDatum;

	// Clipping
	if ((y < _vpY) || (x >= _vpW) || (y >= _vpH))
		return;

	if (x < _vpX)
	{
		w += x - _vpX;
		x = _vpX;
	}

	if ((x + w) > _vpW)
		w = _vpW - x;

	if (w < 1)
		return;

	if (_bpp == 16)
	{
		color = (color >> 8) | (color << 8);
		while (w--)
			_img[_iwidth * y + x++] = (uint16_t)color;
	}
	else if (_bpp == 8)
	{
		color = (color & 0xE000) >> 8 | (color & 0x0700) >> 6 | (color & 0x0018) >> 3;
		memset(_img8 + _iwidth * y + x, (uint8_t)color, w);
	}
	else if (_bpp == 4)
	{
		uint8_t c = (uint8_t)color & 0x0F;
		uint8_t c2 = (c | ((c << 4) & 0xF0));
		if ((x & 0x01) == 1)
		{
			drawPixel(x - _xDatum, y - _yDatum, color);
			x++;
			w--;
			if (w < 1)
				return;
		}

		if (((w + x) & 0x01) == 1)
		{
			// handle the extra one at the other end
			drawPixel(x - _xDatum + w - 1, y - _yDatum, color);
			w--;
			if (w < 1)
				return;
		}
		memset(_img4 + ((_iwidth * y + x) >> 1), c2, (w >> 1));
	}
	else
	{
		x -= _xDatum; // Remove any offset as it will be added by drawPixel
		y -= _yDatum;

		while (w--)
		{
			drawPixel(x++, y, color);
		}
	}
}

/***************************************************************************************
** Function name:           fillRect
** Description:             draw a filled rectangle
***************************************************************************************/
void TFT_eSprite::fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color)
{
	if (!_created)
		return;

	x += _xDatum;
	y += _yDatum;

	// Clipping
	if ((x >= _vpW) || (y >= _vpH))
		return;

	if (x < _vpX)
	{
		w += x - _vpX;
		x = _vpX;
	}
	if (y < _vpY)
	{
		h += y - _vpY;
		y = _vpY;
	}

	if ((x + w) > _vpW)
		w = _vpW - x;
	if ((y + h) > _vpH)
		h = _vpH - y;

	if ((w < 1) || (h < 1))
		return;

	int32_t yp = _iwidth * y + x;

	if (_bpp == 16)
	{
		color = (color >> 8) | (color << 8);
		uint32_t iw = w;
		int32_t ys = yp;
		if (h--)
		{
			while (iw--)
				_img[yp++] = (uint16_t)color;
		}
		yp = ys;
		while (h--)
		{
			yp += _iwidth;
			memcpy(_img + yp, _img + ys, w << 1);
		}
	}
	else if (_bpp == 8)
	{
		color = (color & 0xE000) >> 8 | (color & 0x0700) >> 6 | (color & 0x0018) >> 3;
		while (h--)
		{
			memset(_img8 + yp, (uint8_t)color, w);
			yp += _iwidth;
		}
	}
	else if (_bpp == 4)
	{
		uint8_t c1 = (uint8_t)color & 0x0F;
		uint8_t c2 = c1 | ((c1 << 4) & 0xF0);
		if ((x & 0x01) == 0 && (w & 0x01) == 0)
		{
			yp = (yp >> 1);
			while (h--)
			{
				memset(_img4 + yp, c2, (w >> 1));
				yp += (_iwidth >> 1);
			}
		}
		else if ((x & 0x01) == 0)
		{

			// same as above but you have a hangover on the right.
			yp = (yp >> 1);
			while (h--)
			{
				if (w > 1)
					memset(_img4 + yp, c2, (w - 1) >> 1);
				// handle the rightmost pixel by calling drawPixel
				drawPixel(x + w - 1 - _xDatum, y + h - _yDatum, c1);
				yp += (_iwidth >> 1);
			}
		}
		else if ((w & 0x01) == 1)
		{
			yp = (yp + 1) >> 1;
			while (h--)
			{
				drawPixel(x - _xDatum, y + h - _yDatum, color & 0x0F);
				if (w > 1)
					memset(_img4 + yp, c2, (w - 1) >> 1);
				// same as above but you have a hangover on the left instead
				yp += (_iwidth >> 1);
			}
		}
		else
		{
			yp = (yp + 1) >> 1;
			while (h--)
			{
				drawPixel(x - _xDatum, y + h - _yDatum, color & 0x0F);
				if (w > 1)
					drawPixel(x + w - 1 - _xDatum, y + h - _yDatum, color & 0x0F);
				if (w > 2)
					memset(_img4 + yp, c2, (w - 2) >> 1);
				// maximal hacking, single pixels on left and right.
				yp += (_iwidth >> 1);
			}
		}
	}
	else
	{
		x -= _xDatum;
		y -= _yDatum;
		while (h--)
		{
			int32_t ww = w;
			int32_t xx = x;
			while (ww--)
				drawPixel(xx++, y, color);
			y++;
		}
	}
}
