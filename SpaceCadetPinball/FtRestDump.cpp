// ---- FTDBG: rest-state dump for the differential harness (test/restdump/) ----
//
// FT_RESTDUMP=<path> makes the engine write one JSONL snapshot of the table at rest
// -- a new game started, the first ball fed into the plunger lane and left there for
// FT_RESTDUMP_DELAY seconds of game time -- and exit. test/restdump/wine_probe.py
// reads the same records out of DRAGON.EXE / PIRATES.EXE running under Wine, and
// test/restdump/compare.py diffs the two.
//
// The field set is the intersection of what both sides can produce: every field
// written here has a counterpart whose offset in the originals was read from the
// listings (the offsets are cited in wine_probe.py, not here). Timer handles, frame
// times and the flasher's current blink phase are deliberately left out so that two
// runs give byte-identical output.
//
// Without the variable this is one getenv per process and nothing else.
#include "pch.h"
#include "FtRestDump.h"

#include "control.h"
#include "pb.h"
#include "table_rules.h"
#include "TBall.h"
#include "TBlocker.h"
#include "TBumper.h"
#include "TKickback.h"
#include "TLight.h"
#include "TLightGroup.h"
#include "TPinballComponent.h"
#include "TPinballTable.h"
#include "TTimer.h"

#include <string>

namespace
{
const char* dump_path()
{
	static int state = -1;
	static const char* path = nullptr;
	if (state < 0)
	{
		path = getenv("FT_RESTDUMP");
		state = path && *path ? 1 : 0;
	}
	return state ? path : nullptr;
}

std::string jstr(const char* s)
{
	if (!s)
		return "null";
	std::string out = "\"";
	for (; *s; ++s)
	{
		auto c = static_cast<unsigned char>(*s);
		if (c == '"' || c == '\\')
		{
			out += '\\';
			out += static_cast<char>(c);
		}
		else if (c < 0x20 || c >= 0x7f)
		{
			char buf[8];
			snprintf(buf, sizeof buf, "\\u%04x", c);
			out += buf;
		}
		else
			out += static_cast<char>(c);
	}
	return out + "\"";
}

// Floats are printed with %.6g on both sides, so a value read from the DAT compares
// equal as text.
std::string jfloat(float f)
{
	char buf[32];
	snprintf(buf, sizeof buf, "%.6g", static_cast<double>(f));
	return buf;
}

bool ball_in_plunger_lane(TPinballTable* table)
{
	// The fed ball does not stay at PlungerPosition: on Dragon's Keep it rests on the
	// plunger tip 0.6 below it, on Skulduggery it rolls 3.7 down the launcher lane. So
	// the test is a strip of the lane: one ball in play, within 1 of the feed point's x,
	// between 1.5 above and 5 below it, and not moving. (Skulduggery's captive ball is
	// active from the start but is nowhere near the lane.)
	if (!table->Plunger || table->MultiballCount != 1)
		return false;
	for (auto ball : table->BallList)
	{
		if (!ball->ActiveFlag)
			continue;
		auto dx = ball->Position.X - table->PlungerPosition.X;
		auto dy = ball->Position.Y - table->PlungerPosition.Y;
		if (dx > -1.0f && dx < 1.0f && dy > -1.5f && dy < 5.0f && ball->Speed < 0.5f)
			return true;
	}
	return false;
}

const char* name_of(TPinballComponent* comp)
{
	return comp ? comp->GroupName : nullptr;
}

void write_component(FILE* f, size_t index, TPinballComponent* comp, bool tagged)
{
	fprintf(f, "{\"kind\":\"component\",\"index\":%zu,\"name\":%s,\"type\":%d,\"tagged\":%s,"
	        "\"active\":%d,\"message_field\":%d",
	        index, jstr(comp->GroupName).c_str(), comp->GetComponentType(), tagged ? "true" : "false",
	        comp->ActiveFlag ? 1 : 0, comp->MessageField);

	if (auto light = dynamic_cast<TLight*>(comp))
	{
		fprintf(f, ",\"light_on\":%d,\"flasher_on\":%d,\"toggled_off\":%d,\"toggled_on\":%d,"
		        "\"turn_off_after_flashing\":%d,\"on_bmp_index\":%d,\"tmp_override\":%d,"
		        "\"timeout_running\":%d",
		        light->LightOnFlag ? 1 : 0, light->FlasherOnFlag ? 1 : 0, light->ToggledOffFlag ? 1 : 0,
		        light->ToggledOnFlag ? 1 : 0, light->TurnOffAfterFlashingFg ? 1 : 0, light->LightOnBmpIndex,
		        light->TemporaryOverrideFlag ? 1 : 0, light->TimeoutTimer ? 1 : 0);
	}
	else if (auto group = dynamic_cast<TLightGroup*>(comp))
	{
		int on = 0;
		std::string members = "[";
		for (size_t i = 0; i < group->List.size(); ++i)
		{
			auto l = group->List[i];
			if (l && l->LightOnFlag)
				++on;
			if (i)
				members += ",";
			members += jstr(l ? l->GroupName : nullptr);
		}
		members += "]";
		fprintf(f, ",\"message_field2\":%d,\"animation_flag\":%d,\"timer1_time\":%s,"
		        "\"light_count\":%zu,\"lights_on\":%d,\"members\":%s",
		        static_cast<int>(group->MessageField2), group->AnimationFlag ? 1 : 0,
		        jfloat(group->Timer1Time).c_str(), group->List.size(), on, members.c_str());
	}
	else if (auto blocker = dynamic_cast<TBlocker*>(comp))
	{
		std::string flags = "[";
		for (int i = 0; i < TBlocker::MaxPositions; ++i)
		{
			if (i)
				flags += ",";
			flags += blocker->PositionFlags[i] ? "1" : "0";
		}
		flags += "]";
		fprintf(f, ",\"current_position\":%d,\"home_position\":%d,\"target_position\":%d,"
		        "\"motion_running\":%d,\"timeout_running\":%d,\"position_flags\":%s",
		        blocker->CurrentPosition, blocker->HomePosition,
		        blocker->MotionTimer ? blocker->TargetPosition : -1,
		        blocker->MotionTimer ? 1 : 0, blocker->Timer ? 1 : 0, flags.c_str());
	}
	else if (auto kick = dynamic_cast<TKickback*>(comp))
	{
		fprintf(f, ",\"kick_active\":%d,\"suppressed\":%d", kick->KickActiveFlag ? 1 : 0,
		        kick->Suppressed ? 1 : 0);
	}
	else if (auto bumper = dynamic_cast<TBumper*>(comp))
	{
		fprintf(f, ",\"bmp_index\":%d", bumper->BmpIndex);
	}
	else if (auto t = dynamic_cast<TTimer*>(comp))
	{
		fprintf(f, ",\"running\":%d", t->Timer ? 1 : 0);
	}
	fputs("}\n", f);
}

void write_dump(const char* path, TPinballTable* table)
{
	FILE* f = fopen(path, "w");
	if (!f)
	{
		fprintf(stderr, "FT_RESTDUMP: cannot open %s\n", path);
		return;
	}

	auto rules = CurrentTable;
	fprintf(f, "{\"kind\":\"meta\",\"side\":\"native\",\"table\":%s,\"score_components\":%zu,"
	        "\"simple_components\":%zu,\"component_count\":%zu}\n",
	        jstr(rules ? rules->Name : nullptr).c_str(), rules ? rules->ScoreComponentCount : 0,
	        rules ? rules->SimpleComponentCount : 0, table->ComponentList.size());

	int mode = 0;
	if (rules && rules->ModeRegister && rules->ModeRegister->GetComponent())
		mode = rules->ModeRegister->GetComponent()->MessageField;
	fprintf(f, "{\"kind\":\"table\",\"score\":%d,\"score_e9\":%d,\"player_count\":%d,"
	        "\"current_player\":%d,\"score_multiplier\":%d,\"score_added\":%d,\"bonus_score\":%d,"
	        "\"bonus_score_flag\":%d,\"jackpot_score\":%d,\"jackpot_score_flag\":%d,"
	        "\"super_jackpot_score\":%d,\"super_jackpot_score_flag\":%d,\"ball_count\":%d,"
	        "\"max_ball_count\":%d,\"extra_balls\":%d,\"multiball_count\":%d,"
	        "\"ball_locked_counter\":%d,\"multiball_flag\":%d,\"replay_active_flag\":%d,"
	        "\"tilt_lock_flag\":%d,\"mode\":%d}\n",
	        table->CurScore, table->CurScoreE9, table->PlayerCount, table->CurrentPlayer,
	        table->ScoreMultiplier, table->ScoreAdded, table->BonusScore, table->BonusScoreFlag ? 1 : 0,
	        table->JackpotScore, table->JackpotScoreFlag ? 1 : 0, table->SuperJackpotScore,
	        table->SuperJackpotScoreFlag ? 1 : 0, table->BallCount, table->MaxBallCount,
	        table->ExtraBalls, table->MultiballCount, table->BallLockedCounter,
	        table->MultiballFlag ? 1 : 0, table->ReplayActiveFlag ? 1 : 0, table->TiltLockFlag ? 1 : 0,
	        mode);

	// The tags, in the order of the table's own score_components[] / simple_components[].
	std::vector<TPinballComponent*> tagged;
	auto emit_tag = [&](const char* array, size_t i, component_tag_base* tag)
	{
		auto comp = tag ? tag->GetComponent() : nullptr;
		if (comp)
			tagged.push_back(comp);
		fprintf(f, "{\"kind\":\"tag\",\"array\":\"%s\",\"index\":%zu,\"name\":%s,\"bound\":%s}\n",
		        array, i, jstr(tag ? tag->Name : nullptr).c_str(), jstr(name_of(comp)).c_str());
	};
	if (rules)
	{
		for (size_t i = 0; i < rules->ScoreComponentCount; ++i)
			emit_tag("score", i, &rules->ScoreComponents[i].Tag);
		for (size_t i = 0; i < rules->SimpleComponentCount; ++i)
			emit_tag("simple", i, rules->SimpleComponents[i]);
	}

	for (size_t i = 0; i < table->ComponentList.size(); ++i)
	{
		auto comp = table->ComponentList[i];
		if (!comp)
			continue;
		bool isTagged = std::find(tagged.begin(), tagged.end(), comp) != tagged.end();
		write_component(f, i, comp, isTagged);
	}
	fclose(f);
	fprintf(stderr, "FT_RESTDUMP: wrote %s\n", path);
}
}

void ft_restdump_tick(float dtSec)
{
	auto path = dump_path();
	if (!path)
		return;
	auto table = pb::MainTable;
	if (!table || pb::game_mode != GameModes::InGame)
		return;

	static float restDelay = -1.0f;
	static float atRest = 0.0f;
	if (restDelay < 0.0f)
	{
		auto e = getenv("FT_RESTDUMP_DELAY");
		restDelay = e ? static_cast<float>(atof(e)) : 2.0f;
		if (restDelay < 0.0f)
			restDelay = 0.0f;
	}

	if (!ball_in_plunger_lane(table))
	{
		atRest = 0.0f;
		return;
	}
	atRest += dtSec;
	if (atRest < restDelay)
		return;

	write_dump(path, table);
	fflush(nullptr);
	// No teardown: the dump is the whole point of the run, and the static destructors
	// would run against a live SDL window.
	std::_Exit(0);
}
// ---- end FTDBG ----
