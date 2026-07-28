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

	// Warnings are allowed - overlapping counters and sinks are legitimate - but they should be
	// visible in the log rather than silently accumulating.
	if (Result.HasWarnings())
	{
		AddInfo(FString::Printf(TEXT("Sample 2BHK validation warnings:\n%s"), *Result.ToString()));
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
	TestEqual(TEXT("Sample has 21 walls"), Spec.Walls.Num(), 21);
	TestEqual(TEXT("Sample has 8 false ceilings"), Spec.FalseCeilings.Num(), 8);
	TestEqual(TEXT("Sample has 9 beams"), Spec.Beams.Num(), 9);
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

	// Only the living room's cross beam actually spans a room interior; every other beam sits
	// over a wall, where the wall itself conceals it.
	const FHFBeam* LivingBeam = Spec.DeepestBeamOverRoom(TEXT("R_Living"));
	TestNotNull(TEXT("The living room's cross beam is detected"), LivingBeam);
	TestNull(TEXT("Perimeter beams are not reported as crossing the kitchen"),
		Spec.DeepestBeamOverRoom(TEXT("R_Kitchen")));
	TestNull(TEXT("Perimeter beams are not reported as crossing the common bathroom"),
		Spec.DeepestBeamOverRoom(TEXT("R_CBath")));

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
