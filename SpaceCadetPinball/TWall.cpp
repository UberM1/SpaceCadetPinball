#include "pch.h"
#include "TWall.h"


#include "control.h"
#include "pb.h"
#include "render.h"
#include "timer.h"

TWall::TWall(TPinballTable* table, int groupIndex) : TCollisionComponent(table, groupIndex, true)
{
	if (RenderSprite)
		SpriteSet(-1);
}

int TWall::Message(MessageCode code, float value)
{
	if (code == MessageCode::Reset && Timer)
	{
		timer::kill(Timer);
		TimerExpired(Timer, this);
	}
	// Full Tilt, 0x413c40: Reset ends with 0x413c67 call 0x413550, and PlayerChanged
	// (0x413c72) expires a running timer exactly as Reset does (0x413c81 kill, 0x413c8b
	// call 0x413bc0) and then swaps the per-player MessageField (0x413c97 call 0x413570).
	// 3D Pinball's TWall answers Reset alone.
	if (code == MessageCode::Reset)
		FtPlayerFieldsReset();
	if (code == MessageCode::PlayerChanged && pb::FullTiltMode)
	{
		if (Timer)
		{
			timer::kill(Timer);
			TimerExpired(Timer, this);
		}
		FtPlayerFieldsSwap(value);
	}
	return 0;
}

void TWall::Collision(TBall* ball, vector2* nextPosition, vector2* direction, float distance, TEdgeSegment* edge)
{
	if (DefaultCollision(ball, nextPosition, direction))
	{
		if (ListBitmap)
		{
			SpriteSet(0);
			Timer = timer::set(0.1f, this, TimerExpired);
		}
		control::handler(MessageCode::ControlCollision, this);
	}
}

void TWall::TimerExpired(int timerId, void* caller)
{
	auto wall = static_cast<TWall*>(caller);
	wall->SpriteSet(-1);
	wall->Timer = 0;
	wall->MessageField = 0;
}
