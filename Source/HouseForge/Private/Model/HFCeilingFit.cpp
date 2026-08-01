// Copyright Siddartha G. All Rights Reserved.

#include "Model/HFCeilingFit.h"

#include "Geometry/HFGenerators.h"

namespace
{
	/**
	 * How finely a footprint is sampled across each of its plan axes.
	 *
	 * Five, so the corners, the mid-edges and the centre are all in the set and a long run is asked
	 * at quarter points along its length. The zones a soffit height changes across are insets of the
	 * room boundary measured in tens of centimetres, and the widest fixture in a flat of this class is
	 * a 2.4 m wardrobe - so quarter points are a 60 cm stride against a 30 cm ring. Finer costs
	 * nothing here but buys nothing either.
	 */
	constexpr int32 FootprintSamples = 5;

	FVector2D RotateAbout(const FVector2D& Point, const FVector2D& Centre, double Degrees)
	{
		const double Radians = FMath::DegreesToRadians(Degrees);
		const double C = FMath::Cos(Radians);
		const double S = FMath::Sin(Radians);
		const FVector2D D = Point - Centre;
		return Centre + FVector2D(D.X * C - D.Y * S, D.X * S + D.Y * C);
	}
}

EHFCeilingFitRule FHFCeilingFit::RuleFor(EHFFixtureType Type)
{
	switch (Type)
	{
	// ------------------------------------------------------------------ hung from the ceiling
	case EHFFixtureType::CeilingFan:
		// Its rod already does this, correctly and with tests behind it. Named so the dependency set
		// is complete in one place; the fit deliberately changes nothing about it.
		return EHFCeilingFitRule::HangsOnARod;

	case EHFFixtureType::LightFixture:
		// BaseZ on a ceiling-mounted fixture is measured DOWN FROM THE CEILING already. This is what
		// makes "the ceiling" the finished soffit rather than the slab it is suspended from.
		return EHFCeilingFitRule::HangsFromSoffit;

	// ------------------------------------------------------------------ bought, fixed high on a wall
	case EHFFixtureType::ExhaustFan:
	case EHFFixtureType::ACIndoorUnit:
	case EHFFixtureType::Geyser:
	case EHFFixtureType::Chimney:
	case EHFFixtureType::Pelmet:
	case EHFFixtureType::Curtain:
		return EHFCeilingFitRule::Lowers;

	// ------------------------------------------------------------------ made on site, cut to the room
	case EHFFixtureType::Wardrobe:
	case EHFFixtureType::LoftUnit:
	case EHFFixtureType::KitchenWallCabinet:
	case EHFFixtureType::KitchenTallUnit:
	case EHFFixtureType::Bookshelf:
		return EHFCeilingFitRule::Shortens;

	default:
		// Everything else is either nowhere near a ceiling or nothing this may move. Left alone, and
		// still reported by the validator if a ceiling comes down onto it.
		return EHFCeilingFitRule::Ignores;
	}
}

TArray<FVector2D> FHFCeilingFit::SamplePoints(const FHFFixture& Fixture)
{
	TArray<FVector2D> Out;

	const FVector2D Half = Fixture.Footprint * 0.5;
	if (Half.X <= 0.0 || Half.Y <= 0.0)
	{
		Out.Add(Fixture.Position);
		return Out;
	}

	Out.Reserve(FootprintSamples * FootprintSamples);

	for (int32 IX = 0; IX < FootprintSamples; ++IX)
	{
		const double AlphaX = static_cast<double>(IX) / (FootprintSamples - 1);
		for (int32 IY = 0; IY < FootprintSamples; ++IY)
		{
			const double AlphaY = static_cast<double>(IY) / (FootprintSamples - 1);
			const FVector2D Local(
				FMath::Lerp(-Half.X, Half.X, AlphaX),
				FMath::Lerp(-Half.Y, Half.Y, AlphaY));

			Out.Add(RotateAbout(Fixture.Position + Local, Fixture.Position, Fixture.RotationDegrees));
		}
	}

	return Out;
}

double FHFCeilingFit::LowestSoffitZOver(const FHFFixture& Fixture, const FHFRoom& Room,
	const TArray<FHFFalseCeiling>& Ceilings)
{
	const double SlabZ = Room.FloorZ + Room.CeilingHeight;

	const TArray<FVector2D> Samples = SamplePoints(Fixture);
	double Deepest = 0.0;

	for (const FHFFalseCeiling& Ceiling : Ceilings)
	{
		if (Ceiling.RoomId != Room.Id || Ceiling.Style == EHFCeilingStyle::None)
		{
			continue;
		}

		for (const FVector2D& Sample : Samples)
		{
			// The same function the geometry is built from and the same one the fan's rod is resolved
			// against, so all three answers about one ceiling come from one place. A sample outside
			// the ceiling's outline answers zero, which cannot make the soffit look lower than it is.
			Deepest = FMath::Max(Deepest, FHFGenerators::CeilingSoffitDropAt(Ceiling, Room, Sample));
		}
	}

	return SlabZ - Deepest;
}

FHFCeilingFitResult FHFCeilingFit::Fit(const FHFFixture& Fixture, const FHFRoom& Room,
	const TArray<FHFFalseCeiling>& Ceilings, double Clearance)
{
	FHFCeilingFitResult Result;
	Result.Rule = RuleFor(Fixture.Type);
	Result.BaseZ = Fixture.BaseZ;
	Result.Height = Fixture.Height;
	Result.SoffitZ = LowestSoffitZOver(Fixture, Room, Ceilings);

	const double Gap = FMath::Max(Clearance, 0.0);
	const double SlabZ = Room.FloorZ + Room.CeilingHeight;

	switch (Result.Rule)
	{
	case EHFCeilingFitRule::HangsFromSoffit:
	{
		// Nothing changes about the fixture's own figures - BaseZ stays the drop below the ceiling
		// the drawing asked for. What changes is which ceiling that is measured from, and every
		// consumer resolves it the same way, so saying so is the whole fix.
		//
		// Recorded as a move only when the soffit is not the slab, because that is when the resolved
		// world height differs from what a slab datum would have produced.
		const double Drop = SlabZ - Result.SoffitZ;
		if (Drop > UE_KINDA_SMALL_NUMBER)
		{
			Result.Action = EHFCeilingFitAction::Rehung;
			Result.Adjustment = Drop;
		}
		break;
	}

	case EHFCeilingFitRule::Lowers:
	{
		const double HeadroomZ = Result.SoffitZ - Gap;
		const double TopZ = Room.FloorZ + Fixture.BaseZ + Fixture.Height;

		if (TopZ <= HeadroomZ + UE_KINDA_SMALL_NUMBER)
		{
			break;
		}

		const double WantedBaseZ = HeadroomZ - Fixture.Height - Room.FloorZ;

		if (WantedBaseZ < 0.0)
		{
			// There is not as much wall below the soffit as the fitting is tall. Left where it was
			// drawn: sinking it into the floor would be a second wrong answer, arrived at silently.
			Result.Action = EHFCeilingFitAction::Refused;
			Result.Shortfall = -WantedBaseZ;
			break;
		}

		Result.Action = EHFCeilingFitAction::Lowered;
		Result.Adjustment = Fixture.BaseZ - WantedBaseZ;
		Result.BaseZ = WantedBaseZ;
		break;
	}

	case EHFCeilingFitRule::Shortens:
	{
		const double HeadroomZ = Result.SoffitZ - Gap;
		const double TopZ = Room.FloorZ + Fixture.BaseZ + Fixture.Height;

		if (TopZ <= HeadroomZ + UE_KINDA_SMALL_NUMBER)
		{
			break;
		}

		const double WantedHeight = HeadroomZ - Room.FloorZ - Fixture.BaseZ;

		if (WantedHeight <= 0.0)
		{
			// The soffit is at or below the base the unit is fixed at, so there is no carcass left to
			// cut. Nothing this can do is right, so it does nothing and says so.
			Result.Action = EHFCeilingFitAction::Refused;
			Result.Shortfall = Fixture.Height - FMath::Max(WantedHeight, 0.0);
			break;
		}

		Result.Action = EHFCeilingFitAction::Shortened;
		Result.Adjustment = Fixture.Height - WantedHeight;
		Result.Height = WantedHeight;
		break;
	}

	case EHFCeilingFitRule::HangsOnARod:
	case EHFCeilingFitRule::Ignores:
	default:
		break;
	}

	return Result;
}

TArray<FHFFixture> FHFCeilingFit::FitAll(const FHFHouseSpec& Spec, double Clearance,
	TArray<FString>* OutMoved)
{
	TArray<FHFFixture> Out;
	Out.Reserve(Spec.Fixtures.Num());

	for (const FHFFixture& Fixture : Spec.Fixtures)
	{
		FHFFixture Fitted = Fixture;

		const FHFRoom* Room = Spec.FindRoom(Fixture.RoomId);
		if (Room != nullptr)
		{
			const FHFCeilingFitResult Result = Fit(Fixture, *Room, Spec.FalseCeilings, Clearance);

			// HangsFromSoffit changes the DATUM rather than the figures, and the datum is resolved by
			// whoever places the actor. Writing SoffitZ into BaseZ here would turn a ceiling-relative
			// drop into a floor-relative one and leave the field meaning two things at once.
			if (Result.Rule != EHFCeilingFitRule::HangsFromSoffit)
			{
				Fitted.BaseZ = Result.BaseZ;
				Fitted.Height = Result.Height;
			}

			if (OutMoved != nullptr && Result.Moved())
			{
				OutMoved->Add(Describe(Fixture, Result));
			}
		}

		Out.Add(MoveTemp(Fitted));
	}

	return Out;
}

FString FHFCeilingFit::Describe(const FHFFixture& Fixture, const FHFCeilingFitResult& Result)
{
	const FString Id = Fixture.Id.IsNone() ? TEXT("<unnamed>") : Fixture.Id.ToString();

	switch (Result.Action)
	{
	case EHFCeilingFitAction::Rehung:
		return FString::Printf(
			TEXT("'%s' hangs from the finished soffit at %.1f rather than from the slab, %.1f above it."),
			*Id, Result.SoffitZ, Result.Adjustment);

	case EHFCeilingFitAction::Lowered:
		return FString::Printf(
			TEXT("'%s' was lowered %.1f to %.1f so its head clears the soffit at %.1f."),
			*Id, Result.Adjustment, Result.BaseZ, Result.SoffitZ);

	case EHFCeilingFitAction::Shortened:
		return FString::Printf(
			TEXT("'%s' was cut down %.1f to %.1f tall so its head clears the soffit at %.1f."),
			*Id, Result.Adjustment, Result.Height, Result.SoffitZ);

	case EHFCeilingFitAction::Refused:
		return FString::Printf(
			TEXT("'%s' does not fit under the soffit at %.1f and cannot be made to: it is %.1f too big. It is left as drawn - raise the ceiling over it or move the fitting."),
			*Id, Result.SoffitZ, Result.Shortfall);

	default:
		return FString::Printf(TEXT("'%s' clears the soffit at %.1f."), *Id, Result.SoffitZ);
	}
}
