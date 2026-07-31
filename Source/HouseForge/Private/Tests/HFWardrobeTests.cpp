// Copyright Siddartha G. All Rights Reserved.

#include "HouseForge.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "DynamicMesh/MeshTransforms.h"
#include "Geometry/HFMeshOps.h"
#include "Geometry/HFWardrobeKit.h"
#include "MeshQueries.h"
#include "Misc/AutomationTest.h"
#include "Model/HFArticulation.h"

using namespace UE::Geometry;

#define HF_TEST_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// ---------------------------------------------------------------------------------------------
//
// The composed wardrobe, as a fixture generator actually produces it.
//
// HouseForge.Joinery.WardrobeAssembly already measures a wardrobe laid up BY HAND in a test file,
// board by board, and it is the reason the clearances in this kit are trustworthy. What it cannot
// measure is whether the thing a production caller builds is that wardrobe. Every figure it checks
// was written twice - once into the test's carcass and once into its shutters - so the two agree by
// construction, and a generator that got the relationship between them wrong would not be visible
// to it at all.
//
// So these tests take FHFWardrobeKit::Build, which is what AHFWardrobeActor calls, and measure the
// relationships nothing chose by hand: where the plinth lands relative to a shutter face that was
// worked out from the leaf's own parameters, how many shelves a hanging bay was given, and which
// project figures actually reached any of it.
//
// ---------------------------------------------------------------------------------------------

namespace
{
	double Volume(const FDynamicMesh3& Mesh)
	{
		return TMeshQueries<FDynamicMesh3>::GetVolumeArea(Mesh).X;
	}

	/** A part's mesh in wardrobe space at a given open amount - the pose its component would carry. */
	FDynamicMesh3 Posed(const FHFMeshPart& Part, double OpenAmount)
	{
		FHFPartState State;
		State.PartId = Part.PartId;
		State.PivotTransform = Part.PivotTransform;
		State.Motion = Part.Motion;
		State.OpenAmount = OpenAmount;

		FDynamicMesh3 Out = Part.Mesh;
		MeshTransforms::ApplyTransform(Out, FTransformSRT3d(State.CurrentPose()), /*bReverseOrientationIfNeeded*/ true);
		return Out;
	}

	bool EveryTriangleTagged(const FDynamicMesh3& Mesh)
	{
		for (const int32 Tid : Mesh.TriangleIndicesItr())
		{
			if (Mesh.GetTriangleGroup(Tid) <= 0)
			{
				return false;
			}
		}
		return Mesh.TriangleCount() > 0;
	}

	/** Triangles carrying a role, compared by GROUP ID rather than by the role it decodes to. */
	int32 CountRole(const FDynamicMesh3& Mesh, EHFSurfaceRole Role)
	{
		const int32 Group = FHFMeshOps::GroupForRole(Role);

		int32 Count = 0;
		for (const int32 Tid : Mesh.TriangleIndicesItr())
		{
			if (Mesh.GetTriangleGroup(Tid) == Group)
			{
				++Count;
			}
		}
		return Count;
	}

	/**
	 * A three-bay hinged wardrobe over a plinth, with a hanging bay at its right-hand end.
	 *
	 * 1800 x 600 x 2100 is the commonest wardrobe in a 2BHK, and at 2100 its clear body height is
	 * 196.4 - the same clear height the hand-laid composition test's hanging bay has, so the two
	 * agree about what a rail in a bay that size gets.
	 */
	FHFWardrobeParams TestWardrobe()
	{
		FHFWardrobeParams P;
		P.Width = 180.0;
		P.Depth = 60.0;
		P.Height = 210.0;
		P.BayCount = 3;
		P.PlinthHeight = 10.0;
		P.bHasLoft = false;
		P.ShelfCount = 0;
		P.bHangingRail = true;
		P.MotionKind = EHFShutterMotion::SideHung;
		P.HandleStyle = EHFHandleStyle::Bar;
		return P;
	}
}

// ---------------------------------------------------------------------------------------------

/**
 * The wardrobe a fixture generator builds, closed.
 *
 * Closed is where composition errors are cheapest to find and most likely to be shipped: every
 * screenshot of a wardrobe is of a closed one, and every defect worth catching here looks correct
 * in it.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFWardrobeCompositionTest, "HouseForge.Joinery.WardrobeComposition", HF_TEST_FLAGS)

bool FHFWardrobeCompositionTest::RunTest(const FString& Parameters)
{
	const FHFWardrobeParams P = TestWardrobe();
	const FHFWardrobeBuild W = FHFWardrobeKit::Build(P);

	if (!TestTrue(TEXT("The wardrobe builds"), W.bValid))
	{
		return false;
	}
	if (!TestEqual(TEXT("A hinged run has one leaf per bay"), W.Parts.Num(), 3))
	{
		return false;
	}

	// --------------------------------------------------------------------------- the shell itself

	TestTrue(TEXT("The shell is watertight"), FHFMeshOps::IsClosed(W.Shell));
	TestTrue(TEXT("The shell faces outward"), Volume(W.Shell) > 0.0);
	TestTrue(TEXT("No triangle in the shell lost its surface role"), EveryTriangleTagged(W.Shell));

	// The roles survived being merged, which is what the material panel targets. A merge that
	// renumbered polygroups would leave every one of these at zero while the geometry looked right.
	TestTrue(TEXT("The shell carries carcass board"),
		CountRole(W.Shell, EHFSurfaceRole::JoineryCarcass) > 0);
	TestTrue(TEXT("The shell carries the finished plinth front"),
		CountRole(W.Shell, EHFSurfaceRole::ShutterLaminate) > 0);
	TestTrue(TEXT("The shell carries a hanging rail as metal"),
		CountRole(W.Shell, EHFSurfaceRole::MetalHardware) > 0);

	// Merging is additive: nothing was dropped and nothing was counted twice.
	double SubTotal = Volume(W.Carcass) + Volume(W.Loft) + Volume(W.Plinth) + Volume(W.Cornice);
	for (const FDynamicMesh3& Stack : W.Shelves)
	{
		SubTotal += Volume(Stack);
	}
	TestNearlyEqual(TEXT("The shell is exactly its parts"), Volume(W.Shell), SubTotal,
		FMath::Abs(SubTotal) * 1e-6);

	// ------------------------------------------------------------------------------- the envelope

	const FAxisAlignedBox3d ShellBounds = W.Shell.GetBounds();

	TestNearlyEqual(TEXT("The run starts at the origin corner"), ShellBounds.Min.X, 0.0, 1e-6);
	TestNearlyEqual(TEXT("The run is as long as it was asked to be"), ShellBounds.Max.X, P.Width, 1e-6);
	TestNearlyEqual(TEXT("Nothing the carcass owns comes in front of the front plane"),
		W.Carcass.GetBounds().Min.Y, 0.0, 1e-6);
	TestNearlyEqual(TEXT("The wardrobe is as deep as it was asked to be"),
		W.Carcass.GetBounds().Max.Y, P.Depth, 1e-6);
	TestNearlyEqual(TEXT("The top of the carcass is the height it was drawn at"),
		W.Carcass.GetBounds().Max.Z, P.Height, 1e-6);
	TestNearlyEqual(TEXT("The wardrobe stands on the floor"), ShellBounds.Min.Z, 0.0, 1e-6);

	// ---------------------------------------------------------------------------------- the base

	TestNearlyEqual(TEXT("The carcass sits on top of the plinth"),
		W.Plinth.GetBounds().Max.Z, W.Carcass.GetBounds().Min.Z, 1e-6);
	TestNearlyEqual(TEXT("The plinth is the height it was asked for"),
		W.Plinth.GetBounds().Max.Z, P.PlinthHeight, 1e-6);

	// The toe kick is what makes the run read as furniture rather than a box on the floor, and it is
	// specified from the SHUTTER face - the plane somebody standing in front of it measures from.
	// Nothing wrote that plane down: it was worked out from the leaf's own thickness and clearance,
	// which is exactly the relationship a hand-laid test wardrobe cannot check.
	const double VisibleKick = W.Plinth.GetBounds().Min.Y - W.ShutterFaceY;
	TestNearlyEqual(TEXT("The kick is measured from the shutter face, not from the carcass"),
		VisibleKick, W.PlinthParams.FrontRecess, 1e-6);
	TestTrue(*FString::Printf(TEXT("The kick is one a wardrobe actually has (%.2f cm)"), VisibleKick),
		VisibleKick >= 5.0 - 1e-6 && VisibleKick <= 8.0);

	TestNearlyEqual(TEXT("The plinth's back lands on the carcass back"),
		W.Plinth.GetBounds().Max.Y, W.Carcass.GetBounds().Max.Y, 1e-6);

	// -------------------------------------------------------------------------------- the leaves

	TArray<FAxisAlignedBox3d> Closed;
	for (const FHFMeshPart& Part : W.Parts)
	{
		if (!TestTrue(TEXT("Every bay produced a leaf"), Part.Mesh.TriangleCount() > 0))
		{
			return false;
		}

		TestTrue(TEXT("A leaf with a handle on it is still watertight"), FHFMeshOps::IsClosed(Part.Mesh));
		TestTrue(TEXT("No triangle on a leaf lost its surface role"), EveryTriangleTagged(Part.Mesh));

		// The handle is IN the part, so it swings with it. Checked as a role present in the part's own
		// mesh rather than as a position, because a handle merely NEAR the leaf would pass a position
		// check and still be left behind on the carcass when the leaf opened.
		TestTrue(TEXT("The handle is part of the leaf, not of the carcass"),
			CountRole(Part.Mesh, EHFSurfaceRole::MetalHardware) > 0);

		TestTrue(TEXT("A leaf swings on a hinge"), Part.Motion.Type == EHFMotionType::Hinge);
		TestTrue(TEXT("A leaf opens rather than revolving"), Part.Motion.Opens());

		Closed.Add(Posed(Part, 0.0).GetBounds());
	}

	TestEqual(TEXT("The leaves are named in bay order"), W.Parts[0].PartId, FHFWardrobeKit::ShutterPartId(0));
	TestEqual(TEXT("The last leaf is named for the last bay"), W.Parts[2].PartId, FHFWardrobeKit::ShutterPartId(2));

	// The hinge clearance, which is what keeps a closed leaf off the carcass front edges.
	for (int32 Bay = 0; Bay < Closed.Num(); ++Bay)
	{
		TestNearlyEqual(*FString::Printf(TEXT("Leaf %d hangs its back clearance off the carcass"), Bay),
			-Closed[Bay].Max.Y, W.ShutterParams.BackClearance, 1e-3);
	}

	// The shadow line between one leaf and the next. Without it a run of shutters renders as one
	// unbroken slab, which is the clearest tell there is that joinery was generated rather than built.
	for (int32 Bay = 1; Bay < Closed.Num(); ++Bay)
	{
		const double Reveal = Closed[Bay].Min.X - Closed[Bay - 1].Max.X;
		TestNearlyEqual(*FString::Printf(TEXT("Leaves %d and %d leave exactly one reveal between them"),
			Bay - 1, Bay), Reveal, W.ShutterParams.RevealGap, 1e-3);
	}

	// And the run of leaves covers the run of carcass, half a reveal in at each end.
	TestNearlyEqual(TEXT("The leaves start half a reveal in from the end of the run"),
		Closed[0].Min.X, W.ShutterParams.RevealGap * 0.5, 1e-3);
	TestNearlyEqual(TEXT("The leaves finish half a reveal in from the other end"),
		Closed.Last().Max.X, P.Width - W.ShutterParams.RevealGap * 0.5, 1e-3);

	// -------------------------------------------------------------------------------- the interior

	if (!TestEqual(TEXT("Every bay was shelved or hung"), W.ShelfParams.Num(), 3))
	{
		return false;
	}

	TestEqual(TEXT("Half the bays hang, taken from the right-hand end"), W.HangingBayCount, 1);
	TestTrue(TEXT("The right-hand bay got its rail"), W.ShelfParams[2].bHangingRail);
	TestFalse(TEXT("A shelved bay has no rail"), W.ShelfParams[0].bHangingRail);
	TestTrue(TEXT("A shelved bay actually has shelves"), W.ShelfParams[0].ShelfCount > 0);

	// The rule that makes MinHangingClearance mean something: the hanging bay is given the most
	// shelves that still leave a garment room to hang, which in a 196 cm bay is one.
	TestTrue(*FString::Printf(TEXT("The hanging bay is shelved no further than the rail allows (%d over %d)"),
		W.ShelfParams[2].ShelfCount, W.ShelfParams[0].ShelfCount),
		W.ShelfParams[2].ShelfCount < W.ShelfParams[0].ShelfCount);

	const double Compartment =
		(W.ShelfParams[2].Height - W.ShelfParams[2].ShelfCount * W.ShelfParams[2].ShelfThickness)
		/ static_cast<double>(W.ShelfParams[2].ShelfCount + 1);

	TestTrue(*FString::Printf(TEXT("Something can actually hang under the rail (%.1f cm)"),
		Compartment - W.ShelfParams[2].RailDrop),
		Compartment - W.ShelfParams[2].RailDrop >= W.ShelfParams[2].MinHangingClearance - 1e-6);

	// Every stack sits inside the bay it belongs to, off the boards either side of it: a shelf cut to
	// the exact clear width shares a plane with the carcass side and z-fights down its whole length.
	for (int32 Bay = 0; Bay < W.Shelves.Num(); ++Bay)
	{
		const FBox Clear = FHFJoineryKit::CarcassBayClearVolume(W.CarcassParams, Bay);
		const FAxisAlignedBox3d Stack = W.Shelves[Bay].GetBounds();

		TestNearlyEqual(*FString::Printf(TEXT("Bay %d's shelves are set in off the left board"), Bay),
			Stack.Min.X - Clear.Min.X, FHFWardrobeKit::ShelfEndGap, 1e-6);
		TestNearlyEqual(*FString::Printf(TEXT("Bay %d's shelves are set in off the right board"), Bay),
			Clear.Max.X - Stack.Max.X, FHFWardrobeKit::ShelfEndGap, 1e-6);
		TestTrue(*FString::Printf(TEXT("Bay %d's shelves stay behind the carcass front plane"), Bay),
			Stack.Min.Y > 0.0);
		TestTrue(*FString::Printf(TEXT("Bay %d's shelves stay clear of the back panel"), Bay),
			Stack.Max.Y <= W.CarcassParams.BackFaceY() + 1e-6);
	}

	return true;
}

/**
 * A wardrobe whose leaves slide rather than swing.
 *
 * The commonest wardrobe in a modern Indian flat, and the one that catches a composer out: a sliding
 * leaf PASSES its neighbour instead of swinging clear of it, so a run cannot have one leaf per bay.
 * Four sliding leaves over four bays could never open - every one would have to move somewhere
 * already occupied - and the wardrobe would look perfectly correct until somebody tried.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFWardrobeSlidingRunTest, "HouseForge.Joinery.WardrobeSlidingRun", HF_TEST_FLAGS)

bool FHFWardrobeSlidingRunTest::RunTest(const FString& Parameters)
{
	FHFWardrobeParams P = TestWardrobe();
	P.BayCount = 4;
	P.Width = 240.0;
	P.MotionKind = EHFShutterMotion::Sliding;

	const FHFWardrobeBuild W = FHFWardrobeKit::Build(P);
	if (!TestTrue(TEXT("The sliding wardrobe builds"), W.bValid))
	{
		return false;
	}

	// Two leaves over four bays. The carcass is still divided at BayCount, because what is behind the
	// leaves has nothing to do with how many of them there are.
	if (!TestEqual(TEXT("A sliding run is two leaves whatever the carcass is divided into"),
		W.Parts.Num(), 2))
	{
		return false;
	}
	TestEqual(TEXT("The carcass keeps its four bays"), W.CarcassParams.Bays(), 4);

	// The bar this wardrobe asked for was routed instead, because 32 mm of handle does not fit
	// through 10 mm of running clearance. Read back off the sanitised parameters, which is where a
	// caller can find out what it actually got.
	TestEqual(TEXT("An applied handle on a sliding run is routed instead"),
		FHFWardrobeKit::Sanitise(P).HandleStyle, EHFHandleStyle::HandlelessGroove);

	for (const FHFMeshPart& Part : W.Parts)
	{
		TestTrue(TEXT("A sliding leaf slides"), Part.Motion.Type == EHFMotionType::Slide);
		TestTrue(TEXT("A sliding leaf has somewhere to go"), FMath::Abs(Part.Motion.MaxTravelCm) > 0.0);
	}

	const FAxisAlignedBox3d Left = Posed(W.Parts[0], 0.0).GetBounds();
	const FAxisAlignedBox3d Right = Posed(W.Parts[1], 0.0).GetBounds();

	// THE DIFFERENCE BETWEEN A SLIDING RUN AND A HINGED ONE, measured. Hinged leaves are separated by
	// a reveal; sliding leaves LAP on separate tracks, because a reveal between two sliding leaves
	// would be a hole straight into the wardrobe.
	TestTrue(*FString::Printf(TEXT("The leaves lap rather than leaving a gap (%.2f cm)"),
		Left.Max.X - Right.Min.X),
		Left.Max.X > Right.Min.X);

	// And they are on different tracks, so the lap is not two leaves sharing a volume.
	TestTrue(TEXT("The leaves run on separate tracks"),
		Left.Min.Y > Right.Max.Y || Right.Min.Y > Left.Max.Y);

	// The whole run is covered when both are shut, which is what a closed wardrobe is.
	TestTrue(TEXT("The pair covers the run when closed"),
		FMath::Min(Left.Min.X, Right.Min.X) <= 1.0
		&& FMath::Max(Left.Max.X, Right.Max.X) >= P.Width - 1.0);

	// Opened, the leading leaf has actually moved somewhere.
	const FAxisAlignedBox3d Open = Posed(W.Parts[0], 1.0).GetBounds();
	TestTrue(*FString::Printf(TEXT("A leaf at its stop has travelled (%.1f cm)"), Open.Min.X - Left.Min.X),
		FMath::Abs(Open.Min.X - Left.Min.X) > 1.0);

	return true;
}

/**
 * The loft over a wardrobe, and the one that is not built.
 *
 * A loft is a separate box standing on the body, which is how one is made: a loft unit is built on
 * its own and lifted on afterwards. What matters here is that asking for a loft too shallow to hold
 * anything gets an answer rather than a 24 mm slot - the reference flat's second wardrobe asks for
 * exactly that, because its LoftHeight was never stated and the struct default is a figure in the
 * wrong units.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFWardrobeLoftTest, "HouseForge.Joinery.WardrobeLoft", HF_TEST_FLAGS)

bool FHFWardrobeLoftTest::RunTest(const FString& Parameters)
{
	FHFWardrobeParams P = TestWardrobe();
	P.Height = 240.0;
	P.bHasLoft = true;
	P.LoftHeight = 50.0;

	const FHFWardrobeBuild W = FHFWardrobeKit::Build(P);
	if (!TestTrue(TEXT("The wardrobe with a loft builds"), W.bValid))
	{
		return false;
	}

	TestTrue(TEXT("The loft is a carcass of its own"), W.Loft.TriangleCount() > 0);
	TestTrue(TEXT("The loft is watertight"), FHFMeshOps::IsClosed(W.Loft));

	// Standing ON the body, not overlapping it. Two boards in contact is what a loft unit set on top
	// of a wardrobe really is; two boards in the same space would put a false volume on the model.
	TestNearlyEqual(TEXT("The loft stands on the body's top"),
		W.Loft.GetBounds().Min.Z, W.Carcass.GetBounds().Max.Z, 1e-6);
	TestNearlyEqual(TEXT("The loft reaches the height the wardrobe was drawn at"),
		W.Loft.GetBounds().Max.Z, P.Height, 1e-6);

	// One leaf per bay below, and one per bay above, each its own part.
	if (!TestEqual(TEXT("The loft is shuttered as well as the body"), W.Parts.Num(), 6))
	{
		return false;
	}
	TestEqual(TEXT("The loft's leaves are named for the loft"), W.Parts[3].PartId, FHFWardrobeKit::LoftPartId(0));

	const FAxisAlignedBox3d Body = Posed(W.Parts[0], 0.0).GetBounds();
	const FAxisAlignedBox3d Loft = Posed(W.Parts[3], 0.0).GetBounds();
	TestTrue(TEXT("The loft's leaf is above the body's"), Loft.Min.Z > Body.Max.Z);

	// ------------------------------------------------------------------- the loft that is not built

	// 60 mm. It is the default on FHFFixtureParams, which is a centimetre figure on a struct the spec
	// carries in millimetres, so a drawing that asks for a loft without saying how deep gets this.
	FHFWardrobeParams Shallow = P;
	Shallow.LoftHeight = 6.0;

	const FHFWardrobeParams Sanitised = FHFWardrobeKit::Sanitise(Shallow);
	TestFalse(TEXT("A loft too shallow to hold anything is refused, not built as a slot"),
		Sanitised.bHasLoft);

	const FHFWardrobeBuild NoLoft = FHFWardrobeKit::Build(Shallow);
	TestEqual(TEXT("A refused loft leaves no loft carcass"), NoLoft.Loft.TriangleCount(), 0);
	TestEqual(TEXT("A refused loft leaves no loft leaves"), NoLoft.Parts.Num(), 3);
	TestNearlyEqual(TEXT("The body takes back the height the loft would have had"),
		NoLoft.Carcass.GetBounds().Max.Z, P.Height, 1e-6);

	return true;
}

/**
 * The project's construction figures, reaching a wardrobe.
 *
 * About thirty settings on the Joinery page were inert for one reason: nothing composed the kit, so
 * nothing carried a figure into it. The page even said so - its category is called "takes effect when
 * fixtures land". This is that landing, and each of these four is a control an artist can turn that
 * now changes what is built.
 *
 * Measured on the GEOMETRY rather than on the parameter structs. A figure copied faithfully into a
 * params struct that no generator reads is exactly the failure this is here to catch.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFWardrobeFiguresReachTheGeometryTest,
	"HouseForge.Joinery.WardrobeFiguresReachTheGeometry", HF_TEST_FLAGS)

bool FHFWardrobeFiguresReachTheGeometryTest::RunTest(const FString& Parameters)
{
	const FHFWardrobeParams Base = TestWardrobe();
	const FHFWardrobeBuild Stock = FHFWardrobeKit::Build(Base);

	if (!TestTrue(TEXT("The reference wardrobe builds"), Stock.bValid))
	{
		return false;
	}

	// ------------------------------------------------------------------------- board thickness

	{
		FHFWardrobeParams P = Base;
		P.Joinery.CarcassBoardThickness = 2.5;

		const FHFWardrobeBuild W = FHFWardrobeKit::Build(P);
		const FBox StockBay = FHFJoineryKit::CarcassBayClearVolume(Stock.CarcassParams, 0);
		const FBox ThickBay = FHFJoineryKit::CarcassBayClearVolume(W.CarcassParams, 0);

		TestNearlyEqual(TEXT("A thicker board is the board the carcass is cut from"),
			W.CarcassParams.BoardThickness, 2.5, 1e-9);
		TestTrue(*FString::Printf(TEXT("A thicker carcass leaves less clear width inside (%.2f then %.2f)"),
			StockBay.Max.X - StockBay.Min.X, ThickBay.Max.X - ThickBay.Min.X),
			(ThickBay.Max.X - ThickBay.Min.X) < (StockBay.Max.X - StockBay.Min.X) - 1e-6);
		TestTrue(TEXT("A thicker carcass is more board"), Volume(W.Carcass) > Volume(Stock.Carcass));
	}

	// --------------------------------------------------------------------------- shutter reveal

	{
		FHFWardrobeParams P = Base;
		P.Joinery.ShutterRevealGap = 1.0;

		const FHFWardrobeBuild W = FHFWardrobeKit::Build(P);
		if (!TestEqual(TEXT("The wide-reveal wardrobe still has three leaves"), W.Parts.Num(), 3))
		{
			return false;
		}

		const FAxisAlignedBox3d First = Posed(W.Parts[0], 0.0).GetBounds();
		const FAxisAlignedBox3d Second = Posed(W.Parts[1], 0.0).GetBounds();

		// The shadow line an artist actually sees, measured between two leaves rather than read back
		// off the struct that was set.
		TestNearlyEqual(TEXT("The reveal on the page is the gap between the leaves"),
			Second.Min.X - First.Max.X, 1.0, 1e-3);
	}

	// ---------------------------------------------------------------------------- plinth height

	{
		FHFWardrobeParams P = Base;
		P.Joinery.PlinthHeight = 15.0;
		P.PlinthHeight = 0.0;   // not stated on the drawing, so the project's figure stands

		const FHFWardrobeBuild W = FHFWardrobeKit::Build(P);

		TestNearlyEqual(TEXT("The plinth is the height the project asked for"),
			W.Plinth.GetBounds().Max.Z, 15.0, 1e-6);
		TestNearlyEqual(TEXT("The carcass moved up onto it"),
			W.Carcass.GetBounds().Min.Z, 15.0, 1e-6);
		TestNearlyEqual(TEXT("The wardrobe is still the height it was drawn at"),
			W.Carcass.GetBounds().Max.Z, Base.Height, 1e-6);
	}

	// ----------------------------------------------------------------------- hanging clearance

	{
		// The setting the user asked for by name, and until fixtures landed there was nothing for it
		// to change. At 90 the hanging bay takes one shelf over the rail; raise it and the shelf has to
		// go, because compartments are equal and the rail is in the top one.
		FHFWardrobeParams P = Base;
		P.Joinery.MinHangingClearance = 150.0;

		const FHFWardrobeBuild W = FHFWardrobeKit::Build(P);
		if (!TestEqual(TEXT("Every bay was still laid out"), W.ShelfParams.Num(), 3))
		{
			return false;
		}

		TestTrue(TEXT("The hanging bay still has its rail"), W.ShelfParams[2].bHangingRail);
		TestTrue(*FString::Printf(TEXT("Raising the clearance took the shelf out of the hanging bay (%d then %d)"),
			Stock.ShelfParams[2].ShelfCount, W.ShelfParams[2].ShelfCount),
			W.ShelfParams[2].ShelfCount < Stock.ShelfParams[2].ShelfCount);

		// Every other bay is untouched: the clearance is about hanging, not about shelving.
		TestEqual(TEXT("A shelved bay is unaffected by the hanging clearance"),
			W.ShelfParams[0].ShelfCount, Stock.ShelfParams[0].ShelfCount);
	}

	{
		// Past what the bay can give at all, and the honest answer is no rail - which is how a caller
		// tells "no room to hang" from "not asked for".
		FHFWardrobeParams P = Base;
		P.Joinery.MinHangingClearance = 250.0;

		const FHFWardrobeBuild W = FHFWardrobeKit::Build(P);

		TestFalse(TEXT("A clearance the bay cannot give refuses the rail"), W.ShelfParams[2].bHangingRail);
		TestEqual(TEXT("And then there is no rail in the wardrobe at all"),
			CountRole(W.Shell, EHFSurfaceRole::MetalHardware), 0);
	}

	// ---------------------------------------------------------------------------- module width

	{
		// The figure that divides a run when the drawing did not count the shutters. Exercised through
		// the same path AHFWardrobeActor::ApplyFixture takes.
		FHFWardrobeParams P = Base;
		P.Joinery.ShutterModuleWidth = 60.0;
		P.BayCount = FMath::Max(1, FMath::RoundToInt32(P.Width / P.Joinery.ShutterModuleWidth));

		const FHFWardrobeBuild W = FHFWardrobeKit::Build(P);
		TestEqual(TEXT("A 1800 run at a 600 module is three bays"), W.CarcassParams.Bays(), 3);
	}

	return true;
}

#undef HF_TEST_FLAGS

#endif // WITH_DEV_AUTOMATION_TESTS
