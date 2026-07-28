// Copyright Siddartha G. All Rights Reserved.

#include "HouseForge.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Model/HFTypes.h"
#include "Tests/HFSpecTestHelpers.h"

#define HF_TEST_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** Room area and winding, including the concave shapes these layouts are full of. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFRoomAreaTest, "HouseForge.Model.RoomArea", HF_TEST_FLAGS)

bool FHFRoomAreaTest::RunTest(const FString& Parameters)
{
	FHFRoom Rect;
	Rect.Boundary = { FVector2D(0, 0), FVector2D(400, 0), FVector2D(400, 300), FVector2D(0, 300) };
	TestNearlyEqual(TEXT("400x300 rectangle has area 120000"), Rect.Area(), 120000.0, 0.01);
	TestTrue(TEXT("Counter-clockwise winding gives positive signed area"), Rect.SignedArea() > 0.0);

	// Same shape wound the other way: area is unchanged, sign flips. Generators rely on Area()
	// being winding-independent so a clockwise boundary from a drawing still measures correctly.
	FHFRoom Reversed;
	Reversed.Boundary = { FVector2D(0, 300), FVector2D(400, 300), FVector2D(400, 0), FVector2D(0, 0) };
	TestNearlyEqual(TEXT("Reversed winding has the same unsigned area"), Reversed.Area(), 120000.0, 0.01);
	TestTrue(TEXT("Clockwise winding gives negative signed area"), Reversed.SignedArea() < 0.0);

	// L-shaped room: a 400x300 rectangle with a 200x150 bite taken out of the top-right.
	FHFRoom LShape;
	LShape.Boundary = {
		FVector2D(0, 0), FVector2D(400, 0), FVector2D(400, 150),
		FVector2D(200, 150), FVector2D(200, 300), FVector2D(0, 300)
	};
	TestNearlyEqual(TEXT("L-shaped room area excludes the bite"), LShape.Area(), 120000.0 - 30000.0, 0.01);

	FHFRoom Degenerate;
	Degenerate.Boundary = { FVector2D(0, 0), FVector2D(100, 0) };
	TestNearlyEqual(TEXT("Fewer than 3 points encloses no area"), Degenerate.Area(), 0.0, UE_DOUBLE_KINDA_SMALL_NUMBER);

	return true;
}

/**
 * Point containment must be correct for concave rooms. A convex-only test would place a fixture
 * "inside" a re-entrant corner that is actually outside the room, and the validator would pass a
 * wardrobe standing in the corridor.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFRoomContainsPointTest, "HouseForge.Model.RoomContainsPoint", HF_TEST_FLAGS)

bool FHFRoomContainsPointTest::RunTest(const FString& Parameters)
{
	FHFRoom Rect;
	Rect.Boundary = { FVector2D(0, 0), FVector2D(400, 0), FVector2D(400, 300), FVector2D(0, 300) };

	TestTrue(TEXT("Centre is inside"), Rect.ContainsPoint(FVector2D(200, 150)));
	TestTrue(TEXT("Near-corner interior point is inside"), Rect.ContainsPoint(FVector2D(5, 5)));
	TestFalse(TEXT("Point beyond the east wall is outside"), Rect.ContainsPoint(FVector2D(500, 150)));
	TestFalse(TEXT("Point below the south wall is outside"), Rect.ContainsPoint(FVector2D(200, -10)));
	TestFalse(TEXT("Point left of the west wall is outside"), Rect.ContainsPoint(FVector2D(-1, 150)));

	// The bite is removed from the top-right, so (300, 250) is outside despite being within the
	// overall bounding box - exactly the case a bounds check would get wrong.
	FHFRoom LShape;
	LShape.Boundary = {
		FVector2D(0, 0), FVector2D(400, 0), FVector2D(400, 150),
		FVector2D(200, 150), FVector2D(200, 300), FVector2D(0, 300)
	};

	TestTrue(TEXT("Point in the L's lower arm is inside"), LShape.ContainsPoint(FVector2D(300, 75)));
	TestTrue(TEXT("Point in the L's upper arm is inside"), LShape.ContainsPoint(FVector2D(100, 250)));
	TestFalse(TEXT("Point in the removed bite is outside despite being in the bounding box"),
		LShape.ContainsPoint(FVector2D(300, 250)));

	return true;
}

/**
 * Units conversion is the single point where millimetres become centimetres. Getting it wrong
 * scales the whole house by ten, and running it twice would do so again - hence the idempotence
 * check, which matters because a spec can reach the builder through more than one path.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFUnitsConversionTest, "HouseForge.Model.UnitsConversion", HF_TEST_FLAGS)

bool FHFUnitsConversionTest::RunTest(const FString& Parameters)
{
	TestNearlyEqual(TEXT("mm to cm scale is 0.1"), FHFUnits::ToCentimeterScale(EHFUnits::Millimeters), 0.1, UE_DOUBLE_KINDA_SMALL_NUMBER);
	TestNearlyEqual(TEXT("cm to cm scale is 1"), FHFUnits::ToCentimeterScale(EHFUnits::Centimeters), 1.0, UE_DOUBLE_KINDA_SMALL_NUMBER);
	TestNearlyEqual(TEXT("m to cm scale is 100"), FHFUnits::ToCentimeterScale(EHFUnits::Meters), 100.0, UE_DOUBLE_KINDA_SMALL_NUMBER);

	// A realistic millimetre spec: a 4000x3000 room with 115 thick, 3000 tall walls.
	FHFHouseSpec Spec;
	Spec.Units = EHFUnits::Millimeters;
	Spec.DefaultWallThickness = 115.0;
	Spec.DefaultWallHeight = 3000.0;

	FHFWall Wall;
	Wall.Id = TEXT("W1");
	Wall.Start = FVector2D(0.0, 0.0);
	Wall.End = FVector2D(4000.0, 0.0);
	Wall.Thickness = 115.0;
	Wall.Height = 3000.0;
	Spec.Walls.Add(Wall);

	FHFOpening Door;
	Door.Id = TEXT("D1");
	Door.WallId = TEXT("W1");
	Door.OffsetAlongWall = 2000.0;
	Door.Width = 900.0;
	Door.Height = 2100.0;
	Spec.Openings.Add(Door);

	FHFRoom Room;
	Room.Id = TEXT("R1");
	Room.Boundary = { FVector2D(0, 0), FVector2D(4000, 0), FVector2D(4000, 3000), FVector2D(0, 3000) };
	Room.CeilingHeight = 3000.0;
	Room.SkirtingHeight = 100.0;
	Spec.Rooms.Add(Room);

	FHFFixture Wardrobe;
	Wardrobe.Id = TEXT("F1");
	Wardrobe.RoomId = TEXT("R1");
	Wardrobe.Position = FVector2D(2000.0, 2700.0);
	Wardrobe.Footprint = FVector2D(1800.0, 600.0);
	Wardrobe.Height = 2400.0;
	Wardrobe.RotationDegrees = 90.0;
	Wardrobe.Params.ShutterCount = 3;
	Wardrobe.Params.PlinthHeight = 100.0;
	Spec.Fixtures.Add(Wardrobe);

	FHFUnits::ConvertToCentimeters(Spec);

	TestEqual(TEXT("Units are now centimetres"), Spec.Units, EHFUnits::Centimeters);
	TestNearlyEqual(TEXT("Wall length is 400cm"), Spec.Walls[0].Length(), 400.0, 0.01);
	TestNearlyEqual(TEXT("Wall thickness is 11.5cm"), Spec.Walls[0].Thickness, 11.5, 0.01);
	TestNearlyEqual(TEXT("Wall height is 300cm"), Spec.Walls[0].Height, 300.0, 0.01);
	TestNearlyEqual(TEXT("Door width is 90cm"), Spec.Openings[0].Width, 90.0, 0.01);
	TestNearlyEqual(TEXT("Door offset is 200cm"), Spec.Openings[0].OffsetAlongWall, 200.0, 0.01);
	TestNearlyEqual(TEXT("Room area is 120000 sq cm"), Spec.Rooms[0].Area(), 120000.0, 0.1);
	TestNearlyEqual(TEXT("Skirting is 10cm"), Spec.Rooms[0].SkirtingHeight, 10.0, 0.01);
	TestNearlyEqual(TEXT("Wardrobe is 180cm wide"), Spec.Fixtures[0].Footprint.X, 180.0, 0.01);
	TestNearlyEqual(TEXT("Wardrobe plinth is 10cm"), Spec.Fixtures[0].Params.PlinthHeight, 10.0, 0.01);

	// Dimensionless values must not be scaled.
	TestEqual(TEXT("Shutter count is unscaled"), Spec.Fixtures[0].Params.ShutterCount, 3);
	TestNearlyEqual(TEXT("Rotation is unscaled"), Spec.Fixtures[0].RotationDegrees, 90.0, UE_DOUBLE_KINDA_SMALL_NUMBER);

	// Idempotence: converting an already-converted spec must change nothing.
	FHFUnits::ConvertToCentimeters(Spec);
	TestNearlyEqual(TEXT("Second conversion leaves wall length alone"), Spec.Walls[0].Length(), 400.0, 0.01);
	TestNearlyEqual(TEXT("Second conversion leaves room area alone"), Spec.Rooms[0].Area(), 120000.0, 0.1);

	return true;
}

/** Id lookup is used by every validator rule and every generator. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFSpecLookupTest, "HouseForge.Model.SpecLookup", HF_TEST_FLAGS)

bool FHFSpecLookupTest::RunTest(const FString& Parameters)
{
	const FHFHouseSpec Spec = HouseForgeTest::MakeValidSpec();

	const FHFWall* Wall = Spec.FindWall(TEXT("W_South"));
	TestNotNull(TEXT("Existing wall is found"), Wall);
	if (Wall)
	{
		TestNearlyEqual(TEXT("Found wall has the expected length"), Wall->Length(), 400.0, 0.01);
	}

	TestNull(TEXT("Missing wall returns null"), Spec.FindWall(TEXT("W_Nonexistent")));

	const FHFRoom* Room = Spec.FindRoom(TEXT("R_Bedroom"));
	TestNotNull(TEXT("Existing room is found"), Room);
	TestNull(TEXT("Missing room returns null"), Spec.FindRoom(TEXT("R_Nonexistent")));

	TestNearlyEqual(TEXT("Total floor area sums the rooms"), Spec.TotalFloorArea(), 120000.0, 0.1);

	return true;
}

#undef HF_TEST_FLAGS

#endif // WITH_DEV_AUTOMATION_TESTS
