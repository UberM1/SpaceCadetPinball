#include "pch.h"
#include "TBumper.h"


#include "control.h"
#include "loader.h"
#include "pb.h"
#include "render.h"
#include "timer.h"
#include "TPinballTable.h"
#include "FtAccelDebug.h" // FTDBG

TBumper::TBumper(TPinballTable* table, int groupIndex) : TCollisionComponent(table, groupIndex, true)
{
	visualStruct visual{};

	BmpIndex = 0;
	Timer = 0;
	// 407 is the lamp-fade delay. Absent means no delay was authored, so the fade timer
	// expires on the next tick; the defaulted overload keeps that explicit.
	TimerTime = loader::query_float_attribute(groupIndex, 0, 407, 0.0f);
	loader::query_visual(groupIndex, 0, &visual);
	SoundIndex4 = visual.SoundIndex4;
	SoundIndex3 = visual.SoundIndex3;
	OriginalThreshold = Threshold;
}

int TBumper::Message(MessageCode code, float value)
{
	switch (code)
	{
	case MessageCode::TBumperSetBmpIndex:
		{
			// A sprite-less bumper has no lit states to step through.
			auto nextBmp = static_cast<int>(floor(value));
			auto maxBmp = ListBitmap ? static_cast<int>(ListBitmap->size()) - 1 : 0;
			if (2 * nextBmp > maxBmp)
				nextBmp = maxBmp / 2;
			if (nextBmp < 0)
				nextBmp = 0;
			if (nextBmp != BmpIndex)
			{
				if (nextBmp >= BmpIndex)
					loader::play_sound(SoundIndex4, this, "TBumper1");
				if (nextBmp < BmpIndex)
					loader::play_sound(SoundIndex3, this, "TBumper2");
				BmpIndex = nextBmp;
				Fire();
				control::handler(MessageCode::TBumperSetBmpIndex, this);
			}
			break;
		}
	case MessageCode::TBumperIncBmpIndex:
		{
			auto nextBmp = BmpIndex + 1;
			auto maxBmp = ListBitmap ? static_cast<int>(ListBitmap->size()) - 1 : 0;
			if (2 * nextBmp > maxBmp)
				nextBmp = maxBmp / 2;
			TBumper::Message(MessageCode::TBumperSetBmpIndex, static_cast<float>(nextBmp));
			break;
		}
	case MessageCode::TBumperDecBmpIndex:
		{
			auto nextBmp = BmpIndex - 1;
			if (nextBmp < 0)
				nextBmp = 0;
			TBumper::Message(MessageCode::TBumperSetBmpIndex, static_cast<float>(nextBmp));
			break;
		}
	case MessageCode::PlayerChanged:
		{
			// Full Tilt ends the outgoing player's lamp flash first: 0x4142f9 mov
			// eax,[esi+0x7a] (Timer) / 0x414301 call 0x41f900 (kill) / 0x41430b call
			// 0x414020 (TimerExpired). Same bytes in CADET.EXE; 3D Pinball data keeps
			// upstream's behaviour.
			if (pb::FullTiltMode && Timer)
			{
				if (ft_autoplay_enabled())
					fprintf(stderr, "DBG-pc    bumper %s lamp timer expired at player change\n",
					        GroupName ? GroupName : "?"); // FTDBG
				timer::kill(Timer);
				TimerExpired(Timer, this);
			}
			auto playerPtr = &PlayerData[PinballTable->CurrentPlayer];
			playerPtr->BmpIndex = BmpIndex;
			playerPtr->MessageField = MessageField;

			playerPtr = &PlayerData[static_cast<int>(floor(value))];
			BmpIndex = playerPtr->BmpIndex;
			MessageField = playerPtr->MessageField;
			TBumper::Message(MessageCode::TBumperSetBmpIndex, static_cast<float>(BmpIndex));
			break;
		}
	case MessageCode::Reset:
		{
			if (Timer)
			{
				timer::kill(Timer);
				TimerExpired(Timer, this);
			}
			BmpIndex = 0;
			MessageField = 0;
			for (auto& playerPtr : PlayerData)
			{
				playerPtr.BmpIndex = 0;
				playerPtr.MessageField = 0;
			}
			TimerExpired(0, this);
			break;
		}
	default:
		break;
	}

	return 0;
}

void TBumper::Collision(TBall* ball, vector2* nextPosition, vector2* direction, float distance, TEdgeSegment* edge)
{
	if (DefaultCollision(ball, nextPosition, direction))
	{
		Fire();
		control::handler(MessageCode::ControlCollision, this);
	}
}

void TBumper::TimerExpired(int timerId, void* caller)
{
	auto bump = static_cast<TBumper*>(caller);
	bump->SpriteSet(bump->BmpIndex * 2);
	bump->Timer = 0;
	bump->Threshold = bump->OriginalThreshold;
}

void TBumper::Fire()
{
	SpriteSet(2 * BmpIndex + 1);
	Timer = timer::set(TimerTime, this, TimerExpired);
	Threshold = 1000000000.0;
}
