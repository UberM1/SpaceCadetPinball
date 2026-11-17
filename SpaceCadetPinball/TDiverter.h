#pragma once
#include "TCollisionComponent.h"

// Object type 1043 -- Dragon's Keep's eight `a_dvtr*` diverters, recovered from
// CADET.EXE, whose own table constructor spawns it for type 0x413.
//
// A diverter is a wall that can be taken out of the world: the edges it installs are
// gated on &ActiveFlag, so enabling it closes a path and disabling it opens one. They
// come in pairs (aa/ab, ba/bb, ...) with one of each pair open at a time, which is how
// the table steers a ball down one of two chutes.
//
// Structurally it is TGate with three differences, all visible:
//   * it starts ENABLED. TGate calls control::handler(Reset) from its constructor and
//     TBlocker -- the stand-in this replaces -- starts disabled; the original diverter
//     touches neither ActiveFlag nor its sprite and so inherits the collision
//     component's ActiveFlag = 1.
//   * it answers THREE enable/disable code pairs, not one: the gate pair, the blocker
//     pair and the Full-Tilt-only 0x4b/0x4c pair the magnets use. Dragon's Keep drives
//     them with the gate pair, which is why the TBlocker stand-in never moved them.
//   * it keeps ActiveFlag per player, and restores it on PlayerChanged.
// Its geometry is one segment doubled into a front and a back face, and the back face
// lets the ball through: see the comment on the constructor in TDiverter.cpp. The one
// message it sends is ControlCollision from that back face; no diverter in either table
// has a control function, so nothing listens today.
class TDiverter : public TCollisionComponent
{
public:
	TDiverter(TPinballTable* table, int groupIndex);
	int Message(MessageCode code, float value) override;
	void Collision(TBall* ball, vector2* nextPosition, vector2* direction, float distance,
	               TEdgeSegment* edge) override;

	int SoundIndex4;
	int SoundIndex3;
	// 407, default 0.1 s. It is how long the original holds its second bitmap after the
	// ball crosses the back face -- the flap's visible kick, which Collision does not
	// reproduce. Kept so the value is not silently lost.
	float TimerTime;

private:
	void SetActive(bool active, bool withSound);
	void InstallSegment(const vector2& p0, const vector2& p1, int collisionGroup, float offset);
	void InstallFace(const vector2& start, const vector2& end, int collisionGroup, float offset,
	                 void* wallValue);

	// The original tags its two faces with the literal markers 0 and 1 rather than with
	// a pointer; any non-null value does, so long as Collision only ever tests it.
	static void* BackFaceMarker() { return reinterpret_cast<void*>(1); }

	char PlayerActiveFlag[4]{};
};
