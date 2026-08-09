// Copyright Siddartha G. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UI/HFPanelSection.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

/**
 * THE HOUSEFORGE PANEL. The plugin's first user-facing UI.
 *
 * Everything up to now has been driven over MCP or through the details panel, which means every
 * capability the plugin has is reachable only by asking Claude for it or by knowing which actor to
 * select first. This is the tab that changes that, and its shape is the artist-station from
 * Docs/PanelAndBakeDesign.md: a flat vertical stack of collapsible sections over the editor's own
 * selection.
 *
 * The panel holds no logic. Each section is a view onto UHFEditorSubsystem, which is also what the
 * MCP toolset wraps - so a thing done in the panel and the same thing done by Claude are the same
 * code, and the two surfaces cannot drift apart.
 *
 * One section exists today, SURFACES, because the material library is what exists in Source/. The
 * FHFPanelSection array is how the rest arrive.
 */
class SHFHousePanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SHFHousePanel) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/**
	 * The sections the panel is built from, in the order they stack.
	 *
	 * Static and free of any widget, so a test can ask what the panel is made of without a tab, a
	 * window, or a Slate application. Widget rendering is not worth testing; what the panel is
	 * composed of is.
	 */
	static TArray<FHFPanelSection> BuildSections();
};
