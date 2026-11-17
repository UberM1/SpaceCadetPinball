#include "pch.h"
#include "table_rules.h"

#include <cctype>

#include "control.h"
#include "TLight.h"
#include "TTextBox.h"

// Space Cadet's mode register. control.cpp parks the mode in this dummy light's
// MessageField; the tag is the only handle on it that survives outside that file.
extern component_tag<TLight> control_lite198_tag;

// The bonus tables' bindings live in src/, which CMake now compiles. The switch
// stays so the engine can still be built as plain upstream plus the registry, with
// the two tables registered by identity and no rules behind them.
#ifndef FULLTILT_HAVE_BONUS_RULES
#define FULLTILT_HAVE_BONUS_RULES 1
#endif

#if FULLTILT_HAVE_BONUS_RULES
extern const RcString dragon_rc_strings[];
extern const size_t dragon_rc_strings_count;
extern const RcString pirates_rc_strings[];
extern const size_t pirates_rc_strings_count;

extern component_info dragon_score_components[75];
extern component_tag_base* dragon_simple_components[150];
extern component_info pirates_score_components[74];
extern component_tag_base* pirates_simple_components[192];

namespace dragon
{
	// The dispatcher, not the switch: it reads the mode register and calls
	// MissionControl(mode, code, caller) itself. See TableRules::ModeControl.
	void MissionHandler(MessageCode code, TPinballComponent* caller);
	void TableControl(MessageCode code);
	void KeyHandler(int key, bool flipper); // 0x4282c0

	// The mode register: Dragon parks the quest in its single TTextBox's
	// MessageField, which is also where its dispatcher reads it from.
	extern component_tag<TTextBox> tag_info_text_box;
}

namespace pirates
{
	void Handler(MessageCode code, TPinballComponent* caller);
	void TableControl(MessageCode code);
	void KeyHandler(int key, bool flipper); // 0x42a830
	extern component_tag<TTextBox> tag_info_text_box;
}
#endif

// Field order, because C++11 has no designated initializers:
//   Id, Name, DatFileName, FullTiltData, DemoData,
//   ScoreComponents, ScoreComponentCount, SimpleComponents, SimpleComponentCount,
//   MultiplierIsValue, MovingBlockers, ModeRegister, ModeControl, ModeBeforeComponent,
//   ResetRules, BallDrained, CurrentRank, CurrentMode,
//   MusicPrefix, MusicTrackCount, SettingsPrefix, RcStrings, RcStringCount,
//   UnstuckBoundsExtra, TableControl, KeyHandler (trailing, so rows that leave them
//   out get nullptr)
static const TableRules TableRegistry[] =
{
	// The same table in three releases: one set of rules, three data files. The
	// FullTiltData / DemoData pair is what the engine's ~25 era checks read.
	//
	// Registry order is the order pb::SelectDatFile probes, so these three lead and
	// CADET.DAT is tried first -- upstream's default, preserved verbatim.
	{
		TableId::SpaceCadetFT, "Full Tilt! Space Cadet", "CADET.DAT", true, false,
		control::score_components, 88, control::simple_components, 145,
		false, false, &control_lite198_tag, &control::MissionControl, false,
		nullptr, nullptr, nullptr, nullptr,
		"TABA", 3, "CADET", nullptr, 0,
	},
	{
		TableId::SpaceCadet3DPB, "3D Pinball Space Cadet", "PINBALL.DAT", false, false,
		control::score_components, 88, control::simple_components, 145,
		false, false, &control_lite198_tag, &control::MissionControl, false,
		nullptr, nullptr, nullptr, nullptr,
		"PINBALL", 0, "3DPB", nullptr, 0,
	},
	{
		TableId::SpaceCadetDemo, "Full Tilt! Space Cadet (demo)", "DEMO.DAT", true, true,
		control::score_components, 88, control::simple_components, 145,
		false, false, &control_lite198_tag, &control::MissionControl, false,
		nullptr, nullptr, nullptr, nullptr,
		"TABA", 3, "DEMO", nullptr, 0,
	},

	// The bonus tables are registered unconditionally so SelectDatFile can find
	// their DAT and the identity is real. Their rules are bound behind
	// FULLTILT_HAVE_BONUS_RULES, which is on; built with it off, the identity and
	// the music survive and the bindings go null, which makes the playfield load
	// and nothing score rather than crash.
	{
		TableId::DragonsKeep, "Dragon's Keep", "DRAGON.DAT", true, false,
#if FULLTILT_HAVE_BONUS_RULES
		dragon_score_components, 75, dragon_simple_components, 150,
		true, true,
		// The mode register is the table's one TTextBox; each table's tag now lives
		// in its own namespace, so there is an unambiguous symbol to bind.
		&dragon::tag_info_text_box, &dragon::MissionHandler, true,
#else
		nullptr, 0, nullptr, 0,
		true, true,
		nullptr, nullptr, false,
#endif
		nullptr, nullptr, nullptr, nullptr,
		"TABC", 7, "DRAGON", dragon_rc_strings, dragon_rc_strings_count,
		// What DRAGON.EXE's UnstuckBall (0x42c320) checks besides the plunger.
		"a_flip1,a_flip3,a_flip2",
#if FULLTILT_HAVE_BONUS_RULES
		&dragon::TableControl,
		&dragon::KeyHandler,
#endif
	},
	{
		TableId::Skulduggery, "Skulduggery", "PIRATES.DAT", true, false,
#if FULLTILT_HAVE_BONUS_RULES
		pirates_score_components, 74, pirates_simple_components, 192,
		true, true,
		&pirates::tag_info_text_box, &pirates::Handler, true,
#else
		nullptr, 0, nullptr, 0,
		true, true,
		nullptr, nullptr, false,
#endif
		nullptr, nullptr, nullptr, nullptr,
		"TABB", 6, "PIRATES", pirates_rc_strings, pirates_rc_strings_count,
		// What PIRATES.EXE's UnstuckBall (0x42db30) checks besides the launcher.
		"a_flip1,a_flip3,a_flip2,a_flip4,s_oneway12,a_kick1",
#if FULLTILT_HAVE_BONUS_RULES
		&pirates::TableControl,
		&pirates::KeyHandler,
#endif
	},
};

// 3D Pinball, not the first registry entry: an engine that has not yet run
// SelectDatFile must read the same era flags upstream's `FullTiltMode =
// FullTiltDemoMode = false` gave it. midi's music_init runs before pb::init, and
// SelectDatFile leaves this alone when it finds no DAT at all, so it is never null.
const TableRules* CurrentTable = FindTableRules(TableId::SpaceCadet3DPB);

// Binary search; the generator emits each table sorted by id.
const char* FindRcString(const TableRules* rules, int id)
{
	if (!rules || !rules->RcStrings)
		return nullptr;

	size_t lo = 0, hi = rules->RcStringCount;
	while (lo < hi)
	{
		auto mid = lo + (hi - lo) / 2;
		auto& entry = rules->RcStrings[mid];
		if (entry.Id == id)
			return entry.Text;
		if (entry.Id < id)
			lo = mid + 1;
		else
			hi = mid;
	}
	return nullptr;
}

const TableRules* FindTableRules(TableId id)
{
	for (auto& rules : TableRegistry)
		if (rules.Id == id)
			return &rules;
	return nullptr;
}

const TableRules* FindTableRulesForDat(const char* datFileName)
{
	if (!datFileName)
		return nullptr;

	for (auto& rules : TableRegistry)
	{
		// SelectDatFile probes each name lowercased as well, so the match cannot be
		// case sensitive.
		auto a = datFileName, b = rules.DatFileName;
		for (; *a && *b; a++, b++)
			if (std::tolower(static_cast<unsigned char>(*a)) !=
				std::tolower(static_cast<unsigned char>(*b)))
				break;
		if (!*a && !*b)
			return &rules;
	}
	return nullptr;
}

const char* const* TableDatSearchOrder(size_t& count)
{
	// Static, because the order is the registry's order and the caller iterates it.
	// Space Cadet's three come first, so a drive holding both CADET.DAT and
	// DRAGON.DAT still boots Space Cadet exactly as before.
	static const char* names[sizeof TableRegistry / sizeof TableRegistry[0]];
	count = sizeof names / sizeof names[0];
	for (size_t i = 0; i < count; i++)
		names[i] = TableRegistry[i].DatFileName;
	return names;
}
