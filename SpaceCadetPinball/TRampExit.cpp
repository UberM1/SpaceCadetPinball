#include "pch.h"
#include "TRampExit.h"

#include "control.h"
#include "loader.h"
#include "TBall.h"
#include "TLine.h"
#include "TPinballTable.h"
#include "FtAccelDebug.h" // FTDBG

TRampExit::TRampExit(TPinballTable* table, int groupIndex) : TCollisionComponent(table, groupIndex, false)
{
	visualStruct visual{};
	// The original queries the visual and then uses none of it -- not even the collision
	// group, which comes from 1301 here. Kept because query_visual is also what reports a
	// malformed record, and dropping it would silence that for three components per table.
	loader::query_visual(groupIndex, 0, &visual);

	auto wall = loader::query_float_attribute_opt(groupIndex, 0, 1301);
	if (ft_accel_enabled())
	{
		if (!wall)
			fprintf(stderr, "DBG-rexit raw  %-10s 1301 ABSENT\n", GroupName ? GroupName : "?");
		else
			fprintf(stderr, "DBG-rexit raw  %-10s 1301 = %.3f %.3f %.3f %.3f %.3f %.3f %.3f  visualGroup=0x%x\n",
			        GroupName ? GroupName : "?", wall[0], wall[1], wall[2], wall[3], wall[4],
			        wall[5], wall[6], visual.CollisionGroup); // FTDBG
	}
	if (!wall || wall[2] != 2.0f)
	{
		ft_rexit_register(FtRexitDbgEntry{GroupName, this, 0, 0, {}, {}, false}); // FTDBG
		return;
	}

	CollisionGroup1 = 1 << static_cast<int>(wall[0]);
	CollisionGroup2 = 1 << static_cast<int>(wall[1]);

	vector2 start{wall[3], wall[4]};
	vector2 end{wall[5], wall[6]};

	// Two edges on one segment, each offset to its own side by the ball's collision
	// radius, so neither can be hit from behind the other.
	Edge1 = InstallEdge(start, end, CollisionGroup1, table->CollisionCompOffset);
	Edge2 = InstallEdge(end, start, CollisionGroup2, -table->CollisionCompOffset);

	// FTDBG
	ft_rexit_register(FtRexitDbgEntry{GroupName, this, CollisionGroup1, CollisionGroup2,
	                                  start, end, true});
}

TEdgeSegment* TRampExit::InstallEdge(const vector2& start, const vector2& end, int collisionGroup, float offset)
{
	auto line = new TLine(this, &ActiveFlag, collisionGroup, start, end);
	line->Offset(offset);
	line->place_in_grid(&AABB);
	EdgeList.push_back(line);
	return line;
}

void TRampExit::Collision(TBall* ball, vector2* nextPosition, vector2* direction, float distance,
                          TEdgeSegment* edge)
{
	// Pass-through only: no basic_collision, no sound, no reflection. The ball keeps its
	// direction and speed and simply changes which layer it belongs to.
	ball->not_again(edge);
	ball->RayMaxDistance -= distance;
	ball->Position.X = nextPosition->X;
	ball->Position.Y = nextPosition->Y;

	// Report first, then move the ball -- the original's order, and it matters because a
	// rules handler may inspect or redirect the ball. CollisionDisabledFlag ends this
	// ball's collision scan for the frame, so the edge it has just been handed to cannot
	// catch it on the way out; pb.cpp clears the flag at the top of the next frame.
	if (ft_accel_enabled())
		fprintf(stderr, "DBG-rexit HIT  %-10s comp=%p ball=%p pos=(%.3f,%.3f) maskIn=0x%x edge=%p e1=%p e2=%p -> maskOut=0x%x\n",
		        GroupName ? GroupName : "?", this, static_cast<void*>(ball), ball->Position.X, ball->Position.Y,
		        ball->CollisionMask, static_cast<void*>(edge), static_cast<void*>(Edge1),
		        static_cast<void*>(Edge2), edge == Edge1 ? CollisionGroup2 : CollisionGroup1); // FTDBG

	ft_rexit_note_hit(GroupName, ball->CollisionMask, edge == Edge1 ? CollisionGroup2 : CollisionGroup1); // FTDBG

	if (edge == Edge1)
	{
		control::handler(MessageCode::ControlRampWall2Crossed, this);
		ball->CollisionDisabledFlag = true;
		ball->CollisionMask = CollisionGroup2;
	}
	else
	{
		control::handler(MessageCode::ControlRampWall1Crossed, this);
		ball->CollisionDisabledFlag = true;
		ball->CollisionMask = CollisionGroup1;
	}
}
