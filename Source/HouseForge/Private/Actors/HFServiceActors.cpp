// Copyright Siddartha G. All Rights Reserved.

#include "Actors/HFServiceActors.h"

#include "Model/HFBuildDefaults.h"

using namespace UE::Geometry;

namespace
{
	/**
	 * A hair of daylight between an appliance and the skirting it stands in front of.
	 *
	 * The same figure and the same reason as AHFDeskActor's: two large flat surfaces in exact contact
	 * render as a flickering seam, and at floor level along a wall it is right where the eye follows
	 * the room round.
	 */
	constexpr double SkirtingScribeGap = 0.2;
}

// -------------------------------------------------------------------------- socket and switch plate

void AHFAccessoryPlateActor::ApplyProjectDefaults()
{
	// Nothing. A modular accessory is bought to a range's own sizes - a grid plate, a cover and a
	// 45 mm module are what the manufacturer made, not what this project builds. The hook exists so
	// the composing layer can treat every fixture the same way, and so there is somewhere obvious for
	// a future accessory catalogue to be read from. The same reason AHFSinkActor has one.
}

FHFAccessoryPlateParams AHFAccessoryPlateActor::ParamsFor(const FHFFixture& Fixture)
{
	FHFAccessoryPlateParams P;

	P.Width = Fixture.Footprint.X;
	P.Depth = Fixture.Footprint.Y;
	P.Height = Fixture.Height;

	// TWO, NOT ZERO. A drawing that marked no gang count has said nothing rather than asked for a
	// plate with no modules on it, and a blank plate is a real product this is not: it would come out
	// as a rectangle of white with nothing to press.
	P.GangCount = Fixture.Params.GangCount > 0 ? Fixture.Params.GangCount : 2;

	// WHICH FIXTURE TYPE IT IS, AND THAT IS THE WHOLE DIFFERENCE. Same grid, same cover, same recess;
	// one carries outlets and the other does not.
	P.Kind = Fixture.Type == EHFFixtureType::PowerSocket
		? EHFAccessoryKind::Socket
		: EHFAccessoryKind::Switch;

	// The border scales with the plate but never runs away with it: 12 mm is what a cover leaves round
	// its window, and a 320 mm eight gang plate does not get a 25 mm one just because it is wider.
	P.PlateBorder = FMath::Clamp(FMath::Min(P.Width, P.Height) * 0.09, 0.8, 1.4);

	return FHFWallPlateKit::SanitiseAccessoryPlate(P);
}

void AHFAccessoryPlateActor::ApplyFixture(const FHFFixture& Fixture)
{
	Plate = ParamsFor(Fixture);
}

FDynamicMesh3 AHFAccessoryPlateActor::BuildMesh() const
{
	return FHFWallPlateKit::BuildAccessoryPlate(Plate).Shell;
}

void AHFAccessoryPlateActor::BuildParts(TArray<FHFMeshPart>& OutParts) const
{
	FHFWallPlateBuild Built = FHFWallPlateKit::BuildAccessoryPlate(Plate);
	OutParts.Append(MoveTemp(Built.Parts));
}

// --------------------------------------------------------------------------------- consumer unit

void AHFDistributionBoardActor::ApplyProjectDefaults()
{
	// Nothing. A consumer unit is a bought enclosure on a 17.5 mm DIN module, and neither figure is
	// this project's to choose.
}

void AHFDistributionBoardActor::ApplyFixture(const FHFFixture& Fixture)
{
	Board.Width = Fixture.Footprint.X;
	Board.Depth = Fixture.Footprint.Y;
	Board.Height = Fixture.Height;

	// HOW MANY WAYS A BOARD OF THIS SIZE ACTUALLY HAS, derived rather than declared: a plan marks a
	// distribution board by the box on the wall and never by its schedule. Two thirds of the width
	// populated is what a flat of this class is wired with - the rest is spare ways, which is what a
	// board is oversized for in the first place. Sanitise clamps it to the rail that exists.
	Board.WayCount = FMath::Max(FMath::FloorToInt32((Fixture.Footprint.X * 0.62) / Board.ModulePitch), 2);
}

FDynamicMesh3 AHFDistributionBoardActor::BuildMesh() const
{
	return FHFWallPlateKit::BuildDistributionBoard(Board).Shell;
}

void AHFDistributionBoardActor::BuildParts(TArray<FHFMeshPart>& OutParts) const
{
	FHFWallPlateBuild Built = FHFWallPlateKit::BuildDistributionBoard(Board);
	OutParts.Append(MoveTemp(Built.Parts));
}

// -------------------------------------------------------------------------------- split AC, indoor

void AHFSplitACActor::ApplyProjectDefaults()
{
	// Nothing. A split head is a moulding out of a factory.
}

FHFSplitACParams AHFSplitACActor::ParamsFor(const FHFFixture& Fixture)
{
	FHFSplitACParams P;

	P.Length = Fixture.Footprint.X;
	P.Depth = Fixture.Footprint.Y;
	P.Height = Fixture.Height;

	// One blade per 130 mm of length, which is the pitch a real discharge is louvred at. Derived, so a
	// longer unit gets more fins rather than the same seven stretched across it.
	P.DeflectorCount = FMath::Clamp(FMath::RoundToInt32(Fixture.Footprint.X / 13.0), 3, 12);

	return FHFApplianceKit::SanitiseSplitAC(P);
}

void AHFSplitACActor::ApplyFixture(const FHFFixture& Fixture)
{
	Unit = ParamsFor(Fixture);
}

FDynamicMesh3 AHFSplitACActor::BuildMesh() const
{
	return FHFApplianceKit::BuildSplitAC(Unit).Shell;
}

void AHFSplitACActor::BuildParts(TArray<FHFMeshPart>& OutParts) const
{
	FHFApplianceBuild Built = FHFApplianceKit::BuildSplitAC(Unit);
	OutParts.Append(MoveTemp(Built.Parts));
}

// ------------------------------------------------------------------------------ condensing unit

void AHFCondenserActor::ApplyProjectDefaults()
{
	// Nothing. Bought, and it stands on a balcony where this project owns no figure at all.
}

void AHFCondenserActor::ApplyFixture(const FHFFixture& Fixture)
{
	Unit.Width = Fixture.Footprint.X;
	Unit.Depth = Fixture.Footprint.Y;
	Unit.Height = Fixture.Height;

	// Slats at about a 40 mm pitch over the coil, so a taller case gets more of them rather than
	// eleven stretched fins.
	Unit.CoilSlats = FMath::Clamp(FMath::RoundToInt32(Fixture.Height * 0.8 / 4.0), 4, 24);
}

FDynamicMesh3 AHFCondenserActor::BuildMesh() const
{
	return FHFApplianceKit::BuildCondenser(Unit).Shell;
}

void AHFCondenserActor::BuildParts(TArray<FHFMeshPart>& OutParts) const
{
	FHFApplianceBuild Built = FHFApplianceKit::BuildCondenser(Unit);
	OutParts.Append(MoveTemp(Built.Parts));
}

// ---------------------------------------------------------------------------------- refrigerator

void AHFRefrigeratorActor::ApplyProjectDefaults()
{
	// THE SKIRTING, which is the one figure on a bought appliance that this project does own. A
	// refrigerator is not scribed joinery, so the board runs on behind it and the cabinet has to stand
	// in front of that board; left at zero it stands 18 mm inside it, permanently and invisibly.
	//
	// Resolved here because only the composing layer may read a settings object, and handed to the
	// generator as a plain dimension - see .claude/rules/04-conventions.md and
	// FHFDeskParams::SupportSetback, which is the same figure for the same reason.
	Fridge.SkirtingSetback = FHFBuildDefaults::FromProjectSettings().Skirting.Depth + SkirtingScribeGap;
}

void AHFRefrigeratorActor::ApplyFixture(const FHFFixture& Fixture)
{
	Fridge.Width = Fixture.Footprint.X;
	Fridge.Depth = Fixture.Footprint.Y;
	Fridge.Height = Fixture.Height;
}

FDynamicMesh3 AHFRefrigeratorActor::BuildMesh() const
{
	return FHFApplianceKit::BuildRefrigerator(Fridge).Shell;
}

void AHFRefrigeratorActor::BuildParts(TArray<FHFMeshPart>& OutParts) const
{
	FHFApplianceBuild Built = FHFApplianceKit::BuildRefrigerator(Fridge);
	OutParts.Append(MoveTemp(Built.Parts));
}

// -------------------------------------------------------------------------------- washing machine

void AHFWashingMachineActor::ApplyProjectDefaults()
{
	// As the refrigerator, and for the same reason.
	Washer.SkirtingSetback = FHFBuildDefaults::FromProjectSettings().Skirting.Depth + SkirtingScribeGap;
}

void AHFWashingMachineActor::ApplyFixture(const FHFFixture& Fixture)
{
	Washer.Width = Fixture.Footprint.X;
	Washer.Depth = Fixture.Footprint.Y;
	Washer.Height = Fixture.Height;

	// The porthole scales with the case, within what a domestic front loader is ever made with: 320 mm
	// is standard and the range sold runs about 300 to 360. Its centre goes at 58% of the height, which
	// puts it clear of the fascia above and of the pump filter flap below.
	Washer.PortholeDiameter = FMath::Clamp(Fixture.Footprint.X * 0.53, 26.0, 36.0);
	Washer.PortholeCentreZ = Fixture.Height * 0.50;
}

FDynamicMesh3 AHFWashingMachineActor::BuildMesh() const
{
	return FHFApplianceKit::BuildWashingMachine(Washer).Shell;
}

void AHFWashingMachineActor::BuildParts(TArray<FHFMeshPart>& OutParts) const
{
	FHFApplianceBuild Built = FHFApplianceKit::BuildWashingMachine(Washer);
	OutParts.Append(MoveTemp(Built.Parts));
}
