#include "pch.h"
#include "TKickback.h"


#include "control.h"
#include "loader.h"
#include "maths.h"
#include "render.h"
#include "timer.h"
#include "TPinballTable.h"

TKickback::TKickback(TPinballTable* table, int groupIndex): TCollisionComponent(table, groupIndex, true)
{
	MessageField = 0;
	Timer = 0;
	KickActiveFlag = 0;
	Suppressed = 0;
	TimerTime = 0.7f;
	TimerTime2 = 0.1f;
	Threshold = 1000000000.0f;
}

int TKickback::Message(MessageCode code, float value)
{
	// DRAGON.EXE 0x415d70. Upstream had only the SetTiltLock / Reset pair, and retracted
	// with SpriteSet(-1) where the original uses sprite 0 (0x415e0f reads the bitmap
	// list's entry 0, not a hide).
	switch (code)
	{
	case MessageCode::TMagnetFieldDisable: // 0x4c at 0x415da7: suppress, nothing else
		Suppressed = 1;
		return 0;

	case MessageCode::PlayerChanged: // 0x3fe at 0x415db8: base class only
		TCollisionComponent::Message(code, value);
		return 0;

	case MessageCode::Reset: // 0x402 at 0x415dcb: base Reset, then the shared tail
		TCollisionComponent::Message(code, value);
		break;

	case MessageCode::TMagnetFieldEnable: // 0x4b
	case MessageCode::SetTiltLock:        // 0x3f3
		break;

	default:
		return 0;
	}

	// The shared disarm tail, 0x415dd2 onwards. Note that it clears both flags and
	// restores the threshold whether or not a timer was pending.
	Suppressed = 0;
	KickActiveFlag = 0;
	Threshold = 1000000000.0f;
	if (Timer)
		timer::kill(Timer);
	Timer = 0;
	SpriteSet(0);
	return 0;
}

void TKickback::Collision(TBall* ball, vector2* nextPosition, vector2* direction, float distance,
                          TEdgeSegment* edge)
{
	if (PinballTable->TiltLockFlag)
	{
		maths::basic_collision(ball, nextPosition, direction, Elasticity, Smoothness, 0.0,
		                       Boost);
		return;
	}

	// DRAGON.EXE 0x415ba0, and the class is one shared engine class: the bytes at that
	// address are identical in CADET.EXE, DRAGON.EXE and PIRATES.EXE, same vtable slot,
	// same offsets. So this is a correction to upstream rather than a Full Tilt variant.
	//
	// The shape is a one-shot, two-stage animation:
	//
	//   Collision, first touch -> KickActiveFlag = 1, arm TimerTime  -> ArmExpired
	//   ArmExpired              -> Threshold = 0, sprite 1, arm TimerTime2 -> RetractExpired
	//   Collision, now over the zero threshold -> clear the flag, kill the pending
	//                              timer, arm TimerTime2 -> RetractExpired
	//   RetractExpired          -> sprite 0, Threshold back to 1e9, and notify the
	//                              rules *only if the flag was cleared*, i.e. only if a
	//                              ball was actually kicked.
	//
	// Upstream had one callback that branched on KickActiveFlag and, in the armed
	// branch, played HardHitSoundId and re-armed *itself* every TimerTime2. The flag is
	// only cleared by a qualifying collision, so a ball that merely passes the kickback
	// without one left it armed forever -- replaying the clip ten times a second, which
	// is a deafening overlap rather than a sound. A player hit it twice on Dragon's
	// Keep: "entering the left drain channel ... the audio deafens you". Neither of the
	// original's two callbacks plays anything.
	// 0x415bd7 tests the suppressor first, then the flag; failing either skips straight
	// to the collision below, so a suppressed kickback is still a wall.
	if (!Suppressed && !KickActiveFlag)
	{
		KickActiveFlag = 1;
		Timer = timer::set(TimerTime, this, ArmExpired);
	}

	if (DefaultCollision(ball, nextPosition, direction))
	{
		// Settled: the original really does play two clips on a kick. Its
		// DefaultCollision is 0x413ab0, and the field it plays on the branch that
		// returns 1 is [this+0x72] at 0x413b2b -- the same HardHitSoundId that 0x415c1f
		// then plays again. (0x413b58 plays [this+0x6e], SoftHitSoundId, on the branch
		// that returns 0.) Two overlapping voices, once per kick, which is bounded and
		// is presumably the point: the kicker is meant to sound louder than a wall.
		loader::play_sound(HardHitSoundId, this, "TKickback kick"); // 0x415c1f
		KickActiveFlag = 0;
		if (Timer)
			timer::kill(Timer);
		Timer = timer::set(TimerTime2, this, RetractExpired);
	}
}

// 0x415c60. Arms the kicker: the threshold drops to zero so the next contact counts as
// a kick, and the raised sprite goes up.
void TKickback::ArmExpired(int timerId, void* caller)
{
	auto kick = static_cast<TKickback*>(caller);
	kick->Threshold = 0.0f;
	kick->SpriteSet(1);
	kick->Timer = timer::set(kick->TimerTime2, kick, RetractExpired);
}

// 0x415ce0. Retracts it, and reports the kick.
void TKickback::RetractExpired(int timerId, void* caller)
{
	auto kick = static_cast<TKickback*>(caller);
	kick->SpriteSet(0);
	kick->Timer = 0;
	kick->Threshold = 1000000000.0f;
	// 0x415d49: the notification is conditional on the flag already being clear, which
	// is true only when a collision cleared it -- that is, only when a ball was kicked.
	// Arming and timing out quietly is not an event.
	if (!kick->KickActiveFlag)
		control::handler(MessageCode::ControlTimerExpired, kick);
	kick->KickActiveFlag = 0;
}
