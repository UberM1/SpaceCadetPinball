#include "pch.h"
#include "TSink.h"


#include "control.h"
#include "loader.h"
#include "pb.h"
#include "render.h"
#include "TPinballTable.h"
#include "TBall.h"
#include "TDrain.h"
#include "timer.h"
#include "FtAccelDebug.h" // FTDBG

TSink::TSink(TPinballTable* table, int groupIndex) : TCollisionComponent(table, groupIndex, true)
{
	visualStruct visual{};

	MessageField = 0;
	loader::query_visual(groupIndex, 0, &visual);
	BallThrowDirection = visual.Kicker.ThrowBallDirection;
	ThrowAngleMult = visual.Kicker.ThrowBallAngleMult;
	ThrowSpeedMult1 = visual.Kicker.Boost;
	ThrowSpeedMult2 = visual.Kicker.ThrowBallMult * 0.01f;
	SoundIndex4 = visual.SoundIndex4;
	SoundIndex3 = visual.SoundIndex3;
	// 601 is where the sink spits the ball back out. Absent means the sink has no
	// return point of its own; leave it at the origin rather than invent one -- the
	// ball will be fed from (0,0), which is wrong but not a crash, and no sink on
	// either bonus table has been seen without it.
	auto floatArr = loader::query_float_attribute_opt(groupIndex, 0, 601);
	BallPosition = floatArr ? vector2{floatArr[0], floatArr[1]} : vector2{0.0f, 0.0f};
	// 601's fourth element is the return height, not part of the position. Without it
	// every sink handed the ball back at the playfield's own z.
	BallPositionZ = floatArr ? floatArr[3] : 0.0f;

	// 1301 is the layer to return the ball onto, stored as a bit index. Absent means
	// "the sink's own layer", which is what the visual's collision group already is.
	// Seven of Dragon's eleven sinks are the only way onto the layers they serve --
	// a_sink16 is the sole feeder of a_ramp5's 0x10 -- so without this the ball comes
	// back on the base playfield layer and those ramps cannot be reached at all.
	auto releaseArr = loader::query_float_attribute_opt(groupIndex, 0, 1301);
	ReleaseMask = releaseArr
		              ? 1 << static_cast<int>(floor(releaseArr[0]))
		              : visual.CollisionGroup;
	// 407 is the auto-eject delay; absent means eject on the next tick.
	TimerTime = loader::query_float_attribute(groupIndex, 0, 407, 0.0f);
	ft_sink_register(GroupName, this, AABB, ReleaseMask, TimerTime, BallPosition,
	                 SoundIndex3, SoundIndex4); // FTDBG
}

int TSink::Message(MessageCode code, float value)
{
	switch (code)
	{
	case MessageCode::TSinkResetTimer:
	{
		if (value < 0.0f)
			value = TimerTime;
		auto id = timer::set(value, this, TimerExpired);
		if (ft_accel_enabled() || ft_autoplay_enabled())
			fprintf(stderr, "DBG-sink  MSG %-12s TSinkResetTimer value=%.4f timerTime=%.4f -> id=%d\n",
			        GroupName ? GroupName : "?", value, TimerTime, id); // FTDBG
		break;
	}
	case MessageCode::PlayerChanged:
		timer::kill(TimerExpired);
		PlayerMessagefieldBackup[PinballTable->CurrentPlayer] = MessageField;
		MessageField = PlayerMessagefieldBackup[static_cast<int>(floor(value))];
		break;
	case MessageCode::Reset:
		{
			timer::kill(TimerExpired);
			MessageField = 0;
			for (auto &msgBackup : PlayerMessagefieldBackup)
				msgBackup = 0;
			break;
		}
	default:
		break;
	}
	return 0;
}

void TSink::Collision(TBall* ball, vector2* nextPosition, vector2* direction, float distance, TEdgeSegment* edge)
{
	if (PinballTable->TiltLockFlag)
	{
		PinballTable->Drain->Collision(ball, nextPosition, direction, distance, edge);
	}
	else
	{
		if (ft_accel_enabled() || ft_autoplay_enabled())
			fprintf(stderr, "DBG-sink  HIT %-12s swallow ball=%p\n",
			        GroupName ? GroupName : "?", static_cast<void*>(ball)); // FTDBG
		ball->Disable();
		loader::play_sound(SoundIndex4, ball, "TSink1");
		control::handler(MessageCode::ControlCollision, this);
	}
}

void TSink::TimerExpired(int timerId, void* caller)
{
	auto sink = static_cast<TSink*>(caller);
	auto table = sink->PinballTable;
	if (ft_accel_enabled() || ft_autoplay_enabled())
		fprintf(stderr, "DBG-sink  EXP %-12s ballPos=(%.3f,%.3f) occupied=%d\n",
		        sink->GroupName ? sink->GroupName : "?", sink->BallPosition.X, sink->BallPosition.Y,
		        table->BallCountInRect(sink->BallPosition, table->CollisionCompOffset * 2.0f)); // FTDBG
	if (table->BallCountInRect(sink->BallPosition, table->CollisionCompOffset * 2.0f))
	{
		timer::set(0.5f, sink, TimerExpired);
	}
	else 
	{
		auto ball = table->AddBall(sink->BallPosition);
		if (ft_accel_enabled() || ft_autoplay_enabled())
			fprintf(stderr, "DBG-sink  EJECT %-12s ball=%p dir=(%.3f,%.3f,%.3f) s1=%.3f s2=%.4f\n",
			        sink->GroupName ? sink->GroupName : "?", static_cast<void*>(ball),
			        sink->BallThrowDirection.X, sink->BallThrowDirection.Y, sink->BallThrowDirection.Z,
			        sink->ThrowSpeedMult1, sink->ThrowSpeedMult2); // FTDBG
		assertm(ball, "Failure to create ball in sink");
		ball->CollisionDisabledFlag = true;
		// 0x4185f6 and 0x4185fc, in that order: the layer, then the height, which is
		// relative to the ball's own radius.
		ball->CollisionMask = sink->ReleaseMask;
		// Full Tilt data only: PINBALL.DAT's sinks carry -1.0 in that slot, which
		// pinball.exe never reads, and applying it started every wormhole eject
		// below the table.
		if (pb::FullTiltMode)
			ball->Position.Z = sink->BallPositionZ + ball->Radius;
		ball->throw_ball(&sink->BallThrowDirection, sink->ThrowAngleMult, sink->ThrowSpeedMult1,
			sink->ThrowSpeedMult2);
		if (sink->SoundIndex3)
			loader::play_sound(sink->SoundIndex3, ball, "TSink2");
	}
}
