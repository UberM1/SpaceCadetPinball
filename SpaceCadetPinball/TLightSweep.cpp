#include "pch.h"
#include "TLightSweep.h"

#include "control.h"
#include "loader.h"
#include "timer.h"
#include "TLight.h"
#include "TLightGroup.h"
#include "TPinballTable.h"
#include "TTableLayer.h"

TLightSweep::TLightSweep(TPinballTable* table, TLightGroup* lightGroup, int groupIndex)
	: TPinballComponent(table, groupIndex, false)
{
	LightGroup = lightGroup;
	SweepGroupIndex = groupIndex;

	// The grid spans the table layer, which is the playfield's own extent rather than
	// anything derived from the sweep's group -- the sweep has no geometry of its own.
	auto layer = table->TableLayer;
	float width = layer ? layer->XMax - layer->XMin : 0.0f;
	float height = layer ? layer->YMax - layer->YMin : 0.0f;
	OriginX = layer ? layer->XMin : 0.0f;
	OriginY = layer ? layer->YMin : 0.0f;

	float cellWidth = width / GridWidth;
	float cellHeight = height / GridHeight;
	InvCellWidth = cellWidth != 0.0f ? 1.0f / cellWidth : 0.0f;
	InvCellHeight = cellHeight != 0.0f ? 1.0f / cellHeight : 0.0f;

	if (!LightGroup)
		return;

	// Walk the group backwards, as the original does: a cell's light order is the
	// reverse of the group's, which only matters if two lights share a cell and a
	// sweep's overrides overlap.
	for (int i = static_cast<int>(LightGroup->List.size()) - 1; i >= 0; --i)
	{
		auto light = LightGroup->List[i];
		if (!light)
			continue;
		// 501 is the component's playfield position. A light without one bins at the
		// grid origin, which is what the original's null return does.
		auto pos = loader::query_float_attribute_opt(light->GroupIndex, 0, 501);
		if (!pos)
			continue;

		int cellY = static_cast<int>((pos[1] - OriginY) * InvCellHeight);
		cellY = std::max(0, std::min(cellY, GridHeight - 1));
		int cellX = static_cast<int>((pos[0] - OriginX) * InvCellWidth);
		cellX = std::max(0, std::min(cellX, GridWidth - 1));
		Cells[cellY * GridWidth + cellX].push_back(light);
	}
}

void TLightSweep::Reset()
{
	if (Timer)
	{
		timer::kill(Timer);
		Timer = 0;
	}
}

int TLightSweep::Message(MessageCode code, float value)
{
	switch (code)
	{
	case MessageCode::TLightSweepStart:
		{
			if (!SweepGroupIndex)
				break;

			int sweep = static_cast<int>(value);
			auto stepTime = loader::query_float_attribute_opt(SweepGroupIndex, sweep, 1602);
			StepTime = stepTime ? *stepTime : 0.1f;
			auto overrideTime = loader::query_float_attribute_opt(SweepGroupIndex, sweep, 1604);
			OverrideTime = overrideTime ? *overrideTime : StepTime;

			auto start = loader::query_float_attribute_opt(SweepGroupIndex, sweep, 1600);
			if (start)
			{
				CursorX = std::max(0, std::min(static_cast<int>((start[0] - OriginX) * InvCellWidth),
				                               GridWidth - 1));
				CursorY = std::max(0, std::min(static_cast<int>((start[1] - OriginY) * InvCellHeight),
				                               GridHeight - 1));
			}
			else
			{
				CursorX = CursorY = 0;
			}

			auto outward = loader::query_float_attribute_opt(SweepGroupIndex, sweep, 1601);
			Outward = outward ? static_cast<int>(*outward) : 0;
			auto repeats = loader::query_float_attribute_opt(SweepGroupIndex, sweep, 1603);
			RepeatsLeft = repeats ? static_cast<int>(*repeats) : 1;

			// The sweep has to reach the furthest corner of the grid from wherever it
			// starts, so the radius runs to the longest of the four corner distances.
			int right = GridWidth - CursorX - 1;
			int bottom = GridHeight - CursorY - 1;
			int farthestSq = std::max(std::max(CursorX * CursorX + CursorY * CursorY,
			                                   right * right + bottom * bottom),
			                          std::max(right * right + CursorY * CursorY,
			                                   CursorX * CursorX + bottom * bottom));
			MaxRadius = static_cast<int>(sqrt(static_cast<float>(farthestSq)));
			Radius = Outward ? 0 : MaxRadius;
			Step();
		}
		break;
	case MessageCode::PlayerChanged:
		// 0x40c9d0: the virtual Reset (0x40c9d4 call [vtable+0x18]) stops a sweep in
		// flight, then 0x40c9de call 0x413570 swaps the per-player MessageField. Without
		// the stop a sweep started on one player's ball would step on into the next's.
		Reset();
		FtPlayerFieldsSwap(value);
		break;
	case MessageCode::Reset:
		// 0x40c9e5: 0x40c9e7 call 0x413550, then the virtual Reset at 0x40c9f0.
		FtPlayerFieldsReset();
		Reset();
		break;
	case MessageCode::GameOver:
	case MessageCode::SetTiltLock:
		// Both stop a sweep in flight. GameOver is the port's own addition and is
		// harmless; the original's SetTiltLock arm is the virtual Reset alone.
		Reset();
		break;
	default:
		break;
	}

	return 0;
}

void TLightSweep::Step()
{
	bool finished;
	if (Outward)
	{
		++Radius;
		finished = Radius > MaxRadius;
	}
	else
	{
		--Radius;
		finished = Radius < 0;
	}

	if (finished && --RepeatsLeft > 0)
	{
		// Another pass: jump the radius back to the end it started from.
		Radius = Outward ? 0 : MaxRadius;
		finished = false;
	}

	if (finished)
	{
		control::handler(MessageCode::ControlTimerExpired, this);
		return;
	}

	PlotCircle(CursorX, CursorY, Radius);
	Timer = timer::set(StepTime, this, StepTimerExpired);
}

void TLightSweep::StepTimerExpired(int timerId, void* caller)
{
	auto sweep = static_cast<TLightSweep*>(caller);
	sweep->Timer = 0;
	sweep->Step();
}

// Midpoint circle, eight octants per step, exactly as the original walks it: the
// visible shape is a ring of cells expanding or contracting around the cursor.
void TLightSweep::PlotCircle(int centerX, int centerY, int radius)
{
	int x = radius;
	int y = 0;
	int err = radius * radius;
	int limit = (radius - 1) * radius;

	while (x >= y)
	{
		PlotCell(centerX + y, centerY - x);
		PlotCell(centerX - y, centerY - x);
		PlotCell(centerX + y, centerY + x);
		PlotCell(centerX - y, centerY + x);
		PlotCell(centerX + x, centerY - y);
		PlotCell(centerX - x, centerY - y);
		PlotCell(centerX + x, centerY + y);
		PlotCell(centerX - x, centerY + y);

		++y;
		err += 1 - 2 * y;
		if (err <= limit)
		{
			limit -= 2 * x - 2;
			--x;
		}
	}
}

void TLightSweep::PlotCell(int cellX, int cellY)
{
	if (cellX < 0 || cellX >= GridWidth || cellY < 0 || cellY >= GridHeight)
		return;

	auto& cell = Cells[cellY * GridWidth + cellX];
	for (int i = static_cast<int>(cell.size()) - 1; i >= 0; --i)
		cell[i]->Message(MessageCode::TLightFtTmpOverrideOn, OverrideTime);
}
