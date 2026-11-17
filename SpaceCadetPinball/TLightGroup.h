#pragma once
#include "TPinballComponent.h"


class TLight;

struct TLightGroup_player_backup
{
	int MessageField;
	MessageCode MessageField2;
	float Timer1Time;
	int Unknown3;
};


class TLightGroup :
	public TPinballComponent
{
public:
	TLightGroup(TPinballTable* table, int groupIndex);
	~TLightGroup() override = default;
	int Message(MessageCode code, float value) override;
	virtual void Reset();
	void reschedule_animation(float time);
	void start_animation();
	int next_light_up();
	int next_light_down();

	static void TimerExpired(int timerId, void* caller);
	static void NotifyTimerExpired(int timerId, void* caller);

	std::vector<TLight*> List;

	// Indexed access to the group's members. The bonus tables walk a group one light
	// at a time -- Dragon's Keep's prison-window sweep overrides a single light on or
	// off as a cursor passes over it -- which the group-wide operations cannot express.
	// Returns nullptr out of range rather than asserting, because the cursor wraps.
	TLight* LightAt(int index) const
	{
		if (index < 0 || static_cast<size_t>(index) >= List.size())
			return nullptr;
		return List[index];
	}
	float Timer1Time{};
	float Timer1TimeDefault;
	MessageCode MessageField2{};
	int AnimationFlag{};
	int NotifyTimer;
	int Timer;
	TLightGroup_player_backup PlayerData[4]{};
};
