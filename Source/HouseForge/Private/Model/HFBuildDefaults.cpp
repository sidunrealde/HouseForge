// Copyright Siddartha G. All Rights Reserved.

#include "Model/HFBuildDefaults.h"

// Stamping, not copying. Each overload writes the construction figures and leaves everything else
// exactly as the caller had it - dimensions above all, because those are what the composing layer
// has just worked out and a blanket copy would wipe them.

void FHFJoineryDefaults::ApplyTo(FHFPlinthParams& Params) const
{
	Params.Height = PlinthHeight;
	Params.FrontRecess = PlinthFrontRecess;
	Params.EndRecess = PlinthEndRecess;
	Params.PanelThickness = PlinthPanelThickness;

	// ShutterOverlay is deliberately not set here. It is the figure the shutter was generated with,
	// so it depends on whether this run has doors over its base at all - that is the composing
	// layer's call, and defaulting it would silently deepen every toe kick by a leaf's thickness.
}

void FHFJoineryDefaults::ApplyTo(FHFShutterParams& Params) const
{
	Params.ModuleWidth = ShutterModuleWidth;
	Params.Thickness = ShutterLeafThickness;
	Params.RevealGap = ShutterRevealGap;
	Params.OpenAngleDegrees = ShutterOpenAngleDegrees;
	Params.BackClearance = ShutterBackClearance;
	Params.StileWidth = GlazedShutterStileWidth;
	Params.GlassThickness = ShutterGlassThickness;

	// ModuleHeight is a dimension, not a figure: it is the bay the leaf closes.
}

void FHFJoineryDefaults::ApplyTo(FHFCorniceParams& Params) const
{
	Params.Depth = CorniceDepth;
	Params.Height = CorniceHeight;
	Params.Projection = CorniceProjection;
	Params.ProfileSize = CorniceProfileSize;
	Params.EdgeBevel = CorniceEdgeBevel;
}

void FHFJoineryDefaults::ApplyTo(FHFShelfStackParams& Params) const
{
	Params.FrontSetback = ShelfFrontSetback;
	Params.PartitionThickness = CarcassBoardThickness;
	Params.RailDiameter = HangingRailDiameter;
	Params.RailDrop = HangingRailDrop;
	Params.MinHangingClearance = MinHangingClearance;

	// ShelfThickness and MaxSpan are left at their zero sentinel, which means "whatever this
	// material is". Writing the ply figure over them would generate an 18 mm glass shelf, which is
	// a glass shelf nobody has ever seen.
	//
	// That does NOT make the project's shelf figures unreachable - it makes them travel separately.
	// Hand ShelfFigures() to SanitiseShelfStack or GenerateShelfStack and the sentinel resolves
	// against the project's numbers for the material the bay actually turned out to be.
}

FHFShelfMaterialFigures FHFJoineryDefaults::ShelfFigures() const
{
	FHFShelfMaterialFigures Figures;

	Figures.PlyThickness = ShelfThicknessPly;
	Figures.GlassThickness = ShelfThicknessGlass;
	Figures.PlyMaxSpan = MaxShelfSpanPly;
	Figures.GlassMaxSpan = MaxShelfSpanGlass;

	return Figures;
}

void FHFJoineryDefaults::ApplyTo(FHFDrawerParams& Params) const
{
	Params.ModuleWidth = ShutterModuleWidth;
	Params.CarcassSideThickness = CarcassBoardThickness;
	Params.FrontThickness = DrawerFrontThickness;
	Params.BoxSideThickness = DrawerBoxSideThickness;
	Params.BoxBottomThickness = DrawerBoxBottomThickness;
	Params.RevealGap = DrawerRevealGap;
	Params.BackClearance = DrawerBackClearance;

	// ModuleHeight, CarcassDepth and RunnerLength are dimensions of the unit being built.
}
