// Copyright Siddartha G. All Rights Reserved.

#include "HouseForge.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "DynamicMesh/DynamicMeshAABBTree3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Geometry/HFJoineryKit.h"
#include "Geometry/HFMeshOps.h"
#include "MeshQueries.h"
#include "Misc/AutomationTest.h"
#include "Model/HFTypes.h"
#include "Spatial/FastWinding.h"

using namespace UE::Geometry;

#define HF_TEST_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// ---------------------------------------------------------------------------------------------
//
// The carcass: the datum every other part of the kit is specified against.
//
// Until this generator existed, that datum lived only inside HFJoineryCompositionTests, which laid a
// carcass up by hand out of boards. So the whole kit was specified against a frame no production
// caller could produce. These tests hold the generated carcass to that hand-laid one - not
// approximately, but board for board - because everything the composition tests measure (a shelf's
// fitting gap, a shutter's back clearance, a drawer front's inset) is measured against it.
//
// Nothing here asserts a triangle count. A carcass is asserted as the solid it is: its bounds, the
// volume of board in it, and whether its boards occupy each other's space.
//
// ---------------------------------------------------------------------------------------------

namespace
{
	/** 18 mm BWP ply, the same board the composition tests build their carcass from. */
	constexpr double Board = 1.8;

	/** The composed wardrobe's own figures, so the two carcasses can be compared directly. */
	constexpr double WardrobeWidth = 90.0;
	constexpr double WardrobeDepth = 60.0;
	constexpr double CarcassHeight = 200.0;

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

	/** The carcass exactly as HFJoineryCompositionTests lays it up, at Z = 0 rather than on a plinth. */
	FDynamicMesh3 HandLaidCarcass()
	{
		FDynamicMesh3 Mesh;
		FHFMeshOps::InitialiseMesh(Mesh);

		constexpr EHFSurfaceRole Carc = EHFSurfaceRole::JoineryCarcass;
		constexpr double W = WardrobeWidth;
		constexpr double D = WardrobeDepth;
		constexpr double H = CarcassHeight;
		constexpr double BackY = D - Board;
		constexpr double MidX0 = W * 0.5 - Board * 0.5;
		constexpr double MidX1 = W * 0.5 + Board * 0.5;

		AppendBoard(Mesh, FVector3d(0.0, 0.0, 0.0), FVector3d(Board, D, H), Carc);
		AppendBoard(Mesh, FVector3d(W - Board, 0.0, 0.0), FVector3d(W, D, H), Carc);
		AppendBoard(Mesh, FVector3d(Board, 0.0, 0.0), FVector3d(W - Board, D, Board), Carc);
		AppendBoard(Mesh, FVector3d(Board, 0.0, H - Board), FVector3d(W - Board, D, H), Carc);
		AppendBoard(Mesh, FVector3d(Board, BackY, Board), FVector3d(W - Board, D, H - Board), Carc);
		AppendBoard(Mesh, FVector3d(MidX0, 0.0, Board), FVector3d(MidX1, BackY, H - Board), Carc);

		return Mesh;
	}

	FHFCarcassParams WardrobeCarcassParams()
	{
		FHFCarcassParams P;
		P.Width = WardrobeWidth;
		P.Depth = WardrobeDepth;
		P.Height = CarcassHeight;
		P.BoardThickness = Board;
		P.BackThickness = Board;
		P.BayCount = 2;
		return P;
	}

	/** A solid prepared for containment queries. Holds its mesh by value; the poses are temporaries. */
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

	/** How far the deepest point of Probe reaches inside Solid, in centimetres. */
	double DeepestInside(const FSolid& Solid, const FDynamicMesh3& Probe)
	{
		if (!Solid.Mesh.GetBounds().Intersects(Probe.GetBounds()))
		{
			return 0.0;
		}

		double Deepest = 0.0;

		auto Consider = [&Solid, &Deepest](const FVector3d& Point)
		{
			// A point exactly on a surface has winding 0.5, so 0.75 counts only points properly
			// inside - which is what lets butted boards touch and measure zero.
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

	bool EveryTriangleTagged(const FDynamicMesh3& Mesh, EHFSurfaceRole Role)
	{
		const int32 Group = FHFMeshOps::GroupForRole(Role);
		for (const int32 Tid : Mesh.TriangleIndicesItr())
		{
			if (Mesh.GetTriangleGroup(Tid) != Group)
			{
				return false;
			}
		}
		return Mesh.TriangleCount() > 0;
	}

	/** Whether the mesh carries a UV overlay with an element for every triangle corner. */
	bool HasCompleteUVs(const FDynamicMesh3& Mesh)
	{
		const FDynamicMeshUVOverlay* UVs = Mesh.HasAttributes() ? Mesh.Attributes()->PrimaryUV() : nullptr;
		if (UVs == nullptr)
		{
			return false;
		}

		for (const int32 Tid : Mesh.TriangleIndicesItr())
		{
			if (!UVs->IsSetTriangle(Tid))
			{
				return false;
			}
		}
		return Mesh.TriangleCount() > 0;
	}
}

/**
 * The generated carcass is the hand-laid one.
 *
 * The load-bearing test of this generator, and the reason it was lifted rather than invented. Every
 * clearance HFJoineryCompositionTests measures - the shelf's fitting gap, the shutter's back
 * clearance, the plinth landing under the sides - is measured against a carcass built by hand in
 * that file. If this generator laid its boards up any differently, those tests would carry on
 * passing while measuring a carcass no production caller ever builds.
 *
 * Compared as solids: same bounds, same volume of board, and neither reaching inside the other.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFCarcassMatchesHandLaidTest,
	"HouseForge.Joinery.CarcassMatchesTheHandLaidOne", HF_TEST_FLAGS)

bool FHFCarcassMatchesHandLaidTest::RunTest(const FString& Parameters)
{
	const FDynamicMesh3 Generated = FHFJoineryKit::GenerateCarcass(WardrobeCarcassParams());
	const FDynamicMesh3 HandLaid = HandLaidCarcass();

	if (!TestTrue(TEXT("The generator produced a carcass"), Generated.TriangleCount() > 0))
	{
		return false;
	}

	const FAxisAlignedBox3d GeneratedBounds = Generated.GetBounds();
	const FAxisAlignedBox3d HandLaidBounds = HandLaid.GetBounds();

	TestTrue(TEXT("It occupies exactly the box it was asked for"),
		GeneratedBounds.Min.Equals(FVector3d::Zero(), 1e-6)
		&& GeneratedBounds.Max.Equals(FVector3d(WardrobeWidth, WardrobeDepth, CarcassHeight), 1e-6));

	TestTrue(TEXT("...which is where the hand-laid carcass stands too"),
		GeneratedBounds.Min.Equals(HandLaidBounds.Min, 1e-6)
		&& GeneratedBounds.Max.Equals(HandLaidBounds.Max, 1e-6));

	// Volume rather than triangle count: two carcasses can differ in how they are triangulated and be
	// the same carcass, and can share a triangle count while containing quite different board.
	const double HandLaidVolume = Volume(HandLaid);
	TestTrue(TEXT("The hand-laid carcass contains board"), HandLaidVolume > 0.0);
	TestNearlyEqual(TEXT("The generated carcass contains exactly the same board"),
		Volume(Generated), HandLaidVolume, HandLaidVolume * 1e-6);

	// Same volume in the same bounds could still be a different lay-up. Neither reaching inside the
	// other is what makes them the same solid rather than merely the same size.
	{
		FSolid GeneratedSolid(Generated);
		FSolid HandLaidSolid(HandLaid);

		TestTrue(TEXT("No board of the hand-laid carcass is inside the generated one"),
			DeepestInside(GeneratedSolid, HandLaid) < PenetrationTolerance);
		TestTrue(TEXT("...nor the other way round"),
			DeepestInside(HandLaidSolid, Generated) < PenetrationTolerance);
	}

	return true;
}

/**
 * A carcass is board, butted - not boards sharing each other's space.
 *
 * Overlapping boards would put a false volume on any quantity taken off the model, and would give
 * every interpenetration measurement in the composition tests a baseline to hide inside. Asserted
 * against the volume the boards analytically add up to, so a lap anywhere shows as a shortfall.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFCarcassBoardsAreButtedTest,
	"HouseForge.Joinery.CarcassBoardsDoNotOverlap", HF_TEST_FLAGS)

bool FHFCarcassBoardsAreButtedTest::RunTest(const FString& Parameters)
{
	const FHFCarcassParams P = WardrobeCarcassParams();
	const FDynamicMesh3 Mesh = FHFJoineryKit::GenerateCarcass(P);

	constexpr double W = WardrobeWidth;
	constexpr double D = WardrobeDepth;
	constexpr double H = CarcassHeight;
	constexpr double T = Board;
	constexpr double BackY = D - T;
	const double Between = W - 2.0 * T;

	// Sides full height and depth, top and bottom between them, back behind both, one partition.
	const double Expected =
		2.0 * (T * D * H)                       // sides
		+ 2.0 * (Between * D * T)               // top and bottom
		+ (Between * T * (H - 2.0 * T))         // back
		+ (T * BackY * (H - 2.0 * T));          // mid partition

	TestNearlyEqual(TEXT("The carcass contains exactly the board it is made of"),
		Volume(Mesh), Expected, Expected * 1e-6);

	// Closed, so it is a solid at all: an open shell has no meaningful volume and the figure above
	// would be measuring nothing.
	TestTrue(TEXT("Every board is a closed solid"), FHFMeshOps::IsClosed(Mesh));

	// Roles and UVs, per .claude/rules/04-conventions.md: untagged geometry cannot be re-materialled,
	// and geometry with no UVs takes whatever the material's first tile happens to be.
	TestTrue(TEXT("Every triangle carries the carcass role"),
		EveryTriangleTagged(Mesh, EHFSurfaceRole::JoineryCarcass));
	TestTrue(TEXT("Every triangle carries UVs"), HasCompleteUVs(Mesh));

	return true;
}

/**
 * The bay a shutter closes and the clear volume behind it are the same bay.
 *
 * A carcass that reported its bays half a partition out from where it actually put them would put
 * every shelf into the side of a partition - a fault that reads as a fitting gap on one side and as
 * z-fighting on the other, and which nothing but a measurement would find.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFCarcassBaysTest,
	"HouseForge.Joinery.CarcassBaysAgreeWithItsPartitions", HF_TEST_FLAGS)

bool FHFCarcassBaysTest::RunTest(const FString& Parameters)
{
	const FHFCarcassParams P = WardrobeCarcassParams();

	TestEqual(TEXT("A two-bay carcass has two bays"), P.Bays(), 2);
	TestNearlyEqual(TEXT("Its module is half the run"), P.ModuleWidth(), WardrobeWidth * 0.5, 1e-9);
	TestNearlyEqual(TEXT("Its inside stops at the back panel"), P.BackFaceY(), WardrobeDepth - Board, 1e-9);

	const FBox Left = FHFJoineryKit::CarcassBayClearVolume(P, 0);
	const FBox Right = FHFJoineryKit::CarcassBayClearVolume(P, 1);

	if (!TestTrue(TEXT("Both bays have a clear volume"), Left.IsValid != 0 && Right.IsValid != 0))
	{
		return false;
	}

	// The figures the composition's own shelf placement is set out from: side board on the outside,
	// half a partition on the inside.
	TestNearlyEqual(TEXT("The left bay starts at the side board"), Left.Min.X, Board, 1e-9);
	TestNearlyEqual(TEXT("...and stops half a partition short of the middle"),
		Left.Max.X, WardrobeWidth * 0.5 - Board * 0.5, 1e-9);
	TestNearlyEqual(TEXT("The right bay starts half a partition past it"),
		Right.Min.X, WardrobeWidth * 0.5 + Board * 0.5, 1e-9);
	TestNearlyEqual(TEXT("...and stops at the far side board"), Right.Max.X, WardrobeWidth - Board, 1e-9);

	// Equal bays, which is what makes a run of shutters line up with the boxes behind them.
	TestNearlyEqual(TEXT("Both bays are the same clear width"),
		Left.Max.X - Left.Min.X, Right.Max.X - Right.Min.X, 1e-9);

	// Open at the front - a bay's front is where the shutter goes - and closed at the back panel.
	TestNearlyEqual(TEXT("A bay is clear from the carcass front plane"), Left.Min.Y, 0.0, 1e-9);
	TestNearlyEqual(TEXT("...back to the inner face of the back"), Left.Max.Y, WardrobeDepth - Board, 1e-9);
	TestNearlyEqual(TEXT("...and between the bottom and top boards"), Left.Min.Z, Board, 1e-9);
	TestNearlyEqual(TEXT("...to the underside of the top"), Left.Max.Z, CarcassHeight - Board, 1e-9);
	TestNearlyEqual(TEXT("The clear height is the height less two boards"),
		P.ClearHeight(), CarcassHeight - 2.0 * Board, 1e-9);

	// The module origin is what a shutter's pivot is placed by, and it is set out from the OVERALL
	// width rather than from the clear width inside the sides.
	TestTrue(TEXT("The first module starts at the run's own origin"),
		P.ModuleOrigin(0).Equals(FVector::ZeroVector, 1e-9));
	TestTrue(TEXT("The second starts one module along"),
		P.ModuleOrigin(1).Equals(FVector(WardrobeWidth * 0.5, 0.0, 0.0), 1e-9));

	// A bay index outside the run is answered rather than read off the end of the array.
	TestTrue(TEXT("An out-of-range bay is clamped to a real one"),
		FHFJoineryKit::CarcassBayClearVolume(P, 7).IsValid != 0);

	return true;
}

/**
 * A carcass with more partitions has more partitions, and less room inside.
 *
 * Bays are the one parameter that changes both the mesh and what the composer is told about it, so
 * they are the easiest place for the two to drift apart.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFCarcassPartitionsTest,
	"HouseForge.Joinery.CarcassPartitionsCostBoardAndRoom", HF_TEST_FLAGS)

bool FHFCarcassPartitionsTest::RunTest(const FString& Parameters)
{
	FHFCarcassParams One = WardrobeCarcassParams();
	One.BayCount = 1;

	FHFCarcassParams Four = WardrobeCarcassParams();
	Four.BayCount = 4;

	const double OneVolume = Volume(FHFJoineryKit::GenerateCarcass(One));
	const double FourVolume = Volume(FHFJoineryKit::GenerateCarcass(Four));

	// Three partitions more, each one board thick, running the clear height and the clear depth.
	const double PerPartition = Board * (WardrobeDepth - Board) * (CarcassHeight - 2.0 * Board);
	TestNearlyEqual(TEXT("Three more bays is exactly three more partitions of board"),
		FourVolume - OneVolume, 3.0 * PerPartition, PerPartition * 1e-6);

	// A single-bay carcass has no partition to divide it, so its bay is the whole inside.
	const FBox Whole = FHFJoineryKit::CarcassBayClearVolume(One, 0);
	TestNearlyEqual(TEXT("One bay is clear from side board to side board"),
		Whole.Max.X - Whole.Min.X, WardrobeWidth - 2.0 * Board, 1e-9);

	// Four bays share the same run, so each is narrower by its share of the partitions.
	double FourBaysClear = 0.0;
	for (int32 Bay = 0; Bay < 4; ++Bay)
	{
		const FBox Clear = FHFJoineryKit::CarcassBayClearVolume(Four, Bay);
		if (!TestTrue(TEXT("Every bay of a four-bay carcass has clear volume"), Clear.IsValid != 0))
		{
			return false;
		}
		FourBaysClear += Clear.Max.X - Clear.Min.X;
	}

	TestNearlyEqual(TEXT("What the partitions took is exactly what the bays lost"),
		(Whole.Max.X - Whole.Min.X) - FourBaysClear, 3.0 * Board, 1e-9);

	return true;
}

/**
 * A carcass that cannot be built says so, rather than producing a solid that looks plausible.
 *
 * Every one of these is a real answer a composer has to be able to get: a run of no length, a board
 * thicker than the box, more bays than there is width for.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFCarcassLimitsTest,
	"HouseForge.Joinery.CarcassRefusesWhatCannotBeBuilt", HF_TEST_FLAGS)

bool FHFCarcassLimitsTest::RunTest(const FString& Parameters)
{
	// Nothing at all.
	TestEqual(TEXT("A carcass of no size is an empty mesh, not a degenerate one"),
		FHFJoineryKit::GenerateCarcass(FHFCarcassParams()).TriangleCount(), 0);

	{
		FHFCarcassParams NoWidth = WardrobeCarcassParams();
		NoWidth.Width = 0.0;
		TestEqual(TEXT("A run of no length builds nothing"),
			FHFJoineryKit::GenerateCarcass(NoWidth).TriangleCount(), 0);
		TestTrue(TEXT("...and reports no bay to put anything in"),
			FHFJoineryKit::CarcassBayClearVolume(NoWidth, 0).IsValid == 0);
	}

	{
		// A board thicker than half the box would put the two sides through each other.
		FHFCarcassParams Fat = WardrobeCarcassParams();
		Fat.BoardThickness = 500.0;
		const FHFCarcassParams Clamped = FHFJoineryKit::SanitiseCarcass(Fat);
		TestTrue(TEXT("A board thicker than the box is clamped to fit it"),
			Clamped.BoardThickness <= FMath::Min3(Fat.Width, Fat.Depth, Fat.Height) * 0.5 + 1e-9);

		const FDynamicMesh3 Mesh = FHFJoineryKit::GenerateCarcass(Fat);
		TestTrue(TEXT("...and what comes out is still a closed solid"),
			Mesh.TriangleCount() == 0 || FHFMeshOps::IsClosed(Mesh));
	}

	{
		// More bays than the run can hold. Coming down to what fits, and saying how many, is what
		// lets a composer place exactly as many shutters as there are bays.
		FHFCarcassParams Many = WardrobeCarcassParams();
		Many.BayCount = 12;
		Many.Width = 20.0;

		const FHFCarcassParams Fitted = FHFJoineryKit::SanitiseCarcass(Many);
		TestTrue(TEXT("More bays than fit are reduced to the ones that do"), Fitted.Bays() < 12);
		TestTrue(TEXT("...and never below one"), Fitted.Bays() >= 1);

		for (int32 Bay = 0; Bay < Fitted.Bays(); ++Bay)
		{
			TestTrue(TEXT("Every bay that survived has clear volume in it"),
				FHFJoineryKit::CarcassBayClearVolume(Fitted, Bay).IsValid != 0);
		}
	}

	{
		// A back thicker than the box has nothing left in front of it.
		FHFCarcassParams DeepBack = WardrobeCarcassParams();
		DeepBack.BackThickness = 500.0;
		const FHFCarcassParams Clamped = FHFJoineryKit::SanitiseCarcass(DeepBack);
		TestTrue(TEXT("A back thicker than the carcass is clamped"),
			Clamped.BackFaceY() > 0.0 && Clamped.BackFaceY() < Clamped.Depth);
	}

	return true;
}

/**
 * The construction figures reach the geometry.
 *
 * These are the settings the project exposes, and until there was a carcass to build they changed
 * nothing that could be measured. Each one is moved on its own and the carcass is measured, because
 * a figure that is carried but never used passes any test that only reads the parameters back.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFCarcassFiguresReachTheGeometryTest,
	"HouseForge.Joinery.CarcassFiguresChangeTheCarcass", HF_TEST_FLAGS)

bool FHFCarcassFiguresReachTheGeometryTest::RunTest(const FString& Parameters)
{
	const FHFCarcassParams Base = WardrobeCarcassParams();
	const FBox BaseBay = FHFJoineryKit::CarcassBayClearVolume(Base, 0);
	const double BaseVolume = Volume(FHFJoineryKit::GenerateCarcass(Base));

	{
		// Board thickness: more board, and less room inside for it to leave.
		FHFCarcassParams Thick = Base;
		Thick.BoardThickness = 2.5;

		TestTrue(TEXT("A thicker board puts more board in the carcass"),
			Volume(FHFJoineryKit::GenerateCarcass(Thick)) > BaseVolume);

		const FBox ThickBay = FHFJoineryKit::CarcassBayClearVolume(Thick, 0);
		TestTrue(TEXT("...and takes the difference out of the bay"),
			ThickBay.Max.X - ThickBay.Min.X < BaseBay.Max.X - BaseBay.Min.X);
		TestNearlyEqual(TEXT("The bay starts exactly one board in"), ThickBay.Min.X, 2.5, 1e-9);
	}

	{
		// The back is the figure that sets the clear DEPTH, which is what a hanging rail needs.
		FHFCarcassParams ThinBack = Base;
		ThinBack.BackThickness = 0.6;

		const FBox ThinBay = FHFJoineryKit::CarcassBayClearVolume(ThinBack, 0);
		TestNearlyEqual(TEXT("A 6 mm back leaves the inside deeper"),
			ThinBay.Max.Y, WardrobeDepth - 0.6, 1e-9);
		TestTrue(TEXT("...deeper than an 18 mm one does"), ThinBay.Max.Y > BaseBay.Max.Y);
	}

	{
		// No back at all: the inside runs the whole depth.
		FHFCarcassParams Open = Base;
		Open.bHasBack = false;

		const FBox OpenBay = FHFJoineryKit::CarcassBayClearVolume(Open, 0);
		TestNearlyEqual(TEXT("A carcass with no back is clear to its full depth"),
			OpenBay.Max.Y, WardrobeDepth, 1e-9);
		TestTrue(TEXT("...and contains less board than one with a back"),
			Volume(FHFJoineryKit::GenerateCarcass(Open)) < BaseVolume);
	}

	{
		// Depth and height are the dimensions, and they are the bounds.
		FHFCarcassParams Deeper = Base;
		Deeper.Depth = 65.0;
		Deeper.Height = 220.0;

		const FAxisAlignedBox3d Bounds = FHFJoineryKit::GenerateCarcass(Deeper).GetBounds();
		TestTrue(TEXT("The carcass is exactly the box it was asked for"),
			Bounds.Max.Equals(FVector3d(WardrobeWidth, 65.0, 220.0), 1e-6));
	}

	return true;
}

#undef HF_TEST_FLAGS

#endif // WITH_DEV_AUTOMATION_TESTS
