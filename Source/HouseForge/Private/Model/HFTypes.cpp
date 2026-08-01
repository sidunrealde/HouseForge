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

TArray<TArray<FVector2D>> FHFFalseCeiling::BulkheadStrips(const TArray<FVector2D>& Outline,
	double Width) const
{
	TArray<TArray<FVector2D>> Strips;

	const int32 Count = Outline.Num();
	if (Count < 3 || Width <= 0.0)
	{
		return Strips;
	}

	// Which way is into the room. The boundary is documented counter-clockwise, but a drawing that
	// arrived the other way round would otherwise put every strip outside the building.
	double TwiceArea = 0.0;
	for (int32 i = 0; i < Count; ++i)
	{
		const FVector2D& A = Outline[i];
		const FVector2D& B = Outline[(i + 1) % Count];
		TwiceArea += A.X * B.Y - B.X * A.Y;
	}
	const double Handedness = (TwiceArea >= 0.0) ? 1.0 : -1.0;

	for (int32 i = 0; i < Count; ++i)
	{
		if (!PerimeterBulkheadEdges.IsEmpty() && !PerimeterBulkheadEdges.Contains(i))
		{
			continue;
		}

		const FVector2D& A = Outline[i];
		const FVector2D& B = Outline[(i + 1) % Count];

		const double Length = FVector2D::Distance(A, B);
		if (Length <= UE_KINDA_SMALL_NUMBER)
		{
			continue;
		}

		const FVector2D Direction = (B - A) / Length;
		const FVector2D Inward = FVector2D(-Direction.Y, Direction.X) * Handedness;

		const FVector2D Along = Direction * Width;
		const FVector2D In = Inward * Width;
		const FVector2D Out = Inward * -Width;

		Strips.Add({ A - Along + Out, B + Along + Out, B + Along + In, A - Along + In });
	}

	return Strips;
}

const FHFBeam* FHFHouseSpec::DeepestBeamOnRoomEdge(const FName& RoomId, int32 EdgeIndex) const
{
	const FHFRoom* Room = FindRoom(RoomId);
	if (Room == nullptr || Room->Boundary.Num() < 3
		|| EdgeIndex < 0 || EdgeIndex >= Room->Boundary.Num())
	{
		return nullptr;
	}

	const double Scale = FHFUnits::ToCentimeterScale(Units);
	const double Flush = (Scale > UE_KINDA_SMALL_NUMBER) ? (1.0 / Scale) : 1.0;

	const FVector2D& EdgeA = Room->Boundary[EdgeIndex];
	const FVector2D& EdgeB = Room->Boundary[(EdgeIndex + 1) % Room->Boundary.Num()];

	const double EdgeLength = FVector2D::Distance(EdgeA, EdgeB);
	if (EdgeLength <= UE_KINDA_SMALL_NUMBER)
	{
		return nullptr;
	}

	const FVector2D EdgeDirection = (EdgeB - EdgeA) / EdgeLength;
	const FVector2D EdgeNormal(-EdgeDirection.Y, EdgeDirection.X);

	const FHFBeam* Deepest = nullptr;

	for (const FHFBeam& Beam : Beams)
	{
		const double BeamLength = Beam.Length();
		if (BeamLength <= UE_KINDA_SMALL_NUMBER)
		{
			continue;
		}

		const FVector2D Direction = (Beam.End - Beam.Start) / BeamLength;

		// ALONG THIS EDGE, not merely somewhere in the room. A beam running with a wall leaves the
		// nib that a ring exists to bury; one crossing the open middle of the room is a different
		// problem with a different answer - its own Bulkhead ceiling, which the validator asks for -
		// and giving it a ring on all four sides is how a room comes to be boxed in for nothing.
		if (FMath::Abs(FVector2D::DotProduct(Direction, EdgeDirection)) < 0.966)
		{
			continue;
		}

		// Its centreline has to be ON the edge line, within its own half width: the beam sits on the
		// wall the boundary is set out along.
		const double OffsetStart = FVector2D::DotProduct(Beam.Start - EdgeA, EdgeNormal);
		const double OffsetEnd = FVector2D::DotProduct(Beam.End - EdgeA, EdgeNormal);
		const double Reach = Beam.Width * 0.5 + Flush;

		if (FMath::Abs(OffsetStart) > Reach || FMath::Abs(OffsetEnd) > Reach)
		{
			continue;
		}

		// And it has to run along a real stretch of it, not clip a corner.
		const double AlongStart = FVector2D::DotProduct(Beam.Start - EdgeA, EdgeDirection);
		const double AlongEnd = FVector2D::DotProduct(Beam.End - EdgeA, EdgeDirection);

		const double OverlapFrom = FMath::Max(0.0, FMath::Min(AlongStart, AlongEnd));
		const double OverlapTo = FMath::Min(EdgeLength, FMath::Max(AlongStart, AlongEnd));

		if (OverlapTo - OverlapFrom <= Flush)
		{
			continue;
		}

		// Then the same test DeepestBeamOverRoom applies, restricted to the overlapping stretch: a
		// 230 beam on a 230 wall is flush and shows nothing however far it runs.
		bool bShows = false;
		for (int32 i = 0; i <= HFBeamSampleCount && !bShows; ++i)
		{
			const double T = static_cast<double>(i) / HFBeamSampleCount;
			const FVector2D Point = EdgeA + EdgeDirection * FMath::Lerp(OverlapFrom, OverlapTo, T);

			const double Inside = Room->ContainsPoint(Point)
				? HFDistanceToBoundary(*Room, Point)
				: -HFDistanceToBoundary(*Room, Point);

			const double Concealed = HFConcealingWallHalfThickness(Walls, Point, Direction, Flush);
			bShows = (Inside + Beam.Width * 0.5) > Concealed + Flush;
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
		Ceiling.InnerDrop *= Scale;
		Ceiling.CentrePanelDrop *= Scale;
		Ceiling.PerimeterBulkheadWidth *= Scale;
		Ceiling.PerimeterBulkheadDrop *= Scale;

		Ceiling.Cove.ChannelWidth *= Scale;
		Ceiling.Cove.LipHeight *= Scale;
		Ceiling.Cove.Setback *= Scale;
		Ceiling.Cove.StripWidth *= Scale;
		Ceiling.Cove.StripHeight *= Scale;
		Ceiling.Cove.StripSetback *= Scale;

		// The fitting is bought in millimetres and does not resize with the drawing, but every
		// other length here is in the spec's units and these have to match them or a downlight in
		// a millimetre spec is a 75 metre hole.
		Ceiling.Downlight.CutoutDiameter *= Scale;
		Ceiling.Downlight.FlangeDiameter *= Scale;
		Ceiling.Downlight.FlangeProjection *= Scale;
		Ceiling.Downlight.BodyDepth *= Scale;

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
