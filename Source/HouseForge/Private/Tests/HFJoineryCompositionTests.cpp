// Copyright Siddartha G. All Rights Reserved.

#include "HouseForge.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "DynamicMesh/DynamicMeshAABBTree3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "DynamicMesh/MeshTransforms.h"
#include "Geometry/HFJoineryKit.h"
#include "Geometry/HFMeshOps.h"
#include "MeshQueries.h"
#include "Misc/AutomationTest.h"
#include "Model/HFArticulation.h"
#include "Model/HFTypes.h"
#include "Spatial/FastWinding.h"

using namespace UE::Geometry;

#define HF_TEST_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// ---------------------------------------------------------------------------------------------
//
// Composition, which is the one thing per-part testing cannot reach.
//
// Every part of the joinery kit is already tested on its own: the leaf is the declared size, the
// cornice sweeps its section, the drawer travels as far as it says. All of that can pass while the
// assembled cabinet is wrong, because every defect worth catching here is a defect BETWEEN two
// parts that were each individually right - a shelf sharing a plane with the side it sits in, a
// handle left behind on the carcass when its shutter swings, a drawer that comes out through a
// closed door.
//
// So these tests build whole fixtures - a two-bay wardrobe and a kitchen drawer bank - the way a
// fixture generator will, and measure the relationships:
//
//   penetration   how far one solid reaches inside another. Zero for parts that merely touch.
//   gap           closest approach between two surfaces. What a reveal or a running clearance is.
//
// Both are lengths in centimetres, which is what makes them assertable. Neither is a triangle
// count.
//
// ---------------------------------------------------------------------------------------------

namespace
{
	/** 18 mm BWP ply. Every board in these test carcasses, as in every real one. */
	constexpr double Board = 1.8;

	/** A 19 mm finished leaf on a hinge that leaves 1 mm behind it: 20 mm of overlay in all. */
	constexpr double ShutterThickness = 1.9;
	constexpr double ShutterBackClearance = 0.1;

	/**
	 * Below this two surfaces are coincident as far as a depth buffer is concerned.
	 *
	 * 0.1 mm. Two solids closer than this and facing each other z-fight through every frame of a
	 * walkthrough, which is the artefact a still screenshot will never show. It is a floor on
	 * clearances between parts that MOVE relative to each other, where coincidence flickers; parts
	 * in genuine load-bearing contact - a carcass sitting on its plinth - legitimately measure zero
	 * and are asserted for contact instead.
	 */
	constexpr double MinRunningGap = 0.01;

	/**
	 * The most a mounted part may stand off the thing it is mounted to.
	 *
	 * A part further off its carcass than this is not fitted to anything: it is floating in the
	 * position it happens to have been placed at, and it will read as floating the moment the scene
	 * is lit and something casts a shadow under it.
	 */
	constexpr double MaxMountGap = 1.5;

	/** Interpenetration below this is round-off, not geometry. */
	constexpr double PenetrationTolerance = 1e-3;

	double Volume(const FDynamicMesh3& Mesh)
	{
		return TMeshQueries<FDynamicMesh3>::GetVolumeArea(Mesh).X;
	}

	void AppendBoard(FDynamicMesh3& Mesh, const FVector3d& Min, const FVector3d& Max, EHFSurfaceRole Role)
	{
		FHFMeshOps::AppendBox(Mesh, (Min + Max) * 0.5, (Max - Min) * 0.5, 0.0, Role);
	}

	FDynamicMesh3 Placed(const FDynamicMesh3& Mesh, const FTransform& Where)
	{
		FDynamicMesh3 Out = Mesh;
		MeshTransforms::ApplyTransform(Out, FTransformSRT3d(Where), /*bReverseOrientationIfNeeded*/ true);
		return Out;
	}

	/** A part's mesh in assembly space at a given open amount - the pose the component would carry. */
	FDynamicMesh3 Posed(const FHFMeshPart& Part, double OpenAmount)
	{
		FHFPartState State;
		State.PartId = Part.PartId;
		State.PivotTransform = Part.PivotTransform;
		State.Motion = Part.Motion;
		State.OpenAmount = OpenAmount;
		return Placed(Part.Mesh, State.CurrentPose());
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

	/**
	 * Triangles carrying a role, compared by GROUP ID rather than by the role it decodes to.
	 *
	 * RoleForGroup falls back to WallPaint for any group that is not a role, so decoding first would
	 * report a mesh whose groups had been renumbered into nonsense as a mesh full of painted wall -
	 * which is the precise failure being looked for here.
	 */
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

	// ----------------------------------------------------------------------------- measurement

	/**
	 * A solid prepared for containment and distance queries.
	 *
	 * Holds the mesh by value, because the poses being measured are temporaries and an AABB tree
	 * holding a dangling pointer to one fails as garbage numbers rather than as a crash.
	 */
	struct FSolid
	{
		explicit FSolid(FDynamicMesh3 InMesh)
			: Mesh(MoveTemp(InMesh)), Tree(&Mesh, true), Winding(&Tree, true)
		{
		}

		FDynamicMesh3 Mesh;
		FDynamicMeshAABBTree3 Tree;
		TFastWindingTree<FDynamicMesh3> Winding;
	};

	/**
	 * How far the deepest point of Probe reaches inside Solid, in centimetres.
	 *
	 * Winding number rather than surface intersection, because surface intersection cannot tell
	 * genuine contact from penetration: a cornice screwed down onto a carcass top shares a plane
	 * with it and reports an intersection, and so does a drawer driven through a shutter. Only one
	 * of those is a defect. A point exactly on a surface has winding 0.5, so the 0.75 threshold
	 * counts only points that are properly inside, and touching solids measure zero.
	 *
	 * Triangle centroids are probed as well as vertices, which catches the case where two solids
	 * cross without either one's corners entering the other - a shelf run through a partition.
	 */
	double DeepestInside(const FSolid& Solid, const FDynamicMesh3& Probe)
	{
		if (!Solid.Mesh.GetBounds().Intersects(Probe.GetBounds()))
		{
			return 0.0;
		}

		double Deepest = 0.0;

		auto Consider = [&Solid, &Deepest](const FVector3d& Point)
		{
			if (!Solid.Winding.IsInside(Point, 0.75))
			{
				return;
			}
			double DistanceSqr = TNumericLimits<double>::Max();
			Solid.Tree.FindNearestTriangle(Point, DistanceSqr);
			Deepest = FMath::Max(Deepest, FMath::Sqrt(DistanceSqr));
		};

		for (const int32 Vid : Probe.VertexIndicesItr())
		{
			Consider(Probe.GetVertex(Vid));
		}
		for (const int32 Tid : Probe.TriangleIndicesItr())
		{
			Consider(Probe.GetTriCentroid(Tid));
		}

		return Deepest;
	}

	/**
	 * The deeper of the two directions. Symmetric, so the caller cannot get the order wrong.
	 *
	 * Point sampling has one blind spot, and it is worth stating rather than hoping nobody hits it:
	 * two thin boards crossing at a shallow angle can share a sliver of volume without any of
	 * either one's sampled points landing inside the other. That is why every assertion below pairs
	 * this with a positive SurfaceGap. A strictly positive closest approach means the two surfaces
	 * do not touch at all, which rules out crossing outright; this then rules out the only case a
	 * gap cannot see, one solid wholly swallowed by another. Neither alone is enough.
	 */
	double Penetration(const FSolid& A, const FSolid& B)
	{
		return FMath::Max(DeepestInside(A, B.Mesh), DeepestInside(B, A.Mesh));
	}

	/**
	 * Closest approach between two surfaces, in centimetres, or MaxInterest when further apart.
	 *
	 * Capped because the question is almost always "is this at least a reveal and at most a fixing
	 * gap", and an uncapped search over a whole carcass costs far more than that question is worth.
	 */
	double SurfaceGap(FSolid& A, FSolid& B, double MaxInterest = 10.0)
	{
		double Distance = TNumericLimits<double>::Max();
		A.Tree.FindNearestTriangles(B.Tree, nullptr, Distance,
			FDynamicMeshAABBTree3::FQueryOptions(MaxInterest));
		return FMath::Min(Distance, MaxInterest);
	}

	/**
	 * True when two solids are properly apart: not touching, and neither inside the other.
	 *
	 * The pair of measures, taken together, which is what makes "no interpenetration" an assertion
	 * rather than a hope. See Penetration for why neither on its own would do.
	 */
	bool AreClear(FSolid& A, FSolid& B)
	{
		return SurfaceGap(A, B, 1.0) > MinRunningGap && Penetration(A, B) < PenetrationTolerance;
	}

	// ------------------------------------------------------------------------- the test wardrobe

	constexpr double WardrobeWidth = 90.0;
	constexpr double WardrobeDepth = 60.0;
	constexpr double PlinthHeight = 10.0;
	constexpr double CarcassTop = 210.0;
	constexpr double BayWidth = 45.0;

	/**
	 * Fitting gap on a shelf end, in centimetres.
	 *
	 * 1 mm, and it is not slack - it is what stops the shelf end sharing a plane with the carcass
	 * side it sits between. A shelf cut to the exact clear width is the commonest way a generated
	 * cabinet acquires z-fighting, and it is invisible until the thing is lit and the camera moves.
	 * Every real shelf is cut a millimetre under for the same reason it is here: you have to be
	 * able to get it in.
	 */
	constexpr double ShelfEndGap = 0.1;

	/**
	 * A composed wardrobe.
	 *
	 * The sub-assemblies are kept alongside the merged shell because a clearance between two of
	 * them cannot be measured once they are one mesh, and the clearances are the point.
	 */
	struct FWardrobe
	{
		/** Everything fixed, merged: what an element actor's BuildMesh would return. */
		FDynamicMesh3 Shell;

		FDynamicMesh3 Carcass;
		FDynamicMesh3 Plinth;
		FDynamicMesh3 Cornice;
		TArray<FDynamicMesh3> Shelves;

		/** One shutter per bay, each carrying its own handle. */
		TArray<FHFMeshPart> Parts;

		FHFShutterParams Shutter;
		FHFCorniceParams CorniceParams;
		FHFPlinthParams PlinthParams;

		/** Where the closed shutter faces sit, which is what the cornice projects from. */
		double ShutterFaceY = 0.0;
	};

	/**
	 * The handle for one leaf, in that leaf's own local space.
	 *
	 * Not one line of this is conditional on how the leaf is hung, and that is the point. A leaf of
	 * either hand carries its board on +Y of its hinge axis, so the face that looks out of the
	 * cupboard is the plane Y = 0 for both and the facing is a constant - the same constant a
	 * drawer front uses. What handedness does change is which edge the leaf opens from, and the kit
	 * is asked rather than the answer being written out here: a run of shutters is exactly where a
	 * hand-derived flip gets applied to five leaves and forgotten on the sixth.
	 */
	FHFHandleParams MakeLeafHandle(const FHFShutterParams& Shutter)
	{
		FHFHandleParams Handle;
		Handle.Style = EHFHandleStyle::Bar;
		Handle.PanelBox = FHFJoineryKit::ShutterPanelBox(Shutter);
		Handle.Facing = EHFPanelFacing::NegativeY;
		Handle.Edge = FHFJoineryKit::ShutterLeadingEdge(Shutter);
		Handle.EdgeInset = 5.0;
		Handle.BarLength = 12.8;
		Handle.BarDiameter = 1.2;
		Handle.Projection = 3.2;
		Handle.Embed = 0.2;
		return Handle;
	}

	FWardrobe BuildTestWardrobe()
	{
		FWardrobe W;

		const double Z0 = PlinthHeight;
		const double Z1 = CarcassTop;
		const double MidX0 = BayWidth - Board * 0.5;
		const double MidX1 = BayWidth + Board * 0.5;
		const double BackY = WardrobeDepth - Board;

		// ------------------------------------------------------------------------------ carcass
		//
		// Sides full height, bottom and top running between them, back panel behind, and the mid
		// partition butted between bottom and top in front of the back. Butted rather than lapped
		// so no two boards occupy the same space - overlapping boards would put a false volume on
		// the model and give every penetration measurement below a baseline to hide in.

		FHFMeshOps::InitialiseMesh(W.Carcass);
		constexpr EHFSurfaceRole Carc = EHFSurfaceRole::JoineryCarcass;

		AppendBoard(W.Carcass, FVector3d(0.0, 0.0, Z0), FVector3d(Board, WardrobeDepth, Z1), Carc);
		AppendBoard(W.Carcass, FVector3d(WardrobeWidth - Board, 0.0, Z0), FVector3d(WardrobeWidth, WardrobeDepth, Z1), Carc);
		AppendBoard(W.Carcass, FVector3d(Board, 0.0, Z0), FVector3d(WardrobeWidth - Board, WardrobeDepth, Z0 + Board), Carc);
		AppendBoard(W.Carcass, FVector3d(Board, 0.0, Z1 - Board), FVector3d(WardrobeWidth - Board, WardrobeDepth, Z1), Carc);
		AppendBoard(W.Carcass, FVector3d(Board, BackY, Z0 + Board), FVector3d(WardrobeWidth - Board, WardrobeDepth, Z1 - Board), Carc);
		AppendBoard(W.Carcass, FVector3d(MidX0, 0.0, Z0 + Board), FVector3d(MidX1, BackY, Z1 - Board), Carc);

		// ------------------------------------------------------------------------------- plinth

		W.PlinthParams.Width = WardrobeWidth;
		W.PlinthParams.Depth = WardrobeDepth;
		W.PlinthParams.Height = PlinthHeight;

		// The kick is 50 mm measured from the SHUTTER face, which is where anyone looking at the
		// wardrobe measures it from - so the plinth is told how far in front of the carcass those
		// shutters hang. Leaving that at zero puts the panel 50 mm behind the carcass instead, and
		// the kick you can actually see comes out 70.
		W.PlinthParams.ShutterOverlay = ShutterThickness + ShutterBackClearance;
		W.PlinthParams.FrontRecess = 5.0;
		W.PlinthParams.PanelThickness = Board;
		W.PlinthParams.bRightEndExposed = true;
		W.PlinthParams.EndRecess = 5.0;
		W.Plinth = FHFJoineryKit::GeneratePlinth(W.PlinthParams);

		// ------------------------------------------------------------------------------ shelves
		//
		// One stack per bay, sitting in the clear volume with a fitting gap all round. The left bay
		// is shelved out; the right is a single shelf over a hanging rail, which is what half of
		// every Indian wardrobe is.

		const double ClearHeight = (Z1 - Board) - (Z0 + Board);
		const double BayClear = MidX0 - Board;

		for (int32 Bay = 0; Bay < 2; ++Bay)
		{
			FHFShelfStackParams Stack;
			Stack.Width = BayClear - 2.0 * ShelfEndGap;
			Stack.Depth = BackY;
			Stack.Height = ClearHeight;
			Stack.FrontSetback = 1.0;
			Stack.BackClearance = ShelfEndGap;
			Stack.bMidPartitionWhenOverspan = true;
			Stack.ShelfCount = (Bay == 0)
				? FHFJoineryKit::ShelfCountForClearHeight(ClearHeight)
				: 1;
			Stack.bHangingRail = (Bay == 1);

			const double BayX0 = (Bay == 0) ? Board : MidX1;
			W.Shelves.Add(Placed(FHFJoineryKit::GenerateShelfStack(Stack),
				FTransform(FVector(BayX0 + ShelfEndGap, 0.0, Z0 + Board))));
		}

		// ----------------------------------------------------------------------------- shutters

		W.Shutter.ModuleWidth = BayWidth;
		W.Shutter.ModuleHeight = Z1 - Z0;
		W.Shutter.Thickness = ShutterThickness;
		W.Shutter.RevealGap = 0.3;
		W.Shutter.BackClearance = ShutterBackClearance;
		W.Shutter.OpenAngleDegrees = 100.0;
		W.ShutterFaceY = -(W.Shutter.BackClearance + W.Shutter.Thickness);

		for (int32 Bay = 0; Bay < 2; ++Bay)
		{
			// A pair opening from the middle out, which is how a two-bay wardrobe is hung: the
			// leading edges meet, so one handle sits either side of the same shadow line.
			FHFShutterParams Leaf = W.Shutter;
			Leaf.Hinge = (Bay == 0) ? EHFShutterHinge::Left : EHFShutterHinge::Right;

			FHFMeshPart Part = FHFJoineryKit::BuildShutterPart(Leaf,
				FName(*FString::Printf(TEXT("Shutter%d"), Bay)));

			// Into the LEAF's mesh, in the leaf's own space. Anywhere else and the handle stays on
			// the carcass when the leaf swings - which, closed and seen from the front, looks
			// exactly like success.
			FHFJoineryKit::ApplyHandle(Part.Mesh, MakeLeafHandle(Leaf));

			Part.PivotTransform = FHFJoineryKit::ShutterPivotTransform(Leaf)
				* FTransform(FVector(Bay * BayWidth, 0.0, Z0));
			W.Parts.Add(MoveTemp(Part));
		}

		// ------------------------------------------------------------------------------ cornice
		//
		// Anchored on the shutter face plane at the top of the carcass, which is what the cornice's
		// own local space is defined against - so the moulding projects from the shutters it caps
		// rather than from the carcass behind them.

		W.CorniceParams.Width = WardrobeWidth;
		W.CorniceParams.Depth = 8.0;
		W.CorniceParams.Height = 6.0;
		W.CorniceParams.Projection = 2.5;
		W.CorniceParams.Profile = EHFCorniceProfile::Cove;
		W.CorniceParams.ProfileSize = 2.0;
		W.CorniceParams.EdgeBevel = 0.2;

		FHFMeshOps::InitialiseMesh(W.Cornice);
		FHFJoineryKit::AppendCornice(W.Cornice, W.CorniceParams,
			FTransform(FVector(0.0, W.ShutterFaceY, Z1)));
		FHFMeshOps::ApplyWorldScaleUVs(W.Cornice);

		// --------------------------------------------------------------------------------- shell

		FHFMeshOps::InitialiseMesh(W.Shell);
		FHFMeshOps::AppendPreservingRoles(W.Shell, W.Carcass);
		FHFMeshOps::AppendPreservingRoles(W.Shell, W.Plinth);
		FHFMeshOps::AppendPreservingRoles(W.Shell, W.Cornice);
		for (const FDynamicMesh3& Stack : W.Shelves)
		{
			FHFMeshOps::AppendPreservingRoles(W.Shell, Stack);
		}

		return W;
	}

	// ----------------------------------------------------------------------- the test drawer bank

	constexpr double BankModuleWidth = 45.0;
	constexpr double BankCarcassDepth = 58.0;
	constexpr double BankHeight = 72.0;

	struct FDrawerBank
	{
		FDynamicMesh3 Shell;
		FDynamicMesh3 Carcass;
		/** The cabinet members of the runners, which stay behind when the drawers come out. */
		FDynamicMesh3 Mounts;

		/** The drawers themselves, one per module. */
		TArray<FHFMeshPart> Parts;

		/**
		 * The intermediate runner member carrying each drawer, in the same order.
		 *
		 * Kept apart from the drawers on purpose. They are parts in exactly the same sense - they
		 * slide, they have a pivot and a travel limit - but they are not drawers, and a test that
		 * walked one list would measure reveals between a drawer and a runner.
		 */
		TArray<FHFMeshPart> RunnerParts;

		FHFDrawerBankParams Params;
		TArray<double> FrontHeights;
	};

	/** The handle for one drawer front, in that drawer's own local space. */
	FHFHandleParams MakeDrawerHandle(const FHFDrawerParams& Drawer, const FDynamicMesh3& Front)
	{
		FHFHandleParams Handle;
		Handle.Style = EHFHandleStyle::Bar;

		// Taken off the front's own bounds rather than recomputed from the reveal and the back
		// clearance. Two copies of that arithmetic is how a handle ends up a reveal out of place on
		// one drawer in a bank and nowhere else.
		const FAxisAlignedBox3d Box = Front.GetBounds();
		Handle.PanelBox = FBox(FVector(Box.Min.X, Box.Min.Y, Box.Min.Z),
			FVector(Box.Max.X, Box.Max.Y, Box.Max.Z));

		// The front's outward face is its most negative Y: a drawer pulls out along -Y.
		Handle.Facing = EHFPanelFacing::NegativeY;
		Handle.Edge = EHFHandleEdge::Top;
		Handle.EdgeInset = 3.0;
		Handle.BarLength = 25.6;
		Handle.BarDiameter = 1.2;
		Handle.Projection = 3.2;
		Handle.Embed = 0.2;
		return Handle;
	}

	FDrawerBank BuildTestDrawerBank()
	{
		FDrawerBank B;

		B.Params.Drawer.ModuleWidth = BankModuleWidth;
		B.Params.Drawer.CarcassDepth = BankCarcassDepth;
		B.Params.Drawer.CarcassSideThickness = Board;
		B.Params.Drawer.RevealGap = 0.3;
		B.Params.Drawer.BackClearance = 0.1;
		B.Params.Drawer.Extension = EHFDrawerExtension::Full;
		B.Params.BankHeight = BankHeight;
		B.Params.DrawerCount = 3;
		B.Params.GradationRatio = 2.0;

		FHFMeshOps::InitialiseMesh(B.Mounts);

		TArray<FHFMeshPart> All;
		if (!FHFJoineryKit::BuildDrawerBank(B.Params, All, &B.Mounts))
		{
			return B;
		}

		// Two parts per drawer: the drawer, then the intermediate member it rides on. Split by what
		// the part IS - a geared part is a runner member - rather than by position in the list.
		for (FHFMeshPart& Part : All)
		{
			if (Part.Motion.DrivenByPartId.IsNone())
			{
				B.Parts.Add(MoveTemp(Part));
			}
			else
			{
				B.RunnerParts.Add(MoveTemp(Part));
			}
		}
		FHFJoineryKit::GraduateDrawerFronts(B.Params, B.FrontHeights);

		// A handle per front, in that drawer's own mesh so it travels with the drawer. Each front
		// is a different height, so each handle is placed against its own front's bounds.
		for (int32 Index = 0; Index < B.Parts.Num(); ++Index)
		{
			FHFDrawerParams Drawer = B.Params.Drawer;
			Drawer.ModuleHeight = B.FrontHeights[Index] + B.Params.Drawer.RevealGap;

			const FDynamicMesh3 Front = FHFJoineryKit::GenerateDrawerFront(Drawer);
			FHFJoineryKit::ApplyHandle(B.Parts[Index].Mesh, MakeDrawerHandle(Drawer, Front));
		}

		// ------------------------------------------------------------------------------ carcass

		FHFMeshOps::InitialiseMesh(B.Carcass);
		constexpr EHFSurfaceRole Carc = EHFSurfaceRole::JoineryCarcass;
		const double BackY = BankCarcassDepth;

		AppendBoard(B.Carcass, FVector3d(0.0, 0.0, -Board), FVector3d(Board, BankCarcassDepth, BankHeight), Carc);
		AppendBoard(B.Carcass, FVector3d(BankModuleWidth - Board, 0.0, -Board), FVector3d(BankModuleWidth, BankCarcassDepth, BankHeight), Carc);
		AppendBoard(B.Carcass, FVector3d(Board, 0.0, -Board), FVector3d(BankModuleWidth - Board, BankCarcassDepth, 0.0), Carc);
		AppendBoard(B.Carcass, FVector3d(Board, BackY, 0.0), FVector3d(BankModuleWidth - Board, BackY + Board, BankHeight), Carc);

		FHFMeshOps::InitialiseMesh(B.Shell);
		FHFMeshOps::AppendPreservingRoles(B.Shell, B.Carcass);
		FHFMeshOps::AppendPreservingRoles(B.Shell, B.Mounts);

		return B;
	}

	/** Open amounts sampled across a travel. The endpoints alone miss everything in between. */
	const TArray<double>& SweepSamples()
	{
		static const TArray<double> Samples = { 0.0, 0.05, 0.125, 0.25, 0.375, 0.5, 0.625, 0.75, 0.875, 1.0 };
		return Samples;
	}
}

// ---------------------------------------------------------------------------------------------

/**
 * A composed wardrobe, closed: carcass, plinth, shelves, rail, shutters, handles and cornice.
 *
 * The closed pose is where composition errors are cheapest to find and most likely to be shipped,
 * because every screenshot of a wardrobe is of a closed one and every one of these defects looks
 * correct in it.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFWardrobeAssemblyTest, "HouseForge.Joinery.WardrobeAssembly", HF_TEST_FLAGS)

bool FHFWardrobeAssemblyTest::RunTest(const FString& Parameters)
{
	const FWardrobe W = BuildTestWardrobe();

	if (!TestEqual(TEXT("The wardrobe has one shutter per bay"), W.Parts.Num(), 2))
	{
		return false;
	}
	if (!TestTrue(TEXT("The fixed shell has geometry"), W.Shell.TriangleCount() > 0))
	{
		return false;
	}

	// --------------------------------------------------------------------------- the shell itself

	TestTrue(TEXT("The shell is watertight"), FHFMeshOps::IsClosed(W.Shell));
	TestTrue(TEXT("The shell faces outward"), Volume(W.Shell) > 0.0);
	TestTrue(TEXT("No triangle in the shell lost its surface role"), EveryTriangleTagged(W.Shell));

	// The roles survived being merged, which is what the material panel targets. A merge that
	// renumbered polygroups would leave every one of these at zero while the geometry looked right.
	TestTrue(TEXT("The shell carries carcass board"), CountRole(W.Shell, EHFSurfaceRole::JoineryCarcass) > 0);
	TestTrue(TEXT("The shell carries finished faces - the plinth front and the cornice"),
		CountRole(W.Shell, EHFSurfaceRole::ShutterLaminate) > 0);
	TestTrue(TEXT("The shell carries the hanging rail as metal"),
		CountRole(W.Shell, EHFSurfaceRole::MetalHardware) > 0);

	// Merging is additive: nothing was dropped and nothing was counted twice.
	double SubTotal = Volume(W.Carcass) + Volume(W.Plinth) + Volume(W.Cornice);
	for (const FDynamicMesh3& Stack : W.Shelves)
	{
		SubTotal += Volume(Stack);
	}
	TestNearlyEqual(TEXT("The shell is exactly its parts"), Volume(W.Shell), SubTotal, FMath::Abs(SubTotal) * 1e-6);

	// ------------------------------------------------------------------- the fixed sub-assemblies

	FSolid Carcass(W.Carcass);
	FSolid Plinth(W.Plinth);
	FSolid Cornice(W.Cornice);

	TestTrue(TEXT("The plinth does not reach inside the carcass"),
		Penetration(Carcass, Plinth) < PenetrationTolerance);
	TestTrue(TEXT("The cornice does not reach inside the carcass"),
		Penetration(Carcass, Cornice) < PenetrationTolerance);

	// Both are in genuine load-bearing contact, which is the one case where a shared plane is
	// right: the carcass sits ON the plinth and the cornice sits ON the carcass top.
	TestTrue(TEXT("The carcass sits on the plinth rather than above it"),
		SurfaceGap(Carcass, Plinth) < PenetrationTolerance);
	TestTrue(TEXT("The cornice lands on the carcass rather than floating off it"),
		SurfaceGap(Carcass, Cornice) < PenetrationTolerance);

	// The toe kick is what makes the run read as furniture. Measured against the shutter face,
	// which is the plane it is specified from, not against the carcass.
	//
	// Stated as a figure rather than as an expression in the plinth's own parameters. The check this
	// replaces subtracted ShutterFaceY from BOTH sides, which cancels: it reduced to
	// "Plinth.Min.Y == FrontRecess", an identity that holds for any shutter position whatsoever -
	// including a shutter that does not exist - while its comment described a check it did not
	// perform. The plinth was in fact 70 mm behind the shutter face on a 50 mm kick, and this passed.
	const double VisibleKick = W.Plinth.GetBounds().Min.Y - W.ShutterFaceY;
	TestNearlyEqual(TEXT("The plinth is recessed 50 mm behind the shutter face, as asked"),
		VisibleKick, 5.0, 1e-6);

	// And a domain bound either side of it, so the assertion fails if the datum ever moves again
	// rather than tracking it. A real Indian base unit or wardrobe kicks 50 to 80 mm.
	TestTrue(*FString::Printf(TEXT("The kick is one a wardrobe actually has (%.2f cm)"), VisibleKick),
		VisibleKick >= 5.0 - 1e-6 && VisibleKick <= 8.0);

	// The half the wrong datum would break in the other direction: the plinth still has to land on
	// the carcass back, not 20 mm past it and into the wall.
	TestNearlyEqual(TEXT("The plinth's back lands exactly on the carcass back"),
		W.Plinth.GetBounds().Max.Y, W.Carcass.GetBounds().Max.Y, 1e-6);
	TestNearlyEqual(TEXT("The carcass sits on top of the plinth"),
		W.Plinth.GetBounds().Max.Z, W.Carcass.GetBounds().Min.Z, 1e-6);

	// The cornice's whole job: standing proud of the shutters so it throws a shadow line.
	const FHFCorniceParams Co = FHFJoineryKit::SanitiseCornice(W.CorniceParams);
	TestNearlyEqual(TEXT("The cornice stands proud of the shutter face"),
		W.ShutterFaceY - W.Cornice.GetBounds().Min.Y, Co.Projection, 1e-6);
	TestTrue(TEXT("The cornice reaches back over the carcass to be fixed to it"),
		W.Cornice.GetBounds().Max.Y > 0.0);

	// -------------------------------------------------------------------------------- the shelves

	for (int32 Bay = 0; Bay < W.Shelves.Num(); ++Bay)
	{
		FSolid Stack(W.Shelves[Bay]);
		const FString Where = FString::Printf(TEXT("bay %d"), Bay);

		TestTrue(*FString::Printf(TEXT("The shelves in %s do not cut into the carcass"), *Where),
			Penetration(Carcass, Stack) < PenetrationTolerance);

		// The defect this whole test exists for: a shelf cut to the exact clear width shares a
		// plane with the side it sits between and z-fights down its whole length.
		const double Gap = SurfaceGap(Carcass, Stack);
		TestTrue(*FString::Printf(TEXT("The shelves in %s do not share a plane with the carcass (%.4f cm)"), *Where, Gap),
			Gap > MinRunningGap);

		// And are still fitted into it rather than floating in the middle of the bay.
		TestTrue(*FString::Printf(TEXT("The shelves in %s are fitted to the carcass (%.4f cm)"), *Where, Gap),
			Gap < MaxMountGap);
	}

	// -------------------------------------------------------------------------------- the leaves

	const FHFHandleParams Handle = FHFJoineryKit::SanitiseHandle(MakeLeafHandle(W.Shutter));

	TArray<TSharedPtr<FSolid>> ClosedLeaves;
	for (const FHFMeshPart& Part : W.Parts)
	{
		if (!TestTrue(TEXT("Every bay produced a leaf"), Part.Mesh.TriangleCount() > 0))
		{
			return false;
		}
		TestTrue(TEXT("A leaf with a handle on it is still watertight"), FHFMeshOps::IsClosed(Part.Mesh));
		TestTrue(TEXT("No triangle on a leaf lost its surface role"), EveryTriangleTagged(Part.Mesh));

		// The handle is IN the part, so it swings with it. Checked as a role present in the part's
		// own mesh rather than as a position, because a handle that is merely near the leaf would
		// pass a position check and still be left behind on the carcass.
		TestTrue(TEXT("The handle is part of the leaf, not of the carcass"),
			CountRole(Part.Mesh, EHFSurfaceRole::MetalHardware) > 0);

		ClosedLeaves.Add(MakeShared<FSolid>(Posed(Part, 0.0)));
	}

	FSolid& Left = *ClosedLeaves[0];
	FSolid& Right = *ClosedLeaves[1];
	FSolid Shell(W.Shell);

	TestTrue(TEXT("A closed left leaf is clear of everything fixed"),
		AreClear(Shell, Left));
	TestTrue(TEXT("A closed right leaf is clear of everything fixed"),
		AreClear(Shell, Right));

	// The hinge clearance, which is what keeps a closed leaf off the carcass front edges.
	const double LeftGap = SurfaceGap(Carcass, Left);
	TestNearlyEqual(TEXT("The left leaf hangs its back clearance off the carcass"),
		LeftGap, W.Shutter.BackClearance, 1e-3);
	TestNearlyEqual(TEXT("The right leaf hangs its back clearance off the carcass"),
		SurfaceGap(Carcass, Right), W.Shutter.BackClearance, 1e-3);
	TestTrue(TEXT("The leaves are mounted to the carcass, not floating in front of it"),
		LeftGap < MaxMountGap);

	// The shadow line between the pair. Without it the two leaves render as one slab, which is the
	// clearest tell there is that joinery was generated rather than built.
	TestNearlyEqual(TEXT("The pair of leaves leaves exactly one reveal between them"),
		SurfaceGap(Left, Right), W.Shutter.RevealGap, 1e-3);
	TestTrue(TEXT("The leaves do not touch each other"), SurfaceGap(Left, Right) > MinRunningGap);

	// The handles stand proud, on the outside, and the reveal between them is not swallowed by them.
	const FAxisAlignedBox3d LeftBounds = ClosedLeaves[0]->Mesh.GetBounds();
	TestNearlyEqual(TEXT("The handle stands proud of the closed shutter face"),
		W.ShutterFaceY - LeftBounds.Min.Y, Handle.Projection, 1e-3);

	return true;
}

/**
 * The same wardrobe, opened - and opened through every pose on the way, not just to the stop.
 *
 * Endpoints are the poses a generator is most likely to have been checked against by eye, so they
 * are the poses least likely to be wrong. A leaf that clears its neighbour closed and clears it
 * again at 100 degrees can still scythe through it at 40, and nothing but sampling the sweep will
 * say so.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFWardrobeOpensTest, "HouseForge.Joinery.WardrobeOpens", HF_TEST_FLAGS)

bool FHFWardrobeOpensTest::RunTest(const FString& Parameters)
{
	const FWardrobe W = BuildTestWardrobe();
	if (!TestEqual(TEXT("The wardrobe has one shutter per bay"), W.Parts.Num(), 2))
	{
		return false;
	}

	FSolid Shell(W.Shell);

	for (const double Amount : SweepSamples())
	{
		FSolid Leaf0(Posed(W.Parts[0], Amount));
		FSolid Leaf1(Posed(W.Parts[1], Amount));

		// Nothing passes through the carcass at any point in the swing, and nothing brushes it
		// either. This is the property the leaf's thickness-opposite-the-swing convention exists to
		// guarantee, and the only place it can actually be checked is against a carcass.
		//
		// Only clearance is asserted here, not proximity: a leaf on a concealed hinge legitimately
		// stands its whole thickness clear of the carcass once it is open, so "still fitted to the
		// carcass" is a closed-pose property and is checked there. What has to hold at every angle
		// is that the swept transform keeps the handle on its arc, which is checked below.
		TestTrue(*FString::Printf(TEXT("At %.3f open the left leaf is clear of the carcass, shelves and cornice"), Amount),
			AreClear(Shell, Leaf0));
		TestTrue(*FString::Printf(TEXT("At %.3f open the right leaf is clear of the carcass, shelves and cornice"), Amount),
			AreClear(Shell, Leaf1));

		// A pair opening from the middle out passes closest at the start of the swing, where both
		// leading edges are still in the reveal between them.
		const double Between = SurfaceGap(Leaf0, Leaf1);
		TestTrue(*FString::Printf(TEXT("At %.3f open the leaves do not cut into each other"), Amount),
			AreClear(Leaf0, Leaf1));
		TestTrue(*FString::Printf(TEXT("At %.3f open the leaves keep a visible gap (%.4f cm)"), Amount, Between),
			Between > MinRunningGap);
	}

	// A hinge is a rotation about a fixed axis, so a handle screwed to the leaf traces an arc of
	// constant radius. Anything else means the handle is not actually attached to the part.
	for (const FHFMeshPart& Part : W.Parts)
	{
		FVector3d LocalTip = FVector3d::Zero();
		double Furthest = TNumericLimits<double>::Max();
		for (const int32 Vid : Part.Mesh.VertexIndicesItr())
		{
			const FVector3d V = Part.Mesh.GetVertex(Vid);
			if (V.Y < Furthest)
			{
				Furthest = V.Y;
				LocalTip = V;
			}
		}

		FHFPartState State;
		State.PivotTransform = Part.PivotTransform;
		State.Motion = Part.Motion;

		const FVector Hinge = Part.PivotTransform.GetTranslation();
		const FVector Closed = State.PoseAt(0.0).TransformPosition(FVector(LocalTip));
		const FVector Open = State.PoseAt(1.0).TransformPosition(FVector(LocalTip));

		TestTrue(TEXT("Opening the leaf carries its handle with it"), FVector::Dist(Closed, Open) > 10.0);
		TestNearlyEqual(TEXT("The handle stays the same distance from the hinge axis"),
			FVector2D(Open.X - Hinge.X, Open.Y - Hinge.Y).Size(),
			FVector2D(Closed.X - Hinge.X, Closed.Y - Hinge.Y).Size(), 1e-6);
	}

	return true;
}

/**
 * A kitchen drawer bank: graduated fronts, boxes, runners and handles, closed and running out.
 *
 * The bank is where the runner split matters - the drawer half travels and the cabinet half does
 * not - and where the two halves have to stay clear of each other over the whole stroke rather than
 * just at the ends.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFDrawerBankAssemblyTest, "HouseForge.Joinery.DrawerBankAssembly", HF_TEST_FLAGS)

bool FHFDrawerBankAssemblyTest::RunTest(const FString& Parameters)
{
	const FDrawerBank B = BuildTestDrawerBank();

	if (!TestEqual(TEXT("The bank built every drawer it was asked for"), B.Parts.Num(), B.Params.DrawerCount))
	{
		return false;
	}
	if (!TestEqual(TEXT("And an intermediate runner member to carry each one"),
		B.RunnerParts.Num(), B.Params.DrawerCount))
	{
		return false;
	}
	if (!TestTrue(TEXT("The bank produced runner mounts for the carcass"), B.Mounts.TriangleCount() > 0))
	{
		return false;
	}

	// The cabinet halves of the runners are metal, still, after being merged into a carcass that is
	// board. This is the append that silently renumbered polygroups and left them untargetable.
	TestTrue(TEXT("The runner mounts are still metal after being merged into the carcass"),
		CountRole(B.Mounts, EHFSurfaceRole::MetalHardware) > 0);
	TestTrue(TEXT("The merged shell still carries the mounts as metal"),
		CountRole(B.Shell, EHFSurfaceRole::MetalHardware) >= CountRole(B.Mounts, EHFSurfaceRole::MetalHardware));
	TestTrue(TEXT("The merged shell still carries carcass board"),
		CountRole(B.Shell, EHFSurfaceRole::JoineryCarcass) > 0);
	TestTrue(TEXT("No triangle in the bank's shell lost its surface role"), EveryTriangleTagged(B.Shell));

	// A graduated bank, not an evenly divided one - and summing with one reveal each to the bank.
	double Total = 0.0;
	for (int32 Index = 0; Index < B.FrontHeights.Num(); ++Index)
	{
		Total += B.FrontHeights[Index] + B.Params.Drawer.RevealGap;
		if (Index > 0)
		{
			TestTrue(TEXT("A bank never gets shallower towards the floor"),
				B.FrontHeights[Index] >= B.FrontHeights[Index - 1] - UE_KINDA_SMALL_NUMBER);
		}
	}
	TestNearlyEqual(TEXT("The fronts and their reveals fill the bank exactly"), Total, BankHeight, 1e-6);
	TestTrue(TEXT("The bank is graduated rather than evenly divided"),
		B.FrontHeights.Last() > B.FrontHeights[0] + UE_KINDA_SMALL_NUMBER);

	FSolid Shell(B.Shell);

	// ----------------------------------------------------------------------------- the closed bank

	TArray<TSharedPtr<FSolid>> Closed;
	for (int32 Index = 0; Index < B.Parts.Num(); ++Index)
	{
		const FHFMeshPart& Part = B.Parts[Index];

		TestTrue(TEXT("A drawer with a handle on it is still watertight"), FHFMeshOps::IsClosed(Part.Mesh));
		TestTrue(TEXT("No triangle on a drawer lost its surface role"), EveryTriangleTagged(Part.Mesh));
		TestTrue(TEXT("The handle is part of the drawer, not of the carcass"),
			CountRole(Part.Mesh, EHFSurfaceRole::MetalHardware) > 0);

		// A drawer already carries metal in its own runners, so the role alone does not prove a
		// handle went on. The bounds do: nothing else on a drawer reaches out in front of the front.
		FHFDrawerParams Drawer = B.Params.Drawer;
		Drawer.ModuleHeight = B.FrontHeights[Index] + B.Params.Drawer.RevealGap;

		const FDynamicMesh3 Front = FHFJoineryKit::GenerateDrawerFront(Drawer);
		const FHFHandleParams Handle = FHFJoineryKit::SanitiseHandle(MakeDrawerHandle(Drawer, Front));
		TestNearlyEqual(TEXT("The handle stands proud of the drawer front, so it travels with it"),
			Front.GetBounds().Min.Y - Part.Mesh.GetBounds().Min.Y, Handle.Projection, 1e-3);

		// And the swept transform, which is the assertion that "so it travels with it" actually
		// rests on. Bounds moving is not enough on its own: a handle left in the carcass mesh would
		// leave the drawer's own bounds moving exactly as they should, and the drawer would run out
		// from under its handle. Followed on the point that reaches furthest out of the cabinet,
		// which on a drawer - as on a leaf - is the handle's own tip and nothing else.
		FVector3d LocalTip = FVector3d::Zero();
		double Furthest = TNumericLimits<double>::Max();
		for (const int32 Vid : Part.Mesh.VertexIndicesItr())
		{
			const FVector3d V = Part.Mesh.GetVertex(Vid);
			if (V.Y < Furthest)
			{
				Furthest = V.Y;
				LocalTip = V;
			}
		}
		TestNearlyEqual(TEXT("The furthest point out of the bank is the handle's own tip"),
			LocalTip.Y, Front.GetBounds().Min.Y - Handle.Projection, 1e-3);

		FHFPartState Slide;
		Slide.PivotTransform = Part.PivotTransform;
		Slide.Motion = Part.Motion;

		const FVector Shut = Slide.PoseAt(0.0).TransformPosition(FVector(LocalTip));
		const FVector Pulled = Slide.PoseAt(1.0).TransformPosition(FVector(LocalTip));

		TestNearlyEqual(TEXT("The handle rides the drawer out by exactly its travel"),
			Shut.Y - Pulled.Y, Part.Motion.MaxTravelCm, 1e-6);
		TestTrue(TEXT("And straight out, without wandering across the front of the cabinet"),
			FMath::IsNearlyEqual(Shut.X, Pulled.X, 1e-9) && FMath::IsNearlyEqual(Shut.Z, Pulled.Z, 1e-9));

		Closed.Add(MakeShared<FSolid>(Posed(Part, 0.0)));
	}

	for (int32 Index = 0; Index < Closed.Num(); ++Index)
	{
		TestTrue(*FString::Printf(TEXT("Closed, drawer %d is clear of the carcass and the fixed runners"), Index),
			Penetration(Shell, *Closed[Index]) < PenetrationTolerance);

		// Fitted into the cabinet, not floating in it. A drawer's nearest neighbour is the cabinet
		// half of its own runner, half a millimetre away.
		const double Gap = SurfaceGap(Shell, *Closed[Index]);
		TestTrue(*FString::Printf(TEXT("Drawer %d runs clear of the cabinet without sharing a plane with it (%.4f cm)"),
				Index, Gap),
			Gap > MinRunningGap);
		TestTrue(*FString::Printf(TEXT("Drawer %d is mounted in the cabinet (%.4f cm)"), Index, Gap),
			Gap < MaxMountGap);

		if (Index > 0)
		{
			// The shadow line between one front and the next. Without it a bank renders as a single
			// slab with no indication of how many drawers it is.
			const double Between = SurfaceGap(*Closed[Index - 1], *Closed[Index]);
			TestTrue(*FString::Printf(TEXT("Drawers %d and %d do not cut into each other"), Index - 1, Index),
				AreClear(*Closed[Index - 1], *Closed[Index]));
			TestNearlyEqual(TEXT("Consecutive fronts leave exactly one reveal between them"),
				Between, B.Params.Drawer.RevealGap, 1e-3);
		}
	}

	// ------------------------------------------------------------------------------ running out

	// Taken off the part's own motion rather than recomputed, because the part's motion is what the
	// actor will actually drive: a travel the geometry agrees with but the motion does not is a
	// drawer that slides the wrong distance in the editor and the right distance in the test.
	const double Travel = B.Parts[0].Motion.MaxTravelCm;
	TestTrue(TEXT("A full-extension drawer has travel"), Travel > 0.0);
	TestTrue(TEXT("A drawer slides rather than swings"), B.Parts[0].Motion.Type == EHFMotionType::Slide);

	for (const double Amount : SweepSamples())
	{
		TArray<TSharedPtr<FSolid>> Open;
		for (const FHFMeshPart& Part : B.Parts)
		{
			Open.Add(MakeShared<FSolid>(Posed(Part, Amount)));
		}

		for (int32 Index = 0; Index < Open.Num(); ++Index)
		{
			TestTrue(*FString::Printf(TEXT("At %.3f out, drawer %d does not cut into the carcass or its runners"),
					Amount, Index),
				Penetration(Shell, *Open[Index]) < PenetrationTolerance);

			// The runner has to stay engaged the whole way. A drawer that measures further from the
			// cabinet than the runner is long has come off its runner rather than opened.
			const double Gap = SurfaceGap(Shell, *Open[Index]);
			TestTrue(*FString::Printf(TEXT("At %.3f out, drawer %d is still on its runner (%.4f cm)"),
					Amount, Index, Gap),
				Gap < MaxMountGap);
			TestTrue(*FString::Printf(TEXT("At %.3f out, drawer %d does not share a plane with its runner (%.4f cm)"),
					Amount, Index, Gap),
				Gap > MinRunningGap);

			if (Index > 0)
			{
				TestTrue(*FString::Printf(TEXT("At %.3f out, drawers %d and %d stay clear of each other"),
						Amount, Index - 1, Index),
					AreClear(*Open[Index - 1], *Open[Index]));
			}
		}
	}

	// ------------------------------------------------------------------- still ON the runner

	// The assertion that was missing, and the one the SurfaceGap check above cannot make.
	//
	// That check measures the 0.35 cm the members are apart ACROSS the runner, which is the same at
	// every extension whether the two are still overlapping or have slid completely past each other.
	// What matters is the overlap ALONG it: how much of the moving member is still inside the one
	// carrying it. A two-member runner asked to travel its own full length ends up overlapping by
	// exactly nothing - the drawer, its front and its rails cantilevered on air with a
	// zero-thickness sliver of rail left in the cabinet - and the gap stays 0.35 throughout.
	//
	// Measured on the metal, in Y, which is what engagement physically is.
	{
		auto MetalSpanY = [](const FDynamicMesh3& Mesh)
		{
			const int32 Group = FHFMeshOps::GroupForRole(EHFSurfaceRole::MetalHardware);
			FVector2D Span(TNumericLimits<double>::Max(), -TNumericLimits<double>::Max());

			for (const int32 Tid : Mesh.TriangleIndicesItr())
			{
				if (Mesh.GetTriangleGroup(Tid) != Group)
				{
					continue;
				}
				const FIndex3i Tri = Mesh.GetTriangle(Tid);
				for (int32 Corner = 0; Corner < 3; ++Corner)
				{
					const double Y = Mesh.GetVertex(Tri[Corner]).Y;
					Span.X = FMath::Min(Span.X, Y);
					Span.Y = FMath::Max(Span.Y, Y);
				}
			}
			return Span;
		};

		auto Overlap = [](const FVector2D& A, const FVector2D& C)
		{
			return FMath::Max(0.0, FMath::Min(A.Y, C.Y) - FMath::Max(A.X, C.X));
		};

		const double RunnerLength = FHFJoineryKit::SanitiseDrawer(B.Params.Drawer).RunnerLength;
		TestTrue(TEXT("The bank fitted a real runner"), RunnerLength > 0.0);

		// Exactly half the runner is what a three-member slide keeps engaged at its stop.
		const double MinEngagement = RunnerLength * 0.5 - 1e-6;
		const FVector2D Channel = MetalSpanY(B.Mounts);

		for (const double Amount : SweepSamples())
		{
			for (int32 Index = 0; Index < B.Parts.Num(); ++Index)
			{
				// The drawer's rail measured off the BOX rather than off the assembled part. The
				// part also carries a handle, which is metal too and reaches out in front of the
				// front - it would stretch this span forward and hide a rail that had slid right out
				// of its intermediate.
				FHFDrawerParams Drawer = B.Params.Drawer;
				Drawer.ModuleHeight = B.FrontHeights[Index] + B.Params.Drawer.RevealGap;

				FHFMeshPart RailOnly;
				RailOnly.Mesh = FHFJoineryKit::GenerateDrawerBox(Drawer);
				RailOnly.PivotTransform = B.Parts[Index].PivotTransform;
				RailOnly.Motion = B.Parts[Index].Motion;

				const FVector2D Rail = MetalSpanY(Posed(RailOnly, Amount));
				const FVector2D Intermediate = MetalSpanY(Posed(B.RunnerParts[Index], Amount));

				const double OnIntermediate = Overlap(Rail, Intermediate);
				const double InCabinet = Overlap(Intermediate, Channel);

				TestTrue(*FString::Printf(
						TEXT("At %.3f out, drawer %d is still carried by its intermediate (%.2f cm of %.2f)"),
						Amount, Index, OnIntermediate, RunnerLength),
					OnIntermediate >= MinEngagement);
				TestTrue(*FString::Printf(
						TEXT("At %.3f out, drawer %d's intermediate is still in the cabinet (%.2f cm of %.2f)"),
						Amount, Index, InCabinet, RunnerLength),
					InCabinet >= MinEngagement);
			}
		}

		// The gearing itself: the intermediate covers exactly half the drawer's travel, which is what
		// makes both of those hold at once.
		TestNearlyEqual(TEXT("The intermediate travels exactly half as far as the drawer it carries"),
			B.RunnerParts[0].Motion.MaxTravelCm, B.Parts[0].Motion.MaxTravelCm * 0.5, 1e-9);
		TestEqual(TEXT("And is geared to that drawer rather than posed on its own"),
			B.RunnerParts[0].Motion.DrivenByPartId, B.Parts[0].PartId);
		TestTrue(TEXT("The intermediate slides"),
			B.RunnerParts[0].Motion.Type == EHFMotionType::Slide);

		// And it stays out of everything else while doing it.
		for (const double Amount : SweepSamples())
		{
			for (int32 Index = 0; Index < B.RunnerParts.Num(); ++Index)
			{
				FSolid Member(Posed(B.RunnerParts[Index], Amount));
				FSolid Drawer(Posed(B.Parts[Index], Amount));

				TestTrue(*FString::Printf(TEXT("At %.3f out, runner member %d is clear of the carcass"),
						Amount, Index),
					Penetration(Shell, Member) < PenetrationTolerance);
				TestTrue(*FString::Printf(TEXT("At %.3f out, runner member %d is clear of its drawer"),
						Amount, Index),
					AreClear(Member, Drawer));
			}
		}
	}

	// Fully out, the front stands a full travel in front of where it started, and the box has not
	// been driven out through the back panel on the way.
	const FAxisAlignedBox3d ClosedBox = Closed[0]->Mesh.GetBounds();
	const FAxisAlignedBox3d OpenBox = Posed(B.Parts[0], 1.0).GetBounds();
	TestNearlyEqual(TEXT("A drawer opens by exactly its travel"),
		ClosedBox.Min.Y - OpenBox.Min.Y, Travel, 1e-6);
	TestTrue(TEXT("A closed drawer box stays inside the carcass"),
		ClosedBox.Max.Y <= BankCarcassDepth + UE_KINDA_SMALL_NUMBER);

	return true;
}

/**
 * A drawer bank inside a wardrobe, behind a hinged shutter - and the interlock that implies.
 *
 * This is the composition the framework cannot express. AHFArticulatedActor parents every part to
 * the fixed shell, so no part can hang off another part's moving frame, and a drawer's open amount
 * is therefore independent of its shutter's. Driving both from one master open amount runs the
 * drawer out through the closed leaf on the way.
 *
 * Parenting the drawer to the shutter would NOT fix that, and it is worth being precise about why:
 * an internal drawer's runners are screwed to the CARCASS, not to the leaf. A drawer riding on the
 * leaf's frame would swing out of the cabinet with it, which is a different and worse lie than the
 * one being fixed. The real constraint is an ordering - open the shutter, then pull the drawer -
 * and it is recorded here as a measurement rather than a comment: there is a shutter opening beyond
 * which the drawer is clear at every extension, it is strictly between closed and open, and driving
 * both together does not respect it.
 *
 * Two composition rules fall out of this and are asserted, because nothing in the kit states them:
 *
 *   1. An internal drawer's module must be inset clear of the arc its leaf's thickness sweeps, not
 *      merely inside the carcass. The drawer front is always a full-overlay front the width of its
 *      module, so a module filling the carcass opening puts the front's edge inside the swung leaf.
 *   2. The module must be set back in Y behind the closed leaf, and the runners packed out to the
 *      carcass sides, or the runner mounts are screwed to thin air.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFInternalDrawerInterlockTest, "HouseForge.Joinery.InternalDrawerInterlock", HF_TEST_FLAGS)

bool FHFInternalDrawerInterlockTest::RunTest(const FString& Parameters)
{
	constexpr double Z0 = PlinthHeight;
	constexpr double Z1 = CarcassTop;
	constexpr double BackY = WardrobeDepth - Board;

	// A single-bay wardrobe, hung on the left.
	FHFShutterParams Leaf;
	Leaf.ModuleWidth = BayWidth;
	Leaf.ModuleHeight = Z1 - Z0;
	Leaf.Thickness = 1.9;
	Leaf.RevealGap = 0.3;
	Leaf.BackClearance = 0.1;
	Leaf.OpenAngleDegrees = 100.0;
	Leaf.Hinge = EHFShutterHinge::Left;

	FHFMeshPart Shutter = FHFJoineryKit::BuildShutterPart(Leaf, TEXT("Shutter"));
	FHFJoineryKit::ApplyHandle(Shutter.Mesh, MakeLeafHandle(Leaf));
	Shutter.PivotTransform = FHFJoineryKit::ShutterPivotTransform(Leaf) * FTransform(FVector(0.0, 0.0, Z0));

	// Rule 1, as a number. The hinge axis sits half a reveal in from the module edge, and the leaf's
	// thickness swings about it, so the furthest into the bay the leaf ever reaches is at 90
	// degrees. Anything the drawer owns has to start outside that, with a fitting margin.
	constexpr double HingeMargin = 0.5;
	const double LeafSweepX = Leaf.RevealGap * 0.5 + Leaf.Thickness + HingeMargin;

	// Rule 2, as a number: behind the closed leaf's back face, with the same margin.
	const double BankY = Leaf.Thickness + Leaf.BackClearance + HingeMargin;

	const double BankX0 = FMath::Max(Board, LeafSweepX);
	const double BankX1 = BayWidth - Board - HingeMargin;

	FHFDrawerBankParams Bank;
	Bank.Drawer.ModuleWidth = BankX1 - BankX0;
	Bank.Drawer.CarcassDepth = BackY - BankY;
	// The packer carrying the runner out to the carcass side. Not a carcass side of its own - there
	// is none inside a wardrobe bay - but the same job, and the same figure the drawer needs.
	Bank.Drawer.CarcassSideThickness = 1.25;
	Bank.Drawer.RevealGap = 0.3;
	Bank.Drawer.BackClearance = 0.1;
	Bank.BankHeight = 72.0;
	Bank.DrawerCount = 3;

	TArray<FHFMeshPart> Drawers;
	FDynamicMesh3 Mounts;
	FHFMeshOps::InitialiseMesh(Mounts);
	if (!TestTrue(TEXT("A wardrobe bay takes an internal drawer bank"),
		FHFJoineryKit::BuildDrawerBank(Bank, Drawers, &Mounts)))
	{
		return false;
	}

	// Two moving parts per drawer: the drawer, and the intermediate runner member carrying it. Both
	// are measured against the leaf below, because both are inside the bay and both move.
	TestEqual(TEXT("The bank is a drawer and a runner member each"), Drawers.Num(), 2 * Bank.DrawerCount);

	const FTransform BankAnchor(FVector(BankX0, BankY, Z0 + Board));
	for (FHFMeshPart& Drawer : Drawers)
	{
		Drawer.PivotTransform = Drawer.PivotTransform * BankAnchor;
	}

	// ------------------------------------------------------------------------------- the carcass

	FDynamicMesh3 Carcass;
	FHFMeshOps::InitialiseMesh(Carcass);
	constexpr EHFSurfaceRole Carc = EHFSurfaceRole::JoineryCarcass;

	AppendBoard(Carcass, FVector3d(0.0, 0.0, Z0), FVector3d(Board, WardrobeDepth, Z1), Carc);
	AppendBoard(Carcass, FVector3d(BayWidth - Board, 0.0, Z0), FVector3d(BayWidth, WardrobeDepth, Z1), Carc);
	AppendBoard(Carcass, FVector3d(Board, 0.0, Z0), FVector3d(BayWidth - Board, WardrobeDepth, Z0 + Board), Carc);
	AppendBoard(Carcass, FVector3d(Board, 0.0, Z1 - Board), FVector3d(BayWidth - Board, WardrobeDepth, Z1), Carc);
	AppendBoard(Carcass, FVector3d(Board, BackY, Z0 + Board), FVector3d(BayWidth - Board, WardrobeDepth, Z1 - Board), Carc);

	// The packers, which are what rule 2 is really about: the bank is inset from both sides, so the
	// runner mounts land on board rather than on air.
	const FDynamicMesh3 PlacedMounts = Placed(Mounts, BankAnchor);
	const FAxisAlignedBox3d MountBounds = PlacedMounts.GetBounds();
	const double BankZ0 = Z0 + Board;
	AppendBoard(Carcass, FVector3d(Board, BankY, BankZ0), FVector3d(MountBounds.Min.X, BackY, BankZ0 + Bank.BankHeight), Carc);
	AppendBoard(Carcass, FVector3d(MountBounds.Max.X, BankY, BankZ0), FVector3d(BayWidth - Board, BackY, BankZ0 + Bank.BankHeight), Carc);

	// Rule 2, measured: the cabinet halves of the runners are screwed to the packers rather than
	// hanging in the gap the inset opened up. Contact, not penetration - they are fixed to them.
	{
		FSolid CarcassSolid(Carcass);
		FSolid MountSolid(PlacedMounts);
		TestTrue(TEXT("The runner mounts do not cut into the packers"),
			Penetration(CarcassSolid, MountSolid) < PenetrationTolerance);
		TestTrue(TEXT("The runner mounts land on the packers rather than on air"),
			SurfaceGap(CarcassSolid, MountSolid) < PenetrationTolerance);
	}

	FDynamicMesh3 ShellMesh;
	FHFMeshOps::InitialiseMesh(ShellMesh);
	FHFMeshOps::AppendPreservingRoles(ShellMesh, Carcass);
	FHFMeshOps::AppendPreservingRoles(ShellMesh, PlacedMounts);
	FSolid Shell(MoveTemp(ShellMesh));

	// ------------------------------------------------------------------------------ closed, sound

	{
		FSolid ClosedLeaf(Posed(Shutter, 0.0));
		TestTrue(TEXT("The closed leaf is clear of the carcass and the packers"),
			AreClear(Shell, ClosedLeaf));

		for (int32 Index = 0; Index < Drawers.Num(); ++Index)
		{
			FSolid Drawer(Posed(Drawers[Index], 0.0));
			const double BehindLeaf = SurfaceGap(ClosedLeaf, Drawer);
			const double InBay = SurfaceGap(Shell, Drawer);

			TestTrue(*FString::Printf(TEXT("Closed, internal drawer part %d is clear of the carcass and packers"), Index),
				AreClear(Shell, Drawer));
			TestTrue(*FString::Printf(TEXT("Closed, internal drawer part %d sits behind the closed leaf (%.4f cm)"),
					Index, BehindLeaf),
				AreClear(ClosedLeaf, Drawer));
			TestTrue(*FString::Printf(TEXT("Closed, internal drawer part %d is fitted in the bay (%.4f cm)"), Index, InBay),
				InBay < MaxMountGap);
		}
	}

	// --------------------------------------------------------------- the ordering, as a measurement

	// The smallest shutter opening at which the drawer is clear at every extension. Searched rather
	// than asserted at a hard-coded value, so the number tracks the geometry instead of a comment
	// about it.
	auto ClearAtEveryExtension = [&Shutter, &Drawers](double ShutterAmount)
	{
		FSolid Leafy(Posed(Shutter, ShutterAmount));
		for (const double DrawerAmount : SweepSamples())
		{
			for (const FHFMeshPart& Drawer : Drawers)
			{
				FSolid Box(Posed(Drawer, DrawerAmount));
				if (!AreClear(Leafy, Box))
				{
					return false;
				}
			}
		}
		return true;
	};

	TestFalse(TEXT("A closed leaf does not let its drawers out - the interlock is real"),
		ClearAtEveryExtension(0.0));
	TestTrue(TEXT("A fully open leaf lets every drawer all the way out"),
		ClearAtEveryExtension(1.0));

	double Threshold = 1.0;
	for (const double Amount : SweepSamples())
	{
		if (ClearAtEveryExtension(Amount))
		{
			Threshold = Amount;
			break;
		}
	}
	AddInfo(FString::Printf(
		TEXT("Internal drawers need the shutter at least %.3f open before they can run their full travel."),
		Threshold));
	TestTrue(TEXT("The leaf has to be open before its drawers can run - the ordering is real"),
		Threshold > 0.0);

	// And the failure the framework's flat parenting produces: one master open amount drives both
	// at once, which does not respect that ordering. Pinned so it cannot regress into being
	// "fixed" by parenting a drawer to a leaf it is not screwed to.
	bool bMasterSweepIsClear = true;
	for (const double Amount : SweepSamples())
	{
		FSolid Leafy(Posed(Shutter, Amount));
		for (const FHFMeshPart& Drawer : Drawers)
		{
			FSolid Box(Posed(Drawer, Amount));
			if (!AreClear(Leafy, Box))
			{
				bMasterSweepIsClear = false;
			}
		}
	}
	TestFalse(
		TEXT("Driving a leaf and the drawers behind it from one master open amount is not a physical pose"),
		bMasterSweepIsClear);

	return true;
}

#undef HF_TEST_FLAGS

#endif // WITH_DEV_AUTOMATION_TESTS
