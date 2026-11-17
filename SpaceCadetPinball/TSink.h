#pragma once
#include "maths.h"
#include "TCollisionComponent.h"

class TSink :
	public TCollisionComponent
{
public:
	TSink(TPinballTable* table, int groupIndex);
	int Message(MessageCode code, float value) override;
	void Collision(TBall* ball, vector2* nextPosition, vector2* direction, float distance,
	               TEdgeSegment* edge) override;

	static void TimerExpired(int timerId, void* caller);

	float TimerTime;
	// The layer the ejected ball is put on, and the height it is put at. Both are in
	// the originals' TSink and in neither upstream's nor, until now, this port's: the
	// constructor at DRAGON.EXE 0x418390 reads attribute 1301 as `1 << floor(arr[0])`
	// into +0x76, defaulting to the visual's own collision group when the attribute is
	// absent (0x41842e), and takes attribute 601's *fourth* element into +0x7a
	// (0x41847e), while the port read only 601[0] and 601[1] as the position. The two
	// are then applied to the new ball in TimerExpired, at 0x4185f6 and 0x4185fc.
	int ReleaseMask;
	float BallPositionZ;
	vector2 BallPosition{};
	vector3 BallThrowDirection{};
	float ThrowAngleMult;
	float ThrowSpeedMult1;
	float ThrowSpeedMult2;
	int SoundIndex4;
	int SoundIndex3;
	int PlayerMessagefieldBackup[4]{};
};
