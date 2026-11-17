#pragma once
#include "TCollisionComponent.h"

class TPlunger :
	public TCollisionComponent
{
public:
	// `launcher` is CADET.EXE's third constructor argument: 0 for the ordinary
	// plunger (object type 1001), 1 for Skulduggery's ball launcher (1034).
	TPlunger(TPinballTable* table, int groupIndex, bool launcher = false);
	~TPlunger() override = default;
	void Collision(TBall* ball, vector2* nextPosition, vector2* direction, float distance,
	               TEdgeSegment* edge) override;
	int Message(MessageCode code, float value) override;

	static void BallFeedTimer(int timerId, void* caller);
	static void PullbackTimer(int timerId, void* caller);
	static void ReleasedTimer(int timerId, void* caller);

	int PullbackTimer_;
	int BallFeedTimer_;
	float MaxPullback;
	float PullbackIncrement;
	float PullbackDelay;
	int SoundIndexP1;
	int SoundIndexP2;
	bool PullbackStartedFlag{};
	int SomeCounter{};
	// CADET.EXE's TPlunger field +0x96, the last dword of the 0x9a-byte object.
	// Four reads, all of them "this plunger has no pullback and no sprites":
	//   ctor (0x418885)                  skip the PullbackIncrement computation
	//   PlungerInputPressed (0x4189a4)   launch at once instead of charging up
	//   PlungerInputReleased (0x418a3d)  skip SpriteSet(0)
	//   Reset (0x418cc5)                 skip SpriteSet(0)
	bool LauncherFlag{};
};
