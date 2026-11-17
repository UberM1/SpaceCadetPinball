#pragma once
#include "gdrv.h"

struct scoreStruct
{
	int Score;
	bool DirtyFlag;
	gdrv_bitmap8* BackgroundBmp;
	int OffsetX;
	int OffsetY;
	int Width;
	int Height;
	gdrv_bitmap8* CharBmp[10];
};

struct score_msg_font_type
{
	int GapWidth;
	int Height;
	gdrv_bitmap8* Chars[128]{};
};


class score
{
public:
	static score_msg_font_type* msg_fontp;

	// The second font. Dragon's Keep and Skulduggery display one-character glyph
	// tokens -- the Storm "K" run, Peg Leg's payout steps, the skill-shot ladder --
	// which are characters of a *symbol* font, not of the text font, and come out as
	// "#", "$" and "K" when drawn with pbmsg_ft. That font is in the table's own DAT
	// under the group name "font2": 20 glyph bitmaps in DRAGON.DAT, 52 in
	// PIRATES.DAT, and CADET.DAT has no such group at all -- which is the right
	// shape, since Space Cadet uses no glyph tokens. Null on Space Cadet.
	static score_msg_font_type* glyph_fontp;
	static int init();
	static scoreStruct* create(LPCSTR fieldName, gdrv_bitmap8* renderBgBmp);
	static scoreStruct* dup(scoreStruct* score, int scoreIndex);
	static score_msg_font_type* load_font(LPCSTR lpName);
	static void load_msg_font(LPCSTR lpName);
	static void load_glyph_font(LPCSTR lpName);
	static void unload_msg_font();
	static void erase(scoreStruct* score, int blitFlag);
	static void set(scoreStruct* score, int value);
	static void update(scoreStruct* score);
	static void string_format(int score, char* str);
};
