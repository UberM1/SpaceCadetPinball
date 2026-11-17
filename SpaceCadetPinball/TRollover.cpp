#include "pch.h"
#include "TRollover.h"


#include "control.h"
#include "gdrv.h"
#include "loader.h"
#include "render.h"
#include "TBall.h"
#include "TEdgeSegment.h"
#include "timer.h"
#include "TPinballTable.h"

TRollover::TRollover(TPinballTable* table, int groupIndex, bool createWall) : TCollisionComponent(
	table, groupIndex, createWall)
{
}


TRollover::TRollover(TPinballTable* table, int groupIndex) : TCollisionComponent(table, groupIndex, false)
{
	SpriteSet(0);
	build_walls(groupIndex);
}


int TRollover::Message(MessageCode code, float value)
{
	if (code == MessageCode::Reset)
	{
		// Full Tilt clears the per-player fields first, 0x415770 call 0x413550.
		FtPlayerFieldsReset();
		ActiveFlag = 1;
		RolloverFlag = 0;
		SpriteSet(0);
	}
	// Full Tilt, 0x4157a5: PlayerChanged only swaps the per-player MessageField
	// (0x4157b3 call 0x413570). TTripwire shares this Message (vtable 0x4375e0).
	if (code == MessageCode::PlayerChanged)
		FtPlayerFieldsSwap(value);
	return 0;
}

void TRollover::Collision(TBall* ball, vector2* nextPosition, vector2* direction, float distance,
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
		}
		else
		{
			loader::play_sound(SoftHitSoundId, ball, "TRollover");
			control::handler(MessageCode::ControlCollision, this);
		}
		RolloverFlag = RolloverFlag == 0;
		SpriteSet(RolloverFlag ? -1 : 0);
	}
}

void TRollover::build_walls(int groupIndex)
{
	visualStruct visual{};

	loader::query_visual(groupIndex, 0, &visual);
	// 600 is the rollover's trip wall, 603 the second wall that only collides while the
	// rollover is already tripped. Dragon's light rollovers carry one or neither, and
	// install_wall reads *floatArr for the wall type. Absent means no such wall:
	// install nothing.
	float* arr1 = loader::query_float_attribute_opt(groupIndex, 0, 600);
	if (arr1)
		TEdgeSegment::install_wall(arr1, this, &ActiveFlag, visual.CollisionGroup, 0.0, 600);
	float* arr2 = loader::query_float_attribute_opt(groupIndex, 0, 603);
	if (arr2)
		TEdgeSegment::install_wall(arr2, this, &RolloverFlag, visual.CollisionGroup, 0.0, 603);
}

void TRollover::TimerExpired(int timerId, void* caller)
{
	auto roll = static_cast<TRollover*>(caller);
	roll->ActiveFlag = 1;
}
