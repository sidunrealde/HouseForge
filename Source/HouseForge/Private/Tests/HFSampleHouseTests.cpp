// Copyright Siddartha G. All Rights Reserved.

#include "HouseForge.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Algo/AnyOf.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Model/HFSampleHouse.h"
#include "Model/HFSpecSerializer.h"
#include "Model/HFSpecValidator.h"
#include "Model/HFTypes.h"

#define HF_TEST_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A warning the reference flat is allowed to carry, and the reason it is allowed.
 *
 * There are none. The type exists so that there is somewhere principled to put one, because the
 * alternative to having somewhere is what actually happens: the first time a defensible warning
 * appears, whoever meets it deletes the assertion instead, and the check is gone for every future
 * warning too.
 */
struct FHFAllowedWarning
{
	/** Validator rule code, e.g. "ColumnStandsFree". */
	const TCHAR* Code;

	/** Element it is allowed on. Empty means the rule is allowed anywhere in the flat. */
	const TCHAR* ElementId;

	/** Why this one is acceptable. Written for whoever finds it in a year and wonders. */
	const TCHAR* Why;
};

/**
 * The reference 2BHK is what every later milestone builds and screenshots. If it does not
 * validate cleanly, every downstream test is measuring a broken house.
 *
 * Warnings count as "not cleanly", and that is the point of this test.
 *
 * It used to fail only on HasErrors() and route the warnings to AddInfo, which made a warning-level
 * defect in the golden fixture invisible to the gate BY DESIGN. That is how a sealed foyer, a beam
 * spanning 6.6 m between nothing, a doorway built across a column, two bedroom doors opening into
 * bathrooms and ten fixtures standing in openings all survived a green gate at once. Every one of
 * them was reported on every single run, by name, with the millimetres - and nothing failed, so
 * nobody read it.
 *
 * But a blanket "any warning fails" is not the answer either, and it was the previous attempt here.
 * A warning is a warning precisely because it is sometimes acceptable - the validator says so
 * itself: "buildable, but probably not what the drawing meant". A rule that converts every
 * judgement call into a build break is a rule that gets suppressed the first time somebody is in a
 * hurry, and then it protects nothing.
 *
 * So the bar is: the reference flat produces exactly the warnings this test NAMES, and nothing
 * else. Today that list is empty, so the assertion reduces to zero warnings. Three things follow,
 * and the third is what makes it survive contact with a hurry:
 *
 *   - an unexpected warning FAILS, with its code, its element and its message;
 *   - an expected warning is VISIBLE in the run, quoting the reason it was accepted, so an
 *     exemption cannot become invisible the way the old AddInfo behaviour made everything invisible;
 *   - an exemption that no longer fires FAILS, so the list cannot rot into a pile of stale
 *     suppressions that quietly excuse defects nobody has looked at since.
 *
 * Adding an entry is deliberately a code change with a written justification next to it, reviewable
 * in the diff. Deleting the check is not the path of least resistance any more; naming the warning
 * is.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFSampleHouseValidatesTest, "HouseForge.Model.SampleHouseValidates", HF_TEST_FLAGS)

bool FHFSampleHouseValidatesTest::RunTest(const FString& Parameters)
{
	// Deliberately empty. The reference flat is clean, and every warning it ever carried turned out
	// to be a real defect in the plan rather than a judgement call - see the commit history for the
	// foyer, the beam and the blocked doorways. If you are adding the first entry, the bar is that
	// you can write down why a reader should not act on it.
	static const TArray<FHFAllowedWarning> Allowed = {};

	const FHFHouseSpec Spec = FHFSampleHouse::Make2BHK();
	const FHFValidationResult Result = FHFSpecValidator::Validate(Spec);

	if (Result.HasErrors())
	{
		AddError(FString::Printf(TEXT("Sample 2BHK does not validate:\n%s"), *Result.ToString()));
	}
	TestFalse(TEXT("Sample 2BHK has no validation errors"), Result.HasErrors());

	TArray<bool> Fired;
	Fired.Init(false, Allowed.Num());

	int32 Excused = 0;
	int32 Unexpected = 0;

	for (const FHFValidationIssue& Issue : Result.Issues)
	{
		if (Issue.Severity != EHFValidationSeverity::Warning)
		{
			continue;
		}

		const int32 Match = Allowed.IndexOfByPredicate([&Issue](const FHFAllowedWarning& Entry)
		{
			const bool bAnyElement = (Entry.ElementId == nullptr) || (*Entry.ElementId == TEXT('\0'));
			return Issue.Code == Entry.Code
				&& (bAnyElement || Issue.ElementId == FName(Entry.ElementId));
		});

		if (Match != INDEX_NONE)
		{
			Fired[Match] = true;
			++Excused;

			// Named, therefore visible - with the justification, so a reader can disagree with it.
			AddInfo(FString::Printf(
				TEXT("Known warning '%s' on '%s' is accepted: %s (%s)"),
				*Issue.Code, *Issue.ElementId.ToString(), Allowed[Match].Why, *Issue.Message));
			continue;
		}

		++Unexpected;
		AddError(FString::Printf(
			TEXT("Sample 2BHK reports an unexpected warning '%s' on '%s': %s\n")
			TEXT("The reference flat is the fixture every milestone measures against, so it must be clean. ")
			TEXT("Either fix the flat in FHFSampleHouse::Make2BHK, or - if this warning is genuinely ")
			TEXT("acceptable - add it to the Allowed list in this test with the reason, so it stays visible."),
			*Issue.Code, *Issue.ElementId.ToString(), *Issue.Message));
	}

	// The explicit zero. With an empty Allowed list this says the flat carries no warnings at all;
	// with entries in it, it says the flat carries no warnings beyond the named ones.
	TestEqual(TEXT("Sample 2BHK produces no warnings the test has not named"), Unexpected, 0);
	TestEqual(TEXT("Every warning in the reference flat is accounted for"),
		Excused + Unexpected, Result.CountOf(EHFValidationSeverity::Warning));

	// A stale exemption is a defect excused by a note about a problem that no longer exists, and it
	// will happily go on excusing the next one that happens to share its rule code.
	for (int32 Index = 0; Index < Allowed.Num(); ++Index)
	{
		if (!Fired[Index])
		{
			AddError(FString::Printf(
				TEXT("The exemption for '%s' on '%s' no longer matches any warning, so it is excusing ")
				TEXT("nothing and would silently excuse the next warning with that code. Remove it. ")
				TEXT("It was justified as: %s"),
				Allowed[Index].Code,
				(Allowed[Index].ElementId != nullptr) ? Allowed[Index].ElementId : TEXT("(any)"),
				Allowed[Index].Why));
		}
	}

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

	// Every way an opening can be unusable, not only the two this test started with.
	//
	// It counted OpeningBlockedByFixture and OpeningBlockedByColumn, and passed on a flat with two
	// rooms nobody could walk into. Both of those rules ask whether something is IN the opening, and
	// nothing in the flat was: the refrigerator stood 19 cm in front of the utility door and the
	// shower 33 cm in front of the balcony door, and the common bathroom's leaf could not open past
	// its own WC. A doorway is unusable in three separate ways and this asks about all of them.
	static const TCHAR* const Obstructions[] = {
		TEXT("OpeningBlockedByFixture"),		// something built across a window
		TEXT("OpeningBlockedByColumn"),			// a column in the reveal
		TEXT("DoorwayNotClear"),				// no walkable width left in front of a door
		TEXT("DoorSwingHitsFixture"),			// the width is fine; the leaf cannot swing
		TEXT("FixtureClashesWithStructure"),	// a fitting built into a column or a beam
	};

	int32 Blocked = 0;

	for (const FHFValidationIssue& Issue : Result.Issues)
	{
		const bool bIsObstruction = Algo::AnyOf(Obstructions,
			[&Issue](const TCHAR* Code) { return Issue.Code == Code; });

		if (bIsObstruction)
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
