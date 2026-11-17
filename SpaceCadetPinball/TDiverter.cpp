#include "pch.h"
#include "TDiverter.h"

#include "control.h"
#include "loader.h"
#include "maths.h"
#include "render.h"
#include "TBall.h"
#include "TLine.h"
#include "TPinballTable.h"

// The original builds its own edges rather than letting TCollisionComponent do it: it
// passes createWall = false and then calls its own wall builder (DRAGON.EXE 0x41ba30)
// with the attribute-600 payload, the component's collision group, and the two marker
// values 0 and 1. That builder puts TWO one-sided TLines on the one segment -- the same
// endpoints wound in opposite directions, so each is only reachable from its own side --
// offset by +CollisionCompOffset and -0.8 * CollisionCompOffset, and tags them with the
// two markers so Collision can tell front from back. Both carry &ActiveFlag, so a
// disabled diverter is simply not in the world.
//
// The builder takes one segment, and the constructor (0x41bb70) feeds it from one of
// two places. When attribute 600 describes a single line -- visual.FloatArrCount == 2,
// query_visual's encoding of that -- it is that line. Otherwise (0x41bbeb) it reads
// attribute 604: records of six floats, {2.0, x0, y0, x1, y1, z}, walked 0x18 bytes at
// a time for as long as the leading float equals the 2.0 at 0x4377c0, each handed to
// the same builder. Every a_dvtr* in DRAGON.DAT carries only 604, so reading 600 alone
// left all of Dragon's diverters without a wall.
TDiverter::TDiverter(TPinballTable* table, int groupIndex) : TCollisionComponent(table, groupIndex, false)
{
	visualStruct visual{};
	loader::query_visual(groupIndex, 0, &visual);
	SoundIndex4 = visual.SoundIndex4;
	SoundIndex3 = visual.SoundIndex3;
	TimerTime = loader::query_float_attribute(groupIndex, 0, 407, 0.1f);

	if (visual.FloatArrCount == 2 && visual.FloatArr)
	{
		InstallSegment({visual.FloatArr[0], visual.FloatArr[1]}, {visual.FloatArr[2], visual.FloatArr[3]},
		               visual.CollisionGroup, table->CollisionCompOffset);
		return;
	}

	// Neither attribute and the original installs nothing.
	auto segments = loader::query_float_attribute_opt(groupIndex, 0, 604);
	if (!segments)
		return;
	for (auto record = segments; record[0] == 2.0f; record += 6)
	{
		InstallSegment({record[1], record[2]}, {record[3], record[4]}, visual.CollisionGroup,
		               table->CollisionCompOffset);
	}
}

void TDiverter::InstallSegment(const vector2& p0, const vector2& p1, int collisionGroup, float offset)
{
	InstallFace(p0, p1, collisionGroup, offset, nullptr);
	// 0.8 is the original's constant, and the asymmetry is the point: the back face sits
	// closer to the segment than the front one, so a ball arriving from behind reaches it
	// first and is let through rather than bounced.
	InstallFace(p1, p0, collisionGroup, -offset * 0.8f, BackFaceMarker());
}

void TDiverter::InstallFace(const vector2& start, const vector2& end, int collisionGroup, float offset,
                            void* wallValue)
{
	auto line = new TLine(this, &ActiveFlag, collisionGroup, start, end);
	line->WallValue = wallValue;
	line->Offset(offset);
	line->place_in_grid(&AABB);
	EdgeList.push_back(line);
}

void TDiverter::Collision(TBall* ball, vector2* nextPosition, vector2* direction, float distance,
                          TEdgeSegment* edge)
{
	if (!edge->WallValue)
	{
		// The front face is an ordinary wall, except that the original never reaches
		// TCollisionComponent::Collision's hard-hit branch: it plays only the soft hit
		// sound, and only above the same 0.2 threshold the base class uses.
		if (PinballTable->TiltLockFlag)
		{
			maths::basic_collision(ball, nextPosition, direction, Elasticity, Smoothness, 1000000000.0, 0.0);
			return;
		}

		auto projSpeed = maths::basic_collision(ball, nextPosition, direction, Elasticity, Smoothness,
		                                        Threshold, Boost);
		if (projSpeed > 0.2f && SoftHitSoundId)
			loader::play_sound(SoftHitSoundId, ball, "TDiverter3");
		return;
	}

	// The back face lets the ball through -- this is the one-sidedness, and it is why a
	// ball coming the wrong way down a closed chute is not bounced back up it.
	ball->not_again(edge);
	ball->RayMaxDistance -= distance;
	ball->Position.X = nextPosition->X;
	ball->Position.Y = nextPosition->Y;

	if (PinballTable->TiltLockFlag)
		return;

	if (HardHitSoundId)
		loader::play_sound(HardHitSoundId, ball, "TDiverter4");
	// No diverter in either table has a control function, so this reaches nothing today;
	// it is here because the original sends it, and because it corrects the note that
	// used to stand in TDiverter.h claiming a diverter never reports.
	control::handler(MessageCode::ControlCollision, this);

	// NOT REPRODUCED: the original follows this with a sprite flip -- when the component
	// has more than one bitmap it sets the second for TimerTime seconds and a timer
	// callback (0x41bc50) puts the first back, i.e. the flap visibly kicks as the ball
	// crosses it. Left out because SetActive below drives the same sprite from the
	// enable/disable state, and the two would have to be reconciled; the ball's path is
	// unaffected either way.
}

void TDiverter::SetActive(bool active, bool withSound)
{
	ActiveFlag = active ? 1 : 0;
	SpriteSet(active ? 0 : -1);
	if (withSound)
		loader::play_sound(active ? SoundIndex4 : SoundIndex3, this, active ? "TDiverter2" : "TDiverter1");
}

int TDiverter::Message(MessageCode code, float value)
{
	switch (code)
	{
	case MessageCode::TBlockerEnable:
	case MessageCode::TGateEnable:
	case MessageCode::TMagnetFieldEnable:
		SetActive(true, true);
		break;
	case MessageCode::TBlockerDisable:
	case MessageCode::TGateDisable:
	case MessageCode::TMagnetFieldDisable:
		SetActive(false, true);
		break;
	case MessageCode::PlayerChanged:
		{
			// Hand this player's state back and take up the next one's. The sound plays
			// on the restore too: the original re-enters Message with the blocker pair.
			// The per-player MessageField swap comes first, 0x41befa call 0x413570.
			FtPlayerFieldsSwap(value);
			PlayerActiveFlag[PinballTable->CurrentPlayer] = ActiveFlag;
			auto next = static_cast<int>(value);
			if (next >= 0 && next < 4)
				SetActive(PlayerActiveFlag[next] == 1, true);
		}
		break;
	case MessageCode::Reset:
		// 0x41bf4c call 0x413550, before the per-player flags are cleared.
		FtPlayerFieldsReset();
		for (auto& flag : PlayerActiveFlag)
			flag = 0;
		// The original's Reset falls straight into its disable path, sound included.
		SetActive(false, true);
		break;
	default:
		break;
	}

	return 0;
}
