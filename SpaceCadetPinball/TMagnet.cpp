#include "pch.h"
#include "TMagnet.h"

#include "control.h"
#include "timer.h"
#include "TBall.h"

TMagnet::TMagnet(TPinballTable* table, int groupIndex) : TKickout(table, groupIndex, true)
{
	// TimerTime2 is the kickout's 0.05 s re-arm delay; the magnet reuses the same field
	// as how long it holds the ball, which is why it has to overwrite it. 1.0 s is the
	// original's constant -- there is no attribute behind it.
	TimerTime2 = 1.0f;
	BallCaputeredFlag = 0;
}

void TMagnet::Collision(TBall* ball, vector2* nextPosition, vector2* direction, float distance,
                        TEdgeSegment* edge)
{
	// The ball keeps going. The original runs only the base class's pass-through
	// branch here -- no reposition to the circle centre, no z change, no throw -- so
	// the magnet never takes the ball out of play.
	ball->Position.X = nextPosition->X;
	ball->Position.Y = nextPosition->Y;
	ball->RayMaxDistance -= distance;
	ball->not_again(edge);

	BallCaputeredFlag = 1;
	Timer = timer::set(TimerTime2, this, GrabTimerExpired);
	control::handler(MessageCode::ControlCollision, this);
}

void TMagnet::GrabTimerExpired(int timerId, void* caller)
{
	auto magnet = static_cast<TMagnet*>(caller);
	magnet->BallCaputeredFlag = 0;
	magnet->Timer = 0;
	control::handler(MessageCode::ControlTimerExpired, magnet);
}

int TMagnet::Message(MessageCode code, float value)
{
	switch (code)
	{
	case MessageCode::TMagnetFieldEnable:
		ActiveFlag = 1;
		return 0;
	case MessageCode::TMagnetFieldDisable:
	case MessageCode::SetTiltLock:
	case MessageCode::PlayerChanged:
	case MessageCode::Reset:
		// All four disarm. PlayerChanged first swaps the per-player MessageField and
		// then re-enters with 0x4c (0x416705 call 0x413570, 0x416710 call 0x4166c0);
		// Reset first clears the fields (0x41671d call 0x413550) and falls into the
		// disarm at 0x416722.
		if (code == MessageCode::PlayerChanged)
			FtPlayerFieldsSwap(value);
		else if (code == MessageCode::Reset)
			FtPlayerFieldsReset();
		if (Timer)
		{
			timer::kill(Timer);
			// The original leaves the stale id in place here. Clearing it keeps a
			// second disarm from killing a timer slot that has since been reused.
			Timer = 0;
		}
		ActiveFlag = 0;
		BallCaputeredFlag = 0;
		return 0;
	default:
		return 0;
	}
}
