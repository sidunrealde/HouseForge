// Copyright Siddartha G. All Rights Reserved.

#include "Model/HFSkirtingPlan.h"

namespace
{
	/**
	 * Slack allowed when deciding a wall is set out ON a boundary edge.
	 *
	 * Half a centimetre: far below the thinnest partition in this domain - a 115 wall's centreline is
	 * 5.75 from its own face - and far above the arithmetic of projecting a point onto a line.
	 */
	constexpr double OnTheLine = 0.5;

	/** Parallel to within this, as a dot product of unit directions. Half a degree. */
	constexpr double Parallel = 0.999;

	/** Below this a run or a break has no length worth carrying. */
	constexpr double Negligible = 0.01;

	/**
	 * How high a sill has to be before the opening is a window rather than something you walk through.
	 *
	 * The spec's own convention. A door and a French casement sit on the floor; a window sill in this
	 * flat is 900 at the lowest.
	 */
	constexpr double WalkThroughSill = 1.0;

	FVector2D RotateAbout(const FVector2D& Point, const FVector2D& Centre, double Degrees)
	{
		const double Radians = FMath::DegreesToRadians(Degrees);
		const double C = FMath::Cos(Radians);
		const double S = FMath::Sin(Radians);
		const FVector2D D = Point - Centre;
		return Centre + FVector2D(D.X * C - D.Y * S, D.X * S + D.Y * C);
	}

	/** The four plan corners of a rectangular footprint about a centre. */
	TArray<FVector2D> PlanCorners(const FVector2D& Centre, const FVector2D& Size, double RotationDegrees)
	{
		const FVector2D Half = Size * 0.5;

		TArray<FVector2D> Out;
		Out.Reserve(4);
		Out.Add(RotateAbout(Centre + FVector2D(-Half.X, -Half.Y), Centre, RotationDegrees));
		Out.Add(RotateAbout(Centre + FVector2D(Half.X, -Half.Y), Centre, RotationDegrees));
		Out.Add(RotateAbout(Centre + FVector2D(Half.X, Half.Y), Centre, RotationDegrees));
		Out.Add(RotateAbout(Centre + FVector2D(-Half.X, Half.Y), Centre, RotationDegrees));
		return Out;
	}
}

double FHFSkirtingPlan::BoundaryLength() const
{
	double Total = 0.0;
	for (const FHFSkirtingEdge& Edge : Edges)
	{
		Total += Edge.Length;
	}
	return Total;
}

double FHFSkirtingPlan::CoveredLength() const
{
	double Total = 0.0;
	for (const FHFSkirtingEdge& Edge : Edges)
	{
		for (const FHFSkirtingRun& Run : Edge.Runs)
		{
			Total += Run.Length();
		}
	}
	return Total;
}

double FHFSkirtingPlan::ReturnLength() const
{
	double Total = 0.0;
	for (const FHFSkirtingReturn& Run : Returns)
	{
		Total += Run.Length();
	}
	return Total;
}

bool FHFSkirting::IsDoorway(const FHFOpening& Opening)
{
	const bool bWalkThrough =
		Opening.Kind == EHFOpeningKind::Door ||
		Opening.Kind == EHFOpeningKind::SlidingDoor ||
		Opening.Kind == EHFOpeningKind::Archway;

	return bWalkThrough && Opening.SillHeight <= WalkThroughSill;
}

bool FHFSkirting::IsScribedJoinery(EHFFixtureType Type)
{
	switch (Type)
	{
	// Made on site against the plaster, with the plinth scribed to the floor. The carpenter cuts the
	// skirting out where the carcass lands, because there is nowhere for it to go.
	case EHFFixtureType::Wardrobe:
	case EHFFixtureType::KitchenBaseCabinet:
	case EHFFixtureType::KitchenTallUnit:
	case EHFFixtureType::Bookshelf:
	case EHFFixtureType::ShoeRack:
	case EHFFixtureType::TVUnit:
	case EHFFixtureType::StudyTable:
	case EHFFixtureType::Vanity:
		return true;

	default:
		// Everything else either hangs clear of the floor - a wall cabinet, a loft - or is loose
		// furniture the skirting runs behind. Both want the skirting left alone.
		return false;
	}
}

TArray<const FHFWall*> FHFSkirting::WallsOnEdge(const FVector2D& From, const FVector2D& To,
	const TArray<FHFWall>& Walls)
{
	TArray<const FHFWall*> Out;

	const double EdgeLength = FVector2D::Distance(From, To);
	if (EdgeLength <= UE_KINDA_SMALL_NUMBER)
	{
		return Out;
	}

	const FVector2D Direction = (To - From) / EdgeLength;
	const FVector2D Normal(-Direction.Y, Direction.X);

	for (const FHFWall& Wall : Walls)
	{
		const double WallLength = Wall.Length();
		if (WallLength <= UE_KINDA_SMALL_NUMBER)
		{
			continue;
		}

		const FVector2D WallDirection = (Wall.End - Wall.Start) / WallLength;
		if (FMath::Abs(FVector2D::DotProduct(WallDirection, Direction)) < Parallel)
		{
			continue;
		}

		if (FMath::Abs(FVector2D::DotProduct(Wall.Start - From, Normal)) > OnTheLine)
		{
			continue;
		}

		const double T0 = FVector2D::DotProduct(Wall.Start - From, Direction);
		const double T1 = FVector2D::DotProduct(Wall.End - From, Direction);

		if (FMath::Max(T0, T1) <= OnTheLine || FMath::Min(T0, T1) >= EdgeLength - OnTheLine)
		{
			continue;
		}

		Out.Add(&Wall);
	}

	return Out;
}

bool FHFSkirting::ColumnProjectsInto(const FHFColumn& Column, const FVector2D& From, const FVector2D& To,
	double FaceInset, double& OutProjection, double& OutFrom, double& OutTo)
{
	OutProjection = 0.0;
	OutFrom = 0.0;
	OutTo = 0.0;

	const double EdgeLength = FVector2D::Distance(From, To);
	if (EdgeLength <= UE_KINDA_SMALL_NUMBER)
	{
		return false;
	}

	const FVector2D Direction = (To - From) / EdgeLength;
	const FVector2D Normal(-Direction.Y, Direction.X);

	double Deepest = -TNumericLimits<double>::Max();
	double Shallowest = TNumericLimits<double>::Max();
	double MinAlong = TNumericLimits<double>::Max();
	double MaxAlong = -TNumericLimits<double>::Max();

	for (const FVector2D& Corner : PlanCorners(Column.Position, Column.Size, Column.RotationDegrees))
	{
		const FVector2D Local = Corner - From;

		const double Across = FVector2D::DotProduct(Local, Normal) - FaceInset;
		Deepest = FMath::Max(Deepest, Across);
		Shallowest = FMath::Min(Shallowest, Across);

		const double Along = FVector2D::DotProduct(Local, Direction);
		MinAlong = FMath::Min(MinAlong, Along);
		MaxAlong = FMath::Max(MaxAlong, Along);
	}

	OutProjection = Deepest;
	OutFrom = FMath::Max(0.0, MinAlong);
	OutTo = FMath::Min(EdgeLength, MaxAlong);

	// IT HAS TO BE IN THIS WALL, which is a separate question from being in front of it. Without the
	// near-side test a column standing anywhere in the room answers for every edge of it - a column
	// 2 m off the far wall reads as one projecting 2 m out of it - and the returns then strike out
	// across the room and through the next one. That is not a hypothetical: it built 79 square metres
	// of skirting standing in mid-air the first time this ran.
	//
	// A column that touches no wall at all therefore produces nothing, and the run passes it. Going
	// round a free-standing column is a closed loop rather than a return, and there is not one in
	// this domain to justify writing it.
	const bool bSetInTheWall = Shallowest <= Negligible;

	return bSetInTheWall && Deepest > 0.0 && OutTo - OutFrom > Negligible;
}

FHFSkirtingPlan FHFSkirting::For(const FHFRoom& Room, const TArray<FHFWall>& Walls,
	const TArray<FHFOpening>& Openings, const TArray<FHFColumn>& Columns,
	const TArray<FHFFixture>& Fixtures, const FHFSkirtingParams& Params,
	const TSet<FName>* BuiltFixtureIds)
{
	FHFSkirtingPlan Plan;
	Plan.Depth = Params.Depth;

	// The room's own figure, read here rather than copied onto the plan: it decides which joinery is
	// low enough to displace a skirting, and the generator asks the room for it again when it builds.
	const double Height = FMath::Max(0.0, Room.SkirtingHeight);

	const int32 Count = Room.Boundary.Num();
	if (Count < 3)
	{
		return Plan;
	}

	Plan.Edges.SetNum(Count);

	for (int32 i = 0; i < Count; ++i)
	{
		const FVector2D& A = Room.Boundary[i];
		const FVector2D& B = Room.Boundary[(i + 1) % Count];

		FHFSkirtingEdge& Edge = Plan.Edges[i];
		Edge.Start = A;
		Edge.End = B;
		Edge.Length = FVector2D::Distance(A, B);

		if (Edge.Length <= UE_KINDA_SMALL_NUMBER)
		{
			continue;
		}

		const FVector2D Direction = (B - A) / Edge.Length;
		const FVector2D Normal(-Direction.Y, Direction.X);

		// ------------------------------------------------------------------ the plaster
		//
		// The thickest wall on the edge, because where a 230 and a 115 both run along one line the
		// skirting has to clear the one that stands furthest into the room.
		const TArray<const FHFWall*> OnEdge = WallsOnEdge(A, B, Walls);
		for (const FHFWall* Wall : OnEdge)
		{
			Edge.FaceInset = FMath::Max(Edge.FaceInset, Wall->Thickness * 0.5);
		}

		TArray<FHFSkirtingBreak> EdgeBreaks;

		// ------------------------------------------------------------------ doorways
		//
		// ONLY OPENINGS IN A WALL OF THIS EDGE. Matching on proximity alone - which is what this used
		// to do - lets a door in any collinear wall anywhere in the flat cut a hole in a room it does
		// not open into, and that is most of what "the skirting stops in the middle" was.
		for (const FHFOpening& Opening : Openings)
		{
			if (!IsDoorway(Opening))
			{
				continue;
			}

			const FHFWall* Host = nullptr;
			for (const FHFWall* Wall : OnEdge)
			{
				if (Wall->Id == Opening.WallId)
				{
					Host = Wall;
					break;
				}
			}

			if (Host == nullptr)
			{
				continue;
			}

			const double HostLength = Host->Length();
			if (HostLength <= UE_KINDA_SMALL_NUMBER)
			{
				continue;
			}

			// The opening's centre, set out along its OWN wall, then measured along this edge. The two
			// lines are the same line, but the wall may run the other way down it.
			const FVector2D HostDirection = (Host->End - Host->Start) / HostLength;
			const FVector2D Centre = Host->Start + HostDirection * Opening.OffsetAlongWall;

			const double Along = FVector2D::DotProduct(Centre - A, Direction);

			// Its own width, not the widest door in the flat. A 750 bathroom door takes 750 of
			// skirting with it and no more.
			const double Half = Opening.Width * 0.5 + Params.JambClearance;

			FHFSkirtingBreak Break;
			Break.EdgeIndex = i;
			Break.Start = FMath::Max(0.0, Along - Half);
			Break.End = FMath::Min(Edge.Length, Along + Half);
			Break.Cause = EHFSkirtingBreakCause::Doorway;
			Break.SourceId = Opening.Id;

			// Clamped into the edge, so a doorway round the corner cannot reach in here at all.
			if (Break.Length() > Negligible)
			{
				EdgeBreaks.Add(Break);
			}
		}

		// ------------------------------------------------------------------ columns standing proud
		//
		// NOT AN END. A column in a wall face is the one obstruction a skirting goes ROUND: out along
		// its near flank, across its face and back along the far one. The straight run does stop, so
		// it is a break - but a break that costs no skirting, because the three returns replace it
		// and then some.
		//
		// Ignored when the column stands less proud than the skirting is deep. Then there is nothing
		// to turn round: the board is scribed to the concrete and runs straight past, which is what a
		// mason does with a 10 mm nib.
		if (Height > 0.0)
		{
			for (const FHFColumn& Column : Columns)
			{
				// It has to reach the floor of THIS room. A column starting above the skirting -
				// stub columns over a beam - passes over it and leaves it whole.
				if (Column.BaseZ - Room.FloorZ >= Height)
				{
					continue;
				}

				double Projection = 0.0;
				double FromAlong = 0.0;
				double ToAlong = 0.0;

				if (!ColumnProjectsInto(Column, A, B, Edge.FaceInset, Projection, FromAlong, ToAlong)
					|| Projection <= Plan.Depth)
				{
					continue;
				}

				FHFSkirtingBreak Break;
				Break.EdgeIndex = i;
				Break.Start = FromAlong;
				Break.End = ToAlong;
				Break.Cause = EHFSkirtingBreakCause::Structure;
				Break.SourceId = Column.Id;
				EdgeBreaks.Add(Break);

				// The three lengths that go round it, each with the room on its left.
				//
				// Every one overlaps its neighbour by the section depth, so the two external corners
				// of the return are filled by the union rather than left as a notch - the same way
				// two runs meeting at a room corner fill it. The flanks are extended outward and the
				// face is extended sideways, so the overlap is at the corner and nowhere else.
				const FVector2D Face = A + Normal * Edge.FaceInset;
				const double Out = Projection + Plan.Depth;

				auto AddReturn = [&Plan, &Column](const FVector2D& Start, const FVector2D& End)
				{
					FHFSkirtingReturn Run;
					Run.Start = Start;
					Run.End = End;
					Run.SourceId = Column.Id;
					Plan.Returns.Add(Run);
				};

				// Near flank, running out of the wall. Absent where the column reaches the end of
				// the edge, because there is no exposed flank there - the return carries on round
				// the corner and the next edge picks the column up itself.
				if (FromAlong > Negligible)
				{
					AddReturn(Face + Direction * FromAlong,
						Face + Direction * FromAlong + Normal * Out);
				}

				// The face itself.
				AddReturn(Face + Direction * (FromAlong - Plan.Depth) + Normal * Projection,
					Face + Direction * (ToAlong + Plan.Depth) + Normal * Projection);

				// Far flank, running back into the wall.
				if (ToAlong < Edge.Length - Negligible)
				{
					AddReturn(Face + Direction * ToAlong + Normal * Out,
						Face + Direction * ToAlong);
				}
			}
		}

		// ------------------------------------------------------------------ scribed joinery
		//
		// Only where there is a skirting for it to displace, and only where it would actually foul
		// one: a carcass standing clear of the plaster has the skirting running on behind it.
		if (Height > 0.0)
		{
			for (const FHFFixture& Fixture : Fixtures)
			{
				if (Fixture.RoomId != Room.Id || !IsScribedJoinery(Fixture.Type))
				{
					continue;
				}

				// AND IT HAS TO BE BUILT. A break with nothing standing in it is a length of missing
				// skirting and reads as exactly that: bare plaster meeting bare floor for the width
				// of a unit nobody modelled. Where the caller has told us what it is building, a
				// fixture it is not building leaves the run whole; where it has not, every scribed
				// type still cuts, so a room resolved on its own behaves as it always did.
				if (BuiltFixtureIds != nullptr && !BuiltFixtureIds->Contains(Fixture.Id))
				{
					continue;
				}

				// IT HAS TO REACH THE FLOOR. Height above the floor is the physical test and not a
				// guess at what is built in: anything whose underside is above the top of the skirting
				// passes over it and leaves it whole.
				if (Fixture.BaseZ >= Height)
				{
					continue;
				}

				const TArray<FVector2D> Corners =
					PlanCorners(Fixture.Position, Fixture.Footprint, Fixture.RotationDegrees);

				double NearestAcross = TNumericLimits<double>::Max();
				double MinAlong = TNumericLimits<double>::Max();
				double MaxAlong = -TNumericLimits<double>::Max();

				for (const FVector2D& Corner : Corners)
				{
					const FVector2D Local = Corner - A;

					// Off the FACE, not off the centreline: the skirting occupies the first Depth of
					// room in front of the plaster, and the question is whether the carcass is in it.
					NearestAcross = FMath::Min(NearestAcross,
						FVector2D::DotProduct(Local, Normal) - Edge.FaceInset);

					const double Projection = FVector2D::DotProduct(Local, Direction);
					MinAlong = FMath::Min(MinAlong, Projection);
					MaxAlong = FMath::Max(MaxAlong, Projection);
				}

				if (NearestAcross > Plan.Depth)
				{
					continue;
				}

				FHFSkirtingBreak Break;
				Break.EdgeIndex = i;
				Break.Start = FMath::Max(0.0, MinAlong);
				Break.End = FMath::Min(Edge.Length, MaxAlong);
				Break.Cause = EHFSkirtingBreakCause::Joinery;
				Break.SourceId = Fixture.Id;

				if (Break.Length() > Negligible)
				{
					EdgeBreaks.Add(Break);
				}
			}
		}

		EdgeBreaks.Sort([](const FHFSkirtingBreak& L, const FHFSkirtingBreak& R)
		{
			return L.Start < R.Start;
		});

		// ------------------------------------------------------------------ what is left
		double Cursor = 0.0;
		for (const FHFSkirtingBreak& Break : EdgeBreaks)
		{
			if (Break.Start - Cursor > Negligible)
			{
				Edge.Runs.Add(FHFSkirtingRun{ Cursor, Break.Start });
			}
			Cursor = FMath::Max(Cursor, Break.End);
		}

		if (Edge.Length - Cursor > Negligible)
		{
			Edge.Runs.Add(FHFSkirtingRun{ Cursor, Edge.Length });
		}

		Plan.Breaks.Append(EdgeBreaks);
	}

	return Plan;
}

TArray<FString> FHFSkirting::Describe(const FHFRoom& Room, const FHFSkirtingPlan& Plan)
{
	TArray<FString> Lines;
	Lines.Reserve(Plan.Breaks.Num());

	for (const FHFSkirtingBreak& Break : Plan.Breaks)
	{
		Lines.Add(FString::Printf(TEXT("Room '%s' skirting stops for %s '%s' on edge %d, %.1f to %.1f."),
			*Room.Id.ToString(),
			Break.Cause == EHFSkirtingBreakCause::Doorway ? TEXT("doorway") : TEXT("joinery"),
			*Break.SourceId.ToString(), Break.EdgeIndex, Break.Start, Break.End));
	}

	return Lines;
}
