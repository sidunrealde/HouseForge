// Copyright Siddartha G. All Rights Reserved.

#include "HouseForge.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Geometry/HFGenerators.h"
#include "Geometry/HFMeshOps.h"
#include "MeshQueries.h"
#include "Misc/AutomationTest.h"
#include "Model/HFTypes.h"

using namespace UE::Geometry;

#define HF_TEST_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
	FHFRoom MakeRoom()
	{
		FHFRoom Room;
		Room.Id = TEXT("R1");
		Room.Boundary = { FVector2D(0, 0), FVector2D(400, 0), FVector2D(400, 300), FVector2D(0, 300) };
		Room.FloorZ = 0.0;
		Room.CeilingHeight = 300.0;
		return Room;
	}

	/** An L-shaped room, the concave case naive edge offsetting gets wrong. */
	FHFRoom MakeLShapedRoom()
	{
		FHFRoom Room = MakeRoom();
		Room.Boundary = {
			FVector2D(0, 0), FVector2D(400, 0), FVector2D(400, 150),
			FVector2D(200, 150), FVector2D(200, 300), FVector2D(0, 300)
		};
		return Room;
	}

	FHFFalseCeiling MakeCeiling(EHFCeilingStyle Style, double Drop = 20.0, double BandWidth = 60.0)
	{
		FHFFalseCeiling Ceiling;
		Ceiling.Id = TEXT("FC1");
		Ceiling.RoomId = TEXT("R1");
		Ceiling.Style = Style;
		Ceiling.Drop = Drop;
		Ceiling.BandWidth = BandWidth;
		return Ceiling;
	}
}

/**
 * Polygon offset is the foundation every perimeter style rests on. Shifting each edge along its
 * normal produces self-intersecting garbage on a concave corner, and these layouts are full of
 * L-shaped rooms.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFInsetTest, "HouseForge.Ceilings.PolygonInset", HF_TEST_FLAGS)

bool FHFInsetTest::RunTest(const FString& Parameters)
{
	const TArray<FVector2D> Square = { FVector2D(0, 0), FVector2D(400, 0), FVector2D(400, 400), FVector2D(0, 400) };

	const TArray<TArray<FVector2D>> Inset = FHFMeshOps::InsetPolygon(Square, 50.0);
	if (!TestEqual(TEXT("Insetting a square gives one loop"), Inset.Num(), 1))
	{
		return false;
	}

	// A 400 square inset by 50 becomes a 300 square, so its area drops from 160000 to 90000.
	TestNearlyEqual(TEXT("The inset square has the right area"),
		FMath::Abs(FHFMeshOps::SignedArea(Inset[0])), 90000.0, 100.0);

	// Concave input must survive.
	const TArray<FVector2D> LShape = {
		FVector2D(0, 0), FVector2D(400, 0), FVector2D(400, 150),
		FVector2D(200, 150), FVector2D(200, 300), FVector2D(0, 300)
	};
	const TArray<TArray<FVector2D>> InsetL = FHFMeshOps::InsetPolygon(LShape, 30.0);
	TestTrue(TEXT("An L-shape insets to at least one loop"), InsetL.Num() >= 1);

	double InsetArea = 0.0;
	for (const TArray<FVector2D>& Loop : InsetL)
	{
		InsetArea += FMath::Abs(FHFMeshOps::SignedArea(Loop));
	}
	const double OriginalArea = FMath::Abs(FHFMeshOps::SignedArea(LShape));
	TestTrue(TEXT("Insetting an L-shape shrinks it"), InsetArea < OriginalArea && InsetArea > 0.0);

	// Insetting further than the shape is thick must yield nothing, honestly rather than garbage.
	TestEqual(TEXT("An over-wide inset consumes the polygon"),
		FHFMeshOps::InsetPolygon(Square, 500.0).Num(), 0);

	// A narrow corridor inset past half its width should vanish too.
	const TArray<FVector2D> Corridor = { FVector2D(0, 0), FVector2D(400, 0), FVector2D(400, 100), FVector2D(0, 100) };
	TestEqual(TEXT("Insetting past half a corridor's width consumes it"),
		FHFMeshOps::InsetPolygon(Corridor, 60.0).Num(), 0);

	return true;
}

/** Every style must produce solid, correctly positioned geometry below the structural soffit. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFCeilingStylesTest, "HouseForge.Ceilings.AllStyles", HF_TEST_FLAGS)

bool FHFCeilingStylesTest::RunTest(const FString& Parameters)
{
	const FHFRoom Room = MakeRoom();
	const double StructuralZ = Room.FloorZ + Room.CeilingHeight;

	const TArray<EHFCeilingStyle> Styles = {
		EHFCeilingStyle::Peripheral, EHFCeilingStyle::FullDrop,
		EHFCeilingStyle::Tray, EHFCeilingStyle::Cove
	};

	for (const EHFCeilingStyle Style : Styles)
	{
		FHFFalseCeiling Ceiling = MakeCeiling(Style);
		Ceiling.Cove.ChannelWidth = 8.0;
		Ceiling.Cove.LipHeight = 5.0;
		Ceiling.Cove.Setback = 2.0;

		const FDynamicMesh3 Mesh = FHFGenerators::GenerateCeiling(Ceiling, Room, {}, 0.0);
		const FString Name = StaticEnum<EHFCeilingStyle>()->GetNameStringByValue(static_cast<int64>(Style));

		if (!TestTrue(*FString::Printf(TEXT("%s produces geometry"), *Name), Mesh.TriangleCount() > 0))
		{
			continue;
		}

		const FAxisAlignedBox3d Bounds = Mesh.GetBounds();

		// A false ceiling hangs below the structure. If it sat at or above the slab it would not
		// be a suspended ceiling at all.
		TestTrue(*FString::Printf(TEXT("%s hangs below the structural soffit"), *Name),
			Bounds.Min.Z < StructuralZ - 1.0);
		TestTrue(*FString::Printf(TEXT("%s does not poke above the slab"), *Name),
			Bounds.Max.Z <= StructuralZ + 0.1);

		// It must stay within the room in plan.
		TestTrue(*FString::Printf(TEXT("%s stays inside the room"), *Name),
			Bounds.Min.X >= -0.1 && Bounds.Min.Y >= -0.1 &&
			Bounds.Max.X <= 400.1 && Bounds.Max.Y <= 300.1);
	}

	// None generates nothing at all.
	TestEqual(TEXT("Style None produces no geometry"),
		FHFGenerators::GenerateCeiling(MakeCeiling(EHFCeilingStyle::None), Room, {}, 0.0).TriangleCount(), 0);

	return true;
}

/** A peripheral band must leave the centre of the room open; that is what distinguishes it. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFPeripheralTest, "HouseForge.Ceilings.PeripheralLeavesCentreOpen", HF_TEST_FLAGS)

bool FHFPeripheralTest::RunTest(const FString& Parameters)
{
	const FHFRoom Room = MakeRoom();

	// Distinguish the two ways this can go wrong: the inset failing, or the boolean failing.
	const TArray<TArray<FVector2D>> Inner = FHFMeshOps::InsetPolygon(Room.Boundary, 60.0);
	TestEqual(TEXT("The room insets to one inner loop"), Inner.Num(), 1);
	if (Inner.Num() == 1)
	{
		TestNearlyEqual(TEXT("The inner loop is 280 x 180"),
			FMath::Abs(FHFMeshOps::SignedArea(Inner[0])), 280.0 * 180.0, 500.0);
	}

	const FDynamicMesh3 Band = FHFGenerators::GenerateCeiling(MakeCeiling(EHFCeilingStyle::Peripheral, 20.0, 60.0), Room, {}, 0.0);
	const FDynamicMesh3 Full = FHFGenerators::GenerateCeiling(MakeCeiling(EHFCeilingStyle::FullDrop, 20.0), Room, {}, 0.0);

	const double BandVolume = TMeshQueries<FDynamicMesh3>::GetVolumeArea(Band).X;
	const double FullVolume = TMeshQueries<FDynamicMesh3>::GetVolumeArea(Full).X;

	TestTrue(TEXT("A peripheral band has positive volume"), BandVolume > 0.0);
	TestTrue(TEXT("A full drop has positive volume"), FullVolume > 0.0);

	// The band covers only the perimeter, so in plan it must cover far less than the whole room.
	// 400x300 with a 60 band leaves a 280x180 hole - roughly 42 per cent of the area remains.
	const double RoomArea = 400.0 * 300.0;
	const double BandArea = RoomArea - (280.0 * 180.0);
	const double BandFootprintVolume = BandArea * 20.0;

	TestNearlyEqual(TEXT("The band covers only its perimeter width"),
		BandVolume, BandFootprintVolume, BandFootprintVolume * 0.05);

	// A band wider than the room degrades to a full drop rather than producing nothing.
	const FDynamicMesh3 Overwide = FHFGenerators::GenerateCeiling(
		MakeCeiling(EHFCeilingStyle::Peripheral, 20.0, 500.0), Room, {}, 0.0);
	TestTrue(TEXT("An over-wide band still produces a ceiling"), Overwide.TriangleCount() > 0);

	return true;
}

/** Concave rooms are the case the whole polygon-offset dependency exists for. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFConcaveCeilingTest, "HouseForge.Ceilings.ConcaveRoom", HF_TEST_FLAGS)

bool FHFConcaveCeilingTest::RunTest(const FString& Parameters)
{
	const FHFRoom Room = MakeLShapedRoom();
	const FDynamicMesh3 Mesh = FHFGenerators::GenerateCeiling(
		MakeCeiling(EHFCeilingStyle::Peripheral, 20.0, 40.0), Room, {}, 0.0);

	TestTrue(TEXT("An L-shaped room gets a ceiling"), Mesh.TriangleCount() > 0);
	TestTrue(TEXT("Its volume is positive, so it is not self-intersecting"),
		TMeshQueries<FDynamicMesh3>::GetVolumeArea(Mesh).X > 0.0);

	// The bite must remain absent: a naive offset would spill geometry into it.
	const FAxisAlignedBox3d Bounds = Mesh.GetBounds();
	TestTrue(TEXT("The ceiling stays within the room's bounding box"),
		Bounds.Min.X >= -0.1 && Bounds.Max.X <= 400.1 &&
		Bounds.Min.Y >= -0.1 && Bounds.Max.Y <= 300.1);

	// The removed corner is at (300, 250); no ceiling should reach it.
	const double RectangleArea = 400.0 * 300.0;
	const double LArea = FMath::Abs(FHFMeshOps::SignedArea(Room.Boundary));
	TestTrue(TEXT("The test room really is concave"), LArea < RectangleArea);

	const double Volume = TMeshQueries<FDynamicMesh3>::GetVolumeArea(Mesh).X;
	TestTrue(TEXT("The ceiling covers less than the full rectangle would"),
		Volume < RectangleArea * 20.0);

	return true;
}

/** A fan hanging from a soffit it does not penetrate is an obvious tell. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFFanDropTest, "HouseForge.Ceilings.FanDropIsCut", HF_TEST_FLAGS)

bool FHFFanDropTest::RunTest(const FString& Parameters)
{
	const FHFRoom Room = MakeRoom();
	const FHFFalseCeiling Ceiling = MakeCeiling(EHFCeilingStyle::FullDrop, 30.0);

	const FDynamicMesh3 Plain = FHFGenerators::GenerateCeiling(Ceiling, Room, {}, 0.0);
	const FDynamicMesh3 Cut = FHFGenerators::GenerateCeiling(Ceiling, Room, { FVector2D(200.0, 150.0) }, 10.0);

	TestTrue(TEXT("An uncut ceiling generates"), Plain.TriangleCount() > 0);
	TestTrue(TEXT("A fan drop cuts through the ceiling"),
		TMeshQueries<FDynamicMesh3>::GetVolumeArea(Cut).X <
		TMeshQueries<FDynamicMesh3>::GetVolumeArea(Plain).X);

	// A zero radius must not cut anything, so fans can be present without penetrating.
	const FDynamicMesh3 NoRadius = FHFGenerators::GenerateCeiling(Ceiling, Room, { FVector2D(200.0, 150.0) }, 0.0);
	TestNearlyEqual(TEXT("A zero drop radius cuts nothing"),
		TMeshQueries<FDynamicMesh3>::GetVolumeArea(NoRadius).X,
		TMeshQueries<FDynamicMesh3>::GetVolumeArea(Plain).X, 1.0);

	return true;
}

/** Looking up in the middle of a peripheral ceiling must show structure, not open sky. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFCeilingSlabTest, "HouseForge.Ceilings.StructuralSlab", HF_TEST_FLAGS)

bool FHFCeilingSlabTest::RunTest(const FString& Parameters)
{
	const FHFRoom Room = MakeRoom();
	const FDynamicMesh3 Slab = FHFGenerators::GenerateCeilingSlab(Room, 15.0);

	TestTrue(TEXT("A ceiling slab generates"), Slab.TriangleCount() > 0);
	TestTrue(TEXT("A ceiling slab is watertight"), FHFMeshOps::IsClosed(Slab));

	const FAxisAlignedBox3d Bounds = Slab.GetBounds();
	TestNearlyEqual(TEXT("Its underside is at the room's ceiling height"), Bounds.Min.Z, 300.0, 0.01);
	TestNearlyEqual(TEXT("It thickens upward"), Bounds.Max.Z, 315.0, 0.01);

	return true;
}

#undef HF_TEST_FLAGS

#endif // WITH_DEV_AUTOMATION_TESTS
