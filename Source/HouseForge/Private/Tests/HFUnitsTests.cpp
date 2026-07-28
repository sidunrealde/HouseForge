// Copyright Siddartha G. All Rights Reserved.

#include "HouseForge.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Model/HFSpecValidator.h"
#include "Model/HFTypes.h"
#include "Tests/HFSpecTestHelpers.h"

#define HF_TEST_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** Imperial drawings are common, and the conversions have to be exact rather than approximate. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFImperialScaleTest, "HouseForge.Units.ImperialScales", HF_TEST_FLAGS)

bool FHFImperialScaleTest::RunTest(const FString& Parameters)
{
	TestNearlyEqual(TEXT("One foot is 30.48 cm"), FHFUnits::ToCentimeterScale(EHFUnits::Feet), 30.48, UE_DOUBLE_KINDA_SMALL_NUMBER);
	TestNearlyEqual(TEXT("One inch is 2.54 cm"), FHFUnits::ToCentimeterScale(EHFUnits::Inches), 2.54, UE_DOUBLE_KINDA_SMALL_NUMBER);

	// A whole spec authored in feet must land at the same size as its metric equivalent.
	FHFHouseSpec Imperial;
	Imperial.Units = EHFUnits::Feet;

	FHFWall Wall;
	Wall.Id = TEXT("W1");
	Wall.Start = FVector2D(0.0, 0.0);
	Wall.End = FVector2D(20.0, 0.0);		// 20 ft
	Wall.Thickness = 0.75;					// 9 in
	Wall.Height = 10.0;						// 10 ft
	Imperial.Walls.Add(Wall);

	FHFRoom Room;
	Room.Id = TEXT("R1");
	Room.Boundary = { FVector2D(0, 0), FVector2D(20, 0), FVector2D(20, 15), FVector2D(0, 15) };
	Room.CeilingHeight = 10.0;
	Imperial.Rooms.Add(Room);

	FHFUnits::ConvertToCentimeters(Imperial);

	TestEqual(TEXT("Units become centimetres"), Imperial.Units, EHFUnits::Centimeters);
	TestNearlyEqual(TEXT("20 ft is 609.6 cm"), Imperial.Walls[0].Length(), 609.6, 0.01);
	TestNearlyEqual(TEXT("9 in is 22.86 cm"), Imperial.Walls[0].Thickness, 22.86, 0.01);
	// 20 x 15 ft = 300 sq ft = 27.87 sq m
	TestNearlyEqual(TEXT("A 20x15 ft room is 27.87 sq m"), Imperial.TotalFloorArea() / 10'000.0, 27.8709, 0.01);

	return true;
}

/**
 * Dimension strings on a drawing are written for people, not parsers. Converting 12'-6" by hand is
 * exactly the sort of arithmetic that goes wrong quietly.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFParseLengthTest, "HouseForge.Units.ParseDimensionStrings", HF_TEST_FLAGS)

bool FHFParseLengthTest::RunTest(const FString& Parameters)
{
	auto Expect = [this](const TCHAR* Text, EHFUnits Default, double ExpectedCm)
	{
		double Actual = 0.0;
		const bool bParsed = FHFUnits::ParseLengthToCentimeters(Text, Default, Actual);
		if (!TestTrue(*FString::Printf(TEXT("'%s' parses"), Text), bParsed))
		{
			return;
		}
		TestNearlyEqual(*FString::Printf(TEXT("'%s' is %.2f cm"), Text, ExpectedCm), Actual, ExpectedCm, 0.01);
	};

	// Imperial, in the forms drawings actually use.
	Expect(TEXT("12'-6\""), EHFUnits::Millimeters, 381.0);		// 12 ft + 6 in
	Expect(TEXT("12' 6\""), EHFUnits::Millimeters, 381.0);
	Expect(TEXT("12'"), EHFUnits::Millimeters, 365.76);
	Expect(TEXT("12.5'"), EHFUnits::Millimeters, 381.0);
	Expect(TEXT("78\""), EHFUnits::Millimeters, 198.12);

	// Typographic quotes, which is what a PDF usually contains.
	Expect(TEXT("12′-6″"), EHFUnits::Millimeters, 381.0);

	// Metric, with and without a suffix. mm must win over m.
	Expect(TEXT("3600mm"), EHFUnits::Meters, 360.0);
	Expect(TEXT("360cm"), EHFUnits::Millimeters, 360.0);
	Expect(TEXT("3.6m"), EHFUnits::Millimeters, 360.0);
	Expect(TEXT("3600"), EHFUnits::Millimeters, 360.0);
	Expect(TEXT("3600"), EHFUnits::Centimeters, 3600.0);

	double Ignored = 0.0;
	TestFalse(TEXT("Nonsense is rejected"), FHFUnits::ParseLengthToCentimeters(TEXT("about a metre"), EHFUnits::Millimeters, Ignored));
	TestFalse(TEXT("Empty text is rejected"), FHFUnits::ParseLengthToCentimeters(TEXT("   "), EHFUnits::Millimeters, Ignored));

	return true;
}

/**
 * The reason this rule exists at all.
 *
 * A unit misread leaves the spec perfectly self-consistent - every wall still meets, every opening
 * still fits - so no structural rule can catch it. Only asking whether the result is the size of a
 * dwelling can, and the message has to name the unit that would have been right.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFImplausibleScaleTest, "HouseForge.Units.CatchesUnitMisread", HF_TEST_FLAGS)

bool FHFImplausibleScaleTest::RunTest(const FString& Parameters)
{
	// The baseline is a 400x300 cm room - 12 sq m, plausible - correctly declared in centimetres.
	FHFHouseSpec Good = HouseForgeTest::MakeValidSpec();
	Good.UnitsSource = TEXT("title block");
	TestFalse(TEXT("A plausibly sized spec is accepted"),
		FHFSpecValidator::Validate(Good).Contains(TEXT("ImplausibleScale")));

	// Same numbers, mistakenly declared as millimetres: the house becomes 0.12 sq m.
	FHFHouseSpec Misread = Good;
	Misread.Units = EHFUnits::Millimeters;

	const FHFValidationResult Result = FHFSpecValidator::Validate(Misread);
	TestTrue(TEXT("A tenfold unit misread is caught"), Result.Contains(TEXT("ImplausibleScale")));
	TestTrue(TEXT("It is an error, not a warning"), Result.HasErrors());

	const FHFValidationIssue* Issue = Result.Issues.FindByPredicate(
		[](const FHFValidationIssue& I) { return I.Code == TEXT("ImplausibleScale"); });
	if (Issue != nullptr)
	{
		// The whole value of the rule is telling you which unit was meant.
		TestTrue(TEXT("The message suggests centimetres"), Issue->Message.Contains(TEXT("cm")));
		TestTrue(TEXT("The message points at the title block"), Issue->Message.Contains(TEXT("title block")));
	}

	// And the other direction: metres would make it 12,000 sq m.
	FHFHouseSpec TooBig = Good;
	TooBig.Units = EHFUnits::Meters;
	TestTrue(TEXT("An oversized misread is caught too"),
		FHFSpecValidator::Validate(TooBig).Contains(TEXT("ImplausibleScale")));

	// A blank unitsSource is a warning: units must be read, not assumed.
	FHFHouseSpec NoSource = Good;
	NoSource.UnitsSource.Empty();
	TestTrue(TEXT("A blank unitsSource is flagged"),
		FHFSpecValidator::Validate(NoSource).Contains(TEXT("MissingUnitsSource")));

	return true;
}

/** Per-element sanity, which catches a single mistyped figure the aggregate check would absorb. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFElementPlausibilityTest, "HouseForge.Units.ElementPlausibility", HF_TEST_FLAGS)

bool FHFElementPlausibilityTest::RunTest(const FString& Parameters)
{
	{
		FHFHouseSpec Spec = HouseForgeTest::MakeValidSpec();
		Spec.Rooms[0].CeilingHeight = 800.0;	// 8 m
		TestTrue(TEXT("An absurd ceiling height is flagged"),
			FHFSpecValidator::Validate(Spec).Contains(TEXT("ImplausibleCeilingHeight")));
	}
	{
		FHFHouseSpec Spec = HouseForgeTest::MakeValidSpec();
		Spec.Walls[0].Thickness = 150.0;		// 1.5 m thick partition
		TestTrue(TEXT("An absurd wall thickness is flagged"),
			FHFSpecValidator::Validate(Spec).Contains(TEXT("ImplausibleWallThickness")));
	}
	{
		FHFHouseSpec Spec = HouseForgeTest::MakeValidSpec();
		Spec.Openings[0].Width = 300.0;			// 3 m wide door
		TestTrue(TEXT("An absurd door width is flagged"),
			FHFSpecValidator::Validate(Spec).Contains(TEXT("ImplausibleDoorSize")));
	}
	{
		// The baseline must stay clean, or the rules above prove nothing.
		FHFHouseSpec Spec = HouseForgeTest::MakeValidSpec();
		const FHFValidationResult Result = FHFSpecValidator::Validate(Spec);
		TestFalse(TEXT("A sane spec trips no plausibility rule"),
			Result.Contains(TEXT("ImplausibleCeilingHeight")) ||
			Result.Contains(TEXT("ImplausibleWallThickness")) ||
			Result.Contains(TEXT("ImplausibleDoorSize")));
	}

	return true;
}

/**
 * Swing direction was carried as data but never checked or drawn, so a door hung on the wrong side
 * was invisible in a top-down view - the one view used to compare against the drawing.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFSwingTest, "HouseForge.Units.DoorSwing", HF_TEST_FLAGS)

bool FHFSwingTest::RunTest(const FString& Parameters)
{
	// The baseline door is in the south wall of a room lying to the north, swinging inward.
	FHFHouseSpec Spec = HouseForgeTest::MakeValidSpec();
	TestFalse(TEXT("A door opening into its room is not flagged"),
		FHFSpecValidator::Validate(Spec).Contains(TEXT("SwingBlocked")));

	// Turned outward, the leaf sweeps into open air outside the building.
	FHFHouseSpec Outward = HouseForgeTest::MakeValidSpec();
	Outward.Openings[0].Swing = EHFSwing::OutwardLeft;
	TestTrue(TEXT("A door swinging into solid construction is flagged"),
		FHFSpecValidator::Validate(Outward).Contains(TEXT("SwingBlocked")));

	// A hinged door with no swing recorded means the swing arc was not read off the plan.
	FHFHouseSpec NoSwing = HouseForgeTest::MakeValidSpec();
	NoSwing.Openings[0].Swing = EHFSwing::None;
	TestTrue(TEXT("A door with no swing is flagged"),
		FHFSpecValidator::Validate(NoSwing).Contains(TEXT("MissingSwing")));

	// Sliding doors legitimately have no swing and must not be nagged about.
	FHFHouseSpec Sliding = HouseForgeTest::MakeValidSpec();
	Sliding.Openings[0].Kind = EHFOpeningKind::SlidingDoor;
	Sliding.Openings[0].Swing = EHFSwing::None;
	TestFalse(TEXT("A sliding door needs no swing"),
		FHFSpecValidator::Validate(Sliding).Contains(TEXT("MissingSwing")));

	return true;
}

#undef HF_TEST_FLAGS

#endif // WITH_DEV_AUTOMATION_TESTS
