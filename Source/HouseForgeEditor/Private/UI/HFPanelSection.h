// Copyright Siddartha G. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Internationalization/Text.h"
#include "Templates/Function.h"

class SWidget;

/**
 * ONE PAGE OF THE HOUSEFORGE PANEL.
 *
 * The panel is a tab strip over one page at a time. SHFHousePanel::Construct builds from a TArray
 * of these and knows nothing about any particular one, so a later milestone adds a page by
 * appending an entry rather than by editing the tab.
 *
 * IT WAS A VERTICAL STACK OF COLLAPSIBLE SECTIONS, and it stopped scaling at three. Every section
 * expanded by default, because collapsing one hides the control an artist came for, so a 400 px
 * dock had to carry a connection status, a drop target, a set list, a trace box and a details view
 * at once - and the details view, the one thing that genuinely needs height, got what was left.
 * Sections are pages now: the active one gets the whole panel.
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
	 * Whether the section belongs in the panel at all right now.
	 *
	 * PRESENT or ABSENT, never disabled-in-place: a section that cannot do anything should not be
	 * taking up a tab. Sections that stay usable with an empty level -
	 * SURFACES is one, because finishes are assets rather than level state - simply always return
	 * true and say inside themselves what the level does and does not contain.
	 */
	TFunction<bool()> IsRelevant;

	/** Builds the section's body. Called once, when the panel is constructed. */
	TFunction<TSharedRef<SWidget>()> Build;
};
