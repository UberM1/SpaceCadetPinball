#pragma once

#include "maths.h"
#include "TPinballComponent.h"

class TBall;
struct scoreStruct;
class TFlipper;
class TPlunger;
class TCollisionComponent;
class TDrain;
class TDemo;
class TLightGroup;
class TLightSweep;
class TTableLayer;

struct score_struct_super
{
	scoreStruct* ScoreStruct;
	int Score;
	int ScoreE9Part;
	int JackpotScore;
	int BallCount;
	int ExtraBalls;
	int BallLockedCounter;
	// Full Tilt only: the player's super jackpot, +0x92 of the originals' record.
	int SuperJackpotScore;
};


class TPinballTable : public TPinballComponent
{
public:
	TPinballTable();
	~TPinballTable() override;
	TPinballComponent* find_component(LPCSTR componentName);
	TPinballComponent* find_component_opt(LPCSTR componentName);
	TPinballComponent* find_component(int groupIndex);
	int AddScore(int score);
	void ChangeBallCount(int count);
	void tilt(float time);
	void port_draw() override;
	int Message(MessageCode code, float value) override;
	TBall* AddBall(vector2 position);
	int BallCountInRect(const RectF& rect);
	int BallCountInRect(const vector2& pos, float margin);
	// Counting only the balls on one collision layer. THole's release gate needs it:
	// the original's 0x4130c0 filters on CollisionMask as well as ActiveFlag, which is
	// what lets the hole ignore the ball it is itself about to release -- that ball
	// still carries the hole's capture mask until the release assigns the new one.
	int BallCountInRect(const RectF& rect, int collisionMask);

	static void EndGame_timeout(int timerId, void* caller);
	static void LightShow_timeout(int timerId, void* caller);
	static void replay_timer_callback(int timerId, void* caller);
	static void tilt_timeout(int timerId, void* caller);

	void MessageFlippers(int typeCode, MessageCode code, float value);

	// Kept for the code that reaches for "the" flipper; input goes through
	// MessageFlippers so every flipper on a side responds.
	TFlipper* FlipperL;
	TFlipper* FlipperR;
	scoreStruct* CurScoreStruct;
	scoreStruct* ScoreBallcount;
	scoreStruct* ScorePlayerNumber1;
	int CheatsUsed{};
	int SoundIndex1{};
	int SoundIndex2{};
	int SoundIndex3{};
	int BallInDrainFlag;
	int CurScore{};
	int CurScoreE9{};
	int LightShowTimer;
	int EndGameTimeoutTimer;
	int TiltTimeoutTimer;
	score_struct_super PlayerScores[4]{};
	int PlayerCount;
	int CurrentPlayer;
	TPlunger* Plunger;
	// Resolved once here from TableRules::UnstuckBoundsExtra, because
	// control::UnstuckBall runs for every ball on every frame and must not do a linear
	// scan of ComponentList to find them. Empty on Space Cadet.
	std::vector<TCollisionComponent*> UnstuckBounds;
	TDrain* Drain;
	TDemo* Demo{};
	int XOffset{};
	int YOffset{};
	int Width{};
	int Height{};
	std::vector<TPinballComponent*> ComponentList;
	std::vector<TBall*> BallList;
	std::vector<TFlipper*> FlipperList;
	TLightGroup* LightGroup;

	// The playfield layer, kept because the light sweep needs its extent to lay out
	// its grid. The originals hold it on the table for the same reason.
	TTableLayer* TableLayer{};

	// Full Tilt's `light_sweeps` (object type 1038). At most one per table -- the
	// originals assert on a second -- and null on every Space Cadet table.
	TLightSweep* LightSweep{};
	float GravityDirVectMult{};
	float GravityAngleX{};
	float GravityAnglY{};
	float CollisionCompOffset{};
	vector2 PlungerPosition{};
	int ScoreMultiplier{};
	int ScoreAdded{};
	int ReflexShotScore{};
	int BonusScore{};
	bool BonusScoreFlag{};
	int JackpotScore{};
	bool JackpotScoreFlag{};

	// Full Tilt's second, larger jackpot, at +0x17a in the originals where
	// JackpotScore is +0x172. Dragon's Keep's moat chute pays into it and its
	// mouth sink pays it out. The table's Reset arm sets it to 20,000 (0x419e3a);
	// Space Cadet never reads it.
	int SuperJackpotScore{};
	bool SuperJackpotScoreFlag{};
	int UnknownP71{};
	int BallCount{};
	int MaxBallCount;
	int ExtraBalls{};
	int MultiballCount{};
	int BallLockedCounter{};
	bool MultiballFlag;
	int UnknownP78{};
	int ReplayActiveFlag{};
	int ReplayTimer;
	int UnknownP81{};
	int UnknownP82{};
	int TiltLockFlag;

private:
	static int score_multipliers[5];
};
