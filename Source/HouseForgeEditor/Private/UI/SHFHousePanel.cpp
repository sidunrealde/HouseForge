// Copyright Siddartha G. All Rights Reserved.

#include "UI/SHFHousePanel.h"

#include "UI/HFPanelIds.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "HouseForgePanel"

TArray<FHFPanelSection> SHFHousePanel::BuildSections()
{
	TArray<FHFPanelSection> Sections;
	return Sections;
}

void SHFHousePanel::Construct(const FArguments& InArgs)
{
	TSharedRef<SScrollBox> Stack = SNew(SScrollBox);

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
		Stack->AddSlot()
			.Padding(4.0f, 2.0f)
			[
				SNew(SExpandableArea)
				.InitiallyCollapsed(!Section.bExpandedByDefault)
				.AreaTitle(Section.Title)
				.BodyContent()
				[
					Section.Build()
				]
			];
	}

	if (Shown == 0)
	{
		// Said out loud rather than left blank. An empty panel reads as a panel that failed to
		// load, and the reason it is empty here is a fact about the plugin rather than about the
		// level - so it names the thing that is missing instead of suggesting something to try.
		Stack->AddSlot()
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
