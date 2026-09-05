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
 * select first. This is the tab that changes that.
 *
 * The panel holds no logic. Each section is a view onto UHFEditorSubsystem, which is also what the
 * MCP toolset wraps - so a thing done in the panel and the same thing done by Claude are the same
 * code, and the two surfaces cannot drift apart.
 *
 *
 * A STATUS HEADER, A TAB STRIP, AND ONE PAGE
 * -------------------------------------------
 * It was a vertical stack of collapsible sections, which is what Docs/PanelAndBakeDesign.md
 * reserved, and it stopped scaling at three of them. Every section wants to be expanded - collapse
 * one and you hide the control the artist came for - so a 400 px dock ended up carrying a
 * connection status, a drop target, a set list, a live trace box and a property tree at once. The
 * property tree, the one thing that genuinely needs height, got whatever was left.
 *
 * So sections are pages now, one visible at a time, and the active page gets the whole panel.
 *
 * CLAUDE IS THE EXCEPTION and is not a page. Generate lives in DRAWINGS and is disabled when
 * Claude cannot be reached, so the reason it is disabled has to be readable from DRAWINGS; on a
 * tab of its own, a greyed-out Generate would have its explanation one click away, which is
 * indistinguishable from a broken button. It sits above the tab strip and is on every page. That
 * costs one line while everything works, because it collapses its own detail once connected.
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

private:
	/** The page showing now. Drives both the tab strip's selection and the switcher's index. */
	FName ActiveSection() const;
	void SetActiveSection(FName Id);

	/** Where that page sits in the switcher. Falls back to the first page rather than to none. */
	int32 ActiveIndex() const;

	FName Active;

	/** Page ids in slot order, so an id maps to a switcher index without the switcher knowing ids. */
	TArray<FName> SectionOrder;

	TSharedPtr<class SWidgetSwitcher> Switcher;
};
