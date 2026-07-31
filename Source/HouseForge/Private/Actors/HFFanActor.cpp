// Copyright Siddartha G. All Rights Reserved.

#include "Actors/HFFanActor.h"

#include "Model/HFBuildDefaults.h"

using namespace UE::Geometry;

namespace
{
	/**
	 * The dimensions a drawing actually states about a fan, put onto its parameters.
	 *
	 * Shared between ApplyFixture and DuctOpeningFor on purpose: the hole cored through a wall has to
	 * be derived from exactly the fan that ends up standing in it, and two copies of these rules
	 * would be two copies that drift.
	 */
	void ReadDrawnDimensions(const FHFFixture& Fixture, FHFFanParams& P)
	{
		P.Kind = Fixture.Type == EHFFixtureType::ExhaustFan ? EHFFanKind::Exhaust : EHFFanKind::Ceiling;

		// A fan is specified and bought by its SWEEP, which is what Diameter carries. A drawing that
		// stated one is believed; one that did not falls back on the footprint it was drawn at, because
		// a fan symbol on a plan is drawn at its sweep.
		const double Drawn = FMath::Max(Fixture.Footprint.X, Fixture.Footprint.Y);
		P.SweepDiameter = Fixture.Params.Diameter > 0.0 ? Fixture.Params.Diameter : Drawn;

		if (P.Kind == EHFFanKind::Exhaust)
		{
			// An extract's case depth is how far it stands out of the wall, which is the smaller of the
			// two plan dimensions - the drawing's 250 x 100 is a 250 fan in a 100 deep case.
			const double Shallow = FMath::Min(Fixture.Footprint.X, Fixture.Footprint.Y);
			if (Shallow > 0.0)
			{
				P.CaseDepth = Shallow;
			}

			// Both plan dimensions are the case, so the sweep is the aperture inside it rather than the
			// outside of the box. Left at the drawn width the blades would foul their own frame.
			P.SweepDiameter = Drawn * 0.75;
		}
	}
}

void AHFFanActor::ApplyProjectDefaults(EHFFanKind Kind)
{
	// The composing layer's job, and the only lines in this file that know a settings object could
	// exist. By the time the generator runs, everything it needs is already on the actor.
	Fan = FHFFanKit::DefaultsFor(Kind);
	FHFBuildDefaults::FromProjectSettings().Fan.ApplyTo(Fan);
}

FHFFanParams AHFFanActor::ParamsFor(const FHFFixture& Fixture)
{
	const EHFFanKind Kind = Fixture.Type == EHFFixtureType::ExhaustFan
		? EHFFanKind::Exhaust
		: EHFFanKind::Ceiling;

	FHFFanParams P = FHFFanKit::DefaultsFor(Kind);
	FHFBuildDefaults::FromProjectSettings().Fan.ApplyTo(P);
	ReadDrawnDimensions(Fixture, P);

	return FHFFanKit::Sanitise(P);
}

void AHFFanActor::ApplyCeilingAbove(double SoffitDrop)
{
	// A FAN HANGS FROM THE STRUCTURAL SLAB, so a false ceiling between the slab and the room is
	// something its rod has to get through - and DropLength was a fixed project figure that knew
	// nothing about it. In any room with a full drop the whole rotor was built INSIDE the
	// plasterboard: motor sunk into the panel, blades edge-on slivers lying in it, and from
	// underneath a bladed light fitting glued to the ceiling rather than a fan.
	//
	// ADDED TO THE PROJECT'S FIGURE RATHER THAN MAXED AGAINST IT. The setting is the rod somebody
	// SEES hanging in the room, so it has to be what is left below the soffit: taking the larger of
	// the two would let a 40 cm ceiling swallow the whole 30 cm figure and hang the fan flush with
	// the panel, which is the same wrong picture arrived at more slowly. A real installation hangs
	// its fans at one height whatever the ceiling above them is doing.
	//
	// Resolved here and not in the kit. The room's ceiling is world knowledge, and a generator that
	// read it would stop being a pure function of its parameters -
	// HouseForge.Architecture.GeneratorsDoNotReadSettings is the standing check on that.
	const double Drop = FMath::Max(SoffitDrop, 0.0);

	Fan.CanopyDrop = Drop;
	Fan.DropLength += Drop;
}

void AHFFanActor::ApplyFixture(const FHFFixture& Fixture)
{
	ReadDrawnDimensions(Fixture, Fan);

	// Varied per instance, deterministically. Three fans stopped on the same blade read as three
	// copies of one object; two builds of the same spec that stopped them differently would be two
	// renders disagreeing for no stated reason.
	//
	// DIVIDED BY THE BLADE COUNT, so that what is varied is what can actually be SEEN. A rotor
	// repeats every 1/BladeCount of a turn - a three-blade fan at 0.10 and one at 0.4333 are
	// pixel-identical - so spreading the raw phase over a whole revolution spreads three fans over
	// three copies of the same picture and calls them different. Folded this way, PhaseForId is
	// directly the blade angle a still shows: 0 is a blade where the last one was and 1 is the next
	// blade round.
	//
	// Read from Fan.BladeCount, which is why ApplyProjectDefaults has to run first - it is the
	// project's figure for this kind of fan, and it is already on the params by the time we get here.
	Fan.PhaseTurns = PhaseForId(Fixture.Id) / FMath::Max(Fan.BladeCount, 1);
}

double AHFFanActor::PhaseForId(FName FixtureId)
{
	// A hash rather than a counter, because a counter would depend on the order fixtures happen to
	// appear in the spec: insert a fan in the living room and every fan after it in the file would
	// shift. Keyed on the id, so a fan's phase is a property of that fan.
	const uint32 Hash = GetTypeHash(FixtureId.ToString().ToLower());

	// Into 0..1 of ONE BLADE PITCH - not of a revolution. A rotor repeats every 1/BladeCount turn, so
	// this is the whole of the range in which two fans can look different at all: 0 puts a blade
	// exactly where the previous one stood and 1 is the next blade round. ApplyFixture divides by the
	// blade count to turn it into the phase itself.
	//
	// Stated as a fraction of a blade rather than of a turn because that is the unit any assertion
	// about "stopped on the same blade" has to be made in, and because it is the unit somebody
	// reading a render is judging in.
	return static_cast<double>(Hash % 10000u) / 10000.0;
}

FHFOpening AHFFanActor::DuctOpeningFor(const FHFFixture& Fixture, const FHFWall& Wall, const FHFRoom* Room)
{
	FHFOpening Duct;
	Duct.Id = FName(*FString::Printf(TEXT("%s_Duct"), *Fixture.Id.ToString()));
	Duct.WallId = Wall.Id;

	// Cut as a ventilator rather than a window, which is what it is: a hole high in a wall with no
	// leaf in it. Nothing spawns a sash for it - it is never added to the spec's openings, only to
	// the wall's own list - so the kind is here to say what the hole IS to anything reading it back.
	Duct.Kind = EHFOpeningKind::Ventilator;
	Duct.Swing = EHFSwing::None;

	if (Fixture.Type != EHFFixtureType::ExhaustFan)
	{
		// A ceiling fan cores nothing through a wall. Zero width so a caller that added it anyway
		// cuts nothing, rather than punching a hole for a fan hanging in the middle of the room.
		Duct.Width = 0.0;
		Duct.Height = 0.0;
		return Duct;
	}

	// The very fan that ends up standing in this hole, project figures and all, so the two cannot
	// be sized from different ideas of what the fan is.
	const double Side = ParamsFor(Fixture).DuctSide();
	Duct.Width = Side;
	Duct.Height = Side;

	// Along the wall from its start, to the fan's own centre - OffsetAlongWall is a centre, not an
	// edge. Projected onto the wall rather than taken from the fixture's distance to it, because the
	// fixture stands proud of the face it is fixed to.
	const FVector2D Along = Wall.End - Wall.Start;
	const double Length = Along.Size();
	if (Length > UE_KINDA_SMALL_NUMBER)
	{
		Duct.OffsetAlongWall = FVector2D::DotProduct(Fixture.Position - Wall.Start, Along / Length);
	}

	// ------------------------------------------------------------------- and up the wall, in ITS datum
	//
	// TWO DIFFERENT DATUMS MEET HERE, and getting that wrong hangs a raw hole out below the fan that
	// is supposed to be covering it - which is exactly the invisible-from-the-room failure this
	// function exists to fix, only the other way round.
	//
	// FHFFixture::BaseZ is measured ABOVE THE ROOM FLOOR, and PlacementFor honours that: the rotor
	// goes at Room.FloorZ + BaseZ + Height/2. FHFOpening::SillHeight is measured ABOVE THE WALL'S
	// BASE, and every consumer of one resolves it as Wall.BaseZ + SillHeight - GenerateWall,
	// OpeningCentre and the validator all do. Writing the room-datum figure straight into SillHeight
	// therefore puts the hole at Wall.BaseZ + BaseZ + Height/2 while the fan is at Room.FloorZ +
	// BaseZ + Height/2, and the two disagree by exactly (Wall.BaseZ - Room.FloorZ).
	//
	// Both are zero everywhere in the reference flat, so they agreed by coincidence rather than by
	// construction. One room on a raised floor - a bathroom with its slab dropped and made up, which
	// is normal - and the case stops covering the hole.
	const double FloorZ = Room != nullptr ? Room->FloorZ : 0.0;
	const double CentreZ = FloorZ + Fixture.BaseZ + Fixture.Height * 0.5;

	// Converted into the wall's own datum, so that Wall.BaseZ + SillHeight lands back on the fan's
	// centre - which is the property HouseForge.Editor.AnExtractHasSomethingToBlowThrough asserts.
	Duct.SillHeight = FMath::Max(CentreZ - Wall.BaseZ - Side * 0.5, 0.0);

	return Duct;
}

FTransform AHFFanActor::PlacementFor(const FHFFixture& Fixture, const FHFRoom* Room, const FHFWall* AnchorWall)
{
	const double FloorZ = Room != nullptr ? Room->FloorZ : 0.0;

	if (Fixture.Type != EHFFixtureType::ExhaustFan)
	{
		// A ceiling fan hangs from the STRUCTURAL slab, not from a false ceiling soffit. That is why
		// AHFCeilingActor cuts a hole for the rod in the first place: the rod passes through the
		// plasterboard, and a fan hung off the soffit instead would sit lower than it is drawn and
		// leave the hole above it serving nothing.
		//
		// BaseZ on a ceiling-mounted fixture is measured DOWN from the ceiling - see
		// FHFFixture::IsCeilingMounted - so it lowers the mounting point rather than raising it.
		const double CeilingZ = FloorZ + (Room != nullptr ? Room->CeilingHeight : 300.0) - Fixture.BaseZ;

		// Local +Z is the spin axis pointing away from the surface the fan is fixed to, so on a
		// ceiling it points straight down. Half a turn about X, which takes +Z to -Z and leaves the
		// handedness intact - a mirrored fan would have its blades pitched the wrong way and blow
		// upwards.
		return FTransform(FQuat(FVector::XAxisVector, UE_DOUBLE_PI),
			FVector(Fixture.Position.X, Fixture.Position.Y, CeilingZ));
	}

	// An extract is fixed to the face of the wall it discharges through, at the height the drawing
	// put it, with its axis on that wall's normal.
	const double CentreZ = FloorZ + Fixture.BaseZ + Fixture.Height * 0.5;

	FVector2D Centre = Fixture.Position;
	FVector Axis = FVector::YAxisVector;

	if (AnchorWall != nullptr)
	{
		const FVector2D OnWall = FMath::ClosestPointOnSegment2D(Fixture.Position, AnchorWall->Start, AnchorWall->End);
		FVector2D Out = Fixture.Position - OnWall;

		if (!Out.Normalize())
		{
			// The fixture was drawn ON the wall's centreline, which says nothing about which side it
			// serves. Fall back on the wall's own normal, taken to the left of its direction, so the
			// fan at least faces along an axis rather than being left pointing at nothing.
			const FVector2D Along = (AnchorWall->End - AnchorWall->Start).GetSafeNormal();
			Out = FVector2D(-Along.Y, Along.X);
		}

		// Onto the room-side face rather than the centreline, or the case is buried in the wall.
		Centre = OnWall + Out * (AnchorWall->Thickness * 0.5);
		Axis = FVector(Out.X, Out.Y, 0.0);
	}
	else if (!FMath::IsNearlyZero(Fixture.RotationDegrees))
	{
		// No wall named, so the drawing's angle is all there is to go on.
		const double Radians = FMath::DegreesToRadians(Fixture.RotationDegrees);
		Axis = FVector(-FMath::Sin(Radians), FMath::Cos(Radians), 0.0);
	}

	// ROLL PINNED TO WORLD UP, not left to MakeFromZ. An extract's case is a SQUARE and its cowl is
	// louvred, and neither is symmetric about its own axis: MakeFromZ picks whatever second axis is
	// convenient for the normal it is given, so the same fan on two walls could come out square to
	// the world on one and diamond-on on the other, with its weather blades vertical. Naming the
	// up vector makes local Y vertical and local X horizontal on every wall.
	return FTransform(FRotationMatrix::MakeFromZY(Axis, FVector::ZAxisVector).ToQuat(),
		FVector(Centre.X, Centre.Y, CentreZ));
}

// ------------------------------------------------------------------------------------ generation

FDynamicMesh3 AHFFanActor::BuildMesh() const
{
	return FHFFanKit::Build(Fan).Shell;
}

void AHFFanActor::BuildParts(TArray<FHFMeshPart>& OutParts) const
{
	FHFFanBuild Built = FHFFanKit::Build(Fan);
	OutParts.Append(MoveTemp(Built.Parts));
}
