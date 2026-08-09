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
#include "Model/HFBuildDefaults.h"
#include "Model/HFFixturePlacement.h"
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
	 * How far the worst vertex on the mesh stands OUTSIDE the supporting plane of some triangle.
	 *
	 * THE MEASUREMENT THAT SEES A FOLDED CORNER, and nothing else in this file can.
	 *
	 * A soft box is the offset of an inner box, so it is convex by construction: no vertex may lie on
	 * the outward side of any triangle's plane. Everything else the suite asserts about one survives a
	 * fold untouched - the solid stays closed, its volume stays plausible, its bounds stay exactly the
	 * declared box, and its sharpest dihedral stays below the bevel threshold, because a fold is made
	 * of the same small facets as the surface it folds out of.
	 *
	 * What produced one: AppendSoftBox floored the plan radius proportionally, and RoundedRectangle
	 * puts its arc centres at HalfExtents - Radius, so the moment the floor bound, the corner's arc
	 * centre travelled inward as the roll turned instead of standing still. The top ring's corner
	 * ended up INSIDE the ring below it and the skin folded back on itself: a re-entrant faceted
	 * wedge with a hard crease down each side, on all four corners of every cushion, every mattress
	 * and both sofa arms. Found by looking at a render from four metres; invisible to the gate.
	 *
	 * Returned as a distance rather than a bool so a failure says how deep the fold is.
	 */
	double WorstConcavityCm(const FDynamicMesh3& Mesh)
	{
		double Worst = 0.0;

		for (const int32 Tri : Mesh.TriangleIndicesItr())
		{
			const FVector3d Normal = Mesh.GetTriNormal(Tri);
			if (!Normal.IsNormalized())
			{
				continue;
			}

			const FVector3d OnPlane = Mesh.GetTriCentroid(Tri);

			for (const int32 Vertex : Mesh.VertexIndicesItr())
			{
				Worst = FMath::Max(Worst, (Mesh.GetVertex(Vertex) - OnPlane).Dot(Normal));
			}
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
		// where the roll finishes. Almost, rather than exactly: the kit holds the plan radius a little
		// above both rolls precisely so it never has to converge on a point - see AppendSoftBox.
		TestTrue(FString::Printf(TEXT("The plan radius closes as the roll turns (%.2f cm from the cap corner)"),
			NearestOnCap), NearestOnCap < Soft.CornerRadius * 0.45);

		// And at full width it has NOT: the widest ring keeps its corner radius, so nothing there gets
		// anywhere near the box's own corner. This is the half that fails if the blend is removed.
		TestTrue(FString::Printf(TEXT("The widest ring is still rounded in plan (%.2f cm)"),
			NearestAtMidHeight), NearestAtMidHeight > Soft.CornerRadius * 0.3);
	}

	// NO PART OF THE SURFACE FOLDS BACK ON ITSELF. See WorstConcavityCm: this is the only assertion
	// here that a re-entrant corner fails, and a re-entrant corner is what the sofa, both mattresses
	// and all four chairs were carrying while every other line in this test passed.
	TestTrue(FString::Printf(TEXT("A soft box is convex everywhere (worst %.3f cm outside a face)"),
		WorstConcavityCm(Mesh)), WorstConcavityCm(Mesh) < 0.02);

	// And it holds at the proportions the flat actually builds, not only at this test's tidy 60 cube.
	// A cushion's plan radius equals its top roll, which is exactly the case the floor used to bind on.
	{
		FHFSoftBoxParams Cushion;
		Cushion.CornerRadius = 7.5;
		Cushion.TopRadius = 7.5;
		Cushion.BottomRadius = 3.0;
		Cushion.CornerSteps = 6;
		Cushion.RollSteps = 5;

		FDynamicMesh3 Arm;
		FHFMeshOps::InitialiseMesh(Arm);
		FHFMeshOps::AppendSoftBox(Arm, FVector3d::Zero(), FVector3d(20.0, 90.0, 62.0), Cushion,
			EHFSurfaceRole::Fabric);

		TestTrue(FString::Printf(TEXT("A sofa arm is convex everywhere (worst %.3f cm)"),
			WorstConcavityCm(Arm)), WorstConcavityCm(Arm) < 0.02);
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

// ======================================================================== the four named designs

namespace
{
	/**
	 * The four designs and the box a plan of this living room draws each of them in.
	 *
	 * Held here in centimetres AND in FHFSampleHouse::Make2BHK(EHFSofaDesign) in millimetres, which
	 * would be two places for one fact - so SofaDesignsMatchTheSampleHouse asserts they agree. A
	 * design whose drawn box drifts between the geometry tests and the room-fit tests would pass both
	 * while measuring two different sofas.
	 */
	struct FSofaDesignCase
	{
		EHFSofaDesign Design;
		const TCHAR* Name;

		/** Centimetres. */
		FVector2D Footprint;
		double Height;

		/** The researched seat height for this design, in centimetres. */
		double SeatHeight;

		/** The band its seat depth has to land in, in centimetres. */
		double MinSeatDepth;
		double MaxSeatDepth;
	};

	const FSofaDesignCase SofaDesignCases[] = {
		{ EHFSofaDesign::SquareArm,       TEXT("SquareArm"),       FVector2D(210.0,  90.0), 80.0, 43.0, 55.0, 62.0 },
		{ EHFSofaDesign::ChaiseSectional, TEXT("ChaiseSectional"), FVector2D(220.0, 140.0), 80.0, 43.0, 55.0, 62.0 },
		{ EHFSofaDesign::LowProfile,      TEXT("LowProfile"),      FVector2D(210.0,  95.0), 70.0, 40.0, 58.0, 68.0 },
		{ EHFSofaDesign::RolledArm,       TEXT("RolledArm"),       FVector2D(210.0,  95.0), 85.0, 45.0, 55.0, 62.0 }
	};

	/** An enum reported as a number, because TestEqual has no overload that can print one. */
	int32 AsNumber(EHFSofaDesign Design) { return static_cast<int32>(Design); }

	FHFFixture MakeSofaFixture(const FSofaDesignCase& Case)
	{
		FHFFixture F = MakeFixture(TEXT("F_Sofa"), EHFFixtureType::Sofa, Case.Footprint, Case.Height);
		F.Params.SofaDesign = Case.Design;
		F.Params.bChaiseOnLeft = false;
		return F;
	}

	FHFSofaBuild BuildDesign(const FSofaDesignCase& Case)
	{
		return FHFUpholsteryKit::BuildSofa(AHFSofaActor::ParamsFor(MakeSofaFixture(Case)));
	}

	/**
	 * Width of the FLAT strip along the top of an arm, as a fraction of the arm's own width.
	 *
	 * THE ONLY MEASUREMENT THAT SEPARATES A ROLLED ARM FROM A SQUARE ONE, and neither volume, bounds
	 * nor dihedral angle can do it: both are soft boxes of the same construction, both are exactly
	 * their declared size, and every edge on both welds smooth. What differs is how much of the top
	 * is still flat once the roll has turned - a 70 mm roll on a 180 arm leaves 40 mm of flat, which
	 * is a square arm with the corners eased, and a roll at half the width leaves almost none, which
	 * is a scroll.
	 */
	double ArmFlatTopFraction(const FDynamicMesh3& Arm)
	{
		const FAxisAlignedBox3d Bounds = Arm.GetBounds();
		const FBox Top = BoundsInZBand(Arm, Bounds.Max.Z - 0.01, Bounds.Max.Z + 0.01);

		if (Top.IsValid == 0 || Bounds.Width() <= 0.0)
		{
			return 0.0;
		}

		return (Top.Max.X - Top.Min.X) / Bounds.Width();
	}
}

/**
 * Every named design builds, is the sofa it says it is, and is soft everywhere a soft box has to be.
 *
 * The per-design half of what HouseForge.Upholstery.Sofa asserts about the reference three-seater,
 * plus the two figures that decide whether a sofa reads as furniture or as a game asset: SEAT HEIGHT
 * AND SEAT DEPTH, in centimetres, against the researched range rather than against whatever the code
 * happens to produce. 400-450 to the seat and 550-600 deep is a sofa; 500 and 450 is a hall bench,
 * and the difference is invisible in every screenshot and obvious the moment somebody sits in it.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFSofaDesignsTest, "HouseForge.Upholstery.SofaDesigns", HF_TEST_FLAGS)

bool FHFSofaDesignsTest::RunTest(const FString& Parameters)
{
	for (const FSofaDesignCase& Case : SofaDesignCases)
	{
		const FHFSofaBuild Built = BuildDesign(Case);

		if (!TestTrue(FString::Printf(TEXT("%s builds"), Case.Name), Built.bValid))
		{
			continue;
		}

		const FHFSofaParams& P = Built.Used;

		TestTrue(FString::Printf(TEXT("%s is watertight"), Case.Name),
			FHFMeshOps::IsClosed(Built.Shell));
		TestTrue(FString::Printf(TEXT("%s faces outward"), Case.Name), Volume(Built.Shell) > 0.0);
		TestTrue(FString::Printf(TEXT("%s carries a surface role on every triangle"), Case.Name),
			EveryTriangleHasARole(Built.Shell));

		// Two materials on the three with legs; ONE on the skirted design, deliberately - there is no
		// timber on a sofa whose legs are behind a valance, and inventing some would be four solids
		// nobody can see. See FHFSofaParams::bHasSkirt.
		const TSet<EHFSurfaceRole> Roles = FHFMeshOps::RolesPresent(Built.Shell);
		TestTrue(FString::Printf(TEXT("%s is upholstered in Fabric"), Case.Name),
			Roles.Contains(EHFSurfaceRole::Fabric));
		TestEqual(FString::Printf(TEXT("%s shows timber legs only when it has any"), Case.Name),
			Roles.Contains(EHFSurfaceRole::JoineryCarcass) ? 1 : 0, P.bHasSkirt ? 0 : 1);

		// ---------------------------------------------------------------- the box it was drawn in
		//
		// THE DRAWN BOX IS THE OBJECT, for every design. A sofa that grew past its footprint would go
		// through the wall behind it and the drawing would still say it fitted.
		const FAxisAlignedBox3d Bounds = Built.Shell.GetBounds();

		TestTrue(FString::Printf(TEXT("%s fills its drawn width (%.2f..%.2f of %.1f)"),
			Case.Name, Bounds.Min.X, Bounds.Max.X, Case.Footprint.X),
			FMath::IsNearlyEqual(Bounds.Min.X, 0.0, 0.05)
				&& FMath::IsNearlyEqual(Bounds.Max.X, Case.Footprint.X, 0.05));
		TestTrue(FString::Printf(TEXT("%s fills its drawn depth (%.2f..%.2f of %.1f)"),
			Case.Name, Bounds.Min.Y, Bounds.Max.Y, Case.Footprint.Y),
			FMath::IsNearlyEqual(Bounds.Min.Y, 0.0, 0.05)
				&& FMath::IsNearlyEqual(Bounds.Max.Y, Case.Footprint.Y, 0.05));
		TestTrue(FString::Printf(TEXT("%s stands exactly as tall as it was drawn (%.2f of %.1f)"),
			Case.Name, Bounds.Max.Z, Case.Height),
			FMath::IsNearlyEqual(Bounds.Max.Z, Case.Height, 0.05));

		// The floor, and the one design that deliberately does not touch it: a skirt's hem stops
		// 15 mm clear, which is what stops it reading as painted onto the tiles.
		const double ExpectedLowZ = P.bHasSkirt ? P.SkirtBottomZ() : 0.0;
		TestTrue(FString::Printf(TEXT("%s meets the floor where it should (%.2f, expected %.2f)"),
			Case.Name, Bounds.Min.Z, ExpectedLowZ),
			FMath::IsNearlyEqual(Bounds.Min.Z, ExpectedLowZ, 0.05));

		// -------------------------------------------------------------- the two ergonomic figures
		//
		// MEASURED ON THE BUILT CUSHION, not on the parameter struct: a seat height copied faithfully
		// into a field and then ignored by the generator passes every check made on the struct.
		if (Built.SeatCushions.Num() > 0)
		{
			const FAxisAlignedBox3d Cushion = Built.SeatCushions[0].GetBounds();

			TestTrue(FString::Printf(TEXT("%s seats you at %.1f cm, its researched %.1f"),
				Case.Name, Cushion.Max.Z, Case.SeatHeight),
				FMath::IsNearlyEqual(Cushion.Max.Z, Case.SeatHeight, 0.3));

			// And inside the band that separates a sofa from a dining chair at 450-460 and from a
			// day bed at 350. Asserted as well as the exact figure, so a design added later cannot
			// quietly land outside it.
			TestTrue(FString::Printf(TEXT("%s seats you like a sofa (%.1f cm)"), Case.Name, Cushion.Max.Z),
				Cushion.Max.Z >= 38.0 && Cushion.Max.Z <= 46.0);

			const double SeatDepth = Cushion.Max.Y - Cushion.Min.Y;
			TestTrue(FString::Printf(TEXT("%s has a %.1f cm seat, wanted %.0f..%.0f"),
				Case.Name, SeatDepth, Case.MinSeatDepth, Case.MaxSeatDepth),
				SeatDepth >= Case.MinSeatDepth && SeatDepth <= Case.MaxSeatDepth);

			const double CushionWidth = Cushion.Max.X - Cushion.Min.X;
			TestTrue(FString::Printf(TEXT("%s has seat-width cushions (%.1f cm)"), Case.Name, CushionWidth),
				CushionWidth > 50.0 && CushionWidth < 68.0);
		}
		else
		{
			AddError(FString::Printf(TEXT("%s built no seat cushions"), Case.Name));
		}

		// ------------------------------------------------------------------ NOTHING FOLDS BACK ON ITSELF
		//
		// The assertion that caught the defect this kit was built around - AppendSoftBox flooring its
		// plan radius, which walked the corner's arc centre inward as the roll turned and left a
		// re-entrant faceted wedge on every cushion and both arms. See WorstConcavityCm.
		//
		// Applied to every part that IS one soft box, which is where the property holds. It is not
		// asserted on the shell, the base of an L or a skirt: those are two or four solids in one
		// mesh and are non-convex by construction, so a bound on them would measure the design rather
		// than the primitive.
		TArray<TPair<FString, const FDynamicMesh3*>> SoftParts;
		SoftParts.Emplace(TEXT("back panel"), &Built.Back);
		if (!P.IsSectional())
		{
			SoftParts.Emplace(TEXT("base"), &Built.Base);
		}
		if (P.IsSectional())
		{
			SoftParts.Emplace(TEXT("chaise cushion"), &Built.ChaiseCushion);
		}
		for (int32 Index = 0; Index < Built.Arms.Num(); ++Index)
		{
			SoftParts.Emplace(FString::Printf(TEXT("arm %d"), Index), &Built.Arms[Index]);
		}
		for (int32 Index = 0; Index < Built.SeatCushions.Num(); ++Index)
		{
			SoftParts.Emplace(FString::Printf(TEXT("seat cushion %d"), Index), &Built.SeatCushions[Index]);
		}
		for (int32 Index = 0; Index < Built.BackCushions.Num(); ++Index)
		{
			SoftParts.Emplace(FString::Printf(TEXT("back cushion %d"), Index), &Built.BackCushions[Index]);
		}

		for (const TPair<FString, const FDynamicMesh3*>& Part : SoftParts)
		{
			if (Part.Value->TriangleCount() == 0)
			{
				continue;
			}

			const double Worst = WorstConcavityCm(*Part.Value);
			TestTrue(FString::Printf(TEXT("%s: the %s is convex everywhere (worst %.3f cm)"),
				Case.Name, *Part.Key, Worst), Worst < 0.02);
		}

		// And soft: no arris anywhere on the upholstery sharp enough to need the chamfer that
		// FHFBevelParams deliberately does not give Fabric.
		const FHFBevelParams Bevel;
		for (const FDynamicMesh3& Arm : Built.Arms)
		{
			TestTrue(FString::Printf(TEXT("%s: no arm edge is sharp (%.1f deg)"),
				Case.Name, SharpestEdgeDegrees(Arm)),
				SharpestEdgeDegrees(Arm) < Bevel.MinAngleDegrees);
		}
	}

	return true;
}

/**
 * THE FOUR ARE DIFFERENT OBJECTS, and this is where that claim is measured rather than asserted.
 *
 * The point of a named design is that swapping it changes the room, so "it built" is not the test -
 * every one of them would build if all four were the same box with different numbers. What follows
 * is one measurement per design of the thing that makes it that design, taken on the geometry:
 *
 *   LowProfile       how much daylight there is under it, and how far the leg's foot rakes out
 *   RolledArm        how little of the arm's top is still flat, and that a skirt closed the gap
 *   ChaiseSectional  that the plan is an L and the chaise's cushion is a metre long
 *   SquareArm        that it is none of those things
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFSofaDesignSilhouettesTest,
	"HouseForge.Upholstery.SofaDesignSilhouettes", HF_TEST_FLAGS)

bool FHFSofaDesignSilhouettesTest::RunTest(const FString& Parameters)
{
	const FHFSofaBuild Square = BuildDesign(SofaDesignCases[0]);
	const FHFSofaBuild Sectional = BuildDesign(SofaDesignCases[1]);
	const FHFSofaBuild Low = BuildDesign(SofaDesignCases[2]);
	const FHFSofaBuild Rolled = BuildDesign(SofaDesignCases[3]);

	if (!TestTrue(TEXT("All four designs build"),
		Square.bValid && Sectional.bValid && Low.bValid && Rolled.bValid))
	{
		return false;
	}

	// ----------------------------------------------------------------- LowProfile shows the floor
	//
	// The clear band under the base, in centimetres. It is most of what separates loose furniture
	// from joinery, and this design's whole argument is that MORE of it makes a small room look
	// larger: 180 mm against a square arm's 120.
	{
		const double SquareGap = Square.Base.GetBounds().Min.Z;
		const double LowGap = Low.Base.GetBounds().Min.Z;

		TestTrue(FString::Printf(TEXT("A low-profile sofa shows more floor under it (%.1f vs %.1f cm)"),
			LowGap, SquareGap), LowGap > SquareGap + 4.0);
		TestTrue(FString::Printf(TEXT("A low-profile sofa stands 180 mm clear (%.1f cm)"), LowGap),
			FMath::IsNearlyEqual(LowGap, 18.0, 0.3));

		// And it is LOWER, which is the other half of the name.
		TestTrue(FString::Printf(TEXT("A low-profile sofa is lower (%.1f vs %.1f cm)"),
			Low.Shell.GetBounds().Max.Z, Square.Shell.GetBounds().Max.Z),
			Low.Shell.GetBounds().Max.Z < Square.Shell.GetBounds().Max.Z - 5.0);

		// THE LEGS RAKE OUT AND STAY INSIDE THE BOX.
		//
		// Measured as the SPREAD of the set at the floor against its spread at the head, which is the
		// only form of the question that survives the taper. Comparing one leg's outer edge top to
		// bottom mixes two effects that pull opposite ways - the splay pushes the foot out, the taper
		// pulls its radius in - and on a 50 mm leg the taper eats most of the answer. Across the set
		// the sign is unambiguous: splayed legs stand FURTHER apart at the floor than at the base
		// they carry, and a tapered vertical leg stands closer.
		auto SpreadAt = [](const FDynamicMesh3& Legs, bool bAtFloor) -> double
		{
			const FAxisAlignedBox3d Bounds = Legs.GetBounds();
			const FBox Band = bAtFloor
				? BoundsInZBand(Legs, Bounds.Min.Z, Bounds.Min.Z + 0.5)
				: BoundsInZBand(Legs, Bounds.Max.Z - 0.5, Bounds.Max.Z);

			return Band.IsValid != 0 ? (Band.Max.X - Band.Min.X) : 0.0;
		};

		if (TestTrue(TEXT("The low-profile sofa has legs to measure"), Low.Legs.TriangleCount() > 0))
		{
			const double FootSpread = SpreadAt(Low.Legs, true);
			const double HeadSpread = SpreadAt(Low.Legs, false);

			TestTrue(FString::Printf(TEXT("A splayed leg stands wider at the floor than at the base "
				"(%.1f vs %.1f cm)"), FootSpread, HeadSpread), FootSpread > HeadSpread + 2.0);

			// And the toe is still inside the drawn footprint, or the sofa's plan stops meaning
			// anything to the room that has to hold it. See SanitiseSofa's splay clamp.
			const FAxisAlignedBox3d LegBounds = Low.Legs.GetBounds();
			TestTrue(FString::Printf(TEXT("And its toe stays inside the drawn box (%.2f cm in)"),
				LegBounds.Min.X), LegBounds.Min.X > 0.0);
		}

		// A turned leg does the opposite: vertical, so its tapered foot stands NARROWER than its head.
		{
			const double FootSpread = SpreadAt(Square.Legs, true);
			const double HeadSpread = SpreadAt(Square.Legs, false);

			TestTrue(FString::Printf(TEXT("A turned leg stands vertical (%.1f vs %.1f cm)"),
				FootSpread, HeadSpread), FootSpread < HeadSpread);
		}
	}

	// -------------------------------------------------------------- RolledArm is round on top
	{
		const FAxisAlignedBox3d SquareArm = Square.Arms[0].GetBounds();
		const FAxisAlignedBox3d RolledArmBounds = Rolled.Arms[0].GetBounds();
		const double SquareArmWidth = SquareArm.Max.X - SquareArm.Min.X;
		const double RolledArmWidth = RolledArmBounds.Max.X - RolledArmBounds.Min.X;

		// A ROLL ARM IS FAT BEFORE IT IS ROUND, and this is the bigger of the two differences from
		// across a room: 240 mm of arm against 180. Both are measured on the built solid.
		TestTrue(FString::Printf(TEXT("A rolled arm is a fat arm (%.1f vs %.1f cm)"),
			RolledArmWidth, SquareArmWidth), RolledArmWidth > SquareArmWidth + 4.0);

		const double SquareFlat = ArmFlatTopFraction(Square.Arms[0]);
		const double RolledFlat = ArmFlatTopFraction(Rolled.Arms[0]);

		// AND ITS TOP IS A HALF-ROUND. 30 mm of flat across a 240 arm is a scroll; 40 across 180 is a
		// square arm with its corners eased. The absolute figure is the honest one - the roll cannot
		// reach a true semicircle without landing on AppendSoftBox's singular plan radius, see
		// ClampSoftToBox - so the claim is "flat for under 35 mm", not "flat for nothing".
		TestTrue(FString::Printf(TEXT("A rolled arm's top is flat for under 35 mm (%.1f mm, %.1f%% of "
			"its width)"), RolledFlat * RolledArmWidth * 10.0, RolledFlat * 100.0),
			RolledFlat * RolledArmWidth < 3.5);
		TestTrue(FString::Printf(TEXT("A square arm still has a flat top (%.1f mm, %.1f%%)"),
			SquareFlat * SquareArmWidth * 10.0, SquareFlat * 100.0),
			SquareFlat * SquareArmWidth > 3.8);
		TestTrue(FString::Printf(TEXT("And proportionally more of it (%.1f%% vs %.1f%%)"),
			SquareFlat * 100.0, RolledFlat * 100.0), SquareFlat > RolledFlat * 1.4);

		// AND NOTHING TOUCHES THE FLOOR EXCEPT CLOTH. The skirt exists, the legs do not, and the
		// gap the other three leave open is closed.
		TestTrue(TEXT("A rolled-arm sofa has a skirt"), Rolled.Skirt.TriangleCount() > 0);
		TestEqual(TEXT("And no legs behind it"), Rolled.Legs.TriangleCount(), 0);
		TestEqual(TEXT("Nothing else here has a skirt"), Square.Skirt.TriangleCount(), 0);

		// The skirt reaches from the underside of the base down to its hem, so there is no band of
		// daylight under this design at all - which is the silhouette change.
		const FAxisAlignedBox3d SkirtBounds = Rolled.Skirt.GetBounds();
		TestTrue(FString::Printf(TEXT("The skirt hangs to 15 mm off the floor (%.2f cm)"), SkirtBounds.Min.Z),
			FMath::IsNearlyEqual(SkirtBounds.Min.Z, 1.5, 0.05));
		TestTrue(FString::Printf(TEXT("And up to the base above it (%.2f vs %.2f cm)"),
			SkirtBounds.Max.Z, Rolled.Base.GetBounds().Min.Z),
			SkirtBounds.Max.Z >= Rolled.Base.GetBounds().Min.Z - 0.05);

		// It is also a taller, higher-seated object than the contemporary sofa: 450 to the seat.
		TestTrue(FString::Printf(TEXT("A roll arm seats you higher (%.1f vs %.1f cm)"),
			Rolled.Used.SeatHeight, Square.Used.SeatHeight),
			Rolled.Used.SeatHeight > Square.Used.SeatHeight + 1.0);
	}

	// ------------------------------------------------------------------ ChaiseSectional is an L
	{
		const FHFSofaParams& P = Sectional.Used;

		TestTrue(TEXT("The sectional really is a sectional"), P.IsSectional());
		TestEqual(TEXT("And says so"), AsNumber(P.BuiltDesign()), AsNumber(EHFSofaDesign::ChaiseSectional));

		// THE RETURN, in centimetres. 500 in front of a 900 straight run out of a 1400 drawn box.
		TestTrue(FString::Printf(TEXT("The chaise returns 500 mm in front of the run (%.1f cm)"),
			P.BuiltChaiseProjection()),
			FMath::IsNearlyEqual(P.BuiltChaiseProjection(), 50.0, 0.05));
		TestTrue(FString::Printf(TEXT("Leaving the straight run a sofa's own depth (%.1f cm)"),
			P.MainRunDepth()), FMath::IsNearlyEqual(P.MainRunDepth(), 90.0, 0.05));

		// THE MEASUREMENT THAT SAYS IT IS A CHAISE AND NOT A CORNER SEAT: one uninterrupted cushion
		// over a metre long, where a seat cushion is 570.
		//
		// Measured as Max.Y - Min.Y and not as FAxisAlignedBox3d::Depth(), which is the Z extent:
		// Width/Height/Depth on that type are X/Y/Z, so "Depth" on a cushion is how THICK it is. The
		// first version of this assertion reported a 1070 mm chaise as 140 mm long and passed nothing.
		const FAxisAlignedBox3d Chaise = Sectional.ChaiseCushion.GetBounds();
		const FAxisAlignedBox3d Seat = Sectional.SeatCushions[0].GetBounds();
		const double ChaiseLength = Chaise.Max.Y - Chaise.Min.Y;
		const double SeatLength = Seat.Max.Y - Seat.Min.Y;

		TestTrue(TEXT("The chaise has a cushion"), Sectional.ChaiseCushion.TriangleCount() > 0);
		TestTrue(FString::Printf(TEXT("It is a metre long (%.1f cm)"), ChaiseLength),
			ChaiseLength > 100.0);
		TestTrue(FString::Printf(TEXT("Which is nearly twice a seat cushion (%.1f vs %.1f cm)"),
			ChaiseLength, SeatLength), ChaiseLength > SeatLength * 1.6);

		// AND THE PLAN IS AN L, measured where it matters: at the front of the drawn box only the
		// return is there, and the other end of the sofa is 500 mm behind it. A rectangle would fill
		// the whole width at every depth.
		const FBox Front = BoundsInZBand(Sectional.Shell, P.SeatHeight - 0.5, P.SeatHeight);
		{
			double FrontmostAtRunEnd = TNumericLimits<double>::Max();
			double FrontmostAtChaise = TNumericLimits<double>::Max();

			for (const int32 Vertex : Sectional.Shell.VertexIndicesItr())
			{
				const FVector3d V = Sectional.Shell.GetVertex(Vertex);
				if (V.X < P.Width * 0.25)
				{
					FrontmostAtRunEnd = FMath::Min(FrontmostAtRunEnd, V.Y);
				}
				else if (V.X > P.Width * 0.75)
				{
					FrontmostAtChaise = FMath::Min(FrontmostAtChaise, V.Y);
				}
			}

			TestTrue(FString::Printf(TEXT("The chaise end reaches the front of the box (%.2f cm)"),
				FrontmostAtChaise), FMath::IsNearlyEqual(FrontmostAtChaise, 0.0, 0.05));
			TestTrue(FString::Printf(TEXT("And the other end stops 500 mm behind it (%.2f cm) - an L, "
				"not a rectangle"), FrontmostAtRunEnd),
				FrontmostAtRunEnd > P.BuiltChaiseProjection() - 0.5);
		}

		// The straight run gives up a seat to the return: two cushions plus a chaise, not three.
		TestEqual(TEXT("A 2200 box with a 900 return is a two-seater plus a chaise"),
			Sectional.SeatCushions.Num(), 2);

		// Six legs, not four: an L standing on four is a table with a corner hanging off it.
		TestTrue(TEXT("The return stands on legs of its own"),
			Sectional.Legs.GetBounds().Min.Y < 20.0);

		// And the back runs the WHOLE width, behind the chaise as well as behind the run. Taken to
		// the seat's span instead it would have stopped where the chaise begins and left 900 mm of
		// the sofa open to the room behind it.
		const FAxisAlignedBox3d Back = Sectional.Back.GetBounds();
		const double BackSpan = Back.Max.X - Back.Min.X;
		TestTrue(FString::Printf(TEXT("The back panel spans arm to arm (%.1f of %.1f cm)"),
			BackSpan, P.Width), BackSpan > P.Width - 2.0 * P.ArmWidth - 0.1);
	}

	// ------------------------------------------------------------------- and SquareArm is none of it
	{
		TestFalse(TEXT("A square-arm sofa is not a sectional"), Square.Used.IsSectional());
		TestEqual(TEXT("It has no chaise cushion"), Square.ChaiseCushion.TriangleCount(), 0);
		TestEqual(TEXT("A 2100 box with no return is a three-seater"), Square.SeatCushions.Num(), 3);
		TestTrue(TEXT("It stands on legs"), Square.Legs.TriangleCount() > 0);
	}

	return true;
}

/**
 * The design a spec names, and the one a project defaults to, both reach the geometry.
 *
 * The half of this feature that is not geometry at all. A named design that FHFFixtureParams could
 * carry and AHFSofaActor never read would be EHFShutterMotion's first milestone all over again -
 * every wardrobe in the reference flat side-hung because the field existed and nothing copied it.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFSofaDesignSelectionTest,
	"HouseForge.Upholstery.SofaDesignSelection", HF_TEST_FLAGS)

bool FHFSofaDesignSelectionTest::RunTest(const FString& Parameters)
{
	// ------------------------------------------------------- what the spec names is what gets built
	for (const FSofaDesignCase& Case : SofaDesignCases)
	{
		const FHFSofaParams P = AHFSofaActor::ParamsFor(MakeSofaFixture(Case));
		TestEqual(FString::Printf(TEXT("A fixture naming %s builds one"), Case.Name),
			AsNumber(P.BuiltDesign()), AsNumber(Case.Design));
	}

	// -------------------------------------------------------------- and Default asks the project
	//
	// The sentinel a spec written before designs existed carries, and a drawing that shows a sofa
	// without saying which one. Both mean "whatever this project buys".
	{
		FHFFixture Unnamed = MakeFixture(TEXT("F_Sofa"), EHFFixtureType::Sofa,
			FVector2D(200.0, 95.0), 85.0);
		TestEqual(TEXT("An unnamed sofa carries the sentinel"),
			AsNumber(Unnamed.Params.SofaDesign), AsNumber(EHFSofaDesign::Default));

		// With no project in hand at all: the sofa this kit built before designs existed.
		TestEqual(TEXT("With no project, an unnamed sofa is the contemporary one"),
			AsNumber(AHFSofaActor::ParamsFor(Unnamed).BuiltDesign()), AsNumber(EHFSofaDesign::SquareArm));

		FHFSofaDefaults Project;
		Project.DefaultDesign = EHFSofaDesign::RolledArm;

		const FHFSofaParams P = AHFSofaActor::ParamsFor(Unnamed, Project);
		TestEqual(TEXT("A project that buys roll arms gets one"),
			AsNumber(P.BuiltDesign()), AsNumber(EHFSofaDesign::RolledArm));

		// Measured on the geometry rather than on the field, which is the assertion a value copied
		// and then ignored cannot pass.
		const FHFSofaBuild Built = FHFUpholsteryKit::BuildSofa(P);
		TestTrue(TEXT("And the sofa in the level has the skirt to prove it"),
			Built.Skirt.TriangleCount() > 0);

		// A NAMED DESIGN STILL WINS. The project is the fallback, not an override: a drawing that
		// says the living room has a sectional gets a sectional whatever the project usually buys.
		FHFFixture Named = Unnamed;
		Named.Footprint = FVector2D(220.0, 140.0);
		Named.Params.SofaDesign = EHFSofaDesign::ChaiseSectional;
		TestEqual(TEXT("A drawing that names a design overrules the project"),
			AsNumber(AHFSofaActor::ParamsFor(Named, Project).BuiltDesign()),
			AsNumber(EHFSofaDesign::ChaiseSectional));
	}

	// ---------------------------------------------- a sectional with nowhere to return says so
	//
	// The fallback, and the reason it is not silent. A 900 deep box has no room for a chaise, and the
	// honest answers are either to refuse or to build the straight sofa and REPORT that it did. A
	// 200 mm stub off one end is neither - it is a shape nobody ordered, and the drawing would still
	// say the room had a sectional in it.
	{
		FHFFixture Shallow = MakeFixture(TEXT("F_Sofa"), EHFFixtureType::Sofa,
			FVector2D(210.0, 90.0), 80.0);
		Shallow.Params.SofaDesign = EHFSofaDesign::ChaiseSectional;

		const FHFSofaBuild Built = FHFUpholsteryKit::BuildSofa(AHFSofaActor::ParamsFor(Shallow));

		TestTrue(TEXT("A sectional in a 900 box still builds a sofa"), Built.bValid);
		TestFalse(TEXT("But it is not a sectional"), Built.Used.IsSectional());
		TestEqual(TEXT("And it says so rather than pretending"),
			AsNumber(Built.Used.BuiltDesign()), AsNumber(EHFSofaDesign::SquareArm));
		TestEqual(TEXT("There is no stub of a chaise on it"), Built.ChaiseCushion.TriangleCount(), 0);
		TestTrue(TEXT("It is a straight three-seater"), Built.SeatCushions.Num() == 3);

		// The floor is a figure, not a constant: raise the project's minimum and a return that WAS
		// long enough stops being one.
		FHFFixture Deep = MakeFixture(TEXT("F_Sofa"), EHFFixtureType::Sofa,
			FVector2D(220.0, 140.0), 80.0);
		Deep.Params.SofaDesign = EHFSofaDesign::ChaiseSectional;

		FHFSofaDefaults Fussy;
		Fussy.MinChaiseProjection = 80.0;

		TestTrue(TEXT("A 500 return is a chaise by default"),
			AHFSofaActor::ParamsFor(Deep).IsSectional());
		TestFalse(TEXT("And is not, to a project that wants 800"),
			AHFSofaActor::ParamsFor(Deep, Fussy).IsSectional());
	}

	// -------------------------------------------------- the chaise has a hand, and it is honoured
	{
		FHFFixture Left = MakeFixture(TEXT("F_Sofa"), EHFFixtureType::Sofa, FVector2D(220.0, 140.0), 80.0);
		Left.Params.SofaDesign = EHFSofaDesign::ChaiseSectional;
		Left.Params.bChaiseOnLeft = true;

		FHFFixture Right = Left;
		Right.Params.bChaiseOnLeft = false;

		const FHFSofaBuild L = FHFUpholsteryKit::BuildSofa(AHFSofaActor::ParamsFor(Left));
		const FHFSofaBuild R = FHFUpholsteryKit::BuildSofa(AHFSofaActor::ParamsFor(Right));

		const FAxisAlignedBox3d LeftChaise = L.ChaiseCushion.GetBounds();
		const FAxisAlignedBox3d RightChaise = R.ChaiseCushion.GetBounds();

		TestTrue(FString::Printf(TEXT("A left-hand chaise is at the -X end (%.1f..%.1f cm)"),
			LeftChaise.Min.X, LeftChaise.Max.X), LeftChaise.Max.X < 220.0 * 0.5);
		TestTrue(FString::Printf(TEXT("A right-hand chaise is at the +X end (%.1f..%.1f cm)"),
			RightChaise.Min.X, RightChaise.Max.X), RightChaise.Min.X > 220.0 * 0.5);
	}

	return true;
}

// ================================================ and whether each of them fits the room it is in

namespace
{
	/**
	 * A box given in the fixture's own local frame, mapped into the room. Spec units.
	 *
	 * Local here is the kit's frame - X from 0 to Width, Y from 0 (front) to Depth (back) - and the
	 * yaw is the RESOLVED one rather than the drawn one. FHFFixturePlacement::FacingYaw turns a run
	 * round until its back faces its anchor wall, so the sofa drawn at 180 against W_Mid_Lower is
	 * built at zero; taking the drawn figure would mirror the L and put the chaise at the wrong end.
	 * It never mattered for a rectangle, whose bounds are the same either way.
	 */
	FBox2D MapLocalBox(const FHFFixture& Fixture, double YawDegrees, const FBox2D& Local)
	{
		const double Radians = FMath::DegreesToRadians(YawDegrees);
		const double CosR = FMath::Cos(Radians);
		const double SinR = FMath::Sin(Radians);
		const FVector2D Half = Fixture.Footprint * 0.5;

		FBox2D Out(ForceInit);
		for (int32 Corner = 0; Corner < 4; ++Corner)
		{
			const double LX = ((Corner == 0 || Corner == 3) ? Local.Min.X : Local.Max.X) - Half.X;
			const double LY = ((Corner < 2) ? Local.Min.Y : Local.Max.Y) - Half.Y;

			Out += Fixture.Position + FVector2D(LX * CosR - LY * SinR, LX * SinR + LY * CosR);
		}
		return Out;
	}

	/**
	 * THE SOFA'S REAL PLAN, which on a sectional is an L and not the rectangle round it.
	 *
	 * Two boxes for a sectional - the straight run and the return - and one for everything else. The
	 * distinction is the whole difference between a clearance test that means something and one that
	 * reports the empty crook of an L as occupied: with the bounding box, the coffee table sitting
	 * exactly where a sectional's table goes reads as 17% inside the sofa.
	 *
	 * Boxes[0] is always the straight run, which is what "how far is it from the seat" is measured to.
	 */
	TArray<FBox2D> SofaPlanBoxes(const FHFHouseSpec& Spec, const FHFFixture& Sofa, double UnitsPerCm)
	{
		const FHFWall* Anchor = nullptr;
		for (const FHFWall& Wall : Spec.Walls)
		{
			if (Wall.Id == Sofa.AnchorWallId)
			{
				Anchor = &Wall;
			}
		}

		const double Yaw = FHFFixturePlacement::FacingYaw(Sofa, Anchor);

		FHFFixture InCentimetres = Sofa;
		InCentimetres.Footprint = Sofa.Footprint / UnitsPerCm;
		InCentimetres.Height = Sofa.Height / UnitsPerCm;

		const FHFSofaParams P = AHFSofaActor::ParamsFor(InCentimetres);

		TArray<FBox2D> Out;

		if (!P.IsSectional())
		{
			Out.Add(MapLocalBox(Sofa, Yaw, FBox2D(FVector2D::ZeroVector, Sofa.Footprint)));
			return Out;
		}

		const double Front = P.MainRunFrontY() * UnitsPerCm;
		const double ChaiseW = P.BuiltChaiseWidth() * UnitsPerCm;
		const double ChaiseX0 = P.bChaiseOnLeft ? 0.0 : Sofa.Footprint.X - ChaiseW;

		Out.Add(MapLocalBox(Sofa, Yaw,
			FBox2D(FVector2D(0.0, Front), FVector2D(Sofa.Footprint.X, Sofa.Footprint.Y))));
		Out.Add(MapLocalBox(Sofa, Yaw,
			FBox2D(FVector2D(ChaiseX0, 0.0), FVector2D(ChaiseX0 + ChaiseW, Front))));

		return Out;
	}

	/** Smallest clear distance from any part of the sofa's real plan to a box. */
	double SofaGapTo(const TArray<FBox2D>& SofaBoxes, const FBox2D& Other)
	{
		double Worst = TNumericLimits<double>::Max();
		for (const FBox2D& Box : SofaBoxes)
		{
			Worst = FMath::Min(Worst, GapBetween(Box, Other));
		}
		return Worst;
	}
}

/**
 * EVERY DESIGN HAS TO FIT THE ROOM IT IS IN, and this is the only layer that can ask.
 *
 * A generator may not go looking for the rest of the house, so nothing in FHFUpholsteryKit knows
 * there is a balcony door in front of the sofa or a TV unit facing it. Swapping a sofa design is
 * exactly the kind of change that breaks a room silently: the object is still a perfectly good sofa,
 * still exactly its drawn box, still watertight - and now standing 7 mm off the coffee table, or with
 * its chaise across the way onto the balcony.
 *
 * All in millimetres, off the spec, against the room's finished faces:
 *
 *     W_South's living-room face      Y =  115      the TV wall
 *     W_Mid_Lower's face              Y = 3542.5    the wall the sofa backs onto
 *     W_West's face                   X =  115
 *     W_Living_Bed2's face            X = 6542.5
 *     D_Foyer's leaf sweep            X =  375..1425
 *     D_Living's leaf sweep           X = 4950..5850
 *     D_Balcony                       X = 1200..3000 in W_South
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFSofaDesignsFitTheLivingRoomTest,
	"HouseForge.Upholstery.SofaDesignsFitTheLivingRoom", HF_TEST_FLAGS)

bool FHFSofaDesignsFitTheLivingRoomTest::RunTest(const FString& Parameters)
{
	constexpr double UnitsPerCm = 10.0;

	constexpr double SouthFace = 115.0;
	constexpr double NorthFace = 3542.5;
	constexpr double WestFace = 115.0;
	constexpr double EastFace = 6542.5;

	// How far back somebody pulls a dining chair to sit down, as the dining clearance test uses it.
	constexpr double PullOut = 350.0;

	for (const FSofaDesignCase& Case : SofaDesignCases)
	{
		const FHFHouseSpec Spec = FHFSampleHouse::Make2BHK(Case.Design);

		const FHFFixture* Sofa = Find(Spec, TEXT("F_Sofa"));
		const FHFFixture* Coffee = Find(Spec, TEXT("F_CoffeeTable"));

		if (!TestTrue(FString::Printf(TEXT("%s: the living room still has a sofa and a coffee table"),
			Case.Name), Sofa != nullptr && Coffee != nullptr))
		{
			continue;
		}

		// ------------------------------------------------- the two places this design is written down
		//
		// The geometry tests build from FSofaDesignCase and the room tests build from the sample
		// house; they have to be describing the same sofa or both pass while measuring different ones.
		TestTrue(FString::Printf(TEXT("%s: the sample house draws the box the design tests build (%.0f x %.0f)"),
			Case.Name, Sofa->Footprint.X, Sofa->Footprint.Y),
			FMath::IsNearlyEqual(Sofa->Footprint.X, Case.Footprint.X * UnitsPerCm, 0.5)
				&& FMath::IsNearlyEqual(Sofa->Footprint.Y, Case.Footprint.Y * UnitsPerCm, 0.5));
		TestTrue(FString::Printf(TEXT("%s: and the height (%.0f)"), Case.Name, Sofa->Height),
			FMath::IsNearlyEqual(Sofa->Height, Case.Height * UnitsPerCm, 0.5));
		TestEqual(FString::Printf(TEXT("%s: and names the design"), Case.Name),
			AsNumber(Sofa->Params.SofaDesign), AsNumber(Case.Design));

		const TArray<FBox2D> SofaBoxes = SofaPlanBoxes(Spec, *Sofa, UnitsPerCm);
		const FBox2D DrawnBox = FootprintBounds(*Sofa);
		const FBox2D CoffeeBounds = FootprintBounds(*Coffee);

		// -------------------------------------------------------------- back on the plaster, in the room
		TestTrue(FString::Printf(TEXT("%s: the sofa's back is on the wall (%.1f mm off)"),
			Case.Name, NorthFace - DrawnBox.Max.Y),
			FMath::IsNearlyEqual(DrawnBox.Max.Y, NorthFace, 1.0));

		TestTrue(FString::Printf(TEXT("%s: the sofa is inside the room (X %.0f..%.0f, Y %.0f..%.0f)"),
			Case.Name, DrawnBox.Min.X, DrawnBox.Max.X, DrawnBox.Min.Y, DrawnBox.Max.Y),
			DrawnBox.Min.X > WestFace && DrawnBox.Max.X < EastFace
				&& DrawnBox.Min.Y > SouthFace && DrawnBox.Max.Y <= NorthFace + 0.5);

		// ------------------------------------------------------------------- clear of both doorways
		//
		// The sofa's back is ON the wall both living-room doors are in, so a sofa across either of
		// them is a sofa across the way out of the flat. Measured against the opening itself, which
		// is also the span the leaf sweeps.
		for (const FHFOpening& Opening : Spec.Openings)
		{
			if (Opening.WallId != FName(TEXT("W_Mid_Lower")) || Opening.Kind != EHFOpeningKind::Door)
			{
				continue;
			}

			const double DoorMin = Opening.OffsetAlongWall - Opening.Width * 0.5;
			const double DoorMax = Opening.OffsetAlongWall + Opening.Width * 0.5;
			const double Gap = FMath::Max(DoorMin - DrawnBox.Max.X, DrawnBox.Min.X - DoorMax);

			TestTrue(FString::Printf(TEXT("%s: the sofa clears '%s' (%.0f mm)"),
				Case.Name, *Opening.Id.ToString(), Gap), Gap > 0.0);
		}

		// ------------------------------------------------- and of everything else standing on the floor
		//
		// Against the L's REAL plan, dining chairs included at the position somebody pulls them to.
		// The tolerance is zero: this is not asking for comfort, it is asking whether the sofa is
		// inside something.
		for (const FHFFixture& Other : Spec.Fixtures)
		{
			if (Other.Id == Sofa->Id || Other.RoomId != FName(TEXT("R_Living"))
				|| Other.IsCeilingMounted() || Other.BaseZ > 45.0
				|| Other.Type == EHFFixtureType::Curtain)
			{
				continue;
			}

			const double Gap = SofaGapTo(SofaBoxes, FootprintBounds(Other));
			TestTrue(FString::Printf(TEXT("%s: the sofa clears '%s' (%.0f mm)"),
				Case.Name, *Other.Id.ToString(), Gap), Gap > 0.0);

			if (Other.Type == EHFFixtureType::Chair)
			{
				const double Pulled = SofaGapTo(SofaBoxes,
					FootprintBounds(Other, PullOutOffset(Other, PullOut)));
				TestTrue(FString::Printf(TEXT("%s: and '%s' pulled out to sit in (%.0f mm)"),
					Case.Name, *Other.Id.ToString(), Pulled), Pulled > 0.0);
			}
		}

		// --------------------------------------------------------- it sits correctly against the TV
		//
		// Two things at once. The seating has to be far enough back to watch a television - 1200 is
		// where a 43 inch screen stops filling the eye - and the strip of floor between them is the
		// route across the living room, which nothing may close. Measured to where the console's
		// drawers actually reach when they are open, not to its carcass.
		{
			double TVFront = SouthFace;
			bool bFoundTV = false;

			for (const FHFFixture& Other : Spec.Fixtures)
			{
				if (Other.Type == EHFFixtureType::TVUnit && Other.RoomId == FName(TEXT("R_Living")))
				{
					TVFront = FMath::Max(TVFront, FootprintBounds(Other).Max.Y);
					bFoundTV = true;
				}
			}

			if (TestTrue(FString::Printf(TEXT("%s: there is a TV run to sit against"), Case.Name), bFoundTV))
			{
				// Drawer travel on F_TVUnit_E, which is what really stands in the room.
				constexpr double DrawerTravel = 230.0;

				double Nearest = TNumericLimits<double>::Max();
				for (const FBox2D& Box : SofaBoxes)
				{
					Nearest = FMath::Min(Nearest, Box.Min.Y - TVFront);
				}

				TestTrue(FString::Printf(TEXT("%s: the seating sits back from the TV (%.0f mm)"),
					Case.Name, Nearest), Nearest > 1200.0);
				TestTrue(FString::Printf(TEXT("%s: and clears its drawers pulled out (%.0f mm)"),
					Case.Name, Nearest - DrawerTravel), Nearest - DrawerTravel > 900.0);

				// THE WALKING ROUTE, which is the band between the coffee table and the TV run. 750 is
				// a corridor somebody passes through; below about 600 they turn sideways.
				const double Route = CoffeeBounds.Min.Y - TVFront;
				TestTrue(FString::Printf(TEXT("%s: there is a route across the room (%.0f mm)"),
					Case.Name, Route), Route > 750.0);
			}
		}

		// --------------------------------------------------------- and it does not foul the balcony
		//
		// D_Balcony is an 1800 slider centred at 2100 in W_South, so it occupies X 1200..3000 and a
		// person walks straight out through it. A chaise across it would be exactly the defect the TV
		// run had before it was split around the same door.
		{
			const FHFOpening* Balcony = nullptr;
			for (const FHFOpening& Opening : Spec.Openings)
			{
				if (Opening.Id == FName(TEXT("D_Balcony")))
				{
					Balcony = &Opening;
				}
			}

			if (TestTrue(FString::Printf(TEXT("%s: the balcony door is still there"), Case.Name),
				Balcony != nullptr))
			{
				const double DoorMin = Balcony->OffsetAlongWall - Balcony->Width * 0.5;
				const double DoorMax = Balcony->OffsetAlongWall + Balcony->Width * 0.5;

				const FBox2D Approach(FVector2D(DoorMin, SouthFace), FVector2D(DoorMax, SouthFace + 900.0));

				for (const FBox2D& Box : SofaBoxes)
				{
					TestFalse(FString::Printf(
						TEXT("%s: no part of the sofa stands in the way onto the balcony"), Case.Name),
						Box.Intersect(Approach));
				}

				TestFalse(FString::Printf(TEXT("%s: nor does the coffee table"), Case.Name),
					CoffeeBounds.Intersect(Approach));
			}
		}

		// ------------------------------------------------------- and the table can be reached from it
		//
		// Boxes[0] is the straight run - the part somebody sits on facing the television - so this is
		// the reach a person actually makes for their tea. 300 is close enough to be in the way;
		// past about 600 a coffee table has stopped serving the sofa.
		{
			const double Reach = GapBetween(SofaBoxes[0], CoffeeBounds);
			TestTrue(FString::Printf(TEXT("%s: the coffee table can be reached from the seat (%.0f mm)"),
				Case.Name, Reach), Reach > 300.0 && Reach < 600.0);
		}
	}

	// ------------------------------------------------ and naming the design it already had changes nothing
	//
	// The invariant that keeps the reference flat the SquareArm case rather than a fifth thing:
	// Make2BHK(SquareArm) is Make2BHK() with a name written on the sofa.
	{
		const FHFHouseSpec Plain = FHFSampleHouse::Make2BHK();
		const FHFHouseSpec Named = FHFSampleHouse::Make2BHK(EHFSofaDesign::SquareArm);

		const FHFFixture* PlainSofa = Find(Plain, TEXT("F_Sofa"));
		const FHFFixture* NamedSofa = Find(Named, TEXT("F_Sofa"));
		const FHFFixture* PlainTable = Find(Plain, TEXT("F_CoffeeTable"));
		const FHFFixture* NamedTable = Find(Named, TEXT("F_CoffeeTable"));

		if (PlainSofa != nullptr && NamedSofa != nullptr && PlainTable != nullptr && NamedTable != nullptr)
		{
			TestTrue(TEXT("Naming SquareArm leaves the sofa exactly where it was"),
				NamedSofa->Position.Equals(PlainSofa->Position, 0.01));
			TestTrue(TEXT("And exactly the size it was"),
				NamedSofa->Footprint.Equals(PlainSofa->Footprint, 0.01));
			TestTrue(TEXT("And the seating group with it"),
				NamedTable->Position.Equals(PlainTable->Position, 0.01));
			TestEqual(TEXT("The reference flat's own sofa never named a design at all"),
				AsNumber(PlainSofa->Params.SofaDesign), AsNumber(EHFSofaDesign::Default));
		}
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
