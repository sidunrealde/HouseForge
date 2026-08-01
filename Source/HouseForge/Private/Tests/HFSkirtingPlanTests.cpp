// Copyright Siddartha G. All Rights Reserved.

#include "HouseForge.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Actors/HFHouseActor.h"
#include "Misc/AutomationTest.h"
#include "Model/HFSampleHouse.h"
#include "Model/HFSkirtingPlan.h"
#include "Model/HFTypes.h"

#define HF_TEST_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A SKIRTING RUNS THE WHOLE PERIMETER, AND EVERY GAP IN IT HAS A NAME.
 *
 * Skirting was buried inside the masonry until the plaster offset landed, so the first build anybody
 * could actually see it in was the one where it stopped dead in the middle of walls. Measured over
 * the reference flat at that point: 71.5% of the boundary skirted, and the gaps in the wrong places.
 * The common bathroom had four of them and not one was its own door - they were the living room's,
 * the foyer's, the master bedroom's and the kitchen's, matched to its walls by nothing more than
 * being roughly collinear.
 *
 * The assertion that would have caught all of it is the one in FHFSkirtingCoverageTest below: for
 * every room, boundary length equals covered length plus breaks, and every break names an opening in
 * a wall of that room or a piece of joinery standing in it. Everything else here exercises one rule.
 *
 * In CENTIMETRES. The sample house is authored in millimetres and converted at the top of each test,
 * exactly as AHFHouseActor::SetSpec does at ingest.
 */
namespace
{
	/** A 400 x 300 room, walls 11.5 thick all round, set out on the boundary centrelines. */
	FHFRoom MakeSkirtingRoom()
	{
		FHFRoom Room;
		Room.Id = TEXT("R_Skirt");
		Room.Boundary = { FVector2D(0, 0), FVector2D(400, 0), FVector2D(400, 300), FVector2D(0, 300) };
		Room.FloorZ = 0.0;
		Room.CeilingHeight = 300.0;
		Room.SkirtingHeight = 10.0;
		return Room;
	}

	FHFWall MakeSkirtingWall(FName Id, const FVector2D& Start, const FVector2D& End, double Thickness)
	{
		FHFWall Wall;
		Wall.Id = Id;
		Wall.Start = Start;
		Wall.End = End;
		Wall.Thickness = Thickness;
		Wall.Height = 300.0;
		return Wall;
	}

	/** The four walls of MakeSkirtingRoom, on its four boundary edges. */
	TArray<FHFWall> MakeSkirtingWalls()
	{
		return {
			MakeSkirtingWall(TEXT("W_S"), FVector2D(0, 0), FVector2D(400, 0), 11.5),
			MakeSkirtingWall(TEXT("W_E"), FVector2D(400, 0), FVector2D(400, 300), 11.5),
			MakeSkirtingWall(TEXT("W_N"), FVector2D(400, 300), FVector2D(0, 300), 11.5),
			MakeSkirtingWall(TEXT("W_W"), FVector2D(0, 300), FVector2D(0, 0), 23.0)
		};
	}

	FHFOpening MakeSkirtingOpening(FName Id, FName WallId, EHFOpeningKind Kind,
		double Offset, double Width, double Sill)
	{
		FHFOpening Opening;
		Opening.Id = Id;
		Opening.WallId = WallId;
		Opening.Kind = Kind;
		Opening.OffsetAlongWall = Offset;
		Opening.Width = Width;
		Opening.Height = 210.0;
		Opening.SillHeight = Sill;
		return Opening;
	}

}

/** A room with nothing in it is skirted end to end, and the plaster offset is per edge. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFSkirtingWholePerimeterTest,
	"HouseForge.Model.SkirtingCoversWholePerimeter", HF_TEST_FLAGS)

bool FHFSkirtingWholePerimeterTest::RunTest(const FString& Parameters)
{
	const FHFRoom Room = MakeSkirtingRoom();
	const TArray<FHFWall> Walls = MakeSkirtingWalls();

	const FHFSkirtingPlan Plan = FHFSkirting::For(Room, Walls, {}, {}, {});

	TestEqual(TEXT("One entry per boundary edge"), Plan.Edges.Num(), 4);
	TestEqual(TEXT("Nothing interrupts it"), Plan.Breaks.Num(), 0);
	TestNearlyEqual(TEXT("The whole boundary is skirted"),
		Plan.CoveredLength(), Plan.BoundaryLength(), 0.01);
	TestNearlyEqual(TEXT("And that boundary is 1400"), Plan.BoundaryLength(), 1400.0, 0.01);

	// Per EDGE and not per room: the walls round a room are not all the same thickness, and a skirting
	// laid on the boundary is laid down the middle of the masonry.
	TestNearlyEqual(TEXT("A 115 edge insets 5.75"), Plan.Edges[0].FaceInset, 5.75, 0.01);
	TestNearlyEqual(TEXT("A 230 edge insets 11.5"), Plan.Edges[3].FaceInset, 11.5, 0.01);

	return true;
}

/**
 * A DOORWAY TAKES ITS OWN WIDTH, AND NOTHING TAKES MORE THAN ITS OWN WIDTH.
 *
 * The composing layer used to hand the generator ONE gap width for the whole flat - the widest door
 * in the spec, which in the reference 2BHK is the 1800 balcony slider - and cut every doorway that
 * wide. A 750 bathroom door removed 1800 of skirting: 525 of solid wall left bare on each side of the
 * frame, in a 1800 long bathroom lobby that then had no skirting at all.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFSkirtingDoorwayWidthTest,
	"HouseForge.Model.SkirtingStopsAtTheDoorItIsAt", HF_TEST_FLAGS)

bool FHFSkirtingDoorwayWidthTest::RunTest(const FString& Parameters)
{
	const FHFRoom Room = MakeSkirtingRoom();
	const TArray<FHFWall> Walls = MakeSkirtingWalls();

	// A 75 door in the middle of the south wall, and a 180 slider in the middle of the north one.
	const TArray<FHFOpening> Openings = {
		MakeSkirtingOpening(TEXT("D_Small"), TEXT("W_S"), EHFOpeningKind::Door, 200.0, 75.0, 0.0),
		MakeSkirtingOpening(TEXT("D_Wide"), TEXT("W_N"), EHFOpeningKind::SlidingDoor, 200.0, 180.0, 0.0)
	};

	FHFSkirtingParams Params;
	Params.JambClearance = 1.0;

	const FHFSkirtingPlan Plan = FHFSkirting::For(Room, Walls, Openings, {}, {}, Params);

	TestEqual(TEXT("Two doorways, two breaks"), Plan.Breaks.Num(), 2);

	for (const FHFSkirtingBreak& Break : Plan.Breaks)
	{
		const double Expected = (Break.SourceId == FName(TEXT("D_Small"))) ? 77.0 : 182.0;
		TestNearlyEqual(*FString::Printf(TEXT("%s takes its own width plus jambs"),
			*Break.SourceId.ToString()), Break.Length(), Expected, 0.01);
	}

	// The small door is on edge 0, which runs west to east, so the gap straddles its midpoint.
	const FHFSkirtingEdge& South = Plan.Edges[0];
	TestEqual(TEXT("The south edge is left in two runs"), South.Runs.Num(), 2);
	TestNearlyEqual(TEXT("The first run reaches the jamb"), South.Runs[0].End, 161.5, 0.01);
	TestNearlyEqual(TEXT("The second picks up at the other one"), South.Runs[1].Start, 238.5, 0.01);

	TestNearlyEqual(TEXT("Total gap is the two doors and no more"),
		Plan.BreakLength(), 77.0 + 182.0, 0.01);

	return true;
}

/**
 * A WINDOW DOES NOT INTERRUPT A SKIRTING, however low its sill.
 *
 * The wall under a window is still a wall and the skirting runs straight past it. Only an opening you
 * walk through takes the floor with it.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFSkirtingWindowTest,
	"HouseForge.Model.SkirtingRunsPastAWindow", HF_TEST_FLAGS)

bool FHFSkirtingWindowTest::RunTest(const FString& Parameters)
{
	const FHFRoom Room = MakeSkirtingRoom();
	const TArray<FHFWall> Walls = MakeSkirtingWalls();

	const TArray<FHFOpening> Openings = {
		MakeSkirtingOpening(TEXT("V_Sliding"), TEXT("W_S"), EHFOpeningKind::SlidingWindow, 200.0, 150.0, 90.0),
		MakeSkirtingOpening(TEXT("V_Vent"), TEXT("W_E"), EHFOpeningKind::Ventilator, 150.0, 60.0, 210.0)
	};

	const FHFSkirtingPlan Plan = FHFSkirting::For(Room, Walls, Openings, {}, {});

	TestEqual(TEXT("No window interrupts a skirting"), Plan.Breaks.Num(), 0);
	TestNearlyEqual(TEXT("The whole boundary is still skirted"),
		Plan.CoveredLength(), Plan.BoundaryLength(), 0.01);

	return true;
}

/**
 * A DOORWAY IN SOMEBODY ELSE'S WALL IS NOT THIS ROOM'S PROBLEM.
 *
 * The old matcher took every opening in the spec and asked only whether its centre was within 300 mm
 * of the edge's LINE. Any door in any collinear wall anywhere in the flat therefore cut a hole in a
 * room it does not open into, and a door up to a full gap-width BEFORE the start of an edge still
 * reached into it. That was most of "the skirting stops abruptly in the middle": in the reference
 * flat the common bathroom collected four foreign doorways and the master bathroom two.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFSkirtingForeignDoorwayTest,
	"HouseForge.Model.SkirtingIgnoresDoorwaysInOtherWalls", HF_TEST_FLAGS)

bool FHFSkirtingForeignDoorwayTest::RunTest(const FString& Parameters)
{
	const FHFRoom Room = MakeSkirtingRoom();

	TArray<FHFWall> Walls = MakeSkirtingWalls();

	// COLLINEAR AND ADJACENT, which is the case that used to break. This wall continues the room's
	// south wall eastward, past the corner and into the next room; the room does not touch it.
	Walls.Add(MakeSkirtingWall(TEXT("W_S_Next"), FVector2D(400, 0), FVector2D(800, 0), 11.5));

	// A door in it, close enough to the shared corner that a 180 gap would have reached in.
	const TArray<FHFOpening> Openings = {
		MakeSkirtingOpening(TEXT("D_NextRoom"), TEXT("W_S_Next"), EHFOpeningKind::Door, 40.0, 90.0, 0.0)
	};

	const FHFSkirtingPlan Plan = FHFSkirting::For(Room, Walls, Openings, {}, {});

	TestEqual(TEXT("A door in a wall this room does not have is ignored"), Plan.Breaks.Num(), 0);
	TestNearlyEqual(TEXT("The south run is whole"), Plan.Edges[0].Runs.Num() == 1
		? Plan.Edges[0].Runs[0].Length() : 0.0, 400.0, 0.01);

	return true;
}

/**
 * A WARDROBE SCRIBED TO THE WALL TAKES THE SKIRTING OUT BEHIND IT - and a bed does not.
 *
 * The opposite error to the doorways, and equally wrong: an 1800 wardrobe standing on the floor had
 * an 18 mm board running through the back of its plinth. Loose furniture must not do this, because
 * the skirting genuinely does run on behind a bed.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFSkirtingJoineryTest,
	"HouseForge.Model.SkirtingStopsAtScribedJoinery", HF_TEST_FLAGS)

bool FHFSkirtingJoineryTest::RunTest(const FString& Parameters)
{
	const FHFRoom Room = MakeSkirtingRoom();
	const TArray<FHFWall> Walls = MakeSkirtingWalls();

	// Against the south wall, whose face stands 5.75 inside the boundary. 180 wide by 60 deep.
	FHFFixture Wardrobe;
	Wardrobe.Id = TEXT("F_Wardrobe");
	Wardrobe.RoomId = Room.Id;
	Wardrobe.Type = EHFFixtureType::Wardrobe;
	Wardrobe.Position = FVector2D(150.0, 5.75 + 30.0);
	Wardrobe.Footprint = FVector2D(180.0, 60.0);
	Wardrobe.Height = 240.0;
	Wardrobe.BaseZ = 0.0;

	// A bed against the north wall. Loose furniture; the skirting runs behind it.
	FHFFixture Bed = Wardrobe;
	Bed.Id = TEXT("F_Bed");
	Bed.Type = EHFFixtureType::Bed;
	Bed.Position = FVector2D(200.0, 300.0 - 5.75 - 100.0);
	Bed.Footprint = FVector2D(150.0, 200.0);

	// A run of wall cabinets over the east wall. Built in, but it never reaches the floor.
	FHFFixture WallUnit = Wardrobe;
	WallUnit.Id = TEXT("F_WallUnit");
	WallUnit.Type = EHFFixtureType::KitchenWallCabinet;
	WallUnit.Position = FVector2D(400.0 - 5.75 - 17.5, 150.0);
	WallUnit.Footprint = FVector2D(35.0, 120.0);
	WallUnit.BaseZ = 140.0;

	const FHFSkirtingPlan Plan = FHFSkirting::For(Room, Walls, {}, {}, { Wardrobe, Bed, WallUnit });

	TestEqual(TEXT("Only the wardrobe interrupts it"), Plan.Breaks.Num(), 1);
	if (Plan.Breaks.Num() == 1)
	{
		TestTrue(TEXT("And it is named as joinery"),
			Plan.Breaks[0].Cause == EHFSkirtingBreakCause::Joinery);
		TestEqual(TEXT("By its own id"), Plan.Breaks[0].SourceId, FName(TEXT("F_Wardrobe")));
		TestNearlyEqual(TEXT("Over exactly its own width"), Plan.Breaks[0].Length(), 180.0, 0.01);
	}

	// SAME TYPE, LIFTED OFF THE FLOOR. A wardrobe whose underside clears the top of the skirting
	// passes over it, so the physical test is height and not type alone.
	FHFFixture Floating = Wardrobe;
	Floating.BaseZ = Room.SkirtingHeight + 1.0;
	TestEqual(TEXT("Joinery that clears the skirting leaves it whole"),
		FHFSkirting::For(Room, Walls, {}, {}, { Floating }).Breaks.Num(), 0);

	// SAME TYPE, STANDING OFF THE WALL. Nothing to scribe to, so the skirting runs on behind it.
	FHFFixture Islanded = Wardrobe;
	Islanded.Position = FVector2D(150.0, 150.0);
	TestEqual(TEXT("Joinery clear of the plaster leaves it whole"),
		FHFSkirting::For(Room, Walls, {}, {}, { Islanded }).Breaks.Num(), 0);

	return true;
}

/**
 * A SKIRTING GOES ROUND A COLUMN. IT DOES NOT STOP AT ONE.
 *
 * Found by rendering the flat, and by nothing else: the run went straight into the concrete and came
 * out the far side, so the board was there, watertight, correctly tagged - and invisible for the 450
 * it spent inside the column, with the column's three exposed faces bare. From the room that is
 * indistinguishable from a missing length, and it is what the reference flat looked like in eighteen
 * places once the doorway gaps were right.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFSkirtingColumnTest,
	"HouseForge.Model.SkirtingReturnsRoundAColumn", HF_TEST_FLAGS)

bool FHFSkirtingColumnTest::RunTest(const FString& Parameters)
{
	const FHFRoom Room = MakeSkirtingRoom();
	const TArray<FHFWall> Walls = MakeSkirtingWalls();

	// A 45 x 23 column in the 115 south wall, whose face stands 5.75 inside the boundary. Centred on
	// the boundary line, so it projects 11.5 - 5.75 = 5.75 past the plaster, over its 45 width.
	FHFColumn Column;
	Column.Id = TEXT("COL_1");
	Column.Position = FVector2D(200.0, 0.0);
	Column.Size = FVector2D(45.0, 23.0);
	Column.Height = 300.0;
	Column.BaseZ = 0.0;

	FHFSkirtingParams Params;
	Params.Depth = 1.8;

	const FHFSkirtingPlan Plan = FHFSkirting::For(Room, Walls, {}, { Column }, {}, Params);

	if (!TestEqual(TEXT("The column breaks the straight run"), Plan.Breaks.Num(), 1))
	{
		return false;
	}

	TestTrue(TEXT("And is named as structure"),
		Plan.Breaks[0].Cause == EHFSkirtingBreakCause::Structure);
	TestEqual(TEXT("By its own id"), Plan.Breaks[0].SourceId, FName(TEXT("COL_1")));
	TestNearlyEqual(TEXT("Over exactly its own width"), Plan.Breaks[0].Length(), 45.0, 0.01);
	TestNearlyEqual(TEXT("Starting at its near face"), Plan.Breaks[0].Start, 177.5, 0.01);

	// Three returns: out along the near flank, across the face, back along the far one.
	TestEqual(TEXT("Three lengths go round it"), Plan.Returns.Num(), 3);

	// THE SKIRTING GETS LONGER, NOT SHORTER. A break that removed 450 and gave nothing back is the
	// defect; this is the assertion that separates the fix from it.
	TestTrue(*FString::Printf(
		TEXT("Going round a column adds skirting rather than removing it (%.1f back for %.1f lost)"),
		Plan.ReturnLength(), Plan.BreakLength()),
		Plan.ReturnLength() > Plan.BreakLength());

	// Every return stands off the wall face, which is what makes it a return rather than a
	// duplicate of the run it replaced. The south wall's plaster is at y = 5.75.
	double DeepestOff = 0.0;
	for (const FHFSkirtingReturn& Run : Plan.Returns)
	{
		DeepestOff = FMath::Max3(DeepestOff, Run.Start.Y - 5.75, Run.End.Y - 5.75);
	}
	TestNearlyEqual(TEXT("They reach the column's face and the section in front of it"),
		DeepestOff, 5.75 + Params.Depth, 0.01);

	// ---------------------------------------------------------------- and only when there is
	// something to go round
	//
	// A column standing less proud than the board is thick is scribed to, not returned round - the
	// skirting runs straight past a nib it can be planed against.
	FHFColumn Flush = Column;
	Flush.Size = FVector2D(45.0, 12.0);
	const FHFSkirtingPlan Scribed = FHFSkirting::For(Room, Walls, {}, { Flush }, {}, Params);
	TestEqual(TEXT("A column barely proud of the plaster is scribed to, not returned round"),
		Scribed.Breaks.Num(), 0);
	TestEqual(TEXT("...and needs no returns"), Scribed.Returns.Num(), 0);

	// A column that starts above the top of the skirting passes over it.
	FHFColumn Stub = Column;
	Stub.BaseZ = Room.SkirtingHeight + 1.0;
	TestEqual(TEXT("A column that does not reach the floor leaves the run whole"),
		FHFSkirting::For(Room, Walls, {}, { Stub }, {}, Params).Breaks.Num(), 0);

	return true;
}

/**
 * THE WHOLE FLAT, MEASURED. This is the assertion that would have caught the defect.
 *
 * For every room: the boundary is exactly the skirting plus the breaks, and every break names either
 * a doorway in a wall set out on that very edge or a piece of scribed joinery standing in that room.
 * Nothing else may remove a centimetre of skirting.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFSkirtingCoverageTest,
	"HouseForge.Model.SkirtingCoverageIsFullyExplained", HF_TEST_FLAGS)

bool FHFSkirtingCoverageTest::RunTest(const FString& Parameters)
{
	FHFHouseSpec Spec = FHFSampleHouse::Make2BHK();
	FHFUnits::ConvertToCentimeters(Spec);

	// EXACTLY WHAT THE COMPOSING LAYER PASSES. Resolving here without it would test a plan the flat
	// is never built from, and that is how 710 cm of deleted skirting stayed green.
	const TSet<FName> BuiltIds = AHFHouseActor::BuiltFixtureIds(Spec.Fixtures);

	int32 TotalBreaks = 0;

	for (const FHFRoom& Room : Spec.Rooms)
	{
		const FHFSkirtingPlan Plan = FHFSkirting::For(Room, Spec.Walls, Spec.Openings,
			Spec.Columns, Spec.Fixtures, FHFSkirtingParams(), &BuiltIds);

		// The identity. Runs are cut from the boundary and overlapping breaks merge, so this holds
		// whatever the breaks are - and it fails the moment a run is emitted outside its edge.
		TestNearlyEqual(*FString::Printf(TEXT("%s: boundary is skirting plus gaps"), *Room.Id.ToString()),
			Plan.CoveredLength() + Plan.BreakLength(), Plan.BoundaryLength(), 0.01);

		for (const FHFSkirtingBreak& Break : Plan.Breaks)
		{
			TotalBreaks++;

			const FString Where = FString::Printf(TEXT("%s edge %d: gap for '%s'"),
				*Room.Id.ToString(), Break.EdgeIndex, *Break.SourceId.ToString());

			// Inside its own edge. A gap that overhangs is a gap eating another wall's skirting.
			TestTrue(*FString::Printf(TEXT("%s starts inside the edge"), *Where), Break.Start >= -0.01);
			TestTrue(*FString::Printf(TEXT("%s ends inside the edge"), *Where),
				Break.End <= Plan.Edges[Break.EdgeIndex].Length + 0.01);
			TestTrue(*FString::Printf(TEXT("%s has length"), *Where), Break.Length() > 0.0);

			if (Break.Cause == EHFSkirtingBreakCause::Doorway)
			{
				const FHFOpening* Opening = Spec.Openings.FindByPredicate(
					[&Break](const FHFOpening& O) { return O.Id == Break.SourceId; });

				if (Opening == nullptr)
				{
					AddError(FString::Printf(TEXT("%s names no opening in the spec"), *Where));
					continue;
				}

				TestTrue(*FString::Printf(TEXT("%s is something you walk through"), *Where),
					FHFSkirting::IsDoorway(*Opening));

				// THE WALL HAS TO BE THIS EDGE'S. This is the check the old proximity match lacked.
				const FHFSkirtingEdge& Edge = Plan.Edges[Break.EdgeIndex];
				const TArray<const FHFWall*> OnEdge =
					FHFSkirting::WallsOnEdge(Edge.Start, Edge.End, Spec.Walls);

				const bool bHosted = OnEdge.ContainsByPredicate(
					[Opening](const FHFWall* W) { return W->Id == Opening->WallId; });

				TestTrue(*FString::Printf(TEXT("%s is in a wall on that edge"), *Where), bHosted);

				// Its own width and no more, whatever else is in the flat.
				TestTrue(*FString::Printf(TEXT("%s is no wider than the door plus its jambs"), *Where),
					Break.Length() <= Opening->Width + 2.1);
			}
			else if (Break.Cause == EHFSkirtingBreakCause::Structure)
			{
				const FHFColumn* Column = Spec.Columns.FindByPredicate(
					[&Break](const FHFColumn& C) { return C.Id == Break.SourceId; });

				if (Column == nullptr)
				{
					AddError(FString::Printf(TEXT("%s names no column in the spec"), *Where));
					continue;
				}

				const FHFSkirtingEdge& Edge = Plan.Edges[Break.EdgeIndex];

				double Projection = 0.0;
				double From = 0.0;
				double To = 0.0;
				TestTrue(*FString::Printf(TEXT("%s stands proud of that wall face"), *Where),
					FHFSkirting::ColumnProjectsInto(*Column, Edge.Start, Edge.End, Edge.FaceInset,
						Projection, From, To) && Projection > Plan.Depth);

				// A STRUCTURE BREAK IS A TURN, NOT AN END. It must be answered by lengths that go
				// round, or it is exactly the defect it was written to fix.
				const bool bReturned = Plan.Returns.ContainsByPredicate(
					[&Break](const FHFSkirtingReturn& R) { return R.SourceId == Break.SourceId; });
				TestTrue(*FString::Printf(TEXT("%s is returned round rather than stopped at"), *Where),
					bReturned);
			}
			else
			{
				const FHFFixture* Fixture = Spec.Fixtures.FindByPredicate(
					[&Break](const FHFFixture& F) { return F.Id == Break.SourceId; });

				if (Fixture == nullptr)
				{
					AddError(FString::Printf(TEXT("%s names no fixture in the spec"), *Where));
					continue;
				}

				TestEqual(*FString::Printf(TEXT("%s is joinery in this room"), *Where),
					Fixture->RoomId, Room.Id);
				TestTrue(*FString::Printf(TEXT("%s is scribed joinery"), *Where),
					FHFSkirting::IsScribedJoinery(Fixture->Type));
				TestTrue(*FString::Printf(TEXT("%s reaches the floor"), *Where),
					Fixture->BaseZ < Room.SkirtingHeight);

				// AND SOMETHING WILL ACTUALLY STAND IN IT. This is the assertion the user's first
				// complaint needed: a gap the eye can see into is a defect however correct the rule
				// that made it. The day a base cabinet starts building, its break comes back on its
				// own and this keeps passing.
				TestTrue(*FString::Printf(TEXT("%s is a fixture the house builds"), *Where),
					AHFHouseActor::BuildsGeometryFor(Fixture->Type));
			}
		}

		// Every return belongs to a break, so a length of skirting cannot appear round a column the
		// run was never interrupted for.
		for (const FHFSkirtingReturn& Run : Plan.Returns)
		{
			const bool bHasBreak = Plan.Breaks.ContainsByPredicate([&Run](const FHFSkirtingBreak& B)
			{
				return B.Cause == EHFSkirtingBreakCause::Structure && B.SourceId == Run.SourceId;
			});

			TestTrue(*FString::Printf(TEXT("%s: the return round '%s' answers a break"),
				*Room.Id.ToString(), *Run.SourceId.ToString()), bHasBreak);
		}
	}

	TestTrue(TEXT("The flat has skirting breaks to check at all"), TotalBreaks > 10);

	// ---------------------------------------------------------------- the room that showed the fault
	//
	// The common bathroom's four gaps were the living room's door, the foyer's, the master bedroom's
	// and the kitchen's. It has one door of its own and that is all that may interrupt it.
	const FHFRoom* CBath = Spec.Rooms.FindByPredicate(
		[](const FHFRoom& R) { return R.Id == FName(TEXT("R_CBath")); });

	if (CBath != nullptr)
	{
		const FHFSkirtingPlan Plan = FHFSkirting::For(*CBath, Spec.Walls, Spec.Openings, Spec.Columns,
			Spec.Fixtures, FHFSkirtingParams(), &BuiltIds);
		TestEqual(TEXT("The common bathroom has one gap"), Plan.Breaks.Num(), 1);
		if (Plan.Breaks.Num() == 1)
		{
			TestEqual(TEXT("And it is its own door"), Plan.Breaks[0].SourceId, FName(TEXT("D_CBath")));
		}
	}
	else
	{
		AddError(TEXT("The sample flat has no R_CBath to check."));
	}

	return true;
}

/**
 * THE GAPS THE USER SAW, IN CENTIMETRES, AND THE FACT THAT THEY ARE GONE.
 *
 * "Skirting doesnt cover entirely. They stop abruptly in the middle." Measured off the built
 * triangles at the time: the foyer lost 120 cm to a shoe rack, bedroom 2 lost 120 cm to a study
 * table, and the kitchen lost 230 + 240 cm to two runs of base units - 710 cm of the flat's
 * perimeter with the board deleted and nothing in front of it, because eight fixture types answer
 * true to IsScribedJoinery and only Wardrobe was built.
 *
 * So this measures the two resolutions against each other. Told nothing, the resolver still cuts for
 * every scribed type, which is the right answer for a room being resolved on its own. Told what the
 * house builds, the unbuilt units leave the run whole - and the units that ARE built still cut,
 * because a carcass really does stand there.
 *
 * THE BUILT SET GROWS AS THE CATALOGUE LANDS, AND THAT IS THE POINT OF THE TEST RATHER THAN A
 * NUISANCE. Milestone 9's kitchen group made the two runs of base units real, so the kitchen's
 * 230 + 240 cm came back OUT of the recovered total and went back to being genuine breaks with
 * genuine carcasses in front of them. The assertions below are therefore written against
 * BuildsGeometryFor rather than against a number somebody typed: a break must exist exactly where
 * something is built, and the recovered skirting must be exactly the runs that are not.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFSkirtingUnbuiltJoineryTest,
	"HouseForge.Model.SkirtingIsNotCutForJoineryNobodyBuilds", HF_TEST_FLAGS)

bool FHFSkirtingUnbuiltJoineryTest::RunTest(const FString& Parameters)
{
	FHFHouseSpec Spec = FHFSampleHouse::Make2BHK();
	FHFUnits::ConvertToCentimeters(Spec);

	const TSet<FName> BuiltIds = AHFHouseActor::BuiltFixtureIds(Spec.Fixtures);

	double RecoveredTotal = 0.0;
	int32 JoineryBreaks = 0;

	/** Every fixture that interrupts the board somewhere in the flat, on the informed plan. */
	TSet<FName> Interrupts;

	for (const FHFRoom& Room : Spec.Rooms)
	{
		const FHFSkirtingPlan Uninformed = FHFSkirting::For(Room, Spec.Walls, Spec.Openings,
			Spec.Columns, Spec.Fixtures);
		const FHFSkirtingPlan Built = FHFSkirting::For(Room, Spec.Walls, Spec.Openings,
			Spec.Columns, Spec.Fixtures, FHFSkirtingParams(), &BuiltIds);

		// The identity still holds on the informed plan; recovering skirting must not invent any.
		TestNearlyEqual(*FString::Printf(TEXT("%s: boundary is still skirting plus gaps"), *Room.Id.ToString()),
			Built.CoveredLength() + Built.BreakLength(), Built.BoundaryLength(), 0.01);

		TestTrue(*FString::Printf(TEXT("%s: knowing what is built never removes skirting"), *Room.Id.ToString()),
			Built.CoveredLength() >= Uninformed.CoveredLength() - 0.01);

		RecoveredTotal += Built.CoveredLength() - Uninformed.CoveredLength();

		for (const FHFSkirtingBreak& Break : Built.Breaks)
		{
			if (Break.Cause == EHFSkirtingBreakCause::Joinery)
			{
				++JoineryBreaks;
				Interrupts.Add(Break.SourceId);
			}

			// EVERY BREAK, IN EVERY ROOM, IS PAID FOR. This used to be asked of three rooms by name -
			// the ones the user had looked at - which is a check on the rooms somebody happened to
			// think of rather than on the flat. It costs nothing to ask it of all thirteen.
			TestTrue(*FString::Printf(TEXT("%s: the gap for '%s' has something built standing in it"),
				*Room.Id.ToString(), *Break.SourceId.ToString()),
				Break.Cause != EHFSkirtingBreakCause::Joinery || BuiltIds.Contains(Break.SourceId));
		}
	}

	AddInfo(FString::Printf(TEXT("Skirting recovered across the flat: %.0f cm over %d joinery breaks."),
		RecoveredTotal, JoineryBreaks));

	// NOTHING IS RECOVERED ANY MORE, AND THAT IS THE MILESTONE FINISHING THE JOB. Recovery is the
	// skirting the informed plan puts back because the fixture that would have cut it is not built,
	// so it was 710 cm when only the wardrobe existed and 230 after the kitchen group. The bedroom
	// group takes it to zero: the TV units, the shoe rack and the nightstands are built, and the
	// study table stopped claiming a break it could not pay for - a desk is not scribed joinery,
	// because the board runs on through its knee hole.
	//
	// Zero is also a figure that STAYS zero as the catalogue lands, which is why it replaces the
	// window this test used to assert. Every remaining type either builds or does not scribe, so the
	// only way this can go positive again is somebody adding a scribed type without geometry - which
	// is precisely the 710 cm of missing board the mechanism exists to prevent.
	TestNearlyEqual(TEXT("Every run this flat scribes into its skirting now has a carcass in it"),
		RecoveredTotal, 0.0, 0.01);

	// AND THE OTHER DIRECTION, which no number can express and which is the half that would fail
	// silently. A break with nothing in it is bare plaster; a carcass with no break is a carcass
	// driven through a skirting board, and it looks equally wrong from the same doorway. So every
	// scribed run the house builds against a wall must interrupt the board it stands on - derived
	// from IsScribedJoinery and BuildsGeometryFor rather than from a count somebody keeps in step by
	// hand, which is exactly the drift that put the missing board there in the first place.
	for (const FHFFixture& Fixture : Spec.Fixtures)
	{
		if (!FHFSkirting::IsScribedJoinery(Fixture.Type) || !BuiltIds.Contains(Fixture.Id)
			|| Fixture.AnchorWallId.IsNone())
		{
			continue;
		}

		// A ROOM WITH NO SKIRTING HAS NO BOARD TO INTERRUPT, and both bathrooms are exactly that -
		// tiled to the ceiling, SkirtingHeight zero, so the resolver never reaches its scribed-joinery
		// pass at all. The master bathroom's vanity is scribed joinery and IS built, and it still
		// cannot appear in this set; asking it to would be asking for a break in a board that was
		// never specified, which is the mirror image of the missing board this test exists to catch.
		const FHFRoom* Room = Spec.FindRoom(Fixture.RoomId);
		if (Room == nullptr || Room->SkirtingHeight <= 0.0)
		{
			continue;
		}

		TestTrue(*FString::Printf(TEXT("%s is scribed joinery and is really built"),
			*Fixture.Id.ToString()), AHFHouseActor::BuildsGeometryFor(Fixture.Type));

		TestTrue(*FString::Printf(
			TEXT("'%s' stands against '%s' and interrupts the board it is scribed to"),
			*Fixture.Id.ToString(), *Fixture.AnchorWallId.ToString()),
			Interrupts.Contains(Fixture.Id));
	}

	// The count is reported rather than asserted. How many breaks a flat has is a fact about how many
	// runs it happens to contain and which corners they turn - the west kitchen run cuts TWICE,
	// because it runs hard into the north-west corner and stands against two of the kitchen's walls -
	// and pinning it makes every future fixture a failing test rather than a passing one.
	TestTrue(TEXT("Scribed joinery does interrupt the board somewhere"), JoineryBreaks > 0);

	return true;
}

#undef HF_TEST_FLAGS

#endif // WITH_DEV_AUTOMATION_TESTS
