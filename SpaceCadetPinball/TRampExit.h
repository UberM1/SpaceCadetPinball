#pragma once
#include "TCollisionComponent.h"

struct vector2;

// Object type 1035 -- `s_rexit*`, recovered from CADET.EXE: constructor 0x41b4f0,
// Collision 0x41b6b0, vtable 0x4377e0, object size 0x86. CADET's own TPinballTable
// constructor spawns it for type 0x40b, like the other six Full-Tilt-only classes.
// Dragon's Keep has three, Skulduggery three.
//
// A ramp exit is a boundary between two collision layers, and nothing else. Its whole
// geometry is one line segment out of attribute 1301 -- the same attribute TRamp reads
// for its first switchable side wall, in the same layout: [0] and [1] are collision
// group exponents, [2] is the segment's point count and [3..6] its two endpoints. The
// original builds nothing at all unless [2] is exactly 2, so the class only ever
// handles a single segment.
//
// From that one segment it installs two one-sided TLines: the same endpoints wound in
// opposite directions, offset by plus and minus the table's CollisionCompOffset, each
// carrying one of the two collision groups. A ball can therefore only ever reach the
// edge that faces it, and crossing that edge hands it to the *other* group -- which is
// what leaving a ramp is in this engine. Both crossings also report to the rules, by
// the same two codes the original's TRamp sends from its two side walls, which is how
// Dragon's gargoyle and forest ramp exits pay their dragon eggs once per pass and how
// Skulduggery's ship wire ramp captures the ship.
//
// The vtable confirms it overrides Collision and nothing else: Message, port_draw,
// get_scoring and FieldEffect are all the inherited slots. No sprite, no sound, no
// field, no per-player state.
class TRampExit : public TCollisionComponent
{
public:
	TRampExit(TPinballTable* table, int groupIndex);
	void Collision(TBall* ball, vector2* nextPosition, vector2* direction, float distance,
	               TEdgeSegment* edge) override;

private:
	TEdgeSegment* InstallEdge(const vector2& start, const vector2& end, int collisionGroup, float offset);

	// Edge1 is wound start->end and offset to the positive side, Edge2 is the reverse of
	// both. Null when the component has no usable 1301, in which case it owns no edges
	// and Collision is unreachable.
	TEdgeSegment* Edge1{};
	TEdgeSegment* Edge2{};
	int CollisionGroup1{};
	int CollisionGroup2{};
};
