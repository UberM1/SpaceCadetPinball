#include "pch.h"
#include "TDrain.h"


#include "control.h"
#include "loader.h"
#include "pb.h"
#include "TBall.h"
#include "timer.h"
#include "TPinballTable.h"

TDrain::TDrain(TPinballTable* table, int groupIndex) : TCollisionComponent(table, groupIndex, true)
{
	Timer = 0;
	// 407 is the delay between the last ball draining and the end-of-ball handler.
	// Absent means no delay.
	TimerTime = loader::query_float_attribute(groupIndex, 0, 407, 0.0f);
}

int TDrain::Message(MessageCode code, float value)
{
	// Full Tilt, 0x418777 `cmp eax,0x3f3` -> 0x418792..0x4187a9: SetTiltLock kills the
	// end-of-ball timer and does nothing else (BallInDrainFlag and the per-player
	// fields stay). Dragon's Keep's ball save sends it to its drain (0x424973) so the
	// timer its drained ball armed cannot serve a second ball after the relaunch.
	// CADET.EXE has the same arm at the same address; gated so 3D Pinball keeps
	// upstream's drain.
	if (code == MessageCode::SetTiltLock && pb::FullTiltMode)
	{
		if (Timer)
		{
			timer::kill(Timer);
			Timer = 0;
		}
		return 0;
	}
	if (code == MessageCode::Reset)
	{
		if (Timer)
		{
			timer::kill(Timer);
			Timer = 0;
		}
		// Full Tilt, 0x4187d4 call 0x413550, between the timer kill and the flag.
		FtPlayerFieldsReset();
		PinballTable->BallInDrainFlag = 0;
	}
	// Full Tilt, 0x4187ac: PlayerChanged only swaps the per-player MessageField
	// (0x4187b3 call 0x413570).
	if (code == MessageCode::PlayerChanged)
		FtPlayerFieldsSwap(value);
	return 0;
}

void TDrain::Collision(TBall* ball, vector2* nextPosition, vector2* direction, float distance, TEdgeSegment* edge)
{
	ball->Disable();
	--PinballTable->MultiballCount;
	if (PinballTable->MultiballCount <= 0)
	{
		PinballTable->MultiballCount = 0;
		PinballTable->BallInDrainFlag = 1;
		Timer = timer::set(TimerTime, this, TimerCallback);
	}
	control::handler(MessageCode::ControlCollision, this);
}

void TDrain::TimerCallback(int timerId, void* caller)
{
	auto drain = static_cast<TDrain*>(caller);
	control::handler(MessageCode::ControlTimerExpired, drain);
}
