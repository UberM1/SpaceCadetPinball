#pragma once

// A per-table rules interface, which upstream does not have.
//
// SpaceCadetPinball hardcodes Space Cadet throughout: `control` is a class of
// statics bound to one table's components, `pb::FullTiltMode` / `FullTiltDemoMode`
// are two booleans standing in for a table identity, and `TPinballTable` reaches
// for Space Cadet's own components by name. None of that extends to a third and
// fourth table.
//
// The engine itself does extend. `TPinballTable` already builds the playfield from
// `table_objects` in the DAT, so geometry, collision, sprites and physics are
// table-agnostic today. What has to become table-dependent is the rules: the
// component bindings, the score multipliers, the mode machine and the texts.
//
// This header is the seam. Each table supplies one `TableRules` and the engine
// stops knowing which table it is running.
//
// It lives in the engine tree rather than src/ because the engine includes it --
// pb.cpp, midi.cpp and table_rules.cpp all do -- and src/ already compiles with
// -I on this directory, so one copy serves both sides.

#include "control.h"

class TPinballTable;
class TPinballComponent;
enum class MessageCode;

enum class TableId
{
	SpaceCadet3DPB,  // PINBALL.DAT, the Windows release
	SpaceCadetFT,    // CADET.DAT, the Full Tilt release of the same table
	SpaceCadetDemo,  // DEMO.DAT
	DragonsKeep,     // DRAGON.DAT
	Skulduggery,     // PIRATES.DAT
};

// One entry of a table's own RT_STRING resource table.
//
// The two Full Tilt bonus tables identify their messages by the resource ids of
// their own executables (Dragon's Keep and Skulduggery both run 1001..3076), while
// the port's `Msg` enum is Space Cadet's string table and nothing else. Looking a
// bonus-table id up in it misses, and translations::GetTranslation answers
// "!Missing MsgId!" -- which is what the playfield showed for every message the
// rules displayed.
struct RcString
{
	int Id;
	const char* Text;
};

struct TableRules
{
	TableId Id;
	const char* Name;        // window title and About text
	const char* DatFileName; // selects the data file, replacing pb::SelectDatFile's fixed list

	// Which era the DAT is from, and whether it is a demo cut of it. These are not
	// derivable from `Id` by anything but a switch, and the engine asks about them
	// constantly -- resolution count, SOUND/ subfolder, kicker Z attribute, plunger
	// and hole timings. `pb::FullTiltMode` / `pb::FullTiltDemoMode` are now views of
	// these two fields, which is what keeps ~25 existing call sites honest.
	bool FullTiltData;
	bool DemoData;

	// The bindings `control::make_links` walks. Space Cadet's are
	// score_components[88] / simple_components[145]; the bonus tables have their
	// own, recovered from the originals' .data.
	component_info* ScoreComponents;
	size_t ScoreComponentCount;
	component_tag_base** SimpleComponents;
	size_t SimpleComponentCount;

	// No per-table multiplier array is needed, and adding one would be wrong.
	//
	// Upstream models the multiplier as an index: `score_multipliers[5] =
	// {1,2,3,5,10}` with `ScoreMultiplier` holding 0..4, neutral at 0. That is a
	// 3D Pinball-ism. The Full Tilt builds hold the multiplier VALUE directly --
	// CADET.EXE's AddScore is `*(this+0x15e) * score + ScoreAdded` with no indexed
	// load, and its callers write 1, 2, 3, 5 and 10 into that field, neutral at 1.
	// Dragon's Keep writes 2, 3, 4 and 5 there, which is simply 2x..5x.
	//
	// So the fix is to make `TPinballTable::ScoreMultiplier` the value everywhere
	// and have the 3D Pinball path write values instead of indices. A per-table
	// array would reintroduce the indexing this table never had.

	// How TPinballTable::ScoreMultiplier is read. 3D Pinball and Space Cadet hold
	// an INDEX into score_multipliers[5] (neutral 0); the Full Tilt bonus tables
	// hold the multiplier VALUE (neutral 1, written as 2..5 by the weapon and
	// fathom awards). Reading one as the other either scales wrongly or, for a
	// value of 5, indexes off the end of the array -- which is why this is a flag
	// and not a comment. The long note below says why no per-table array is right.
	bool MultiplierIsValue;

	// Which TBlocker the table gets. Two different classes share the name: upstream's
	// is 3D Pinball's two-state prop (sprite 0 / no sprite, one timer, raises
	// ControlTimerExpired), the Full Tilt engine's is a multi-position moving part with
	// a position index, one collision-edge set per position, a step timer and a second
	// timeout timer -- DRAGON.EXE 0x414d60 / 0x414f80, the same bytes at the same
	// addresses in CADET.EXE and PIRATES.EXE.
	//
	// So this is a flag and not a table property: the Full Tilt engine would give the
	// moving class to CADET.DAT too, but the port's Space Cadet rules are written
	// against the 3D Pinball one -- control::DrainBallBlockerControl needs
	// TBlockerEnable's own timer to raise ControlTimerExpired, and the moving class
	// raises ControlBlockerMotionDone from its step callback instead and never arms
	// that timer at all. True only for the two bonus tables, whose parts otherwise do
	// not move: the gargoyle rocks, dragon jaw, sword, centre post and moat blocker on
	// Dragon's Keep, the cannon and lane blockers on Skulduggery.
	bool MovingBlockers;

	// Where the table keeps its mode. Space Cadet uses the dummy light `lite198`,
	// Dragon's Keep the MessageField of its single TTextBox `info_text_box`. The
	// engine only needs to read and write it, not know which object it is.
	component_tag_base* ModeRegister;

	// The mode machine, as `control::handler` calls it: after the component's own
	// handler, with no mode argument. Space Cadet's is `control::MissionControl`
	// over 17 missions and 9 ranks; Dragon's Keep has 6 quests and no ranks at all,
	// so the rank hooks below are allowed to be null.
	//
	// The bonus tables split this in two -- a `(Mode, MessageCode, caller)` switch
	// plus a dispatcher that reads the mode register and calls it -- so the entry
	// point bound here is the dispatcher (`dragon::MissionHandler`,
	// `pirates::Handler`), not the switch.
	void (*ModeControl)(MessageCode code, TPinballComponent* caller);

	// Which of the two runs first. Space Cadet's `control::handler` calls the
	// component's own handler and then MissionControl; both bonus tables do the
	// opposite -- DRAGON.EXE's 0x423060 and PIRATES.EXE's 0x423080 both call the
	// mode machine and then the component's ControlFunc. It is not cosmetic: many
	// of Dragon's handlers suppress their own scoring while a quest is running,
	// which only works if the quest controller has already seen the message, and
	// running it the other way round double-scores silently.
	//
	// A field rather than something derived from `ModeControl`, because the order
	// is a property of the table's rules and not of which function implements them.
	bool ModeBeforeComponent;

	void (*ResetRules)(TPinballTable* table);
	void (*BallDrained)(TPinballTable* table);

	// Space Cadet 2026's HUD reads a rank; tables without one leave this null and
	// the HUD hides the section rather than showing an empty one.
	int (*CurrentRank)();
	int (*CurrentMode)();

	// midi.cpp loads TABA1..TABA3 for Space Cadet. Skulduggery ships TABB1..TABB6
	// and Dragon's Keep TABC1..TABC7, so the count is per table.
	//
	// A count of 0 means the prefix is the whole file name and there is no index:
	// 3D Pinball ships one track called PINBALL.MID, not PINBALL1.MID, so it cannot
	// be expressed as a one-entry indexed set.
	const char* MusicPrefix;
	int MusicTrackCount;

	// `options::TableKeyPrefix` already knows three tables; it needs five, and the
	// high score file has to be namespaced per table or they overwrite each other.
	const char* SettingsPrefix;

	// The table's own RT_STRING resources, sorted by id, or null for a table whose
	// messages are in the shared `Msg` enum. Space Cadet's three entries leave this
	// null and keep going through translations.cpp, so they stay localisable; the
	// bonus tables have no translations to go through -- their text only ever
	// existed in English, in their own executables.
	//
	// Generated into table_strings.cpp by analysis/gen_table_strings.py from the
	// dumps in analysis/, which is also where the reasoning for embedding rather
	// than reading at runtime is written down.
	const RcString* RcStrings;
	size_t RcStringCount;

	// A third component whose bounds also hold a ball legitimately still, beyond the
	// two flippers and the plunger that control::UnstuckBall always checks.
	//
	// The originals' UnstuckBall makes *four* CheckBallInControlBounds calls where
	// CADET.EXE's makes three: DRAGON.EXE 0x42c320 calls it at 0x42c32c, 0x42c33c,
	// 0x42c34c and 0x42c35c on the tags at 0x43a714, 0x43a724, 0x43a734 and 0x439d44 --
	// a_flip1, a_flip3, a_flip2 and s_plunger1 (names from analysis/dump_tags.py).
	// Dragon's Keep has four flippers and the rules check three of them, so "every
	// flipper" would not be faithful either; the names go here instead, comma
	// separated. Null on Space Cadet, whose two flippers control.cpp names itself.
	const char* UnstuckBoundsExtra;

	// The table's own table-level handler, the one TPinballTable calls with the
	// public codes (tilt, new game, game over). Space Cadet's is
	// `control::table_control_handler`'s body and stays there, so this is null for
	// it. The bonus tables each have their own -- DRAGON.EXE 0x424260, PIRATES.EXE
	// 0x4230c0, both reached from the call at 0x419e87 -- and running Space Cadet's
	// in their place dereferenced `lite77`, a tag neither table binds, on the first
	// tilt. It is also where each sets its opening jackpot, so it has to run after
	// TPinballTable's own NewGame body, which it does: the call is at the tail of
	// TPinballTable::Message.
	void (*TableControl)(MessageCode code);

	// The table's own key handler, which replaces Space Cadet's backdoor
	// (`control::pbctrl_bdoor_controller`'s cheat arms) on the tables that have one.
	// DRAGON.EXE 0x4282c0 and PIRATES.EXE 0x42a830, each called from the engine's
	// keydown at 0x412154 (`push esi; call`), after the game-mode gate and before the
	// engine's own "hidden test" matcher (0x411f50) and the flipper/plunger dispatch.
	// Two jobs: the flipper fast-forward of the end-of-ball bonus count, and the
	// table's own typed cheat codes.
	//
	// `key` is what the originals were handed, a Windows virtual-key code: for letters
	// that is the upper-case character, for digits the digit. pb.cpp upper-cases SDL's
	// lower-case letter keycodes to match and passes every other keycode through,
	// which only matters as "not one of the cheat letters". A non-keyboard input bound
	// to a flipper arrives as key 0. `flipper` is the originals' compare of the key
	// against options' left/right flipper keys (DRAGON 0x4282d9/0x4282e1 against
	// 0x434f74/0x434f78, the same cells keydown tests at 0x412183/0x4121a5), done here
	// through the engine's bindings so a remapped or gamepad flipper counts too.
	//
	// Null for Space Cadet, whose backdoor stays exactly as it was.
	void (*KeyHandler)(int key, bool flipper);

	// How long the "Player N" / demo "Player N" text stays up, the time each table's
	// text-event function passes to TTextBox::Display for codes 0x60..0x67:
	// CADET.EXE 0x42a750 6.0 s (`push 0x40c00000`, e.g. 0x42a8b9), PIRATES.EXE
	// 0x422b90 1.0 s (`push 0x3f800000`, e.g. 0x422cf0), DRAGON.EXE 0x422b80 untimed
	// (`push 0xbf800000`, e.g. 0x422ce0). 0 means untimed (-1), which is also
	// 3D Pinball's, so rows that leave it out keep upstream's behaviour.
	float PlayerTextTime;
};

// The table in play. Replaces pb::FullTiltMode / pb::FullTiltDemoMode, which
// cannot express more than two tables between them.
extern const TableRules* CurrentTable;

const TableRules* FindTableRules(TableId id);

// The table's own text for a resource id, or null when it has none -- including
// every Space Cadet id, which belongs to the shared `Msg` enum instead.
// pb::get_rc_string consults this first and falls through to translations.cpp.
const char* FindRcString(const TableRules* rules, int id);
const TableRules* FindTableRulesForDat(const char* datFileName);

// The DAT names pb::SelectDatFile probes, in registry order. Space Cadet's three
// come first, so a directory holding several DATs still picks Space Cadet.
const char* const* TableDatSearchOrder(size_t& count);

// Message codes the Full Tilt binaries use and 3D Pinball does not, so upstream's
// enum has no name for them. Numbering here follows upstream's own scheme; these
// are new entries, not the originals' values. (The originals renumber everything
// piecewise relative to upstream -- TLight +0, TLightGroup/TSoloTarget/TBlocker +4,
// gate/kickout/sink +9, Control* +10, ControlCollision +11 -- but that only matters
// when reading the 1996 binaries, never at runtime here.)
enum class FtMessageCode
{
	QuestTeardown,   // original 0x51
	QuestAbort,      // original 0x52
	QuestRestart,    // original 0x53
	LastScorerChanged, // original 0x4d; no component consumes it, kept for fidelity
};
