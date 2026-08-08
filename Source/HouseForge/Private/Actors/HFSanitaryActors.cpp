// Copyright Siddartha G. All Rights Reserved.

#include "Actors/HFSanitaryActors.h"

using namespace UE::Geometry;

// ------------------------------------------------------------------------------------------- WC

void AHFWCActor::ApplyProjectDefaults()
{
	// Nothing on a WC comes off the joinery settings page - a vitreous china pan is what the
	// manufacturer cast, not what this project builds. The hook exists so the composing layer can
	// treat every fixture the same way, and so there is somewhere obvious for a future sanitaryware
	// catalogue to be read from. The same reason AHFSinkActor has one.
}

FHFWCParams AHFWCActor::ParamsFor(const FHFFixture& Fixture)
{
	FHFWCParams P;
	P.Width = Fixture.Footprint.X;
	P.Projection = Fixture.Footprint.Y;

	// THE DRAWN HEIGHT IS THE SEAT, not the top of the fitting. That is how a plan dimensions a WC -
	// 400 is the figure that has to agree with everything else in the room - and it is why this actor
	// has a ParamsFor at all: the object standing there is nearly twice that with its cistern on, and
	// FHFCeilingFit needs the built envelope rather than the drawn box.
	P.SeatHeight = Fixture.Height;

	// A close-coupled cistern is a fixed proportion of the fitting rather than a free figure: it is
	// 340-380 wide, about 180 deep and about 380 tall on every one of them, because that is what holds
	// six litres in a box a person can lift onto a pan.
	P.CisternDepth = FMath::Min(18.0, Fixture.Footprint.Y * 0.32);
	P.CisternHeight = 38.0;

	return FHFSanitaryKit::SanitiseWC(P);
}

void AHFWCActor::ApplyFixture(const FHFFixture& Fixture)
{
	WC = ParamsFor(Fixture);
}

FDynamicMesh3 AHFWCActor::BuildMesh() const
{
	return FHFSanitaryKit::BuildWC(WC).Shell;
}

void AHFWCActor::BuildParts(TArray<FHFMeshPart>& OutParts) const
{
	FHFWCBuild Built = FHFSanitaryKit::BuildWC(WC);
	OutParts.Append(MoveTemp(Built.Parts));
}

// ---------------------------------------------------------------------------------------- basin

void AHFBasinActor::ApplyProjectDefaults()
{
	// As the WC: bought, not built.
}

void AHFBasinActor::ApplyFixture(const FHFFixture& Fixture)
{
	Basin.Width = Fixture.Footprint.X;
	Basin.Depth = Fixture.Footprint.Y;

	// The drawn height of a counter basin is how far the bowl stands ABOVE the surface it sits on,
	// which is what makes its BaseZ the surface and not its own underside.
	Basin.Height = Fixture.Height;

	// How deep the water can stand: most of the bowl's height, less the china under it. Derived
	// rather than declared, because no drawing of a flat this size dimensions the inside of a basin.
	Basin.BowlDepth = FMath::Max(Fixture.Height - Basin.CeramicThickness * 3.0, 0.0);

	// A LEDGE WIDE ENOUGH FOR THE TAP THAT GOES ON IT, scaled to the basin rather than fixed: a 500
	// basin and a 550 one both take the same tap, but a much smaller one would have its whole depth
	// taken by a ledge sized for a bigger fitting.
	Basin.TapLedgeWidth = FMath::Clamp(Fixture.Footprint.Y * 0.22, 6.0, 11.0);
	Basin.CornerRadius = FMath::Min(Fixture.Footprint.X, Fixture.Footprint.Y) * 0.2;

	// A basin tap is shorter and reaches less far than a kitchen one, because it is filling a bowl
	// rather than a pan, and it stands on a ledge that is already 180 above the counter.
	Basin.Tap.BodyHeight = 15.0;
	Basin.Tap.BodyRadius = 1.8;
	Basin.Tap.SpoutReach = FMath::Max(Fixture.Footprint.Y * 0.28, 8.0);
	Basin.Tap.SpoutRadius = 1.1;
	Basin.Tap.LeverLength = 7.0;

	// Half the swivel of a kitchen tap. A basin has one bowl and the spout only has to come off it to
	// be cleaned round; a full 90 each way would swing it out over the floor.
	Basin.Tap.SpoutSwivelDegrees = 45.0;
}

void AHFBasinActor::ApplyMount(EHFBasinMount Mount)
{
	Basin.Mount = Mount;
}

FDynamicMesh3 AHFBasinActor::BuildMesh() const
{
	return FHFSanitaryKit::BuildBasin(Basin).Shell;
}

void AHFBasinActor::BuildParts(TArray<FHFMeshPart>& OutParts) const
{
	FHFBasinBuild Built = FHFSanitaryKit::BuildBasin(Basin);
	OutParts.Append(MoveTemp(Built.Parts));
}

// --------------------------------------------------------------------------------------- shower

void AHFShowerActor::ApplyProjectDefaults()
{
	// As the rest: brassware is bought.
}

void AHFShowerActor::ApplyFixture(const FHFFixture& Fixture)
{
	Shower.Width = Fixture.Footprint.X;
	Shower.Depth = Fixture.Footprint.Y;
	Shower.Height = Fixture.Height;

	// THE ROSE HAS TO BE OVER SOMEBODY STANDING IN THE MIDDLE OF THE AREA, which is the one figure on
	// a shower that has to be derived rather than fixed: an arm sized for a 900 wet area reaches
	// nearly halfway across it, and the same arm in a 700 recess puts the spray on the far wall.
	//
	// A little short of the middle, because the person using it stands facing the mixer rather than
	// dead centre, and because the arm's own length is measured from the wall face.
	Shower.ArmReach = FMath::Clamp(Fixture.Footprint.Y * 0.38, 20.0, 45.0);
}

FDynamicMesh3 AHFShowerActor::BuildMesh() const
{
	return FHFSanitaryKit::BuildShower(Shower).Shell;
}

void AHFShowerActor::BuildParts(TArray<FHFMeshPart>& OutParts) const
{
	FHFShowerBuild Built = FHFSanitaryKit::BuildShower(Shower);
	OutParts.Append(MoveTemp(Built.Parts));
}

// --------------------------------------------------------------------------------------- geyser

void AHFGeyserActor::ApplyProjectDefaults()
{
	// As the rest: a pressure vessel is bought.
}

void AHFGeyserActor::ApplyFixture(const FHFFixture& Fixture)
{
	Geyser.Length = Fixture.Footprint.X;
	Geyser.Depth = Fixture.Footprint.Y;
	Geyser.Height = Fixture.Height;
}

FDynamicMesh3 AHFGeyserActor::BuildMesh() const
{
	return FHFApplianceKit::BuildGeyser(Geyser).Shell;
}

void AHFGeyserActor::BuildParts(TArray<FHFMeshPart>& OutParts) const
{
	FHFApplianceBuild Built = FHFApplianceKit::BuildGeyser(Geyser);
	OutParts.Append(MoveTemp(Built.Parts));
}

// --------------------------------------------------------------------------------------- mirror

void AHFMirrorActor::ApplyProjectDefaults()
{
	// Nothing. A mirror is cut to size by a glazier and has no construction figure this project owns.
}

void AHFMirrorActor::ApplyFixture(const FHFFixture& Fixture)
{
	Mirror.Width = Fixture.Footprint.X;
	Mirror.Depth = Fixture.Footprint.Y;
	Mirror.Height = Fixture.Height;

	// THE BEVEL SCALES WITH THE PLATE, within what a glazier actually grinds. 15-25 mm is the whole
	// range that is ever specified: below 15 it does not catch light and above 25 it starts to
	// distort what it reflects, which is why this is a clamp and not a fraction.
	Mirror.BevelWidth = FMath::Clamp(FMath::Min(Fixture.Footprint.X, Fixture.Height) * 0.028, 1.5, 2.5);
}

FDynamicMesh3 AHFMirrorActor::BuildMesh() const
{
	return FHFWallPlateKit::BuildMirror(Mirror).Shell;
}

// ----------------------------------------------------------------------------------- towel rail

void AHFTowelRailActor::ApplyProjectDefaults()
{
	// Nothing. Bought.
}

void AHFTowelRailActor::ApplyFixture(const FHFFixture& Fixture)
{
	Rail.Width = Fixture.Footprint.X;
	Rail.Depth = Fixture.Footprint.Y;
	Rail.Height = Fixture.Height;
}

FDynamicMesh3 AHFTowelRailActor::BuildMesh() const
{
	return FHFFrameKit::BuildTowelRail(Rail).Shell;
}
