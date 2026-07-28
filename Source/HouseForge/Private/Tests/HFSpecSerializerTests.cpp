// Copyright Siddartha G. All Rights Reserved.

#include "HouseForge.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Model/HFSpecSerializer.h"
#include "Model/HFTypes.h"
#include "Tests/HFSpecTestHelpers.h"

#define HF_TEST_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The JSON round-trip is the contract with Claude: whatever it writes must survive parsing
 * unchanged. A field that silently drops here becomes a house missing its wardrobes.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFSpecRoundTripTest, "HouseForge.Model.SpecJsonRoundTrip", HF_TEST_FLAGS)

bool FHFSpecRoundTripTest::RunTest(const FString& Parameters)
{
	FHFHouseSpec Original = HouseForgeTest::MakeValidSpec();

	// Populate the parts a bare spec leaves empty, so the round-trip covers nested structs,
	// nested arrays, enums and the param bag rather than just the trivial top level.
	FHFFalseCeiling Ceiling;
	Ceiling.Id = TEXT("FC1");
	Ceiling.RoomId = TEXT("R_Bedroom");
	Ceiling.Style = EHFCeilingStyle::Cove;
	Ceiling.Drop = 20.0;
	Ceiling.BandWidth = 60.0;
	Ceiling.Cove.ChannelWidth = 8.0;
	Ceiling.Cove.LipHeight = 4.0;
	Ceiling.Cove.bHasLedStrip = true;
	Ceiling.LightPositions = { FVector2D(100.0, 100.0), FVector2D(300.0, 200.0) };
	Original.FalseCeilings.Add(Ceiling);

	FHFFixture Wardrobe = HouseForgeTest::MakeFixture(TEXT("F_Wardrobe"), EHFFixtureType::Wardrobe, FVector2D(200.0, 250.0));
	Wardrobe.Label = TEXT("3-door wardrobe");
	Wardrobe.Footprint = FVector2D(180.0, 60.0);
	Wardrobe.Height = 240.0;
	Wardrobe.RotationDegrees = 180.0;
	Wardrobe.AnchorWallId = TEXT("W_North");
	Wardrobe.Params.ShutterCount = 3;
	Wardrobe.Params.ShelfCount = 5;
	Wardrobe.Params.bHasLoft = true;
	Wardrobe.Params.bHasHangingRail = true;
	Wardrobe.Params.HandleStyle = EHFHandleStyle::JProfile;
	Original.Fixtures.Add(Wardrobe);

	FString Json;
	FString Error;
	if (!TestTrue(TEXT("Spec serialises to JSON"), FHFSpecSerializer::ToJsonString(Original, Json, Error)))
	{
		AddError(FString::Printf(TEXT("Serialisation failed: %s"), *Error));
		return false;
	}

	TestFalse(TEXT("Serialised JSON is not empty"), Json.IsEmpty());

	FHFHouseSpec Parsed;
	if (!TestTrue(TEXT("JSON parses back into a spec"), FHFSpecSerializer::FromJsonString(Json, Parsed, Error)))
	{
		AddError(FString::Printf(TEXT("Parsing failed: %s"), *Error));
		return false;
	}

	// Top level
	TestEqual(TEXT("Schema version survives"), Parsed.SchemaVersion, Original.SchemaVersion);
	TestEqual(TEXT("Name survives"), Parsed.Name, Original.Name);
	TestEqual(TEXT("Units survive"), Parsed.Units, Original.Units);

	// Collections
	TestEqual(TEXT("Wall count survives"), Parsed.Walls.Num(), Original.Walls.Num());
	TestEqual(TEXT("Opening count survives"), Parsed.Openings.Num(), Original.Openings.Num());
	TestEqual(TEXT("Room count survives"), Parsed.Rooms.Num(), Original.Rooms.Num());
	TestEqual(TEXT("False ceiling count survives"), Parsed.FalseCeilings.Num(), Original.FalseCeilings.Num());
	TestEqual(TEXT("Fixture count survives"), Parsed.Fixtures.Num(), Original.Fixtures.Num());

	// Wall geometry, to the precision the drawings are read at
	if (Parsed.Walls.Num() == Original.Walls.Num() && Parsed.Walls.Num() > 0)
	{
		TestEqual(TEXT("Wall id survives"), Parsed.Walls[0].Id, Original.Walls[0].Id);
		TestTrue(TEXT("Wall start survives"), Parsed.Walls[0].Start.Equals(Original.Walls[0].Start, UE_KINDA_SMALL_NUMBER));
		TestTrue(TEXT("Wall end survives"), Parsed.Walls[0].End.Equals(Original.Walls[0].End, UE_KINDA_SMALL_NUMBER));
		TestNearlyEqual(TEXT("Wall thickness survives"), Parsed.Walls[0].Thickness, Original.Walls[0].Thickness, UE_DOUBLE_KINDA_SMALL_NUMBER);
		TestTrue(TEXT("Wall external flag survives"), Parsed.Walls[0].bIsExternal);
	}

	// Room boundary array
	if (Parsed.Rooms.Num() > 0)
	{
		TestEqual(TEXT("Room boundary point count survives"), Parsed.Rooms[0].Boundary.Num(), Original.Rooms[0].Boundary.Num());
		TestEqual(TEXT("Room type enum survives"), Parsed.Rooms[0].Type, EHFRoomType::Bedroom);
		TestNearlyEqual(TEXT("Room area survives"), Parsed.Rooms[0].Area(), Original.Rooms[0].Area(), 0.01);
	}

	// Nested struct inside a struct, plus its array
	if (Parsed.FalseCeilings.Num() > 0)
	{
		const FHFFalseCeiling& P = Parsed.FalseCeilings[0];
		TestEqual(TEXT("Ceiling style enum survives"), P.Style, EHFCeilingStyle::Cove);
		TestNearlyEqual(TEXT("Cove channel width survives"), P.Cove.ChannelWidth, 8.0, UE_DOUBLE_KINDA_SMALL_NUMBER);
		TestTrue(TEXT("Cove LED flag survives"), P.Cove.bHasLedStrip);
		TestEqual(TEXT("Light position count survives"), P.LightPositions.Num(), 2);
	}

	// The param bag, which is where a silent drop would be least visible
	if (Parsed.Fixtures.Num() > 0)
	{
		const FHFFixture& P = Parsed.Fixtures[0];
		TestEqual(TEXT("Fixture type enum survives"), P.Type, EHFFixtureType::Wardrobe);
		TestEqual(TEXT("Fixture label survives"), P.Label, Wardrobe.Label);
		TestEqual(TEXT("Shutter count survives"), P.Params.ShutterCount, 3);
		TestEqual(TEXT("Shelf count survives"), P.Params.ShelfCount, 5);
		TestTrue(TEXT("Loft flag survives"), P.Params.bHasLoft);
		TestEqual(TEXT("Handle style enum survives"), P.Params.HandleStyle, EHFHandleStyle::JProfile);
		TestEqual(TEXT("Anchor wall survives"), P.AnchorWallId, FName(TEXT("W_North")));
		TestNearlyEqual(TEXT("Rotation survives"), P.RotationDegrees, 180.0, UE_DOUBLE_KINDA_SMALL_NUMBER);
	}

	return true;
}

/** Malformed input must be rejected with a reason, not accepted as an empty house. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFSpecRejectsBadJsonTest, "HouseForge.Model.SpecRejectsMalformedJson", HF_TEST_FLAGS)

bool FHFSpecRejectsBadJsonTest::RunTest(const FString& Parameters)
{
	FHFHouseSpec Spec;
	FString Error;

	TestFalse(TEXT("Empty string is rejected"), FHFSpecSerializer::FromJsonString(TEXT(""), Spec, Error));
	TestFalse(TEXT("Empty-string rejection explains itself"), Error.IsEmpty());

	Error.Reset();
	TestFalse(TEXT("Truncated JSON is rejected"), FHFSpecSerializer::FromJsonString(TEXT("{\"name\": \"broken\""), Spec, Error));
	TestFalse(TEXT("Truncated-JSON rejection explains itself"), Error.IsEmpty());

	Error.Reset();
	TestFalse(TEXT("Non-object JSON is rejected"), FHFSpecSerializer::FromJsonString(TEXT("[1, 2, 3]"), Spec, Error));

	return true;
}

#undef HF_TEST_FLAGS

#endif // WITH_DEV_AUTOMATION_TESTS
