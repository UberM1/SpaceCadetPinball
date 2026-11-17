// ---- FTDBG: temporary accelerator/ramp-exit harness (remove before finishing) ----
#include "pch.h"
#include "FtAccelDebug.h"
#include "pb.h"
#include "TBall.h"
#include "TPinballTable.h"
#include "TPinballComponent.h"
#include "TTableLayer.h"
#include "TEdgeManager.h"
#include "TEdgeBox.h"
#include "TBlocker.h"
#include "TKickback.h"
#include "TLine.h"
#include "TCircle.h"
#include "TEdgeSegment.h"
#include <vector>

static TBall* active_ball(TPinballTable* t);
struct FtHoleDbgEntry { const char* Name; vector2 Center; float RadiusSq; int Group; int ReleaseMask; float Z; };
static std::vector<FtHoleDbgEntry> gHoles;
struct FtSinkDbgEntry { const char* Name; void* Comp; RectF AABB; int Group; float TimerTime; vector2 BallPos; };
static std::vector<FtSinkDbgEntry> gSinks;
struct FtKoutDbgEntry
{
	const char* Name; void* Comp; vector2 Center; float Radius; int Group;
	float TimerTime1, TimerTime2, SetZ, FieldMult;
	int NotSomeFlag, ActiveFlag, ZAttrPresent; float RadiusScale; int RadiusScalePresent;
};
static std::vector<FtKoutDbgEntry> gKouts;
static std::vector<FtAccelDbgEntry> gAccels;
static std::vector<FtRexitDbgEntry> gRexits;

bool ft_accel_enabled()
{
	static int enabled = -1;
	if (enabled < 0)
		enabled = getenv("FT_ACCELTEST") ? 1 : 0;
	return enabled != 0;
}

bool ft_autoplay_enabled()
{
	static int enabled = -1;
	if (enabled < 0)
		enabled = getenv("FT_AUTOPLAY") ? 1 : 0;
	return enabled != 0;
}

bool ft_autodrain_enabled()
{
	static int enabled = -1;
	if (enabled < 0)
		enabled = getenv("FT_AUTODRAIN") ? 1 : 0;
	return enabled != 0;
}

bool ft_blockdbg_enabled()
{
	static int enabled = -1;
	if (enabled < 0)
		enabled = getenv("FT_BLOCKDBG") ? 1 : 0;
	return enabled != 0;
}

struct HitCount
{
	const char* Name;
	int Count;
	float Last;
	float WindowStart;
	int WindowCount;
};
static std::vector<HitCount> gAccelHits, gRexitHits;
static float gNow = 0.0f;

void ft_debug_clear_registries()
{
	// The registries are filled by component constructors and hold their GroupName
	// and component pointers, which die with the table. Called at the top of
	// TPinballTable's constructor, so each table load starts from empty.
	gHoles.clear();
	gSinks.clear();
	gKouts.clear();
	gAccels.clear();
	gRexits.clear();
	gAccelHits.clear();
	gRexitHits.clear();
}

static void note(std::vector<HitCount>& v, const char* kind, const char* name, const char* extra)
{
	HitCount* h = nullptr;
	for (auto& e : v)
		if (e.Name == name)
			h = &e;
	if (!h)
	{
		v.push_back(HitCount{name, 0, gNow, gNow, 0});
		h = &v.back();
	}
	h->Count++;
	if (gNow - h->WindowStart > 2.0f)
	{
		h->WindowStart = gNow;
		h->WindowCount = 0;
	}
	h->WindowCount++;
	h->Last = gNow;
	fprintf(stderr, "DBG-hit   t=%7.2f %s %-12s total=%-4d in2s=%-3d %s\n",
	        gNow, kind, name ? name : "?", h->Count, h->WindowCount, extra);
	if (h->WindowCount == 15)
		fprintf(stderr, "DBG-LOOP  t=%7.2f !!! %s %s hit 15 times in 2s -- sound would be looping\n",
		        gNow, kind, name ? name : "?");
}

void ft_accel_note_hit(const char* name)
{
	if (ft_autoplay_enabled())
		note(gAccelHits, "accel", name, "");
}

void ft_rexit_note_hit(const char* name, int maskIn, int maskOut)
{
	if (!ft_autoplay_enabled())
		return;
	char buf[64];
	snprintf(buf, sizeof buf, "mask 0x%x -> 0x%x", maskIn, maskOut);
	note(gRexitHits, "rexit", name, buf);
}

static void autoplay(float dtSec)
{
	auto table = pb::MainTable;
	if (!table)
		return;
	gNow += dtSec;

	static int stage = 0;
	static float lastFlip = 0.0f;
	static bool flipDown = false;
	static float noBallSince = -1.0f;
	static int maskRun = 0;
	static int lastMask = 1;
	static float maskRunStart = 0.0f;
	static float lastReport = 0.0f;

	if (stage == 0 && gNow > 1.0f)
	{
		fprintf(stderr, "DBG-auto2 new game; %zu accelerators, %zu ramp exits\n", gAccels.size(), gRexits.size());
		pb::replay_level(false);
		stage = 1;
		return;
	}
	if (stage == 1 && gNow > 2.0f)
	{
		pb::launch_ball();
		stage = 2;
		return;
	}
	if (stage < 2)
		return;

	// FT_HOLETEST=<n>: drive the ball into hole n and then trace the layer chain.
	static int holeTest = -2;
	static bool holeDriven = false;
	if (holeTest == -2)
	{
		auto e = getenv("FT_HOLETEST");
		holeTest = e ? atoi(e) : -1;
	}
	if (holeTest >= 0)
	{
		auto bb = active_ball(table);
		if (bb && !holeDriven && gNow > 3.0f && static_cast<size_t>(holeTest) < gHoles.size())
		{
			auto& h = gHoles[holeTest];
			fprintf(stderr, "DBG-chain drive into hole[%d] %s center=(%.2f,%.2f) group=0x%x release=0x%x\n",
			        holeTest, h.Name ? h.Name : "?", h.Center.X, h.Center.Y, h.Group, h.ReleaseMask);
			bb->Position.X = h.Center.X;
			bb->Position.Y = h.Center.Y;
			bb->Position.Z = getenv("FT_LAYERTEST") ? h.Z : bb->Position.Z;
			bb->PrevPosition = bb->Position;
			bb->Direction = {0.0f, 0.3f, 0.0f};
			bb->Speed = 2.0f;
			bb->CollisionMask = getenv("FT_LAYERTEST") ? h.ReleaseMask : h.Group;
			bb->CollisionComp = nullptr;
			bb->StuckCounter = 0;
			holeDriven = true;
		}
		if (bb)
		{
			static int n = 0;
			if ((n++ % 8) == 0)
				fprintf(stderr, "DBG-chain t=%7.2f pos=(%.3f,%.3f,%.3f) spd=%.3f mask=0x%x comp=%p stuck=%d\n",
				        gNow, bb->Position.X, bb->Position.Y, bb->Position.Z, bb->Speed,
				        bb->CollisionMask, static_cast<void*>(bb->CollisionComp), bb->StuckCounter);
		}
	}

	// FT_SINKTEST=<n>: drop the ball into sink n and watch for the eject.
	static int sinkTest = -2;
	static bool sinkDriven = false;
	if (sinkTest == -2)
	{
		auto e = getenv("FT_SINKTEST");
		sinkTest = e ? atoi(e) : -1;
	}
	if (sinkTest >= 0)
	{
		auto bb = active_ball(table);
		static float lastDrive = 0.0f;
		if (getenv("FT_SINKREPEAT") && gNow - lastDrive > 6.0f)
			sinkDriven = false;
		if (bb && !sinkDriven && gNow > 3.0f && static_cast<size_t>(sinkTest) < gSinks.size())
		{
			auto& k = gSinks[sinkTest];
			fprintf(stderr, "DBG-chain drive into sink[%d] %s\n", sinkTest, k.Name ? k.Name : "?");
			bb->Position.X = (k.AABB.XMin + k.AABB.XMax) / 2.0f;
			bb->Position.Y = (k.AABB.YMin + k.AABB.YMax) / 2.0f + 0.6f;
			bb->PrevPosition = bb->Position;
			bb->Direction = {0.0f, -1.0f, 0.0f};
			bb->Speed = 8.0f;
			bb->CollisionMask = k.Group;
			bb->CollisionComp = nullptr;
			bb->StuckCounter = 0;
			sinkDriven = true;
			lastDrive = gNow;
		}
		static int m = 0;
		if ((m++ % 16) == 0)
		{
			int n = 0;
			for (auto bl : table->BallList)
				if (bl->ActiveFlag)
				{
					fprintf(stderr, "DBG-chain t=%7.2f ball%d pos=(%.3f,%.3f) spd=%.3f mask=0x%x\n",
					        gNow, n, bl->Position.X, bl->Position.Y, bl->Speed, bl->CollisionMask);
					n++;
				}
			if (n == 0)
				fprintf(stderr, "DBG-chain t=%7.2f NO ACTIVE BALL (balls=%zu)\n", gNow, table->BallList.size());
		}
		return;
	}

	// FT_KICKTEST=<n>: drive the ball into TKickback n (Dragon has one, a_kick1, the left
	// outlane kicker), then take the ball away again FT_KICKGONE ms later -- which is what
	// the left drain channel does: the ball brushes the kicker on its way past and then
	// drains, so it never comes back to collide a second time. Prints the kickback's own
	// state every frame so the 0.1 s re-arm is visible next to the play_sound rate.
	static int kickTest = -2;
	static bool kickDriven = false;
	if (kickTest == -2)
	{
		auto e = getenv("FT_KICKTEST");
		kickTest = e ? atoi(e) : -1;
	}
	if (kickTest >= 0)
	{
		std::vector<TKickback*> kicks;
		for (auto c : table->ComponentList)
			if (auto k = dynamic_cast<TKickback*>(c))
				kicks.push_back(k);
		if (static_cast<size_t>(kickTest) >= kicks.size())
		{
			static bool said = false;
			if (!said)
			{
				said = true;
				fprintf(stderr, "DBG-kick  no TKickback[%d]; table has %zu\n", kickTest, kicks.size());
			}
			return;
		}
		auto kick = kicks[kickTest];
		auto bb = active_ball(table);
		static float driveTime = 0.0f;
		static bool removed = false;
		// Aim at the kickback's own wall segment rather than at its AABB: a_kick1's AABB
		// is a degenerate line (y 12.344..12.344) and the ball has to arrive from the side
		// the segment's normal faces, on the segment's own collision group.
		// FT_KICKSIDE=-1 approaches from the other side.
		if (bb && !kickDriven && gNow > 3.0f)
		{
			TLine* seg = nullptr;
			for (auto e : kick->EdgeList)
				if (auto ln = dynamic_cast<TLine*>(e))
				{
					seg = ln;
					break;
				}
			if (!seg)
			{
				fprintf(stderr, "DBG-kick  kickback[%d] %s has no TLine edge (%zu edges)\n",
				        kickTest, kick->GroupName ? kick->GroupName : "?", kick->EdgeList.size());
				return;
			}
			float side = getenv("FT_KICKSIDE") ? static_cast<float>(atoi(getenv("FT_KICKSIDE"))) : 1.0f;
			float mx = (seg->X0 + seg->X1) / 2.0f, my = (seg->Y0 + seg->Y1) / 2.0f;
			float dx = seg->X1 - seg->X0, dy = seg->Y1 - seg->Y0;
			float l = sqrtf(dx * dx + dy * dy);
			float nx = -dy / l * side, ny = dx / l * side;
			fprintf(stderr, "DBG-kick  drive into kickback[%d] %s seg=(%.3f,%.3f)->(%.3f,%.3f)"
			        " group=0x%x n=(%.3f,%.3f) hardHit=%d softHit=%d t1=%.3f t2=%.3f active=%d\n",
			        kickTest, kick->GroupName ? kick->GroupName : "?", seg->X0, seg->Y0, seg->X1,
			        seg->Y1, seg->CollisionGroup, nx, ny, kick->HardHitSoundId, kick->SoftHitSoundId,
			        kick->TimerTime, kick->TimerTime2, kick->ActiveFlag);
			kick->ActiveFlag = 1;
			bb->Position.X = mx + nx * 0.5f;
			bb->Position.Y = my + ny * 0.5f;
			bb->Position.Z = 0.0f;
			bb->PrevPosition = bb->Position;
			bb->Direction = {-nx, -ny, 0.0f};
			bb->Speed = 6.0f;
			bb->CollisionMask = seg->CollisionGroup;
			bb->CollisionComp = nullptr;
			bb->StuckCounter = 0;
			kickDriven = true;
			driveTime = gNow;
		}
		// Take the ball off the playfield the way TDrain::Collision does.
		float goneMs = getenv("FT_KICKGONE") ? static_cast<float>(atoi(getenv("FT_KICKGONE"))) : 300.0f;
		if (kickDriven && !removed && gNow - driveTime > goneMs / 1000.0f)
		{
			removed = true;
			if (bb)
			{
				fprintf(stderr, "DBG-kick  t=%7.2f ball removed (mimics TDrain::Collision)"
				        " kickActive=%d timer=%d threshold=%.1f\n",
				        gNow, kick->KickActiveFlag, kick->Timer, kick->Threshold);
				bb->Disable();
			}
		}
		static int kn = 0;
		if ((kn++ % 8) == 0)
			fprintf(stderr, "DBG-kick  t=%7.2f kickActive=%d timer=%d threshold=%.1f ball=%s\n",
			        gNow, kick->KickActiveFlag, kick->Timer, kick->Threshold,
			        active_ball(table) ? "live" : "gone");
		return;
	}

	// FT_KOUTTEST=<n>: drive the ball into kickout n and watch for the kick.
	// FT_KOUTREPEAT=1 re-drives every 8 s, which is how a kickout that captures once
	// and then never lets go is told from one that is simply slow.
	static int koutTest = -2;
	static bool koutDriven = false;
	if (koutTest == -2)
	{
		auto e = getenv("FT_KOUTTEST");
		koutTest = e ? atoi(e) : -1;
	}
	if (koutTest >= 0)
	{
		auto bb = active_ball(table);
		static float lastDrive = 0.0f;
		if (getenv("FT_KOUTREPEAT") && gNow - lastDrive > 8.0f)
			koutDriven = false;
		if (bb && !koutDriven && gNow > 3.0f && static_cast<size_t>(koutTest) < gKouts.size())
		{
			auto& k = gKouts[koutTest];
			// FT_KOUTARM=1 forces the kickout live first: Dragon's a_kout3 is a type-1029
			// kickout that starts with ActiveFlag 0 and is armed only by a spell.
			if (getenv("FT_KOUTARM"))
				static_cast<TPinballComponent*>(k.Comp)->ActiveFlag = 1;
			fprintf(stderr, "DBG-kout  drive into kout[%d] %s center=(%.3f,%.3f) r=%.3f group=0x%x"
			        " active=%d\n",
			        koutTest, k.Name ? k.Name : "?", k.Center.X, k.Center.Y, k.Radius, k.Group,
			        static_cast<TPinballComponent*>(k.Comp)->ActiveFlag);
			// FT_KOUTCENTER=1 drops the ball straight onto the mouth instead of rolling it
			// in from outside: a_kout3's capture circle is 0.039 across (306 = 0.01 x a
			// 3.938 field radius), too small to hit by aiming from 4 units away.
			bool atCentre = getenv("FT_KOUTCENTER") != nullptr;
			bb->Position.X = k.Center.X;
			bb->Position.Y = atCentre ? k.Center.Y : k.Center.Y + k.Radius + 0.4f;
			bb->PrevPosition = bb->Position;
			bb->Direction = {0.0f, -1.0f, 0.0f};
			bb->Speed = atCentre ? 0.2f : 6.0f;
			bb->CollisionMask = k.Group;
			bb->CollisionComp = nullptr;
			bb->StuckCounter = 0;
			koutDriven = true;
			lastDrive = gNow;
		}
		// FT_KOUTBLOCK=1: dump every moving blocker's position, and with FT_KOUTCLEAR=1
		// retract them all (TBlockerDisable) before the ball is driven in. The pair is the
		// control experiment for "the kicked ball is bouncing off a rock".
		static bool blockDumped = false;
		if ((getenv("FT_KOUTBLOCK") || getenv("FT_KOUTCLEAR") || getenv("FT_KOUTBLOCKPOS")) &&
			!blockDumped && gNow > 2.5f)
		{
			blockDumped = true;
			for (auto c : table->ComponentList)
			{
				auto bl = dynamic_cast<TBlocker*>(c);
				if (!bl)
					continue;
				fprintf(stderr, "DBG-koutblk %-12s movingPart=%d cur=%d target=%d home=%d flags=%d%d%d%d%d%d%d%d\n",
				        bl->GroupName ? bl->GroupName : "?", bl->MovingPart, bl->CurrentPosition,
				        bl->TargetPosition, bl->HomePosition, bl->PositionFlags[0], bl->PositionFlags[1],
				        bl->PositionFlags[2], bl->PositionFlags[3], bl->PositionFlags[4],
				        bl->PositionFlags[5], bl->PositionFlags[6], bl->PositionFlags[7]);
				// FT_KOUTCLEAR=1 retracts every blocker; FT_KOUTBLOCKPOS=<n> parks every
				// blocker at position n instead, which is how "which position leaves the
				// kickout channel clear" gets answered without guessing.
				if (auto pos = getenv("FT_KOUTBLOCKPOS"))
					bl->Message(MessageCode::TBlockerSetPosition, static_cast<float>(atoi(pos)));
				else if (getenv("FT_KOUTCLEAR"))
					bl->Message(MessageCode::TBlockerSnapClosed, 0.0f);
			}
		}

		// FT_KOUTEDGES=1: one-shot dump of every collision edge in the grid boxes around
		// the kickout, which is how "the ball is thrown into a wall" is told from "the
		// kickout never fires".
		static bool edgesDumped = false;
		if (getenv("FT_KOUTEDGES") && !edgesDumped && koutDriven &&
			static_cast<size_t>(koutTest) < gKouts.size())
		{
			edgesDumped = true;
			auto& k = gKouts[koutTest];
			auto em = TTableLayer::edge_manager;
			int cx = em->box_x(k.Center.X), cy = em->box_y(k.Center.Y);
			for (int dy = -1; dy <= 1; dy++)
				for (int dx = -1; dx <= 1; dx++)
				{
					int bx = cx + dx, by = cy + dy;
					if (bx < 0 || by < 0 || bx >= em->MaxBoxX || by >= em->MaxBoxY)
						continue;
					auto& box = em->BoxArray[bx + by * em->MaxBoxX];
					for (auto e : box.EdgeList)
					{
						auto cc = e->CollisionComponent;
						// Only the edges that can actually stop the ball, and only the ones
						// within a ball's reach of the kickout's mouth: the raw box dump is
						// hundreds of lines of scenery.
						if (!(e->CollisionGroup & 1) || !e->ActiveFlagPtr || !*e->ActiveFlagPtr)
							continue;
						char geo[96] = "?";
						auto ln = dynamic_cast<TLine*>(e);
						auto ci = dynamic_cast<TCircle*>(e);
						float d = 1e9f;
						if (ln)
						{
							snprintf(geo, sizeof geo, "line (%.3f,%.3f)->(%.3f,%.3f)", ln->X0, ln->Y0,
							         ln->X1, ln->Y1);
							float mx = (ln->X0 + ln->X1) / 2.0f - k.Center.X;
							float my = (ln->Y0 + ln->Y1) / 2.0f - k.Center.Y;
							d = sqrtf(mx * mx + my * my);
						}
						else if (ci)
						{
							snprintf(geo, sizeof geo, "circle (%.3f,%.3f) rSq=%.4f", ci->Circle.Center.X,
							         ci->Circle.Center.Y, ci->Circle.RadiusSq);
							float mx = ci->Circle.Center.X - k.Center.X;
							float my = ci->Circle.Center.Y - k.Center.Y;
							d = sqrtf(mx * mx + my * my);
						}
						if (d > 1.2f)
							continue;
						fprintf(stderr, "DBG-koutedge d=%.3f group=0x%x comp=%-12s type=%-5d %s\n",
						        d, e->CollisionGroup, cc && cc->GroupName ? cc->GroupName : "?",
						        cc ? ComponentTypeOf(cc) : -1, geo);
					}
				}
		}

		// Keep flapping the flippers during a kickout test: Dragon's a_kout3 is released by
		// a flipper press (part2's DragonBloodLightControl / a_flip2 handler), not by a
		// timer, so a test that never presses one cannot tell "held on purpose" from
		// "stuck".
		if (gNow - lastFlip > 0.2f)
		{
			lastFlip = gNow;
			flipDown = !flipDown;
			table->Message(flipDown ? MessageCode::LeftFlipperInputPressed : MessageCode::LeftFlipperInputReleased,
			               pb::time_now);
			table->Message(flipDown ? MessageCode::RightFlipperInputPressed : MessageCode::RightFlipperInputReleased,
			               pb::time_now);
		}

		static int m = 0;
		static int koutEvery = getenv("FT_KOUTTRACE") ? 1 : 16;
		if ((m++ % koutEvery) == 0)
		{
			int n = 0;
			for (auto bl : table->BallList)
				if (bl->ActiveFlag)
				{
					fprintf(stderr, "DBG-kout  t=%7.2f ball%d pos=(%.3f,%.3f,%.3f) spd=%.3f mask=0x%x"
					        " comp=%p\n",
					        gNow, n, bl->Position.X, bl->Position.Y, bl->Position.Z, bl->Speed,
					        bl->CollisionMask, static_cast<void*>(bl->CollisionComp));
					n++;
				}
			if (n == 0)
				fprintf(stderr, "DBG-kout  t=%7.2f NO ACTIVE BALL (balls=%zu)\n", gNow, table->BallList.size());
		}
		return;
	}

	// Keep a ball in play.
	auto b = active_ball(table);
	if (!b)
	{
		if (noBallSince < 0.0f)
			noBallSince = gNow;
		else if (gNow - noBallSince > 1.0f)
		{
			noBallSince = -1.0f;
			// FT_AUTOPLAY_KEEP: keep the game (and its players) going; start a new
			// one only after game over. Without it every gap of a second restarts.
			if (!getenv("FT_AUTOPLAY_KEEP") || pb::game_mode == GameModes::GameOver)
				pb::replay_level(false);
			pb::launch_ball();
			fprintf(stderr, "DBG-auto2 t=%7.2f relaunch player=%d ball=%d\n", gNow,
			        table->CurrentPlayer, table->BallCount);
		}
		return;
	}
	noBallSince = -1.0f;

	// A ball sitting still (the plunger lane, after a launch the light show swallowed):
	// pull the plunger again every 3 s.
	// "Still" is by position: a ball resting on the plunger keeps a small speed.
	static float stillSince = -1.0f;
	static vector2 stillPos{};
	const float dx = b->Position.X - stillPos.X, dy = b->Position.Y - stillPos.Y;
	if (stillSince >= 0.0f && dx * dx + dy * dy < 0.01f)
	{
		if (gNow - stillSince > 3.0f)
		{
			stillSince = -1.0f;
			pb::launch_ball();
			fprintf(stderr, "DBG-auto2 t=%7.2f launch (ball still at %.2f,%.2f) player=%d ball=%d\n",
			        gNow, b->Position.X, b->Position.Y, table->CurrentPlayer, table->BallCount);
		}
	}
	else
	{
		stillSince = gNow;
		stillPos = {b->Position.X, b->Position.Y};
	}

	// Flap both flippers.
	if (gNow - lastFlip > 0.2f)
	{
		lastFlip = gNow;
		flipDown = !flipDown;
		table->Message(flipDown ? MessageCode::LeftFlipperInputPressed : MessageCode::LeftFlipperInputReleased,
		               pb::time_now);
		table->Message(flipDown ? MessageCode::RightFlipperInputPressed : MessageCode::RightFlipperInputReleased,
		               pb::time_now);
	}

	// Watch for a ball parked off the main collision layer.
	if (b->CollisionMask != lastMask)
	{
		if (maskRun > 0 && lastMask != 1)
			fprintf(stderr, "DBG-layer t=%7.2f left mask 0x%x after %.2fs (%d frames)\n",
			        gNow, lastMask, gNow - maskRunStart, maskRun);
		lastMask = b->CollisionMask;
		maskRun = 0;
		maskRunStart = gNow;
	}
	maskRun++;
	if (lastMask != 1 && gNow - maskRunStart > 5.0f && gNow - lastReport > 5.0f)
	{
		lastReport = gNow;
		fprintf(stderr, "DBG-STUCK t=%7.2f !!! ball on mask 0x%x for %.2fs pos=(%.3f,%.3f,%.3f) spd=%.3f comp=%p stuck=%d\n",
		        gNow, lastMask, gNow - maskRunStart, b->Position.X, b->Position.Y, b->Position.Z,
		        b->Speed, static_cast<void*>(b->CollisionComp), b->StuckCounter);
	}
}

void ft_sink_register(const char* name, void* comp, RectF aabb, int group, float timerTime,
                      vector2 ballPos, int s3, int s4)
{
	gSinks.push_back(FtSinkDbgEntry{name, comp, aabb, group, timerTime, ballPos});
	if (ft_accel_enabled() || ft_autoplay_enabled())
		fprintf(stderr, "DBG-sink  reg %-12s comp=%p aabb[x %.2f..%.2f y %.2f..%.2f] group=0x%x"
		        " timerTime=%.4f ballPos=(%.3f,%.3f) s3=%d s4=%d\n",
		        name ? name : "?", comp, aabb.XMin, aabb.XMax, aabb.YMin, aabb.YMax, group,
		        timerTime, ballPos.X, ballPos.Y, s3, s4);
}

void ft_kickout_register(const char* name, void* comp, vector2 center, float radius, int group,
                         float timerTime1, float timerTime2, float setZ, float fieldMult,
                         int notSomeFlag, int activeFlag, int zAttrPresent, float radiusScale,
                         int radiusScalePresent)
{
	gKouts.push_back(FtKoutDbgEntry{name, comp, center, radius, group, timerTime1, timerTime2, setZ,
	                                fieldMult, notSomeFlag, activeFlag, zAttrPresent, radiusScale,
	                                radiusScalePresent});
	if (ft_accel_enabled() || ft_autoplay_enabled())
		fprintf(stderr, "DBG-kout  reg [%2zu] %-12s comp=%p center=(%.3f,%.3f) r=%.3f group=0x%x"
		        " t1=%.3f t2=%.3f setZ=%.3f (408 %s) field=%.3f notSome=%d active=%d"
		        " r306=%.4f (306 %s)\n",
		        gKouts.size() - 1, name ? name : "?", comp, center.X, center.Y, radius, group,
		        timerTime1, timerTime2, setZ, zAttrPresent ? "present" : "ABSENT", fieldMult,
		        notSomeFlag, activeFlag, radiusScale, radiusScalePresent ? "present" : "ABSENT");
}

void ft_hole_register(const char* name, vector2 center, float radiusSq, int group, int releaseMask, float z)
{
	gHoles.push_back(FtHoleDbgEntry{name, center, radiusSq, group, releaseMask, z});
	if (ft_accel_enabled() || ft_autoplay_enabled())
		fprintf(stderr, "DBG-hole  reg %-12s center=(%.3f,%.3f) r=%.3f group=0x%x release=0x%x z=%.3f\n",
		        name ? name : "?", center.X, center.Y, sqrtf(radiusSq), group, releaseMask, z);
}

void ft_accel_register(const FtAccelDbgEntry& e)
{
	gAccels.push_back(e);
	if (!ft_accel_enabled())
		return;
	fprintf(stderr,
	        "DBG-accel ctor %-10s comp=%p aabb[xmin=%.1f xmax=%.1f ymin=%.1f ymax=%.1f]"
	        " center=(%.2f,%.2f) r=%.3f group=0x%x dir=(%.3f,%.3f,%.3f)\n",
	        e.Name ? e.Name : "?", e.Comp, e.AABB.XMin, e.AABB.XMax, e.AABB.YMin, e.AABB.YMax,
	        e.Center.X, e.Center.Y, sqrtf(e.RadiusSq), e.Group, e.Dir.X, e.Dir.Y, e.Dir.Z);
}

void ft_rexit_register(const FtRexitDbgEntry& e)
{
	gRexits.push_back(e);
	if (!ft_accel_enabled())
		return;
	fprintf(stderr, "DBG-rexit ctor %-10s comp=%p built=%d g1=0x%x g2=0x%x seg=(%.2f,%.2f)->(%.2f,%.2f)\n",
	        e.Name ? e.Name : "?", e.Comp, e.Built, e.Group1, e.Group2,
	        e.Start.X, e.Start.Y, e.End.X, e.End.Y);
}

static TBall* active_ball(TPinballTable* t)
{
	for (auto b : t->BallList)
		if (b->ActiveFlag)
			return b;
	return nullptr;
}

static void trace(const char* tag, TPinballTable* t)
{
	auto b = active_ball(t);
	if (!b)
	{
		fprintf(stderr, "DBG-trace %-6s no active ball\n", tag);
		return;
	}
	auto em = TTableLayer::edge_manager;
	int bx = em->box_x(b->Position.X), by = em->box_y(b->Position.Y);
	fprintf(stderr,
	        "DBG-trace %-6s pos=(%.3f,%.3f,%.3f) dir=(%.3f,%.3f) spd=%.3f mask=0x%x comp=%p"
	        " box=(%d,%d) disabled=%d stuck=%d rad=%.3f\n",
	        tag, b->Position.X, b->Position.Y, b->Position.Z, b->Direction.X, b->Direction.Y,
	        b->Speed, b->CollisionMask, static_cast<void*>(b->CollisionComp), bx, by,
	        b->CollisionDisabledFlag, b->StuckCounter, b->Radius);
}

void ft_accel_tick(float dtSec)
{
	if (ft_autoplay_enabled())
	{
		autoplay(dtSec);
		return;
	}
	if (!ft_accel_enabled())
		return;
	auto table = pb::MainTable;
	if (!table)
		return;

	static float t = 0.0f;
	static int stage = 0;
	static int traceFrames = 0;
	static size_t target = 0;
	static int mode = 0; // 0 = accelerators, 1 = ramp exits
	t += dtSec;

	// How many ticks of trace after each injection.
	const int TRACE_LEN = 90;

	if (stage == 0 && t > 1.0f)
	{
		fprintf(stderr, "DBG-accel new game; %zu accelerators, %zu ramp exits\n", gAccels.size(), gRexits.size());
		pb::replay_level(false);
		stage = 1;
		return;
	}
	if (stage == 1 && t > 2.0f)
	{
		pb::launch_ball();
		stage = 2;
		return;
	}
	if (stage < 2)
		return;

	if (traceFrames > 0)
	{
		traceFrames--;
		trace("run", table);
		return;
	}

	// Inject the next test case.
	if (mode == 0)
	{
		if (target >= gAccels.size())
		{
			mode = 1;
			target = 0;
			return;
		}
		auto& a = gAccels[target];
		auto b = active_ball(table);
		if (!b)
		{
			fprintf(stderr, "DBG-accel no ball, relaunching\n");
			pb::launch_ball();
			return;
		}
		float r = sqrtf(a.RadiusSq);
		vector2 d{a.Dir.X, a.Dir.Y};
		float m = sqrtf(d.X * d.X + d.Y * d.Y);
		if (m < 1e-6f)
		{
			d = {0.0f, 1.0f};
			m = 1.0f;
		}
		d.X /= m;
		d.Y /= m;
		fprintf(stderr, "\nDBG-accel ==== inject accel[%zu] %s center=(%.2f,%.2f) r=%.3f group=0x%x "
		        "approach dir=(%.3f,%.3f) ====\n",
		        target, a.Name ? a.Name : "?", a.Center.X, a.Center.Y, r, a.Group, d.X, d.Y);
		b->Position.X = a.Center.X - d.X * r * 1.2f;
		b->Position.Y = a.Center.Y - d.Y * r * 1.2f;
		b->PrevPosition = b->Position;
		b->Direction.X = d.X;
		b->Direction.Y = d.Y;
		b->Direction.Z = 0.0f;
		b->Speed = 25.0f;
		b->CollisionComp = nullptr;
		b->CollisionMask = a.Group;
		b->StuckCounter = 0;
		b->LastActiveTime = pb::time_ticks;
		trace("inj", table);
		traceFrames = TRACE_LEN;
		target++;
		return;
	}

	if (target >= gRexits.size())
	{
		fprintf(stderr, "DBG-accel ==== done ====\n");
		stage = 99;
		return;
	}
	auto& x = gRexits[target];
	auto b = active_ball(table);
	if (!b)
	{
		pb::launch_ball();
		return;
	}
	if (!x.Built)
	{
		fprintf(stderr, "DBG-rexit ==== skip rexit[%zu] %s: not built ====\n", target, x.Name ? x.Name : "?");
		target++;
		return;
	}
	vector2 mid{(x.Start.X + x.End.X) / 2.0f, (x.Start.Y + x.End.Y) / 2.0f};
	vector2 seg{x.End.X - x.Start.X, x.End.Y - x.Start.Y};
	float sl = sqrtf(seg.X * seg.X + seg.Y * seg.Y);
	// Normal to the segment.
	vector2 n{-seg.Y / sl, seg.X / sl};
	fprintf(stderr, "\nDBG-rexit ==== inject rexit[%zu] %s mid=(%.2f,%.2f) n=(%.3f,%.3f) g1=0x%x g2=0x%x ====\n",
	        target, x.Name ? x.Name : "?", mid.X, mid.Y, n.X, n.Y, x.Group1, x.Group2);
	b->Position.X = mid.X - n.X * 0.4f;
	b->Position.Y = mid.Y - n.Y * 0.4f;
	b->PrevPosition = b->Position;
	b->Direction.X = n.X;
	b->Direction.Y = n.Y;
	b->Direction.Z = 0.0f;
	b->Speed = 12.0f;
	b->CollisionComp = nullptr;
	b->CollisionMask = x.Group1;
	b->StuckCounter = 0;
	b->LastActiveTime = pb::time_ticks;
	trace("inj", table);
	traceFrames = TRACE_LEN;
	target++;
}
// ---- end FTDBG ----
