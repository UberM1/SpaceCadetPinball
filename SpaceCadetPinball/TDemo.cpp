#include "pch.h"
#include "TDemo.h"


#include "loader.h"
#include "pb.h"
#include "TEdgeSegment.h"
#include "timer.h"
#include "TPinballTable.h"
#include "TBall.h"

TDemo::TDemo(TPinballTable* table, int groupIndex)
	: TCollisionComponent(table, groupIndex, false)
{
	visualStruct visual{};

	RestartGameTimer = 0;
	PlungerFlag = 0;
	FlipLeftTimer = 0;
	FlipRightTimer = 0;
	MessageField = 0;
	UnusedBaseFlag = 0;
	ActiveFlag = 0;
	FlipRightFlag = 0;
	FlipLeftFlag = 0;
	table->Demo = this;
	auto floatArr1 = loader::query_float_attribute_opt(groupIndex, 0, 407);
	if (floatArr1)
	{
		FlipTimerTime1 = floatArr1[0];
		FlipTimerTime2 = floatArr1[1];
		UnFlipTimerTime1 = floatArr1[2];
		UnFlipTimerTime2 = floatArr1[3];
	}
	else
	{
		FlipTimerTime1 = 0.2f;
		FlipTimerTime2 = 0.1f;
		UnFlipTimerTime1 = 0.2f;
		UnFlipTimerTime2 = 0.1f;
	}
	loader::query_visual(groupIndex, 0, &visual);
	// 1400..1404 are the five trigger walls the demo AI aims at. install_wall reads
	// *floatArr to pick a wall type, so each must be checked. A demo group that omits
	// one simply has no wall there -- install nothing and leave the edge handle null;
	// the demo's own code already tests Edge1/Edge2/Edge3 by identity only.
	auto v5 = loader::query_float_attribute_opt(groupIndex, 0, 1400);
	Edge1 = v5 ? TEdgeSegment::install_wall(v5, this, &ActiveFlag, visual.CollisionGroup, 0.0, 1400) : nullptr;

	auto v6 = loader::query_float_attribute_opt(groupIndex, 0, 1401);
	if (v6)
		TEdgeSegment::install_wall(v6, this, &ActiveFlag, visual.CollisionGroup, 0.0, 1401);

	auto v7 = loader::query_float_attribute_opt(groupIndex, 0, 1402);
	Edge2 = v7 ? TEdgeSegment::install_wall(v7, this, &ActiveFlag, visual.CollisionGroup, 0.0, 1402) : nullptr;

	auto v8 = loader::query_float_attribute_opt(groupIndex, 0, 1403);
	if (v8)
		TEdgeSegment::install_wall(v8, this, &ActiveFlag, visual.CollisionGroup, 0.0, 1403);

	auto v9 = loader::query_float_attribute_opt(groupIndex, 0, 1404);
	Edge3 = v9
		? TEdgeSegment::install_wall(v9, this, &ActiveFlag, visual.CollisionGroup, table->CollisionCompOffset, 1404)
		: nullptr;

	// Full Tilt, 0x417bbc..0x417c2f: two optional extra flip-trigger walls, each on a
	// collision layer of its own choosing rather than the demo group's, which is what
	// lets the demo work the upper flippers. Each attribute is {layer, wall...}: the
	// layer is truncated (0x417bdc / 0x417c15 call 0x42c7c8, ftol) into a mask
	// 1 << layer, and the wall after it is installed with offset 0 and the wall value
	// of the wall it duplicates -- 1405 as 1401 (flip left, 0x417be3 push 0x579), 1406
	// as 1403 (flip right, 0x417c1c push 0x57b). In the data: DRAGON.DAT 1406 on layer
	// 0; PIRATES.DAT 1405 on layer 0 and 1406 on layer 2; CADET.DAT and PINBALL.DAT
	// carry neither.
	auto v10 = loader::query_float_attribute_opt(groupIndex, 0, 1405);
	if (v10)
		TEdgeSegment::install_wall(v10 + 1, this, &ActiveFlag, 1u << static_cast<int>(v10[0]), 0.0, 1401);

	auto v11 = loader::query_float_attribute_opt(groupIndex, 0, 1406);
	if (v11)
		TEdgeSegment::install_wall(v11 + 1, this, &ActiveFlag, 1u << static_cast<int>(v11[0]), 0.0, 1403);
}

int TDemo::Message(MessageCode code, float value)
{
	switch (code)
	{
	case MessageCode::NewGame:
		if (RestartGameTimer)
			timer::kill(RestartGameTimer);
		RestartGameTimer = 0;
		break;
	case MessageCode::GameOver:
		if (RestartGameTimer)
			timer::kill(RestartGameTimer);
		RestartGameTimer = 0;
		if (ActiveFlag != 0)
			RestartGameTimer = timer::set(5.0, this, NewGameRestartTimer);
		break;
	case MessageCode::Reset:
		if (FlipLeftTimer)
			timer::kill(FlipLeftTimer);
		FlipLeftTimer = 0;
		if (FlipRightTimer)
			timer::kill(FlipRightTimer);
		FlipRightTimer = 0;

		if (FlipLeftFlag != 0)
			UnFlipLeft(0, this);
		if (FlipRightFlag)
			UnFlipRight(0, this);
		if (PlungerFlag)
			PlungerRelease(0, this);
		// Full Tilt's Reset arm (jump-table slot 0x402 -> 0x41835d) is this call to
		// 0x413550 alone.
		FtPlayerFieldsReset();
		break;
	case MessageCode::PlayerChanged:
		// Full Tilt, slot 0x3fe -> 0x418305: the per-player MessageField swap
		// (0x41830c call 0x413570) and nothing else.
		FtPlayerFieldsSwap(value);
		break;
	default:
		break;
	}
	return 0;
}

void TDemo::Collision(TBall* ball, vector2* nextPosition, vector2* direction, float distance, TEdgeSegment* edge)
{
	ball->not_again(edge);
	ball->Position.X = nextPosition->X;
	ball->Position.Y = nextPosition->Y;
	ball->RayMaxDistance -= distance;

	switch (reinterpret_cast<size_t>(edge->WallValue))
	{
	case 1400:
		if (!FlipLeftTimer && !FlipLeftFlag)
		{
			float time = FlipTimerTime1 + FlipTimerTime2 - RandFloat() * (FlipTimerTime2 + FlipTimerTime2);
			FlipLeftTimer = timer::set(time, this, FlipLeft);
		}
		break;
	case 1401:
		FlipLeft(0, this);
		break;
	case 1402:
		if (!FlipRightTimer && !FlipRightFlag)
		{
			float time = FlipTimerTime1 + FlipTimerTime2 - RandFloat() * (FlipTimerTime2 + FlipTimerTime2);
			FlipRightTimer = timer::set(time, this, FlipRight);
		}
		break;
	case 1403:
		FlipRight(0, this);
		break;
	case 1404:
		if (!PlungerFlag)
		{
			PinballTable->Message(MessageCode::PlungerInputPressed, 0);
			float time = RandFloat() + 2.0f;
			PlungerFlag = timer::set(time, this, PlungerRelease);
		}
		break;
	default:
		break;
	}
}

void TDemo::PlungerRelease(int timerId, void* caller)
{
	auto demo = static_cast<TDemo*>(caller);
	demo->PlungerFlag = 0;
	demo->PinballTable->Message(MessageCode::PlungerInputReleased, pb::time_next);
}

void TDemo::UnFlipRight(int timerId, void* caller)
{
	auto demo = static_cast<TDemo*>(caller);
	if (demo->FlipRightFlag)
		demo->PinballTable->Message(MessageCode::RightFlipperInputReleased, pb::time_next);
	demo->FlipRightFlag = 0;
}

void TDemo::UnFlipLeft(int timerId, void* caller)
{
	auto demo = static_cast<TDemo*>(caller);
	if (demo->FlipLeftFlag)
		demo->PinballTable->Message(MessageCode::LeftFlipperInputReleased, pb::time_next);
	demo->FlipLeftFlag = 0;
}

void TDemo::FlipRight(int timerId, void* caller)
{
	auto demo = static_cast<TDemo*>(caller);
	if (!demo->FlipRightFlag)
	{
		if (demo->FlipRightTimer)
		{
			timer::kill(demo->FlipRightTimer);
			demo->FlipRightTimer = 0;
		}
		demo->PinballTable->Message(MessageCode::RightFlipperInputPressed, pb::time_next);
		demo->FlipRightFlag = 1;
		float time = demo->UnFlipTimerTime1 + demo->UnFlipTimerTime2 - RandFloat() *
			(demo->UnFlipTimerTime2 + demo->UnFlipTimerTime2);
		timer::set(time, demo, UnFlipRight);
	}
}

void TDemo::FlipLeft(int timerId, void* caller)
{
	auto demo = static_cast<TDemo*>(caller);
	if (!demo->FlipLeftFlag)
	{
		if (demo->FlipLeftTimer)
		{
			timer::kill(demo->FlipLeftTimer);
			demo->FlipLeftTimer = 0;
		}
		demo->PinballTable->Message(MessageCode::LeftFlipperInputPressed, pb::time_next);
		demo->FlipLeftFlag = 1;
		float time = demo->UnFlipTimerTime1 + demo->UnFlipTimerTime2 - RandFloat() *
			(demo->UnFlipTimerTime2 + demo->UnFlipTimerTime2);
		timer::set(time, demo, UnFlipLeft);
	}
}

void TDemo::NewGameRestartTimer(int timerId, void* caller)
{
	auto demo = static_cast<TDemo*>(caller);
	pb::replay_level(true);
	demo->PinballTable->Message(MessageCode::NewGame, static_cast<float>(demo->PinballTable->PlayerCount));
	demo->RestartGameTimer = 0;
}
