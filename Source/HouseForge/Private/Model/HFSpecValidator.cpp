// Copyright Siddartha G. All Rights Reserved.

#include "Model/HFSpecValidator.h"

namespace
{
	/** Minimum clear height under a false ceiling before the room becomes oppressive. */
	constexpr double MinHeadroomCm = 210.0;

	/** Fixture overlap below this fraction of the smaller footprint is treated as touching. */
	constexpr double OverlapToleranceRatio = 0.05;

	FString Describe(const FName& Id)
	{
		return Id.IsNone() ? FString(TEXT("<unnamed>")) : Id.ToString();
	}

	/** Appliances and sanitary ware that are set into, or mounted over, a cabinet run. */
	bool IsInsetFitting(EHFFixtureType Type)
	{
		return Type == EHFFixtureType::Sink
			|| Type == EHFFixtureType::Hob
			|| Type == EHFFixtureType::Chimney
			|| Type == EHFFixtureType::Basin;
	}

	/** Cabinet runs and worktops that inset fittings legitimately occupy. */
	bool IsCabinetRun(EHFFixtureType Type)
	{
		return Type == EHFFixtureType::CounterTop
			|| Type == EHFFixtureType::KitchenBaseCabinet
			|| Type == EHFFixtureType::KitchenWallCabinet
			|| Type == EHFFixtureType::Vanity;
	}

	/**
	 * A sink cut into a worktop, a hob dropped into a counter, a basin over a vanity - these are
	 * meant to overlap. Reporting them would train whoever reads the report to ignore the rule.
	 */
	bool IsExpectedOverlap(EHFFixtureType A, EHFFixtureType B)
	{
		return (IsInsetFitting(A) && IsCabinetRun(B)) || (IsCabinetRun(A) && IsInsetFitting(B));
	}

	/** The four corners of a fixture's footprint after rotation, in plan. */
	TArray<FVector2D, TInlineAllocator<4>> FootprintCorners(const FHFFixture& Fixture)
	{
		const double Radians = FMath::DegreesToRadians(Fixture.RotationDegrees);
		const double CosR = FMath::Cos(Radians);
		const double SinR = FMath::Sin(Radians);

		const double HalfW = Fixture.Footprint.X * 0.5;
		const double HalfD = Fixture.Footprint.Y * 0.5;

		TArray<FVector2D, TInlineAllocator<4>> Corners;
		for (int32 Corner = 0; Corner < 4; ++Corner)
		{
			const double LocalX = (Corner == 0 || Corner == 3) ? -HalfW : HalfW;
			const double LocalY = (Corner < 2) ? -HalfD : HalfD;

			Corners.Add(Fixture.Position + FVector2D(
				LocalX * CosR - LocalY * SinR,
				LocalX * SinR + LocalY * CosR));
		}
		return Corners;
	}

	/** Axis-aligned bounds of a rotated footprint. Cheap, and slightly generous on the diagonal. */
	FBox2D RotatedBounds(const FHFFixture& Fixture)
	{
		FBox2D Bounds(ForceInit);
		for (const FVector2D& Corner : FootprintCorners(Fixture))
		{
			Bounds += Corner;
		}
		return Bounds;
	}

	/** Reports any id used more than once, since later lookups would silently take the first. */
	template <typename ElementType, typename GetIdFunc>
	void CheckDuplicateIds(
		const TArray<ElementType>& Elements,
		const TCHAR* Kind,
		const FString& Code,
		GetIdFunc GetId,
		FHFValidationResult& Result)
	{
		TSet<FName> Seen;
		for (const ElementType& Element : Elements)
		{
			const FName Id = GetId(Element);

			if (Id.IsNone())
			{
				Result.Add(EHFValidationSeverity::Error, TEXT("MissingId"), Id,
					FString::Printf(TEXT("A %s has no id. Every element needs a unique id so openings, ceilings and fixtures can reference it."), Kind));
				continue;
			}

			bool bAlreadyPresent = false;
			Seen.Add(Id, &bAlreadyPresent);
			if (bAlreadyPresent)
			{
				Result.Add(EHFValidationSeverity::Error, Code, Id,
					FString::Printf(TEXT("Duplicate %s id '%s'. Ids must be unique within the spec."), Kind, *Id.ToString()));
			}
		}
	}
}

// ------------------------------------------------------------------------------- FHFValidationResult

void FHFValidationResult::Add(EHFValidationSeverity Severity, const FString& Code, const FName& ElementId, const FString& Message)
{
	FHFValidationIssue& Issue = Issues.AddDefaulted_GetRef();
	Issue.Severity = Severity;
	Issue.Code = Code;
	Issue.ElementId = ElementId;
	Issue.Message = Message;
}

bool FHFValidationResult::HasErrors() const
{
	return Issues.ContainsByPredicate([](const FHFValidationIssue& I) { return I.Severity == EHFValidationSeverity::Error; });
}

bool FHFValidationResult::HasWarnings() const
{
	return Issues.ContainsByPredicate([](const FHFValidationIssue& I) { return I.Severity == EHFValidationSeverity::Warning; });
}

int32 FHFValidationResult::CountOf(EHFValidationSeverity Severity) const
{
	int32 Count = 0;
	for (const FHFValidationIssue& Issue : Issues)
	{
		if (Issue.Severity == Severity)
		{
			++Count;
		}
	}
	return Count;
}

bool FHFValidationResult::Contains(const FString& Code) const
{
	return Issues.ContainsByPredicate([&Code](const FHFValidationIssue& I) { return I.Code == Code; });
}

FString FHFValidationResult::ToString() const
{
	if (Issues.IsEmpty())
	{
		return TEXT("Spec is valid: no issues found.");
	}

	TArray<FString> Lines;
	Lines.Add(FString::Printf(TEXT("Spec validation: %d error(s), %d warning(s)."),
		CountOf(EHFValidationSeverity::Error), CountOf(EHFValidationSeverity::Warning)));

	for (const FHFValidationIssue& Issue : Issues)
	{
		Lines.Add(FString::Printf(TEXT("  [%s] %s (%s): %s"),
			Issue.Severity == EHFValidationSeverity::Error ? TEXT("ERROR") : TEXT("WARN"),
			*Issue.Code,
			*Describe(Issue.ElementId),
			*Issue.Message));
	}

	return FString::Join(Lines, TEXT("\n"));
}

// -------------------------------------------------------------------------------- FHFSpecValidator

FHFValidationResult FHFSpecValidator::Validate(const FHFHouseSpec& Spec)
{
	FHFValidationResult Result;

	// ---------------------------------------------------------------------------- spec level
	if (Spec.SchemaVersion <= 0)
	{
		Result.Add(EHFValidationSeverity::Error, TEXT("BadSchemaVersion"), NAME_None,
			FString::Printf(TEXT("schemaVersion is %d; expected 1 or higher."), Spec.SchemaVersion));
	}

	if (Spec.Walls.IsEmpty())
	{
		Result.Add(EHFValidationSeverity::Error, TEXT("NoWalls"), NAME_None,
			TEXT("Spec has no walls. A house needs at least an enclosing shell."));
	}

	if (Spec.Rooms.IsEmpty())
	{
		Result.Add(EHFValidationSeverity::Error, TEXT("NoRooms"), NAME_None,
			TEXT("Spec has no rooms. Floors, ceilings and fixture placement all derive from rooms."));
	}

	CheckDuplicateIds(Spec.Walls, TEXT("wall"), TEXT("DuplicateWallId"), [](const FHFWall& W) { return W.Id; }, Result);
	CheckDuplicateIds(Spec.Rooms, TEXT("room"), TEXT("DuplicateRoomId"), [](const FHFRoom& R) { return R.Id; }, Result);
	CheckDuplicateIds(Spec.Openings, TEXT("opening"), TEXT("DuplicateOpeningId"), [](const FHFOpening& O) { return O.Id; }, Result);
	CheckDuplicateIds(Spec.Fixtures, TEXT("fixture"), TEXT("DuplicateFixtureId"), [](const FHFFixture& F) { return F.Id; }, Result);
	CheckDuplicateIds(Spec.FalseCeilings, TEXT("false ceiling"), TEXT("DuplicateCeilingId"), [](const FHFFalseCeiling& C) { return C.Id; }, Result);

	// -------------------------------------------------------------------------------- walls
	for (const FHFWall& Wall : Spec.Walls)
	{
		const double Length = Wall.Length();
		if (Length <= UE_KINDA_SMALL_NUMBER)
		{
			Result.Add(EHFValidationSeverity::Error, TEXT("ZeroLengthWall"), Wall.Id,
				FString::Printf(TEXT("Wall '%s' has zero length: start and end are both (%.1f, %.1f)."),
					*Describe(Wall.Id), Wall.Start.X, Wall.Start.Y));
		}

		if (Wall.Thickness <= 0.0)
		{
			Result.Add(EHFValidationSeverity::Error, TEXT("NonPositiveWallThickness"), Wall.Id,
				FString::Printf(TEXT("Wall '%s' has thickness %.2f; must be greater than zero."), *Describe(Wall.Id), Wall.Thickness));
		}

		if (Wall.Height <= 0.0)
		{
			Result.Add(EHFValidationSeverity::Error, TEXT("NonPositiveWallHeight"), Wall.Id,
				FString::Printf(TEXT("Wall '%s' has height %.2f; must be greater than zero."), *Describe(Wall.Id), Wall.Height));
		}
	}

	// ----------------------------------------------------------------------------- openings
	for (const FHFOpening& Opening : Spec.Openings)
	{
		const FHFWall* Wall = Spec.FindWall(Opening.WallId);
		if (Wall == nullptr)
		{
			Result.Add(EHFValidationSeverity::Error, TEXT("UnknownWallReference"), Opening.Id,
				FString::Printf(TEXT("Opening '%s' references wall '%s', which does not exist."),
					*Describe(Opening.Id), *Describe(Opening.WallId)));
			continue;
		}

		if (Opening.Width <= 0.0 || Opening.Height <= 0.0)
		{
			Result.Add(EHFValidationSeverity::Error, TEXT("NonPositiveOpeningSize"), Opening.Id,
				FString::Printf(TEXT("Opening '%s' is %.2f x %.2f; both width and height must be greater than zero."),
					*Describe(Opening.Id), Opening.Width, Opening.Height));
			continue;
		}

		// An opening that runs past either end of its wall would boolean away the wall's corner.
		const double WallLength = Wall->Length();
		const double Near = Opening.OffsetAlongWall - (Opening.Width * 0.5);
		const double Far  = Opening.OffsetAlongWall + (Opening.Width * 0.5);

		if (Near < -UE_KINDA_SMALL_NUMBER || Far > WallLength + UE_KINDA_SMALL_NUMBER)
		{
			Result.Add(EHFValidationSeverity::Error, TEXT("OpeningExceedsWall"), Opening.Id,
				FString::Printf(TEXT("Opening '%s' spans %.1f to %.1f along wall '%s', which is only %.1f long. Move it or narrow it."),
					*Describe(Opening.Id), Near, Far, *Describe(Wall->Id), WallLength));
		}

		if (Opening.HeadHeight() > Wall->Height + UE_KINDA_SMALL_NUMBER)
		{
			Result.Add(EHFValidationSeverity::Error, TEXT("OpeningExceedsWallHeight"), Opening.Id,
				FString::Printf(TEXT("Opening '%s' reaches %.1f high (sill %.1f + height %.1f) but wall '%s' is only %.1f tall."),
					*Describe(Opening.Id), Opening.HeadHeight(), Opening.SillHeight, Opening.Height,
					*Describe(Wall->Id), Wall->Height));
		}

		if (Opening.SillHeight < 0.0)
		{
			Result.Add(EHFValidationSeverity::Error, TEXT("NegativeSill"), Opening.Id,
				FString::Printf(TEXT("Opening '%s' has sill height %.2f; must not be negative."), *Describe(Opening.Id), Opening.SillHeight));
		}

		const bool bIsDoor = Opening.Kind == EHFOpeningKind::Door || Opening.Kind == EHFOpeningKind::SlidingDoor;
		if (bIsDoor && Opening.SillHeight > UE_KINDA_SMALL_NUMBER)
		{
			Result.Add(EHFValidationSeverity::Warning, TEXT("DoorWithSill"), Opening.Id,
				FString::Printf(TEXT("Door '%s' has a sill height of %.1f. Doors normally start at floor level; did you mean a window?"),
					*Describe(Opening.Id), Opening.SillHeight));
		}
	}

	// Two openings overlapping on one wall would boolean into a single ragged hole. Cheap to
	// check, and easy to author by accident when a ventilator is placed over a door.
	for (int32 i = 0; i < Spec.Openings.Num(); ++i)
	{
		const FHFOpening& A = Spec.Openings[i];
		if (A.Width <= 0.0)
		{
			continue;
		}

		for (int32 j = i + 1; j < Spec.Openings.Num(); ++j)
		{
			const FHFOpening& B = Spec.Openings[j];
			if (B.Width <= 0.0 || A.WallId != B.WallId || A.WallId.IsNone())
			{
				continue;
			}

			const double ANear = A.OffsetAlongWall - A.Width * 0.5;
			const double AFar  = A.OffsetAlongWall + A.Width * 0.5;
			const double BNear = B.OffsetAlongWall - B.Width * 0.5;
			const double BFar  = B.OffsetAlongWall + B.Width * 0.5;

			const bool bSeparatedAlongWall = AFar <= BNear + UE_KINDA_SMALL_NUMBER
										  || BFar <= ANear + UE_KINDA_SMALL_NUMBER;
			// Stacked openings - a ventilator sitting directly on a door head - are fine.
			const bool bSeparatedVertically = A.HeadHeight() <= B.SillHeight + UE_KINDA_SMALL_NUMBER
										   || B.HeadHeight() <= A.SillHeight + UE_KINDA_SMALL_NUMBER;

			if (!bSeparatedAlongWall && !bSeparatedVertically)
			{
				Result.Add(EHFValidationSeverity::Error, TEXT("OpeningsOverlap"), A.Id,
					FString::Printf(TEXT("Openings '%s' (%.0f-%.0f) and '%s' (%.0f-%.0f) overlap on wall '%s' and share a height range; they would cut one ragged hole."),
						*Describe(A.Id), ANear, AFar, *Describe(B.Id), BNear, BFar, *Describe(A.WallId)));
			}
		}
	}

	// -------------------------------------------------------------------------------- rooms
	for (const FHFRoom& Room : Spec.Rooms)
	{
		if (Room.Boundary.Num() < 3)
		{
			Result.Add(EHFValidationSeverity::Error, TEXT("UnclosedRoom"), Room.Id,
				FString::Printf(TEXT("Room '%s' has %d boundary points; a closed polygon needs at least 3. Do not repeat the first point - the closing edge is implicit."),
					*Describe(Room.Id), Room.Boundary.Num()));
			continue;
		}

		// A repeated first/last point is the most common way a hand-written boundary goes wrong:
		// it produces a zero-length edge that breaks polygon offset for the false ceiling.
		if (Room.Boundary.Num() >= 2 && Room.Boundary[0].Equals(Room.Boundary.Last(), UE_KINDA_SMALL_NUMBER))
		{
			Result.Add(EHFValidationSeverity::Error, TEXT("RepeatedClosingPoint"), Room.Id,
				FString::Printf(TEXT("Room '%s' repeats its first point at the end of the boundary. The closing edge is implicit; remove the duplicate."),
					*Describe(Room.Id)));
		}

		if (FMath::IsNearlyZero(Room.SignedArea()))
		{
			Result.Add(EHFValidationSeverity::Error, TEXT("DegenerateRoom"), Room.Id,
				FString::Printf(TEXT("Room '%s' encloses zero area; its boundary points are collinear or coincident."), *Describe(Room.Id)));
		}

		if (Room.CeilingHeight <= 0.0)
		{
			Result.Add(EHFValidationSeverity::Error, TEXT("NonPositiveCeilingHeight"), Room.Id,
				FString::Printf(TEXT("Room '%s' has ceiling height %.2f; must be greater than zero."), *Describe(Room.Id), Room.CeilingHeight));
		}

		if (Room.SkirtingHeight < 0.0)
		{
			Result.Add(EHFValidationSeverity::Error, TEXT("NegativeSkirting"), Room.Id,
				FString::Printf(TEXT("Room '%s' has skirting height %.2f; must not be negative."), *Describe(Room.Id), Room.SkirtingHeight));
		}
	}

	// ----------------------------------------------------------------------- false ceilings
	for (const FHFFalseCeiling& Ceiling : Spec.FalseCeilings)
	{
		const FHFRoom* Room = Spec.FindRoom(Ceiling.RoomId);
		if (Room == nullptr)
		{
			Result.Add(EHFValidationSeverity::Error, TEXT("UnknownRoomReference"), Ceiling.Id,
				FString::Printf(TEXT("False ceiling '%s' references room '%s', which does not exist."),
					*Describe(Ceiling.Id), *Describe(Ceiling.RoomId)));
			continue;
		}

		if (Ceiling.Style == EHFCeilingStyle::None)
		{
			continue;
		}

		if (Ceiling.Drop <= 0.0)
		{
			Result.Add(EHFValidationSeverity::Error, TEXT("NonPositiveCeilingDrop"), Ceiling.Id,
				FString::Printf(TEXT("False ceiling '%s' has drop %.2f; a false ceiling must hang below the slab."),
					*Describe(Ceiling.Id), Ceiling.Drop));
		}
		else if (Ceiling.Drop >= Room->CeilingHeight)
		{
			Result.Add(EHFValidationSeverity::Error, TEXT("CeilingDropExceedsRoom"), Ceiling.Id,
				FString::Printf(TEXT("False ceiling '%s' drops %.1f but room '%s' is only %.1f tall; it would land at or below the floor."),
					*Describe(Ceiling.Id), Ceiling.Drop, *Describe(Room->Id), Room->CeilingHeight));
		}
		else if (Room->CeilingHeight - Ceiling.Drop < MinHeadroomCm)
		{
			Result.Add(EHFValidationSeverity::Warning, TEXT("LowHeadroom"), Ceiling.Id,
				FString::Printf(TEXT("False ceiling '%s' leaves %.1f clear in room '%s', below the %.0f usually treated as minimum headroom."),
					*Describe(Ceiling.Id), Room->CeilingHeight - Ceiling.Drop, *Describe(Room->Id), MinHeadroomCm));
		}

		const bool bNeedsBand =
			Ceiling.Style == EHFCeilingStyle::Peripheral ||
			Ceiling.Style == EHFCeilingStyle::Cove ||
			Ceiling.Style == EHFCeilingStyle::Tray;

		if (bNeedsBand && Ceiling.BandWidth <= 0.0)
		{
			Result.Add(EHFValidationSeverity::Error, TEXT("MissingCeilingBand"), Ceiling.Id,
				FString::Printf(TEXT("False ceiling '%s' is a perimeter style but has band width %.2f; it would generate nothing."),
					*Describe(Ceiling.Id), Ceiling.BandWidth));
		}

		if (Ceiling.Style == EHFCeilingStyle::Bulkhead && Ceiling.ExplicitPolygon.Num() < 3)
		{
			Result.Add(EHFValidationSeverity::Error, TEXT("BulkheadNeedsPolygon"), Ceiling.Id,
				FString::Printf(TEXT("Bulkhead '%s' has %d polygon points. A bulkhead is a localised drop and needs its own polygon of at least 3 points."),
					*Describe(Ceiling.Id), Ceiling.ExplicitPolygon.Num()));
		}

		// A door that opens into the room must clear the false ceiling above it.
		for (const FHFOpening& Opening : Spec.Openings)
		{
			const bool bIsDoor = Opening.Kind == EHFOpeningKind::Door || Opening.Kind == EHFOpeningKind::SlidingDoor;
			if (!bIsDoor)
			{
				continue;
			}

			const FHFWall* Wall = Spec.FindWall(Opening.WallId);
			if (Wall == nullptr)
			{
				continue;
			}

			// Only meaningful for a full drop; a peripheral band may well sit clear of the door.
			const bool bCoversWholeRoom =
				Ceiling.Style == EHFCeilingStyle::FullDrop ||
				Ceiling.Style == EHFCeilingStyle::Tray;

			if (bCoversWholeRoom && Opening.HeadHeight() > Room->CeilingHeight - Ceiling.Drop + UE_KINDA_SMALL_NUMBER)
			{
				Result.Add(EHFValidationSeverity::Warning, TEXT("CeilingBelowDoorHead"), Ceiling.Id,
					FString::Printf(TEXT("False ceiling '%s' sits at %.1f but door '%s' reaches %.1f; the ceiling would cut through the door head."),
						*Describe(Ceiling.Id), Room->CeilingHeight - Ceiling.Drop, *Describe(Opening.Id), Opening.HeadHeight()));
			}
		}
	}

	// ----------------------------------------------------------------------------- fixtures
	for (const FHFFixture& Fixture : Spec.Fixtures)
	{
		const FHFRoom* Room = Spec.FindRoom(Fixture.RoomId);
		if (Room == nullptr)
		{
			Result.Add(EHFValidationSeverity::Error, TEXT("UnknownRoomReference"), Fixture.Id,
				FString::Printf(TEXT("Fixture '%s' references room '%s', which does not exist."),
					*Describe(Fixture.Id), *Describe(Fixture.RoomId)));
			continue;
		}

		if (Fixture.Type == EHFFixtureType::Unknown)
		{
			Result.Add(EHFValidationSeverity::Warning, TEXT("UnknownFixtureType"), Fixture.Id,
				FString::Printf(TEXT("Fixture '%s' has no type, so no generator will produce geometry for it."), *Describe(Fixture.Id)));
		}

		if (Fixture.Footprint.X <= 0.0 || Fixture.Footprint.Y <= 0.0)
		{
			Result.Add(EHFValidationSeverity::Error, TEXT("NonPositiveFootprint"), Fixture.Id,
				FString::Printf(TEXT("Fixture '%s' has footprint %.2f x %.2f; both dimensions must be greater than zero."),
					*Describe(Fixture.Id), Fixture.Footprint.X, Fixture.Footprint.Y));
		}

		if (Fixture.Height <= 0.0)
		{
			Result.Add(EHFValidationSeverity::Error, TEXT("NonPositiveFixtureHeight"), Fixture.Id,
				FString::Printf(TEXT("Fixture '%s' has height %.2f; must be greater than zero."), *Describe(Fixture.Id), Fixture.Height));
		}

		if (Room->Boundary.Num() >= 3 && !Room->ContainsPoint(Fixture.Position))
		{
			Result.Add(EHFValidationSeverity::Error, TEXT("FixtureOutsideRoom"), Fixture.Id,
				FString::Printf(TEXT("Fixture '%s' sits at (%.1f, %.1f), outside room '%s'. Check the position or the room it was assigned to."),
					*Describe(Fixture.Id), Fixture.Position.X, Fixture.Position.Y, *Describe(Room->Id)));
		}
		// A centred-but-oversized fixture passes the point test while its geometry pokes straight
		// through a wall. Wall-anchored fixtures are exempt: room boundaries run along wall
		// centrelines, so a wardrobe backing onto its wall is meant to cross the boundary.
		else if (Room->Boundary.Num() >= 3 && Fixture.AnchorWallId.IsNone() && !Fixture.IsCeilingMounted()
			&& Fixture.Footprint.X > 0.0 && Fixture.Footprint.Y > 0.0)
		{
			for (const FVector2D& Corner : FootprintCorners(Fixture))
			{
				if (!Room->ContainsPoint(Corner))
				{
					Result.Add(EHFValidationSeverity::Warning, TEXT("FixtureFootprintCrossesWall"), Fixture.Id,
						FString::Printf(TEXT("Fixture '%s' is centred inside room '%s' but its %.0f x %.0f footprint reaches (%.1f, %.1f), outside the room. It would intersect a wall."),
							*Describe(Fixture.Id), *Describe(Room->Id),
							Fixture.Footprint.X, Fixture.Footprint.Y, Corner.X, Corner.Y));
					break;
				}
			}
		}

		if (!Fixture.AnchorWallId.IsNone() && Spec.FindWall(Fixture.AnchorWallId) == nullptr)
		{
			Result.Add(EHFValidationSeverity::Error, TEXT("UnknownWallReference"), Fixture.Id,
				FString::Printf(TEXT("Fixture '%s' anchors to wall '%s', which does not exist."),
					*Describe(Fixture.Id), *Describe(Fixture.AnchorWallId)));
		}
	}

	// Overlapping fixtures are a warning, not an error: a chair tucked under a dining table
	// overlaps legitimately, and so does a hob set into a counter. Worth flagging, not blocking.
	for (int32 i = 0; i < Spec.Fixtures.Num(); ++i)
	{
		const FHFFixture& A = Spec.Fixtures[i];
		if (A.Footprint.X <= 0.0 || A.Footprint.Y <= 0.0 || A.IsCeilingMounted())
		{
			continue;
		}

		for (int32 j = i + 1; j < Spec.Fixtures.Num(); ++j)
		{
			const FHFFixture& B = Spec.Fixtures[j];
			if (B.Footprint.X <= 0.0 || B.Footprint.Y <= 0.0 || B.IsCeilingMounted())
			{
				continue;
			}

			if (IsExpectedOverlap(A.Type, B.Type))
			{
				continue;
			}

			// Fixtures stacked vertically - a wall cabinet over a counter - do not overlap.
			const double ATop = A.BaseZ + A.Height;
			const double BTop = B.BaseZ + B.Height;
			if (ATop <= B.BaseZ + UE_KINDA_SMALL_NUMBER || BTop <= A.BaseZ + UE_KINDA_SMALL_NUMBER)
			{
				continue;
			}

			const FBox2D BoundsA = RotatedBounds(A);
			const FBox2D BoundsB = RotatedBounds(B);
			if (!BoundsA.Intersect(BoundsB))
			{
				continue;
			}

			const FBox2D Overlap = BoundsA.Overlap(BoundsB);
			const double OverlapArea = Overlap.GetArea();
			const double SmallerArea = FMath::Min(BoundsA.GetArea(), BoundsB.GetArea());

			if (SmallerArea > 0.0 && (OverlapArea / SmallerArea) > OverlapToleranceRatio)
			{
				Result.Add(EHFValidationSeverity::Warning, TEXT("OverlappingFixtures"), A.Id,
					FString::Printf(TEXT("Fixtures '%s' and '%s' overlap by %.0f%% of the smaller footprint and share a height range."),
						*Describe(A.Id), *Describe(B.Id), (OverlapArea / SmallerArea) * 100.0));
			}
		}
	}

	return Result;
}
