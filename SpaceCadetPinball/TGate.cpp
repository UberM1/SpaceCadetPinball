#include "pch.h"
#include "TGate.h"


#include "control.h"
#include "loader.h"
#include "pb.h"
#include "render.h"

TGate::TGate(TPinballTable* table, int groupIndex) : TCollisionComponent(table, groupIndex, true)
{
	visualStruct visual{};

	loader::query_visual(groupIndex, 0, &visual);
	SoundIndex4 = visual.SoundIndex4;
	SoundIndex3 = visual.SoundIndex3;
	ActiveFlag = 1;
	SpriteSet(0);
	control::handler(MessageCode::Reset, this);
}

int TGate::Message(MessageCode code, float value)
{
	// Full Tilt, 0x415410. PlayerChanged swaps the per-player MessageField (0x415493
	// call 0x413570) and then runs straight into the 0x3e TGateDisable arm at 0x415498 --
	// ActiveFlag = 0, no sprite, the disable sound -- before the shared control::handler
	// call at 0x4154bc. Reset clears the fields first (0x4154ca call 0x413550) and then
	// takes the enable path as below. 3D Pinball's TGate ignores PlayerChanged.
	if (code == MessageCode::Reset)
		FtPlayerFieldsReset();
	if (code == MessageCode::PlayerChanged && pb::FullTiltMode)
	{
		FtPlayerFieldsSwap(value);
		ActiveFlag = 0;
		SpriteSet(-1);
		loader::play_sound(SoundIndex3, this, "TGate1");
	}

	switch (code)
	{
	case MessageCode::TGateDisable:
		ActiveFlag = 0;
		SpriteSet(-1);
		loader::play_sound(SoundIndex3, this, "TGate1");
		break;
	case MessageCode::Reset:
	case MessageCode::TGateEnable:
		ActiveFlag = 1;
		SpriteSet(0);
		if (code == MessageCode::TGateEnable)
			loader::play_sound(SoundIndex4, this, "TGate2");
		break;
	default: break;
	}

	control::handler(code, this);
	return 0;
}
