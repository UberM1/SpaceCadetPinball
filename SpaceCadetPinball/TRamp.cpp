#include "pch.h"
#include "TRamp.h"


#include "control.h"
#include "loader.h"
#include "TBall.h"
#include "TEdgeSegment.h"
#include "TLine.h"
#include "TPinballTable.h"
#include "TTableLayer.h"
#include "FtAccelDebug.h" // FTDBG


TRamp::TRamp(TPinballTable* table, int groupIndex) : TCollisionComponent(table, groupIndex, false)
{
	visualStruct visual{};
	vector2 wall1End{}, wall1Start{}, wall2Start{}, wall2End{};

	MessageField = 0;
	UnusedBaseFlag = 1;
	loader::query_visual(groupIndex, 0, &visual);
	CollisionGroup = visual.CollisionGroup;

	BallFieldMult = loader::query_float_attribute(groupIndex, 0, 701, 0.2f);
	BallZOffsetFlag = static_cast<int>(loader::query_float_attribute(groupIndex, 0, 1305, 0));

	// Attribute 1300 is the ramp's plane list -- its entire geometry. Without it there
	// is nothing to build: no planes to walk, no bounding box, no field to insert.
	// Dragon's Keep and Skulduggery have no such ramp today, but the rest of this
	// constructor is meaningless without planes, so bail out rather than guess.
	auto floatArr3Plane = loader::query_float_attribute_opt(groupIndex, 0, 1300);
	if (!floatArr3Plane)
	{
		RampPlaneCount = 0;
		RampPlane = nullptr;
		Line1 = Line2 = Line3 = nullptr;
		Wall1CollisionGroup = Wall2CollisionGroup = 0;
		Wall1BallOffset = Wall2BallOffset = 0.0f;
		return;
	}
	RampPlaneCount = static_cast<int>(floor(*floatArr3Plane));
	RampPlane = reinterpret_cast<ramp_plane_type*>(floatArr3Plane + 1);

	// 1303 is the ramp's entry wall. Skulduggery's ramp exits (object type 1035) are
	// ramps with only the one ramp wall and no entry wall; Dragon's a_rmp5 is the same.
	// Absent means there is no entry edge -- install none.
	auto wall0Arr = loader::query_float_attribute_opt(groupIndex, 0, 1303);
	if (wall0Arr)
	{
		auto wall0CollisionGroup = 1 << static_cast<int>(floor(wall0Arr[0]));
		auto wall0Pts = reinterpret_cast<wall_point_type*>(wall0Arr + 2);
		Line1 = new TLine(this, &ActiveFlag, wall0CollisionGroup, wall0Pts->Pt1, wall0Pts->Pt0);
		Line1->WallValue = nullptr;
		Line1->place_in_grid(&AABB);
		EdgeList.push_back(Line1);
	}
	else
	{
		Line1 = nullptr;
	}

	// 1301/1302 are the two switchable side walls. When one is absent the ramp has no
	// such side: leave its collision group zero and its endpoints degenerate, so the
	// per-plane edge loop below never matches it and the plane edge keeps the ramp's
	// own collision group. Nothing is defaulted -- the wall simply does not exist.
	auto wall1Arr = loader::query_float_attribute_opt(groupIndex, 0, 1301);
	auto wall1Enabled = 0;
	Wall1CollisionGroup = 0;
	Wall1BallOffset = 0.0f;
	Line2 = nullptr;
	if (wall1Arr)
	{
		Wall1CollisionGroup = 1 << static_cast<int>(floor(wall1Arr[0]));
		wall1Enabled = static_cast<int>(floor(wall1Arr[1]));
		Wall1BallOffset = wall1Arr[7];
		maths::find_closest_edge(
			RampPlane,
			RampPlaneCount,
			reinterpret_cast<wall_point_type*>(wall1Arr + 3),
			wall1End,
			wall1Start);

		Line2 = new TLine(this, &ActiveFlag, CollisionGroup, wall1Start, wall1End);
		Line2->WallValue = nullptr;
		Line2->place_in_grid(&AABB);
		EdgeList.push_back(Line2);
	}

	auto wall2Arr = loader::query_float_attribute_opt(groupIndex, 0, 1302);
	auto wall2Enabled = 0;
	Wall2CollisionGroup = 0;
	Wall2BallOffset = 0.0f;
	Line3 = nullptr;
	if (wall2Arr)
	{
		Wall2CollisionGroup = 1 << static_cast<int>(floor(wall2Arr[0]));
		wall2Enabled = static_cast<int>(floor(wall2Arr[1]));
		Wall2BallOffset = wall2Arr[7];
		maths::find_closest_edge(
			RampPlane,
			RampPlaneCount,
			reinterpret_cast<wall_point_type*>(wall2Arr + 3),
			wall2End,
			wall2Start);

		Line3 = new TLine(this, &ActiveFlag, CollisionGroup, wall2Start, wall2End);
		Line3->WallValue = nullptr;
		Line3->place_in_grid(&AABB);
		EdgeList.push_back(Line3);
	}


	auto xMin = 1000000000.0f;
	auto yMin = 1000000000.0f;
	auto yMax = -1000000000.0f;
	auto xMax = -1000000000.0f;
	for (auto index = 0; index < RampPlaneCount; index++)
	{
		auto& plane = RampPlane[index];
		vector2* pointOrder[4] = { &plane.V1, &plane.V2, &plane.V3, &plane.V1 };

		xMin = std::min(std::min(std::min(plane.V3.X, plane.V1.X), plane.V2.X), xMin);
		yMin = std::min(std::min(std::min(plane.V3.Y, plane.V1.Y), plane.V2.Y), xMin); // Sic
		xMax = std::max(std::max(std::max(plane.V3.X, plane.V1.X), plane.V2.X), xMin);
		yMax = std::max(std::max(std::max(plane.V3.Y, plane.V1.Y), plane.V2.Y), xMin);
		
		for (auto pt = 0; pt < 3; pt++)
		{
			auto& point1 = *pointOrder[pt], point2 = *pointOrder[pt + 1];
			auto collisionGroup = 0;

			if (point1 == wall1End && point2 == wall1Start)
			{
				if (wall1Enabled)
					collisionGroup = Wall1CollisionGroup;
			}
			else if (point1 == wall2End && point2 == wall2Start)
			{
				if (wall2Enabled)
					collisionGroup = Wall2CollisionGroup;
			}
			else
				collisionGroup = visual.CollisionGroup;

			if (collisionGroup)
			{
				auto line = new TLine(this, &ActiveFlag, collisionGroup, point1, point2);
				line->WallValue = &plane;
				line->place_in_grid(&AABB);
				EdgeList.push_back(line);
			}
		}

		plane.FieldForce.X = cos(plane.GravityAngle2) * sin(plane.GravityAngle1) *
			PinballTable->GravityDirVectMult;
		plane.FieldForce.Y = sin(plane.GravityAngle2) * sin(plane.GravityAngle1) *
			PinballTable->GravityDirVectMult;
	}

	Field.ActiveFlag = &ActiveFlag;
	Field.CollisionComp = this;
	Field.CollisionGroup = visual.CollisionGroup;

	auto x1 = xMax;
	auto y1 = yMax;
	auto x0 = xMin;
	auto y0 = yMin;
	if (ft_accel_enabled())
		fprintf(stderr, "DBG-ramp  %-12s rampGroup=0x%x w1Group=0x%x(en=%d) w2Group=0x%x(en=%d) planes=%d\n",
		        GroupName ? GroupName : "?", CollisionGroup, Wall1CollisionGroup, wall1Enabled,
		        Wall2CollisionGroup, wall2Enabled, RampPlaneCount); // FTDBG
	TTableLayer::edges_insert_square(y0, x0, y1, x1, nullptr, &Field);
}

void TRamp::Collision(TBall* ball, vector2* nextPosition, vector2* direction, float distance, TEdgeSegment* edge)
{
	ball->not_again(edge);
	ball->Position.X = nextPosition->X;
	ball->Position.Y = nextPosition->Y;
	ball->RayMaxDistance -= distance;

	auto plane = static_cast<ramp_plane_type*>(edge->WallValue);
	if (plane)
	{
		ball->CollisionFlag = 1;
		ball->CollisionOffset.X = plane->BallCollisionOffset.X;
		ball->CollisionOffset.Y = plane->BallCollisionOffset.Y;
		ball->CollisionOffset.Z = plane->BallCollisionOffset.Z;
		ball->RampFieldForce.X = plane->FieldForce.X;
		ball->RampFieldForce.Y = plane->FieldForce.Y;
		ball->Position.Z = ball->Position.X * ball->CollisionOffset.X + ball->Position.Y * ball->CollisionOffset.Y +
			ball->Radius + ball->CollisionOffset.Z;

		// The original guards the rest on the ball not already being on this ramp's
		// layer (0x41790a `cmp [ball+0xf7],eax` / `je` to the epilogue), so the plane
		// geometry is reapplied every frame the ball rides the ramp but the layer
		// change is announced once, on entry. Upstream wrote the mask unconditionally
		// and announced nothing; writing it inside the guard is the same value, and
		// 0x54 has consumers -- Dragon's RampEntered arm and Skulduggery's a_ramp8.
		if (ball->CollisionMask != CollisionGroup)
		{
			// Ends this ball's edge scanning for the frame, which is what every other
			// component that moves a ball between layers does (THole, TRampExit,
			// TKickout, TSink, TAccelerator). Without it the ball can still hit an edge
			// of the layer it just left, in the same frame. 0x417917 in the original.
			ball->CollisionDisabledFlag = true;
			ball->CollisionMask = CollisionGroup;
			control::handler(MessageCode::ControlRampPlaneCrossed, this);
		}
		return;
	}

	if (edge == Line1)
	{
		if (!PinballTable->TiltLockFlag)
		{
			loader::play_sound(SoftHitSoundId, ball, "TRamp");
			control::handler(MessageCode::ControlCollision, this);
		}
	}
	else
	{
		ball->CollisionFlag = 0;

		// The originals notify the rules on both side walls, with distinct codes;
		// upstream kept only the Line1 ControlCollision above. Space Cadet never
		// binds a control function to a ramp, so the loss is invisible there --
		// but Dragon's Keep's a_ramp103 and a_ramp6 both test 0x56, and without
		// this they never fire at all. Codes read from TRamp::Collision at
		// DRAGON.EXE 0x4177d0; the plane branch's 0x54 is sent above.
		// Both wall branches disable the ball's collisions for the rest of the frame
		// in the original, at 0x417979 and 0x4179b8 -- the same `[ball+0x9b] = 1` that
		// TBall::Disable writes at 0x402c09, which is what pins the offset.
		if (edge == Line2)
		{
			ball->CollisionDisabledFlag = true;
			ball->CollisionMask = Wall1CollisionGroup;
			if (BallZOffsetFlag)
				ball->Position.Z = ball->Radius + Wall1BallOffset;
			control::handler(MessageCode::ControlRampWall1Crossed, this);
		}
		else
		{
			ball->CollisionDisabledFlag = true;
			ball->CollisionMask = Wall2CollisionGroup;
			if (BallZOffsetFlag)
				ball->Position.Z = ball->Radius + Wall2BallOffset;
			control::handler(MessageCode::ControlRampWall2Crossed, this);
		}
	}
}

int TRamp::FieldEffect(TBall* ball, vector2* vecDst)
{
	vecDst->X = ball->RampFieldForce.X - ball->Direction.X * ball->Speed * BallFieldMult;
	vecDst->Y = ball->RampFieldForce.Y - ball->Direction.Y * ball->Speed * BallFieldMult;
	return 1;
}

void TRamp::port_draw()
{
	TCollisionComponent::port_draw();
}
