// Copyright Siddartha G. All Rights Reserved.

#include "HouseForge.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
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

#undef HF_TEST_FLAGS

#endif // WITH_DEV_AUTOMATION_TESTS
