#include "pch.h"
#include "TTimer.h"

#include "control.h"
#include "pb.h"
#include "timer.h"

TTimer::TTimer(TPinballTable* table, int groupIndex) : TPinballComponent(table, groupIndex, true)
{
	Timer = 0;
}

int TTimer::Message(MessageCode code, float value)
{
	switch (code)
	{
	case MessageCode::TTimerResetTimer:
		if (Timer)
			timer::kill(Timer);
		Timer = 0;
		// A negative value *cancels*; it does not arm. DRAGON.EXE's TTimer::Message is
		// at 0x41b380 and its 0x45 arm at 0x41b3bc kills the old timer, compares the
		// value against the 0.0 at 0x437790, and returns via 0x41b3d9 without arming
		// when it is below -- only 0x41b3df onwards calls timer::set.
		//
		// This matters on the bonus tables, and it is the opposite of what the same
		// code does on a TBlocker, which clamps with max(value, 0.0) at 0x4152b5 and
		// so fires immediately. Upstream gives both classes message 59 and both the
		// clamp; the bonus rules send -1.0f to `mode_timer` -- a TTimer -- meaning
		// "stop the quest clock", and with the clamp that tripped the clock instead of
		// stopping it. No Space Cadet caller sends 59 to a TTimer at all (its only
		// sender is control.cpp's blocker duration), so nothing there changes.
		if (value < 0.0f)
			break;
		Timer = timer::set(value, this, TimerExpired);
		break;
	case MessageCode::TTimerGetRemaining:
		// 0x41b3fb: no timer armed returns 0; otherwise the remaining time, clamped
		// at 0.0, times the 1000.0 at 0x4377a0, floored. Upstream's TTimer has no
		// such arm -- Space Cadet never asks -- and without it every bonus-table
		// countdown read 0 and the per-mode time display never started.
		if (!Timer)
			return 0;
		return static_cast<int>(std::floor(std::max(timer::remaining(Timer), 0.0f) * 1000.0f));
	case MessageCode::PlayerChanged:
		// Full Tilt, jump-table slot 0x3fe -> 0x41b453: swap the per-player MessageField
		// (0x41b45a call 0x413570), then kill the timer (0x41b467) exactly as the tilt,
		// game-over and reset arms do. Without the kill a clock started on one player's
		// ball could expire during the next player's. 3D Pinball's TTimer ignores
		// PlayerChanged.
		if (!pb::FullTiltMode)
			break;
		FtPlayerFieldsSwap(value);
		if (Timer)
		{
			timer::kill(Timer);
			Timer = 0;
		}
		break;
	case MessageCode::Reset:
		// 0x41b47c call 0x413550, then the shared timer kill at 0x41b483.
		FtPlayerFieldsReset();
		// fall through
	case MessageCode::SetTiltLock:
	case MessageCode::GameOver:
		if (Timer)
		{
			timer::kill(Timer);
			Timer = 0;
		}
		break;
	default: break;
	}
	return 0;
}


void TTimer::TimerExpired(int timerId, void* caller)
{
	auto timer = static_cast<TTimer*>(caller);
	timer->Timer = 0;
	control::handler(MessageCode::ControlTimerExpired, timer);
}
