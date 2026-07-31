// Copyright Siddartha G. All Rights Reserved.

#include "Model/HFSettings.h"

UHFSettings::UHFSettings()
{
	// Project Settings > Plugins > HouseForge.
	//
	// The container is not chosen here - UDeveloperSettings::GetContainerName() derives it from the
	// config name, and `config=HouseForge` is neither EditorSettings nor EditorPerProjectUserSettings,
	// so it resolves to "Project". That is the intent: these figures describe the building and belong
	// to the project, not to whoever happens to be modelling it.
	//
	// No registration call is needed or possible. The settings viewer iterates every UDeveloperSettings
	// CDO and registers what it finds, so declaring the class is the whole of it.
	CategoryName = TEXT("Plugins");
	SectionName = TEXT("HouseForge");
}

FHFBuildDefaults UHFSettings::Resolve() const
{
	FHFBuildDefaults Out;

	Out.Opening.Door = Door;
	Out.Opening.SlidingWindow = SlidingWindow;
	Out.Opening.Ventilator = Ventilator;
	Out.Opening.FixedWindow = FixedWindow;

	FHFJoineryDefaults& J = Out.Joinery;

	J.CarcassBoardThickness = CarcassBoardThickness;
	J.ShutterLeafThickness = ShutterLeafThickness;
	J.DrawerFrontThickness = DrawerFrontThickness;
	J.DrawerBoxSideThickness = DrawerBoxSideThickness;
	J.DrawerBoxBottomThickness = DrawerBoxBottomThickness;
	J.PlinthPanelThickness = PlinthPanelThickness;

	J.ShutterRevealGap = ShutterRevealGap;
	J.DrawerRevealGap = DrawerRevealGap;
	J.ShutterBackClearance = ShutterBackClearance;
	J.DrawerBackClearance = DrawerBackClearance;
	J.ShelfFrontSetback = ShelfFrontSetback;

	J.PlinthHeight = PlinthHeight;
	J.PlinthFrontRecess = PlinthFrontRecess;
	J.PlinthEndRecess = PlinthEndRecess;

	J.CorniceDepth = CorniceDepth;
	J.CorniceHeight = CorniceHeight;
	J.CorniceProjection = CorniceProjection;
	J.CorniceProfileSize = CorniceProfileSize;
	J.CorniceEdgeBevel = CorniceEdgeBevel;

	J.TargetShelfSpacing = TargetShelfSpacing;
	J.MinUsefulCompartment = MinUsefulCompartment;
	J.ShelfThicknessPly = ShelfThicknessPly;
	J.ShelfThicknessGlass = ShelfThicknessGlass;
	J.MaxShelfSpanPly = MaxShelfSpanPly;
	J.MaxShelfSpanGlass = MaxShelfSpanGlass;
	J.HangingRailDiameter = HangingRailDiameter;
	J.HangingRailDrop = HangingRailDrop;
	J.MinHangingClearance = MinHangingClearance;

	J.ShutterModuleWidth = ShutterModuleWidth;
	J.ShutterOpenAngleDegrees = ShutterOpenAngleDegrees;
	J.GlazedShutterStileWidth = GlazedShutterStileWidth;
	J.ShutterGlassThickness = ShutterGlassThickness;

	FHFFanDefaults& F = Out.Fan;

	F.CeilingFanRpm = CeilingFanRpm;
	F.ExhaustFanRpm = ExhaustFanRpm;
	F.CeilingFanBladeCount = CeilingFanBladeCount;
	F.ExhaustFanBladeCount = ExhaustFanBladeCount;
	F.BladePitchDegrees = BladePitchDegrees;
	F.CeilingFanDropLength = CeilingFanDropLength;

	Out.Validation = Validation;

	return Out;
}

FHFBuildDefaults FHFBuildDefaults::FromProjectSettings()
{
	// Defined here rather than in HFBuildDefaults.cpp so that translation unit - and everything that
	// includes only HFBuildDefaults.h - stays free of any reference to the settings class.
	//
	// GetDefault<> can return null before the CDO exists, which is a real state during early module
	// startup and in a bare commandlet. Falling back to a default-constructed value rather than
	// asserting is deliberate: the defaults ARE the compiled-in figures, so the worst case is the
	// behaviour the plugin had before this page existed.
	if (const UHFSettings* Settings = GetDefault<UHFSettings>())
	{
		return Settings->Resolve();
	}

	return FHFBuildDefaults();
}
