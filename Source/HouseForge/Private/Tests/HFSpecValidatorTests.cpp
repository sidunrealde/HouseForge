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

#undef HF_TEST_FLAGS

#endif // WITH_DEV_AUTOMATION_TESTS
