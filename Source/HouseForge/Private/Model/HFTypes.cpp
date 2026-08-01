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

bool HFPolygonContainsPoint(const TArray<FVector2D>& Polygon, const FVector2D& Point)
{
	// Even-odd ray cast along +X. Handles the concave L-shaped rooms these layouts are full of;
	// a convex-only test would wrongly place fixtures inside re-entrant corners.
	const int32 Count = Polygon.Num();
	if (Count < 3)
	{
		return false;
	}

	bool bInside = false;
	for (int32 i = 0, j = Count - 1; i < Count; j = i++)
	{
		const FVector2D& A = Polygon[i];
		const FVector2D& B = Polygon[j];

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

bool FHFRoom::ContainsPoint(const FVector2D& Point) const
{
	return HFPolygonContainsPoint(Boundary, Point);
}

const FHFWall* FHFHouseSpec::FindWall(const FName& WallId) const
{
	return Walls.FindByPredicate([&WallId](const FHFWall& W) { return W.Id == WallId; });
}

const FHFRoom* FHFHouseSpec::FindRoom(const FName& RoomId) const
{
	return Rooms.FindByPredicate([&RoomId](const FHFRoom& R) { return R.Id == RoomId; });
}

namespace
{
	/**
	 * Distance from a plan point to the nearest edge of a room, unsigned.
	 */
	double HFDistanceToBoundary(const FHFRoom& Room, const FVector2D& Point)
	{
		double Nearest = TNumericLimits<double>::Max();
		const int32 Count = Room.Boundary.Num();
		for (int32 i = 0, j = Count - 1; i < Count; j = i++)
		{
			const FVector2D& A = Room.Boundary[j];
			const FVector2D& B = Room.Boundary[i];
			const FVector2D Edge = B - A;
			const double LengthSq = Edge.SizeSquared();

			const double T = (LengthSq > UE_KINDA_SMALL_NUMBER)
				? FMath::Clamp(FVector2D::DotProduct(Point - A, Edge) / LengthSq, 0.0, 1.0)
				: 0.0;

			Nearest = FMath::Min(Nearest, FVector2D::Distance(Point, A + Edge * T));
		}
		return Nearest;
	}

	/**
	 * Half the thickness of the wall running directly beneath a beam at a plan point, or 0 where the
	 * beam has no wall under it.
	 *
	 * WHAT A WALL CAN AND CANNOT CONCEAL, which is the whole question a ceiling drop turns on.
	 *
	 * A beam is set out on a wall's centreline, so it is hidden by that wall only as far as the wall
	 * itself reaches: a 230 beam over a 230 wall is flush on both faces and genuinely invisible, and
	 * the SAME beam over a 115 partition stands 57.5 proud of the plaster on both sides for the
	 * whole length of the run. That is not a subtlety - it is a continuous ledge round the top of
	 * every room the partition borders, and it is what "a ragged dark line along the top of every
	 * wall" turned out to be once the flashing behind it was fixed.
	 *
	 * Only walls PARALLEL to the beam are asked. A partition crossing under a beam touches its
	 * centreline at a single point and conceals nothing along it; counting its thickness would let
	 * one junction vouch for a whole run.
	 */
	double HFConcealingWallHalfThickness(const TArray<FHFWall>& Walls, const FVector2D& Point,
		const FVector2D& BeamDirection, double Tolerance)
	{
		double Half = 0.0;

		for (const FHFWall& Wall : Walls)
		{
			const double Length = Wall.Length();
			if (Length <= UE_KINDA_SMALL_NUMBER)
			{
				continue;
			}

			const FVector2D Direction = (Wall.End - Wall.Start) / Length;
			if (FMath::Abs(FVector2D::DotProduct(Direction, BeamDirection)) < 0.999)
			{
				continue;
			}

			const double T = FMath::Clamp(
				FVector2D::DotProduct(Point - Wall.Start, Direction) / Length, 0.0, 1.0);

			if (FVector2D::Distance(Point, FMath::Lerp(Wall.Start, Wall.End, T)) <= Tolerance)
			{
				Half = FMath::Max(Half, Wall.Thickness * 0.5);
			}
		}

		return Half;
	}

	/** Sample along a beam rather than intersecting polygons. A beam either runs with a room's edge or crosses it. */
	constexpr int32 HFBeamSampleCount = 24;
}

const FHFBeam* FHFHouseSpec::DeepestBeamOverRoom(const FName& RoomId) const
{
	const FHFRoom* Room = FindRoom(RoomId);
	if (Room == nullptr || Room->Boundary.Num() < 3)
	{
		return nullptr;
	}

	// Below this a beam is flush with its wall for modelling purposes. In CENTIMETRES, converted
	// into whatever the spec declares, because "a centimetre of concrete does not read as a ledge"
	// is a statement about the building and not about the numbers it happens to be written in.
	const double Scale = FHFUnits::ToCentimeterScale(Units);
	const double Flush = (Scale > UE_KINDA_SMALL_NUMBER) ? (1.0 / Scale) : 1.0;

	const FHFBeam* Deepest = nullptr;
	for (const FHFBeam& Beam : Beams)
	{
		const double BeamLength = Beam.Length();
		if (BeamLength <= UE_KINDA_SMALL_NUMBER)
		{
			continue;
		}

		const FVector2D Direction = (Beam.End - Beam.Start) / BeamLength;

		bool bShows = false;
		for (int32 i = 0; i <= HFBeamSampleCount && !bShows; ++i)
		{
			const double T = static_cast<double>(i) / HFBeamSampleCount;
			const FVector2D Point = FMath::Lerp(Beam.Start, Beam.End, T);

			// How far into the room the beam's own footprint reaches, signed from the boundary.
			// A centreline on the boundary reaches in by half the beam's width; one already inside
			// reaches further; one outside has to make the distance up before it counts at all.
			const double Inside = Room->ContainsPoint(Point)
				? HFDistanceToBoundary(*Room, Point)
				: -HFDistanceToBoundary(*Room, Point);

			const double Reach = Inside + Beam.Width * 0.5;
			const double Concealed = HFConcealingWallHalfThickness(Walls, Point, Direction, Flush);

			bShows = Reach > Concealed + Flush;
		}

		if (bShows && (Deepest == nullptr || Beam.Depth > Deepest->Depth))
		{
			Deepest = &Beam;
		}
	}

	return Deepest;
}

const FHFBeam* FHFHouseSpec::DeepestBeamCrossingRoom(const FName& RoomId) const
{
	const FHFRoom* Room = FindRoom(RoomId);
	if (Room == nullptr || Room->Boundary.Num() < 3)
	{
		return nullptr;
	}

	const FHFBeam* Deepest = nullptr;
	for (const FHFBeam& Beam : Beams)
	{
		if (Beam.Length() <= UE_KINDA_SMALL_NUMBER)
		{
			continue;
		}

		// Clear of every boundary edge by its own half width: the beam is in open room, not running
		// along a wall line. A plain inside/outside test would call every perimeter beam a crossing.
		const double Clearance = Beam.Width * 0.5;

		bool bCrosses = false;
		for (int32 i = 0; i <= HFBeamSampleCount && !bCrosses; ++i)
		{
			const double T = static_cast<double>(i) / HFBeamSampleCount;
			const FVector2D Point = FMath::Lerp(Beam.Start, Beam.End, T);
			bCrosses = Room->ContainsPoint(Point) && HFDistanceToBoundary(*Room, Point) > Clearance;
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
	case EHFUnits::Feet:		return 30.48;
	case EHFUnits::Inches:		return 2.54;
	default:					return 1.0;
	}
}

FString FHFUnits::ShortName(EHFUnits Units)
{
	switch (Units)
	{
	case EHFUnits::Millimeters:	return TEXT("mm");
	case EHFUnits::Centimeters:	return TEXT("cm");
	case EHFUnits::Meters:		return TEXT("m");
	case EHFUnits::Feet:		return TEXT("ft");
	case EHFUnits::Inches:		return TEXT("in");
	default:					return TEXT("?");
	}
}

bool FHFUnits::ParseLengthToCentimeters(const FString& Text, EHFUnits DefaultUnits, double& OutCentimeters)
{
	FString Working = Text.TrimStartAndEnd();
	if (Working.IsEmpty())
	{
		return false;
	}

	// Normalise the typographic quotes drawings and PDFs are full of, so 12′-6″ parses the same
	// as 12'-6".
	Working.ReplaceInline(TEXT("′"), TEXT("'"));
	Working.ReplaceInline(TEXT("″"), TEXT("\""));
	Working.ReplaceInline(TEXT("’"), TEXT("'"));
	Working.ReplaceInline(TEXT("”"), TEXT("\""));

	auto ReadNumber = [](const FString& In, double& Out)
	{
		const FString Trimmed = In.TrimStartAndEnd();
		if (Trimmed.IsEmpty() || !Trimmed.IsNumeric())
		{
			// IsNumeric rejects a leading '+' and decimals in some builds, so fall back to Atod
			// but require the text to start like a number.
			if (Trimmed.IsEmpty() || !(FChar::IsDigit(Trimmed[0]) || Trimmed[0] == TEXT('.') || Trimmed[0] == TEXT('-')))
			{
				return false;
			}
		}
		Out = FCString::Atod(*Trimmed);
		return true;
	};

	// Feet and inches combined: 12'-6", 12' 6", 12'6
	if (Working.Contains(TEXT("'")))
	{
		FString FeetPart;
		FString RemainderPart;
		Working.Split(TEXT("'"), &FeetPart, &RemainderPart);

		double FeetValue = 0.0;
		if (!ReadNumber(FeetPart, FeetValue))
		{
			return false;
		}

		double InchesValue = 0.0;
		FString Remainder = RemainderPart.TrimStartAndEnd();
		Remainder.RemoveFromStart(TEXT("-"));
		Remainder.ReplaceInline(TEXT("\""), TEXT(""));
		Remainder.TrimStartAndEndInline();

		if (!Remainder.IsEmpty() && !ReadNumber(Remainder, InchesValue))
		{
			return false;
		}

		OutCentimeters = (FeetValue * 30.48) + (InchesValue * 2.54);
		return true;
	}

	// Inches alone: 78"
	if (Working.EndsWith(TEXT("\"")))
	{
		double Inches = 0.0;
		if (!ReadNumber(Working.LeftChop(1), Inches))
		{
			return false;
		}
		OutCentimeters = Inches * 2.54;
		return true;
	}

	// Metric with an explicit suffix. Check mm before m so "3600mm" is not read as metres.
	struct FSuffix { const TCHAR* Text; EHFUnits Units; };
	static const FSuffix Suffixes[] = {
		{ TEXT("mm"), EHFUnits::Millimeters },
		{ TEXT("cm"), EHFUnits::Centimeters },
		{ TEXT("in"), EHFUnits::Inches },
		{ TEXT("ft"), EHFUnits::Feet },
		{ TEXT("m"),  EHFUnits::Meters },
	};

	const FString Lower = Working.ToLower();
	for (const FSuffix& Suffix : Suffixes)
	{
		if (Lower.EndsWith(Suffix.Text))
		{
			double Value = 0.0;
			if (!ReadNumber(Working.LeftChop(FCString::Strlen(Suffix.Text)), Value))
			{
				return false;
			}
			OutCentimeters = Value * ToCentimeterScale(Suffix.Units);
			return true;
		}
	}

	// A bare number is in whatever the spec declared - which is exactly why it has to declare.
	double Bare = 0.0;
	if (!ReadNumber(Working, Bare))
	{
		return false;
	}
	OutCentimeters = Bare * ToCentimeterScale(DefaultUnits);
	return true;
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
