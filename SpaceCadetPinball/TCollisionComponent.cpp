#include "pch.h"
#include "TCollisionComponent.h"
#include "loader.h"
#include "maths.h"
#include "TEdgeSegment.h"
#include "TPinballTable.h"
#include "TBall.h"


TCollisionComponent::TCollisionComponent(TPinballTable* table, int groupIndex, bool createWall) :
	TPinballComponent(table, groupIndex, true)
{
	visualStruct visual{};

	ActiveFlag = 1;
	AABB = { -10000, -10000, 10000, 10000 };
	if (GroupName != nullptr)
		UnusedBaseFlag = 1;
	if (groupIndex <= 0)
	{
		loader::default_vsi(&visual);
	}
	else
	{
		loader::query_visual(groupIndex, 0, &visual);
		if (createWall)
		{
			// Full Tilt's bonus tables have blockers with no wall at all -- Dragon's
			// a_sword, a_jaw, a_center_post and the two rocks, Skulduggery's a_cannon
			// and a_bloce. They read as animated props rather than collidable bodies.
			// Space Cadet has none, so the original code dereferenced the result
			// unconditionally and crashed here on either bonus table.
			float offset = table->CollisionCompOffset;
			float* floatArr = loader::query_float_attribute_opt(groupIndex, 0, 600);
			if (floatArr)
				TEdgeSegment::install_wall(floatArr, this, &ActiveFlag, visual.CollisionGroup, offset, 0);
		}
	}

	Threshold = visual.Kicker.Threshold;
	Elasticity = visual.Elasticity;
	Smoothness = visual.Smoothness;
	Boost = visual.Kicker.Boost;
	HardHitSoundId = visual.Kicker.HardHitSoundId;
	SoftHitSoundId = visual.SoftHitSoundId;
	GroupIndex = groupIndex;
}

TCollisionComponent::~TCollisionComponent()
{
	for (auto edge : EdgeList)
		delete edge;
}


void TCollisionComponent::port_draw()
{
	for (auto edge : EdgeList)
		edge->port_draw();
}

bool TCollisionComponent::DefaultCollision(TBall* ball, vector2* nextPosition, vector2* direction)
{
	if (PinballTable->TiltLockFlag)
	{
		maths::basic_collision(ball, nextPosition, direction, Elasticity, Smoothness, 1000000000.0, 0.0);
		return false;
	}

	bool collision = false;
	auto projSpeed = maths::basic_collision(ball, nextPosition, direction, Elasticity, Smoothness, Threshold, Boost);
	if (projSpeed > Threshold)
	{
		loader::play_sound(HardHitSoundId, ball, "TCollisionComponent1");
		collision = true;
	}
	else if (projSpeed > 0.2f)
		loader::play_sound(SoftHitSoundId, ball, "TCollisionComponent2");

	return collision;
}

void TCollisionComponent::Collision(TBall* ball, vector2* nextPosition, vector2* direction,
                                    float distance, TEdgeSegment* edge)
{
	if (PinballTable->TiltLockFlag)
	{
		maths::basic_collision(ball, nextPosition, direction, Elasticity, Smoothness, 1000000000.0, 0.0);
		return;
	}

	auto projSpeed = maths::basic_collision(
		ball,
		nextPosition,
		direction,
		Elasticity,
		Smoothness,
		Threshold,
		Boost);
	if (projSpeed > Threshold)
		loader::play_sound(HardHitSoundId, ball, "TCollisionComponent3");
	else if (projSpeed > 0.2f)
		loader::play_sound(SoftHitSoundId, ball, "TCollisionComponent4");
}

int TCollisionComponent::FieldEffect(TBall* ball, vector2* vecDst)
{
	return 0;
}
