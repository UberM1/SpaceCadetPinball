#pragma once
#include "maths.h"
#include "TCollisionComponent.h"
#include "TEdgeManager.h"

// Object type 1036 -- `a_accl*`, recovered from CADET.EXE: constructor 0x41b7d0,
// Collision 0x41b8f0, FieldEffect 0x41b9f0, vtable 0x437800, object size 0xa6. CADET's
// own TPinballTable constructor spawns it for type 0x40c. Dragon's Keep has four,
// Skulduggery two.
//
// It is a kicker turned inside out, and the only Full-Tilt-only class that asks the
// collision component for the standard attribute-600 wall (createWall = true). That
// wall is all the geometry it has, and its only job is to mark which ball to push: the
// push itself happens in FieldEffect, on the next evaluation of a field circle laid
// over the component's own AABB.
//
// So the sequence is: ball hits the wall -> the wall notes the ball, plays the
// component's 1100 sound and raises ControlCollision -> the ball, still inside the
// field, is thrown along the kicker's own direction with the kicker's own multipliers,
// exactly the throw_ball call TKickout makes when its timer expires. That is why a
// TKickout stand-in moved the ball wrongly rather than merely approximately: a
// kickout's field pulls the ball toward a centre and parks it, an accelerator's pushes
// it along a fixed direction and never takes it out of play.
//
// FieldEffect returns 0 in both branches, so there is no continuous force -- the
// accelerator is a one-shot kick, and between kicks its field costs nothing.
class TAccelerator : public TCollisionComponent
{
public:
	TAccelerator(TPinballTable* table, int groupIndex);
	void Collision(TBall* ball, vector2* nextPosition, vector2* direction, float distance,
	               TEdgeSegment* edge) override;
	int FieldEffect(TBall* ball, vector2* vecDst) override;

private:
	// The ball that hit the wall and has not been thrown yet. One at a time: a second
	// ball arriving before the field fires replaces the first, which is the original's
	// behaviour and not a bug worth fixing here.
	TBall* Ball{};

	// Straight out of visual.Kicker, under TKickout's names for the same four values.
	vector3 ThrowDirection{};
	float ThrowAngleMult{};
	float ThrowSpeedMult1{};
	float ThrowSpeedMult2{};

	int SoundIndex4{};
	field_effect_type Field{};
};
