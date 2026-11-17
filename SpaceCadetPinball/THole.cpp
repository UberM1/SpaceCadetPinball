#include "pch.h"
#include "THole.h"


#include "control.h"
#include "loader.h"
#include "pb.h"
#include "TBall.h"
#include "timer.h"
#include "TPinballTable.h"
#include "TTableLayer.h"
#include "FtAccelDebug.h" // FTDBG

THole::THole(TPinballTable* table, int groupIndex) : TCollisionComponent(table, groupIndex, false)
{
	visualStruct visual{};
	circle_type circle{};

	Unknown4 = 0.050000001f;
	MessageField = 0;
	Timer = 0;
	BallCapturedFlag = false;
	Unknown3 = loader::query_float_attribute(groupIndex, 0, 407, 0.25f);
	GravityMult = loader::query_float_attribute(groupIndex, 0, 701, 0.2f);
	// 305 is the hole's own pull strength. Absent means the hole does not pull.
	GravityPull = loader::query_float_attribute(groupIndex, 0, 305, 0.0f);
	// 1306 (0x51a), read at 0x4167c3 into +0x92 with 0.0 when absent (0x4167de). Only
	// Dragon's s_hole118 has it, at 1.0; no 3D Pinball hole does, so PINBALL.DAT keeps
	// the fall below.
	LiftSpeed = loader::query_float_attribute(groupIndex, 0, 1306, 0.0f);

	loader::query_visual(groupIndex, 0, &visual);
	if (ft_accel_enabled() || ft_autoplay_enabled())
		fprintf(stderr, "DBG-hole  %-12s visGroup=0x%x floatArr=%p 1304=%.2f\n",
		        GroupName ? GroupName : "?", visual.CollisionGroup, static_cast<void*>(visual.FloatArr),
		        loader::query_float_attribute(groupIndex, 0, 1304, -1.0f)); // FTDBG
	// query_visual leaves FloatArr null when the group's float record does not start
	// with 600, i.e. the hole has no circle geometry at all. Everything below -- the
	// centre, the collision circle, the field insert -- is that geometry, so there is
	// no hole to build: skip it rather than install a hole at the origin.
	if (!visual.FloatArr)
	{
		Circle = {};
		Circle.RadiusSq = 0.001f;
		ZSetValue = 0.0f;
		CollisionMask = 0;
		ft_hole_register(GroupName, {}, 0.0f, visual.CollisionGroup, 0, 0.0f); // FTDBG
		return;
	}
	Circle.Center.X = visual.FloatArr[0];
	Circle.Center.Y = visual.FloatArr[1];
	// 306 scales the visual radius into the capture radius. Absent means unscaled: the
	// existing zero check below then clamps to the engine's own 0.001f floor.
	Circle.RadiusSq = loader::query_float_attribute(groupIndex, 0, 306, 0.0f) * visual.FloatArr[2];
	if (Circle.RadiusSq == 0.0f)
		Circle.RadiusSq = 0.001f;

	auto tCircle = new TCircle(this, &ActiveFlag, visual.CollisionGroup,
	                           reinterpret_cast<vector3*>(visual.FloatArr),
	                           Circle.RadiusSq);
	tCircle->place_in_grid(&AABB);
	EdgeList.push_back(tCircle);

	// 408 is the captured ball's z, and it is the attribute's *fourth* element plus the
	// table's own CollisionCompOffset -- DRAGON.EXE 0x4168f6 pushes 0x198 (408), and
	// 0x416929 reads `[table+0x152] + arr[3]` into ZSetValue at 0x416937. The port took
	// arr[2] and added nothing, which put every captured ball 0.114 too low on
	// s_hole100 and s_hole101 and 2.394 too *high* on s_hole118 -- that one is the
	// tunnel the ball was reported rendering oddly in. Absent means the hole does not
	// move the ball in z.
	auto zArr = loader::query_float_attribute_opt(groupIndex, 0, 408);
	ZSetValue = zArr ? PinballTable->CollisionCompOffset + zArr[3] : 0.0f;
	// 1304 is the collision layer the captured ball is moved onto. Absent means the
	// hole does not relayer the ball; 0 becomes layer 1 under Full Tilt's 1 << value,
	// which is the base playfield layer a ball already lives on.
	CollisionMask = static_cast<int>(floor(loader::query_float_attribute(groupIndex, 0, 1304, 0.0f)));

	// CollisionMask difference: 3DPB - value given as is, FT: mask = 1 << value
	if (pb::FullTiltMode)
		CollisionMask = 1 << CollisionMask;

	Circle.RadiusSq = visual.FloatArr[2] * visual.FloatArr[2];
	circle.RadiusSq = Circle.RadiusSq;
	circle.Center.X = Circle.Center.X;
	circle.Center.Y = Circle.Center.Y;

	if (ft_accel_enabled())
		fprintf(stderr, "DBG-hole  %-12s visGroup=0x%x releaseMask=0x%x\n",
		        GroupName ? GroupName : "?", visual.CollisionGroup, CollisionMask); // FTDBG
	ft_hole_register(GroupName, Circle.Center, Circle.RadiusSq, visual.CollisionGroup, CollisionMask, ZSetValue); // FTDBG
	Field.ActiveFlag = &ActiveFlag;
	Field.CollisionComp = this;
	Field.CollisionGroup = visual.CollisionGroup;
	TTableLayer::edges_insert_circle(&circle, nullptr, &Field);
}

int THole::Message(MessageCode code, float value)
{
	// Full Tilt, 0x416c70. Reset (0x416e47) and PlayerChanged (0x416e0b) share a tail:
	// clear or swap the per-player MessageField (0x416e49 call 0x413550 / 0x416e12 call
	// 0x413570), drop the release retry (0x413180 on +0xbe; the port retests every frame
	// instead), and if a ball is held kill the capture timer and start the release at
	// once (0x416e30 kill, 0x416e38 call 0x416a90, which is TimerExpired below).
	// 3D Pinball's THole answers Reset alone.
	if (code == MessageCode::PlayerChanged && !pb::FullTiltMode)
		return 0;
	if (code == MessageCode::Reset)
		FtPlayerFieldsReset();
	else if (code == MessageCode::PlayerChanged)
		FtPlayerFieldsSwap(value);
	if ((code == MessageCode::Reset || code == MessageCode::PlayerChanged) && BallCapturedFlag)
	{
		if (Timer)
			timer::kill(Timer);
		Timer = 0;
		BallCapturedSecondStage = true;
	}

	// SetTiltLock, 0x416cba: store -1.0 over the value and fall into the 0x43 arm at
	// 0x416cc2, whose value <= 0 branch releases at once (0x416cdb call 0x416a90).
	// Full Tilt only, like the arm it falls into.
	if (code == MessageCode::SetTiltLock && pb::FullTiltMode)
	{
		code = MessageCode::THoleReleaseBall;
		value = -1.0f;
	}

	// Release the captured ball after `value` seconds, or at once if it is not
	// positive. Space Cadet never sends this -- its holes release on their own
	// capture timer -- but the bonus tables park a ball and let it go on a rule's
	// say-so, and without this arm the ball is captured for good.
	if (code == MessageCode::THoleReleaseBall && BallCapturedFlag)
	{
		if (Timer)
			timer::kill(Timer);
		Timer = value > 0.0f ? timer::set(value, this, TimerExpired) : 0;
		if (!Timer)
			TimerExpired(0, this);
	}

	return 0;
}

void THole::Collision(TBall* ball, vector2* nextPosition, vector2* direction, float distance, TEdgeSegment* edge)
{
	if (ft_accel_enabled() || ft_autoplay_enabled())
		fprintf(stderr, "DBG-hole  HIT %-12s captured=%d ball=%p mask=0x%x\n",
		        GroupName ? GroupName : "?", BallCapturedFlag, static_cast<void*>(ball), ball->CollisionMask); // FTDBG
	if (!BallCapturedFlag)
	{
		BallCapturedSecondStage = false;
		Threshold = 1e9f;
		BallCapturedFlag = true;
		ball->CollisionComp = this;
		ball->Position.X = Circle.Center.X;
		ball->Position.Y = Circle.Center.Y;
		// 0x416a0e..0x416a14: the ball's z speed starts at 1306, 0.0 when absent.
		ball->Direction.Z = LiftSpeed;
		ball->CollisionDisabledFlag = true;

		// Ramp hole has no delay in FT.
		auto captureTime = pb::FullTiltMode ? 0 : 0.5f;
		Timer = timer::set(captureTime, this, TimerExpired);

		if (!PinballTable->TiltLockFlag)
		{
			loader::play_sound(HardHitSoundId, ball, "THole1");
			control::handler(MessageCode::ControlBallCaptured, this);
		}
	}
	else
	{
		DefaultCollision(ball, nextPosition, direction);
	}
}

int THole::FieldEffect(TBall* ball, vector2* vecDst)
{
	int result;
	vector2 direction{};

	if (BallCapturedFlag)
	{
		if (BallCapturedSecondStage)
		{
			// 0x416acb: with 1306 > 0 the ball rises at that constant speed
			// (0x416ae3 mov [ball+0xbf],[hole+0x92]) until z >= ZSetValue (0x416b5b test
			// ah,0x41 / jne 0x416b79); otherwise gravity pulls it down (0x416af1..
			// 0x416b0c) until z <= ZSetValue (0x416b6a test ah,0x1 / je 0x416b79). The
			// port used only the second, and since s_hole118's ZSetValue is above the
			// ball, z <= ZSetValue held on the first frame and the ball snapped up.
			const bool lift = LiftSpeed > 0.0f;
			if (lift)
				ball->Direction.Z = LiftSpeed;
			else
				ball->Direction.Z -= PinballTable->GravityDirVectMult * ball->TimeDelta * GravityMult;
			ball->Position.Z += ball->Direction.Z;
			if (lift ? ball->Position.Z >= ZSetValue : ball->Position.Z <= ZSetValue)
			{
				ball->Position.Z = ZSetValue;
				ball->Direction = {};
				ball->Speed = 0.0f;

				// The release is gated on the mouth being clear. The original splits
				// this in two -- its FieldEffect stops the ball here (DRAGON.EXE
				// 0x416b79: Direction zeroed) and sends itself 0x3f8, whose arm at
				// 0x416cea does the test and re-arms its own timer when the box is
				// occupied (0x416d8d) instead of releasing. Retesting here on the next
				// frame is the same loop without the extra timer: Direction.Z was just
				// zeroed, gravity takes z back below ZSetValue, and this branch runs
				// again.
				//
				// Without the gate a hole drops its ball on top of a ball already
				// standing in the mouth, which only happens during multiball -- and
				// both bonus tables have multiball. The box is the ball's position
				// plus or minus 2 x CollisionCompOffset, the same extent and the same
				// field TSink::TimerExpired uses (0x418509), and the count is filtered
				// to the layer being released onto, so the held ball -- which still
				// carries the hole's capture mask -- does not count itself.
				RectF mouth{};
				const auto margin = PinballTable->CollisionCompOffset * 2.0f;
				mouth.XMin = ball->Position.X - margin;
				mouth.XMax = ball->Position.X + margin;
				mouth.YMin = ball->Position.Y - margin;
				mouth.YMax = ball->Position.Y + margin;
				if (PinballTable->BallCountInRect(mouth, CollisionMask))
				{
					if (ft_accel_enabled() || ft_autoplay_enabled())
						fprintf(stderr, "DBG-holerel %-12s HELD, mouth occupied on mask 0x%x\n",
						        GroupName ? GroupName : "?", CollisionMask); // FTDBG
					return 0;
				}

				BallCapturedFlag = false;
				BallCapturedSecondStage = false;
				if (ft_accel_enabled() || ft_autoplay_enabled())
					fprintf(stderr, "DBG-holerel %-12s mask 0x%x -> 0x%x z=%.3f\n",
					        GroupName ? GroupName : "?", ball->CollisionMask, CollisionMask, ZSetValue); // FTDBG
				// 0x416dcc. Ends the ball's edge scanning for this frame, as every
				// other layer change does; the port was missing it here.
				ball->CollisionDisabledFlag = true;
				ball->CollisionMask = CollisionMask;
				ball->CollisionComp = nullptr;
				loader::play_sound(SoftHitSoundId, ball, "THole2");
				control::handler(MessageCode::ControlBallReleased, this);
			}
		}
		result = 0;
	}
	else
	{
		direction.X = Circle.Center.X - ball->Position.X;
		direction.Y = Circle.Center.Y - ball->Position.Y;
		if (direction.X * direction.X + direction.Y * direction.Y <= Circle.RadiusSq)
		{
			maths::normalize_2d(direction);
			vecDst->X = direction.X * GravityPull - ball->Direction.X * ball->Speed;
			vecDst->Y = direction.Y * GravityPull - ball->Direction.Y * ball->Speed;
			result = 1;
		}
		else
		{
			result = 0;
		}
	}
	return result;
}

void THole::TimerExpired(int timerId, void* caller)
{
	auto hole = static_cast<THole*>(caller);
	hole->Timer = 0;
	hole->BallCapturedSecondStage = true;
}
