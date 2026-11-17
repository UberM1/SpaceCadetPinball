#include "pch.h"
#include "TPlunger.h"


#include "control.h"
#include "loader.h"
#include "maths.h"
#include "pb.h"
#include "render.h"
#include "TBall.h"
#include "timer.h"
#include "TPinballTable.h"
#include "FtAccelDebug.h" // FTDBG

TPlunger::TPlunger(TPinballTable* table, int groupIndex, bool launcher)
	: TCollisionComponent(table, groupIndex, true), LauncherFlag(launcher)
{
	visualStruct visual{};

	loader::query_visual(groupIndex, 0, &visual);
	Boost = 0.0;
	BallFeedTimer_ = 0;
	PullbackTimer_ = 0;
	SoundIndexP1 = visual.SoundIndex4;
	SoundIndexP2 = visual.SoundIndex3;
	HardHitSoundId = visual.Kicker.HardHitSoundId;
	Threshold = 1000000000.0;

	// CADET.EXE's TPlunger constructor (0x418800). 3DPB hardcodes MaxPullback and
	// floors the increment; Full Tilt reads MaxPullback out of float attribute 402,
	// defaulting to 50 when the group has none (0x41887f), and does not floor.
	// Skulduggery's s_launcher1 carries 402 = 100.
	if (pb::FullTiltMode)
	{
		float* maxPullbackAttr = loader::query_float_attribute_opt(groupIndex, 0, 402);
		MaxPullback = maxPullbackAttr ? maxPullbackAttr[0] : 50.0f;

		// 0x418885: the launcher skips this whole computation, so PullbackIncrement
		// keeps the zero the calloc at 0x42acf9 left. That is not a loose end -- the
		// launcher never charges up (see PlungerInputPressed below), and its collision
		// boost comes from the SomeCounter path, which reads MaxPullback directly.
		if (!LauncherFlag)
		{
			// 0x41888e: no sprite list, or an empty one, means one step to full.
			PullbackIncrement = ListBitmap && !ListBitmap->empty()
				                    ? MaxPullback / (ListBitmap->size() * 8.0f)
				                    : MaxPullback;
		}
		else
		{
			PullbackIncrement = 0.0f;
		}
	}
	else
	{
		MaxPullback = 100;
		auto bmpCount = ListBitmap && !ListBitmap->empty() ? ListBitmap->size() : 1u;
		PullbackIncrement = std::floor(MaxPullback / (bmpCount * 8.0f));
	}

	Elasticity = 0.5f;
	Smoothness = 0.5f;
	
	// 0x4188e3: Full Tilt steps the pullback every 0.05 s ([+0x82] = 0x3ccccccd, read by
	// PullbackTimer at 0x418dbe/0x418dd2); 3DPB every 0.025 s.
	PullbackDelay = pb::FullTiltMode ? 0.05f : 0.025f;
	// 601 is where the plunger feeds the ball in. Skulduggery's launcher (object type
	// 1034) is mapped onto TPlunger and need not carry one; absent means this component
	// contributes no feed point, so leave the table's existing one alone.
	float* floatArr = loader::query_float_attribute_opt(groupIndex, 0, 601);
	if (floatArr)
		table->PlungerPosition = {floatArr[0], floatArr[1]};
}

void TPlunger::Collision(TBall* ball, vector2* nextPosition, vector2* direction, float distance, TEdgeSegment* edge)
{
	if (PinballTable->TiltLockFlag || SomeCounter > 0) 
	{
		auto boost = RandFloat() * MaxPullback * 0.1f + MaxPullback;
		maths::basic_collision(ball, nextPosition, direction, Elasticity, Smoothness, 0, boost);
		if (SomeCounter)
			SomeCounter--;
		Message(MessageCode::PlungerInputReleased, 0.0);
	}
	else 
	{
		auto boost = RandFloat() * Boost * 0.1f + Boost;
		maths::basic_collision(ball, nextPosition, direction, Elasticity, Smoothness, Threshold, boost);
	}
}

int TPlunger::Message(MessageCode code, float value)
{
	if (ft_autodrain_enabled())
		fprintf(stderr, "DBG-plunger msg=%d value=%f launcher=%d someCounter=%d multiball=%d\n",
		        static_cast<int>(code), value, (int)LauncherFlag, SomeCounter, PinballTable->MultiballCount);
	switch (code)
	{
	case MessageCode::PlungerInputPressed:
		if (!PullbackStartedFlag && (!pb::FullTiltMode || PinballTable->MultiballCount > 0 && !PinballTable->
			TiltLockFlag))
		{
			// 0x4189a4: the launcher has no pullback at all. Pressing the key fires it
			// at once, at full power, via PlungerLaunchBall -- the cannon behaviour
			// Skulduggery's s_launcher1 needs, since it has no plunger to charge.
			if (LauncherFlag)
			{
				Message(MessageCode::PlungerLaunchBall, 0.0f);
				break;
			}

			PullbackStartedFlag = true;
			Boost = 0.0;
			Threshold = 1000000000.0;
			loader::play_sound(HardHitSoundId, this, "TPlunger1");
			PullbackTimer(0, this);
		}
		break;
	case MessageCode::PlungerFeedBall:
		{
			if (PinballTable->BallCountInRect(PinballTable->PlungerPosition, PinballTable->CollisionCompOffset * 1.2f))
			{
				timer::set(1.0f, this, BallFeedTimer);
			}
			else
			{
				auto ball = PinballTable->AddBall(PinballTable->PlungerPosition);
				assertm(ball, "Failure to create ball in plunger");
				PinballTable->MultiballCount++;
				PinballTable->BallInDrainFlag = 0;
				pb::tilt_no_more();
			}
			break;
		}
	case MessageCode::PlungerStartFeedTimer:
		timer::set(0.95999998f, this, BallFeedTimer);
		loader::play_sound(SoundIndexP1, this, "TPlunger2");
		break;
	case MessageCode::PlungerLaunchBall:
		PullbackStartedFlag = true;
		Boost = MaxPullback;
		Message(MessageCode::PlungerInputReleased, 0.0f);
		break;
	case MessageCode::PlungerRelaunchBall:
		SomeCounter++;
		timer::set(value, this, BallFeedTimer);
		loader::play_sound(SoundIndexP1, this, "TPlunger2_1");
		PullbackStartedFlag = true;
		PullbackTimer(0, this);
		break;
	case MessageCode::PlayerChanged:
		// Full Tilt swaps the per-player MessageField before clearing the pullback,
		// 0x418c48 call 0x413570.
		FtPlayerFieldsSwap(value);
		PullbackStartedFlag = false;
		Boost = 0.0f;
		Threshold = 1000000000.0f;
		SomeCounter = 0;
		timer::kill(BallFeedTimer);
		timer::kill(PullbackTimer);
		timer::kill(ReleasedTimer);
		break;
	case MessageCode::SetTiltLock:
		SomeCounter = 0;
		timer::kill(BallFeedTimer);
		break;
	case MessageCode::PlungerInputReleased:
	case MessageCode::Resume:
	case MessageCode::LooseFocus:
		if (PullbackStartedFlag && !SomeCounter)
		{
			PullbackStartedFlag = false;
			Threshold = 0.0;
			if (PullbackTimer_)
				timer::kill(PullbackTimer_);
			PullbackTimer_ = 0;
			loader::play_sound(SoundIndexP2, this, "TPlunger3");
			// 0x418a3d: skipped for the launcher, which has no sprite list.
			if (!LauncherFlag)
				SpriteSet(0);
			// 0x418aa0: Full Tilt holds the release boost for 0.2 s (push 0x3e4ccccd)
			// before ReleasedTimer (0x418930) drops it; 3DPB reuses PullbackDelay,
			// 0.025 s. A ball resting on the plunger only collides with it every
			// frame or two, so the 25 ms window often closed with no collision in it
			// and the key press did nothing -- on Skulduggery's launcher, where the
			// press is the release, most presses were lost.
			timer::set(pb::FullTiltMode ? 0.2f : PullbackDelay, this, ReleasedTimer);
		}
		break;
	case MessageCode::Reset:
	{
		// 0x418c8c call 0x413550, ahead of the field clears and timer kills.
		FtPlayerFieldsReset();
		PullbackStartedFlag = false;
		Boost = 0.0f;
		Threshold = 1000000000.0f;
		SomeCounter = 0;

		timer::kill(BallFeedTimer);
		timer::kill(PullbackTimer);
		timer::kill(ReleasedTimer);

		// 0x418cc5: skipped for the launcher, as above.
		if (!LauncherFlag)
			SpriteSet(0);
		break;
	}
	default:
		break;
	}

	control::handler(code, this);
	return 0;
}

void TPlunger::BallFeedTimer(int timerId, void* caller)
{
	auto plunger = static_cast<TPlunger*>(caller);
	if (ft_autodrain_enabled())
		fprintf(stderr, "DBG-plunger BallFeedTimer fired\n");
	plunger->Message(MessageCode::PlungerFeedBall, 0.0);
}

void TPlunger::PullbackTimer(int timerId, void* caller)
{
	auto plunger = static_cast<TPlunger*>(caller);
	plunger->Boost += plunger->PullbackIncrement;
	if (plunger->Boost <= plunger->MaxPullback)
	{
		if (plunger->SomeCounter)
		{
			plunger->PullbackTimer_ = timer::set(plunger->PullbackDelay / 4.0f, plunger, PullbackTimer);
		}
		else
		{
			plunger->PullbackTimer_ = timer::set(plunger->PullbackDelay, plunger, PullbackTimer);
		}
	}
	else
	{
		plunger->PullbackTimer_ = 0;
		plunger->Boost = plunger->MaxPullback;
	}

	// 0x418dea: with no sprite list there is nothing to step, and the original skips
	// the whole block rather than calling SpriteSet with a degenerate index. That is
	// the launcher's case on every tick.
	if (!plunger->ListBitmap || plunger->ListBitmap->empty())
		return;

	int index = static_cast<int>(floor(
		static_cast<float>(plunger->ListBitmap->size() - 1) *
		(plunger->Boost / plunger->MaxPullback)));
	plunger->SpriteSet(index);
}

void TPlunger::ReleasedTimer(int timerId, void* caller)
{
	auto plunger = static_cast<TPlunger*>(caller);
	plunger->Threshold = 1000000000.0;
	plunger->Boost = 0.0;
}
