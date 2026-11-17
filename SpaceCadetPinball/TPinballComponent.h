#pragma once

struct SpriteData;
struct vector2i;
struct zmap_header_type;
struct gdrv_bitmap8;
struct render_sprite;
struct component_control;
struct vector2;
class TPinballTable;


enum class MessageCode
{
	// Private codes <1000, different meaning for each component
	TFlipperNull = 0,
	TFlipperExtend = 1,
	TFlipperRetract = 2,

	TLightTurnOff = 0,
	TLightTurnOn = 1,
	TLightGetLightOnFlag = 2,
	TLightGetFlasherOnFlag = 3,
	TLightFlasherStart = 4,
	TLightApplyMultDelay = 5,
	TLightApplyDelay = 6,
	TLightFlasherStartTimed = 7,
	TLightTurnOffTimed = 8,
	TLightTurnOnTimed = 9,
	TLightSetOnStateBmpIndex = 11,
	TLightIncOnStateBmpIndex = 12,
	TLightDecOnStateBmpIndex = 13,
	TLightResetTimed = 14,
	TLightFlasherStartTimedThenStayOn = 15,
	TLightFlasherStartTimedThenStayOff = 16,
	TLightToggleValue = 17,
	TLightResetAndToggleValue = 18,
	TLightResetAndTurnOn = 19,
	TLightResetAndTurnOff = 20,
	TLightToggle = 21,
	TLightResetAndToggle = 22,
	TLightSetMessageField = 23,
	TLightFtTmpOverrideOn = -24,
	TLightFtTmpOverrideOff = -25,
	TLightFtResetOverride = -26,

	// Full-Tilt-only codes, recovered from CADET.EXE's engine with the component
	// classes that answer them. Upstream's private block stops at 67, so these keep
	// the originals' own numbers; 24 and 27 are shared with the TLight / TLightGroup
	// families, which is safe because no component class answers both.
	//
	// TLightSweepStart (27) is sent only to a type-1038 light_sweeps, with a float
	// selecting which of the group's canned sweeps to run. The sweep then overrides
	// the lights it crosses with TLightFtTmpOverrideOn, which is where upstream's
	// placeholder value for that code turns out to be exactly right.
	TLightSweepStart = 27,

	// 75/76, sent by both tables to the type-1042 magnets and to captive kickouts,
	// and answered by the type-1043 diverters as a third synonym for their gate pair.
	// All they do is flip ActiveFlag, which takes the device's collision circle in and
	// out of the world.
	// THole's private release code, Full Tilt only. Upstream's THole::Message
	// answers Reset alone, so a table that releases a captured ball this way --
	// Dragon's Keep's s_hole118 parks the ball and then lets it go -- held it
	// forever. Numbered as the original has it, like the magnet pair below.
	THoleReleaseBall = 67,

	TMagnetFieldEnable = 75,
	TMagnetFieldDisable = 76,

	// Original 0x4e, arm 0x416480 of DRAGON.EXE's TKickout::Message: destroy the ball
	// the kickout is holding rather than return it -- TBall::Disable at 0x4164bd and
	// --MultiballCount at 0x4164c5. 78 is the original's own number and nothing else
	// in this enum uses it.
	TKickoutDestroyBall = 78,

	// 84/85/86, raised at the rules as a ball moves through a ramp. The original's
	// TRamp sends 84 from its plane branch when the ball changes layer onto the ramp,
	// 85 from its 1301 wall and 86 from its 1302 wall; a type-1035 ramp exit sends the
	// 85/86 pair from its two one-sided edges. Both tables' rules read 86 as "a pass was
	// completed" and score on it -- dragon_part2.cpp and pirates_part2.cpp already cast
	// the raw 0x55/0x56 for exactly these -- and 84 has real consumers too, Dragon's
	// 0x54 arm and Skulduggery's a_ramp8, which releases a_blocg on it.
	//
	// Read off DRAGON.EXE's TRamp::Collision at 0x4177d0: the plane branch at 0x41791d
	// pushes 0x54, the Line2 branch at 0x4179a5 pushes 0x55, the Line3 branch 0x56.
	ControlRampPlaneCrossed = 84,
	ControlRampWall1Crossed = 85,
	ControlRampWall2Crossed = 86,

	TLightGroupNull = 0,
	TLightGroupStepBackward = 24,
	TLightGroupStepForward = 25,
	TLightGroupAnimationBackward = 26,
	TLightGroupAnimationForward = 27,
	TLightGroupLightShowAnimation = 28,
	TLightGroupGameOverAnimation = 29,
	TLightGroupRandomAnimationSaturation = 30,
	TLightGroupRandomAnimationDesaturation = 31,
	TLightGroupOffsetAnimationForward = 32,
	TLightGroupOffsetAnimationBackward = 33,
	TLightGroupReset = 34,
	TLightGroupTurnOnAtIndex = 35,
	TLightGroupTurnOffAtIndex = 36,
	TLightGroupGetOnCount = 37,
	TLightGroupGetLightCount = 38,
	TLightGroupGetMessage2 = 39,
	TLightGroupGetAnimationFlag = 40,
	TLightGroupResetAndTurnOn = 41,
	TLightGroupResetAndTurnOff = 42,
	TLightGroupRestartNotifyTimer = 43,
	TLightGroupFlashWhenOn = 44,
	TLightGroupToggleSplitIndex = 45,
	TLightGroupStartFlasher = 46,
	TLightGroupCountdownEnded = 47,

	TBumperSetBmpIndex = 11,
	TBumperIncBmpIndex = 12,
	TBumperDecBmpIndex = 13,

	TComponentGroupResetNotifyTimer = 48,

	TPopupTargetDisable = 49,
	TPopupTargetEnable = 50,

	TBlockerDisable = 51,
	TBlockerEnable = 52,
	TBlockerRestartTimeout = 59,

	// The rest of Full Tilt's moving-part protocol. Its TBlocker jump-tables 0x37..0x3c
	// at DRAGON.EXE 0x415398 (byte-identical in CADET.EXE and PIRATES.EXE), and 0x37/0x38
	// are the pair upstream calls TBlockerDisable/TBlockerEnable. The other four, and the
	// arrival notification the step callback raises, have no 3D Pinball counterpart.
	//
	// 57 and 58 are the originals' own 0x39 and 0x3a and have to keep those values:
	// src/pirates_*.cpp and src/dragon_missions.cpp already cast the raw numbers. They
	// collide numerically with ControlBallCaptured / ControlBallReleased, which is
	// harmless -- those two only ever travel to control::handler, never into a
	// component's Message().
	TBlockerSetPosition = 57,     // original 0x39, arm 0x4150ce: show position floor(value)
	TBlockerMoveToPosition = 58,  // original 0x3a, arm 0x4151fb: step towards it

	// 0x3b and 0x3c are RENUMBERED rather than kept, because the originals' 0x3b is
	// numerically 59 -- which is already TBlockerRestartTimeout here, upstream's own
	// value for a different message, and the one Space Cadet's control.cpp and Dragon's
	// armour handlers both send. Nothing in either table's rules sends 0x3b or 0x3c
	// (checked across src/), so no call site depends on the original numbers.
	TBlockerSnapOpen = 70,        // original 0x3b, arm 0x415245
	TBlockerSnapClosed = 71,      // original 0x3c, arm 0x415284

	// Original 0x3d, raised by the step callback at 0x414f00 once the part's position
	// index has reached its target, and delivered to the part's own ControlFunc.
	//
	// 87, not the original's own 61. Full Tilt's 0x3d and its 0x47 are two different
	// codes and the port's enum already spends 61 on the second of them
	// (ControlNotifyTimerExpired, which TLightGroup raises), so keeping the original's
	// number here made one value mean two things. That was survivable only while the
	// rules spelled both as raw casts; the moment either was bound to its symbol the
	// switch had two identical labels and would not compile. The port's numbering is
	// the port's -- nothing outside it reads these -- so this one moves.
	ControlBlockerMotionDone = 87,

	TGateDisable = 53,
	TGateEnable = 54,

	TKickoutRestartTimer = 55,

	TSinkUnknown7 = 7,
	TSinkResetTimer = 56,

	TSoloTargetDisable = 49,
	TSoloTargetEnable = 50,

	TTimerResetTimer = 59,
	TTimerGetRemaining = 72,      // original 0x48, arm 0x41b3fb; Full Tilt only

	ControlBallCaptured = 57,
	ControlBallReleased = 58,
	ControlTimerExpired = 60,
	ControlNotifyTimerExpired = 61,
	ControlSpinnerLoopReset = 62,
	ControlCollision = 63,
	ControlEnableMultiplier = 64,
	ControlDisableMultiplier = 65,
	ControlMissionComplete = 66,
	ControlMissionStarted = 67,

	// Public codes 1000+, apply to all components
	LeftFlipperInputPressed = 1000,
	LeftFlipperInputReleased = 1001,
	RightFlipperInputPressed = 1002,
	RightFlipperInputReleased = 1003,
	PlungerInputPressed = 1004,
	PlungerInputReleased = 1005,
	Pause = 1008,
	Resume = 1009,
	LooseFocus = 1010,
	SetTiltLock = 1011,
	ClearTiltLock = 1012,
	StartGamePlayer1 = 1013,
	NewGame = 1014,
	PlungerFeedBall = 1015,
	PlungerStartFeedTimer = 1016,
	PlungerLaunchBall = 1017,
	PlungerRelaunchBall = 1018,
	PlayerChanged = 1020,
	SwitchToNextPlayer = 1021,
	GameOver = 1022,
	Reset = 1024,
};

// Null-safe component type.
//
// The mode machines dispatch two kinds of message through one entry point: a real
// collision or timer, which carries the component it came from, and a mode-level
// notification like "this mode just started", which has no component and passes
// nullptr. The controllers then branch on the caller's type -- and a controller that
// does so unguarded crashes the moment a notification reaches it. Dragon's Keep
// segfaulted exactly there, in RescueMaidenController, on a flipper press during the
// egg exchange.
//
// Returning 0 is safe because it is not a type code: the DAT's object types start at
// 1000, so no branch can match it.
class TPinballComponent;
inline int ComponentTypeOf(const TPinballComponent* component);

class TPinballComponent
{
public:
	TPinballComponent(TPinballTable* table, int groupIndex, bool loadVisuals);
	virtual ~TPinballComponent();
	virtual int Message(MessageCode code, float value);
	virtual void port_draw();
	int get_scoring(unsigned int index) const;
	virtual vector2 get_coordinates();
	void SpriteSet(int index) const;
	void SpriteSetBall(int index, vector2i pos, float depth) const;

	// The object type code this component was spawned from -- 1000 TWall, 1005
	// TBumper, 1032 TTimer and so on, exactly the codes in the DAT's table_objects.
	//
	// The original reaches this through vtable slot 0, which the decompilation took
	// for the destructor; the Full Tilt dispatchers call it and switch the result
	// over 0x3ee/0x3f9/0x408/0x40e. Space Cadet never needs it, but the bonus tables
	// bind one handler to several component classes and discriminate at runtime --
	// every Dragon's Keep quest has to tell mode_timer (1032) from light_sweeps
	// (1038) when a timer expires, and both arrive through the same message.
	int GetComponentType() const { return ComponentType; }

	// Full Tilt keeps MessageField per player on every component. Its base class has
	// two helpers, the same bytes at the same addresses in CADET.EXE, DRAGON.EXE and
	// PIRATES.EXE:
	//   0x413550  zero MessageField and the four per-player copies at +0x1a..+0x26
	//   0x413570  copies[CurrentPlayer] = MessageField; MessageField = copies[floor(value)]
	// The base Message 0x4135b0 calls the first on 0x402 (Reset) and the second on 0x3fe
	// (PlayerChanged), and 26 of the overriding Message()s call them from their own
	// Reset / PlayerChanged arms. 3D Pinball keeps no such copy -- upstream gives one
	// only to TLight, TBumper, TLightGroup, TSink and TPopupTarget, each its own -- so
	// both helpers do nothing unless pb::FullTiltMode, which is what keeps PINBALL.DAT
	// exactly as it was.
	void FtPlayerFieldsReset();
	void FtPlayerFieldsSwap(float nextPlayer);

	int ComponentType = 0;
	char UnusedBaseFlag;
	char ActiveFlag;
	int MessageField;
	char* GroupName;
	component_control* Control;
	int GroupIndex;
	render_sprite* RenderSprite;
	TPinballTable* PinballTable;
	std::vector<SpriteData>* ListBitmap;
	// +0x1a..+0x26 in the Full Tilt engine; see FtPlayerFieldsSwap.
	int FtPlayerMessageField[4]{};
private:
	float VisualPosNormX;
	float VisualPosNormY;
};

inline int ComponentTypeOf(const TPinballComponent* component)
{
	return component ? component->GetComponentType() : 0;
}
