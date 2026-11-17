#pragma once
#include "TCircle.h"
#include "TCollisionComponent.h"
#include "TEdgeManager.h"

class THole :
	public TCollisionComponent
{
public:
	THole(TPinballTable* table, int groupIndex);
	int Message(MessageCode code, float value) override;
	void Collision(TBall* ball, vector2* nextPosition, vector2* direction, float distance,
	               TEdgeSegment* edge) override;
	int FieldEffect(TBall* ball, vector2* vecDst) override;

	static void TimerExpired(int timerId, void* caller);

	bool BallCapturedFlag{};
	bool BallCapturedSecondStage{};
	int Timer;
	float Unknown3;
	float Unknown4;
	float GravityMult;
	float ZSetValue;
	int CollisionMask;
	float GravityPull;
	// DAT attribute 1306, Full Tilt's +0x92: a constant z speed for the release lift.
	// 0 (absent) keeps the gravity fall onto ZSetValue.
	float LiftSpeed;
	circle_type Circle{};
	field_effect_type Field{};
};
