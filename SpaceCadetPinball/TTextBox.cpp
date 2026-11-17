#include "pch.h"
#include "TTextBox.h"

#include "control.h"
#include "fullscrn.h"
#include "loader.h"
#include "pb.h"
#include "render.h"
#include "score.h"
#include "timer.h"


TTextBox::TTextBox(TPinballTable* table, int groupIndex) : TPinballComponent(table, groupIndex, true)
{
	OffsetX = 0;
	OffsetY = 0;
	Width = 0;
	Height = 0;
	BgBmp = render::background_bitmap;
	Font = score::msg_fontp;
	CurrentMessage = nullptr;
	PreviousMessage = nullptr;
	Timer = 0;

	if (groupIndex > 0)
	{
		/*Full tilt: text box dimensions index is offset by resolution*/
		int arrLength;
		// 1500 is the box rectangle for this resolution. Absent means the box has no
		// authored geometry: keep the zero rect set above, which draws nothing, rather
		// than invent a position on the playfield.
		auto dimensions = loader::query_iattribute_opt(groupIndex + fullscrn::GetResolution(), 1500, &arrLength);
		if (dimensions && arrLength >= 4)
		{
			OffsetX = dimensions[0];
			OffsetY = dimensions[1];
			Width = dimensions[2];
			Height = dimensions[3];
		}
	}
}

TTextBox::~TTextBox()
{
	if (Timer)
	{
		if (Timer != -1)
			timer::kill(Timer);
		Timer = 0;
	}
	while (CurrentMessage)
	{
		TTextBoxMessage* message = CurrentMessage;
		TTextBoxMessage* nextMessage = message->NextMessage;
		delete message;
		CurrentMessage = nextMessage;
	}
}

int TTextBox::Message(MessageCode code, float value)
{
	// Full Tilt, 0x41e110 is nothing but the two player-field arms: 0x3fe -> 0x41e12c
	// call 0x413570, 0x402 -> 0x41e136 call 0x413550. It matters because Dragon's Keep
	// keeps its quest number in info_text_box's MessageField, so the quest is per
	// player and a new game starts from none. Both helpers are no-ops on 3D Pinball data.
	if (code == MessageCode::PlayerChanged)
		FtPlayerFieldsSwap(value);
	else if (code == MessageCode::Reset)
		FtPlayerFieldsReset();
	return 0;
}

void TTextBox::TimerExpired(int timerId, void* caller)
{
	auto tb = static_cast<TTextBox*>(caller);
	if (pb::FullTiltMode)
	{
		tb->FtTimerExpired();
		return;
	}
	TTextBoxMessage* message = tb->CurrentMessage;
	tb->Timer = 0;
	if (message)
	{
		TTextBoxMessage* nextMessage = message->NextMessage;
		delete message;
		tb->CurrentMessage = nextMessage;
		tb->Draw();
		control::handler(MessageCode::ControlTimerExpired, tb);
	}
}

void TTextBox::Clear(bool lowPriorityOnly)
{
	gdrv_bitmap8* bmp = BgBmp;
	if (bmp)
		gdrv::copy_bitmap(
			render::vscreen,
			Width,
			Height,
			OffsetX,
			OffsetY,
			bmp,
			OffsetX,
			OffsetY);
	else
		gdrv::fill_bitmap(render::vscreen, Width, Height, OffsetX, OffsetY, 0);
	if (Timer)
	{
		if (Timer != -1)
			timer::kill(Timer);
		Timer = 0;
	}
	while (CurrentMessage && (!lowPriorityOnly || CurrentMessage->LowPriority))
	{
		auto message = CurrentMessage;
		CurrentMessage = message->NextMessage;
		delete message;
	}
	if (CurrentMessage)
	{
		if (!pb::FullTiltMode)
			Draw();
		else if (FtSchedule())
			Render();
	}
}

// Full Tilt, 0x41d8e0: kill the display timer and free every queued message. Unlike
// Clear() it leaves the box's pixels alone -- Display calls it and draws the new head
// straight after.
void TTextBox::FtClearQueue()
{
	if (Timer)
	{
		if (Timer != -1)
			timer::kill(Timer);
		Timer = 0;
	}
	while (CurrentMessage)
	{
		auto message = CurrentMessage;
		CurrentMessage = message->NextMessage;
		delete message;
	}
}

// Full Tilt, 0x41dd30, the scheduler every queue change goes through. Returns whether
// the head is to be drawn.
//  - The timer is always re-armed from scratch (0x41dd35..0x41dd4a).
//  - An untimed head returns at once with no timer at all (Timer stays 0;
//    0x41dd58..0x41dd74). Display keeps an untimed message alone in the queue, so
//    nothing waits behind it.
//  - Heads more than 2 s past their end are dropped (0x41dd79..0x41ddd2, `fcomp -2.0`).
//  - An empty queue, and only an empty queue, tells the rules: control code 0x46
//    with the box as caller (0x41dddb), which is ControlTimerExpired. If the rules
//    did not queue something in answer, the box is erased (0x41dde8 call 0x41d930).
//  - Otherwise the head's remaining time, at least 0.25 s (0x41ddf8..0x41de0a),
//    arms the timer whose callback is 0x41dce0 (FtTimerExpired).
bool TTextBox::FtSchedule()
{
	if (Timer && Timer != -1)
		timer::kill(Timer);
	Timer = 0;

	float timeLeft = 0.0f;
	if (CurrentMessage)
	{
		if (CurrentMessage->Time == -1.0f)
			return true;
		while (CurrentMessage)
		{
			timeLeft = CurrentMessage->TimeLeft();
			if (timeLeft > -2.0f)
				break;
			auto message = CurrentMessage;
			CurrentMessage = message->NextMessage;
			delete message;
		}
	}

	if (!CurrentMessage)
	{
		control::handler(MessageCode::ControlTimerExpired, this);
		if (!CurrentMessage)
			Clear();
		return false;
	}

	if (timeLeft < 0.25f)
		timeLeft = 0.25f;
	Timer = timer::set(timeLeft, this, TimerExpired);
	return true;
}

// Full Tilt, 0x41dce0: drop the head that has run its time, reschedule, and draw the
// new head if there is one to draw. No message to the rules here; the scheduler sends
// it once the queue is empty.
void TTextBox::FtTimerExpired()
{
	Timer = 0;
	if (!CurrentMessage)
		return;
	auto message = CurrentMessage;
	CurrentMessage = message->NextMessage;
	delete message;
	if (FtSchedule())
		Render();
}

// Full Tilt, TTextBox::Display 0x41de40, which differs from upstream's in what an
// untimed message does to the queue (CADET.EXE has the same code at the same
// addresses, so this is the Full Tilt engine's and not a bonus-table rule):
//  - Same text in the same font as the last queued message (0x41de55..0x41dea5):
//    if both the head and the new message are untimed, nothing happens; if the new
//    one is timed, the last message takes the new time and the queue is rescheduled
//    (0x41dee3..0x41df06), without a redraw. Same text but untimed after a timed
//    head falls through to a fresh message.
//  - An untimed head is thrown away by any new message (0x41df19..0x41df33).
//  - An untimed new message throws away the whole queue (0x41df38..0x41df4c), so it
//    shows at once instead of waiting behind the timed messages still queued.
//  - The new message is appended, the queue rescheduled and the head drawn
//    (0x41e019..0x41e035).
void TTextBox::FtDisplay(const char* text, float time, bool lowPriority, bool glyph)
{
	if (CurrentMessage && !strcmp(text, PreviousMessage->Text) && PreviousMessage->Glyph == glyph)
	{
		if (CurrentMessage->Time == -1.0f && time == -1.0f)
			return;
		if (time != -1.0f)
		{
			PreviousMessage->Refresh(time);
			FtSchedule();
			return;
		}
	}

	if (CurrentMessage && CurrentMessage->Time == -1.0f)
		FtClearQueue();
	if (time == -1.0f)
		FtClearQueue();

	auto message = new TTextBoxMessage(text, time, lowPriority, glyph);
	if (!message->Text)
	{
		delete message;
		return;
	}
	if (CurrentMessage)
		PreviousMessage->NextMessage = message;
	else
		CurrentMessage = message;
	PreviousMessage = message;
	FtSchedule();
	Render();
}

score_msg_font_type* TTextBox::FontFor(const TTextBoxMessage* message) const
{
	// Height 0 would mean a group that exists but holds no bitmaps, and the layout
	// loop below advances by the font height -- it would spin forever. Treat such a
	// font as absent.
	if (message && message->Glyph && score::glyph_fontp && score::glyph_fontp->Height > 0)
		return score::glyph_fontp;
	return Font;
}

void TTextBox::DisplayGlyph(const char* text, float time)
{
	DisplayInternal(text, time, false, true);
}

void TTextBox::Display(const char* text, float time, bool lowPriority)
{
	DisplayInternal(text, time, lowPriority, false);
}

void TTextBox::DisplayInternal(const char* text, float time, bool lowPriority, bool glyph)
{
	if (!text)
		return;

	if (pb::FullTiltMode)
	{
		FtDisplay(text, time, lowPriority, glyph);
		return;
	}

	// The de-duplication has to take the font into account as well as the text: a
	// glyph token and a piece of ordinary text can be the same string ("K", "5") and
	// refreshing one in place of the other would draw it in the wrong font.
	if (CurrentMessage && !strcmp(text, PreviousMessage->Text) && PreviousMessage->Glyph == glyph)
	{
		PreviousMessage->Refresh(time);
		if (PreviousMessage == CurrentMessage)
		{
			if (Timer && Timer != -1)
				timer::kill(Timer);
			if (time == -1.0f)
				Timer = -1;
			else
				Timer = timer::set(time, this, TimerExpired);
		}
	}
	else
	{
		if (Timer == -1)
			Clear();

		auto message = new TTextBoxMessage(text, time, lowPriority, glyph);
		if (message->Text)
		{
			if (CurrentMessage)
				PreviousMessage->NextMessage = message;
			else
				CurrentMessage = message;
			PreviousMessage = message;
			if (Timer == 0)
				Draw();
		}
		else
		{
			delete message;
		}
	}
}

void TTextBox::DrawImGui()
{
	// Do nothing when using a font (the text will be rendered to VScreen in TTextBox::Draw)
	if (Font || !CurrentMessage)
		return;

	char windowName[64];
	SDL_Rect rect;
	ImGuiWindowFlags window_flags =
		ImGuiWindowFlags_NoBackground |
		ImGuiWindowFlags_NoDecoration |
		ImGuiWindowFlags_NoSavedSettings |
		ImGuiWindowFlags_NoFocusOnAppearing |
		ImGuiWindowFlags_NoInputs;

	rect.x = OffsetX;
	rect.y = OffsetY;
	rect.w = Width;
	rect.h = Height;

	rect = fullscrn::GetScreenRectFromPinballRect(rect);

	ImGui::SetNextWindowPos(ImVec2(static_cast<float>(rect.x), static_cast<float>(rect.y)));
	ImGui::SetNextWindowSize(ImVec2(static_cast<float>(rect.w), static_cast<float>(rect.h)));

	// Use the pointer to generate a window unique name per text box
	snprintf(windowName, sizeof(windowName), "TTextBox_%p", static_cast<void*>(this));
	if (ImGui::Begin(windowName, nullptr, window_flags))
	{
		ImGui::SetWindowFontScale(fullscrn::GetScreenToPinballRatio());

		// ToDo: centered text in FT
		ImGui::PushStyleColor(ImGuiCol_Text, pb::TextBoxColor);
		ImGui::TextWrapped("%s", CurrentMessage->Text);
		ImGui::PopStyleColor();
	}
	ImGui::End();
}

void TTextBox::Draw()
{
	bool display = false;
	while (CurrentMessage)
	{
		if (CurrentMessage->Time == -1.0f)
		{
			if (!CurrentMessage->NextMessage)
			{
				Timer = -1;
				display = true;
				break;
			}
		}
		else if (CurrentMessage->TimeLeft() >= -2.0f)
		{
			Timer = timer::set(std::max(CurrentMessage->TimeLeft(), 0.25f), this, TimerExpired);
			display = true;
			break;
		}

		auto tmp = CurrentMessage;
		CurrentMessage = CurrentMessage->NextMessage;
		delete tmp;
	}

	if (display)
		Render();
	else
		Render(false);
}

// The drawing half of upstream's Draw: repaint the box's background, then the head
// message unless `withText` is false. Split out so the Full Tilt scheduler can decide
// what is shown and leave the drawing to the same code.
void TTextBox::Render(bool withText)
{
	auto bmp = BgBmp;
	if (bmp)
		gdrv::copy_bitmap(
			render::vscreen,
			Width,
			Height,
			OffsetX,
			OffsetY,
			bmp,
			OffsetX,
			OffsetY);
	else
		gdrv::fill_bitmap(render::vscreen, Width, Height, OffsetX, OffsetY, 0);

	if (withText && CurrentMessage)
	{
		if (!Font)
		{
			// Immediate mode drawing using system font is handled by TTextBox::DrawImGui
			return;
		}

		// Per message, not per box: the same box alternates between announcements in
		// the text font and glyph runs in the symbol font.
		auto font = FontFor(CurrentMessage);

		std::vector<LayoutResult> lines{};
		auto textHeight = 0;
		for (auto text = CurrentMessage->Text; ; textHeight += font->Height)
		{
			if (!text[0] || textHeight + font->Height > Height)
				break;

			auto line = LayoutTextLine(text, font);
			if (line.Start == line.End)
				break;
			lines.push_back(line);
			text = line.End;
		}

		// Textboxes in FT display texts centered
		auto offY = OffsetY;
		if (pb::FullTiltMode)
			offY += (Height - textHeight) / 2;
		for (auto line : lines)
		{
			auto offX = OffsetX;
			if (pb::FullTiltMode)
				offX += (Width - line.Width) / 2;
			for (auto text = line.Start; text < line.End; text++)
			{
				auto charBmp = font->Chars[*text & 0x7F];
				if (charBmp)
				{
					auto height = charBmp->Height;
					auto width = charBmp->Width;
					if (render::background_bitmap)
						gdrv::copy_bitmap_w_transparency(render::vscreen, width, height, offX, offY, charBmp, 0,
						                                 0);
					else
						gdrv::copy_bitmap(render::vscreen, width, height, offX, offY, charBmp, 0, 0);
					offX += charBmp->Width + font->GapWidth;
				}
			}
			offY += font->Height;
		}
	}
}

TTextBox::LayoutResult TTextBox::LayoutTextLine(char* textStart, const score_msg_font_type* font) const
{
	auto lineWidth = 0, wordWidth = 0;
	char *wordBoundary = nullptr, *textEnd;
	for (textEnd = textStart; ; ++textEnd)
	{
		auto maskedChar = textEnd[0] & 0x7F;
		if (!maskedChar || maskedChar == '\n')
			break;

		auto charBmp = font->Chars[maskedChar];
		if (!charBmp)
			continue;

		auto width = lineWidth + charBmp->Width + font->GapWidth;
		if (width > Width)
		{
			if (wordBoundary)
			{
				textEnd = wordBoundary;
				lineWidth = wordWidth;
			}
			break;
		}
		if (maskedChar == ' ')
		{
			wordBoundary = textEnd;
			wordWidth = width;
		}
		lineWidth = width;
	}

	while ((*textEnd & 0x7F) == ' ')
		++textEnd;
	if ((*textEnd & 0x7F) == '\n')
		++textEnd;
	return LayoutResult{textStart, textEnd, lineWidth};
}
