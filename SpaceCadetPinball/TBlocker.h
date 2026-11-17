#pragma once
#include "TCollisionComponent.h"

// Two different classes share this name.
//
// Upstream's TBlocker is the 3D Pinball one, decompiled from pinball.exe: a two-state
// prop whose TBlockerEnable shows sprite 0, arms a timer and raises ControlTimerExpired,
// and whose TBlockerDisable hides it again.
//
// The Full Tilt engine's TBlocker is a *multi-position moving part* -- Dragon's Keep's
// gargoyle rocks, dragon jaw, sword, centre post and moat blocker; Skulduggery's cannon,
// gates and lane blockers. It carries a position index, one collision-edge set per
// position with its own active flag, a step timer that walks one position per tick, and
// a separate timeout timer that is the real TBlockerRestartTimeout. Nothing in it ever
// touches ActiveFlag or MessageField, which is itself load-bearing: Dragon's armour
// handlers park their stage in MessageField across a TBlockerDisable.
//
// Addresses (DRAGON.EXE; the engine is byte-identical in CADET.EXE and PIRATES.EXE at
// the same addresses, only relocations differ):
//   0x414d60  constructor, vtable 0x437560
//   0x414ee0  timeout-timer callback   -> control::handler(0x46 = ControlTimerExpired)
//   0x414f00  motion-step callback     -> control::handler(0x3d) once it has arrived
//   0x414f80  Message, jump table 0x415398 covering 0x37..0x3c, plus 0x45,
//             0x3f3 (SetTiltLock), 0x3fe (PlayerChanged), 0x402 (Reset)
//
// Both classes live here, selected by TableRules::MovingBlockers. The Space Cadet path
// is the old body verbatim, because its DrainBallBlockerControl depends on
// TBlockerEnable's own timer raising ControlTimerExpired, which the Full Tilt machine
// does not do -- it raises 0x3d from the step callback instead.
class TBlocker :
	public TCollisionComponent
{
public:
	// Full Tilt caps a blocker at 8 positions: the constructor asserts when
	// loader::query_visual_states(groupIndex) > 8 (0x414da1, assert line 0x6b3), and
	// the per-position flag array it fills runs +0x76..+0x7d with the position index
	// at +0x7e immediately after it.
	static const int MaxPositions = 8;

	TBlocker(TPinballTable* table, int groupIndex);
	int Message(MessageCode code, float value) override;

	static void TimerExpired(int timerId, void* caller);

	// 0x414f00. Walks one position towards TargetPosition per StepTime, and raises
	// MessageCode::ControlBlockerMotionDone when it is already there.
	static void MotionExpired(int timerId, void* caller);

	// 0x4131e0. A position change refused because a ball sits inside the part is
	// retried after 0.5 s rather than dropped.
	static void RetryExpired(int timerId, void* caller);

	int InitialDuration;
	int ExtendedDuration;
	int Timer;
	int SoundIndex4;
	int SoundIndex3;

	// --- Full Tilt moving-part state. Inert when MovingPart is false. -------------

	// Which of the two classes this instance is, fixed at construction from
	// TableRules::MovingBlockers.
	bool MovingPart = false;

	// +0x76..+0x7d. One active flag per position, handed to install_wall as that
	// position's TEdgeSegment::ActiveFlagPtr, so exactly the current position's edges
	// collide. Upstream keys its single wall to ActiveFlag instead.
	char PositionFlags[MaxPositions] = {};

	// +0x7e. The position currently shown, -1 for "retracted, no sprite". An int, not
	// a float: 0x41516b stores the truncated index straight into it and 0x414f18
	// compares it against +0x92 with `cmp DWORD PTR [esi+0x7e],eax`. (TSink's +0x7e is
	// a float TimerTime -- a different class at the same offset; see
	// src/dragon_part3.cpp.)
	int CurrentPosition = -1;

	// +0x92. The position the step timer is walking towards.
	int TargetPosition = -1;

	// +0x82. Where Reset / SetTiltLock / PlayerChanged put it. DAT attribute 409,
	// default -1 (0x414e7c..0x414ea5).
	int HomePosition = -1;

	// +0x96. Seconds per position step. DAT attribute 903, default 0.1
	// (0x414e57..0x414e7b).
	float StepTime = 0.1f;

	// +0x9a. The step timer, callback 0x414f00.
	int MotionTimer = 0;

	// +0x9e..+0xad. The deferred-retry record, a four-field struct in the original
	// {timer, code, value, component}.
	int RetryTimer = 0;
	MessageCode RetryCode = static_cast<MessageCode>(0);
	float RetryValue = 0.0f;

private:
	void SetPosition(MessageCode code, float value);
	int LastPosition() const;
	void KillMotion();
	void KillRetry();
	void ArmMotion(float delay);
	int FtMessage(MessageCode code, float value);
};
