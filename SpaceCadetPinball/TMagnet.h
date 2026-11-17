#pragma once
#include "TKickout.h"

// Object type 1042 -- Skulduggery's `s_magnet1`..`s_magnet6`, recovered from CADET.EXE,
// whose own table constructor spawns it for type 0x412.
//
// It is a TKickout subclass -- the original's object is exactly TKickout's size, adds
// no fields, and its constructor just overwrites the vtable and TimerTime2 -- but it
// inverts the kickout's bargain. A kickout swallows the ball, parks it at the circle's
// centre and later throws it; a magnet lets the ball pass straight through (the base
// class's "already captured" path) and instead arms a one-shot timer, so what it
// models is a field that grabs for a moment and lets go.
//
// The rules see two messages from it: ControlCollision the instant the ball crosses it,
// and ControlTimerExpired when the grab ends. Arming and disarming come in from the
// rules as the two Full-Tilt-only codes 0x4b/0x4c, which only flip ActiveFlag -- and
// since the collision circle TKickout installs is gated on &ActiveFlag, a disarmed
// magnet is simply not in the world.
class TMagnet : public TKickout
{
public:
	TMagnet(TPinballTable* table, int groupIndex);
	int Message(MessageCode code, float value) override;
	void Collision(TBall* ball, vector2* nextPosition, vector2* direction, float distance,
	               TEdgeSegment* edge) override;

	static void GrabTimerExpired(int timerId, void* caller);
};
