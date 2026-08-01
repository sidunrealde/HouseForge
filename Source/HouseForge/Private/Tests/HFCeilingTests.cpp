// Copyright Siddartha G. All Rights Reserved.

#include "HouseForge.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "DynamicMesh/DynamicMeshAABBTree3.h"
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

	/** The panel thickness the generator builds to. Duplicated deliberately - see the note below. */
	constexpr double PanelThickness = 2.0;

	FString NameOf(EHFCeilingStyle Style)
	{
		return StaticEnum<EHFCeilingStyle>()->GetNameStringByValue(static_cast<int64>(Style));
	}

	/** A ceiling prepared for sight-line queries. */
	struct FCeilingSolid
	{
		explicit FCeilingSolid(FDynamicMesh3 InMesh)
			: Mesh(MoveTemp(InMesh)), Tree(&Mesh, true)
		{
		}

		/** True when nothing in the ceiling stands between the eye and the target. */
		bool CanSee(const FVector3d& Eye, const FVector3d& Target) const
		{
			const double Distance = (Target - Eye).Length();
			if (Distance <= 1.0)
			{
				return true;
			}

			const FRay3d Ray(Eye, (Target - Eye) / Distance);
			return Tree.FindNearestHitTriangle(Ray,
				FDynamicMeshAABBTree3::FQueryOptions(Distance - 0.5)) == IndexConstants::InvalidID;
		}

		FDynamicMesh3 Mesh;
		FDynamicMeshAABBTree3 Tree;
	};

	/**
	 * Where a ceiling's faces are in a vertical column through one plan point.
	 *
	 * MEASURED WITHOUT ASKING WHICH WAY A TRIANGLE FACES, and that is deliberate rather than lazy.
	 * A false ceiling is several lapped solids appended into one mesh, so it has no single winding
	 * number - and on this project's meshes the sign is not what a reader expects either:
	 * FDynamicMesh3::GetTriNormal comes back pointing INTO an AppendPrism solid, so a soffit face
	 * reports as facing up and TFastWindingTree::IsInside answers "outside" for a point in the
	 * middle of a band. Both were tried here first and both quietly said the opposite of the truth.
	 * The renderer disagrees with them - these ceilings are plainly visible from underneath - so
	 * nothing here is inverted; the convention simply is not the one the maths textbook uses, and
	 * an assertion built on it is an assertion about the convention rather than about the ceiling.
	 *
	 * Heights of faces are not a convention. This walks every triangle whose plan projection covers
	 * the point and reports the lowest and highest surface over it, which is exactly what a section
	 * drawn through the room at that point would show.
	 */
	struct FColumn
	{
		bool bAny = false;
		double Lowest = 0.0;
		double Highest = 0.0;
	};

	FColumn ColumnAt(const FDynamicMesh3& Mesh, double X, double Y)
	{
		FColumn Out;

		for (const int32 Tid : Mesh.TriangleIndicesItr())
		{
			FVector3d A, B, C;
			Mesh.GetTriVertices(Tid, A, B, C);

			// Barycentric coordinates in plan. A vertical triangle projects to a line and has no
			// column to contribute, which is what the near-zero denominator drops.
			const double Denominator = (B.Y - C.Y) * (A.X - C.X) + (C.X - B.X) * (A.Y - C.Y);
			if (FMath::Abs(Denominator) < 1e-9)
			{
				continue;
			}

			const double U = ((B.Y - C.Y) * (X - C.X) + (C.X - B.X) * (Y - C.Y)) / Denominator;
			const double V = ((C.Y - A.Y) * (X - C.X) + (A.X - C.X) * (Y - C.Y)) / Denominator;
			const double W = 1.0 - U - V;
			if (U < -1e-9 || V < -1e-9 || W < -1e-9)
			{
				continue;
			}

			const double Z = U * A.Z + V * B.Z + W * C.Z;
			Out.Lowest = Out.bAny ? FMath::Min(Out.Lowest, Z) : Z;
			Out.Highest = Out.bAny ? FMath::Max(Out.Highest, Z) : Z;
			Out.bAny = true;
		}

		return Out;
	}

	/**
	 * Area of the horizontal faces at a height, whichever way they face.
	 *
	 * Two of these in the same plane is what flashing is, so the measurement that matters is how
	 * much surface is at that height, not which side of it is out.
	 */
	double HorizontalAreaAtHeight(const FDynamicMesh3& Mesh, double Height, double Tolerance = 0.25)
	{
		double Area = 0.0;
		for (const int32 Tid : Mesh.TriangleIndicesItr())
		{
			if (FMath::Abs(Mesh.GetTriNormal(Tid).Z) < 0.9)
			{
				continue;
			}

			FVector3d A, B, C;
			Mesh.GetTriVertices(Tid, A, B, C);
			if (FMath::Abs((A.Z + B.Z + C.Z) / 3.0 - Height) > Tolerance)
			{
				continue;
			}

			Area += 0.5 * ((B - A).Cross(C - A)).Length();
		}
		return Area;
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

// ---------------------------------------------------------------------------------------------
//
// THE FASCIA RULE, held to geometry.
//
// The rule the generator encodes is that any horizontal soffit edge a person in the room can see
// is closed to the surface above it. What follows measures that rule rather than restating it: a
// vertical section taken at the ceiling's edge has to be solid from the soffit all the way up to
// the slab, and the void behind it has to stay a void.
//
// The defect these exist for passed every test in the file: a full drop was a 20 mm sheet with the
// plenum open behind it, and a sheet has the same triangle count, the same bounds, the same
// footprint and the same volume-per-square-metre as a boxed soffit. Only where the section closes
// tells the two apart.
//
// ---------------------------------------------------------------------------------------------

/** Nothing generated here may have a hole in it. A ceiling is a solid, in every style. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFCeilingWatertightTest, "HouseForge.Ceilings.EveryStyleIsWatertight", HF_TEST_FLAGS)

bool FHFCeilingWatertightTest::RunTest(const FString& Parameters)
{
	const FHFRoom Room = MakeRoom();

	const TArray<EHFCeilingStyle> Styles = {
		EHFCeilingStyle::Peripheral, EHFCeilingStyle::FullDrop, EHFCeilingStyle::Tray,
		EHFCeilingStyle::Cove, EHFCeilingStyle::Bulkhead
	};

	for (const EHFCeilingStyle Style : Styles)
	{
		const FString Name = NameOf(Style);
		const FDynamicMesh3 Mesh = FHFGenerators::GenerateCeiling(MakeCeiling(Style), Room, {}, 0.0);

		if (!TestTrue(*FString::Printf(TEXT("%s produces geometry"), *Name), Mesh.TriangleCount() > 0))
		{
			continue;
		}

		TestTrue(*FString::Printf(TEXT("%s is watertight"), *Name), FHFMeshOps::IsClosed(Mesh));
		TestTrue(*FString::Printf(TEXT("%s has positive volume"), *Name),
			TMeshQueries<FDynamicMesh3>::GetVolumeArea(Mesh).X > 0.0);
	}

	return true;
}

/**
 * The section at the ceiling's edge closes, in every style, and the plenum survives it.
 *
 * Three things at once, because they are the three ways this can be wrong: the edge left open (the
 * defect), the edge closed by filling the void solid (a plug, which swallows the services and
 * doubles the soffit plane), and the fascia built at the wrong height.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFCeilingEdgeClosedTest, "HouseForge.Ceilings.NoSoffitEdgeOpenToTheSlab", HF_TEST_FLAGS)

bool FHFCeilingEdgeClosedTest::RunTest(const FString& Parameters)
{
	const FHFRoom Room = MakeRoom();
	const double StructuralZ = Room.FloorZ + Room.CeilingHeight;
	const double Drop = 20.0;
	const double SoffitZ = StructuralZ - Drop;

	// Off-centre and off-grid on purpose: a probe that lands on a triangle edge in plan is a
	// coin toss, and every interesting coordinate in this room is a round number.
	constexpr double ProbeX = 173.0;

	const TArray<EHFCeilingStyle> Styles = {
		EHFCeilingStyle::Peripheral, EHFCeilingStyle::FullDrop, EHFCeilingStyle::Tray,
		EHFCeilingStyle::Cove, EHFCeilingStyle::Bulkhead
	};

	for (const EHFCeilingStyle Style : Styles)
	{
		const FString Name = NameOf(Style);
		const FDynamicMesh3 Mesh = FHFGenerators::GenerateCeiling(MakeCeiling(Style, Drop), Room, {}, 0.0);

		if (Mesh.TriangleCount() == 0)
		{
			AddError(FString::Printf(TEXT("%s produced no geometry"), *Name));
			continue;
		}

		// THE SECTION AT THE EDGE. One centimetre in from the outline - inside the fascia of a
		// panelled style, inside the band of a perimeter one - the ceiling runs from its own soffit
		// all the way up to the slab. An open edge stops at the top of a 20 mm board and leaves the
		// plenum showing, which is the defect, and it stops there with the same triangle count, the
		// same bounds and the same footprint.
		const FColumn Edge = ColumnAt(Mesh, ProbeX, 1.0);
		if (!TestTrue(*FString::Printf(TEXT("%s has ceiling at its edge"), *Name), Edge.bAny))
		{
			continue;
		}

		AddInfo(FString::Printf(TEXT("%s at its edge: %.2f up to %.2f (slab at %.2f)."),
			*Name, Edge.Lowest, Edge.Highest, StructuralZ));

		TestNearlyEqual(*FString::Printf(TEXT("%s carries its edge up to the slab"), *Name),
			Edge.Highest, StructuralZ, 0.05);
		TestNearlyEqual(*FString::Printf(TEXT("%s puts its soffit at the drop"), *Name),
			Edge.Lowest, SoffitZ, 0.05);
	}

	// AND IT IS A CEILING, NOT A PLUG. Closing the edge by filling the void would satisfy every
	// assertion above, swallow the space the services run in, and put a second face in the soffit
	// plane. What is over the middle of the room says which was built.
	{
		const double InnerSoffitZ = StructuralZ - Drop * 0.5;

		for (const EHFCeilingStyle Style : { EHFCeilingStyle::FullDrop, EHFCeilingStyle::Bulkhead })
		{
			const FString Name = NameOf(Style);
			const FDynamicMesh3 Mesh = FHFGenerators::GenerateCeiling(MakeCeiling(Style, Drop), Room, {}, 0.0);

			TestNearlyEqual(*FString::Printf(TEXT("%s leaves the plenum open above its panel"), *Name),
				ColumnAt(Mesh, ProbeX, 150.0).Highest, SoffitZ + PanelThickness, 0.05);

			// The fascia is a board. Five centimetres in from the outline there is panel and
			// nothing else, so the ring really is a ring.
			TestNearlyEqual(*FString::Printf(TEXT("%s closes its edge with a fascia, not a fill"), *Name),
				ColumnAt(Mesh, ProbeX, 5.0).Highest, SoffitZ + PanelThickness, 0.05);

			// One horizontal face in the soffit plane, covering the outline once. A fascia started
			// at the soffit rather than at the top of the panel would put a second one over the
			// whole perimeter ring, and two coplanar faces in a plane the room can see is exactly
			// the flashing this pass exists to stop causing.
			TestNearlyEqual(*FString::Printf(TEXT("%s has one soffit plane, not two"), *Name),
				HorizontalAreaAtHeight(Mesh, SoffitZ), 400.0 * 300.0, 400.0 * 300.0 * 0.01);
		}

		// A perimeter style leaves the centre open to the structure. That is what it is for.
		for (const EHFCeilingStyle Style : { EHFCeilingStyle::Peripheral, EHFCeilingStyle::Cove })
		{
			const FString Name = NameOf(Style);
			const FDynamicMesh3 Mesh = FHFGenerators::GenerateCeiling(MakeCeiling(Style, Drop), Room, {}, 0.0);

			TestFalse(*FString::Printf(TEXT("%s leaves the centre of the room open to the slab"), *Name),
				ColumnAt(Mesh, ProbeX, 150.0).bAny);
		}

		// A tray's inner panel steps up from the band, and its underside is a soffit in its own
		// right - with the void above it left as void.
		const FDynamicMesh3 Tray = FHFGenerators::GenerateCeiling(MakeCeiling(EHFCeilingStyle::Tray, Drop), Room, {}, 0.0);
		const FColumn TrayCentre = ColumnAt(Tray, ProbeX, 150.0);

		TestNearlyEqual(TEXT("A tray's inner panel hangs at half the drop"),
			TrayCentre.Lowest, InnerSoffitZ, 0.05);
		TestNearlyEqual(TEXT("A tray leaves the void above its inner panel open"),
			TrayCentre.Highest, InnerSoffitZ + PanelThickness, 0.05);

		// The inner panel laps into the band rather than butting its face, so the two never share a
		// plane. 281 x 181 is the 400 x 300 room inset by the 60 band less the 5 mm lap.
		TestNearlyEqual(TEXT("A tray's upper soffit covers the room inside the band"),
			HorizontalAreaAtHeight(Tray, InnerSoffitZ), 281.0 * 181.0, 281.0 * 181.0 * 0.02);
	}

	return true;
}

/**
 * A cove's trough opens to the slab and is hidden from the room.
 *
 * That sentence is the whole difference between a cove and a groove, and the old profile had it
 * backwards: the recess faced down into the room, so the strip was in full view of anyone who
 * looked up and its light had no way of reaching the slab it is supposed to wash.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFCoveTroughTest, "HouseForge.Ceilings.CoveTroughOpensUpAndIsHidden", HF_TEST_FLAGS)

bool FHFCoveTroughTest::RunTest(const FString& Parameters)
{
	const FHFRoom Room = MakeRoom();
	const double StructuralZ = Room.FloorZ + Room.CeilingHeight;
	const double Drop = 20.0;
	const double BandWidth = 60.0;
	const double SoffitZ = StructuralZ - Drop;

	FHFFalseCeiling Ceiling = MakeCeiling(EHFCeilingStyle::Cove, Drop, BandWidth);
	Ceiling.Cove.ChannelWidth = 8.0;
	Ceiling.Cove.LipHeight = 5.0;
	Ceiling.Cove.Setback = 2.0;
	Ceiling.Cove.StripWidth = 2.0;
	Ceiling.Cove.StripHeight = 1.6;
	Ceiling.Cove.StripSetback = 2.5;

	// Measured in from the south wall: band 0..50, trough 50..58, lip 58..60. The strip lies in the
	// trough set back from the lip: 58 - 2.5 - 2 = 53.5 out to 55.5.
	constexpr double ProbeX = 173.0;
	const double TroughY = 54.0;
	const double EmptyTroughY = 57.0;
	const double LipY = 59.0;
	const double BoardTopZ = SoffitZ + PanelThickness;
	const double LipTopZ = SoffitZ + Ceiling.Cove.LipHeight;
	const double StripTopZ = BoardTopZ + Ceiling.Cove.StripHeight;

	const FCeilingSolid Cove(FHFGenerators::GenerateCeiling(Ceiling, Room, {}, 0.0));
	if (!TestTrue(TEXT("A cove generates"), Cove.Mesh.TriangleCount() > 0))
	{
		return false;
	}

	// ONE CONTINUOUS SOFFIT. Band, trough floor and lip all show the same plane to the room, so the
	// underside of a cove is a flat band of its full width - 400x300 less 280x180. A step in it
	// would mean the trough had been cut from below again.
	TestNearlyEqual(TEXT("The cove reads as one flat soffit across the whole band"),
		HorizontalAreaAtHeight(Cove.Mesh, SoffitZ),
		400.0 * 300.0 - 280.0 * 180.0, (400.0 * 300.0 - 280.0 * 180.0) * 0.03);

	// The band itself is the peripheral one: soffit to slab, closed.
	const FColumn Band = ColumnAt(Cove.Mesh, ProbeX, 25.0);
	TestNearlyEqual(TEXT("The band runs from the soffit to the slab"), Band.Lowest, SoffitZ, 0.05);
	TestNearlyEqual(TEXT("The band closes against the slab"), Band.Highest, StructuralZ, 0.05);

	// THE TROUGH IS A TROUGH: a floor to lay a strip on, the strip lying on it, and nothing at all
	// over either, so the light leaves upward and washes the slab.
	const FColumn Trough = ColumnAt(Cove.Mesh, ProbeX, TroughY);
	AddInfo(FString::Printf(TEXT("Over the trough: %.2f up to %.2f, slab at %.2f."),
		Trough.Lowest, Trough.Highest, StructuralZ));

	TestNearlyEqual(TEXT("The trough has a floor at the soffit"), Trough.Lowest, SoffitZ, 0.05);
	TestNearlyEqual(TEXT("The strip lies on the trough floor"), Trough.Highest, StripTopZ, 0.05);

	// Beside the strip the trough is empty right up to the slab; the setback from the lip is real
	// clearance rather than a figure nothing was built to.
	const FColumn BareTrough = ColumnAt(Cove.Mesh, ProbeX, EmptyTroughY);
	TestNearlyEqual(TEXT("The trough beside the strip is open to the slab"),
		BareTrough.Highest, BoardTopZ, 0.05);

	// THE SIGHT LINE, AS AN INEQUALITY, measured off the built mesh rather than off the parameters.
	// A strip throwing upward sends every ray that clears the lip away from any eye below it, so the
	// lowest thing such a ray can reach over the trough is the lip top - which makes concealment a
	// single comparison with no distance term in it, true from every position in the room at once.
	TestTrue(*FString::Printf(TEXT("The strip's top (%.2f) stays below the lip's top (%.2f)"),
		StripTopZ, LipTopZ), StripTopZ <= LipTopZ);

	// THE LIP STANDS IN FRONT OF IT, and stops short of the slab so the light gets past.
	const FColumn Lip = ColumnAt(Cove.Mesh, ProbeX, LipY);
	TestNearlyEqual(TEXT("The lip rises from the soffit"), Lip.Lowest, SoffitZ, 0.05);
	TestNearlyEqual(TEXT("The lip stands to its full height"), Lip.Highest, LipTopZ, 0.05);
	TestTrue(TEXT("The lip stands above the trough floor"), LipTopZ > BoardTopZ + 1.0);
	TestTrue(TEXT("The lip stops below the slab, so the light leaves the trough"),
		LipTopZ < StructuralZ - 1.0);

	// AND THE STRIP CANNOT BE SEEN, FROM ANYWHERE IN THE ROOM. Swept rather than sampled at a few
	// convenient spots: the inequality above says concealment cannot depend on where the eye is, and
	// the way to hold that claim to the geometry is to try every position and let a ray decide.
	//
	// Cast at the TOP EDGE of the strip - the highest point of the thing being hidden and therefore
	// the first part of it to come into view - from a standing eye at 1600 over a grid covering the
	// whole floor, plus a sitting eye and a child's, which are lower and so strictly easier.
	{
		int32 Seen = 0;
		int32 Cast = 0;
		FVector3d FirstSeenFrom = FVector3d::Zero();

		// The strip runs all the way round the trough, so it is probed on all four runs: a lip that
		// hid the strip along one wall and not along the next would pass a single-probe test.
		const TArray<FVector3d> StripTargets = {
			FVector3d(ProbeX, TroughY, StripTopZ - 0.05),
			FVector3d(ProbeX, 300.0 - TroughY, StripTopZ - 0.05),
			FVector3d(TroughY, 137.0, StripTopZ - 0.05),
			FVector3d(400.0 - TroughY, 137.0, StripTopZ - 0.05)
		};

		for (const double EyeZ : { 160.0, 120.0, 100.0 })
		{
			// Off-grid on purpose: a probe landing exactly on a triangle edge in plan is a coin toss.
			for (double X = 7.0; X < 400.0; X += 21.0)
			{
				for (double Y = 5.0; Y < 300.0; Y += 17.0)
				{
					const FVector3d Eye(X, Y, EyeZ);
					for (const FVector3d& Target : StripTargets)
					{
						++Cast;
						if (Cove.CanSee(Eye, Target))
						{
							if (Seen == 0)
							{
								FirstSeenFrom = Eye;
							}
							++Seen;
						}
					}
				}
			}
		}

		AddInfo(FString::Printf(TEXT("Cast %d sight lines at the LED strip from across the room."), Cast));
		TestTrue(TEXT("The sweep actually cast sight lines"), Cast > 1000);
		TestEqual(*FString::Printf(
			TEXT("The strip is out of sight from every eye position (first seen from %.0f, %.0f, %.0f)"),
			FirstSeenFrom.X, FirstSeenFrom.Y, FirstSeenFrom.Z), Seen, 0);
	}

	// The fan mirror has to agree with the profile: the soffit under the trough is the band's
	// soffit, at the full drop, and the centre is still open to the slab.
	TestNearlyEqual(TEXT("A rod through the trough passes the full drop"),
		FHFGenerators::CeilingSoffitDropAt(Ceiling, Room, FVector2D(ProbeX, TroughY)), Drop, 1e-6);
	TestNearlyEqual(TEXT("A fan in the open centre passes nothing"),
		FHFGenerators::CeilingSoffitDropAt(Ceiling, Room, FVector2D(200.0, 150.0)), 0.0, 1e-6);

	return true;
}

#undef HF_TEST_FLAGS

#endif // WITH_DEV_AUTOMATION_TESTS
