#include "pch.h"
#include "TKickout.h"


#include "control.h"
#include "loader.h"
#include "pb.h"
#include "table_rules.h"
#include "TBall.h"
#include "TCircle.h"
#include "timer.h"
#include "TPinballTable.h"
#include "TTableLayer.h"
#include "FtAccelDebug.h"

TKickout::TKickout(TPinballTable* table, int groupIndex, bool someFlag): TCollisionComponent(
	table, groupIndex, false)
{
	visualStruct visual{};
	circle_type circle{};

	NotSomeFlag = !someFlag;
	if (!someFlag)
		ActiveFlag = 0;
	TimerTime1 = 1.5;
	TimerTime2 = 0.05f;
	MessageField = 0;
	Timer = 0;
	BallCaputeredFlag = 0;
	// 305 is the kickout's own field strength. Absent means it exerts no field.
	FieldMult = loader::query_float_attribute(groupIndex, 0, 305, 0.0f);
	loader::query_visual(groupIndex, 0, &visual);
	SoftHitSoundId = visual.SoftHitSoundId;
	HardHitSoundId = visual.Kicker.HardHitSoundId;

	// Dragon's accelerators (object type 1036, mapped onto TKickout) and several of its
	// kickers have no circle geometry, so query_visual leaves FloatArr null. The centre,
	// the collision circle and the field insert below are all that geometry: with none,
	// there is no capture area to install.
	if (!visual.FloatArr)
	{
		Circle = {};
		Circle.RadiusSq = 0.001f;
		CollisionBallSetZ = 0.0f;
		ThrowSpeedMult2 = visual.Kicker.ThrowBallMult * 0.01f;
		BallThrowDirection = visual.Kicker.ThrowBallDirection;
		ThrowAngleMult = visual.Kicker.ThrowBallAngleMult;
		ThrowSpeedMult1 = visual.Kicker.Boost;
		ft_kickout_register(GroupName, this, Circle.Center, 0.0f, visual.CollisionGroup, TimerTime1,
		                    TimerTime2, CollisionBallSetZ, FieldMult, NotSomeFlag, ActiveFlag, 0,
		                    0.0f, 0); // FTDBG
		return;
	}
	Circle.Center.X = visual.FloatArr[0];
	Circle.Center.Y = visual.FloatArr[1];
	// 306 scales the visual radius into the capture radius. Absent means 0.1, NOT 0:
	// DRAGON.EXE 0x415e96 queries it and 0x415ecb writes 0x3dcccccd = 0.1f into the
	// slot when the lookup misses, and the product is clamped to 0.001f only when it
	// comes out exactly zero (0x415f31 compares against the 0.0 at 0x437338, 0x415f3f
	// stores 0x3a83126f = 0.001f). The port defaulted to 0.0f and therefore always took
	// the clamp, which is 100x too small -- Skulduggery's six s_magnet* carry neither
	// 306 nor 408, so every one of them had a 0.001-unit capture circle instead of
	// 0.1 x 0.670 = 0.067, i.e. could never grab a ball. CADET.DAT's three kickouts all
	// carry 306 (0.05 / 0.9 / 0.2), so Space Cadet is measurably unaffected.
	auto rAttr = loader::query_float_attribute_opt(groupIndex, 0, 306);
	Circle.RadiusSq = (rAttr ? rAttr[0] : 0.1f) * visual.FloatArr[2];
	if (Circle.RadiusSq == 0.0f)
		Circle.RadiusSq = 0.001f;
	auto tCircle = new TCircle(this, &ActiveFlag, visual.CollisionGroup,
	                           reinterpret_cast<vector3*>(visual.FloatArr), Circle.RadiusSq);
	if (tCircle)
	{
		tCircle->place_in_grid(&AABB);
		EdgeList.push_back(tCircle);
	}

	Circle.RadiusSq = visual.FloatArr[2] * visual.FloatArr[2];
	// 408 is the z the captured ball is held at. Absent means table->CollisionCompOffset,
	// not 0: DRAGON.EXE 0x415fdc pushes 0x198, 0x416007 takes arr[3] when the lookup
	// hits, and 0x416012..0x41601c loads [table+0x152] -- CollisionCompOffset -- when it
	// misses. Unlike THole (which adds CollisionCompOffset to arr[3], 0x416929) the
	// kickout uses arr[3] alone.
	//
	// The element index stays data-flavour dependent, as before; only the default
	// changes. It bites the same six s_magnet* that 306 does: they were holding the ball
	// at z 0.0 instead of CollisionCompOffset, the TSink/THole error of section 12 again.
	// All three CADET.DAT kickouts carry 408, so Space Cadet is measurably unaffected.
	auto zAttr = loader::query_float_attribute_opt(groupIndex, 0, 408);
	CollisionBallSetZ = !zAttr
		                ? table->CollisionCompOffset
		                : CurrentTable->FullTiltData && !CurrentTable->DemoData ? zAttr[3] : zAttr[2];
	ThrowSpeedMult2 = visual.Kicker.ThrowBallMult * 0.01f;
	BallThrowDirection = visual.Kicker.ThrowBallDirection;
	ThrowAngleMult = visual.Kicker.ThrowBallAngleMult;
	ThrowSpeedMult1 = visual.Kicker.Boost;

	circle.RadiusSq = Circle.RadiusSq;
	circle.Center.X = Circle.Center.X;
	circle.Center.Y = Circle.Center.Y;
	Field.ActiveFlag = &ActiveFlag;
	Field.CollisionComp = this;
	Field.CollisionGroup = visual.CollisionGroup;
	TTableLayer::edges_insert_circle(&circle, nullptr, &Field);
	ft_kickout_register(GroupName, this, Circle.Center, visual.FloatArr[2], visual.CollisionGroup,
	                    TimerTime1, TimerTime2, CollisionBallSetZ, FieldMult, NotSomeFlag, ActiveFlag,
	                    zAttr ? 1 : 0, rAttr ? rAttr[0] : -1.0f, rAttr ? 1 : 0); // FTDBG
}

// Original 0x4e, which has no port symbol yet: "destroy the ball you are holding".
// Nothing in src/ or in upstream's control.cpp sends it, so the arm is reachable only
// once the enum gains a name for it -- see analysis/dragon-kickouts.md. 78 is free in
// MessageCode (75/76 magnet pair, 70/71 blocker snaps, 84..87 ramp/blocker, 67 hole).

int TKickout::Message(MessageCode code, float value)
{
	if (ft_accel_enabled() || ft_autoplay_enabled())
		fprintf(stderr, "DBG-kout  MSG %-12s code=%d value=%.4f captured=%d active=%d timer=%d\n",
		        GroupName ? GroupName : "?", static_cast<int>(code), value, BallCaputeredFlag,
		        ActiveFlag, Timer); // FTDBG
	switch (code)
	{
	case MessageCode::TKickoutRestartTimer:
		if (BallCaputeredFlag)
		{
			if (value < 0.0f)
				value = TimerTime1;
			Timer = timer::set(value, this, TimerExpired);
		}
		break;
	// 0x4b / 0x4c, the arms the port was missing. Both are in CADET.EXE's engine too --
	// the whole class is byte-identical across CADET/DRAGON/PIRATES.EXE apart from
	// relocations -- and neither upstream's control.cpp nor src/ sends them to a
	// kickout, so no table_rules.h switch is needed to keep Space Cadet still.
	case MessageCode::TMagnetFieldEnable: // 0x4b, arm 0x416439
		ActiveFlag = 1;
		break;
	case MessageCode::TMagnetFieldDisable: // 0x4c, arm 0x416443
		ActiveFlag = 0;
		if (BallCaputeredFlag)
		{
			if (Timer)
				timer::kill(Timer);
			TimerExpired(0, this);
			// TimerExpired has just armed the 0.05 s re-arm timer; the original kills
			// that too (0x416466..0x41646e) and zeroes the handle, which is what makes
			// 0x4c a real disable: ResetTimerExpired never runs, so ActiveFlag stays 0.
			if (Timer)
				timer::kill(Timer);
			Timer = 0;
		}
		break;
	case MessageCode::TKickoutDestroyBall: // 0x4e, arm 0x416480
		if (BallCaputeredFlag)
		{
			BallCaputeredFlag = 0;
			if (NotSomeFlag)
				ActiveFlag = 0;
			if (Timer)
			{
				timer::kill(Timer);
				Timer = 0;
			}
			if (Ball)
			{
				// 0x4164bd calls TBall::Disable (0x402c00) and 0x4164c5 decrements
				// [table+0x192], MultiballCount. No throw, no sound: the ball is gone.
				Ball->Disable();
				PinballTable->MultiballCount--;
				Ball = nullptr;
			}
		}
		break;
	case MessageCode::SetTiltLock: // 0x3f3, arm 0x4164db
		if (NotSomeFlag)
			ActiveFlag = 0;
		break;
	case MessageCode::PlayerChanged: // 0x3fe, arm 0x4164eb
		// Same release as Reset, then the per-player MessageField swap at 0x416517
		// call 0x413570. Dragon's Keep keeps its selected quest in a_kout2's
		// MessageField, which is how the selection stays with the player.
		if (BallCaputeredFlag)
		{
			if (Timer)
				timer::kill(Timer);
			TimerExpired(0, this);
		}
		if (NotSomeFlag)
			ActiveFlag = 0;
		FtPlayerFieldsSwap(value);
		break;
	case MessageCode::Reset: // 0x402, arm 0x416522
		if (BallCaputeredFlag)
		{
			if (Timer)
				timer::kill(Timer);
			TimerExpired(0, this);
		}
		if (NotSomeFlag)
			ActiveFlag = 0;
		// The original falls through to the base class's reset (0x413550), which zeroes
		// MessageField and the per-player copies; TPinballComponent::Message does that
		// for Reset (and, on 3D Pinball data, upstream's MessageField = 0).
		TPinballComponent::Message(code, value);
		break;
	default:
		break;
	}

	return 0;
}

void TKickout::Collision(TBall* ball, vector2* nextPosition, vector2* direction, float distance, TEdgeSegment* edge)
{
	if (!BallCaputeredFlag)
	{
		Ball = ball;
		Threshold = 1000000000.0;
		BallCaputeredFlag = 1;
		if (ft_accel_enabled() || ft_autoplay_enabled())
			fprintf(stderr, "DBG-kout  CAPTURE %-12s ballZ %.3f -> setZ %.3f mask=0x%x tilt=%d\n",
			        GroupName ? GroupName : "?", ball->Position.Z, CollisionBallSetZ,
			        ball->CollisionMask, PinballTable->TiltLockFlag); // FTDBG
		ball->CollisionComp = this;
		ball->Position.X = Circle.Center.X;
		ball->Position.Y = Circle.Center.Y;
		OriginalBallZ = ball->Position.Z;
		ball->Position.Z = CollisionBallSetZ;
		ball->CollisionDisabledFlag = true;
		if (PinballTable->TiltLockFlag) // [table+0x1b2], 0x416138
		{
			// 0x416141: dispatched through the vtable as 0x40 with 0.1f.
			Message(MessageCode::TKickoutRestartTimer, 0.1f);
		}
		else
		{
			loader::play_sound(SoftHitSoundId, ball, "TKickout1");
			// 0x41615f pushes 0x4a; the port's own number for it is ControlCollision.
			control::handler(MessageCode::ControlCollision, this);
		}
	}
	else
	{
		ball->Position.X = nextPosition->X;
		ball->Position.Y = nextPosition->Y;
		ball->RayMaxDistance -= distance;
		ball->not_again(edge);
	}
}

int TKickout::FieldEffect(TBall* ball, vector2* dstVec)
{
	vector2 direction{};

	if (BallCaputeredFlag)
		return 0;
	direction.X = Circle.Center.X - ball->Position.X;
	direction.Y = Circle.Center.Y - ball->Position.Y;
	if (direction.Y * direction.Y + direction.X * direction.X > Circle.RadiusSq)
		return 0;
	maths::normalize_2d(direction);
	dstVec->X = direction.X * FieldMult - ball->Direction.X * ball->Speed;
	dstVec->Y = direction.Y * FieldMult - ball->Direction.Y * ball->Speed;
	return 1;
}

void TKickout::TimerExpired(int timerId, void* caller)
{
	auto kick = static_cast<TKickout*>(caller);
	if (ft_accel_enabled() || ft_autoplay_enabled())
		fprintf(stderr, "DBG-koutrel  %-12s captured=%d ball=%p\n",
		        kick->GroupName ? kick->GroupName : "?", kick->BallCaputeredFlag,
		        static_cast<void*>(kick->Ball)); // FTDBG
	if (kick->BallCaputeredFlag)
	{
		kick->BallCaputeredFlag = 0;
		kick->Timer = timer::set(kick->TimerTime2, kick, ResetTimerExpired);
		if (kick->Ball)
		{		
			loader::play_sound(kick->HardHitSoundId, kick->Ball, "TKickout2");
			kick->Ball->Position.Z = kick->OriginalBallZ;
			kick->Ball->throw_ball(&kick->BallThrowDirection, kick->ThrowAngleMult, kick->ThrowSpeedMult1,
			                  kick->ThrowSpeedMult2);
			if (ft_accel_enabled() || ft_autoplay_enabled())
				fprintf(stderr, "DBG-koutthrow %-12s dir=(%.3f,%.3f,%.3f) angMult=%.3f m1=%.3f m2=%.3f"
				        " -> pos=(%.3f,%.3f,%.3f) bdir=(%.3f,%.3f,%.3f) spd=%.3f\n",
				        kick->GroupName ? kick->GroupName : "?", kick->BallThrowDirection.X,
				        kick->BallThrowDirection.Y, kick->BallThrowDirection.Z, kick->ThrowAngleMult,
				        kick->ThrowSpeedMult1, kick->ThrowSpeedMult2, kick->Ball->Position.X,
				        kick->Ball->Position.Y, kick->Ball->Position.Z, kick->Ball->Direction.X,
				        kick->Ball->Direction.Y, kick->Ball->Direction.Z, kick->Ball->Speed); // FTDBG
			kick->ActiveFlag = 0;
			kick->Ball = nullptr;
		}
	}
}

void TKickout::ResetTimerExpired(int timerId, void* caller)
{
	auto kick = static_cast<TKickout*>(caller);
	if (!kick->NotSomeFlag)
		kick->ActiveFlag = 1;
	kick->Timer = 0;
}
