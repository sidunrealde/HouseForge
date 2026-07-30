// Copyright Siddartha G. All Rights Reserved.

#include "HouseForge.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Model/HFSampleHouse.h"
#include "Model/HFSpecValidator.h"
#include "Model/HFTypes.h"
#include "Tests/HFSpecTestHelpers.h"

#define HF_TEST_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
	using namespace HouseForgeTest;

	/**
	 * Asserts that breaking one thing about the known-good spec raises exactly the expected rule.
	 *
	 * Checking the code rather than just "some error" is what stops a test passing for the wrong
	 * reason - a spec can easily be invalid in a way the test did not intend.
	 */
	void ExpectIssue(
		FAutomationTestBase& Test,
		const FHFHouseSpec& Spec,
		const TCHAR* ExpectedCode,
		EHFValidationSeverity ExpectedSeverity)
	{
		const FHFValidationResult Result = FHFSpecValidator::Validate(Spec);

		if (!Result.Contains(ExpectedCode))
		{
			Test.AddError(FString::Printf(
				TEXT("Expected validation code '%s' but it was not raised. Full report:\n%s"),
				ExpectedCode, *Result.ToString()));
			return;
		}

		const FHFValidationIssue* Issue = Result.Issues.FindByPredicate(
			[ExpectedCode](const FHFValidationIssue& I) { return I.Code == ExpectedCode; });

		Test.TestEqual(FString::Printf(TEXT("'%s' has the expected severity"), ExpectedCode),
			Issue->Severity, ExpectedSeverity);

		// A message that does not name the problem is useless to whoever has to fix the spec.
		Test.TestTrue(FString::Printf(TEXT("'%s' carries an explanatory message"), ExpectedCode),
			Issue->Message.Len() > 20);
	}
}

/** The known-good spec must pass cleanly, or every other test in this file is meaningless. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFValidatorAcceptsValidSpecTest, "HouseForge.Model.Validator.AcceptsValidSpec", HF_TEST_FLAGS)

bool FHFValidatorAcceptsValidSpecTest::RunTest(const FString& Parameters)
{
	const FHFHouseSpec Spec = MakeValidSpec();
	const FHFValidationResult Result = FHFSpecValidator::Validate(Spec);

	if (Result.HasErrors())
	{
		AddError(FString::Printf(TEXT("Baseline spec should be valid but reported errors:\n%s"), *Result.ToString()));
	}

	TestFalse(TEXT("Baseline spec has no errors"), Result.HasErrors());
	TestFalse(TEXT("Baseline spec has no warnings either"), Result.HasWarnings());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFValidatorStructuralTest, "HouseForge.Model.Validator.StructuralRules", HF_TEST_FLAGS)

bool FHFValidatorStructuralTest::RunTest(const FString& Parameters)
{
	{
		FHFHouseSpec Spec = MakeValidSpec();
		Spec.Walls.Empty();
		ExpectIssue(*this, Spec, TEXT("NoWalls"), EHFValidationSeverity::Error);
	}
	{
		FHFHouseSpec Spec = MakeValidSpec();
		Spec.Rooms.Empty();
		ExpectIssue(*this, Spec, TEXT("NoRooms"), EHFValidationSeverity::Error);
	}
	{
		FHFHouseSpec Spec = MakeValidSpec();
		Spec.SchemaVersion = 0;
		ExpectIssue(*this, Spec, TEXT("BadSchemaVersion"), EHFValidationSeverity::Error);
	}
	{
		// Two walls sharing an id: a later opening lookup would silently bind to the first.
		FHFHouseSpec Spec = MakeValidSpec();
		Spec.Walls[1].Id = Spec.Walls[0].Id;
		ExpectIssue(*this, Spec, TEXT("DuplicateWallId"), EHFValidationSeverity::Error);
	}
	{
		FHFHouseSpec Spec = MakeValidSpec();
		Spec.Rooms[0].Id = NAME_None;
		ExpectIssue(*this, Spec, TEXT("MissingId"), EHFValidationSeverity::Error);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFValidatorWallRulesTest, "HouseForge.Model.Validator.WallRules", HF_TEST_FLAGS)

bool FHFValidatorWallRulesTest::RunTest(const FString& Parameters)
{
	{
		FHFHouseSpec Spec = MakeValidSpec();
		Spec.Walls[0].End = Spec.Walls[0].Start;
		ExpectIssue(*this, Spec, TEXT("ZeroLengthWall"), EHFValidationSeverity::Error);
	}
	{
		FHFHouseSpec Spec = MakeValidSpec();
		Spec.Walls[0].Thickness = 0.0;
		ExpectIssue(*this, Spec, TEXT("NonPositiveWallThickness"), EHFValidationSeverity::Error);
	}
	{
		FHFHouseSpec Spec = MakeValidSpec();
		Spec.Walls[0].Height = -10.0;
		ExpectIssue(*this, Spec, TEXT("NonPositiveWallHeight"), EHFValidationSeverity::Error);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFValidatorOpeningRulesTest, "HouseForge.Model.Validator.OpeningRules", HF_TEST_FLAGS)

bool FHFValidatorOpeningRulesTest::RunTest(const FString& Parameters)
{
	{
		FHFHouseSpec Spec = MakeValidSpec();
		Spec.Openings[0].WallId = TEXT("W_DoesNotExist");
		ExpectIssue(*this, Spec, TEXT("UnknownWallReference"), EHFValidationSeverity::Error);
	}
	{
		// Wall is 400 long; a 90-wide door centred at 380 runs from 335 to 425 and would boolean
		// away the wall's corner.
		FHFHouseSpec Spec = MakeValidSpec();
		Spec.Openings[0].OffsetAlongWall = 380.0;
		ExpectIssue(*this, Spec, TEXT("OpeningExceedsWall"), EHFValidationSeverity::Error);
	}
	{
		// Running off the near end is just as broken as running off the far end.
		FHFHouseSpec Spec = MakeValidSpec();
		Spec.Openings[0].OffsetAlongWall = 10.0;
		ExpectIssue(*this, Spec, TEXT("OpeningExceedsWall"), EHFValidationSeverity::Error);
	}
	{
		FHFHouseSpec Spec = MakeValidSpec();
		Spec.Openings[0].SillHeight = 200.0;
		Spec.Openings[0].Height = 210.0;	// head at 410, wall is 300
		ExpectIssue(*this, Spec, TEXT("OpeningExceedsWallHeight"), EHFValidationSeverity::Error);
	}
	{
		FHFHouseSpec Spec = MakeValidSpec();
		Spec.Openings[0].Width = 0.0;
		ExpectIssue(*this, Spec, TEXT("NonPositiveOpeningSize"), EHFValidationSeverity::Error);
	}
	{
		// Buildable, but almost certainly a window mislabelled as a door.
		FHFHouseSpec Spec = MakeValidSpec();
		Spec.Openings[0].SillHeight = 90.0;
		ExpectIssue(*this, Spec, TEXT("DoorWithSill"), EHFValidationSeverity::Warning);
	}

	return true;
}

/**
 * Two openings overlapping on one wall boolean into a single ragged hole. Easy to author by
 * accident - it happened in the reference 2BHK, where a bathroom ventilator was placed on the
 * same wall as the master bedroom door and ran straight through it.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFValidatorOpeningOverlapTest, "HouseForge.Model.Validator.OpeningsOverlap", HF_TEST_FLAGS)

bool FHFValidatorOpeningOverlapTest::RunTest(const FString& Parameters)
{
	{
		// Door spans 155..245; a second opening at 200 sits right on top of it.
		FHFHouseSpec Spec = MakeValidSpec();
		FHFOpening Extra = Spec.Openings[0];
		Extra.Id = TEXT("W1");
		Extra.Kind = EHFOpeningKind::Window;
		Extra.OffsetAlongWall = 200.0;
		Extra.Width = 60.0;
		Extra.SillHeight = 90.0;
		Extra.Height = 100.0;			// 90..190, inside the door's 0..210
		Spec.Openings.Add(Extra);
		ExpectIssue(*this, Spec, TEXT("OpeningsOverlap"), EHFValidationSeverity::Error);
	}
	{
		// A ventilator stacked directly on a door head is how bathrooms without an external wall
		// are actually ventilated, so it must not be flagged.
		FHFHouseSpec Spec = MakeValidSpec();
		FHFOpening Vent;
		Vent.Id = TEXT("V1");
		Vent.WallId = TEXT("W_South");
		Vent.OffsetAlongWall = 200.0;
		Vent.Width = 60.0;
		Vent.SillHeight = 210.0;		// exactly the door head
		Vent.Height = 45.0;
		Vent.Kind = EHFOpeningKind::Ventilator;
		Spec.Openings.Add(Vent);

		const FHFValidationResult Result = FHFSpecValidator::Validate(Spec);
		TestFalse(TEXT("A ventilator stacked on a door head is not an overlap"),
			Result.Contains(TEXT("OpeningsOverlap")));
	}
	{
		// Openings side by side on the same wall are fine.
		FHFHouseSpec Spec = MakeValidSpec();
		Spec.Openings[0].OffsetAlongWall = 100.0;	// 55..145
		FHFOpening Second = Spec.Openings[0];
		Second.Id = TEXT("D2");
		Second.OffsetAlongWall = 300.0;				// 255..345
		Spec.Openings.Add(Second);

		const FHFValidationResult Result = FHFSpecValidator::Validate(Spec);
		TestFalse(TEXT("Separated openings on one wall are not an overlap"),
			Result.Contains(TEXT("OpeningsOverlap")));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFValidatorRoomRulesTest, "HouseForge.Model.Validator.RoomRules", HF_TEST_FLAGS)

bool FHFValidatorRoomRulesTest::RunTest(const FString& Parameters)
{
	{
		FHFHouseSpec Spec = MakeValidSpec();
		Spec.Rooms[0].Boundary = { FVector2D(0, 0), FVector2D(400, 0) };
		ExpectIssue(*this, Spec, TEXT("UnclosedRoom"), EHFValidationSeverity::Error);
	}
	{
		// The most common hand-authoring mistake: repeating the first point to "close" the loop,
		// which leaves a zero-length edge that breaks polygon offset for the false ceiling.
		FHFHouseSpec Spec = MakeValidSpec();
		// Copy first: TArray::Add asserts if handed a reference into the array it is growing.
		const FVector2D FirstPoint = Spec.Rooms[0].Boundary[0];
		Spec.Rooms[0].Boundary.Add(FirstPoint);
		ExpectIssue(*this, Spec, TEXT("RepeatedClosingPoint"), EHFValidationSeverity::Error);
	}
	{
		FHFHouseSpec Spec = MakeValidSpec();
		Spec.Rooms[0].Boundary = { FVector2D(0, 0), FVector2D(200, 0), FVector2D(400, 0) };
		ExpectIssue(*this, Spec, TEXT("DegenerateRoom"), EHFValidationSeverity::Error);
	}
	{
		FHFHouseSpec Spec = MakeValidSpec();
		Spec.Rooms[0].CeilingHeight = 0.0;
		ExpectIssue(*this, Spec, TEXT("NonPositiveCeilingHeight"), EHFValidationSeverity::Error);
	}
	{
		// A bow-tie: the same four corners listed in the wrong order, which is an ordinary mis-read
		// of a plan rather than abuse. It has enough points, no repeated closing point and a
		// perfectly good area, so every other rule here passes it - and then the triangulator
		// declines it and the room comes back with no floor at all while its skirting, emitted per
		// edge, generates perfectly. From above that reads as an unfinished floor, not a failure.
		FHFHouseSpec Spec = MakeValidSpec();
		Spec.Rooms[0].Boundary = {
			FVector2D(0, 0), FVector2D(400, 0), FVector2D(0, 300), FVector2D(400, 300) };
		ExpectIssue(*this, Spec, TEXT("SelfIntersectingRoom"), EHFValidationSeverity::Error);
	}
	{
		// A figure-eight, to prove the sweep is not just spotting the one crossing shape.
		FHFHouseSpec Spec = MakeValidSpec();
		Spec.Rooms[0].Boundary = {
			FVector2D(0, 0), FVector2D(200, 200), FVector2D(400, 0),
			FVector2D(400, 200), FVector2D(200, 0), FVector2D(0, 200) };
		ExpectIssue(*this, Spec, TEXT("SelfIntersectingRoom"), EHFValidationSeverity::Error);
	}
	{
		// And an L-shaped room, which is what half these layouts are, must NOT be flagged: a
		// concave polygon is still a simple one, and a check that could not tell the difference
		// would reject most of the flats this plugin exists for.
		FHFHouseSpec Spec = MakeValidSpec();
		Spec.Rooms[0].Boundary = {
			FVector2D(0, 0), FVector2D(400, 0), FVector2D(400, 150),
			FVector2D(200, 150), FVector2D(200, 300), FVector2D(0, 300) };

		const FHFValidationResult Result = FHFSpecValidator::Validate(Spec);
		TestFalse(TEXT("A concave L-shaped room is a simple polygon and is accepted"),
			Result.Contains(TEXT("SelfIntersectingRoom")));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFValidatorCeilingRulesTest, "HouseForge.Model.Validator.CeilingRules", HF_TEST_FLAGS)

bool FHFValidatorCeilingRulesTest::RunTest(const FString& Parameters)
{
	auto WithCeiling = [](EHFCeilingStyle Style, double Drop, double BandWidth)
	{
		FHFHouseSpec Spec = MakeValidSpec();
		FHFFalseCeiling Ceiling;
		Ceiling.Id = TEXT("FC1");
		Ceiling.RoomId = TEXT("R_Bedroom");
		Ceiling.Style = Style;
		Ceiling.Drop = Drop;
		Ceiling.BandWidth = BandWidth;
		Spec.FalseCeilings.Add(Ceiling);
		return Spec;
	};

	{
		FHFHouseSpec Spec = WithCeiling(EHFCeilingStyle::Peripheral, 20.0, 60.0);
		Spec.FalseCeilings[0].RoomId = TEXT("R_DoesNotExist");
		ExpectIssue(*this, Spec, TEXT("UnknownRoomReference"), EHFValidationSeverity::Error);
	}
	{
		// Room is 300 tall; a 320 drop would put the ceiling below the floor.
		ExpectIssue(*this, WithCeiling(EHFCeilingStyle::FullDrop, 320.0, 60.0),
			TEXT("CeilingDropExceedsRoom"), EHFValidationSeverity::Error);
	}
	{
		ExpectIssue(*this, WithCeiling(EHFCeilingStyle::Peripheral, 0.0, 60.0),
			TEXT("NonPositiveCeilingDrop"), EHFValidationSeverity::Error);
	}
	{
		// A perimeter style with no band generates nothing at all - silently an empty ceiling.
		ExpectIssue(*this, WithCeiling(EHFCeilingStyle::Peripheral, 20.0, 0.0),
			TEXT("MissingCeilingBand"), EHFValidationSeverity::Error);
	}
	{
		ExpectIssue(*this, WithCeiling(EHFCeilingStyle::Bulkhead, 20.0, 60.0),
			TEXT("BulkheadNeedsPolygon"), EHFValidationSeverity::Error);
	}
	{
		// 300 tall room, 100 drop leaves 200 clear - under the 210 minimum headroom.
		ExpectIssue(*this, WithCeiling(EHFCeilingStyle::FullDrop, 100.0, 60.0),
			TEXT("LowHeadroom"), EHFValidationSeverity::Warning);
	}
	{
		// A full drop to 250 clears the 210 door head, so no headroom complaint; but drop it to
		// just below the door and the ceiling would cut through the door head.
		FHFHouseSpec Spec = WithCeiling(EHFCeilingStyle::FullDrop, 95.0, 60.0);
		ExpectIssue(*this, Spec, TEXT("CeilingBelowDoorHead"), EHFValidationSeverity::Warning);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFValidatorFixtureRulesTest, "HouseForge.Model.Validator.FixtureRules", HF_TEST_FLAGS)

bool FHFValidatorFixtureRulesTest::RunTest(const FString& Parameters)
{
	{
		FHFHouseSpec Spec = MakeValidSpec();
		Spec.Fixtures.Add(MakeFixture(TEXT("F1"), EHFFixtureType::Bed, FVector2D(200, 150)));
		Spec.Fixtures[0].RoomId = TEXT("R_DoesNotExist");
		ExpectIssue(*this, Spec, TEXT("UnknownRoomReference"), EHFValidationSeverity::Error);
	}
	{
		// Room spans 0..400 by 0..300; a fixture at (900, 900) is nowhere near it.
		FHFHouseSpec Spec = MakeValidSpec();
		Spec.Fixtures.Add(MakeFixture(TEXT("F1"), EHFFixtureType::Bed, FVector2D(900, 900)));
		ExpectIssue(*this, Spec, TEXT("FixtureOutsideRoom"), EHFValidationSeverity::Error);
	}
	{
		FHFHouseSpec Spec = MakeValidSpec();
		Spec.Fixtures.Add(MakeFixture(TEXT("F1"), EHFFixtureType::Bed, FVector2D(200, 150)));
		Spec.Fixtures[0].Footprint = FVector2D(0.0, 60.0);
		ExpectIssue(*this, Spec, TEXT("NonPositiveFootprint"), EHFValidationSeverity::Error);
	}
	{
		FHFHouseSpec Spec = MakeValidSpec();
		Spec.Fixtures.Add(MakeFixture(TEXT("F1"), EHFFixtureType::Bed, FVector2D(200, 150)));
		Spec.Fixtures[0].Height = 0.0;
		ExpectIssue(*this, Spec, TEXT("NonPositiveFixtureHeight"), EHFValidationSeverity::Error);
	}
	{
		FHFHouseSpec Spec = MakeValidSpec();
		Spec.Fixtures.Add(MakeFixture(TEXT("F1"), EHFFixtureType::Bed, FVector2D(200, 150)));
		Spec.Fixtures[0].AnchorWallId = TEXT("W_DoesNotExist");
		ExpectIssue(*this, Spec, TEXT("UnknownWallReference"), EHFValidationSeverity::Error);
	}
	{
		FHFHouseSpec Spec = MakeValidSpec();
		Spec.Fixtures.Add(MakeFixture(TEXT("F1"), EHFFixtureType::Unknown, FVector2D(200, 150)));
		ExpectIssue(*this, Spec, TEXT("UnknownFixtureType"), EHFValidationSeverity::Warning);
	}
	{
		// Two beds in the same place, at the same height.
		FHFHouseSpec Spec = MakeValidSpec();
		Spec.Fixtures.Add(MakeFixture(TEXT("F1"), EHFFixtureType::Bed, FVector2D(200, 150)));
		Spec.Fixtures.Add(MakeFixture(TEXT("F2"), EHFFixtureType::Bed, FVector2D(210, 150)));
		ExpectIssue(*this, Spec, TEXT("OverlappingFixtures"), EHFValidationSeverity::Warning);
	}

	return true;
}

/**
 * A fixture can be centred well inside its room and still drive its geometry through a wall.
 * Checking only the centre point missed exactly this in the reference 2BHK, where a 900-deep
 * shower centred 300 from the partition overhung it by 150.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFValidatorFootprintTest, "HouseForge.Model.Validator.FixtureFootprintCrossesWall", HF_TEST_FLAGS)

bool FHFValidatorFootprintTest::RunTest(const FString& Parameters)
{
	{
		// Room spans 0..400 by 0..300. A 200-deep fixture centred at y=250 reaches y=350.
		FHFHouseSpec Spec = MakeValidSpec();
		FHFFixture& Shower = Spec.Fixtures.Add_GetRef(MakeFixture(TEXT("F1"), EHFFixtureType::Shower, FVector2D(200, 250)));
		Shower.Footprint = FVector2D(200.0, 200.0);
		ExpectIssue(*this, Spec, TEXT("FixtureFootprintCrossesWall"), EHFValidationSeverity::Warning);
	}
	{
		// Rotation must be accounted for: the same footprint turned 90 degrees fits where the
		// unrotated one would not.
		FHFHouseSpec Spec = MakeValidSpec();
		FHFFixture& Unit = Spec.Fixtures.Add_GetRef(MakeFixture(TEXT("F1"), EHFFixtureType::Wardrobe, FVector2D(200, 150)));
		Unit.Footprint = FVector2D(280.0, 100.0);
		Unit.RotationDegrees = 90.0;	// now 100 wide by 280 deep, reaching y=10..290
		const FHFValidationResult Result = FHFSpecValidator::Validate(Spec);
		TestFalse(TEXT("A rotated footprint that fits is not flagged"), Result.Contains(TEXT("FixtureFootprintCrossesWall")));
	}
	{
		// Wall-anchored fixtures are exempt: room boundaries run along wall centrelines, so a
		// wardrobe backing onto its wall is supposed to cross the boundary.
		FHFHouseSpec Spec = MakeValidSpec();
		FHFFixture& Wardrobe = Spec.Fixtures.Add_GetRef(MakeFixture(TEXT("F1"), EHFFixtureType::Wardrobe, FVector2D(200, 280)));
		Wardrobe.Footprint = FVector2D(180.0, 60.0);
		Wardrobe.AnchorWallId = TEXT("W_North");
		const FHFValidationResult Result = FHFSpecValidator::Validate(Spec);
		TestFalse(TEXT("Wall-anchored fixtures are exempt"), Result.Contains(TEXT("FixtureFootprintCrossesWall")));
	}

	return true;
}

/**
 * Vertically stacked fixtures - a wall cabinet above a counter - overlap in plan but not in
 * space. Flagging them would train whoever reads the report to ignore the rule.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFValidatorStackedFixturesTest, "HouseForge.Model.Validator.StackedFixturesDoNotOverlap", HF_TEST_FLAGS)

bool FHFValidatorStackedFixturesTest::RunTest(const FString& Parameters)
{
	FHFHouseSpec Spec = MakeValidSpec();

	FHFFixture Base = MakeFixture(TEXT("F_Base"), EHFFixtureType::KitchenBaseCabinet, FVector2D(200, 150));
	Base.BaseZ = 0.0;
	Base.Height = 85.0;
	Spec.Fixtures.Add(Base);

	FHFFixture Wall = MakeFixture(TEXT("F_Wall"), EHFFixtureType::KitchenWallCabinet, FVector2D(200, 150));
	Wall.BaseZ = 140.0;	// clears the base cabinet's 85 top
	Wall.Height = 70.0;
	Spec.Fixtures.Add(Wall);

	const FHFValidationResult Result = FHFSpecValidator::Validate(Spec);

	TestFalse(TEXT("Stacked cabinets are not reported as overlapping"), Result.Contains(TEXT("OverlappingFixtures")));
	TestFalse(TEXT("Stacked cabinets produce no errors"), Result.HasErrors());

	return true;
}

/**
 * A sink cut into a worktop occupies exactly the same plan area at the same height. Flagging that
 * would make the overlap rule noise, and noise gets ignored - so inset fittings are exempt.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFValidatorInsetFittingsTest, "HouseForge.Model.Validator.InsetFittingsDoNotOverlap", HF_TEST_FLAGS)

bool FHFValidatorInsetFittingsTest::RunTest(const FString& Parameters)
{
	FHFHouseSpec Spec = MakeValidSpec();

	FHFFixture& Counter = Spec.Fixtures.Add_GetRef(
		MakeFixture(TEXT("F_Counter"), EHFFixtureType::CounterTop, FVector2D(200, 150)));
	Counter.Footprint = FVector2D(200.0, 60.0);
	Counter.BaseZ = 85.0;
	Counter.Height = 4.0;

	FHFFixture& Sink = Spec.Fixtures.Add_GetRef(
		MakeFixture(TEXT("F_Sink"), EHFFixtureType::Sink, FVector2D(200, 150)));
	Sink.Footprint = FVector2D(80.0, 45.0);
	Sink.BaseZ = 69.0;
	Sink.Height = 20.0;

	const FHFValidationResult Result = FHFSpecValidator::Validate(Spec);
	TestFalse(TEXT("A sink set into a counter is not reported as overlapping"),
		Result.Contains(TEXT("OverlappingFixtures")));

	// The exemption must be narrow: two beds in the same place are still a real problem.
	FHFHouseSpec Beds = MakeValidSpec();
	Beds.Fixtures.Add(MakeFixture(TEXT("F1"), EHFFixtureType::Bed, FVector2D(200, 150)));
	Beds.Fixtures.Add(MakeFixture(TEXT("F2"), EHFFixtureType::Bed, FVector2D(205, 150)));
	TestTrue(TEXT("The exemption does not suppress genuine overlaps"),
		FHFSpecValidator::Validate(Beds).Contains(TEXT("OverlappingFixtures")));

	return true;
}

/**
 * A misread drawing usually has several problems at once. Reporting them one per round-trip
 * would make correction needlessly slow, so the validator must run every rule in one pass.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFValidatorReportsAllIssuesTest, "HouseForge.Model.Validator.ReportsAllIssuesInOnePass", HF_TEST_FLAGS)

bool FHFValidatorReportsAllIssuesTest::RunTest(const FString& Parameters)
{
	FHFHouseSpec Spec = MakeValidSpec();
	Spec.Walls[0].Thickness = 0.0;					// NonPositiveWallThickness
	Spec.Walls[1].End = Spec.Walls[1].Start;		// ZeroLengthWall
	Spec.Openings[0].OffsetAlongWall = 395.0;		// OpeningExceedsWall
	Spec.Rooms[0].CeilingHeight = -5.0;				// NonPositiveCeilingHeight

	const FHFValidationResult Result = FHFSpecValidator::Validate(Spec);

	TestTrue(TEXT("Wall thickness problem reported"), Result.Contains(TEXT("NonPositiveWallThickness")));
	TestTrue(TEXT("Zero-length wall reported"), Result.Contains(TEXT("ZeroLengthWall")));
	TestTrue(TEXT("Oversized opening reported"), Result.Contains(TEXT("OpeningExceedsWall")));
	TestTrue(TEXT("Ceiling height problem reported"), Result.Contains(TEXT("NonPositiveCeilingHeight")));
	TestTrue(TEXT("All four surfaced together"), Result.CountOf(EHFValidationSeverity::Error) >= 4);

	// The report is what gets fed back to Claude, so it has to name the codes.
	const FString Report = Result.ToString();
	TestTrue(TEXT("Report names the zero-length wall rule"), Report.Contains(TEXT("ZeroLengthWall")));
	TestTrue(TEXT("Report counts the errors"), Report.Contains(TEXT("error")));

	return true;
}

/**
 * A column standing inside a doorway is reported.
 *
 * Nothing else catches this: OpeningsOverlap compares openings with each other, and SwingBlocked
 * asks where the leaf ends up rather than whether the hole it swings out of is clear. It reached
 * the reference 2BHK - D_Bed2 overlapped COL_M1 by 75 mm - and survived to geometry as a door leaf
 * built inside a column, visible only as a warning buried in a sweep test's report.
 *
 * Two things about the rule let that happen, and both are asserted below.
 *
 * It reported a doorway at warning severity, so nothing failed. A door with a column in it cannot
 * be built and cannot be walked through; there is no second reading to weigh up, and it is an error
 * now. A window keeps the warning, because glazing can be made to fit round a pier.
 *
 * And it only looked inside the wall's own thickness. A column deeper than its partition is set out
 * flush with one face of it rather than centred on it - the ordinary way a 450 x 230 column stands
 * in a 115 wall - and its whole bulk is then outside the wall's slab, so the rule excluded exactly
 * the construction it existed to catch.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFValidatorColumnInOpeningTest,
	"HouseForge.Validation.ColumnStandingInAnOpening", HF_TEST_FLAGS)

bool FHFValidatorColumnInOpeningTest::RunTest(const FString& Parameters)
{
	// W_South runs from (0,0) to (400,0) and D1 is 90 wide at offset 200, so the clear opening
	// spans 155..245 along the wall.
	auto SpecWithColumnAt = [](const FVector2D& Position, double Height = 300.0)
	{
		FHFHouseSpec Spec = MakeValidSpec();

		FHFColumn Column;
		Column.Id = TEXT("COL_1");
		Column.Position = Position;
		Column.Size = FVector2D(45.0, 23.0);
		Column.Height = Height;
		Spec.Columns.Add(Column);

		return Spec;
	};

	// Biting 37.5 cm out of a 90 cm doorway. An error, not a warning: nothing about that is a
	// judgement call, and a warning is what let the reference flat keep one for thirty-four commits.
	ExpectIssue(*this, SpecWithColumnAt(FVector2D(170.0, 0.0)),
		TEXT("OpeningBlockedByColumn"), EHFValidationSeverity::Error);

	// The same column against a window is a warning. A pier across a glazing line is bad practice
	// and buildable; a pier in a doorway is neither.
	{
		FHFHouseSpec Spec = SpecWithColumnAt(FVector2D(170.0, 0.0));
		Spec.Openings[0].Kind = EHFOpeningKind::SlidingWindow;
		Spec.Openings[0].Swing = EHFSwing::None;
		Spec.Openings[0].SillHeight = 90.0;
		Spec.Openings[0].Height = 120.0;

		ExpectIssue(*this, Spec, TEXT("OpeningBlockedByColumn"), EHFValidationSeverity::Warning);
	}

	// A column packed out flush against the wall's face, standing squarely in the doorway.
	//
	// This is the case the rule used to miss entirely, and it is not an exotic one - it is how a
	// column bigger than the partition it stands in is built. W_South is 11.5 thick, so its north
	// face is at Y 5.75; a 23-deep column centred at 17.25 has its front face exactly on that line
	// and every millimetre of it in the room. The old band test - anything at or beyond half the
	// wall's thickness is not this wall's problem - skipped it without a word, while the door it
	// blocks is a 90 opening with 37.5 of concrete across it.
	ExpectIssue(*this, SpecWithColumnAt(FVector2D(170.0, 17.25)),
		TEXT("OpeningBlockedByColumn"), EHFValidationSeverity::Error);

	// The message has to be actionable: which opening, which column, and how much.
	{
		const FHFValidationResult Result = FHFSpecValidator::Validate(SpecWithColumnAt(FVector2D(170.0, 0.0)));
		const FHFValidationIssue* Issue = Result.Issues.FindByPredicate(
			[](const FHFValidationIssue& I) { return I.Code == TEXT("OpeningBlockedByColumn"); });

		if (TestNotNull(TEXT("The clash is reported"), Issue))
		{
			TestTrue(TEXT("The message names the column"), Issue->Message.Contains(TEXT("COL_1")));
			TestTrue(TEXT("The message quantifies the overlap"), Issue->Message.Contains(TEXT("37.5")));
		}
	}

	// A column whose face lands exactly on the jamb is normal construction, not a clash: its far
	// edge is at 155, which is where the opening starts.
	TestFalse(TEXT("A column abutting the jamb is not reported"),
		FHFSpecValidator::Validate(SpecWithColumnAt(FVector2D(132.5, 0.0)))
			.Contains(TEXT("OpeningBlockedByColumn")));

	// In the same wall but nowhere near the opening.
	TestFalse(TEXT("A column elsewhere in the wall is not reported"),
		FHFSpecValidator::Validate(SpecWithColumnAt(FVector2D(50.0, 0.0)))
			.Contains(TEXT("OpeningBlockedByColumn")));

	// Lined up with the opening in plan but standing free in the room, a metre off the wall. That is
	// a pier somebody has to walk round, not a column in a doorway, and it belongs to whoever laid
	// the room out. The widened band has to stop somewhere, and this is where.
	TestFalse(TEXT("A column standing free of the wall is not reported"),
		FHFSpecValidator::Validate(SpecWithColumnAt(FVector2D(170.0, 100.0)))
			.Contains(TEXT("OpeningBlockedByColumn")));

	// Overlapping in plan but stopping below the sill: a window over a low plinth is not blocked.
	{
		FHFHouseSpec Spec = SpecWithColumnAt(FVector2D(170.0, 0.0), /*Height*/ 40.0);
		Spec.Openings[0].Kind = EHFOpeningKind::Window;
		Spec.Openings[0].Swing = EHFSwing::None;
		Spec.Openings[0].SillHeight = 90.0;
		Spec.Openings[0].Height = 135.0;

		TestFalse(TEXT("A column that stops below the sill is not reported"),
			FHFSpecValidator::Validate(Spec).Contains(TEXT("OpeningBlockedByColumn")));
	}

	return true;
}

/**
 * A fixture standing in front of an opening.
 *
 * The same misread as a column in a doorway, one layer out, and until now nothing looked for it: a
 * wardrobe and a window are drawn on different layers and read as separate things. It went unnoticed
 * because a fixed pane behind a wardrobe merely looks odd. A sliding sash behind one cannot be
 * opened - its catch is inside the cupboard - so the reference flat's own east bedroom window had to
 * move before its sashes meant anything.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFValidatorFixtureInOpeningTest, "HouseForge.Model.Validator.FixtureInOpening", HF_TEST_FLAGS)

bool FHFValidatorFixtureInOpeningTest::RunTest(const FString& Parameters)
{
	// W_South runs from (0,0) to (400,0). Turn D1 into a window so the height test has something to
	// say: 120 wide at offset 200 on a 90 sill, so it spans 140..260 along the wall and 90..225 up.
	auto SpecWithWardrobeAt = [](const FVector2D& Position, double BaseZ = 0.0, double Height = 240.0)
	{
		FHFHouseSpec Spec = MakeValidSpec();

		Spec.Openings[0].Kind = EHFOpeningKind::SlidingWindow;
		Spec.Openings[0].Swing = EHFSwing::None;
		Spec.Openings[0].Width = 120.0;
		Spec.Openings[0].SillHeight = 90.0;
		Spec.Openings[0].Height = 135.0;

		FHFFixture Wardrobe = MakeFixture(TEXT("F_Wardrobe"), EHFFixtureType::Wardrobe, Position);
		Wardrobe.Footprint = FVector2D(180.0, 60.0);
		Wardrobe.BaseZ = BaseZ;
		Wardrobe.Height = Height;
		Spec.Fixtures.Add(Wardrobe);

		return Spec;
	};

	// Standing against the wall with its back on the inner face at Y=5.75, centred on the window.
	ExpectIssue(*this, SpecWithWardrobeAt(FVector2D(200.0, 35.75)),
		TEXT("OpeningBlockedByFixture"), EHFValidationSeverity::Warning);

	// The message has to be actionable: which fixture, which opening, and how much of it.
	{
		const FHFValidationResult Result = FHFSpecValidator::Validate(SpecWithWardrobeAt(FVector2D(200.0, 35.75)));
		const FHFValidationIssue* Issue = Result.Issues.FindByPredicate(
			[](const FHFValidationIssue& I) { return I.Code == TEXT("OpeningBlockedByFixture"); });

		if (TestNotNull(TEXT("The obstruction is reported"), Issue))
		{
			TestTrue(TEXT("The message names the fixture"), Issue->Message.Contains(TEXT("F_Wardrobe")));
			TestTrue(TEXT("The message names the opening"), Issue->Message.Contains(TEXT("D1")));
			// The whole 120 of width and the whole 135 of height: a wardrobe taller than the head.
			TestTrue(TEXT("The message quantifies the obstruction"), Issue->Message.Contains(TEXT("120.0")));
		}
	}

	// Along the same wall but well clear of the opening.
	TestFalse(TEXT("A wardrobe elsewhere along the wall is not reported"),
		FHFSpecValidator::Validate(SpecWithWardrobeAt(FVector2D(20.0, 35.75)))
			.Contains(TEXT("OpeningBlockedByFixture")));

	// In front of the window in plan, but out in the middle of the room and anchored to nothing.
	// A bed two metres from a window is in the photograph and in nobody's way.
	TestFalse(TEXT("A fixture standing off the wall is not reported"),
		FHFSpecValidator::Validate(SpecWithWardrobeAt(FVector2D(200.0, 200.0)))
			.Contains(TEXT("OpeningBlockedByFixture")));

	// Below the sill: base units under a window are the whole point of a kitchen.
	TestFalse(TEXT("A run that stops below the sill is not reported"),
		FHFSpecValidator::Validate(SpecWithWardrobeAt(FVector2D(200.0, 35.75), /*BaseZ*/ 0.0, /*Height*/ 85.0))
			.Contains(TEXT("OpeningBlockedByFixture")));

	// Above the head: a pelmet sits on a window head by design, and so does a boxed-in AC unit.
	TestFalse(TEXT("A pelmet above the head is not reported"),
		FHFSpecValidator::Validate(SpecWithWardrobeAt(FVector2D(200.0, 35.75), /*BaseZ*/ 235.0, /*Height*/ 20.0))
			.Contains(TEXT("OpeningBlockedByFixture")));

	// A position read off a plan is not exact. A fixture that says which wall it stands against is
	// against it, even when its footprint is drawn a few centimetres proud of the face - which is
	// how the reference flat's TV unit came to sit in front of a balcony door unnoticed.
	{
		FHFHouseSpec Spec = SpecWithWardrobeAt(FVector2D(200.0, 45.0));
		TestFalse(TEXT("Drawn proud of the wall and anchored to nothing, it is not reported"),
			FHFSpecValidator::Validate(Spec).Contains(TEXT("OpeningBlockedByFixture")));

		Spec.Fixtures.Last().AnchorWallId = TEXT("W_South");
		TestTrue(TEXT("The same fixture anchored to that wall is reported"),
			FHFSpecValidator::Validate(Spec).Contains(TEXT("OpeningBlockedByFixture")));
	}

	// The threshold is a project convention, not a law: a run drawn hard against a jamb shares an
	// edge with it and is normal construction. Its far edge lands at 140, where the window starts.
	TestFalse(TEXT("A run abutting the jamb is not reported"),
		FHFSpecValidator::Validate(SpecWithWardrobeAt(FVector2D(50.0, 35.75)))
			.Contains(TEXT("OpeningBlockedByFixture")));

	return true;
}

/**
 * A beam has to stand on something.
 *
 * Nothing asked before. The beam rules checked length, size and depth - every one of them a
 * property of the beam by itself - and the ceiling rules checked what a beam does to a soffit.
 * Between them, nothing ever looked underneath one.
 *
 * The reference flat carried the consequence for the whole of milestone 8: BM_Living_Cross ran the
 * full width of the living room across its exact centre, on no wall line, between no columns, under
 * a Cove ceiling that leaves the middle of a room at slab height. It validated clean every run and a
 * user found it in a render.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFValidatorBeamSupportTest,
	"HouseForge.Validation.BeamStandsOnSomething", HF_TEST_FLAGS)

bool FHFValidatorBeamSupportTest::RunTest(const FString& Parameters)
{
	// MakeValidSpec is in centimetres: a 400 x 300 room with walls on its four boundary lines.
	// A beam sized for that, rather than FHFBeam's millimetre defaults.
	auto MakeBeam = [](const FName& Id, const FVector2D& Start, const FVector2D& End)
	{
		FHFBeam Beam;
		Beam.Id = Id;
		Beam.Start = Start;
		Beam.End = End;
		Beam.Width = 23.0;
		Beam.Depth = 45.0;
		Beam.SoffitZ = 300.0;
		return Beam;
	};

	auto MakeColumn = [](const FName& Id, const FVector2D& Position)
	{
		FHFColumn Column;
		Column.Id = Id;
		Column.Position = Position;
		Column.Size = FVector2D(45.0, 23.0);
		Column.Height = 300.0;
		return Column;
	};

	// A beam on a wall line, which is where eight of the reference flat's nine were and where all
	// eight of them still are.
	{
		FHFHouseSpec Spec = MakeValidSpec();
		Spec.Beams.Add(MakeBeam(TEXT("BM_South"), FVector2D(0.0, 0.0), FVector2D(400.0, 0.0)));

		TestFalse(TEXT("A beam following a wall line is supported"),
			FHFSpecValidator::Validate(Spec).Contains(TEXT("BeamNotSupported")));
	}

	// A clear span between two columns, with nothing under the middle of it. That is what a beam is
	// for, so the gap must not be reported.
	{
		FHFHouseSpec Spec = MakeValidSpec();
		Spec.Beams.Add(MakeBeam(TEXT("BM_Cross"), FVector2D(50.0, 150.0), FVector2D(350.0, 150.0)));
		Spec.Columns.Add(MakeColumn(TEXT("COL_A"), FVector2D(50.0, 150.0)));
		Spec.Columns.Add(MakeColumn(TEXT("COL_B"), FVector2D(350.0, 150.0)));

		TestFalse(TEXT("A clear span between two columns is supported"),
			FHFSpecValidator::Validate(Spec).Contains(TEXT("BeamNotSupported")));
	}

	// Nothing at either end: an error, because no reading of the spec makes it stand up.
	{
		FHFHouseSpec Spec = MakeValidSpec();
		Spec.Beams.Add(MakeBeam(TEXT("BM_Floating"), FVector2D(50.0, 150.0), FVector2D(350.0, 150.0)));
		ExpectIssue(*this, Spec, TEXT("BeamNotSupported"), EHFValidationSeverity::Error);

		const FHFValidationResult Result = FHFSpecValidator::Validate(Spec);
		const FHFValidationIssue* Issue = Result.Issues.FindByPredicate(
			[](const FHFValidationIssue& I) { return I.Code == TEXT("BeamNotSupported"); });

		if (TestNotNull(TEXT("The floating beam is reported"), Issue))
		{
			TestTrue(TEXT("The message names the beam"), Issue->Message.Contains(TEXT("BM_Floating")));
			// The whole 300 of it is over open floor, and saying so is what makes the report actionable.
			TestTrue(TEXT("The message quantifies the unsupported run"), Issue->Message.Contains(TEXT("300.0")));
		}
	}

	// One end borne and one loose is a cantilever, which is a real thing this model cannot tell
	// apart from a mistake. It warns rather than failing, and names the end that is loose.
	{
		FHFHouseSpec Spec = MakeValidSpec();
		Spec.Beams.Add(MakeBeam(TEXT("BM_Cantilever"), FVector2D(50.0, 150.0), FVector2D(350.0, 150.0)));
		Spec.Columns.Add(MakeColumn(TEXT("COL_A"), FVector2D(50.0, 150.0)));
		ExpectIssue(*this, Spec, TEXT("BeamNotSupported"), EHFValidationSeverity::Warning);
	}

	// A beam landing on the mid-span of two other beams is NOT accepted, and that is deliberate
	// rather than an oversight. A secondary framing into a primary is real construction, but the
	// primary has to be sized for the point load and nothing in a spec says whether it was. It is
	// also exactly what BM_Living_Cross did - both its ends landed on beams - so accepting it would
	// accept the defect this rule exists for.
	{
		FHFHouseSpec Spec = MakeValidSpec();
		Spec.Beams.Add(MakeBeam(TEXT("BM_West"),  FVector2D(0.0, 0.0), FVector2D(0.0, 300.0)));
		Spec.Beams.Add(MakeBeam(TEXT("BM_East"),  FVector2D(400.0, 0.0), FVector2D(400.0, 300.0)));
		Spec.Beams.Add(MakeBeam(TEXT("BM_Cross"), FVector2D(0.0, 150.0), FVector2D(400.0, 150.0)));

		const FHFValidationResult Result = FHFSpecValidator::Validate(Spec);
		const FHFValidationIssue* Issue = Result.Issues.FindByPredicate(
			[](const FHFValidationIssue& I) { return I.Code == TEXT("BeamNotSupported"); });

		if (TestNotNull(TEXT("A beam framing into two other beams is still reported"), Issue))
		{
			TestEqual(TEXT("And only the crossing beam is"), Issue->ElementId, FName(TEXT("BM_Cross")));
		}
	}

	// The lateral tolerance is the beam's own width and nothing more, so the eight beams in the
	// reference flat pass because they are exactly on their wall lines rather than because the rule
	// is generous. This wall is 20 clear of a beam 23 wide, so it is beside it, not under it.
	{
		FHFHouseSpec Spec = MakeValidSpec();
		Spec.Beams.Add(MakeBeam(TEXT("BM_Offset"), FVector2D(50.0, 20.0), FVector2D(350.0, 20.0)));
		ExpectIssue(*this, Spec, TEXT("BeamNotSupported"), EHFValidationSeverity::Error);
	}

	// And the real thing. Put BM_Living_Cross back into the reference flat exactly as it was - a
	// literal Y of 1800, 400 deep where every other beam is 450 - and the rule must catch it.
	{
		FHFHouseSpec Spec = FHFSampleHouse::Make2BHK();

		FHFBeam Cross;
		Cross.Id = TEXT("BM_Living_Cross");
		Cross.Start = FVector2D(0.0, 1800.0);
		Cross.End = FVector2D(6600.0, 1800.0);
		Cross.Width = 230.0;
		Cross.Depth = 400.0;
		Cross.SoffitZ = 3000.0;
		Spec.Beams.Add(Cross);

		ExpectIssue(*this, Spec, TEXT("BeamNotSupported"), EHFValidationSeverity::Error);
	}

	// The flat as it stands has eight beams and every one of them is honestly borne.
	TestFalse(TEXT("Every beam in the reference flat is supported"),
		FHFSpecValidator::Validate(FHFSampleHouse::Make2BHK()).Contains(TEXT("BeamNotSupported")));

	return true;
}

/**
 * A column standing in open floor.
 *
 * The same blindness as the beam rule, one element over: nothing asked what a column was doing
 * where it was. A column is the one thing in a plan a reader cannot argue with once it is built -
 * it is concrete standing in the room - and one that is on no wall junction and under no beam is an
 * obstruction with nothing to carry, usually a column read off the wrong layer of a drawing.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFValidatorFreeColumnTest,
	"HouseForge.Validation.ColumnStandsFree", HF_TEST_FLAGS)

bool FHFValidatorFreeColumnTest::RunTest(const FString& Parameters)
{
	auto SpecWithColumnAt = [](const FVector2D& Position, double RotationDegrees = 0.0)
	{
		FHFHouseSpec Spec = MakeValidSpec();

		FHFColumn Column;
		Column.Id = TEXT("COL_1");
		Column.Position = Position;
		Column.Size = FVector2D(45.0, 23.0);
		Column.RotationDegrees = RotationDegrees;
		Column.Height = 300.0;
		Spec.Columns.Add(Column);

		return Spec;
	};

	// On the junction of the south and west walls, which is where all eleven of the reference
	// flat's columns are.
	TestFalse(TEXT("A column on a wall junction is not reported"),
		FHFSpecValidator::Validate(SpecWithColumnAt(FVector2D(0.0, 0.0)))
			.Contains(TEXT("ColumnStandsFree")));

	// Out in the middle of the room, on nothing.
	ExpectIssue(*this, SpecWithColumnAt(FVector2D(200.0, 150.0)),
		TEXT("ColumnStandsFree"), EHFValidationSeverity::Warning);

	// Free of every wall but carrying a beam. This is ordinary in a large room and is precisely what
	// the beam rule demands of a beam that does not follow a wall line, so the two rules have to
	// agree: a column put there to hold a beam up passes.
	{
		FHFHouseSpec Spec = SpecWithColumnAt(FVector2D(200.0, 150.0));

		FHFBeam Beam;
		Beam.Id = TEXT("BM_1");
		Beam.Start = FVector2D(200.0, 0.0);
		Beam.End = FVector2D(200.0, 150.0);
		Beam.Width = 23.0;
		Beam.Depth = 45.0;
		Beam.SoffitZ = 300.0;
		Spec.Beams.Add(Beam);

		TestFalse(TEXT("A column under a beam is not reported"),
			FHFSpecValidator::Validate(Spec).Contains(TEXT("ColumnStandsFree")));
	}

	// A column that barely reaches its wall still counts. Centred at (200, 311) it spans Y
	// 299.5..322.5, so the north wall's centreline at Y 300 passes through the last half centimetre
	// of it - and it is on that wall. Sampling the wall's centreline against the column would be the
	// obvious way to test this and would be wrong at exactly this margin, which is why the check is
	// a real segment-rectangle clip.
	TestFalse(TEXT("A column that only just reaches its wall still counts"),
		FHFSpecValidator::Validate(SpecWithColumnAt(FVector2D(200.0, 311.0)))
			.Contains(TEXT("ColumnStandsFree")));

	// Pulled 40 back off the same wall it is on nothing again - its near edge is at Y 258.5, well
	// clear of the centreline at 300 - so the margin above is the rule being exact rather than the
	// rule being generous.
	TestTrue(TEXT("The same column pulled off the wall is reported"),
		FHFSpecValidator::Validate(SpecWithColumnAt(FVector2D(200.0, 270.0)))
			.Contains(TEXT("ColumnStandsFree")));

	// Every column in the reference flat is on a junction of two walls.
	TestFalse(TEXT("No column in the reference flat stands free"),
		FHFSpecValidator::Validate(FHFSampleHouse::Make2BHK()).Contains(TEXT("ColumnStandsFree")));

	return true;
}

/**
 * A bulkhead only excuses a beam it is actually OVER.
 *
 * CeilingDoesNotClearBeam has an exemption for a perimeter ceiling - a Cove or a Peripheral band
 * leaves the middle of a room at slab height, so it cannot conceal a beam crossing it, and the way
 * that is detailed in practice is a separate bulkhead boxing the beam in.
 *
 * The exemption was positional-blind. It asked whether a deep-enough bulkhead EXISTED IN THE ROOM,
 * which is a question whose answer conceals nothing: a bulkhead is by definition a localised drop
 * with its own polygon - the BulkheadNeedsPolygon rule insists on one - so a bulkhead over the TV
 * unit at one end of a living room excused a beam crossing the middle of it.
 *
 * That is exactly how BM_Living_Cross validated clean: Cove drop 200, Bulkhead drop 450, beam depth
 * 400, and the three numbers alone satisfied every clause.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFValidatorBulkheadOverBeamTest,
	"HouseForge.Validation.BulkheadMustCoverTheBeam", HF_TEST_FLAGS)

bool FHFValidatorBulkheadOverBeamTest::RunTest(const FString& Parameters)
{
	// The historical configuration, rebuilt: the reference flat with BM_Living_Cross back in it and
	// a bulkhead of some description in the living room.
	auto SpecWithBulkhead = [](double Drop, const TArray<FVector2D>& Polygon)
	{
		FHFHouseSpec Spec = FHFSampleHouse::Make2BHK();

		FHFBeam Cross;
		Cross.Id = TEXT("BM_Living_Cross");
		Cross.Start = FVector2D(0.0, 1800.0);
		Cross.End = FVector2D(6600.0, 1800.0);
		Cross.Width = 230.0;
		Cross.Depth = 400.0;
		Cross.SoffitZ = 3000.0;
		Spec.Beams.Add(Cross);

		FHFFalseCeiling Bulkhead;
		Bulkhead.Id = TEXT("FC_Living_Bulkhead");
		Bulkhead.RoomId = TEXT("R_Living");
		Bulkhead.Style = EHFCeilingStyle::Bulkhead;
		Bulkhead.Drop = Drop;
		Bulkhead.BandWidth = 0.0;
		Bulkhead.ExplicitPolygon = Polygon;
		Spec.FalseCeilings.Add(Bulkhead);

		return Spec;
	};

	// The complaint has to be the COVE's. The bulkhead is a false ceiling too and the same rule runs
	// over it, so a test that merely asked whether the code appeared anywhere would pass on a
	// shallow bulkhead complaining about itself while the exemption stayed broken.
	auto CoveIsReported = [](const FHFHouseSpec& Spec)
	{
		return FHFSpecValidator::Validate(Spec).Issues.ContainsByPredicate(
			[](const FHFValidationIssue& Issue)
			{
				return Issue.Code == TEXT("CeilingDoesNotClearBeam")
					&& Issue.ElementId == FName(TEXT("FC_Living"))
					&& Issue.Severity == EHFValidationSeverity::Warning;
			});
	};

	// Over the beam and deep enough: the exemption still works, which it has to, or the rule would
	// simply be refusing the detail it exists to allow.
	TestFalse(TEXT("A bulkhead over the beam excuses the cove"),
		CoveIsReported(SpecWithBulkhead(450.0, {
			FVector2D(0.0, 1700.0), FVector2D(6600.0, 1700.0),
			FVector2D(6600.0, 1900.0), FVector2D(0.0, 1900.0) })));

	// The same bulkhead, same depth, same room - at the wrong end of it. This is the case the old
	// exemption waved through, and it is not a contrived one: a bulkhead over the TV unit along the
	// south wall is the commonest bulkhead there is in a living room.
	TestTrue(TEXT("A bulkhead at the wrong end of the room excuses nothing"),
		CoveIsReported(SpecWithBulkhead(450.0, {
			FVector2D(0.0, 200.0), FVector2D(6600.0, 200.0),
			FVector2D(6600.0, 400.0), FVector2D(0.0, 400.0) })));

	// Covering only part of the beam's run across the room is not covering it: the rest of the beam
	// still hangs out of the soffit, which is what somebody standing in the room would see.
	TestTrue(TEXT("A bulkhead over half the beam excuses nothing"),
		CoveIsReported(SpecWithBulkhead(450.0, {
			FVector2D(0.0, 1700.0), FVector2D(3000.0, 1700.0),
			FVector2D(3000.0, 1900.0), FVector2D(0.0, 1900.0) })));

	// Over the beam but shallower than it. The depth clause was never the broken half, and it still
	// has to hold.
	TestTrue(TEXT("A bulkhead shallower than the beam excuses nothing"),
		CoveIsReported(SpecWithBulkhead(300.0, {
			FVector2D(0.0, 1700.0), FVector2D(6600.0, 1700.0),
			FVector2D(6600.0, 1900.0), FVector2D(0.0, 1900.0) })));

	// The flat as it stands has no beam over any room to conceal, so nothing here is being kept
	// quiet by an exemption at all.
	TestFalse(TEXT("No ceiling in the reference flat is hiding a beam behind an exemption"),
		FHFSpecValidator::Validate(FHFSampleHouse::Make2BHK())
			.Contains(TEXT("CeilingDoesNotClearBeam")));

	return true;
}

/**
 * Can you actually walk through the doorway.
 *
 * Every real obstruction in the reference flat was invisible to OpeningBlockedByFixture, and it was
 * invisible for a reason that rule states out loud: it only considers a fixture standing against the
 * opening's OWN wall. That is right for a window and exactly wrong for a door. What blocks a door is
 * not in the wall - it is the refrigerator 19 cm in front of it, anchored to a wall at right angles,
 * or the shower 33 cm in front of it, anchored to nothing at all. Two rooms in the flat could not be
 * entered and the suite reported it clean.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFValidatorDoorwayClearanceTest,
	"HouseForge.Validation.DoorwayIsWalkable", HF_TEST_FLAGS)

bool FHFValidatorDoorwayClearanceTest::RunTest(const FString& Parameters)
{
	// MakeValidSpec's door: 90 wide, centred at 200 along the south wall, so it spans 155..245.
	auto SpecWithBlocker = [](const FVector2D& Position, const FVector2D& Footprint,
		double Height = 180.0, const FName& Anchor = NAME_None)
	{
		FHFHouseSpec Spec = MakeValidSpec();

		FHFFixture Blocker = MakeFixture(TEXT("F_Blocker"), EHFFixtureType::Refrigerator, Position);
		Blocker.Footprint = Footprint;
		Blocker.Height = Height;
		Blocker.AnchorWallId = Anchor;
		Spec.Fixtures.Add(Blocker);

		return Spec;
	};

	// The reference flat's defect in miniature: a tall box standing clear of the wall, square in
	// front of the doorway, anchored to nothing. The old rule could not see this at all - the
	// fixture never touches the door's wall and never names it.
	{
		FHFHouseSpec Spec = SpecWithBlocker(FVector2D(200.0, 45.0), FVector2D(70.0, 70.0));
		ExpectIssue(*this, Spec, TEXT("DoorwayNotClear"), EHFValidationSeverity::Error);

		// And the rule it slipped past still does not see it, which is the point of splitting them.
		TestFalse(TEXT("The window rule does not claim this one"),
			FHFSpecValidator::Validate(Spec).Contains(TEXT("OpeningBlockedByFixture")));
	}

	// Anchored to a PERPENDICULAR wall, which is how the refrigerator was declared. The anchor gate
	// admits a fixture that names this wall; naming another one must not buy it a pass.
	ExpectIssue(*this, SpecWithBlocker(FVector2D(200.0, 45.0), FVector2D(70.0, 70.0), 180.0, TEXT("W_East")),
		TEXT("DoorwayNotClear"), EHFValidationSeverity::Error);

	// The same fixture, twice, to show that what counts is WHERE the gap falls rather than how much
	// is covered. A 30-wide box across the middle of a 90 door leaves two 30 slots and is a wall; the
	// identical box at the end of the same door leaves 75 unbroken and is furniture.
	//
	// This is why the rule measures the widest remaining run and not the overlap. One that fired on
	// any overlap at all would report the reference flat's shoe rack, 44 cm back and clipping the
	// last 22 cm of a doorway with 67 clear beside it, and reports nobody can act on stop being read.
	ExpectIssue(*this, SpecWithBlocker(FVector2D(200.0, 45.0), FVector2D(30.0, 70.0)),
		TEXT("DoorwayNotClear"), EHFValidationSeverity::Error);

	TestFalse(TEXT("The same box at the end of the doorway leaves a walkable width"),
		FHFSpecValidator::Validate(SpecWithBlocker(FVector2D(245.0, 45.0), FVector2D(30.0, 70.0)))
			.Contains(TEXT("DoorwayNotClear")));

	// Far enough back to be furniture rather than an obstruction. The default approach depth is 75,
	// and the wall face is at 5.75, so a fixture whose near face is beyond 80.75 is out of the strip.
	TestFalse(TEXT("A fixture beyond the approach depth is not reported"),
		FHFSpecValidator::Validate(SpecWithBlocker(FVector2D(200.0, 150.0), FVector2D(70.0, 70.0)))
			.Contains(TEXT("DoorwayNotClear")));

	// Above the door head. A pelmet or a high-level cupboard over a doorway is normal construction.
	{
		FHFHouseSpec Spec = MakeValidSpec();
		FHFFixture Overhead = MakeFixture(TEXT("F_Overhead"), EHFFixtureType::KitchenWallCabinet,
			FVector2D(200.0, 45.0));
		Overhead.Footprint = FVector2D(70.0, 70.0);
		Overhead.BaseZ = 215.0;
		Overhead.Height = 60.0;
		Spec.Fixtures.Add(Overhead);

		TestFalse(TEXT("A fixture above the door head is not in the doorway"),
			FHFSpecValidator::Validate(Spec).Contains(TEXT("DoorwayNotClear")));
	}

	// Both limits are project figures rather than constants, and each has to be shown to bite.
	{
		const FHFHouseSpec Spec = SpecWithBlocker(FVector2D(200.0, 45.0), FVector2D(30.0, 70.0));

		// A project willing to accept a 30 cm squeeze can say so.
		FHFValidationLimits Lenient;
		Lenient.MinClearPassageCm = 25.0;
		TestFalse(TEXT("A lower minimum passage lets the same spec through"),
			FHFSpecValidator::Validate(Spec, Lenient).Contains(TEXT("DoorwayNotClear")));

		// And a project that only counts what is hard against the wall can say that too, which is
		// the old behaviour and proves the approach depth is what admits this fixture at all.
		FHFValidationLimits NoApproach;
		NoApproach.DoorApproachDepthCm = 0.0;
		TestFalse(TEXT("A zero approach depth stops seeing a fixture standing off the wall"),
			FHFSpecValidator::Validate(Spec, NoApproach).Contains(TEXT("DoorwayNotClear")));
	}

	// And the flat itself, which is the assertion that matters.
	TestFalse(TEXT("Every doorway in the reference flat is walkable"),
		FHFSpecValidator::Validate(FHFSampleHouse::Make2BHK()).Contains(TEXT("DoorwayNotClear")));

	return true;
}

/**
 * And can the leaf get past.
 *
 * A doorway can measure perfectly and still not open. D_CBath in the reference flat left 675 of its
 * 750 clear - no width rule could ever have complained - but the WC reached 7.5 cm past the hinge
 * jamb into the quadrant the leaf sweeps, and the door fouled it at 56 degrees and lay across it at
 * 90. SwingBlocked did not catch it either: that rule asks where the leaf's TIP lands, and the tip
 * lands in open floor on the far side of the pan.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFValidatorSwingArcTest,
	"HouseForge.Validation.DoorLeafSweepsClear", HF_TEST_FLAGS)

bool FHFValidatorSwingArcTest::RunTest(const FString& Parameters)
{
	// The door is 90 wide, centred at 200 on the south wall, InwardLeft - so it hinges at 155 and
	// sweeps the quadrant x 155..245, y 0..90.
	auto SpecWithFixtureAt = [](const FVector2D& Position, const FVector2D& Footprint)
	{
		FHFHouseSpec Spec = MakeValidSpec();

		FHFFixture Fitting = MakeFixture(TEXT("F_WC"), EHFFixtureType::WC, Position);
		Fitting.Footprint = Footprint;
		Fitting.Height = 40.0;
		Spec.Fixtures.Add(Fitting);

		return Spec;
	};

	// Reaching 4 cm past the hinge jamb, which is the D_CBath case almost to scale: nearly all of the
	// fitting is behind the hinge and out of the way, and the sliver that is not stops the door.
	ExpectIssue(*this, SpecWithFixtureAt(FVector2D(140.0, 40.0), FVector2D(38.0, 60.0)),
		TEXT("DoorSwingHitsFixture"), EHFValidationSeverity::Error);

	// Pulled 21 cm west so it is wholly behind the hinge, and the same fitting is fine. The margin
	// between these two is the whole rule - a width check cannot tell them apart, because neither
	// takes a millimetre off the doorway itself.
	TestFalse(TEXT("A fitting behind the hinge is not in the arc"),
		FHFSpecValidator::Validate(SpecWithFixtureAt(FVector2D(119.0, 40.0), FVector2D(38.0, 60.0)))
			.Contains(TEXT("DoorSwingHitsFixture")));

	TestFalse(TEXT("And neither fitting touches the doorway's own width"),
		FHFSpecValidator::Validate(SpecWithFixtureAt(FVector2D(140.0, 40.0), FVector2D(38.0, 60.0)))
			.Contains(TEXT("DoorwayNotClear")));

	// Beyond the leaf's reach. The arc is only as deep as the door is wide, so a fitting at 100 from
	// a 90 leaf is clear - the rule must not simply report everything near the door.
	TestFalse(TEXT("A fitting beyond the leaf's reach is not in the arc"),
		FHFSpecValidator::Validate(SpecWithFixtureAt(FVector2D(200.0, 130.0), FVector2D(38.0, 30.0)))
			.Contains(TEXT("DoorSwingHitsFixture")));

	// Straight in front of the door, well inside the quadrant.
	ExpectIssue(*this, SpecWithFixtureAt(FVector2D(200.0, 50.0), FVector2D(38.0, 60.0)),
		TEXT("DoorSwingHitsFixture"), EHFValidationSeverity::Error);

	// A sliding door does not sweep, so nothing standing beside it is a swing problem.
	{
		FHFHouseSpec Spec = SpecWithFixtureAt(FVector2D(135.0, 40.0), FVector2D(38.0, 60.0));
		Spec.Openings[0].Kind = EHFOpeningKind::SlidingDoor;
		Spec.Openings[0].Swing = EHFSwing::None;

		TestFalse(TEXT("A sliding door has no arc to block"),
			FHFSpecValidator::Validate(Spec).Contains(TEXT("DoorSwingHitsFixture")));
	}

	// And the flat.
	TestFalse(TEXT("Every door leaf in the reference flat sweeps clear"),
		FHFSpecValidator::Validate(FHFSampleHouse::Make2BHK()).Contains(TEXT("DoorSwingHitsFixture")));

	return true;
}

/**
 * A fixture built into the frame.
 *
 * Nothing compared a fixture with a column or a beam - the column rule looks at columns against
 * openings, the overlap rule at fixtures against each other, and the articulation sweep builds
 * column and beam solids but only sweeps the moving parts of OPENINGS against them. So the utility's
 * 300 mm extract fan had 125 x 45 of itself cored through COL_N1, and both bathroom fans sat 100 mm
 * up inside BM_Mid_Upper, and nothing said a word.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFValidatorFixtureStructureTest,
	"HouseForge.Validation.FixtureClashesWithStructure", HF_TEST_FLAGS)

bool FHFValidatorFixtureStructureTest::RunTest(const FString& Parameters)
{
	auto SpecWithFanAt = [](const FVector2D& Position, double BaseZ)
	{
		FHFHouseSpec Spec = MakeValidSpec();

		FHFFixture Fan = MakeFixture(TEXT("F_Fan"), EHFFixtureType::ExhaustFan, Position);
		Fan.Footprint = FVector2D(30.0, 10.0);
		Fan.Height = 30.0;
		Fan.BaseZ = BaseZ;
		Spec.Fixtures.Add(Fan);

		FHFColumn Column;
		Column.Id = TEXT("COL_1");
		Column.Position = FVector2D(200.0, 150.0);
		Column.Size = FVector2D(45.0, 23.0);
		Column.Height = 300.0;
		Spec.Columns.Add(Column);

		FHFBeam Beam;
		Beam.Id = TEXT("BM_1");
		Beam.Start = FVector2D(0.0, 300.0);
		Beam.End = FVector2D(400.0, 300.0);
		Beam.Width = 23.0;
		Beam.Depth = 45.0;
		Beam.SoffitZ = 300.0;
		Spec.Beams.Add(Beam);

		return Spec;
	};

	// Through the column: an error, because nobody cores an RCC column for a duct.
	{
		FHFHouseSpec Spec = SpecWithFanAt(FVector2D(200.0, 150.0), 220.0);
		ExpectIssue(*this, Spec, TEXT("FixtureClashesWithStructure"), EHFValidationSeverity::Error);

		const FHFValidationResult Result = FHFSpecValidator::Validate(Spec);
		const FHFValidationIssue* Issue = Result.Issues.FindByPredicate(
			[](const FHFValidationIssue& I) { return I.Code == TEXT("FixtureClashesWithStructure"); });
		if (TestNotNull(TEXT("The clash is reported"), Issue))
		{
			TestTrue(TEXT("The message names the column"), Issue->Message.Contains(TEXT("COL_1")));
		}
	}

	// Into the beam: a warning, because services are dropped under one all the time and a bulkhead
	// can take them. The beam occupies z 255..300 at y 288.5..311.5.
	ExpectIssue(*this, SpecWithFanAt(FVector2D(200.0, 300.0), 260.0),
		TEXT("FixtureClashesWithStructure"), EHFValidationSeverity::Warning);

	// Under the beam rather than in it, which is where a fan belongs.
	TestFalse(TEXT("A fixture below the beam soffit is not a clash"),
		FHFSpecValidator::Validate(SpecWithFanAt(FVector2D(200.0, 300.0), 220.0))
			.Contains(TEXT("FixtureClashesWithStructure")));

	// Beside the column, at the same height. Touching is not overlapping: the column spans
	// x 177.5..222.5, so a 30-wide fan centred at 240 shares no ground with it.
	TestFalse(TEXT("A fixture beside the column is not a clash"),
		FHFSpecValidator::Validate(SpecWithFanAt(FVector2D(240.0, 150.0), 220.0))
			.Contains(TEXT("FixtureClashesWithStructure")));

	// And the flat, which had three of these in it.
	TestFalse(TEXT("Nothing in the reference flat is built into the frame"),
		FHFSpecValidator::Validate(FHFSampleHouse::Make2BHK()).Contains(TEXT("FixtureClashesWithStructure")));

	return true;
}

/**
 * Headroom is a centimetre figure and a spec is in whatever it declares.
 *
 * MinHeadroomCm was compared against raw spec coordinates. On the millimetre spec this plugin's
 * reference flat is written in, that asked whether a beam left less than 21 cm beneath it - so the
 * rule never fired, on any millimetre drawing, ever. On a metre spec it fired on every ceiling in
 * the file. The shipped test passed because its spec was in centimetres, which is the one unit where
 * the bug is invisible.
 *
 * Built in all three units from the same physical room, so the assertion is that the report does not
 * depend on the unit it was drawn in.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFValidatorHeadroomUnitsTest,
	"HouseForge.Validation.HeadroomIsUnitIndependent", HF_TEST_FLAGS)

bool FHFValidatorHeadroomUnitsTest::RunTest(const FString& Parameters)
{
	// One room, 3 m walls, with a false ceiling dropped by the given number of centimetres.
	auto MakeRoom = [](EHFUnits Units, double Scale, double CeilingDropCm)
	{
		FHFHouseSpec Spec = MakeValidSpec();
		Spec.Units = Units;

		for (FHFWall& Wall : Spec.Walls)
		{
			Wall.Start *= Scale;
			Wall.End *= Scale;
			Wall.Thickness *= Scale;
			Wall.Height *= Scale;
		}
		for (FHFOpening& Opening : Spec.Openings)
		{
			Opening.OffsetAlongWall *= Scale;
			Opening.Width *= Scale;
			Opening.Height *= Scale;
		}
		for (FHFRoom& Room : Spec.Rooms)
		{
			for (FVector2D& Point : Room.Boundary)
			{
				Point *= Scale;
			}
			Room.CeilingHeight *= Scale;
			Room.SkirtingHeight *= Scale;
		}
		Spec.DefaultWallThickness *= Scale;
		Spec.DefaultWallHeight *= Scale;

		FHFFalseCeiling Ceiling;
		Ceiling.Id = TEXT("FC_1");
		Ceiling.RoomId = TEXT("R_Bedroom");
		Ceiling.Style = EHFCeilingStyle::FullDrop;
		Ceiling.Drop = CeilingDropCm * Scale;
		Spec.FalseCeilings.Add(Ceiling);

		return Spec;
	};

	struct FCase
	{
		EHFUnits Units;
		double Scale;
		const TCHAR* Name;
	};

	const FCase Cases[] = {
		{ EHFUnits::Centimeters, 1.0,   TEXT("centimetres") },
		{ EHFUnits::Millimeters, 10.0,  TEXT("millimetres") },
		{ EHFUnits::Meters,      0.01,  TEXT("metres") },
	};

	for (const FCase& Case : Cases)
	{
		// 300 - 40 = 260 clear, comfortably over the 210 default. Nothing should be said, and on a
		// millimetre or metre spec the old rule got this wrong in one direction or the other.
		TestFalse(*FString::Printf(TEXT("A 260 cm ceiling is not low headroom in %s"), Case.Name),
			FHFSpecValidator::Validate(MakeRoom(Case.Units, Case.Scale, 40.0))
				.Contains(TEXT("LowHeadroom")));

		// 300 - 120 = 180 clear, below the 210 default. This is the case a millimetre spec could
		// never report.
		TestTrue(*FString::Printf(TEXT("A 180 cm ceiling IS low headroom in %s"), Case.Name),
			FHFSpecValidator::Validate(MakeRoom(Case.Units, Case.Scale, 120.0))
				.Contains(TEXT("LowHeadroom")));

		// And the same for a beam: soffit at 300, 120 deep, so 180 clear beneath it.
		{
			FHFHouseSpec Spec = MakeRoom(Case.Units, Case.Scale, 40.0);

			FHFBeam Beam;
			Beam.Id = TEXT("BM_Low");
			Beam.Start = FVector2D(0.0, 300.0 * Case.Scale);
			Beam.End = FVector2D(400.0 * Case.Scale, 300.0 * Case.Scale);
			Beam.Width = 23.0 * Case.Scale;
			Beam.Depth = 120.0 * Case.Scale;
			Beam.SoffitZ = 300.0 * Case.Scale;
			Spec.Beams.Add(Beam);

			TestTrue(*FString::Printf(TEXT("A beam leaving 180 cm IS low headroom in %s"), Case.Name),
				FHFSpecValidator::Validate(Spec).Contains(TEXT("BeamLowHeadroom")));
		}

		// The project figure still governs: raise the floor to 270 and the comfortable room reports.
		{
			FHFValidationLimits Tall;
			Tall.MinHeadroomCm = 270.0;

			TestTrue(*FString::Printf(TEXT("Raising the limit reports the 260 cm ceiling in %s"), Case.Name),
				FHFSpecValidator::Validate(MakeRoom(Case.Units, Case.Scale, 40.0), Tall)
					.Contains(TEXT("LowHeadroom")));
		}
	}

	return true;
}

#undef HF_TEST_FLAGS

#endif // WITH_DEV_AUTOMATION_TESTS
