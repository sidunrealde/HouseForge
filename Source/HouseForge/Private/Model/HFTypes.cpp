// Copyright Siddartha G. All Rights Reserved.

#include "Model/HFTypes.h"

double FHFRoom::SignedArea() const
{
	// Shoelace. The boundary's closing edge is implicit, so the last vertex wraps to the first.
	const int32 Count = Boundary.Num();
	if (Count < 3)
	{
		return 0.0;
	}

	double Twice = 0.0;
	for (int32 i = 0, j = Count - 1; i < Count; j = i++)
	{
		Twice += (Boundary[j].X * Boundary[i].Y) - (Boundary[i].X * Boundary[j].Y);
	}

	return Twice * 0.5;
}

bool FHFRoom::ContainsPoint(const FVector2D& Point) const
{
	// Even-odd ray cast along +X. Handles the concave L-shaped rooms these layouts are full of;
	// a convex-only test would wrongly place fixtures inside re-entrant corners.
	const int32 Count = Boundary.Num();
	if (Count < 3)
	{
		return false;
	}

	bool bInside = false;
	for (int32 i = 0, j = Count - 1; i < Count; j = i++)
	{
		const FVector2D& A = Boundary[i];
		const FVector2D& B = Boundary[j];

		const bool bStraddles = (A.Y > Point.Y) != (B.Y > Point.Y);
		if (!bStraddles)
		{
			continue;
		}

		const double Denominator = B.Y - A.Y;
		if (FMath::IsNearlyZero(Denominator))
		{
			continue;
		}

		const double CrossingX = A.X + ((Point.Y - A.Y) / Denominator) * (B.X - A.X);
		if (Point.X < CrossingX)
		{
			bInside = !bInside;
		}
	}

	return bInside;
}

const FHFWall* FHFHouseSpec::FindWall(const FName& WallId) const
{
	return Walls.FindByPredicate([&WallId](const FHFWall& W) { return W.Id == WallId; });
}

const FHFRoom* FHFHouseSpec::FindRoom(const FName& RoomId) const
{
	return Rooms.FindByPredicate([&RoomId](const FHFRoom& R) { return R.Id == RoomId; });
}

const FHFBeam* FHFHouseSpec::DeepestBeamOverRoom(const FName& RoomId) const
{
	const FHFRoom* Room = FindRoom(RoomId);
	if (Room == nullptr || Room->Boundary.Num() < 3)
	{
		return nullptr;
	}

	// Sample along the beam rather than doing a true polygon-segment intersection. A beam either
	// spans a room or misses it entirely, so sampling is sufficient here.
	constexpr int32 SampleCount = 24;

	// Most beams sit directly over a wall, which means their centreline runs exactly along a room
	// boundary. Those are concealed by the wall itself and are not what a ceiling has to clear, so
	// a sample only counts when it is clear of every boundary edge - a plain inside/outside test
	// would report every perimeter beam as crossing every room it borders.
	auto DistanceToBoundary = [Room](const FVector2D& Point)
	{
		double Nearest = TNumericLimits<double>::Max();
		const int32 Count = Room->Boundary.Num();
		for (int32 i = 0, j = Count - 1; i < Count; j = i++)
		{
			const FVector2D& A = Room->Boundary[j];
			const FVector2D& B = Room->Boundary[i];
			const FVector2D Edge = B - A;
			const double LengthSq = Edge.SizeSquared();

			const double T = (LengthSq > UE_KINDA_SMALL_NUMBER)
				? FMath::Clamp(FVector2D::DotProduct(Point - A, Edge) / LengthSq, 0.0, 1.0)
				: 0.0;

			Nearest = FMath::Min(Nearest, FVector2D::Distance(Point, A + Edge * T));
		}
		return Nearest;
	};

	const FHFBeam* Deepest = nullptr;
	for (const FHFBeam& Beam : Beams)
	{
		if (Beam.Length() <= UE_KINDA_SMALL_NUMBER)
		{
			continue;
		}

		const double Clearance = Beam.Width * 0.5;

		bool bCrosses = false;
		for (int32 i = 0; i <= SampleCount && !bCrosses; ++i)
		{
			const double T = static_cast<double>(i) / SampleCount;
			const FVector2D Point = FMath::Lerp(Beam.Start, Beam.End, T);
			bCrosses = Room->ContainsPoint(Point) && DistanceToBoundary(Point) > Clearance;
		}

		if (bCrosses && (Deepest == nullptr || Beam.Depth > Deepest->Depth))
		{
			Deepest = &Beam;
		}
	}

	return Deepest;
}

double FHFHouseSpec::TotalFloorArea() const
{
	double Total = 0.0;
	for (const FHFRoom& Room : Rooms)
	{
		Total += Room.Area();
	}
	return Total;
}

double FHFUnits::ToCentimeterScale(EHFUnits Units)
{
	switch (Units)
	{
	case EHFUnits::Millimeters:	return 0.1;
	case EHFUnits::Centimeters:	return 1.0;
	case EHFUnits::Meters:		return 100.0;
	default:					return 1.0;
	}
}

void FHFUnits::ConvertToCentimeters(FHFHouseSpec& Spec)
{
	const double Scale = ToCentimeterScale(Spec.Units);
	if (FMath::IsNearlyEqual(Scale, 1.0))
	{
		// Already centimetres. Returning early is what makes this idempotent, so calling it twice
		// on the same spec cannot silently shrink the house by a factor of ten.
		Spec.Units = EHFUnits::Centimeters;
		return;
	}

	Spec.DefaultWallThickness *= Scale;
	Spec.DefaultWallHeight *= Scale;

	for (FHFWall& Wall : Spec.Walls)
	{
		Wall.Start *= Scale;
		Wall.End *= Scale;
		Wall.Thickness *= Scale;
		Wall.Height *= Scale;
		Wall.BaseZ *= Scale;
	}

	for (FHFOpening& Opening : Spec.Openings)
	{
		Opening.OffsetAlongWall *= Scale;
		Opening.Width *= Scale;
		Opening.Height *= Scale;
		Opening.SillHeight *= Scale;
	}

	for (FHFBeam& Beam : Spec.Beams)
	{
		Beam.Start *= Scale;
		Beam.End *= Scale;
		Beam.Width *= Scale;
		Beam.Depth *= Scale;
		Beam.SoffitZ *= Scale;
	}

	for (FHFColumn& Column : Spec.Columns)
	{
		Column.Position *= Scale;
		Column.Size *= Scale;
		Column.Height *= Scale;
		Column.BaseZ *= Scale;
		// RotationDegrees is dimensionless.
	}

	for (FHFRoom& Room : Spec.Rooms)
	{
		for (FVector2D& Point : Room.Boundary)
		{
			Point *= Scale;
		}
		Room.FloorZ *= Scale;
		Room.CeilingHeight *= Scale;
		Room.SkirtingHeight *= Scale;
	}

	for (FHFFalseCeiling& Ceiling : Spec.FalseCeilings)
	{
		Ceiling.Drop *= Scale;
		Ceiling.BandWidth *= Scale;
		Ceiling.Cove.ChannelWidth *= Scale;
		Ceiling.Cove.LipHeight *= Scale;
		Ceiling.Cove.Setback *= Scale;

		for (FVector2D& Point : Ceiling.ExplicitPolygon)
		{
			Point *= Scale;
		}
		for (FVector2D& Point : Ceiling.LightPositions)
		{
			Point *= Scale;
		}
	}

	for (FHFFixture& Fixture : Spec.Fixtures)
	{
		Fixture.Position *= Scale;
		Fixture.Footprint *= Scale;
		Fixture.Height *= Scale;
		Fixture.BaseZ *= Scale;

		// Counts, flags and angles are dimensionless; only the lengths in the param bag scale.
		Fixture.Params.LoftHeight *= Scale;
		Fixture.Params.PlinthHeight *= Scale;
		Fixture.Params.UpstandHeight *= Scale;
		Fixture.Params.CorniceHeight *= Scale;
		Fixture.Params.Diameter *= Scale;
	}

	Spec.Units = EHFUnits::Centimeters;
}
