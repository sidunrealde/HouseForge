// Copyright Siddartha G. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Internationalization/Text.h"
#include "Templates/Function.h"

class SWidget;

/**
 * ONE COLLAPSIBLE SECTION OF THE HOUSEFORGE PANEL.
 *
 * The panel is a flat vertical stack of these over the editor's own selection - no workflow rail
 * and no tree. SHFHousePanel::Construct builds from a TArray of them and knows nothing about any
 * particular one, so a later milestone adds a section by appending an entry rather than by editing
 * the tab.
 *
 * This is the seam Docs/PanelAndBakeDesign.md reserved, and it is deliberately the whole
 * reservation: ASSETS and LIGHT get an entry when the code behind them exists in Source/, and not
 * before. A greyed-out row promising a feature nobody has written is fake UI.
 */
struct FHFPanelSection
{
	/** Stable id, from HFPanelSectionIds. What a test names a section by. */
	FName Id;

	/** Section header, in the panel's own vocabulary. */
	FText Title;

	/**
	 * Whether the section belongs in the stack at all right now.
	 *
	 * PRESENT or ABSENT, never disabled-in-place: a section that cannot do anything should not be
	 * taking up a header's worth of a 400 px dock. Sections that stay usable with an empty level -
	 * SURFACES is one, because finishes are assets rather than level state - simply always return
	 * true and say inside themselves what the level does and does not contain.
	 */
	TFunction<bool()> IsRelevant;

	/** Builds the section's body. Called once, when the panel is constructed. */
	TFunction<TSharedRef<SWidget>()> Build;

	/** Whether the section starts open. */
	bool bExpandedByDefault = true;
};
