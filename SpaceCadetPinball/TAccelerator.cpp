#include "pch.h"
#include "TAccelerator.h"

#include "control.h"
#include "loader.h"
#include "TBall.h"
#include "TPinballTable.h"
#include "TTableLayer.h"
#include "FtAccelDebug.h" // FTDBG

TAccelerator::TAccelerator(TPinballTable* table, int groupIndex) : TCollisionComponent(table, groupIndex, true)
{
	visualStruct visual{};
	loader::query_visual(groupIndex, 0, &visual);

	ThrowDirection = visual.Kicker.ThrowBallDirection;
	ThrowAngleMult = visual.Kicker.ThrowBallAngleMult;
	ThrowSpeedMult1 = visual.Kicker.Boost;
	// The same 1/100 the kicker's throw multiplier gets everywhere else in the engine.
	ThrowSpeedMult2 = visual.Kicker.ThrowBallMult * 0.01f;
	SoundIndex4 = visual.SoundIndex4;

	// 1.8 is the original's constant, and it is the full extent being halved and then
	// widened: extent / 1.8 is half-extent / 0.9, so the circle clears the wall by a
	// tenth. The larger axis wins, so a long thin accelerator still covers its own wall.
	circle_type circle{};
	circle.Center.X = (AABB.XMin + AABB.XMax) / 2.0f;
	circle.Center.Y = (AABB.YMin + AABB.YMax) / 2.0f;
	auto radius = std::max((AABB.YMax - AABB.YMin) / 1.8f, (AABB.XMax - AABB.XMin) / 1.8f);
	circle.RadiusSq = radius * radius;

	Field.ActiveFlag = &ActiveFlag;
	Field.CollisionComp = this;
	Field.CollisionGroup = visual.CollisionGroup;
	TTableLayer::edges_insert_circle(&circle, nullptr, &Field);

	// FTDBG
	ft_accel_register(FtAccelDbgEntry{GroupName, this, circle.Center, circle.RadiusSq,
	                                  Field.CollisionGroup, ThrowDirection, AABB});
}

void TAccelerator::Collision(TBall* ball, vector2* nextPosition, vector2* direction, float distance,
                             TEdgeSegment* edge)
{
	// The wall does not push back -- the ball goes through it and gets accelerated
	// instead, so there is no basic_collision here either.
	ball->not_again(edge);
	ball->RayMaxDistance -= distance;
	ball->Position.X = nextPosition->X;
	ball->Position.Y = nextPosition->Y;

	// A tilted table is inert: the ball crosses the wall and nothing is armed, which
	// leaves the field with nothing to throw.
	if (PinballTable->TiltLockFlag)
		return;

	if (ft_accel_enabled())
		fprintf(stderr, "DBG-accel HIT   %-10s comp=%p ball=%p pos=(%.3f,%.3f) mask=0x%x dist=%.4f edge=%p\n",
		        GroupName ? GroupName : "?", this, static_cast<void*>(ball), ball->Position.X,
		        ball->Position.Y, ball->CollisionMask, distance, static_cast<void*>(edge)); // FTDBG

	ft_accel_note_hit(GroupName); // FTDBG
	Ball = ball;
	ball->CollisionDisabledFlag = true;
	loader::play_sound(SoundIndex4, ball, "TAccelerator");
	control::handler(MessageCode::ControlCollision, this);
}

int TAccelerator::FieldEffect(TBall* ball, vector2* vecDst)
{
	// Only the ball the wall armed, and only once: clearing Ball here is what makes the
	// kick one-shot. Every other ball crossing the field sees nothing.
	if (ft_accel_enabled())
		fprintf(stderr, "DBG-accel FIELD %-10s comp=%p ball=%p armed=%p match=%d pos=(%.3f,%.3f)\n",
		        GroupName ? GroupName : "?", this, static_cast<void*>(ball), static_cast<void*>(Ball),
		        Ball == ball, ball->Position.X, ball->Position.Y); // FTDBG

	if (Ball != ball)
		return 0;

	Ball = nullptr;
	if (ft_accel_enabled())
		fprintf(stderr, "DBG-accel THROW %-10s dir=(%.3f,%.3f,%.3f) angMult=%.4f s1=%.4f s2=%.4f\n",
		        GroupName ? GroupName : "?", ThrowDirection.X, ThrowDirection.Y, ThrowDirection.Z,
		        ThrowAngleMult, ThrowSpeedMult1, ThrowSpeedMult2); // FTDBG
	ball->throw_ball(&ThrowDirection, ThrowAngleMult, ThrowSpeedMult1, ThrowSpeedMult2);

	// Zero either way: the throw has already set the ball's direction and speed outright,
	// so there is no force for the caller to add on top of it.
	return 0;
}
