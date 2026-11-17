#include "pch.h"
#include "TFlagSpinner.h"


#include "control.h"
#include "loader.h"
#include "pb.h"
#include "render.h"
#include "TBall.h"
#include "timer.h"
#include "TLine.h"
#include "TPinballTable.h"

TFlagSpinner::TFlagSpinner(TPinballTable* table, int groupIndex) : TCollisionComponent(table, groupIndex, false)
{
	visualStruct visual{};
	vector2 end{}, start{};

	Timer = 0;
	loader::query_visual(groupIndex, 0, &visual);
	// FloatArr is null when the group has no line geometry. The two collider lines
	// below are the spinner's whole physical presence; with no endpoints, install none
	// and let it be a prop. Falls through to the speed attributes, which have their own
	// upstream defaults.
	if (visual.FloatArr)
	{
		end.X = visual.FloatArr[0];
		end.Y = visual.FloatArr[1];
		start.X = visual.FloatArr[2];
		start.Y = visual.FloatArr[3];
		auto line = new TLine(this, &ActiveFlag, visual.CollisionGroup, start, end);
		line->place_in_grid(&AABB);
		EdgeList.push_back(line);

		line = new TLine(this, &ActiveFlag, visual.CollisionGroup, end, start);
		PrevCollider = line;
		line->place_in_grid(&AABB);
		EdgeList.push_back(line);
	}

	SpeedDecrement = 0.64999998f;
	MaxSpeed = 50000.0f;
	MinSpeed = 5.0f;
	auto speedDec = loader::query_float_attribute_opt(groupIndex, 0, 1202);
	if (speedDec)
		SpeedDecrement = *speedDec;
	auto maxSpeed = loader::query_float_attribute_opt(groupIndex, 0, 1200);
	if (maxSpeed)
		MaxSpeed = *maxSpeed;
	auto minSpeed = loader::query_float_attribute_opt(groupIndex, 0, 1201);
	if (minSpeed)
		MinSpeed = *minSpeed;
}

int TFlagSpinner::Message(MessageCode code, float value)
{
	if (code == MessageCode::Reset)
	{
		if (Timer)
		{
			timer::kill(Timer);
			Timer = 0;
		}
		BmpIndex = 0;
		SpriteSet(BmpIndex);
		// 0x41481f call 0x413550, after the timer kill and before the sprite reset.
		FtPlayerFieldsReset();
	}
	else if (code == MessageCode::PlayerChanged && pb::FullTiltMode)
	{
		// Full Tilt, 0x414889: kill the spin timer (0x41489b) and swap the per-player
		// MessageField (0x4148b1 call 0x413570). The flag is left where it is.
		if (Timer)
		{
			timer::kill(Timer);
			Timer = 0;
		}
		FtPlayerFieldsSwap(value);
	}
	return 0;
}

void TFlagSpinner::Collision(TBall* ball, vector2* nextPosition, vector2* direction, float distance,
                             TEdgeSegment* edge)
{
	ball->Position.X = nextPosition->X;
	ball->Position.Y = nextPosition->Y;
	ball->RayMaxDistance = ball->RayMaxDistance - distance;
	ball->not_again(edge);

	SpinDirection = 2 * (PrevCollider != edge) - 1;
	if (ball->Speed == 0.0f)
		Speed = MinSpeed;
	else
		Speed = ball->Speed * 20.0f;
	if (Speed < MinSpeed)
		Speed = MinSpeed;
	if (Speed > MaxSpeed)
		Speed = MaxSpeed;
	NextFrame();
}

void TFlagSpinner::NextFrame()
{
	BmpIndex += SpinDirection;
	int bmpIndex = BmpIndex;
	// A spinner with no sprite list has no frames to advance through.
	int bmpCount = ListBitmap ? static_cast<int>(ListBitmap->size()) : 0;
	if (bmpCount <= 0)
		return;
	if (bmpIndex >= bmpCount)
		BmpIndex = 0;
	else if (bmpIndex < 0)
		BmpIndex = bmpCount - 1;

	if (!PinballTable->TiltLockFlag)
	{
		control::handler(MessageCode::ControlCollision, this);
		if (SoftHitSoundId)
			loader::play_sound(SoftHitSoundId, this, "TFlagSpinner");
		if (!BmpIndex)
			control::handler(MessageCode::ControlSpinnerLoopReset, this);
	}

	SpriteSet(BmpIndex);
	Speed *= SpeedDecrement;
	if (Speed >= MinSpeed)
	{
		timer::set(1.0f / Speed, this, SpinTimer);
	}
}

void TFlagSpinner::SpinTimer(int timerId, void* caller)
{
	auto spinner = static_cast<TFlagSpinner*>(caller);
	spinner->Timer = 0;
	spinner->NextFrame();
}
