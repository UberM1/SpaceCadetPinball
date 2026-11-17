#pragma once
class TTextBoxMessage
{
public:
	TTextBoxMessage* NextMessage;
	char* Text;
	float Time;
	int EndTicks;
	bool LowPriority;
	// Draw this message with the symbol font rather than the text font. Set only by
	// TTextBox::DisplayGlyph, which the bonus tables' glyph tokens go through.
	bool Glyph;

	TTextBoxMessage(const char* text, float time, bool lowPriority, bool glyph = false);
	~TTextBoxMessage();
	float TimeLeft() const;
	void Refresh(float time);
};
