#pragma once
#include "score.h"
#include "TPinballComponent.h"
#include "TTextBoxMessage.h"

class TTextBox :
	public TPinballComponent
{
public:
	int OffsetX;
	int OffsetY;
	int Width;
	int Height;
	int Timer;
	gdrv_bitmap8* BgBmp;
	score_msg_font_type* Font;
	TTextBoxMessage* CurrentMessage;
	TTextBoxMessage* PreviousMessage;

	TTextBox(TPinballTable* table, int groupIndex);
	~TTextBox() override;
	int Message(MessageCode code, float value) override;
	void Clear(bool lowPriorityOnly = false);
	void Display(const char* text, float time, bool lowPriority = false);
	// The glyph-token entry point. Same queue, same timing; the message is simply
	// marked to draw with score::glyph_fontp. Falls back to the text font when the
	// table has no "font2" group, which is every Space Cadet table.
	void DisplayGlyph(const char* text, float time);
	void DrawImGui();

private:
	struct LayoutResult
	{
		char *Start, *End;
		int Width;
	};

	static void TimerExpired(int timerId, void* caller);

	void Draw();
	void DisplayInternal(const char* text, float time, bool lowPriority, bool glyph);
	score_msg_font_type* FontFor(const TTextBoxMessage* message) const;
	LayoutResult LayoutTextLine(char* textStart, const score_msg_font_type* font) const;
};
