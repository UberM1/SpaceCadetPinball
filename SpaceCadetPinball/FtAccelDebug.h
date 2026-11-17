// ---- FTDBG: temporary accelerator/ramp-exit harness (remove before finishing) ----
#pragma once
#include "maths.h"
struct FtAccelDbgEntry
{
	const char* Name;
	void* Comp;
	vector2 Center;
	float RadiusSq;
	int Group;
	vector3 Dir;
	RectF AABB;
};
struct FtRexitDbgEntry
{
	const char* Name;
	void* Comp;
	int Group1, Group2;
	vector2 Start, End;
	bool Built;
};
void ft_hole_register(const char* name, vector2 center, float radiusSq, int group, int releaseMask, float z);
// FT_KOUTTEST=<n> drives the ball into kickout n; the registry is filled by TKickout's
// constructor in table order, so the index is the construction order printed by
// "DBG-kout  reg".
void ft_kickout_register(const char* name, void* comp, vector2 center, float radius, int group,
                         float timerTime1, float timerTime2, float setZ, float fieldMult,
                         int notSomeFlag, int activeFlag, int zAttrPresent, float radiusScale,
                         int radiusScalePresent);
void ft_sink_register(const char* name, void* comp, RectF aabb, int group, float timerTime,
                      vector2 ballPos, int s3, int s4);
void ft_accel_register(const FtAccelDbgEntry& e);
void ft_rexit_register(const FtRexitDbgEntry& e);
void ft_accel_tick(float dtSec);
// FT_TABLETEST=tilt|players, see the end of FtAccelDebug.cpp.
void ft_tabletest_tick(float dtSec);
bool ft_accel_enabled();
bool ft_autoplay_enabled();
// FT_AUTODRAIN and FT_BLOCKDBG, read once like the two above.
bool ft_autodrain_enabled();
bool ft_blockdbg_enabled();
// Empties every *_register list; TPinballTable's constructor calls it.
void ft_debug_clear_registries();
void ft_accel_note_hit(const char* name);
void ft_rexit_note_hit(const char* name, int maskIn, int maskOut);
