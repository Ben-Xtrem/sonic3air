/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Copyright (C) 2017-2022 by Eukaryot
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#include "oxygen/pch.h"
#include "oxygen/rendering/parts/PaletteManager.h"
#include "oxygen/simulation/EmulatorInterface.h"


Color PaletteManager::unpackColor(uint16 packedColor)
{
	// Differentiate between:
	//  - Original hardware's packed colors (9-bit): 0000 BBB0 GGG0 RRR0
	//  - Extended packed colors (15-bit):           1BBB BBGG GGGR RRRR

	// Note that extended packed colors can be matched to the original packed colors when not using the lowermost 2 bits of each channel
	//  -> That also means when using these bits as well, they can even go higher than pure white at 0xff, but they will get clamped at that point

	uint32 color = 0xff000000;
	uint8& r = ((uint8*)&color)[0];
	uint8& g = ((uint8*)&color)[1];
	uint8& b = ((uint8*)&color)[2];
	if (packedColor & 0x8000)
	{
		r = std::min(((packedColor) & 0x1f) * 0x09, 0xff);
		g = std::min(((packedColor >> 5) & 0x1f) * 0x09, 0xff);
		b = std::min(((packedColor >> 10) & 0x1f) * 0x09, 0xff);
	}
	else
	{
		r = ((packedColor >> 1) & 0x07) * 0x24;
		g = ((packedColor >> 5) & 0x07) * 0x24;
		b = ((packedColor >> 9) & 0x07) * 0x24;
	}
	return Color::fromABGR32(color);
}

void PaletteManager::preFrameUpdate()
{
	mSplitPositionY = 0xff;
	mUsesGlobalComponentTint = false;
	mGlobalComponentTintColor = Color::WHITE;
	mGlobalComponentAddedColor = Color::TRANSPARENT;
}

const uint32* PaletteManager::getPalette(int paletteIndex) const
{
	RMX_ASSERT(paletteIndex < 2, "Invalid palette index " << paletteIndex);
	return mPalette[paletteIndex];
}

void PaletteManager::getPalette(Color* palette, int paletteIndex) const
{
	for (int i = 0; i < 0x100; ++i)
	{
		palette[i] = getPaletteEntry(paletteIndex, i);
	}
}

Color PaletteManager::getPaletteEntry(int paletteIndex, uint8 colorIndex) const
{
	RMX_ASSERT(paletteIndex < 2, "Invalid palette index " << paletteIndex);
	return Color::fromABGR32(mPalette[paletteIndex][colorIndex]);
}

uint16 PaletteManager::getPaletteEntryPacked(int paletteIndex, uint8 colorIndex, bool allowExtendedPacked) const
{
	const Color color = getPaletteEntry(paletteIndex, colorIndex);
	if (allowExtendedPacked)	// For notes on extended packed color format, see "writePaletteEntryPacked"
	{
		const uint32 r = ((roundToInt(saturate(color.r) * 255.0f) + 0x04) / 0x09);
		const uint32 g = ((roundToInt(saturate(color.g) * 255.0f) + 0x04) / 0x09);
		const uint32 b = ((roundToInt(saturate(color.b) * 255.0f) + 0x04) / 0x09);
		return (r) + (g << 5) + (b << 10) + 0x8000;
	}
	else
	{
		const uint32 r = ((roundToInt(saturate(color.r) * 255.0f) + 0x12) / 0x24);
		const uint32 g = ((roundToInt(saturate(color.g) * 255.0f) + 0x12) / 0x24);
		const uint32 b = ((roundToInt(saturate(color.b) * 255.0f) + 0x12) / 0x24);
		return (r << 1) + (g << 5) + (b << 9);
	}
}

void PaletteManager::writePaletteEntry(int paletteIndex, uint8 colorIndex, uint32 color)
{
	if (paletteIndex == 0)
	{
		mPalette[0][colorIndex] = color;
		mPackedPaletteCache[0][colorIndex].mIsValid = false;
	}

	// Secondary palette gets written in both cases
	{
		mPalette[1][colorIndex] = color;
		mPackedPaletteCache[1][colorIndex].mIsValid = false;
	}
}

void PaletteManager::writePaletteEntryPacked(int paletteIndex, uint8 colorIndex, uint16 packedColor)
{
	// Differentiate between:
	//  - Original hardware's packed colors (9-bit): 0000 BBB0 GGG0 RRR0
	//  - Extended packed colors (15-bit):           1BBB BBGG GGGR RRRR

	// Note that extended packed colors can be matched to the original packed colors when not using the lowermost 2 bits of each channel
	//  -> That also means when using these bits as well, they can even go higher than pure white at 0xff, but they will get clamped at that point

	PackedPaletteColor& cache = mPackedPaletteCache[paletteIndex][colorIndex];
	if (cache.mPackedColor == packedColor && cache.mIsValid)
	{
		// Nothing to do... well except if this is the primary palette, then we should still make sure the secondary palette has the same color
		if (paletteIndex == 0 && mPalette[1][colorIndex] != mPalette[0][colorIndex])
		{
			mPalette[1][colorIndex] = mPalette[0][colorIndex];
			mPackedPaletteCache[1][colorIndex].mPackedColor = mPackedPaletteCache[0][colorIndex].mPackedColor;
			mPackedPaletteCache[1][colorIndex].mIsValid = true;
		}
		return;
	}

	unsigned int color = (colorIndex & 0x0f) ? 0xff000000 : 0;
	unsigned int packed = (unsigned int)packedColor;
	if (packed & 0x8000)
	{
		static const unsigned int CONV[0x20] = { 0, 9, 18, 27, 36, 45, 54, 63, 72, 81, 90, 99, 108, 117, 126, 135, 144, 153, 162, 171, 180, 189, 198, 207, 216, 225, 234, 243, 252, 255, 255, 255 };
		color = color | (CONV[((packed))       & 0x1f])
					  | (CONV[((packed) >> 5)  & 0x1f] << 8)
					  | (CONV[((packed) >> 10) & 0x1f] << 16);
	}
	else
	{
		static const unsigned int CONV[8] = { 0, 36, 72, 108, 144, 180, 216, 252 };
		color = color | (CONV[(packed >> 1) & 0x07])
					  | (CONV[(packed >> 5) & 0x07] << 8)
					  | (CONV[(packed >> 9) & 0x07] << 16);
	}

	if (paletteIndex == 0)
	{
		mPalette[0][colorIndex] = color;
		cache.mPackedColor = packedColor;
		cache.mIsValid = true;
	}

	// Secondary palette gets written in both cases
	{
		mPalette[1][colorIndex] = color;
		mPackedPaletteCache[1][colorIndex].mPackedColor = packedColor;
		mPackedPaletteCache[1][colorIndex].mIsValid = true;
	}
}

void PaletteManager::setPaletteSplitPositionY(uint8 py)
{
	mSplitPositionY = py;
}

void PaletteManager::setGlobalComponentTint(const Vec4f& tintColor, const Vec4f& addedColor)
{
	mUsesGlobalComponentTint = true;
	mGlobalComponentTintColor = tintColor;
	mGlobalComponentAddedColor = addedColor;
}
