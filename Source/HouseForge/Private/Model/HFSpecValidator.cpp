// Copyright Siddartha G. All Rights Reserved.

#include "Model/HFSpecValidator.h"

namespace
{
	// The two thresholds that used to live here are now FHFValidationLimits, because they are
	// project conventions rather than laws of geometry. Everything else this file checks is
	// structural and stays hardcoded.

	FString Describe(const FName& Id)
	{
		return Id.IsNone() ? FString(TEXT("<unnamed>")) : Id.ToString();
	}

	/**
	 * Where a closed boundary crosses itself, if it does.
	 *
	 * A plain sweep over every pair of non-adjacent edges. A room has a dozen corners at most, so
	 * the quadratic cost is nothing, and the alternative - trusting the boundary and finding out
	 * downstream - is a floor with no triangles in it and no message anywhere.
	 *
	 * Edges that merely share an endpoint are skipped: consecutive edges of any polygon touch by
	 * construction, and the closing edge touches the first.
	 */
	bool FindSelfIntersection(const TArray<FVector2D>& Boundary, FVector2D& OutWhere)
	{
		const int32 Count = Boundary.Num();
		if (Count < 4)
		{
			// Three edges cannot cross without two of them being collinear, which the zero-area
			// check already reports.
			return false;
		}

		// Sign of the area of the triangle ABC: which side of AB the point C lies on.
		auto Side = [](const FVector2D& A, const FVector2D& B, const FVector2D& C)
		{
			const double Cross = (B.X - A.X) * (C.Y - A.Y) - (B.Y - A.Y) * (C.X - A.X);
			return FMath::IsNearlyZero(Cross, UE_KINDA_SMALL_NUMBER) ? 0.0 : FMath::Sign(Cross);
		};

		for (int32 i = 0; i < Count; ++i)
		{
			const FVector2D& A0 = Boundary[i];
			const FVector2D& A1 = Boundary[(i + 1) % Count];

			for (int32 j = i + 1; j < Count; ++j)
			{
				// Adjacent edges, and the closing edge against the first, share an endpoint.
				if (j == i + 1 || (i == 0 && j == Count - 1))
				{
					continue;
				}

				const FVector2D& B0 = Boundary[j];
				const FVector2D& B1 = Boundary[(j + 1) % Count];

				// Proper crossing only: each segment strictly straddles the other's line. Touching
				// at a point is reported by the zero-area and repeated-point checks instead, and
				// treating it as a crossing here would flag every polygon with a collinear corner.
				const double D0 = Side(A0, A1, B0);
				const double D1 = Side(A0, A1, B1);
				const double D2 = Side(B0, B1, A0);
				const double D3 = Side(B0, B1, A1);

				if (D0 * D1 < 0.0 && D2 * D3 < 0.0)
				{
					// The crossing point, for a message somebody can act on.
					const double Denominator = (A1.X - A0.X) * (B1.Y - B0.Y) - (A1.Y - A0.Y) * (B1.X - B0.X);
					const double T = FMath::IsNearlyZero(Denominator)
						? 0.5
						: ((B0.X - A0.X) * (B1.Y - B0.Y) - (B0.Y - A0.Y) * (B1.X - B0.X)) / Denominator;

					OutWhere = A0 + (A1 - A0) * T;
					return true;
				}
			}
		}

		return false;
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

FHFValidationResult FHFSpecValidator::Validate(const FHFHouseSpec& Spec,
	const FHFValidationLimits& Limits)
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
	CheckDuplicateIds(Spec.Beams, TEXT("beam"), TEXT("DuplicateBeamId"), [](const FHFBeam& B) { return B.Id; }, Result);
	CheckDuplicateIds(Spec.Columns, TEXT("column"), TEXT("DuplicateColumnId"), [](const FHFColumn& C) { return C.Id; }, Result);

	// -------------------------------------------------------------------- plausibility of scale
	// A unit misread is uniquely dangerous: it leaves the spec perfectly self-consistent - every
	// wall still meets, every opening still fits - and simply builds the house at the wrong scale.
	// No structural rule can catch it. Only asking "is this the size of a dwelling?" can.
	{
		const double Scale = FHFUnits::ToCentimeterScale(Spec.Units);
		const double AreaSqM = (Spec.TotalFloorArea() * Scale * Scale) / 10'000.0;

		constexpr double MinPlausibleSqM = 8.0;
		constexpr double MaxPlausibleSqM = 2000.0;

		if (!Spec.Rooms.IsEmpty() && (AreaSqM < MinPlausibleSqM || AreaSqM > MaxPlausibleSqM))
		{
			// Work out which unit would have made this plausible, and say so - that turns an
			// unhelpful "wrong size" into an actionable "you probably meant millimetres".
			FString Suggestion;
			for (const EHFUnits Candidate : { EHFUnits::Millimeters, EHFUnits::Centimeters,
											  EHFUnits::Meters, EHFUnits::Feet, EHFUnits::Inches })
			{
				if (Candidate == Spec.Units)
				{
					continue;
				}

				const double CandidateScale = FHFUnits::ToCentimeterScale(Candidate);
				const double CandidateArea = (Spec.TotalFloorArea() * CandidateScale * CandidateScale) / 10'000.0;
				if (CandidateArea >= MinPlausibleSqM && CandidateArea <= MaxPlausibleSqM)
				{
					Suggestion = FString::Printf(
						TEXT(" Read as %s the total would be %.1f sq m, which is plausible - check the drawing's title block."),
						*FHFUnits::ShortName(Candidate), CandidateArea);
					break;
				}
			}

			Result.Add(EHFValidationSeverity::Error, TEXT("ImplausibleScale"), NAME_None,
				FString::Printf(TEXT("Total floor area is %.2f sq m with units declared as %s, which is not a plausible dwelling.%s"),
					AreaSqM, *FHFUnits::ShortName(Spec.Units), *Suggestion));
		}

		if (Spec.UnitsSource.IsEmpty())
		{
			Result.Add(EHFValidationSeverity::Warning, TEXT("MissingUnitsSource"), NAME_None,
				TEXT("unitsSource is blank. Record where the units were read from on the drawing - a title block note, a dimension string, a scale bar - so the units are read rather than assumed."));
		}

		// Per-element sanity. These catch a single mistyped figure, which the aggregate check
		// above would average away.
		for (const FHFRoom& Room : Spec.Rooms)
		{
			const double HeightCm = Room.CeilingHeight * Scale;
			if (HeightCm > 0.0 && (HeightCm < 200.0 || HeightCm > 500.0))
			{
				Result.Add(EHFValidationSeverity::Warning, TEXT("ImplausibleCeilingHeight"), Room.Id,
					FString::Printf(TEXT("Room '%s' has a ceiling height of %.0f cm; dwellings are normally 240 to 400."),
						*Describe(Room.Id), HeightCm));
			}
		}

		for (const FHFWall& Wall : Spec.Walls)
		{
			const double ThicknessCm = Wall.Thickness * Scale;
			if (ThicknessCm > 0.0 && (ThicknessCm < 4.0 || ThicknessCm > 60.0))
			{
				Result.Add(EHFValidationSeverity::Warning, TEXT("ImplausibleWallThickness"), Wall.Id,
					FString::Printf(TEXT("Wall '%s' is %.1f cm thick; partitions are normally 8 to 30."),
						*Describe(Wall.Id), ThicknessCm));
			}
		}

		for (const FHFOpening& Opening : Spec.Openings)
		{
			const bool bIsDoor = Opening.Kind == EHFOpeningKind::Door || Opening.Kind == EHFOpeningKind::SlidingDoor;
			if (!bIsDoor)
			{
				continue;
			}

			const double WidthCm = Opening.Width * Scale;
			const double HeightCm = Opening.Height * Scale;

			if (WidthCm > 0.0 && (WidthCm < 45.0 || WidthCm > 250.0))
			{
				Result.Add(EHFValidationSeverity::Warning, TEXT("ImplausibleDoorSize"), Opening.Id,
					FString::Printf(TEXT("Door '%s' is %.0f cm wide; doors are normally 60 to 120."),
						*Describe(Opening.Id), WidthCm));
			}
			if (HeightCm > 0.0 && (HeightCm < 160.0 || HeightCm > 300.0))
			{
				Result.Add(EHFValidationSeverity::Warning, TEXT("ImplausibleDoorSize"), Opening.Id,
					FString::Printf(TEXT("Door '%s' is %.0f cm tall; doors are normally 200 to 240."),
						*Describe(Opening.Id), HeightCm));
			}
		}
	}

	// ------------------------------------------------------------------------- door swings
	for (const FHFOpening& Opening : Spec.Openings)
	{
		if (Opening.Kind != EHFOpeningKind::Door)
		{
			continue;
		}

		if (Opening.Swing == EHFSwing::None)
		{
			Result.Add(EHFValidationSeverity::Warning, TEXT("MissingSwing"), Opening.Id,
				FString::Printf(TEXT("Door '%s' has no swing direction. Read the swing arc on the plan; a door hung on the wrong side is invisible in a top-down view."),
					*Describe(Opening.Id)));
			continue;
		}

		const FHFWall* Wall = Spec.FindWall(Opening.WallId);
		if (Wall == nullptr || Wall->Length() <= UE_KINDA_SMALL_NUMBER)
		{
			continue;
		}

		// The leaf sweeps perpendicular to the wall. If its tip lands outside every room, the
		// door opens into solid construction or into nothing - a misread swing arc.
		const FVector2D Direction = (Wall->End - Wall->Start) / Wall->Length();
		const FVector2D Normal(-Direction.Y, Direction.X);
		const double Side = (Opening.Swing == EHFSwing::InwardLeft || Opening.Swing == EHFSwing::InwardRight) ? 1.0 : -1.0;

		const FVector2D Hinge = Wall->Start + Direction * Opening.OffsetAlongWall;
		const FVector2D LeafTip = Hinge + Normal * (Opening.Width * 0.9 * Side);

		const bool bOpensIntoARoom = Spec.Rooms.ContainsByPredicate(
			[&LeafTip](const FHFRoom& Room)
			{
				return Room.Boundary.Num() >= 3 && Room.ContainsPoint(LeafTip);
			});

		if (!bOpensIntoARoom)
		{
			Result.Add(EHFValidationSeverity::Warning, TEXT("SwingBlocked"), Opening.Id,
				FString::Printf(TEXT("Door '%s' swings %s but its leaf reaches (%.0f, %.0f), which is not inside any room; it would open into solid construction."),
					*Describe(Opening.Id),
					Side > 0.0 ? TEXT("inward") : TEXT("outward"),
					LeafTip.X, LeafTip.Y));
		}
	}

	// -------------------------------------------------------------------------------- beams
	for (const FHFBeam& Beam : Spec.Beams)
	{
		if (Beam.Length() <= UE_KINDA_SMALL_NUMBER)
		{
			Result.Add(EHFValidationSeverity::Error, TEXT("ZeroLengthBeam"), Beam.Id,
				FString::Printf(TEXT("Beam '%s' has zero length: start and end are both (%.1f, %.1f)."),
					*Describe(Beam.Id), Beam.Start.X, Beam.Start.Y));
		}

		if (Beam.Width <= 0.0 || Beam.Depth <= 0.0)
		{
			Result.Add(EHFValidationSeverity::Error, TEXT("NonPositiveBeamSize"), Beam.Id,
				FString::Printf(TEXT("Beam '%s' is %.2f wide by %.2f deep; both must be greater than zero."),
					*Describe(Beam.Id), Beam.Width, Beam.Depth));
			continue;
		}

		if (Beam.Depth >= Beam.SoffitZ)
		{
			Result.Add(EHFValidationSeverity::Error, TEXT("BeamDepthExceedsStorey"), Beam.Id,
				FString::Printf(TEXT("Beam '%s' hangs %.1f below a soffit at %.1f; it would reach the floor."),
					*Describe(Beam.Id), Beam.Depth, Beam.SoffitZ));
		}
		else if (Beam.ClearHeight() < Limits.MinHeadroomCm)
		{
			Result.Add(EHFValidationSeverity::Warning, TEXT("BeamLowHeadroom"), Beam.Id,
				FString::Printf(TEXT("Beam '%s' leaves %.1f clear beneath it, below the %.0f usually treated as minimum headroom."),
					*Describe(Beam.Id), Beam.ClearHeight(), Limits.MinHeadroomCm));
		}
	}

	// ------------------------------------------------------------------------------ columns
	for (const FHFColumn& Column : Spec.Columns)
	{
		if (Column.Size.X <= 0.0 || Column.Size.Y <= 0.0)
		{
			Result.Add(EHFValidationSeverity::Error, TEXT("NonPositiveColumnSize"), Column.Id,
				FString::Printf(TEXT("Column '%s' is %.2f x %.2f in plan; both dimensions must be greater than zero."),
					*Describe(Column.Id), Column.Size.X, Column.Size.Y));
		}

		if (Column.Height <= 0.0)
		{
			Result.Add(EHFValidationSeverity::Error, TEXT("NonPositiveColumnHeight"), Column.Id,
				FString::Printf(TEXT("Column '%s' has height %.2f; must be greater than zero."),
					*Describe(Column.Id), Column.Height));
		}
	}

	// ------------------------------------------------------------- columns inside openings
	//
	// A doorway cut partly through a column cannot be built and cannot be walked through, and
	// nothing else here catches it: OpeningsOverlap compares openings only, and SwingBlocked asks
	// where the leaf ENDS UP rather than whether the hole is clear. It is exactly the misread a
	// drawing invites, because a column on a grid line and a door beside it are drawn as separate
	// things and read as separate things.
	//
	// The reference 2BHK has one: D_Bed2's doorway overlaps COL_M1 by 75 mm, which reached the
	// geometry as a leaf embedded in a column and was visible only in a sweep test's warning.
	{
		const double ScaleToCm = FHFUnits::ToCentimeterScale(Spec.Units);

		// A column whose face lands exactly on a jamb is normal construction, so only a real bite
		// out of the clear opening is reported.
		const double TouchTolerance = (ScaleToCm > 0.0) ? 1.0 / ScaleToCm : 0.0;

		for (const FHFOpening& Opening : Spec.Openings)
		{
			const FHFWall* Wall = Spec.FindWall(Opening.WallId);
			if (Wall == nullptr || Wall->Length() <= UE_KINDA_SMALL_NUMBER ||
				Opening.Width <= 0.0 || Opening.Height <= 0.0)
			{
				continue;
			}

			const FVector2D Direction = (Wall->End - Wall->Start) / Wall->Length();
			const FVector2D Normal(-Direction.Y, Direction.X);

			const double OpeningMin = Opening.OffsetAlongWall - Opening.Width * 0.5;
			const double OpeningMax = Opening.OffsetAlongWall + Opening.Width * 0.5;
			const double SillZ = Wall->BaseZ + Opening.SillHeight;
			const double HeadZ = SillZ + Opening.Height;

			for (const FHFColumn& Column : Spec.Columns)
			{
				if (Column.Size.X <= 0.0 || Column.Size.Y <= 0.0 || Column.Height <= 0.0)
				{
					continue;
				}

				// The column's four corners, projected onto the wall's own axes.
				const double Radians = FMath::DegreesToRadians(Column.RotationDegrees);
				const FVector2D AxisX(FMath::Cos(Radians), FMath::Sin(Radians));
				const FVector2D AxisY(-AxisX.Y, AxisX.X);

				double AlongMin = TNumericLimits<double>::Max();
				double AlongMax = -TNumericLimits<double>::Max();
				double AcrossMin = TNumericLimits<double>::Max();
				double AcrossMax = -TNumericLimits<double>::Max();

				for (const double SignX : { -0.5, 0.5 })
				{
					for (const double SignY : { -0.5, 0.5 })
					{
						const FVector2D Corner = Column.Position
							+ AxisX * (Column.Size.X * SignX)
							+ AxisY * (Column.Size.Y * SignY);
						const FVector2D Relative = Corner - Wall->Start;

						const double Along = FVector2D::DotProduct(Relative, Direction);
						const double Across = FVector2D::DotProduct(Relative, Normal);

						AlongMin = FMath::Min(AlongMin, Along);
						AlongMax = FMath::Max(AlongMax, Along);
						AcrossMin = FMath::Min(AcrossMin, Across);
						AcrossMax = FMath::Max(AcrossMax, Across);
					}
				}

				// It has to be in this wall's thickness, in the opening's span, and at its height.
				const double HalfThickness = Wall->Thickness * 0.5;
				if (AcrossMax <= -HalfThickness || AcrossMin >= HalfThickness)
				{
					continue;
				}

				if (Column.BaseZ >= HeadZ || Column.BaseZ + Column.Height <= SillZ)
				{
					continue;
				}

				const double Overlap =
					FMath::Min(AlongMax, OpeningMax) - FMath::Max(AlongMin, OpeningMin);

				if (Overlap > TouchTolerance)
				{
					Result.Add(EHFValidationSeverity::Warning, TEXT("OpeningBlockedByColumn"), Opening.Id,
						FString::Printf(TEXT("Opening '%s' overlaps column '%s' by %.1f cm of its %.1f cm width; the column stands inside the clear opening. Move the opening clear of the column, or the column off the opening."),
							*Describe(Opening.Id), *Describe(Column.Id),
							Overlap * ScaleToCm, Opening.Width * ScaleToCm));
				}
			}
		}
	}

	// A fixture standing in front of an opening.
	//
	// The same misread as a column in a doorway, one layer out: a wardrobe against the east wall and
	// a window in the east wall are drawn as separate things and read as separate things, and until
	// now nothing noticed that one was in front of the other. A wall solid is swept against; a
	// fixture never was.
	//
	// It matters more since a window became a sliding unit with a catch somebody reaches for. Fixed
	// glazing behind a wardrobe merely looks odd; a sash behind one cannot be opened at all, and the
	// window photographs as a cabinet.
	{
		const double ScaleToCm = FHFUnits::ToCentimeterScale(Spec.Units);
		const double Obstruction = (ScaleToCm > 0.0) ? Limits.MinOpeningObstructionCm / ScaleToCm : 0.0;

		for (const FHFOpening& Opening : Spec.Openings)
		{
			const FHFWall* Wall = Spec.FindWall(Opening.WallId);
			if (Wall == nullptr || Wall->Length() <= UE_KINDA_SMALL_NUMBER ||
				Opening.Width <= 0.0 || Opening.Height <= 0.0)
			{
				continue;
			}

			const FVector2D Direction = (Wall->End - Wall->Start) / Wall->Length();
			const FVector2D Normal(-Direction.Y, Direction.X);
			const double HalfThickness = Wall->Thickness * 0.5;

			const double OpeningMin = Opening.OffsetAlongWall - Opening.Width * 0.5;
			const double OpeningMax = Opening.OffsetAlongWall + Opening.Width * 0.5;
			const double SillZ = Wall->BaseZ + Opening.SillHeight;
			const double HeadZ = SillZ + Opening.Height;

			for (const FHFFixture& Fixture : Spec.Fixtures)
			{
				// A ceiling-mounted fixture measures its BaseZ down from the ceiling rather than up
				// from the floor, so its height range cannot be compared with a sill without the room
				// it hangs in. Nothing that hangs from a ceiling stands in front of a window anyway.
				if (Fixture.Footprint.X <= 0.0 || Fixture.Footprint.Y <= 0.0 || Fixture.Height <= 0.0
					|| Fixture.IsCeilingMounted())
				{
					continue;
				}

				double AlongMin = TNumericLimits<double>::Max();
				double AlongMax = -TNumericLimits<double>::Max();
				double AcrossMin = TNumericLimits<double>::Max();
				double AcrossMax = -TNumericLimits<double>::Max();

				for (const FVector2D& Corner : FootprintCorners(Fixture))
				{
					const FVector2D Relative = Corner - Wall->Start;
					const double Along = FVector2D::DotProduct(Relative, Direction);
					const double Across = FVector2D::DotProduct(Relative, Normal);

					AlongMin = FMath::Min(AlongMin, Along);
					AlongMax = FMath::Max(AlongMax, Along);
					AcrossMin = FMath::Min(AcrossMin, Across);
					AcrossMax = FMath::Max(AcrossMax, Across);
				}

				// It has to be standing against THIS wall. A dining table out in the middle of the
				// room is in front of the window in a photograph and in nobody's way in fact.
				//
				// Two ways of being against it, because a position read off a plan is not exact: the
				// fixture says so, or its footprint reaches the wall's own slab. The declaration is
				// what catches the near miss - the reference flat's TV unit is anchored to the south
				// wall and drawn 60 mm proud of its face, which is a rounding on a drawing and still
				// a cabinet in front of whatever is behind it.
				const bool bAgainstThisWall =
					(!Fixture.AnchorWallId.IsNone() && Fixture.AnchorWallId == Wall->Id) ||
					(AcrossMin <= HalfThickness && AcrossMax >= -HalfThickness);

				if (!bAgainstThisWall)
				{
					continue;
				}

				const double AlongOverlap =
					FMath::Min(AlongMax, OpeningMax) - FMath::Max(AlongMin, OpeningMin);
				const double UpOverlap =
					FMath::Min(Fixture.BaseZ + Fixture.Height, HeadZ) - FMath::Max(Fixture.BaseZ, SillZ);

				// Both, or a pelmet sitting on a window head and a base unit stopping at a sill would
				// each read as an obstruction rather than as the neat detail they are.
				if (AlongOverlap <= Obstruction || UpOverlap <= Obstruction)
				{
					continue;
				}

				const double Covered = (AlongOverlap * UpOverlap) / (Opening.Width * Opening.Height);

				Result.Add(EHFValidationSeverity::Warning, TEXT("OpeningBlockedByFixture"), Opening.Id,
					FString::Printf(TEXT("Fixture '%s' stands across %.0f%% of opening '%s': %.1f cm of its %.1f cm width and %.1f cm of its %.1f cm height. Move one clear of the other, or split the run around the opening."),
						*Describe(Fixture.Id), Covered * 100.0, *Describe(Opening.Id),
						AlongOverlap * ScaleToCm, Opening.Width * ScaleToCm,
						UpOverlap * ScaleToCm, Opening.Height * ScaleToCm));
			}
		}
	}

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

		// A boundary that crosses itself - a bow-tie or a figure-eight - is an ordinary mis-read of
		// a plan, not abuse: it passes every check above, since it has enough points, no repeated
		// closing point and a perfectly good area. Everything downstream then quietly declines it.
		// The triangulator produces nothing for a polygon that is not simple, so the room comes back
		// with a floor of no triangles while its skirting, emitted per edge, generates perfectly -
		// and a top-down view still shows the room outline, so the hole reads as an unfinished floor
		// rather than as a failure. Polygon offset, which every false ceiling depends on, is no
		// better defined on one.
		if (FVector2D Crossing; FindSelfIntersection(Room.Boundary, Crossing))
		{
			Result.Add(EHFValidationSeverity::Error, TEXT("SelfIntersectingRoom"), Room.Id,
				FString::Printf(TEXT("Room '%s' has a boundary that crosses itself near (%.1f, %.1f). Every triangulation, offset and inset downstream needs a simple polygon; check the order the corners are listed in."),
					*Describe(Room.Id), Crossing.X, Crossing.Y));
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
		else if (Room->CeilingHeight - Ceiling.Drop < Limits.MinHeadroomCm)
		{
			Result.Add(EHFValidationSeverity::Warning, TEXT("LowHeadroom"), Ceiling.Id,
				FString::Printf(TEXT("False ceiling '%s' leaves %.1f clear in room '%s', below the %.0f usually treated as minimum headroom."),
					*Describe(Ceiling.Id), Room->CeilingHeight - Ceiling.Drop, *Describe(Room->Id), Limits.MinHeadroomCm));
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

		// The whole point of a false ceiling here is to conceal the beams crossing the room. A
		// ceiling shallower than the deepest beam would leave it hanging through the finished
		// soffit, which is the single most common mistake when a ceiling drop is picked by eye.
		if (const FHFBeam* Beam = Spec.DeepestBeamOverRoom(Ceiling.RoomId))
		{
			if (Ceiling.Drop + UE_KINDA_SMALL_NUMBER < Beam->Depth)
			{
				// A peripheral band leaves the centre of the room at slab height, so it conceals
				// nothing mid-span. It is only excusable when a bulkhead in the same room boxes
				// the beam in - which is exactly how this is detailed in practice.
				const bool bIsPerimeterOnly =
					Ceiling.Style == EHFCeilingStyle::Peripheral ||
					Ceiling.Style == EHFCeilingStyle::Cove;

				const bool bBulkheadCoversIt = Spec.FalseCeilings.ContainsByPredicate(
					[&Ceiling, Beam](const FHFFalseCeiling& Other)
					{
						return Other.RoomId == Ceiling.RoomId
							&& Other.Style == EHFCeilingStyle::Bulkhead
							&& Other.Drop + UE_KINDA_SMALL_NUMBER >= Beam->Depth;
					});

				if (!(bIsPerimeterOnly && bBulkheadCoversIt))
				{
					Result.Add(EHFValidationSeverity::Warning, TEXT("CeilingDoesNotClearBeam"), Ceiling.Id,
						FString::Printf(TEXT("False ceiling '%s' drops %.1f but beam '%s' over room '%s' hangs %.1f; the beam would show through the soffit. Deepen the ceiling or add a bulkhead over the beam."),
							*Describe(Ceiling.Id), Ceiling.Drop, *Describe(Beam->Id), *Describe(Room->Id), Beam->Depth));
				}
			}
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

			if (SmallerArea > 0.0 && (OverlapArea / SmallerArea) > Limits.FixtureOverlapToleranceRatio)
			{
				Result.Add(EHFValidationSeverity::Warning, TEXT("OverlappingFixtures"), A.Id,
					FString::Printf(TEXT("Fixtures '%s' and '%s' overlap by %.0f%% of the smaller footprint and share a height range."),
						*Describe(A.Id), *Describe(B.Id), (OverlapArea / SmallerArea) * 100.0));
			}
		}
	}

	return Result;
}
