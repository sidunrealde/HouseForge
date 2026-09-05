// Copyright Siddartha G. All Rights Reserved.

#include "UI/SHFHousePanel.h"

#include "UI/HFPanelIds.h"
#include "UI/SHFClaudePanel.h"
#include "UI/SHFDrawingsPanel.h"
#include "UI/SHFMaterialPanel.h"
#include "Widgets/Input/SSegmentedControl.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "HouseForgePanel"

TArray<FHFPanelSection> SHFHousePanel::BuildSections()
{
	TArray<FHFPanelSection> Sections;

	// NO CLAUDE SECTION. It is the header instead - see the note in Construct. Generate lives in
	// DRAWINGS and is disabled without a connection, so the reason it is disabled has to be legible
	// from DRAWINGS. On a tab of its own it would not be.

	// Drawings first, because that is where a house comes from, and it is the tab an artist opens
	// the panel to reach.
	FHFPanelSection& Drawings = Sections.AddDefaulted_GetRef();
	Drawings.Id = HFPanelSectionIds::Drawings();
	Drawings.Title = LOCTEXT("DrawingsSection", "DRAWINGS");

	// Always present. Drawings are files on disk rather than level state, so an empty level does
	// not make this inert - and this is the section an artist uses BEFORE there is a level at all.
	Drawings.IsRelevant = []() { return true; };
	Drawings.Build = []() -> TSharedRef<SWidget> { return SNew(SHFDrawingsPanel); };

	FHFPanelSection& Surfaces = Sections.AddDefaulted_GetRef();
	Surfaces.Id = HFPanelSectionIds::Surfaces();
	Surfaces.Title = LOCTEXT("SurfacesSection", "SURFACES");

	// Always present, with or without a house. Finishes are assets rather than level state, so an
	// empty level does not make this section inert - it makes the usage figures inside it read
	// "not in this level", which is a fact worth showing rather than a reason to hide the controls.
	Surfaces.IsRelevant = []() { return true; };
	Surfaces.Build = []() -> TSharedRef<SWidget> { return SNew(SHFMaterialPanel); };

	return Sections;
}

FName SHFHousePanel::ActiveSection() const
{
	return Active;
}

void SHFHousePanel::SetActiveSection(FName Id)
{
	Active = Id;
}

int32 SHFHousePanel::ActiveIndex() const
{
	const int32 Index = SectionOrder.IndexOfByKey(Active);
	return Index == INDEX_NONE ? 0 : Index;
}

void SHFHousePanel::Construct(const FArguments& InArgs)
{
	const TSharedRef<SSegmentedControl<FName>> Tabs =
		SNew(SSegmentedControl<FName>)
		.Value(this, &SHFHousePanel::ActiveSection)
		.OnValueChanged(this, &SHFHousePanel::SetActiveSection);

	SAssignNew(Switcher, SWidgetSwitcher)
		.WidgetIndex(this, &SHFHousePanel::ActiveIndex);

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

		// Slot order and SectionOrder order are the same list, which is what lets ActiveIndex map
		// an id to a page without the switcher knowing anything about ids.
		Tabs->AddSlot(Section.Id, false).Text(Section.Title);
		Switcher->AddSlot()[Section.Build()];
		SectionOrder.Add(Section.Id);
	}

	// Deferred until every slot is in, as the engine's own SSegmentedControl factory does - each
	// AddSlot would otherwise rebuild the whole strip.
	Tabs->RebuildChildren();

	if (SectionOrder.Num() == 0)
	{
		// Said out loud rather than left blank. An empty panel reads as a panel that failed to
		// load, and the reason it is empty here is a fact about the plugin rather than about the
		// level - so it names the thing that is missing instead of suggesting something to try.
		ChildSlot
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
			[
				SNew(STextBlock)
				.AutoWrapText(true)
				.Text(LOCTEXT("NoSections",
					"HouseForge has no panel sections built yet. Everything the plugin does is "
					"reachable over MCP and from the details panel of a selected element."))
			]
		];
		return;
	}

	Active = SectionOrder[0];

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.Padding(0.0f)
		[
			SNew(SVerticalBox)

			// THE CONNECTION STATUS, ABOVE THE TABS AND ON EVERY PAGE.
			//
			// Not a tab, deliberately. Generate lives in DRAWINGS and is disabled when Claude
			// cannot be reached; put the status on a tab of its own and an artist looking at a
			// greyed-out Generate has no way to see why without leaving the page that shows it.
			// A disabled control whose reason is one click away is a control that reads as broken.
			//
			// It costs one line when everything is working - SHFClaudePanel collapses its own
			// detail text once connected - and grows only while there is something to say.
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SHFClaudePanel)
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SSeparator)
				.Thickness(1.0f)
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Center)
			.Padding(4.0f, 6.0f)
			[
				Tabs
			]

			// FILL, and this is the link that used to be missing.
			//
			// A details view puts its tree in a FillHeight slot (SDetailsView.cpp:437), so in an
			// auto-height parent it collapses to the tree's desired size - a few rows, not the
			// property list. Every widget between the tab and such a view has to pass fill height
			// down. In the old stack that had to be opted into per section, and a section that took
			// the remaining height starved the ones below it; here the active page is the only page,
			// so it simply gets all of it.
			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			.Padding(4.0f, 2.0f)
			[
				Switcher.ToSharedRef()
			]
		]
	];
}

#undef LOCTEXT_NAMESPACE
