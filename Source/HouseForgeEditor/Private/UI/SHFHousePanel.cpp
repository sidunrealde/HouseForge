// Copyright Siddartha G. All Rights Reserved.

#include "UI/SHFHousePanel.h"

#include "UI/HFPanelIds.h"
#include "UI/SHFMaterialPanel.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "HouseForgePanel"

TArray<FHFPanelSection> SHFHousePanel::BuildSections()
{
	TArray<FHFPanelSection> Sections;

	FHFPanelSection& Surfaces = Sections.AddDefaulted_GetRef();
	Surfaces.Id = HFPanelSectionIds::Surfaces();
	Surfaces.Title = LOCTEXT("SurfacesSection", "SURFACES");
	Surfaces.bExpandedByDefault = true;

	// It holds a details view, so it has to be given the tab's remaining height - see
	// FHFPanelSection::bFillsRemainingSpace for what happens otherwise.
	Surfaces.bFillsRemainingSpace = true;

	// Always present, with or without a house. Finishes are assets rather than level state, so an
	// empty level does not make this section inert - it makes the usage figures inside it read
	// "not in this level", which is a fact worth showing rather than a reason to hide the controls.
	Surfaces.IsRelevant = []() { return true; };
	Surfaces.Build = []() -> TSharedRef<SWidget> { return SNew(SHFMaterialPanel); };

	return Sections;
}

void SHFHousePanel::Construct(const FArguments& InArgs)
{
	// A vertical box rather than a scroll box. A scroll box hands its content unbounded height,
	// which silently defeats FillHeight - and a section holding a details view has to be given fill
	// height all the way down from the tab or the property list collapses to a few rows. Sections
	// that scroll do it inside themselves.
	TSharedRef<SVerticalBox> Stack = SNew(SVerticalBox);

	int32 Shown = 0;
	for (FHFPanelSection& Section : BuildSections())
	{
		if (Section.IsRelevant && !Section.IsRelevant())
		{
			continue;
		}
		if (!Section.Build)
		{
			continue;
		}

		++Shown;

		// SExpandableArea already slots its body with FillHeight (SExpandableArea.cpp:71), so the
		// only link in the chain this has to supply is the outer slot.
		const TSharedRef<SWidget> Area =
			SNew(SExpandableArea)
			.InitiallyCollapsed(!Section.bExpandedByDefault)
			.AreaTitle(Section.Title)
			.BodyContent()
			[
				Section.Build()
			];

		if (Section.bFillsRemainingSpace)
		{
			Stack->AddSlot().FillHeight(1.0f).Padding(4.0f, 2.0f)[Area];
		}
		else
		{
			Stack->AddSlot().AutoHeight().Padding(4.0f, 2.0f)[Area];
		}
	}

	if (Shown == 0)
	{
		// Said out loud rather than left blank. An empty panel reads as a panel that failed to
		// load, and the reason it is empty here is a fact about the plugin rather than about the
		// level - so it names the thing that is missing instead of suggesting something to try.
		Stack->AddSlot()
			.AutoHeight()
			.Padding(12.0f)
			[
				SNew(STextBlock)
				.AutoWrapText(true)
				.Text(LOCTEXT("NoSections",
					"HouseForge has no panel sections built yet. Everything the plugin does is "
					"reachable over MCP and from the details panel of a selected element."))
			];
	}

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.Padding(0.0f)
		[
			Stack
		]
	];
}

#undef LOCTEXT_NAMESPACE
