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

	/**
	 * True if the segment A->B touches the oriented rectangle at all.
	 *
	 * Liang-Barsky in the rectangle's own frame: clip the segment's parameter range against each
	 * slab in turn and see whether anything survives. Exact, unlike sampling the segment, which
	 * misses a wall that clips a column's corner - and a column caught by a corner is exactly the
	 * marginal case a rule about columns standing free has to get right.
	 */
	bool SegmentHitsRect(const FVector2D& A, const FVector2D& B,
		const FVector2D& Centre, const FVector2D& Size, double RotationDegrees)
	{
		const double Radians = FMath::DegreesToRadians(RotationDegrees);
		const FVector2D AxisX(FMath::Cos(Radians), FMath::Sin(Radians));
		const FVector2D AxisY(-AxisX.Y, AxisX.X);

		auto ToLocal = [&AxisX, &AxisY, &Centre](const FVector2D& Point)
		{
			const FVector2D Relative = Point - Centre;
			return FVector2D(FVector2D::DotProduct(Relative, AxisX), FVector2D::DotProduct(Relative, AxisY));
		};

		const FVector2D LocalA = ToLocal(A);
		const FVector2D LocalB = ToLocal(B);

		const double Start[2] = { LocalA.X, LocalA.Y };
		const double Delta[2] = { LocalB.X - LocalA.X, LocalB.Y - LocalA.Y };
		const double Half[2]  = { Size.X * 0.5, Size.Y * 0.5 };

		double TMin = 0.0;
		double TMax = 1.0;

		for (int32 Axis = 0; Axis < 2; ++Axis)
		{
			const double Low = -Half[Axis] - Start[Axis];
			const double High = Half[Axis] - Start[Axis];

			if (FMath::IsNearlyZero(Delta[Axis]))
			{
				// Parallel to this pair of edges: either wholly within the slab or wholly outside.
				if (Low > 0.0 || High < 0.0)
				{
					return false;
				}
				continue;
			}

			double T0 = Low / Delta[Axis];
			double T1 = High / Delta[Axis];
			if (T0 > T1)
			{
				Swap(T0, T1);
			}

			TMin = FMath::Max(TMin, T0);
			TMax = FMath::Min(TMax, T1);

			if (TMin > TMax)
			{
				return false;
			}
		}

		return true;
	}

	/** A half-open interval along some axis. Used for the runs of a doorway a fixture eats into. */
	struct FInterval
	{
		double Min = 0.0;
		double Max = 0.0;
	};

	/**
	 * Whether two rotated rectangles overlap in plan, by separating axis.
	 *
	 * Four axes suffice for two boxes - the two edge normals of each - and finding any one that
	 * separates them settles it. Bounding boxes would be wrong here in the direction that matters:
	 * they report clashes that are not there, and a rule that cries wolf about a fixture touching a
	 * column is one whose real reports stop being read.
	 */
	bool OrientedBoxesOverlap(
		const FVector2D& CentreA, const FVector2D& SizeA, double RotationA,
		const FVector2D& CentreB, const FVector2D& SizeB, double RotationB)
	{
		const double RadiansA = FMath::DegreesToRadians(RotationA);
		const double RadiansB = FMath::DegreesToRadians(RotationB);

		const FVector2D AxisA0(FMath::Cos(RadiansA), FMath::Sin(RadiansA));
		const FVector2D AxisA1(-AxisA0.Y, AxisA0.X);
		const FVector2D AxisB0(FMath::Cos(RadiansB), FMath::Sin(RadiansB));
		const FVector2D AxisB1(-AxisB0.Y, AxisB0.X);

		const FVector2D Between = CentreB - CentreA;
		const FVector2D Axes[4] = { AxisA0, AxisA1, AxisB0, AxisB1 };

		for (const FVector2D& Axis : Axes)
		{
			// Each box's extent along this axis is the sum of its half-sizes projected onto it.
			const double ReachA =
				FMath::Abs(FVector2D::DotProduct(AxisA0, Axis)) * SizeA.X * 0.5 +
				FMath::Abs(FVector2D::DotProduct(AxisA1, Axis)) * SizeA.Y * 0.5;
			const double ReachB =
				FMath::Abs(FVector2D::DotProduct(AxisB0, Axis)) * SizeB.X * 0.5 +
				FMath::Abs(FVector2D::DotProduct(AxisB1, Axis)) * SizeB.Y * 0.5;

			// Touching is not overlapping: a fixture set flush against a column shares a face with
			// it by design, and every wall-anchored fixture in a plan does exactly that.
			if (FMath::Abs(FVector2D::DotProduct(Between, Axis)) >= ReachA + ReachB - UE_KINDA_SMALL_NUMBER)
			{
				return false;
			}
		}

		return true;
	}

	/** A stretch of a beam that has something under it, measured along the beam from its Start. */
	struct FSupportedRun
	{
		double Min = 0.0;
		double Max = 0.0;
	};

	/**
	 * The stretches of a beam that are carried by something.
	 *
	 * Two things count, and both are things this data model can say for certain hold load up:
	 *
	 * A WALL RUNNING UNDER THE BEAM, along its whole shared length. This is the ordinary case and
	 * every beam in the reference flat is one: the beam is on the frame's grid line, the wall is on
	 * the same line, and the wall conceals the beam so nothing shows in the room either.
	 *
	 * A COLUMN UNDER THE BEAM, at a point.
	 *
	 * What deliberately does NOT count is another beam. A secondary beam framing into a primary is
	 * real construction, but it is a decision somebody has to make - the primary has to be sized for
	 * the point load - and none of that is in a spec. Accepting it would accept BM_Living_Cross,
	 * which landed on the mid-span of two beams and crossed the middle of the living room.
	 *
	 * Nor does a wall CROSSING the beam at an angle. In an RCC-framed flat the beams carry the
	 * walls, not the other way round; every wall in a spec like this is infill masonry under a slab,
	 * and treating a partition as a support inverts the load path. A 115 brick wall does not carry a
	 * 6.6 m beam's reaction, and nothing in FHFWall says which walls are meant to.
	 */
	void GatherBeamSupports(const FHFHouseSpec& Spec, const FHFBeam& Beam, TArray<FSupportedRun>& OutRuns)
	{
		const double Length = Beam.Length();
		if (Length <= UE_KINDA_SMALL_NUMBER)
		{
			return;
		}

		const FVector2D Direction = (Beam.End - Beam.Start) / Length;
		const FVector2D Normal(-Direction.Y, Direction.X);

		// The beam's own footprint. A support has to be under the beam, not beside it.
		const double HalfWidth = Beam.Width * 0.5;

		auto AddClippedRun = [&OutRuns, Length](double A, double B)
		{
			const double Min = FMath::Max(FMath::Min(A, B), 0.0);
			const double Max = FMath::Min(FMath::Max(A, B), Length);
			if (Max >= Min)
			{
				OutRuns.Add({ Min, Max });
			}
		};

		for (const FHFWall& Wall : Spec.Walls)
		{
			if (Wall.Length() <= UE_KINDA_SMALL_NUMBER)
			{
				continue;
			}

			const FVector2D StartRelative = Wall.Start - Beam.Start;
			const FVector2D EndRelative = Wall.End - Beam.Start;

			// Both ends of the wall inside the beam's footprint band, which is a parallelism test
			// and an offset test at once: a wall that crosses the beam has one end far outside.
			if (FMath::Abs(FVector2D::DotProduct(StartRelative, Normal)) > HalfWidth ||
				FMath::Abs(FVector2D::DotProduct(EndRelative, Normal)) > HalfWidth)
			{
				continue;
			}

			AddClippedRun(
				FVector2D::DotProduct(StartRelative, Direction),
				FVector2D::DotProduct(EndRelative, Direction));
		}

		for (const FHFColumn& Column : Spec.Columns)
		{
			if (Column.Size.X <= 0.0 || Column.Size.Y <= 0.0 || Column.Height <= 0.0)
			{
				continue;
			}

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
					const FVector2D Relative = Column.Position
						+ AxisX * (Column.Size.X * SignX)
						+ AxisY * (Column.Size.Y * SignY)
						- Beam.Start;

					const double Along = FVector2D::DotProduct(Relative, Direction);
					const double Across = FVector2D::DotProduct(Relative, Normal);

					AlongMin = FMath::Min(AlongMin, Along);
					AlongMax = FMath::Max(AlongMax, Along);
					AcrossMin = FMath::Min(AcrossMin, Across);
					AcrossMax = FMath::Max(AcrossMax, Across);
				}
			}

			if (AcrossMin > HalfWidth || AcrossMax < -HalfWidth)
			{
				continue;
			}

			AddClippedRun(AlongMin, AlongMax);
		}
	}

	/** Total length of a beam with nothing under it, given its supported runs. */
	double UnsupportedLength(TArray<FSupportedRun> Runs, double Length)
	{
		Runs.Sort([](const FSupportedRun& A, const FSupportedRun& B) { return A.Min < B.Min; });

		double Covered = 0.0;
		double Reached = 0.0;
		for (const FSupportedRun& Run : Runs)
		{
			const double From = FMath::Max(Run.Min, Reached);
			if (Run.Max > From)
			{
				Covered += Run.Max - From;
				Reached = Run.Max;
			}
		}

		return FMath::Max(Length - Covered, 0.0);
	}

	/**
	 * Whether a bulkhead's polygon actually covers a beam where it crosses a room.
	 *
	 * The beam's centreline rather than its full footprint: a bulkhead is normally built a little
	 * wider than the beam it boxes in, but a few centimetres either way is a drafting rounding
	 * rather than a design error, and failing a spec over it would train whoever reads the report to
	 * ignore the rule. What the rule exists to catch is a bulkhead somewhere else entirely.
	 *
	 * Sampled at the same rate as FHFHouseSpec::DeepestBeamOverRoom, so the rule that finds a beam
	 * over a room and the rule that asks whether it is covered agree about where the beam is.
	 */
	bool BulkheadCoversBeamOverRoom(const FHFFalseCeiling& Bulkhead, const FHFBeam& Beam, const FHFRoom& Room)
	{
		if (Bulkhead.ExplicitPolygon.Num() < 3 || Beam.Length() <= UE_KINDA_SMALL_NUMBER)
		{
			return false;
		}

		constexpr int32 SampleCount = 24;

		bool bSawBeamInsideRoom = false;
		for (int32 Sample = 0; Sample <= SampleCount; ++Sample)
		{
			const FVector2D Point = FMath::Lerp(Beam.Start, Beam.End,
				static_cast<double>(Sample) / SampleCount);

			// Only the part of the beam that is actually over this room has to be boxed in; where it
			// runs on past the room's walls it is somebody else's ceiling.
			if (!Room.ContainsPoint(Point))
			{
				continue;
			}

			bSawBeamInsideRoom = true;
			if (!HFPolygonContainsPoint(Bulkhead.ExplicitPolygon, Point))
			{
				return false;
			}
		}

		return bSawBeamInsideRoom;
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
		// In CENTIMETRES on both sides. MinHeadroomCm is a centimetre figure and ClearHeight() is in
		// whatever the spec declares, and until now they were compared raw - so on a millimetre spec
		// the rule asked whether a beam left less than 21 cm beneath it and never fired, while on a
		// metre spec 2.8 was below 210 and every beam in the file was reported. The reference flat is
		// in millimetres, so this rule has been inert for its whole life.
		//
		// The column and fixture rules further down already scale; these two were simply missed.
		else if (Beam.ClearHeight() * FHFUnits::ToCentimeterScale(Spec.Units) < Limits.MinHeadroomCm)
		{
			Result.Add(EHFValidationSeverity::Warning, TEXT("BeamLowHeadroom"), Beam.Id,
				FString::Printf(TEXT("Beam '%s' leaves %.1f cm clear beneath it, below the %.0f cm usually treated as minimum headroom."),
					*Describe(Beam.Id), Beam.ClearHeight() * FHFUnits::ToCentimeterScale(Spec.Units),
					Limits.MinHeadroomCm));
		}
	}

	// ------------------------------------------------------------------- beams stand on something
	//
	// Nothing used to ask what held a beam up. The beam rules above check that it has length, has
	// size, and does not reach the floor - every one of them a property of the beam alone - and the
	// ceiling rules check what a beam does to a soffit. Between them nothing looked underneath it.
	//
	// The reference flat carried one for the whole of milestone 8. BM_Living_Cross ran the full
	// 6600 width of the living room at Y 1800, which is the room's exact centre: no wall on that
	// line, no column at either end, and a Cove ceiling that leaves the middle of the room at slab
	// height so there was nothing to conceal it either. It validated clean on every run and a user
	// found it by looking at a render.
	//
	// The rule is a grid check, not a structural analysis, and it is worth being plain about that: a
	// beam belongs on the frame's grid, which means it either follows a wall line or is a clear span
	// framed between columns. Anything else is a beam floating in a room.
	//
	// Severity splits on how many ends are loose, because that is where the honest doubt is. One
	// loose end is a cantilever, which is real and which this model cannot tell apart from a
	// mistake, so it warns. Both ends loose is not a cantilever and not anything else: there is
	// nothing under it at either end and nothing along it, and no reading of the spec makes it
	// stand up. BM_Living_Cross was both.
	{
		const double ScaleToCm = FHFUnits::ToCentimeterScale(Spec.Units);

		// A support whose edge lands exactly on the beam's end is a bearing, not a miss. Same
		// figure as the column-in-opening rule uses, for the same reason.
		const double Tolerance = (ScaleToCm > 0.0) ? 1.0 / ScaleToCm : 0.0;

		for (const FHFBeam& Beam : Spec.Beams)
		{
			const double Length = Beam.Length();
			if (Length <= UE_KINDA_SMALL_NUMBER || Beam.Width <= 0.0)
			{
				continue;	// Already reported as zero-length or non-positive.
			}

			TArray<FSupportedRun> Runs;
			GatherBeamSupports(Spec, Beam, Runs);

			auto IsBorneAt = [&Runs, Tolerance](double Along)
			{
				return Runs.ContainsByPredicate([Along, Tolerance](const FSupportedRun& Run)
				{
					return Along >= Run.Min - Tolerance && Along <= Run.Max + Tolerance;
				});
			};

			const bool bStartBorne = IsBorneAt(0.0);
			const bool bEndBorne = IsBorneAt(Length);

			if (bStartBorne && bEndBorne)
			{
				// Gaps in the middle are the point of a beam: it spans between its supports.
				continue;
			}

			const int32 LooseEnds = (bStartBorne ? 0 : 1) + (bEndBorne ? 0 : 1);
			const FVector2D& Loose = bStartBorne ? Beam.End : Beam.Start;

			Result.Add(
				LooseEnds == 2 ? EHFValidationSeverity::Error : EHFValidationSeverity::Warning,
				TEXT("BeamNotSupported"), Beam.Id,
				LooseEnds == 2
					? FString::Printf(TEXT("Beam '%s' runs (%.1f, %.1f) to (%.1f, %.1f) with nothing under either end and %.1f cm of its %.1f cm length over open floor. A beam has to follow a wall line or span between columns; put a wall or a column under each end, or delete the beam."),
						*Describe(Beam.Id), Beam.Start.X, Beam.Start.Y, Beam.End.X, Beam.End.Y,
						UnsupportedLength(Runs, Length) * ScaleToCm, Length * ScaleToCm)
					: FString::Printf(TEXT("Beam '%s' has nothing under its end at (%.1f, %.1f); only the other end is borne. That is a cantilever if it was meant to be one - otherwise put a wall or a column under it."),
						*Describe(Beam.Id), Loose.X, Loose.Y));
		}
	}

	// ------------------------------------------------------------------------------ columns
	for (const FHFColumn& Column : Spec.Columns)
	{
		const bool bHasPlanSize = Column.Size.X > 0.0 && Column.Size.Y > 0.0;

		if (!bHasPlanSize)
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

		// The other half of the same blindness. Nothing asked what a column was doing there either,
		// and a column is the one element a reader cannot argue with once it is built: it is 450 of
		// concrete standing in the room, and if it is not on a wall junction or under a beam it is
		// an obstruction somebody has to walk round for no reason.
		//
		// A warning rather than an error, and the distinction is the same one the beam rule makes: a
		// free-standing column carrying a beam is ordinary in a large room, and this rule accepts
		// exactly that - a column under a beam passes. What it catches is a column under nothing,
		// which is a column read off the wrong layer of a drawing.
		const bool bOnStructure =
			Spec.Walls.ContainsByPredicate([&Column](const FHFWall& Wall)
			{
				return Wall.Length() > UE_KINDA_SMALL_NUMBER
					&& SegmentHitsRect(Wall.Start, Wall.End, Column.Position, Column.Size, Column.RotationDegrees);
			}) ||
			Spec.Beams.ContainsByPredicate([&Column](const FHFBeam& Beam)
			{
				return Beam.Length() > UE_KINDA_SMALL_NUMBER
					&& SegmentHitsRect(Beam.Start, Beam.End, Column.Position, Column.Size, Column.RotationDegrees);
			});

		if (!bOnStructure)
		{
			Result.Add(EHFValidationSeverity::Warning, TEXT("ColumnStandsFree"), Column.Id,
				FString::Printf(TEXT("Column '%s' stands at (%.1f, %.1f) with no wall through it and no beam over it. A column belongs on a wall junction or under a beam; a %.0f x %.0f pier in open floor is an obstruction with nothing to carry."),
					*Describe(Column.Id), Column.Position.X, Column.Position.Y,
					Column.Size.X, Column.Size.Y));
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
	// The reference 2BHK carried one for thirty-four commits: D_Bed2's doorway overlapped COL_M1 by
	// 75 mm and reached the geometry as a door leaf built inside a column. Two things let it
	// through, and both are fixed here.
	//
	// It was reported as a WARNING. Nothing in the suite fails on a warning - SampleHouseValidates
	// only asks HasErrors() and prints the rest - so the golden fixture every later milestone
	// measures against was allowed to carry an unbuildable doorway and say so quietly on every run.
	// A column in a doorway is not a judgement call with two defensible answers; there is nothing
	// for a reader to weigh up, because the doorway cannot be built and cannot be walked through. So
	// a door is now an error. A window keeps the warning: a pier across a glazing line is bad
	// practice rather than an impossibility, and the frame can be made to fit round it.
	//
	// And it only ever looked inside the wall's own thickness, which is not where a column that
	// blocks a doorway usually is. A column deeper than the partition it belongs to is built flush
	// with one face of it rather than centred on it - that is how a 450 x 230 column on a 115 wall
	// is actually set out - and its bulk then lies wholly outside the wall's slab. The old test
	// excluded exactly the construction it existed to catch, and did so silently.
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

				// It has to be in this wall's line, in the opening's span, and at its height.
				//
				// "In the wall's line" is a whole thickness either side of the centreline, not half.
				// Half is the wall's own slab, and a column packed out against either face of it -
				// which is where a column bigger than its partition goes - falls outside that and
				// was never looked at. A whole thickness covers the column flush on either face and
				// still stops well short of a pier standing free in the room, which belongs to
				// whoever placed the furniture rather than to this rule.
				const double ObstructionBand = Wall->Thickness;
				if (AcrossMax <= -ObstructionBand || AcrossMin >= ObstructionBand)
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
					const bool bIsDoorway = Opening.Kind == EHFOpeningKind::Door
						|| Opening.Kind == EHFOpeningKind::SlidingDoor;

					Result.Add(
						bIsDoorway ? EHFValidationSeverity::Error : EHFValidationSeverity::Warning,
						TEXT("OpeningBlockedByColumn"), Opening.Id,
						FString::Printf(TEXT("%s '%s' overlaps column '%s' by %.1f cm of its %.1f cm width; the column stands inside the clear opening%s. Move the opening clear of the column, or the column off the opening."),
							bIsDoorway ? TEXT("Doorway") : TEXT("Opening"),
							*Describe(Opening.Id), *Describe(Column.Id),
							Overlap * ScaleToCm, Opening.Width * ScaleToCm,
							bIsDoorway ? TEXT(" and nothing can be built or walked through it") : TEXT("")));
				}
			}
		}
	}

	// A fixture standing in front of a WINDOW.
	//
	// The same misread as a column in a doorway, one layer out: a wardrobe against the east wall and
	// a window in the east wall are drawn as separate things and read as separate things, and until
	// now nothing noticed that one was in front of the other. A wall solid is swept against; a
	// fixture never was.
	//
	// It matters more since a window became a sliding unit with a catch somebody reaches for. Fixed
	// glazing behind a wardrobe merely looks odd; a sash behind one cannot be opened at all, and the
	// window photographs as a cabinet.
	//
	// DOORS ARE NOT IN HERE, and that is the correction. This rule gates on the fixture being against
	// the opening's own wall, which is right for a window - a dining table out in the middle of the
	// room is in front of the window in a photograph and in nobody's way in fact - and exactly wrong
	// for a door, where the thing in the way is almost never touching the wall. It is the fridge 19 cm
	// in front of the doorway. Doors are judged by DoorwayNotClear below, on the floor either side of
	// them rather than on the plane of the wall.
	{
		const double ScaleToCm = FHFUnits::ToCentimeterScale(Spec.Units);
		const double Obstruction = (ScaleToCm > 0.0) ? Limits.MinOpeningObstructionCm / ScaleToCm : 0.0;

		for (const FHFOpening& Opening : Spec.Openings)
		{
			if (Opening.Kind == EHFOpeningKind::Door || Opening.Kind == EHFOpeningKind::SlidingDoor)
			{
				continue;
			}

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

	// ------------------------------------------------------------------ can you walk through it
	//
	// The rule the flat needed and did not have. Every genuine obstruction in the reference plan was
	// invisible to the window rule above, because that rule asks whether the fixture is against the
	// opening's wall and the things that block doors are not: the refrigerator standing 19 cm in
	// front of the utility door, anchored to a wall at right angles to it; the shower and the WC 33
	// and 29 cm in front of the balcony door, anchored to nothing. Both rooms could not be entered,
	// and the suite reported a clean flat.
	//
	// So a doorway is measured on the floor rather than on the plane of the wall: anything standing
	// within DoorApproachDepth of either face, at the height of the opening, takes width off it. What
	// is then judged is not how much is blocked but how much unbroken width is LEFT - a wardrobe
	// clipping 11 cm off the end of an 1800 sliding door leaves 1685 and is nothing, while 50 cm out
	// of the middle of a 900 door leaves two 200 slots and is a wall.
	{
		const double ScaleToCm = FHFUnits::ToCentimeterScale(Spec.Units);
		const double Approach = (ScaleToCm > 0.0) ? Limits.DoorApproachDepthCm / ScaleToCm : 0.0;
		const double MinPassage = (ScaleToCm > 0.0) ? Limits.MinClearPassageCm / ScaleToCm : 0.0;

		for (const FHFOpening& Opening : Spec.Openings)
		{
			if (Opening.Kind != EHFOpeningKind::Door && Opening.Kind != EHFOpeningKind::SlidingDoor)
			{
				continue;
			}

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

			// Every bite taken out of the opening's width, and who took it.
			TArray<FInterval> Bites;
			TSet<FName> Blockers;

			for (const FHFFixture& Fixture : Spec.Fixtures)
			{
				if (Fixture.Footprint.X <= 0.0 || Fixture.Footprint.Y <= 0.0 || Fixture.Height <= 0.0
					|| Fixture.IsCeilingMounted())
				{
					continue;
				}

				if (FMath::Min(Fixture.BaseZ + Fixture.Height, HeadZ) - FMath::Max(Fixture.BaseZ, SillZ) <= 0.0)
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
					AlongMin = FMath::Min(AlongMin, FVector2D::DotProduct(Relative, Direction));
					AlongMax = FMath::Max(AlongMax, FVector2D::DotProduct(Relative, Direction));
					AcrossMin = FMath::Min(AcrossMin, FVector2D::DotProduct(Relative, Normal));
					AcrossMax = FMath::Max(AcrossMax, FVector2D::DotProduct(Relative, Normal));
				}

				// Within the approach strip on one side or the other. Both sides, because a door is
				// blocked just as thoroughly from the room it opens out of.
				if (AcrossMin > HalfThickness + Approach || AcrossMax < -HalfThickness - Approach)
				{
					continue;
				}

				const double Lo = FMath::Max(AlongMin, OpeningMin);
				const double Hi = FMath::Min(AlongMax, OpeningMax);
				if (Hi > Lo)
				{
					Bites.Add({ Lo, Hi });
					Blockers.Add(Fixture.Id);
				}
			}

			if (Bites.IsEmpty())
			{
				// A doorway with nothing in front of it is not blocked. It may still be too narrow to
				// use, but that is a property of the door and ImplausibleDoorSize says so - reporting
				// it here would name no fixture and give whoever reads it nothing to move.
				continue;
			}

			// The widest unbroken run left between the bites.
			Bites.Sort([](const FInterval& A, const FInterval& B) { return A.Min < B.Min; });

			double Widest = 0.0;
			double Reached = OpeningMin;
			for (const FInterval& Bite : Bites)
			{
				Widest = FMath::Max(Widest, Bite.Min - Reached);
				Reached = FMath::Max(Reached, Bite.Max);
			}
			Widest = FMath::Max(Widest, OpeningMax - Reached);

			if (Widest >= MinPassage)
			{
				continue;
			}

			TArray<FString> Names;
			for (const FName& Id : Blockers)
			{
				Names.Add(Describe(Id));
			}
			Names.Sort();

			Result.Add(EHFValidationSeverity::Error, TEXT("DoorwayNotClear"), Opening.Id,
				FString::Printf(TEXT("Doorway '%s' has only %.1f cm of unbroken width left of its %.1f cm: %s stand within %.0f cm of it. Nobody walks through %.1f cm - move the fixture or the door."),
					*Describe(Opening.Id), Widest * ScaleToCm, Opening.Width * ScaleToCm,
					*FString::Join(Names, TEXT(", ")), Limits.DoorApproachDepthCm, Widest * ScaleToCm));
		}
	}

	// ------------------------------------------------------------------ and can the leaf get past
	//
	// A door that is clear straight ahead can still be unopenable, because the leaf does not travel
	// straight ahead - it sweeps a quarter circle of its own width off the hinge jamb, and anything
	// standing in that quadrant stops it.
	//
	// D_CBath in the reference flat was the case: the WC reached 7.5 cm past the hinge jamb, which is
	// nothing in plan and left 675 of the 750 doorway clear, so no rule about width could ever have
	// seen it. The leaf fouled the pan at 56 degrees and lay across it at 90. Nothing looked - the
	// SwingBlocked rule above asks only where the leaf's TIP lands, and the tip lands in open floor.
	{
		const double ScaleToCm = FHFUnits::ToCentimeterScale(Spec.Units);
		constexpr int32 Steps = 18;			// every 5 degrees

		for (const FHFOpening& Opening : Spec.Openings)
		{
			if (Opening.Kind != EHFOpeningKind::Door || Opening.Swing == EHFSwing::None ||
				Opening.Width <= 0.0 || Opening.Height <= 0.0)
			{
				continue;
			}

			const FHFWall* Wall = Spec.FindWall(Opening.WallId);
			if (Wall == nullptr || Wall->Length() <= UE_KINDA_SMALL_NUMBER)
			{
				continue;
			}

			const FVector2D Direction = (Wall->End - Wall->Start) / Wall->Length();
			const FVector2D Normal(-Direction.Y, Direction.X);

			// Same convention as AHFHouseActor::DrawSwing, so the rule and the drawn arc agree.
			const bool bHingeAtNear =
				Opening.Swing == EHFSwing::InwardLeft || Opening.Swing == EHFSwing::OutwardLeft;
			const double Side =
				(Opening.Swing == EHFSwing::InwardLeft || Opening.Swing == EHFSwing::InwardRight) ? 1.0 : -1.0;

			const double HalfWidth = Opening.Width * 0.5;
			const FVector2D Hinge = Wall->Start +
				Direction * (Opening.OffsetAlongWall + (bHingeAtNear ? -HalfWidth : HalfWidth));
			const FVector2D Closed = bHingeAtNear ? Direction : -Direction;
			const FVector2D Open = Normal * Side;

			const double SillZ = Wall->BaseZ + Opening.SillHeight;
			const double HeadZ = SillZ + Opening.Height;

			for (const FHFFixture& Fixture : Spec.Fixtures)
			{
				if (Fixture.Footprint.X <= 0.0 || Fixture.Footprint.Y <= 0.0 || Fixture.Height <= 0.0
					|| Fixture.IsCeilingMounted())
				{
					continue;
				}

				if (FMath::Min(Fixture.BaseZ + Fixture.Height, HeadZ) - FMath::Max(Fixture.BaseZ, SillZ) <= 0.0)
				{
					continue;
				}

				// From just off closed, so a fixture sitting IN the opening is DoorwayNotClear's to
				// report rather than being called a swing problem as well.
				double FirstFoul = -1.0;
				for (int32 Step = 1; Step <= Steps; ++Step)
				{
					const double Angle = (static_cast<double>(Step) / Steps) * HALF_PI;
					const FVector2D Leaf = Hinge +
						(Closed * FMath::Cos(Angle) + Open * FMath::Sin(Angle)) * Opening.Width;

					if (SegmentHitsRect(Hinge, Leaf, Fixture.Position, Fixture.Footprint, Fixture.RotationDegrees))
					{
						FirstFoul = FMath::RadiansToDegrees(Angle);
						break;
					}
				}

				if (FirstFoul < 0.0)
				{
					continue;
				}

				Result.Add(EHFValidationSeverity::Error, TEXT("DoorSwingHitsFixture"), Opening.Id,
					FString::Printf(TEXT("The leaf of door '%s' sweeps into fixture '%s' at about %.0f degrees open; its %.1f cm quadrant off the hinge is not clear. The doorway itself measures fine - it is the arc that is blocked."),
						*Describe(Opening.Id), *Describe(Fixture.Id), FirstFoul,
						Opening.Width * ScaleToCm));
			}
		}
	}

	// ---------------------------------------------------------- fixtures against the frame itself
	//
	// Nothing compared a fixture with a column or a beam. The column rule looks at columns against
	// openings, the overlap rule looks at fixtures against each other, and the articulation sweep
	// builds wall, column and beam solids but only sweeps the moving parts of OPENINGS against them.
	// A fixture could be, and was, built straight through the frame: F_Exh_Utility had 125 x 45 mm
	// of a 300 mm extract fan cored through COL_N1, and both bathroom fans sat 100 mm up inside
	// BM_Mid_Upper.
	//
	// A beam warns and a column errors, and the difference is real rather than a hedge. Services are
	// routinely dropped under a beam and a beam can be boxed in with them; an RCC column is not
	// something a duct gets cut through on site, whatever the drawing says.
	{
		const double ScaleToCm = FHFUnits::ToCentimeterScale(Spec.Units);

		for (const FHFFixture& Fixture : Spec.Fixtures)
		{
			if (Fixture.Footprint.X <= 0.0 || Fixture.Footprint.Y <= 0.0 || Fixture.Height <= 0.0
				|| Fixture.IsCeilingMounted())
			{
				continue;	// A ceiling-mounted fixture measures its base from a ceiling, not the floor.
			}

			const double FixtureBottom = Fixture.BaseZ;
			const double FixtureTop = Fixture.BaseZ + Fixture.Height;

			for (const FHFColumn& Column : Spec.Columns)
			{
				if (Column.Size.X <= 0.0 || Column.Size.Y <= 0.0 || Column.Height <= 0.0)
				{
					continue;
				}

				if (FMath::Min(FixtureTop, Column.Height) - FMath::Max(FixtureBottom, 0.0) <= 0.0 ||
					!OrientedBoxesOverlap(Fixture.Position, Fixture.Footprint, Fixture.RotationDegrees,
						Column.Position, Column.Size, Column.RotationDegrees))
				{
					continue;
				}

				Result.Add(EHFValidationSeverity::Error, TEXT("FixtureClashesWithStructure"), Fixture.Id,
					FString::Printf(TEXT("Fixture '%s' is built into column '%s' at (%.1f, %.1f). A column is not something a fixture gets cut into on site; move the fixture off it."),
						*Describe(Fixture.Id), *Describe(Column.Id), Column.Position.X, Column.Position.Y));
			}

			for (const FHFBeam& Beam : Spec.Beams)
			{
				const double Length = Beam.Length();
				if (Length <= UE_KINDA_SMALL_NUMBER || Beam.Width <= 0.0 || Beam.Depth <= 0.0)
				{
					continue;
				}

				const double Overlap =
					FMath::Min(FixtureTop, Beam.SoffitZ) - FMath::Max(FixtureBottom, Beam.SoffitZ - Beam.Depth);
				if (Overlap <= 0.0)
				{
					continue;
				}

				const FVector2D Centre = (Beam.Start + Beam.End) * 0.5;
				const FVector2D Along = (Beam.End - Beam.Start) / Length;
				const double Rotation = FMath::RadiansToDegrees(FMath::Atan2(Along.Y, Along.X));

				if (!OrientedBoxesOverlap(Fixture.Position, Fixture.Footprint, Fixture.RotationDegrees,
					Centre, FVector2D(Length, Beam.Width), Rotation))
				{
					continue;
				}

				Result.Add(EHFValidationSeverity::Warning, TEXT("FixtureClashesWithStructure"), Fixture.Id,
					FString::Printf(TEXT("Fixture '%s' runs %.1f cm up inside beam '%s'. Services are dropped below a beam rather than through it - lower the fixture, or box the beam in with it."),
						*Describe(Fixture.Id), Overlap * ScaleToCm, *Describe(Beam.Id)));
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
		// Scaled to centimetres, for the same reason BeamLowHeadroom above is: the limit is a
		// centimetre figure and the spec is in whatever it declares.
		else if ((Room->CeilingHeight - Ceiling.Drop) * FHFUnits::ToCentimeterScale(Spec.Units)
			< Limits.MinHeadroomCm)
		{
			Result.Add(EHFValidationSeverity::Warning, TEXT("LowHeadroom"), Ceiling.Id,
				FString::Printf(TEXT("False ceiling '%s' leaves %.1f cm clear in room '%s', below the %.0f cm usually treated as minimum headroom."),
					*Describe(Ceiling.Id),
					(Room->CeilingHeight - Ceiling.Drop) * FHFUnits::ToCentimeterScale(Spec.Units),
					*Describe(Room->Id), Limits.MinHeadroomCm));
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
				// nothing mid-span. It is only excusable when a bulkhead boxes the beam in - which
				// is exactly how this is detailed in practice.
				const bool bIsPerimeterOnly =
					Ceiling.Style == EHFCeilingStyle::Peripheral ||
					Ceiling.Style == EHFCeilingStyle::Cove;

				// OVER the beam, not merely in the same room.
				//
				// This exemption used to ask only whether a deep-enough bulkhead existed somewhere
				// in the room, which is a question whose answer cannot conceal anything. A bulkhead
				// is by definition a LOCALISED drop with its own polygon - BulkheadNeedsPolygon,
				// thirty lines below, refuses one without a polygon - so a bulkhead over the TV unit
				// at one end of a living room excused a beam crossing the middle of it.
				//
				// That is not hypothetical. It is how BM_Living_Cross validated clean: R_Living had
				// a Cove at 200, a Bulkhead at 450, and a beam 400 deep, and the three figures alone
				// satisfied every clause. The beam and the bulkhead did happen to coincide in that
				// case, but nothing checked it and nothing would have noticed when they stopped.
				const bool bBulkheadCoversIt = Spec.FalseCeilings.ContainsByPredicate(
					[&Ceiling, Beam, Room](const FHFFalseCeiling& Other)
					{
						return Other.RoomId == Ceiling.RoomId
							&& Other.Style == EHFCeilingStyle::Bulkhead
							&& Other.Drop + UE_KINDA_SMALL_NUMBER >= Beam->Depth
							&& BulkheadCoversBeamOverRoom(Other, *Beam, *Room);
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
