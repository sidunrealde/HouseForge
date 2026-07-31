// Copyright Siddartha G. All Rights Reserved.

#include "Model/HFBuildDefaults.h"

#include "Geometry/HFJoineryKit.h"

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

void FHFJoineryDefaults::ApplyTo(FHFCarcassParams& Params) const
{
	Params.BoardThickness = CarcassBoardThickness;

	// BackThickness is deliberately left alone. A carcass back is 6 to 12 mm ply pinned into a rebate,
	// genuinely a different board from the 18 the sides are cut from, and FHFCarcassParams keeps the
	// two apart for exactly that reason: the back is the one figure that changes the CLEAR DEPTH a
	// shelf and a hanging rail get. Stamping the side board onto it would make a project that thickened
	// its carcass quietly build a shallower wardrobe inside than the one it ordered.
	//
	// There is no settings control for it yet because nothing has asked for one, and a control on the
	// page that nothing had ever been measured against is what this whole struct exists to avoid.

	// Width, Depth, Height and BayCount are dimensions of the unit being built, not figures.
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

int32 FHFJoineryDefaults::ShelfCountFor(double ClearHeight, double ShelfThickness) const
{
	// The whole point of this function: both figures come from the project rather than being left
	// zero for the kit to fill in from its own constants, which is what every caller did before and
	// is why the two controls moved nothing.
	return FHFJoineryKit::ShelfCountForClearHeight(
		ClearHeight,
		TargetShelfSpacing,
		ShelfThickness > 0.0 ? ShelfThickness : ShelfThicknessPly,
		MinUsefulCompartment);
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

// ------------------------------------------------------------------------------------------ fans

void FHFFanDefaults::ApplyTo(FHFFanParams& Params) const
{
	// Only the figures, never the dimensions. SweepDiameter comes off the drawing - a fan is bought
	// and specified by its sweep - and stamping a project default over it would silently make every
	// fan in the flat the same size whatever the plan said.
	//
	// The two kinds share nothing, so each takes its own: a ceiling fan turns at 300 rpm on three
	// blades and an extract at 1350 on five, and one figure covering both would be wrong for one of
	// them by a factor of four.
	const bool bCeiling = Params.Kind == EHFFanKind::Ceiling;

	Params.RevolutionsPerMinute = bCeiling ? CeilingFanRpm : ExhaustFanRpm;
	Params.BladeCount = bCeiling ? CeilingFanBladeCount : ExhaustFanBladeCount;

	// THE PITCH IS PER KIND TOO, and it was not. One BladePitchDegrees was stamped over both, which
	// put a ceiling fan's 12 degrees onto every extract in the flat and made the kit's own 22 - set
	// deliberately, with a comment saying an extract is a different object - unreachable from
	// anything the house built. A 5-blade 22 cm impeller at 12 degrees reads as flat spokes.
	Params.BladePitchDegrees = bCeiling ? CeilingFanBladePitchDegrees : ExhaustFanBladePitchDegrees;

	if (bCeiling)
	{
		// A rod length means nothing on an extract, which is set into a wall rather than hung off a
		// slab, and writing one would leave a figure on the params that the generator ignores.
		Params.DropLength = CeilingFanDropLength;
	}
}
