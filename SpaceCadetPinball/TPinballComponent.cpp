#include "pch.h"
#include "TPinballComponent.h"

#include "control.h"
#include "loader.h"
#include "pb.h"
#include "proj.h"
#include "render.h"
#include "TPinballTable.h"
#include "TTableLayer.h"

TPinballComponent::TPinballComponent(TPinballTable* table, int groupIndex, bool loadVisuals)
{
	visualStruct visual{};

	MessageField = 0;
	UnusedBaseFlag = 0;
	ActiveFlag = 0;
	PinballTable = table;
	RenderSprite = nullptr;
	ListBitmap = nullptr;
	GroupName = nullptr;
	Control = nullptr;
	VisualPosNormX= -1.0f;
	VisualPosNormY = -1.0f;
	GroupIndex = groupIndex;
	if (table)
		table->ComponentList.push_back(this);
	if (groupIndex >= 0)
		GroupName = loader::query_name(groupIndex);

	if (loadVisuals && groupIndex >= 0)
	{
		int visualCount = loader::query_visual_states(groupIndex);
		for (int index = 0; index < visualCount; ++index)
		{
			loader::query_visual(groupIndex, index, &visual);
			if (visual.Bitmap.Bmp)
			{
				assertm(visual.Bitmap.ZMap, "Bitmap/zMap pairing is mandatory");
				if (!ListBitmap)
					ListBitmap = new std::vector<SpriteData>();
				ListBitmap->push_back(visual.Bitmap);
			}
		}

		if (ListBitmap)
		{
			rectangle_type bmp1Rect{}, tmpRect{};
			const auto rootSprite = ListBitmap->at(0);
			const auto rootBmp = rootSprite.Bmp;

			bmp1Rect.XPosition = rootBmp->XPosition - table->XOffset;
			bmp1Rect.YPosition = rootBmp->YPosition - table->YOffset;
			bmp1Rect.Width = rootBmp->Width;
			bmp1Rect.Height = rootBmp->Height;

			for (auto index = 1u; index < ListBitmap->size(); index++)
			{
				auto bmp = ListBitmap->at(index).Bmp;
				tmpRect.XPosition = bmp->XPosition - table->XOffset;
				tmpRect.YPosition = bmp->YPosition - table->YOffset;
				tmpRect.Width = bmp->Width;
				tmpRect.Height = bmp->Height;
				maths::enclosing_box(bmp1Rect, tmpRect, bmp1Rect);
			}

			RenderSprite = new render_sprite(
				VisualTypes::Sprite,
				rootBmp,
				rootSprite.ZMap,
				rootBmp->XPosition - table->XOffset,
				rootBmp->YPosition - table->YOffset,
				&bmp1Rect);

			// Sound position = center of root visual, reverse-projected, normalized.
			auto& rect = RenderSprite->BmpRect;
			vector2i pos2D{ rect.XPosition + rect.Width / 2, rect.YPosition + rect.Height / 2 };
			auto pos3D = proj::ReverseXForm(pos2D);
			auto posNorm = TTableLayer::edge_manager->NormalizeBox(pos3D);
			VisualPosNormX = posNorm.X;
			VisualPosNormY = posNorm.Y;
		}
	}
}


TPinballComponent::~TPinballComponent()
{
	if (PinballTable)
	{
		// ComponentList contains one reference to each component.
		auto& components = PinballTable->ComponentList;
		auto position = std::find(components.begin(), components.end(), this);
		if (position != components.end())
			components.erase(position);
	}

	delete ListBitmap;
}


int TPinballComponent::Message(MessageCode code, float value)
{
	// Full Tilt's base Message, 0x4135b0, is two tests and nothing else:
	//   4135b8 cmp esi,0x402 / 4135c2 call 0x413550
	//   4135c7 cmp esi,0x3fe / 4135d6 call 0x413570
	// It never writes the message code into MessageField the way upstream's does
	// below, so the bonus tables' components that inherit it -- TOneway, TRamp, TSound,
	// TRampExit, TAccelerator (vtables 0x4374c0, 0x437680, 0x437758, 0x4377e0,
	// 0x437800) -- keep whatever the rules put there.
	if (pb::FullTiltMode)
	{
		if (code == MessageCode::Reset)
			FtPlayerFieldsReset();
		if (code == MessageCode::PlayerChanged)
			FtPlayerFieldsSwap(value);
		return 0;
	}

	MessageField = static_cast<int>(code);
	if (code == MessageCode::Reset)
		MessageField = 0;
	return 0;
}

void TPinballComponent::FtPlayerFieldsReset()
{
	// 0x413550: xor eax,eax, then eax into +0x6 (MessageField) and +0x1a/+0x1e/+0x22/+0x26.
	if (!pb::FullTiltMode)
		return;
	MessageField = 0;
	for (auto& field : FtPlayerMessageField)
		field = 0;
}

void TPinballComponent::FtPlayerFieldsSwap(float nextPlayer)
{
	// 0x413570:
	//   41357d mov edx,[[this+0x36]+0x106]     CurrentPlayer, read before the table
	//                                          switches -- TPinballTable updates it
	//                                          only after messaging every component
	//   413589 mov [esi+edx*4+0x1a],ecx        copies[CurrentPlayer] = MessageField
	//   41358d call 0x42c7ef / 413595 0x42c7c8 floor, then ftol, of the new player
	//   41359e mov [esi+0x6],ecx               MessageField = copies[new player]
	// The clamps are the port's: the original trusts both indices.
	if (!pb::FullTiltMode)
		return;
	auto current = PinballTable ? PinballTable->CurrentPlayer : 0;
	if (current >= 0 && current < 4)
		FtPlayerMessageField[current] = MessageField;
	auto next = static_cast<int>(floor(nextPlayer));
	if (next >= 0 && next < 4)
		MessageField = FtPlayerMessageField[next];
}

void TPinballComponent::port_draw()
{
}

int TPinballComponent::get_scoring(unsigned int index) const
{
	return Control == nullptr || index >= Control->ScoreCount ? 0 : Control->Scores[index];
}

vector2 TPinballComponent::get_coordinates()
{
	return {VisualPosNormX, VisualPosNormY};
}

void TPinballComponent::SpriteSet(int index) const
{
	if (!ListBitmap)
		return;

	int xPos, yPos;
	gdrv_bitmap8* bmp;
	zmap_header_type* zMap;
	// Full Tilt's bonus tables have components with fewer visual states than Space
	// Cadet's rules index -- spot targets with a single bitmap, blockers with none.
	// at() would throw; an out-of-range state simply has no bitmap to show.
	if (index >= static_cast<int>(ListBitmap->size()))
		return;

	if (index >= 0)
	{
		auto& spriteData = ListBitmap->at(index);
		bmp = spriteData.Bmp;
		zMap = spriteData.ZMap;
		xPos = bmp->XPosition - PinballTable->XOffset;
		yPos = bmp->YPosition - PinballTable->YOffset;
	}
	else
	{
		bmp = nullptr;
		zMap = nullptr;
		xPos = RenderSprite->BmpRect.XPosition;
		yPos = RenderSprite->BmpRect.YPosition;
	}

	RenderSprite->set(bmp, zMap, xPos, yPos);
}

void TPinballComponent::SpriteSetBall(int index, vector2i pos, float depth) const
{
	if (ListBitmap)
	{
		gdrv_bitmap8* bmp = nullptr;
		if (index >= 0)
		{
			bmp = ListBitmap->at(index).Bmp;
			pos.X -= bmp->Width / 2;
			pos.Y -= bmp->Height / 2;
		}
		
		RenderSprite->ball_set(bmp, depth, pos.X, pos.Y);
	}
}
