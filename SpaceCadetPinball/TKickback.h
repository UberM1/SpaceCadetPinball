#pragma once
#include "TCollisionComponent.h"

class TKickback :
	public TCollisionComponent
{
public:
	TKickback(TPinballTable* table, int groupIndex);
	int Message(MessageCode code, float value) override;
	void Collision(TBall* ball, vector2* nextPosition, vector2* direction, float distance,
		TEdgeSegment* edge) override;

	// Two distinct timer callbacks, as the original has: DRAGON.EXE 0x415c60 arms the
	// kicker and 0x415ce0 retracts it. Upstream collapsed them into one function that
	// branches on KickActiveFlag and re-arms itself -- see the note in TKickback.cpp.
	static void ArmExpired(int timerId, void* caller);
	static void RetractExpired(int timerId, void* caller);

	float TimerTime;
	float TimerTime2;
	int Timer;
	int KickActiveFlag;
	// [this+0x86] in the original: set by 0x4c and cleared by 0x4b, a tilt lock or a
	// reset. Collision tests it *before* KickActiveFlag (0x415bd7), so a suppressed
	// kickback still stops the ball -- it simply never arms and never kicks.
	int Suppressed;
};
