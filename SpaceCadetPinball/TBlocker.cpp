#include "pch.h"
#include "TBlocker.h"


#include "control.h"
#include "loader.h"
#include "render.h"
#include "timer.h"
#include "TEdgeSegment.h"
#include "TPinballTable.h"
#include "table_rules.h"
#include "FtAccelDebug.h" // FTDBG

// Which of the two TBlocker classes this build of the table wants. Read before the
// member initialiser list runs, because it selects the base constructor's createWall:
// the Full Tilt class installs one wall per position itself, keyed to that position's
// own flag, and must not also get TCollisionComponent's single ActiveFlag wall.
static bool MovingBlockersEnabled()
{
	return CurrentTable && CurrentTable->MovingBlockers;
}

TBlocker::TBlocker(TPinballTable* table, int groupIndex)
	: TCollisionComponent(table, groupIndex, !MovingBlockersEnabled())
{
	visualStruct visual{};

	loader::query_visual(groupIndex, 0, &visual);
	SoundIndex4 = visual.SoundIndex4;
	SoundIndex3 = visual.SoundIndex3;
	InitialDuration = 55;
	ExtendedDuration = 5;
	Threshold = 1000000000.0f;
	Timer = 0;
	MovingPart = MovingBlockersEnabled();

	if (!MovingPart)
	{
		// Space Cadet, verbatim as before.
		MessageField = 0;
		ActiveFlag = 0;
		SpriteSet(-1);
		return;
	}

	// --- Full Tilt, DRAGON.EXE 0x414d60 ------------------------------------------
	//
	// ActiveFlag stays the 1 TCollisionComponent set: the original's Message never
	// writes +0x5, and collision is gated per position by PositionFlags instead.
	// MessageField is cleared by the Reset message at the end, not here -- the
	// original ends its constructor with Message(0x402, 0.0) at 0x414ec0.
	if (groupIndex > 0)
	{
		// 0x414d97: query_visual_states, asserting on more than 8 (0x414da1). The
		// port clamps rather than asserting, because an assert here would take a
		// table down for data the rules can simply not reach.
		int count = loader::query_visual_states(groupIndex);
		if (count > MaxPositions)
			count = MaxPositions;

		// 0x414dcb..0x414df7: one wall per position, from attribute 600 of that
		// position's state record, each bound to its own flag byte. Dragon's a_sword,
		// a_jaw, a_center_post and the two rocks have no wall at all on some or all
		// positions, which is why the lookup is the non-erroring form.
		for (int i = 0; i < count; i++)
		{
			float* floatArr = loader::query_float_attribute_opt(groupIndex, i, 600);
			if (floatArr)
				TEdgeSegment::install_wall(floatArr, this, &PositionFlags[i], visual.CollisionGroup,
				                           table->CollisionCompOffset, 0);
		}

		// 0x414e07..0x414e56: attribute 407 carries the two armour durations as
		// floats, cast to int. Upstream hardcodes the 55 / 5 fallback the original
		// uses when the attribute is absent, which is why those two lines above are
		// still the defaults rather than being replaced.
		if (float* durations = loader::query_float_attribute_opt(groupIndex, 0, 407))
		{
			InitialDuration = static_cast<int>(durations[0]);
			ExtendedDuration = static_cast<int>(durations[1]);
		}

		// 0x414e57..0x414e7b: attribute 903, seconds per step, default 0.1.
		if (float* step = loader::query_float_attribute_opt(groupIndex, 0, 903))
			StepTime = *step;

		// 0x414e7c..0x414ea5: attribute 409, the home position, default -1.
		if (float* home = loader::query_float_attribute_opt(groupIndex, 0, 409))
			HomePosition = static_cast<int>(*home);

		if (ft_blockdbg_enabled())
			fprintf(stderr, "DBG-block ctor %-12s states=%d home=%d step=%.3f dur=%d/%d walls=%zu\n",
			        GroupName ? GroupName : "?", count, HomePosition, StepTime,
			        InitialDuration, ExtendedDuration, EdgeList.size());
	}

	Message(MessageCode::Reset, 0.0f);
}

int TBlocker::Message(MessageCode code, float value)
{
	if (MovingPart)
		return FtMessage(code, value);

	switch (code)
	{
	case MessageCode::SetTiltLock:
	case MessageCode::PlayerChanged:
	case MessageCode::Reset:
	case MessageCode::TBlockerDisable:
		if (Timer)
		{
			timer::kill(Timer);
			Timer = 0;
		}
		MessageField = 0;
		ActiveFlag = 0;
		SpriteSet(-1);
		if (code == MessageCode::TBlockerDisable)
			loader::play_sound(SoundIndex3, this, "TBlocker1");
		break;
	case MessageCode::TBlockerEnable:
		ActiveFlag = 1;
		loader::play_sound(SoundIndex4, this, "TBlocker2");
		SpriteSet(0);
		if (Timer)
			timer::kill(Timer);
		Timer = 0;
		// Strictly positive, not >= 0. The originals' moving-part Message (0x414f80
		// case 0x38) guards this with `if (0.0 < param_2)` and sends itself 0x3a --
		// walk to the last frame -- otherwise, which arms the *motion* timer and
		// raises 0x3d, not 0x46. With >= 0, a close with value 0 armed a zero-delay
		// ControlTimerExpired instead, and Dragon's Keep's moat blocker then span:
		// its own handler answers that expiry with another TBlockerEnable(0.0).
		// timer::check only ever fires the two timers at the head of its sorted list
		// per frame, so a component re-arming at zero delay starves every other
		// timer in the game -- including the end-of-ball text box, which is what hung
		// the drain bonus count-up and with it the next ball. Space Cadet is
		// unaffected: DrainBallBlockerControl passes InitialDuration or -1, never 0.
		if (value > 0)
			Timer = timer::set(value, this, TimerExpired);
		break;
	case MessageCode::TBlockerRestartTimeout:
		if (Timer)
			timer::kill(Timer);
		Timer = timer::set(std::max(value, 0.0f), this, TimerExpired);
		break;
	default:
		break;
	}

	return 0;
}

// The last position in the sprite list, and -1 when there is no list at all. The
// original spells it `list ? list->Data->Count - 1 : 0 - 1` three times over, at
// 0x41504d, 0x415088 and 0x415245.
int TBlocker::LastPosition() const
{
	return ListBitmap ? static_cast<int>(ListBitmap->size()) - 1 : -1;
}

void TBlocker::KillMotion()
{
	if (MotionTimer)
	{
		timer::kill(MotionTimer);
		MotionTimer = 0;
	}
}

void TBlocker::KillRetry()
{
	if (RetryTimer)
	{
		timer::kill(RetryTimer);
		RetryTimer = 0;
	}
}

void TBlocker::ArmMotion(float delay)
{
	KillMotion();
	MotionTimer = timer::set(delay, this, MotionExpired);
}

// 0x4150ce, the 0x39 arm: show one position now.
void TBlocker::SetPosition(MessageCode code, float value)
{
	// __ftol truncates towards zero, so -1.0 is -1 and 0.9 is 0.
	const int index = static_cast<int>(value);

	KillRetry();
	KillMotion();

	// 0x41510e: a part never moves into a ball. The box is the part's own AABB,
	// which on the Full Tilt path is the union of every position's edges because
	// install_wall merged each of them into it. Retracting (index < 0) is always
	// allowed; extending is deferred by 0.5 s and retried, not dropped (0x4131a0,
	// whose callback 0x4131e0 re-sends the same code and value).
	if (index >= 0 && PinballTable->BallCountInRect(AABB))
	{
		RetryCode = code;
		RetryValue = value;
		RetryTimer = timer::set(0.5f, this, RetryExpired);
		return;
	}

	// 0x415130: clamp the *old* index into range before clearing its flag, which
	// matters only if the sprite list shrank under it. Kept because it is what the
	// original does, not because a DAT is known to need it.
	if (!ListBitmap)
	{
		if (CurrentPosition >= 0)
			CurrentPosition = -1;
	}
	else if (static_cast<int>(ListBitmap->size()) <= CurrentPosition)
	{
		CurrentPosition = static_cast<int>(ListBitmap->size()) - 1;
	}

	if (CurrentPosition >= 0 && CurrentPosition < MaxPositions)
		PositionFlags[CurrentPosition] = 0;

	CurrentPosition = index;
	if (index < 0 || !ListBitmap || index >= static_cast<int>(ListBitmap->size()))
	{
		// 0x4151db / 0x4151eb: no position, or no bitmap for it -- blank the sprite.
		SpriteSet(-1);
		return;
	}

	if (index < MaxPositions)
		PositionFlags[index] = 1;
	if (ft_blockdbg_enabled())
		fprintf(stderr, "DBG-block move %-12s -> %d\n", GroupName ? GroupName : "?", index);
	// 0x4151b4: bitmap and zmap indexed by position, placed at the bitmap's own
	// XPosition/YPosition less the table origin -- exactly SpriteSet.
	SpriteSet(index);
}

// 0x414f80, the Full Tilt class.
int TBlocker::FtMessage(MessageCode code, float value)
{
	switch (code)
	{
	// 0x37 at 0x414fd3. Retract: after `value` seconds if positive, immediately
	// otherwise -- in which case it dispatches 0x3a(-1.0) to itself. Note it does not
	// touch MessageField or ActiveFlag; Dragon's armour handlers keep their stage in
	// MessageField across this message.
	case MessageCode::TBlockerDisable:
		if (value > 0.0f)
		{
			TargetPosition = -1;
			ArmMotion(value);
		}
		else
		{
			FtMessage(MessageCode::TBlockerMoveToPosition, -1.0f);
		}
		loader::play_sound(SoundIndex3, this, "TBlocker1");
		break;

	// 0x38 at 0x415038. The mirror image, aiming at the last position.
	case MessageCode::TBlockerEnable:
		if (value > 0.0f)
		{
			TargetPosition = LastPosition();
			ArmMotion(value);
		}
		else
		{
			FtMessage(MessageCode::TBlockerMoveToPosition, static_cast<float>(LastPosition()));
		}
		loader::play_sound(SoundIndex4, this, "TBlocker2");
		break;

	// 0x39 at 0x4150ce.
	case MessageCode::TBlockerSetPosition:
		SetPosition(code, value);
		break;

	// 0x3a at 0x4151fb. Aim, then step there one position per StepTime.
	case MessageCode::TBlockerMoveToPosition:
		TargetPosition = static_cast<int>(value);
		ArmMotion(StepTime);
		break;

	// 0x3b at 0x415245: snap fully open. Nothing in either table's rules sends it,
	// which is what let it be renumbered away from the 59 collision below.
	case MessageCode::TBlockerSnapOpen:
		FtMessage(MessageCode::TBlockerSetPosition, static_cast<float>(LastPosition()));
		loader::play_sound(SoundIndex4, this, "TBlocker2");
		break;

	// 0x3c at 0x415284: snap fully closed.
	case MessageCode::TBlockerSnapClosed:
		FtMessage(MessageCode::TBlockerSetPosition, -1.0f);
		loader::play_sound(SoundIndex3, this, "TBlocker1");
		break;

	// 0x45 at 0x4152a5 -- the real TBlockerRestartTimeout, and a timer of its own,
	// separate from the motion timer. The original computes max(value, 0.0) with an
	// fcom/fxch pair at 0x4152b5..0x4152d9 and arms callback 0x414ee0, which raises
	// ControlTimerExpired (0x46). So a negative value fires the timeout immediately;
	// it is NOT a cancel, whatever src/dragon_part2.cpp's note says.
	case MessageCode::TBlockerRestartTimeout:
		if (Timer)
			timer::kill(Timer);
		Timer = timer::set(std::max(value, 0.0f), this, TimerExpired);
		break;

	// 0x3f3 at 0x4152a5's neighbour 0x415340, reached from the jg/je chain at
	// 0x414fad. 0x3f3 is SetTiltLock, verified from the sender: TPinballTable::Message
	// at 0x419765 broadcasts it to every component and then sends 8
	// (TLightTurnOffTimed) to the table's light group, which is upstream's
	// TPinballTable::tilt() line for line.
	//
	// 0x402 (Reset, 0x415339) runs TPinballComponent::Reset (0x413550) first; 0x3fe
	// (PlayerChanged, 0x4152e9) runs the base's player-field swap (0x413570). Both
	// then fall into the same tail: drop both timers and go to HomePosition.
	case MessageCode::SetTiltLock:
	case MessageCode::Reset:
	case MessageCode::PlayerChanged:
		// The moving class only exists on Full Tilt data (MovingBlockers), so the
		// helpers' own FullTiltMode gate is always open here.
		if (code == MessageCode::Reset)
			FtPlayerFieldsReset();
		else if (code == MessageCode::PlayerChanged)
			FtPlayerFieldsSwap(value);
		if (Timer)
		{
			timer::kill(Timer);
			Timer = 0;
		}
		KillMotion();
		FtMessage(MessageCode::TBlockerSetPosition, static_cast<float>(HomePosition));
		break;

	default:
		break;
	}

	return 0;
}

void TBlocker::TimerExpired(int timerId, void* caller)
{
	auto blocker = static_cast<TBlocker*>(caller);
	blocker->Timer = 0;
	control::handler(MessageCode::ControlTimerExpired, blocker);
}

// 0x414f00.
void TBlocker::MotionExpired(int timerId, void* caller)
{
	auto blocker = static_cast<TBlocker*>(caller);
	blocker->MotionTimer = 0;

	// 0x414f18: arrived. The notification goes to the rules, not to a component.
	if (blocker->CurrentPosition == blocker->TargetPosition)
	{
		control::handler(MessageCode::ControlBlockerMotionDone, blocker);
		return;
	}

	// 0x414f35: a ball inside the part stalls the step but not the timer, so the
	// part resumes by itself once the ball has rolled off.
	if (!blocker->PinballTable->BallCountInRect(blocker->AABB))
	{
		const int next = blocker->TargetPosition > blocker->CurrentPosition
			                 ? blocker->CurrentPosition + 1
			                 : blocker->CurrentPosition - 1;
		blocker->Message(MessageCode::TBlockerSetPosition, static_cast<float>(next));
	}

	// 0x414f60: re-armed unconditionally, and after the step -- which has just
	// zeroed MotionTimer itself.
	blocker->MotionTimer = timer::set(blocker->StepTime, blocker, MotionExpired);
}

// 0x4131e0.
void TBlocker::RetryExpired(int timerId, void* caller)
{
	auto blocker = static_cast<TBlocker*>(caller);
	blocker->RetryTimer = 0;
	blocker->Message(blocker->RetryCode, blocker->RetryValue);
}
