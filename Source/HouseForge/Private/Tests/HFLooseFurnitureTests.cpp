// Copyright Siddartha G. All Rights Reserved.

#include "HouseForge.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Actors/HFLooseFurnitureActors.h"
#include "Geometry/HFFrameKit.h"
#include "Geometry/HFMeshOps.h"
#include "Geometry/HFRenderFinish.h"
#include "Geometry/HFUpholsteryKit.h"
#include "MeshQueries.h"
#include "Misc/AutomationTest.h"
#include "Model/HFSampleHouse.h"
#include "Model/HFTypes.h"

using namespace UE::Geometry;

#define HF_TEST_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// ---------------------------------------------------------------------------------------------
//
// The loose furniture group: the soft-box primitive it is all built from, the sofa, the two tables
// and the dining chairs, and the one thing about them that only the ROOM can answer - whether a
// chair can be pulled out.
//
// Measured on volume, bounds, roles, radii and clearances in centimetres, and never on a triangle
// count. A count changes the moment a radius gains a step and says nothing about whether the
// geometry is right; see .claude/rules/04-conventions.md.
//
// Nothing in this group articulates, so there is no swept transform to assert. What replaces it is
// the clearance test at the bottom of this file: "a chair pulled out must not foul anything" is the
// motion this group actually has, and it is a fact about the flat rather than about a mesh.
//
// ---------------------------------------------------------------------------------------------

namespace
{
	double Volume(const FDynamicMesh3& Mesh)
	{
		return TMeshQueries<FDynamicMesh3>::GetVolumeArea(Mesh).X;
	}

	/** True when every triangle in the mesh carries a polygroup that maps back to a surface role. */
	bool EveryTriangleHasARole(const FDynamicMesh3& Mesh)
	{
		if (Mesh.TriangleCount() == 0)
		{
			return false;
		}

		for (const int32 Tri : Mesh.TriangleIndicesItr())
		{
			const int32 Group = Mesh.GetTriangleGroup(Tri);
			if (Group <= 0 || Group > FHFMeshOps::NumSurfaceRoles())
			{
				return false;
			}
		}
		return true;
	}

	/**
	 * The sharpest dihedral angle anywhere in the mesh, in degrees.
	 *
	 * THE MEASUREMENT THAT SAYS A SOFT FORM IS SOFT. A rounded box and a chamfered box are not
	 * distinguishable by volume, by bounds or by any silhouette a test can take - but they are
	 * completely distinguishable by this: a chamfer meets its parent face at 45 degrees and a real
	 * radius never turns more than one step at a time. Below FHFBevelParams::MinAngleDegrees every
	 * edge welds smooth under ComputeShadingNormals, which is exactly the property upholstery needs
	 * and the reason the bevel pass correctly does nothing to it.
	 */
	double SharpestEdgeDegrees(const FDynamicMesh3& Mesh)
	{
		double Worst = 0.0;

		for (const int32 Edge : Mesh.EdgeIndicesItr())
		{
			const FIndex2i Tris = Mesh.GetEdgeT(Edge);
			if (Tris.B == FDynamicMesh3::InvalidID)
			{
				continue;
			}

			const FVector3d A = Mesh.GetTriNormal(Tris.A);
			const FVector3d B = Mesh.GetTriNormal(Tris.B);
			if (!A.IsNormalized() || !B.IsNormalized())
			{
				continue;
			}

			const double Degrees = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(A.Dot(B), -1.0, 1.0)));
			Worst = FMath::Max(Worst, Degrees);
		}

		return Worst;
	}

	/**
	 * Bounds of only the geometry lying in a Z band - how a lean is measured without a transform.
	 *
	 * The band has to be chosen against the mesh rather than against the object, and that caught this
	 * file out once already: a soft box is LOFTED, so its only vertices are on the rolled levels at
	 * the very top and bottom. A band taken across the middle of one - which is where you would look
	 * for the middle of a cushion - is completely empty, and a test measuring it silently measures
	 * nothing. Bands here are therefore anchored to the ends.
	 */
	FBox BoundsInZBand(const FDynamicMesh3& Mesh, double Z0, double Z1)
	{
		FBox Out(ForceInit);

		for (const int32 Vertex : Mesh.VertexIndicesItr())
		{
			const FVector3d P = Mesh.GetVertex(Vertex);
			if (P.Z >= Z0 && P.Z <= Z1)
			{
				Out += FVector(P);
			}
		}

		return Out;
	}

	/** How far the front face travels back between the bottom of a lofted solid and its top. */
	double LeanOf(const FDynamicMesh3& Mesh, double BandFraction = 0.1)
	{
		const FAxisAlignedBox3d Bounds = Mesh.GetBounds();
		const double Band = Bounds.Height() * BandFraction;

		const FBox Low = BoundsInZBand(Mesh, Bounds.Min.Z, Bounds.Min.Z + Band);
		const FBox High = BoundsInZBand(Mesh, Bounds.Max.Z - Band, Bounds.Max.Z);

		if (Low.IsValid == 0 || High.IsValid == 0)
		{
			return 0.0;
		}

		return High.Min.Y - Low.Min.Y;
	}

	FHFFixture MakeFixture(const TCHAR* Id, EHFFixtureType Type, const FVector2D& Footprint, double Height)
	{
		FHFFixture F;
		F.Id = Id;
		F.RoomId = TEXT("R_Test");
		F.Type = Type;
		F.Footprint = Footprint;
		F.Height = Height;
		return F;
	}

	/** The three-seater the reference flat draws, in centimetres. */
	FHFSofaParams ReferenceSofa()
	{
		return AHFSofaActor::ParamsFor(
			MakeFixture(TEXT("F_Sofa"), EHFFixtureType::Sofa, FVector2D(210.0, 90.0), 80.0));
	}

	FHFTableParams ReferenceDiningTable()
	{
		return AHFTableActor::ParamsFor(
			MakeFixture(TEXT("F_DiningTable"), EHFFixtureType::DiningTable, FVector2D(140.0, 80.0), 75.0));
	}

	FHFTableParams ReferenceCoffeeTable()
	{
		return AHFTableActor::ParamsFor(
			MakeFixture(TEXT("F_CoffeeTable"), EHFFixtureType::CoffeeTable, FVector2D(110.0, 60.0), 40.0));
	}

	FHFChairParams ReferenceChair()
	{
		return AHFChairActor::ParamsFor(
			MakeFixture(TEXT("F_Chair"), EHFFixtureType::Chair, FVector2D(45.0, 48.0), 85.0));
	}
}

// =============================================================================== the soft box

/**
 * A soft box is closed, positive, exactly the size it was asked for, and SMOOTH.
 *
 * The last of those is the whole point of the primitive existing. FHFBevelParams gives Fabric a
 * chamfer width of zero by design, so a cushion that leaned on BevelConvexEdges would come out with
 * perfectly sharp arrises; this asserts that it does not need to.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFSoftBoxTest, "HouseForge.Geometry.SoftBox", HF_TEST_FLAGS)

bool FHFSoftBoxTest::RunTest(const FString& Parameters)
{
	const FVector3d Min(0.0, 0.0, 0.0);
	const FVector3d Max(60.0, 60.0, 14.0);

	// The plan radius carries the top roll, which is what makes the corner a sphere octant rather
	// than a flat lozenge - see FHFSoftBoxParams::CornerRadius.
	FHFSoftBoxParams Soft;
	Soft.CornerRadius = 5.0;
	Soft.TopRadius = 5.0;
	Soft.BottomRadius = 2.0;
	Soft.RollSteps = 4;

	FDynamicMesh3 Mesh;
	FHFMeshOps::InitialiseMesh(Mesh);
	TestTrue(TEXT("A soft box builds"),
		FHFMeshOps::AppendSoftBox(Mesh, Min, Max, Soft, EHFSurfaceRole::Fabric));

	TestTrue(TEXT("A soft box is watertight"), FHFMeshOps::IsClosed(Mesh));
	TestTrue(TEXT("A soft box faces outward"), Volume(Mesh) > 0.0);
	TestTrue(TEXT("Every triangle carries a surface role"), EveryTriangleHasARole(Mesh));

	// BOUNDS ARE EXACTLY THE BOX ASKED FOR, never larger. Every radius is measured inward, which is
	// what lets a fixture's footprint still be asserted against the drawing after it is rounded.
	const FAxisAlignedBox3d Bounds = Mesh.GetBounds();
	TestTrue(TEXT("A soft box fills its declared box exactly"),
		Bounds.Min.Equals(Min, 0.01) && Bounds.Max.Equals(Max, 0.01));

	// Rounded, not chamfered, and not a sphere either: it has lost the corners and kept the box.
	const double BoxVolume = (Max.X - Min.X) * (Max.Y - Min.Y) * (Max.Z - Min.Z);
	TestTrue(TEXT("The radii actually take material off the box"), Volume(Mesh) < BoxVolume * 0.995);
	TestTrue(TEXT("A soft box is still a box"), Volume(Mesh) > BoxVolume * 0.85);

	// THE ASSERTION THE WHOLE PRIMITIVE EXISTS FOR. Below the bevel threshold everywhere, so
	// ComputeShadingNormals welds it smooth and the chamfer pass has nothing to do to it.
	const FHFBevelParams Bevel;
	TestTrue(FString::Printf(TEXT("No edge on a soft box is sharp enough to need a chamfer (%.1f deg)"),
		SharpestEdgeDegrees(Mesh)), SharpestEdgeDegrees(Mesh) < Bevel.MinAngleDegrees);

	// THE CORNER IS A CONTINUOUS SURFACE AND NOT A FLAT LOZENGE, which is the one thing about this
	// primitive that a volume or a bounds check cannot see and that was plainly visible the first
	// time the sofa was rendered. Held constant while the ring drew in, the plan radius fails to meet
	// the roll and leaves a facet with its own highlight on every corner of every cushion.
	//
	// Measured as the plan radius surviving at the top ring: it has to have closed to nothing by the
	// time the roll has finished turning, or the two radii never met.
	{
		// Where the top cap's own corner would be if its plan radius had closed to nothing.
		const FVector2D CapCorner(Min.X + Soft.TopRadius, Min.Y + Soft.TopRadius);

		double NearestOnCap = TNumericLimits<double>::Max();
		double NearestAtMidHeight = TNumericLimits<double>::Max();

		for (const int32 Vertex : Mesh.VertexIndicesItr())
		{
			const FVector3d Point = Mesh.GetVertex(Vertex);
			const double ToCorner = FVector2D::Distance(FVector2D(Point.X, Point.Y), CapCorner);

			if (Point.Z >= Max.Z - 0.01)
			{
				NearestOnCap = FMath::Min(NearestOnCap, ToCorner);
			}
			else if (FMath::IsNearlyEqual(Point.Z, Min.Z + Soft.BottomRadius, 0.01))
			{
				// The widest ring, where the plan radius is at its full figure.
				NearestAtMidHeight = FMath::Min(NearestAtMidHeight,
					FVector2D::Distance(FVector2D(Point.X, Point.Y), FVector2D(Min.X, Min.Y)));
			}
		}

		// With CornerRadius equal to TopRadius the plan radius has closed almost to nothing exactly
		// where the roll finishes. Almost, rather than exactly: a corner allowed to converge on a
		// point stacks coincident vertices and creases - see AppendSoftBox's floor.
		TestTrue(FString::Printf(TEXT("The plan radius closes as the roll turns (%.2f cm from the cap corner)"),
			NearestOnCap), NearestOnCap < Soft.CornerRadius * 0.25);

		// And at full width it has NOT: the widest ring keeps its corner radius, so nothing there gets
		// anywhere near the box's own corner. This is the half that fails if the blend is removed.
		TestTrue(FString::Printf(TEXT("The widest ring is still rounded in plan (%.2f cm)"),
			NearestAtMidHeight), NearestAtMidHeight > Soft.CornerRadius * 0.3);
	}

	// A radius bigger than the box can carry is clamped rather than turning the solid inside out.
	{
		FHFSoftBoxParams Absurd;
		Absurd.CornerRadius = 500.0;
		Absurd.TopRadius = 500.0;
		Absurd.BottomRadius = 500.0;

		FDynamicMesh3 Clamped;
		FHFMeshOps::InitialiseMesh(Clamped);
		FHFMeshOps::AppendSoftBox(Clamped, Min, Max, Absurd, EHFSurfaceRole::Fabric);

		TestTrue(TEXT("An absurd radius still builds a closed solid"), FHFMeshOps::IsClosed(Clamped));
		TestTrue(TEXT("An absurd radius still has positive volume"), Volume(Clamped) > 0.0);
		TestTrue(TEXT("An absurd radius still stays inside the box"),
			Clamped.GetBounds().Max.Z <= Max.Z + 0.01 && Clamped.GetBounds().Min.Z >= Min.Z - 0.01);
	}

	// Degenerate input appends nothing rather than a sliver, like every other primitive here.
	{
		FDynamicMesh3 Flat;
		FHFMeshOps::InitialiseMesh(Flat);
		TestFalse(TEXT("A zero-height soft box is refused"),
			FHFMeshOps::AppendSoftBox(Flat, Min, FVector3d(60.0, 60.0, 0.0), Soft, EHFSurfaceRole::Fabric));
		TestEqual(TEXT("A refused soft box leaves nothing behind"), Flat.TriangleCount(), 0);
	}

	return true;
}

/**
 * A rake leans the box back over its own height WITHOUT growing out of its declared depth.
 *
 * Both halves matter. The lean is what makes a back cushion read as seating rather than as a slab;
 * staying inside the box is what lets the sofa's footprint still be checked against the drawing.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFSoftBoxRakeTest, "HouseForge.Geometry.SoftBoxRake", HF_TEST_FLAGS)

bool FHFSoftBoxRakeTest::RunTest(const FString& Parameters)
{
	const FVector3d Min(0.0, 0.0, 0.0);
	const FVector3d Max(50.0, 20.0, 40.0);
	constexpr double Rake = 6.0;

	FHFSoftBoxParams Soft;
	Soft.CornerRadius = 2.0;
	Soft.TopRadius = 2.0;
	Soft.BottomRadius = 2.0;
	Soft.RakeY = Rake;

	FDynamicMesh3 Mesh;
	FHFMeshOps::InitialiseMesh(Mesh);
	TestTrue(TEXT("A raked soft box builds"),
		FHFMeshOps::AppendSoftBox(Mesh, Min, Max, Soft, EHFSurfaceRole::Fabric));

	TestTrue(TEXT("A raked soft box is watertight"), FHFMeshOps::IsClosed(Mesh));
	TestTrue(TEXT("A raked soft box faces outward"), Volume(Mesh) > 0.0);

	// NEVER OUTSIDE THE DECLARED BOX. A lean that grew the footprint would put every raked cushion
	// and every chair back through the object behind it, and nothing measuring the drawing would say.
	const FAxisAlignedBox3d Bounds = Mesh.GetBounds();
	TestTrue(TEXT("A raked box stays inside the box it was declared with"),
		Bounds.Min.Y >= Min.Y - 0.01 && Bounds.Max.Y <= Max.Y + 0.01);

	// And it genuinely leans: measured as the travel of the front face between the bottom and the
	// top, in centimetres, rather than as "the vertices are not all in one plane".
	const double Travel = LeanOf(Mesh);
	TestTrue(FString::Printf(TEXT("The top of a raked box leans back by the rake (%.2f of %.2f cm)"),
		Travel, Rake), Travel > Rake * 0.55 && Travel < Rake * 1.05);

	// A box with no rake does not lean, which is what makes the figure above mean something.
	{
		FHFSoftBoxParams Upright = Soft;
		Upright.RakeY = 0.0;

		FDynamicMesh3 Straight;
		FHFMeshOps::InitialiseMesh(Straight);
		FHFMeshOps::AppendSoftBox(Straight, Min, Max, Upright, EHFSurfaceRole::Fabric);

		TestTrue(TEXT("An unraked box does not lean"), FMath::Abs(LeanOf(Straight)) < 0.01);
	}

	return true;
}

// =================================================================================== the sofa

/** The reference three-seater: closed, positive, exactly the drawn box, and in two materials. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFSofaTest, "HouseForge.Upholstery.Sofa", HF_TEST_FLAGS)

bool FHFSofaTest::RunTest(const FString& Parameters)
{
	const FHFSofaParams P = ReferenceSofa();
	const FHFSofaBuild Built = FHFUpholsteryKit::BuildSofa(P);

	if (!TestTrue(TEXT("The reference sofa builds"), Built.bValid))
	{
		return false;
	}

	TestTrue(TEXT("A sofa is watertight"), FHFMeshOps::IsClosed(Built.Shell));
	TestTrue(TEXT("A sofa faces outward"), Volume(Built.Shell) > 0.0);
	TestTrue(TEXT("Every triangle carries a surface role"), EveryTriangleHasARole(Built.Shell));

	// BOUNDS ARE THE DRAWN BOX. A sofa that grew past its footprint would go through the wall it
	// stands against, and the drawing would still say it fitted.
	const FAxisAlignedBox3d Bounds = Built.Shell.GetBounds();
	TestTrue(TEXT("A sofa fills its drawn width"),
		FMath::IsNearlyEqual(Bounds.Min.X, 0.0, 0.05) && FMath::IsNearlyEqual(Bounds.Max.X, 210.0, 0.05));
	TestTrue(TEXT("A sofa fills its drawn depth"),
		FMath::IsNearlyEqual(Bounds.Min.Y, 0.0, 0.05) && FMath::IsNearlyEqual(Bounds.Max.Y, 90.0, 0.05));
	TestTrue(TEXT("A sofa stands exactly as tall as it was drawn"),
		FMath::IsNearlyEqual(Bounds.Min.Z, 0.0, 0.05) && FMath::IsNearlyEqual(Bounds.Max.Z, 80.0, 0.05));

	// Two materials, and the hard one is the legs. A sofa in one material is the crate the kit exists
	// to avoid; see FHFUpholsteryKit.
	const TSet<EHFSurfaceRole> Roles = FHFMeshOps::RolesPresent(Built.Shell);
	TestTrue(TEXT("The upholstery is Fabric"), Roles.Contains(EHFSurfaceRole::Fabric));
	TestTrue(TEXT("The legs are timber"), Roles.Contains(EHFSurfaceRole::JoineryCarcass));

	// THREE SEATS, DERIVED AND NOT ASSUMED. 2100 less two 180 arms is 1740 of clear width, which is
	// three 560 cushions and their gaps.
	TestEqual(TEXT("A 2100 sofa comes out as a three-seater"), Built.Used.SeatCount, 3);
	TestEqual(TEXT("One seat cushion per seat"), Built.SeatCushions.Num(), 3);
	TestEqual(TEXT("One back cushion per seat"), Built.BackCushions.Num(), 3);
	TestEqual(TEXT("Two arms"), Built.Arms.Num(), 2);

	// The figures somebody actually sits on, in centimetres.
	TestTrue(TEXT("A seat cushion is a sofa's seat, not a bench pad"),
		Built.Used.SeatCushionDepth() > 50.0);
	TestTrue(TEXT("A seat cushion is the width of a seat"),
		Built.Used.SeatCushionWidth() > 50.0 && Built.Used.SeatCushionWidth() < 65.0);

	return true;
}

/**
 * The clearances between the forms, which is what stops a sofa being one slab.
 *
 * Every one of these is a gap BETWEEN two sub-assemblies, and that is why FHFSofaBuild keeps them:
 * merged into the shell, not one of these questions has an answer.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFSofaFormsTest, "HouseForge.Upholstery.SofaForms", HF_TEST_FLAGS)

bool FHFSofaFormsTest::RunTest(const FString& Parameters)
{
	const FHFSofaParams P = ReferenceSofa();
	const FHFSofaBuild Built = FHFUpholsteryKit::BuildSofa(P);

	if (!TestTrue(TEXT("The reference sofa builds"), Built.bValid))
	{
		return false;
	}

	// ------------------------------------------------------------------- the seat is at seat height
	for (int32 Seat = 0; Seat < Built.SeatCushions.Num(); ++Seat)
	{
		const FAxisAlignedBox3d Cushion = Built.SeatCushions[Seat].GetBounds();

		TestTrue(FString::Printf(TEXT("Seat cushion %d tops out at the seat height"), Seat),
			FMath::IsNearlyEqual(Cushion.Max.Z, Built.Used.SeatHeight, 0.05));
		TestTrue(FString::Printf(TEXT("Seat cushion %d sits on the deck"), Seat),
			FMath::IsNearlyEqual(Cushion.Min.Z, Built.Used.DeckZ(), 0.05));
	}

	// ------------------------------------------------------- the gaps that make three read as three
	//
	// Measured between one cushion's bounds and the next, in centimetres. Without them a three-seater
	// is one continuous 1740 mm slab and no radius on its edges changes that.
	for (int32 Seat = 1; Seat < Built.SeatCushions.Num(); ++Seat)
	{
		const double Gap = Built.SeatCushions[Seat].GetBounds().Min.X
			- Built.SeatCushions[Seat - 1].GetBounds().Max.X;

		TestTrue(FString::Printf(TEXT("There is a shadow gap between cushions %d and %d"), Seat - 1, Seat),
			Gap > Built.Used.CushionGap * 0.5);
	}

	// And the outer cushions do not touch the arms either.
	{
		const double LeftGap = Built.SeatCushions[0].GetBounds().Min.X - Built.Arms[0].GetBounds().Max.X;
		const double RightGap = Built.Arms[1].GetBounds().Min.X
			- Built.SeatCushions.Last().GetBounds().Max.X;

		TestTrue(TEXT("The left cushion clears the left arm"), LeftGap > 0.0);
		TestTrue(TEXT("The right cushion clears the right arm"), RightGap > 0.0);
	}

	// ------------------------------------------------------------------ the arms oversail the base
	//
	// The silhouette of a sofa is its arms. Built flush with the base, the whole piece is one slab
	// from the floor to the seat and the arms stop being separately legible from a metre away.
	{
		const FAxisAlignedBox3d Base = Built.Base.GetBounds();
		const FAxisAlignedBox3d LeftArm = Built.Arms[0].GetBounds();

		TestTrue(TEXT("The arm stands proud of the base at the side"), LeftArm.Min.X < Base.Min.X - 0.5);
		TestTrue(TEXT("The arm stands proud of the base at the front"), LeftArm.Min.Y < Base.Min.Y - 0.5);
		TestTrue(TEXT("The arm is above the seat and below the back"),
			LeftArm.Max.Z > Built.Used.SeatHeight && LeftArm.Max.Z < Built.Used.Height);
	}

	// ----------------------------------------------------------------- the back cushions LEAN back
	//
	// The single thing that decides whether the sofa reads as seating. Measured as the travel of the
	// cushion's own front face between its bottom and its top, in centimetres.
	{
		const double Lean = LeanOf(Built.BackCushions[1]);
		TestTrue(FString::Printf(TEXT("The back cushion leans back (%.2f cm of %.2f)"),
			Lean, Built.Used.BackRake), Lean > Built.Used.BackRake * 0.5);

		// The seat cushion does not, which is what makes the figure above mean something.
		TestTrue(TEXT("The seat cushion does not lean"), FMath::Abs(LeanOf(Built.SeatCushions[1])) < 0.01);
	}

	// And the seat cushion is in front of the back cushion rather than under it.
	{
		const double Gap = Built.BackCushions[1].GetBounds().Min.Y
			- Built.SeatCushions[1].GetBounds().Max.Y;
		TestTrue(TEXT("The seat cushion clears the back cushion"), Gap > 0.0);
	}

	// -------------------------------------------------------------------- and light gets underneath
	//
	// A 120 mm gap of daylight under a sofa is most of what separates loose furniture from joinery.
	{
		const FAxisAlignedBox3d Base = Built.Base.GetBounds();
		TestTrue(TEXT("The base is lifted clear of the floor on legs"),
			Base.Min.Z > Built.Used.LegHeight - 0.05);
		TestTrue(TEXT("The legs reach the floor"),
			FMath::IsNearlyEqual(Built.Legs.GetBounds().Min.Z, 0.0, 0.05));
	}

	return true;
}

/** Parameters change the sofa, and a sofa that cannot be built comes back empty rather than wrong. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFSofaParametersTest, "HouseForge.Upholstery.SofaParameters", HF_TEST_FLAGS)

bool FHFSofaParametersTest::RunTest(const FString& Parameters)
{
	const FHFSofaBuild Three = FHFUpholsteryKit::BuildSofa(ReferenceSofa());

	// A two-seater is not a three-seater with the same geometry.
	{
		const FHFSofaParams Two = AHFSofaActor::ParamsFor(
			MakeFixture(TEXT("F_Sofa2"), EHFFixtureType::Sofa, FVector2D(150.0, 90.0), 80.0));
		const FHFSofaBuild Built = FHFUpholsteryKit::BuildSofa(Two);

		TestEqual(TEXT("A 1500 sofa comes out as a two-seater"), Built.Used.SeatCount, 2);
		TestEqual(TEXT("A two-seater has two seat cushions"), Built.SeatCushions.Num(), 2);
		TestTrue(TEXT("A two-seater is narrower"),
			Built.Shell.GetBounds().Max.X < Three.Shell.GetBounds().Max.X - 1.0);
	}

	// Seat height moves the cushions and nothing else has to be told.
	{
		FHFSofaParams Low = ReferenceSofa();
		Low.SeatHeight = 38.0;
		const FHFSofaBuild Built = FHFUpholsteryKit::BuildSofa(Low);

		TestTrue(TEXT("A lower seat lowers the cushion"),
			Built.SeatCushions[0].GetBounds().Max.Z
				< Three.SeatCushions[0].GetBounds().Max.Z - 1.0);
	}

	// A deeper arm eats the clear width, and the cushions give it up.
	{
		FHFSofaParams Wide = ReferenceSofa();
		Wide.ArmWidth = 30.0;
		const FHFSofaBuild Built = FHFUpholsteryKit::BuildSofa(Wide);

		TestTrue(TEXT("A wider arm narrows the cushions"),
			Built.Used.SeatCushionWidth() < Three.Used.SeatCushionWidth() - 1.0);
	}

	// Degenerate input: nothing, rather than a sliver that carries through every later measurement.
	{
		FHFSofaParams Nothing = ReferenceSofa();
		Nothing.Width = 0.0;
		const FHFSofaBuild Built = FHFUpholsteryKit::BuildSofa(Nothing);

		TestFalse(TEXT("A sofa with no width is refused"), Built.bValid);
		TestEqual(TEXT("A refused sofa leaves no geometry"), Built.Shell.TriangleCount(), 0);
	}

	{
		FHFSofaParams Flat = ReferenceSofa();
		Flat.Depth = 0.0;
		const FHFSofaBuild Built = FHFUpholsteryKit::BuildSofa(Flat);

		TestFalse(TEXT("A sofa with no depth is refused"), Built.bValid);
		TestEqual(TEXT("A refused sofa leaves no geometry"), Built.Shell.TriangleCount(), 0);
	}

	// AND A UNITS MISTAKE IS CLAMPED RATHER THAN REFUSED, which is the documented policy here and is
	// worth an assertion because it is the difference between "no sofa" and "a sofa nobody can see".
	// A drawing that gave a three-seater 50 mm of height has not asked for nothing; the honest answer
	// is the sofa that fits inside what it asked for - closed, positive, and never a sliver.
	{
		FHFSofaParams Squashed = ReferenceSofa();
		Squashed.Height = 5.0;
		const FHFSofaBuild Built = FHFUpholsteryKit::BuildSofa(Squashed);

		TestTrue(TEXT("A 50 mm sofa is clamped into something buildable"), Built.bValid);
		TestTrue(TEXT("A clamped sofa is still watertight"), FHFMeshOps::IsClosed(Built.Shell));
		TestTrue(TEXT("A clamped sofa still faces outward"), Volume(Built.Shell) > 0.0);
		TestTrue(TEXT("A clamped sofa does not grow past what it was asked for"),
			Built.Shell.GetBounds().Max.Z <= 5.05);
	}

	return true;
}

// ================================================================================= the tables

/** The dining table: closed, positive, exactly the drawn box, and with knees under it. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFDiningTableTest, "HouseForge.Frame.DiningTable", HF_TEST_FLAGS)

bool FHFDiningTableTest::RunTest(const FString& Parameters)
{
	const FHFTableParams P = ReferenceDiningTable();
	const FHFTableBuild Built = FHFFrameKit::BuildTable(P);

	if (!TestTrue(TEXT("The reference dining table builds"), Built.bValid))
	{
		return false;
	}

	TestTrue(TEXT("A table is watertight"), FHFMeshOps::IsClosed(Built.Shell));
	TestTrue(TEXT("A table faces outward"), Volume(Built.Shell) > 0.0);
	TestTrue(TEXT("Every triangle carries a surface role"), EveryTriangleHasARole(Built.Shell));

	// Origin at the centre of the footprint - the datum FreeStanding places loose furniture by.
	const FAxisAlignedBox3d Bounds = Built.Shell.GetBounds();
	TestTrue(TEXT("The table fills its drawn footprint about its own centre"),
		Bounds.Min.Equals(FVector3d(-70.0, -40.0, 0.0), 0.05)
		&& Bounds.Max.Equals(FVector3d(70.0, 40.0, 75.0), 0.05));

	// TWO MATERIALS, and the top is the one anybody looks at.
	const TSet<EHFSurfaceRole> Roles = FHFMeshOps::RolesPresent(Built.Shell);
	TestTrue(TEXT("The top is faced"), Roles.Contains(EHFSurfaceRole::ShutterLaminate));
	TestTrue(TEXT("The frame is timber"), Roles.Contains(EHFSurfaceRole::JoineryCarcass));

	// THE MEASUREMENT THAT DECIDES WHETHER ANYBODY CAN SIT AT IT, and the one figure a drawing never
	// states. 650 is what a knee needs; a 100 mm rail under a 30 mm top would leave 620.
	TestTrue(FString::Printf(TEXT("There is knee room under the apron (%.1f cm)"), Built.Used.KneeClearance()),
		Built.Used.KneeClearance() >= 65.0);

	// Four legs, and they reach the floor.
	TestTrue(TEXT("The legs reach the floor"),
		FMath::IsNearlyEqual(Built.Legs.GetBounds().Min.Z, 0.0, 0.05));
	TestTrue(TEXT("The legs run up into the top"),
		Built.Legs.GetBounds().Max.Z > Built.Used.TopUnderZ());

	// Roughly four legs' worth of timber, not one post or eight. Measured as volume rather than as a
	// count of anything, so a rounded arris does not change the answer.
	const double LegVolume = Built.Used.LegSection * Built.Used.LegSection * Built.Used.TopUnderZ();
	TestTrue(TEXT("There are four legs' worth of leg"),
		Volume(Built.Legs) > LegVolume * 3.5 && Volume(Built.Legs) < LegVolume * 4.6);

	// A dining table has nothing between its legs. See the coffee table, which does.
	TestEqual(TEXT("A dining table has no lower shelf"), Built.Shelf.TriangleCount(), 0);

	// The top's edges are rolled rather than left sharp: at this scale a chamfer is a hard line with
	// a highlight on it, and a table edge is what a hand runs along.
	TestTrue(TEXT("The top has a rolled edge"), Built.Used.EdgeRoll > 0.0);

	return true;
}

/** A coffee table is the same object at another size, plus the shelf that makes it one. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFCoffeeTableTest, "HouseForge.Frame.CoffeeTable", HF_TEST_FLAGS)

bool FHFCoffeeTableTest::RunTest(const FString& Parameters)
{
	const FHFTableBuild Built = FHFFrameKit::BuildTable(ReferenceCoffeeTable());

	if (!TestTrue(TEXT("The reference coffee table builds"), Built.bValid))
	{
		return false;
	}

	TestTrue(TEXT("A coffee table is watertight"), FHFMeshOps::IsClosed(Built.Shell));
	TestTrue(TEXT("A coffee table faces outward"), Volume(Built.Shell) > 0.0);
	TestTrue(TEXT("Every triangle carries a surface role"), EveryTriangleHasARole(Built.Shell));

	const FAxisAlignedBox3d Bounds = Built.Shell.GetBounds();
	TestTrue(TEXT("The coffee table fills its drawn footprint"),
		Bounds.Min.Equals(FVector3d(-55.0, -30.0, 0.0), 0.05)
		&& Bounds.Max.Equals(FVector3d(55.0, 30.0, 40.0), 0.05));

	// THE SHELF IS THE DIFFERENCE BETWEEN THE TWO OBJECTS. Without it a coffee table is a low dining
	// table, and the flat already has a dining table.
	TestTrue(TEXT("A coffee table has a lower shelf"), Built.Shelf.TriangleCount() > 0);
	TestTrue(TEXT("The shelf is below the apron and above the floor"),
		Built.Shelf.GetBounds().Max.Z < Built.Used.KneeClearance()
		&& Built.Shelf.GetBounds().Min.Z > 0.0);

	// Lighter in every member than the dining table, which is what 400 tall needs.
	const FHFTableParams Dining = ReferenceDiningTable();
	TestTrue(TEXT("A coffee table has a lighter leg than a dining table"),
		Built.Used.LegSection < Dining.LegSection);

	// Degenerate input.
	{
		FHFTableParams Nothing = ReferenceCoffeeTable();
		Nothing.Width = 0.0;
		const FHFTableBuild Empty = FHFFrameKit::BuildTable(Nothing);

		TestFalse(TEXT("A table with no width is refused"), Empty.bValid);
		TestEqual(TEXT("A refused table leaves no geometry"), Empty.Shell.TriangleCount(), 0);
	}

	return true;
}

// ================================================================================== the chair

/** A dining chair: closed, positive, exactly the drawn box, seat at 450, and a back that leans. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFChairTest, "HouseForge.Frame.Chair", HF_TEST_FLAGS)

bool FHFChairTest::RunTest(const FString& Parameters)
{
	const FHFChairParams P = ReferenceChair();
	const FHFChairBuild Built = FHFFrameKit::BuildChair(P);

	if (!TestTrue(TEXT("The reference chair builds"), Built.bValid))
	{
		return false;
	}

	TestTrue(TEXT("A chair is watertight"), FHFMeshOps::IsClosed(Built.Shell));
	TestTrue(TEXT("A chair faces outward"), Volume(Built.Shell) > 0.0);
	TestTrue(TEXT("Every triangle carries a surface role"), EveryTriangleHasARole(Built.Shell));

	// THE FOOTPRINT HAS TO BE TRUSTWORTHY, because the whole clearance check in the living room is
	// done against it: a chair that swept 50 mm more than it declared would be pulled out into the
	// sofa with every measurement saying it cleared.
	const FAxisAlignedBox3d Bounds = Built.Shell.GetBounds();
	TestTrue(TEXT("The chair fills its drawn footprint about its own centre"),
		Bounds.Min.X >= -22.5 - 0.05 && Bounds.Max.X <= 22.5 + 0.05
		&& Bounds.Min.Y >= -24.0 - 0.05 && Bounds.Max.Y <= 24.0 + 0.05);
	TestTrue(TEXT("The chair stands exactly as tall as it was drawn"),
		FMath::IsNearlyEqual(Bounds.Min.Z, 0.0, 0.05) && FMath::IsNearlyEqual(Bounds.Max.Z, 85.0, 0.05));

	const TSet<EHFSurfaceRole> Roles = FHFMeshOps::RolesPresent(Built.Shell);
	TestTrue(TEXT("The frame is timber"), Roles.Contains(EHFSurfaceRole::JoineryCarcass));
	TestTrue(TEXT("The seat pad is fabric"), Roles.Contains(EHFSurfaceRole::Fabric));
	TestTrue(TEXT("The back rest is faced"), Roles.Contains(EHFSurfaceRole::ShutterLaminate));

	// The seat is at seat height, which is what has to agree with the table it is pulled up to.
	TestTrue(TEXT("The seat pad tops out at the seat height"),
		FMath::IsNearlyEqual(Built.Cushion.GetBounds().Max.Z, Built.Used.SeatHeight, 0.05));

	// A DINING CHAIR HAS TO GO UNDER THE TABLE IT BELONGS TO. 450 of seat under a 660 apron leaves
	// 210 of thigh room, which is what an Indian dining set actually is; below about 180 it is not a
	// set at all.
	const FHFTableParams Table = ReferenceDiningTable();
	const double ThighRoom = Table.KneeClearance() - Built.Used.SeatHeight;
	TestTrue(FString::Printf(TEXT("There is thigh room between the seat and the apron (%.1f cm)"), ThighRoom),
		ThighRoom > 18.0);

	// The back leans, measured in centimetres of travel rather than as an angle nobody can check.
	{
		const double Lean = LeanOf(Built.BackRest);
		TestTrue(FString::Printf(TEXT("The back rest leans back with its stiles (%.2f cm)"), Lean),
			Lean > 0.2);
	}

	// EVERYTHING BELOW THE SEAT IS VERTICAL, which is what makes the footprint trustworthy: a chair
	// whose rear legs splayed would sweep more than it declared the moment somebody pulled it out,
	// and the whole clearance check in the living room is done against the declared footprint.
	//
	// Stated as "nothing under the seat reaches past the seat's own back edge" rather than as two
	// bands compared, because a lofted member has vertices only at its ends and a band taken across
	// the middle of one is empty - see BoundsInZBand.
	{
		const double SeatBackY = -Built.Used.Depth * 0.5 + Built.Used.SeatDepth();
		const FBox UnderSeat = BoundsInZBand(Built.Frame, 0.0, Built.Used.SeatUnderZ());

		if (TestTrue(TEXT("There is a frame under the seat"), UnderSeat.IsValid != 0))
		{
			TestTrue(FString::Printf(TEXT("Nothing under the seat leans past its back edge (%.2f of %.2f)"),
				UnderSeat.Max.Y, SeatBackY), UnderSeat.Max.Y <= SeatBackY + 0.05);
			TestTrue(TEXT("The front legs are on the drawn front"),
				UnderSeat.Min.Y <= -Built.Used.Depth * 0.5 + Built.Used.TimberRoll + 0.05);
		}
	}

	// Parameters change the chair.
	{
		FHFChairParams Tall = P;
		Tall.Height = 100.0;
		const FHFChairBuild Built2 = FHFFrameKit::BuildChair(Tall);
		TestTrue(TEXT("A taller chair is taller"), Built2.Shell.GetBounds().Max.Z > Bounds.Max.Z + 10.0);
	}

	{
		FHFChairParams Bare = P;
		Bare.CushionThickness = 0.0;
		const FHFChairBuild Built2 = FHFFrameKit::BuildChair(Bare);
		TestEqual(TEXT("A chair with no pad has no fabric on it"), Built2.Cushion.TriangleCount(), 0);
	}

	// Degenerate input.
	{
		FHFChairParams Nothing = P;
		Nothing.Width = 0.0;
		const FHFChairBuild Empty = FHFFrameKit::BuildChair(Nothing);

		TestFalse(TEXT("A chair with no width is refused"), Empty.bValid);
		TestEqual(TEXT("A refused chair leaves no geometry"), Empty.Shell.TriangleCount(), 0);
	}

	return true;
}

// ======================================================== and the one thing only the room knows

namespace
{
	/** Axis-aligned bounds of a fixture's footprint after rotation, in spec units. */
	FBox2D FootprintBounds(const FHFFixture& Fixture, const FVector2D& Offset = FVector2D::ZeroVector)
	{
		const double Radians = FMath::DegreesToRadians(Fixture.RotationDegrees);
		const double CosR = FMath::Cos(Radians);
		const double SinR = FMath::Sin(Radians);

		const double HalfW = Fixture.Footprint.X * 0.5;
		const double HalfD = Fixture.Footprint.Y * 0.5;

		FBox2D Bounds(ForceInit);
		for (int32 Corner = 0; Corner < 4; ++Corner)
		{
			const double LocalX = (Corner == 0 || Corner == 3) ? -HalfW : HalfW;
			const double LocalY = (Corner < 2) ? -HalfD : HalfD;

			Bounds += Fixture.Position + Offset + FVector2D(
				LocalX * CosR - LocalY * SinR,
				LocalX * SinR + LocalY * CosR);
		}
		return Bounds;
	}

	/** Where a chair ends up when somebody pulls it back to sit down. Local +Y is BACK. */
	FVector2D PullOutOffset(const FHFFixture& Chair, double Distance)
	{
		const double Radians = FMath::DegreesToRadians(Chair.RotationDegrees);
		return FVector2D(-FMath::Sin(Radians) * Distance, FMath::Cos(Radians) * Distance);
	}

	/** Clear distance between two axis-aligned boxes; negative when they overlap. */
	double GapBetween(const FBox2D& A, const FBox2D& B)
	{
		const double GapX = FMath::Max(A.Min.X - B.Max.X, B.Min.X - A.Max.X);
		const double GapY = FMath::Max(A.Min.Y - B.Max.Y, B.Min.Y - A.Max.Y);
		return FMath::Max(GapX, GapY);
	}

	const FHFFixture* Find(const FHFHouseSpec& Spec, const TCHAR* Id)
	{
		for (const FHFFixture& Fixture : Spec.Fixtures)
		{
			if (Fixture.Id == FName(Id))
			{
				return &Fixture;
			}
		}
		return nullptr;
	}
}

/**
 * A chair pulled out must not foul anything, and the dining table must not block the balcony door.
 *
 * NEITHER OF THESE IS A QUESTION ABOUT A MESH. A generator may not go looking for the rest of the
 * house and a chair does not know there is a sofa; the only layer that can see both is the one
 * holding the spec, so the check lives here and is measured in millimetres off the drawing.
 *
 * This is what stands in for a motion assertion in a group where nothing articulates. The chairs are
 * the moving part of a dining set - they are moved rather than hinged - and "did it move" is the
 * wrong question about them. "Where does it end up, and what is there" is the right one, and it is
 * exactly the question the wardrobe's two cancelling leaves would have failed.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFDiningClearanceTest, "HouseForge.Living.DiningClearance", HF_TEST_FLAGS)

bool FHFDiningClearanceTest::RunTest(const FString& Parameters)
{
	const FHFHouseSpec Spec = FHFSampleHouse::Make2BHK();

	// How far back somebody pulls a chair to sit down. 350 mm is getting into it; it is not the same
	// as the 150 the chair is tucked under the table by.
	constexpr double PullOut = 350.0;

	const FHFFixture* Table = Find(Spec, TEXT("F_DiningTable"));
	const FHFFixture* Sofa = Find(Spec, TEXT("F_Sofa"));
	const FHFFixture* Coffee = Find(Spec, TEXT("F_CoffeeTable"));

	if (!TestTrue(TEXT("The living room has a dining table, a sofa and a coffee table"),
		Table != nullptr && Sofa != nullptr && Coffee != nullptr))
	{
		return false;
	}

	// ------------------------------------------------------------------- there are chairs at all
	TArray<const FHFFixture*> Chairs;
	for (const FHFFixture& Fixture : Spec.Fixtures)
	{
		if (Fixture.Type == EHFFixtureType::Chair && Fixture.RoomId == FName(TEXT("R_Living")))
		{
			Chairs.Add(&Fixture);
		}
	}

	TestEqual(TEXT("A four-seater has four chairs"), Chairs.Num(), 4);

	// --------------------------------------------------------- every chair is tucked to the table
	//
	// Tucked, not standing beside it: a chair whose footprint does not overlap its table is 150 mm
	// away from it, and the pulled-out figure below would then be measured from the wrong place.
	const FBox2D TableBounds = FootprintBounds(*Table);

	for (const FHFFixture* Chair : Chairs)
	{
		TestTrue(FString::Printf(TEXT("Chair '%s' is tucked under the table"), *Chair->Id.ToString()),
			FootprintBounds(*Chair).Intersect(TableBounds));
	}

	// -------------------------------------------------- and pulled out, it fouls nothing at all
	//
	// Against everything else in the room that stands on the floor. The tolerance is zero: this is
	// not asking for comfort, it is asking whether the chair goes THROUGH the sofa.
	for (const FHFFixture* Chair : Chairs)
	{
		const FBox2D Pulled = FootprintBounds(*Chair, PullOutOffset(*Chair, PullOut));

		for (const FHFFixture& Other : Spec.Fixtures)
		{
			if (Other.Id == Chair->Id || Other.RoomId != FName(TEXT("R_Living"))
				|| Other.Type == EHFFixtureType::DiningTable || Other.IsCeilingMounted())
			{
				continue;
			}

			// Only what stands on the floor and reaches seat height can be fouled by a chair. A
			// socket at 300 on the wall is behind it, and a pelmet is 2 m over its head.
			if (Other.BaseZ > 45.0)
			{
				continue;
			}

			const double Gap = GapBetween(Pulled, FootprintBounds(Other));

			TestTrue(FString::Printf(
				TEXT("Chair '%s' pulled out clears '%s' (%.0f mm)"),
				*Chair->Id.ToString(), *Other.Id.ToString(), Gap), Gap > 0.0);
		}
	}

	// ------------------------------------------------ the table does not stand in the balcony door
	//
	// D_Balcony is a 1800 sliding unit centred at 2100 along W_South, so it occupies X 1200..3000 and
	// a person walks straight out through it. A dining table across it would be the same defect the
	// TV run had before it was split around the same door.
	{
		const FHFOpening* Balcony = nullptr;
		for (const FHFOpening& Opening : Spec.Openings)
		{
			if (Opening.Id == FName(TEXT("D_Balcony")))
			{
				Balcony = &Opening;
			}
		}

		if (TestTrue(TEXT("The balcony door is still there"), Balcony != nullptr))
		{
			const double DoorMin = Balcony->OffsetAlongWall - Balcony->Width * 0.5;
			const double DoorMax = Balcony->OffsetAlongWall + Balcony->Width * 0.5;

			// W_South runs west to east along Y = 0, so an offset along it IS an X coordinate.
			for (const FHFFixture& Fixture : Spec.Fixtures)
			{
				if (Fixture.RoomId != FName(TEXT("R_Living")) || Fixture.IsCeilingMounted()
					|| Fixture.BaseZ > 45.0)
				{
					continue;
				}

				const FBox2D Bounds = FootprintBounds(Fixture);
				const bool bAcrossTheDoor = Bounds.Max.X > DoorMin && Bounds.Min.X < DoorMax;
				const bool bInTheApproach = Bounds.Min.Y < 900.0;

				TestFalse(FString::Printf(
					TEXT("'%s' does not stand in the approach to the balcony door"),
					*Fixture.Id.ToString()), bAcrossTheDoor && bInTheApproach);
			}
		}
	}

	// ------------------------------------------------------------ and the dining end has room in it
	//
	// The figures the sample house's own comment claims, measured rather than trusted.
	{
		const FBox2D SofaBounds = FootprintBounds(*Sofa);
		const FBox2D CoffeeBounds = FootprintBounds(*Coffee);

		double WorstToSofa = TNumericLimits<double>::Max();
		double WorstToCoffee = TNumericLimits<double>::Max();

		for (const FHFFixture* Chair : Chairs)
		{
			const FBox2D Pulled = FootprintBounds(*Chair, PullOutOffset(*Chair, PullOut));
			WorstToSofa = FMath::Min(WorstToSofa, GapBetween(Pulled, SofaBounds));
			WorstToCoffee = FMath::Min(WorstToCoffee, GapBetween(Pulled, CoffeeBounds));
		}

		TestTrue(FString::Printf(TEXT("A pulled-out chair keeps 100 mm off the sofa (%.0f mm)"), WorstToSofa),
			WorstToSofa > 100.0);
		TestTrue(FString::Printf(TEXT("A pulled-out chair keeps 50 mm off the coffee table (%.0f mm)"), WorstToCoffee),
			WorstToCoffee > 50.0);
	}

	// ---------------------------------------------------------------- the sofa is against its wall
	//
	// W_Mid_Lower is 115 thick on the Y = 3600 line, so its living-room face is at 3542.5. A sofa
	// that stood off it by a fifth of its own depth was what this group found by rendering the room.
	{
		const FBox2D SofaBounds = FootprintBounds(*Sofa);
		TestTrue(FString::Printf(TEXT("The sofa's back is on the plaster (%.1f mm off)"),
			3542.5 - SofaBounds.Max.Y), FMath::IsNearlyEqual(SofaBounds.Max.Y, 3542.5, 1.0));
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
