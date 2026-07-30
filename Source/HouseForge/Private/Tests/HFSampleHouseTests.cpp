// Copyright Siddartha G. All Rights Reserved.

#include "HouseForge.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Model/HFSampleHouse.h"
#include "Model/HFSpecSerializer.h"
#include "Model/HFSpecValidator.h"
#include "Model/HFTypes.h"

#define HF_TEST_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The reference 2BHK is what every later milestone builds and screenshots. If it does not
 * validate cleanly, every downstream test is measuring a broken house.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFSampleHouseValidatesTest, "HouseForge.Model.SampleHouseValidates", HF_TEST_FLAGS)

bool FHFSampleHouseValidatesTest::RunTest(const FString& Parameters)
{
	const FHFHouseSpec Spec = FHFSampleHouse::Make2BHK();
	const FHFValidationResult Result = FHFSpecValidator::Validate(Spec);

	if (Result.HasErrors())
	{
		AddError(FString::Printf(TEXT("Sample 2BHK does not validate:\n%s"), *Result.ToString()));
	}
	TestFalse(TEXT("Sample 2BHK has no validation errors"), Result.HasErrors());

	// Warnings fail too, and that is the change.
	//
	// This test used to print them and pass. That is how a doorway built across a column, two
	// bedroom doors opening into bathrooms, and six fixtures standing in openings all lived in the
	// golden fixture at once: every one of them was reported on every run, by name, with the
	// millimetres, and nothing failed. A reference flat is not a place to keep known defects -
	// everything downstream measures against it, and a warning nobody has to clear is a warning
	// nobody reads.
	if (Result.HasWarnings())
	{
		AddError(FString::Printf(
			TEXT("Sample 2BHK validates with warnings, which the reference flat is not allowed to carry:\n%s"),
			*Result.ToString()));
	}
	TestFalse(TEXT("Sample 2BHK has no validation warnings either"), Result.HasWarnings());

	return true;
}

/** Structural expectations, so an accidental edit to the layout cannot pass unnoticed. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFSampleHouseShapeTest, "HouseForge.Model.SampleHouseShape", HF_TEST_FLAGS)

bool FHFSampleHouseShapeTest::RunTest(const FString& Parameters)
{
	FHFHouseSpec Spec = FHFSampleHouse::Make2BHK();

	TestEqual(TEXT("Spec is authored in millimetres"), Spec.Units, EHFUnits::Millimeters);
	TestEqual(TEXT("Sample has 12 rooms"), Spec.Rooms.Num(), 12);
	// 22, not 21. The service band lost W_CBath_MBath when the utility left it, and the utility
	// gained W_Kitchen_Util and W_Kitchen_Util_S where it now sits in the kitchen's corner.
	TestEqual(TEXT("Sample has 22 walls"), Spec.Walls.Num(), 22);
	// 7 and 8, not 8 and 9. BM_Living_Cross crossed the middle of the living room on no wall line
	// and between no columns, and FC_Living_Beam was the bulkhead that existed only to box it in.
	TestEqual(TEXT("Sample has 7 false ceilings"), Spec.FalseCeilings.Num(), 7);
	TestEqual(TEXT("Sample has 8 beams"), Spec.Beams.Num(), 8);
	TestEqual(TEXT("Sample has 11 columns"), Spec.Columns.Num(), 11);

	TestTrue(TEXT("Sample has a main entrance door"), Spec.Openings.ContainsByPredicate(
		[](const FHFOpening& O) { return O.Id == FName(TEXT("D_Main")); }));

	// Three balconies: living, master bedroom, and the utility wash area.
	int32 Balconies = 0;
	for (const FHFRoom& Room : Spec.Rooms)
	{
		Balconies += (Room.Type == EHFRoomType::Balcony) ? 1 : 0;
	}
	TestEqual(TEXT("Sample has 3 balconies"), Balconies, 3);

	// No beam crosses the interior of any room. Every one of the eight sits over a wall for its
	// whole length, where the wall itself conceals it.
	//
	// This used to say the opposite for the living room: BM_Living_Cross was asserted PRESENT, and
	// the assertion was written as though a beam through the middle of a room were a feature of the
	// layout worth pinning. It was the defect. Sweeping every room is the assertion that was wanted
	// all along - it is what would have caught the beam, and it holds the line for the next one.
	for (const FHFRoom& Room : Spec.Rooms)
	{
		const FHFBeam* Crossing = Spec.DeepestBeamOverRoom(Room.Id);
		TestNull(*FString::Printf(TEXT("No beam crosses the interior of '%s' (%s)"),
			*Room.Id.ToString(), *Room.Name), Crossing);
	}

	// Every room must be reachable by the builder, and every opening must host on a real wall.
	for (const FHFFalseCeiling& Ceiling : Spec.FalseCeilings)
	{
		TestNotNull(*FString::Printf(TEXT("Ceiling '%s' references a real room"), *Ceiling.RoomId.ToString()),
			Spec.FindRoom(Ceiling.RoomId));
	}
	for (const FHFOpening& Opening : Spec.Openings)
	{
		TestNotNull(*FString::Printf(TEXT("Opening '%s' references a real wall"), *Opening.Id.ToString()),
			Spec.FindWall(Opening.WallId));
	}

	// Carpet area of a real 2BHK, sanity-checked in square metres.
	const double AreaSqM = Spec.TotalFloorArea() / 1'000'000.0;
	TestTrue(*FString::Printf(TEXT("Total floor area %.1f sq m is plausible for a 2BHK"), AreaSqM),
		AreaSqM > 80.0 && AreaSqM < 120.0);

	// After conversion the same house must measure the same in centimetres.
	FHFUnits::ConvertToCentimeters(Spec);
	const double AreaSqMAfter = Spec.TotalFloorArea() / 10'000.0;
	TestNearlyEqual(TEXT("Floor area survives unit conversion"), AreaSqMAfter, AreaSqM, 0.01);

	return true;
}

/**
 * Nothing in the reference flat stands in front of any opening - window or doorway.
 *
 * A window used to be fixed glazing: a wardrobe in front of one looked odd and nothing more, and
 * both of the flat's blocked windows survived every test in the suite because no test looked. A
 * sliding window is different. Its catch is on the meeting stile of the running sash, at about
 * mid-height in the middle of the opening, and a wardrobe or a run of kitchen wall units across that
 * is a window that cannot be opened - the one failure a screenshot of a closed window will not show.
 *
 * Doorways used to be reported and allowed. They were symptoms of a plan that could not work: the
 * corridor had no wall onto either bedroom, so both bedroom doors were hung on bathroom walls and
 * the bathroom fittings behind them were exactly where a bathroom's fittings belong. The circulation
 * has been redrawn since, the doors open into the rooms they serve, and a fixture standing in a
 * doorway is now what it always should have been: a failure, in a flat somebody has to walk through.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFSampleHouseWindowsAreClearTest, "HouseForge.Model.SampleHouseWindowsAreClear", HF_TEST_FLAGS)

bool FHFSampleHouseWindowsAreClearTest::RunTest(const FString& Parameters)
{
	const FHFHouseSpec Spec = FHFSampleHouse::Make2BHK();
	const FHFValidationResult Result = FHFSpecValidator::Validate(Spec);

	int32 Blocked = 0;

	for (const FHFValidationIssue& Issue : Result.Issues)
	{
		if (Issue.Code == TEXT("OpeningBlockedByFixture") || Issue.Code == TEXT("OpeningBlockedByColumn"))
		{
			++Blocked;
			AddError(Issue.Message);
		}
	}

	TestEqual(TEXT("No opening in the reference flat is obstructed"), Blocked, 0);

	// The plan has to hold together as circulation, not only as geometry: a door that opens into
	// masonry passes every dimensional check in the file above.
	TestFalse(TEXT("No door in the reference flat swings into solid construction"),
		Result.Contains(TEXT("SwingBlocked")));

	return true;
}

/**
 * Every room in the flat can be reached from the front door.
 *
 * Not a dimension - a plan that fails this validates perfectly, because a room with no door is a
 * room with one opening fewer and nothing else. The reference flat failed it for thirty-four
 * commits: D_Main was the only opening the foyer had, so the front door led into a sealed 1800 x
 * 1800 box and no room in the dwelling was reachable from it at all. Nothing looked, because every
 * test here asked about sizes.
 *
 * Rooms are joined where an opening's wall separates them, walked breadth-first from the room the
 * entrance door opens into, and balconies count - a balcony reached through nothing is a slab in
 * mid-air.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFSampleHouseIsConnectedTest, "HouseForge.Model.SampleHouseIsConnected", HF_TEST_FLAGS)

bool FHFSampleHouseIsConnectedTest::RunTest(const FString& Parameters)
{
	const FHFHouseSpec Spec = FHFSampleHouse::Make2BHK();

	// Which rooms each opening joins: the two the wall runs between, found by stepping off the
	// opening's centre to either side of its wall.
	TMap<FName, TSet<FName>> Neighbours;
	for (const FHFRoom& Room : Spec.Rooms)
	{
		Neighbours.Add(Room.Id);
	}

	auto RoomAt = [&Spec](const FVector2D& Point) -> FName
	{
		const FHFRoom* Found = Spec.Rooms.FindByPredicate(
			[&Point](const FHFRoom& Room) { return Room.Boundary.Num() >= 3 && Room.ContainsPoint(Point); });
		return Found != nullptr ? Found->Id : NAME_None;
	};

	for (const FHFOpening& Opening : Spec.Openings)
	{
		const bool bIsDoor = Opening.Kind == EHFOpeningKind::Door || Opening.Kind == EHFOpeningKind::SlidingDoor;
		if (!bIsDoor)
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
		const FVector2D Centre = Wall->Start + Direction * Opening.OffsetAlongWall;

		// Far enough off the wall to be clear of its own thickness, near enough to stay in the room.
		const double Step = Wall->Thickness + 100.0;
		const FName A = RoomAt(Centre + Normal * Step);
		const FName Bside = RoomAt(Centre - Normal * Step);

		if (!A.IsNone() && !Bside.IsNone())
		{
			Neighbours[A].Add(Bside);
			Neighbours[Bside].Add(A);
		}
	}

	// The front door: the one that opens off the outside world into the flat.
	const FHFOpening* Entrance = Spec.Openings.FindByPredicate(
		[](const FHFOpening& O) { return O.Id == FName(TEXT("D_Main")); });
	if (!TestNotNull(TEXT("The flat has a main entrance"), Entrance))
	{
		return false;
	}

	const FHFWall* EntranceWall = Spec.FindWall(Entrance->WallId);
	if (!TestNotNull(TEXT("The entrance hangs on a real wall"), EntranceWall))
	{
		return false;
	}

	const FVector2D EntranceDir = (EntranceWall->End - EntranceWall->Start) / EntranceWall->Length();
	const FVector2D EntranceNormal(-EntranceDir.Y, EntranceDir.X);
	const FVector2D EntranceCentre = EntranceWall->Start + EntranceDir * Entrance->OffsetAlongWall;
	const FName Start = RoomAt(EntranceCentre + EntranceNormal * (EntranceWall->Thickness + 100.0));

	if (!TestFalse(TEXT("The entrance opens into a room"), Start.IsNone()))
	{
		return false;
	}

	TSet<FName> Reached = { Start };
	TArray<FName> Queue = { Start };
	while (!Queue.IsEmpty())
	{
		const FName Current = Queue.Pop();
		for (const FName& Next : Neighbours[Current])
		{
			if (!Reached.Contains(Next))
			{
				Reached.Add(Next);
				Queue.Add(Next);
			}
		}
	}

	for (const FHFRoom& Room : Spec.Rooms)
	{
		if (!Reached.Contains(Room.Id))
		{
			AddError(FString::Printf(
				TEXT("Room '%s' (%s) cannot be reached from the front door. No sequence of doorways leads to it."),
				*Room.Id.ToString(), *Room.Name));
		}
	}

	TestEqual(TEXT("Every room is reachable from the front door"), Reached.Num(), Spec.Rooms.Num());

	return true;
}

/**
 * The committed Reference/Specs/Sample2BHK.json is a generated artifact of Make2BHK(). If someone
 * edits the layout in code and forgets to re-export, the drawings and the spec would disagree -
 * so the gate fails here rather than letting them drift apart silently.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFSampleSpecFileInSyncTest, "HouseForge.Model.SampleSpecFileInSync", HF_TEST_FLAGS)

bool FHFSampleSpecFileInSyncTest::RunTest(const FString& Parameters)
{
	const FString Path = FHFSampleHouse::GetCommittedSpecPath();
	if (!TestFalse(TEXT("Committed spec path resolves"), Path.IsEmpty()))
	{
		return false;
	}

	if (!FPaths::FileExists(Path))
	{
		AddError(FString::Printf(
			TEXT("Committed spec missing at '%s'. Regenerate it with the console command: HouseForge.ExportSampleSpec"),
			*Path));
		return false;
	}

	FHFHouseSpec FromFile;
	FString Error;
	if (!TestTrue(TEXT("Committed spec parses"), FHFSpecSerializer::LoadFromFile(Path, FromFile, Error)))
	{
		AddError(Error);
		return false;
	}

	const FHFHouseSpec FromCode = FHFSampleHouse::Make2BHK();

	// Compare the serialised forms: that catches a changed dimension or a dropped fixture, not
	// just a changed count.
	FString JsonFromCode;
	FString JsonFromFile;
	FString SerialiseError;
	TestTrue(TEXT("Code spec serialises"), FHFSpecSerializer::ToJsonString(FromCode, JsonFromCode, SerialiseError));
	TestTrue(TEXT("File spec re-serialises"), FHFSpecSerializer::ToJsonString(FromFile, JsonFromFile, SerialiseError));

	if (JsonFromCode != JsonFromFile)
	{
		AddError(FString::Printf(
			TEXT("Committed spec at '%s' is out of date with FHFSampleHouse::Make2BHK(). ")
			TEXT("Regenerate it with the console command: HouseForge.ExportSampleSpec"),
			*Path));
	}

	TestEqual(TEXT("Committed spec matches the code definition"), JsonFromFile, JsonFromCode);

	return true;
}

#undef HF_TEST_FLAGS

#endif // WITH_DEV_AUTOMATION_TESTS
