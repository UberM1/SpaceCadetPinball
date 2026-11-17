#pragma once
#include "TPinballComponent.h"

class TLight;
class TLightGroup;

// Object type 1038 -- Full Tilt's `light_sweeps`, recovered from CADET.EXE.
//
// The class is engine code that all three executables share: CADET.EXE's own
// TPinballTable constructor spawns it for type 0x40e, which is why the assertion
// string "sweep == NULL" is in the Space Cadet binary too even though Space Cadet's
// table data never defines one. Only one may exist per table, hence that assertion.
//
// It is a light show, not a playfield object: it owns no geometry, its port_draw is
// empty, and its only output is TLightFtTmpOverrideOn sent to other components' lights.
// The constructor bins every light in the table's LightGroup into a fixed 15x25 grid
// over the playfield; a sweep then walks a growing or shrinking circle of grid cells
// outward from, or inward toward, a start point and flashes whatever lights it crosses.
//
// `Message(TLightSweepStart, n)` picks sweep n out of the group's own attributes:
//
//   1600  start point (x, y) in playfield coordinates
//   1601  direction: 0 inward (radius counts down), non-zero outward
//   1602  seconds per step            (default 0.1)
//   1603  repeat count                (default 1)
//   1604  how long each light stays overridden on (default: the step time)
//
// When the last repeat finishes it raises ControlTimerExpired, which is how the rules
// learn a sweep is over -- every Dragon's Keep quest distinguishes that from its
// mode_timer expiring by the caller's component type.
class TLightSweep : public TPinballComponent
{
public:
	TLightSweep(TPinballTable* table, TLightGroup* lightGroup, int groupIndex);
	~TLightSweep() override = default;
	int Message(MessageCode code, float value) override;
	void port_draw() override
	{
	}

	virtual void Reset();

private:
	void Step();
	void PlotCircle(int centerX, int centerY, int radius);
	void PlotCell(int cellX, int cellY);
	static void StepTimerExpired(int timerId, void* caller);

	// The grid is fixed at 15 wide by 25 tall in the original; the cell size follows
	// from the table layer's extent, so the sweep is resolution-independent.
	static constexpr int GridWidth = 15;
	static constexpr int GridHeight = 25;

	// Which lights fall in each grid cell. One pass at construction; a light never
	// moves, so this never has to be rebuilt.
	std::vector<TLight*> Cells[GridWidth * GridHeight];

	TLightGroup* LightGroup;
	// The attribute group the sweep definitions are read from. The original keeps its
	// own copy rather than using the base GroupIndex, and a zero one disables the
	// component outright.
	int SweepGroupIndex;

	float OriginX{}, OriginY{};
	float InvCellWidth{}, InvCellHeight{};

	float StepTime{};
	float OverrideTime{};
	int CursorX{}, CursorY{};
	int Outward{};
	int RepeatsLeft{};
	int Radius{};
	int MaxRadius{};
	int Timer{};
};
