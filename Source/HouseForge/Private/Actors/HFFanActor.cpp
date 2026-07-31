// Copyright Siddartha G. All Rights Reserved.

#include "Actors/HFFanActor.h"

#include "Model/HFBuildDefaults.h"

using namespace UE::Geometry;

void AHFFanActor::ApplyProjectDefaults(EHFFanKind Kind)
{
	// The composing layer's job, and the only lines in this file that know a settings object could
	// exist. By the time the generator runs, everything it needs is already on the actor.
	Fan = FHFFanKit::DefaultsFor(Kind);
	FHFBuildDefaults::FromProjectSettings().Fan.ApplyTo(Fan);
}

void AHFFanActor::ApplyFixture(const FHFFixture& Fixture)
{
	Fan.Kind = Fixture.Type == EHFFixtureType::ExhaustFan ? EHFFanKind::Exhaust : EHFFanKind::Ceiling;

	// A fan is specified and bought by its SWEEP, which is what Diameter carries. A drawing that
	// stated one is believed; one that did not falls back on the footprint it was drawn at, because
	// a fan symbol on a plan is drawn at its sweep.
	const double Drawn = FMath::Max(Fixture.Footprint.X, Fixture.Footprint.Y);
	Fan.SweepDiameter = Fixture.Params.Diameter > 0.0 ? Fixture.Params.Diameter : Drawn;

	if (Fan.Kind == EHFFanKind::Exhaust)
	{
		// An extract's case depth is how far it stands out of the wall, which is the smaller of the
		// two plan dimensions - the drawing's 250 x 100 is a 250 fan in a 100 deep case.
		const double Shallow = FMath::Min(Fixture.Footprint.X, Fixture.Footprint.Y);
		if (Shallow > 0.0)
		{
			Fan.CaseDepth = Shallow;
		}

		// Both plan dimensions are the case, so the sweep is the aperture inside it rather than the
		// outside of the box. Left at the drawn width the blades would foul their own frame.
		Fan.SweepDiameter = Drawn * 0.75;
	}

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

	return FTransform(FRotationMatrix::MakeFromZ(Axis).ToQuat(),
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
