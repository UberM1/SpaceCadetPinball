#include "pch.h"
#include "TLightRollover.h"


#include "control.h"
#include "loader.h"
#include "render.h"
#include "TBall.h"
#include "timer.h"
#include "TPinballTable.h"

TLightRollover::TLightRollover(TPinballTable* table, int groupIndex) : TRollover(table, groupIndex, false)
{
	RolloverFlag = 0;
	Timer = 0;
	SpriteSet(-1);
	build_walls(groupIndex);
	// 407 is the lit-state hold time. Several of Dragon's light rollovers omit it;
	// absent means no hold, i.e. the sprite clears on the next tick.
	FloatArr = loader::query_float_attribute(groupIndex, 0, 407, 0.0f);
}

int TLightRollover::Message(MessageCode code, float value)
{
	// Full Tilt, 0x4159f8: PlayerChanged only swaps the per-player MessageField
	// (0x415a06 call 0x413570).
	if (code == MessageCode::PlayerChanged)
		FtPlayerFieldsSwap(value);
	if (code == MessageCode::Reset)
	{
		// Full Tilt clears the per-player fields first, 0x4159c0 call 0x413550.
		FtPlayerFieldsReset();
		ActiveFlag = 1;
		RolloverFlag = 0;
		if (Timer)
			timer::kill(Timer);
		Timer = 0;
		SpriteSet(-1);
	}
	return 0;
}

void TLightRollover::Collision(TBall* ball, vector2* nextPosition, vector2* direction, float distance,
                               TEdgeSegment* edge)
{
	ball->Position.X = nextPosition->X;
	ball->Position.Y = nextPosition->Y;
	ball->RayMaxDistance -= distance;
	ball->not_again(edge);
	if (!PinballTable->TiltLockFlag)
	{
		if (RolloverFlag)
		{
			timer::set(0.1f, this, TimerExpired);
			ActiveFlag = 0;
			RolloverFlag = RolloverFlag == 0;
			if (Timer == 0)
				Timer = timer::set(FloatArr, this, delay_expired);
		}
		else
		{
			loader::play_sound(SoftHitSoundId, this, "TLightRollover");
			control::handler(MessageCode::ControlCollision, this);
			RolloverFlag = RolloverFlag == 0;
			SpriteSet(0);
		}
	}
}

void TLightRollover::delay_expired(int timerId, void* caller)
{
	auto roll = static_cast<TLightRollover*>(caller);
	roll->SpriteSet(-1);
	roll->Timer = 0;
}
