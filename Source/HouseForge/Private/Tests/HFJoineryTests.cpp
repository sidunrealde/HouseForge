// Copyright Siddartha G. All Rights Reserved.

#include "HouseForge.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Geometry/HFJoineryKit.h"
#include "Geometry/HFMeshOps.h"
#include "MeshQueries.h"
#include "Misc/AutomationTest.h"
#include "Model/HFTypes.h"

using namespace UE::Geometry;

#define HF_TEST_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
	/** A 240 cm kitchen base run: 60 deep, on a 100 mm plinth kicked back 50 mm, in 18 mm ply. */
	FHFPlinthParams MakeBaseRunPlinth()
	{
		FHFPlinthParams Params;
		Params.Width = 240.0;
		Params.Depth = 60.0;
		Params.Height = 10.0;
		Params.FrontRecess = 5.0;
		Params.EndRecess = 5.0;
		Params.PanelThickness = 1.8;
		return Params;
	}

	double Volume(const FDynamicMesh3& Mesh)
	{
		return TMeshQueries<FDynamicMesh3>::GetVolumeArea(Mesh).X;
	}

	/**
	 * The board a ladder frame of these dimensions is made of.
	 *
	 * Stated independently of the generator rather than read back from it, so the test fails if the
	 * plinth quietly becomes a solid block - which would look identical from the front and be wrong
	 * on every take-off.
	 */
	double ExpectedFrameVolume(const FHFPlinthParams& Params)
	{
		const double Left = Params.bLeftEndExposed ? Params.EndRecess : 0.0;
		const double Right = Params.bRightEndExposed ? Params.EndRecess : 0.0;

		const double Span = Params.Width - Left - Right;
		const double Reach = Params.Depth - Params.FrontRecess;
		const double T = Params.PanelThickness;

		return Params.Height * T * (2.0 * Span + 2.0 * (Reach - 2.0 * T));
	}

	/**
	 * The roles carried by the triangles whose centroids fall inside a region.
	 *
	 * A region rather than a plane: several rails present a face on the same plane, and a query that
	 * cannot tell the front rail's end from the end rail's outer face would pass whichever way round
	 * the roles were assigned.
	 */
	void RolesInRegion(const FDynamicMesh3& Mesh, const FAxisAlignedBox3d& Region,
		TSet<EHFSurfaceRole>& OutRoles, int32& OutCount)
	{
		OutCount = 0;
		for (const int32 Tid : Mesh.TriangleIndicesItr())
		{
			FVector3d A, B, C;
			Mesh.GetTriVertices(Tid, A, B, C);

			if (Region.Contains((A + B + C) / 3.0))
			{
				++OutCount;
				OutRoles.Add(FHFMeshOps::RoleForGroup(Mesh.GetTriangleGroup(Tid)));
			}
		}
	}

	/** Asserts that one face, isolated by region, is tagged with exactly one expected role. */
	bool FaceHasRole(FAutomationTestBase& Test, const FDynamicMesh3& Mesh, const FAxisAlignedBox3d& Region,
		EHFSurfaceRole Expected, const TCHAR* What)
	{
		TSet<EHFSurfaceRole> Roles;
		int32 Count = 0;
		RolesInRegion(Mesh, Region, Roles, Count);

		if (Count == 0)
		{
			Test.AddError(FString::Printf(TEXT("%s: no geometry found where that face should be."), What));
			return false;
		}
		if (Roles.Num() != 1 || !Roles.Contains(Expected))
		{
			Test.AddError(FString::Printf(TEXT("%s: face carries %d role(s), and not the expected one."),
				What, Roles.Num()));
			return false;
		}
		return true;
	}

	/**
	 * True when one UV unit really is TexelSizeCm of world.
	 *
	 * Every face here is axis-aligned, so a planar projection of it is isometric and the check is
	 * exact: a UV edge times the texel size must be the world edge it came from. Without that,
	 * tiling expressed in millimetres in the material panel means nothing.
	 */
	bool HasWorldScaleUVs(const FDynamicMesh3& Mesh, double TexelSizeCm)
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

			FVector3d P[3];
			Mesh.GetTriVertices(Tid, P[0], P[1], P[2]);

			FVector2f UV[3];
			UVs->GetTriElements(Tid, UV[0], UV[1], UV[2]);

			for (int32 i = 0; i < 3; ++i)
			{
				const int32 j = (i + 1) % 3;
				const double WorldEdge = (P[j] - P[i]).Length();
				const double UVEdge = (FVector2d(UV[j].X, UV[j].Y) - FVector2d(UV[i].X, UV[i].Y)).Length();

				if (!FMath::IsNearlyEqual(UVEdge * TexelSizeCm, WorldEdge, 0.01))
				{
					return false;
				}
			}
		}
		return true;
	}
}

/**
 * The plinth as a solid: watertight, positive, the size it says it is, and tagged.
 *
 * Asserted on volume and bounds rather than on triangle counts. A count passes for a mesh built
 * from the wrong boards in the wrong places, which is exactly the failure worth catching.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFPlinthTest, "HouseForge.Joinery.Plinth", HF_TEST_FLAGS)

bool FHFPlinthTest::RunTest(const FString& Parameters)
{
	const FHFPlinthParams Params = MakeBaseRunPlinth();
	const FDynamicMesh3 Mesh = FHFJoineryKit::GeneratePlinth(Params);

	if (!TestTrue(TEXT("A plinth of real dimensions produces geometry"), Mesh.TriangleCount() > 0))
	{
		return false;
	}

	TestTrue(TEXT("The plinth is watertight"), FHFMeshOps::IsClosed(Mesh));
	TestTrue(TEXT("The plinth faces outward"), Volume(Mesh) > 0.0);

	// The board it is really made of: two rails the length of the run and two closing the ends.
	const double Expected = ExpectedFrameVolume(Params);
	TestNearlyEqual(TEXT("The plinth contains the board a ladder frame is made of"),
		Volume(Mesh), Expected, FMath::Abs(Expected) * 0.001);

	// A solid block of the same envelope would be more than ten times the material. That is the
	// mistake this pins down: it would look identical from the only angle anybody sees it from.
	TestTrue(TEXT("The plinth is a frame, not a solid block"),
		Volume(Mesh) < 240.0 * 55.0 * 10.0 * 0.5);

	const FAxisAlignedBox3d Bounds = Mesh.GetBounds();
	TestNearlyEqual(TEXT("It starts at the left end of the run"), Bounds.Min.X, 0.0, 0.001);
	TestNearlyEqual(TEXT("It runs the full width of the carcass"), Bounds.Max.X, 240.0, 0.001);
	TestNearlyEqual(TEXT("Its face is set back by the toe kick"), Bounds.Min.Y, 5.0, 0.001);
	TestNearlyEqual(TEXT("It reaches the back of the carcass"), Bounds.Max.Y, 60.0, 0.001);
	TestNearlyEqual(TEXT("It sits on the floor"), Bounds.Min.Z, 0.0, 0.001);
	TestNearlyEqual(TEXT("It is the height the carcass will stand on"), Bounds.Max.Z, 10.0, 0.001);

	// Untagged geometry cannot be re-materialled by the user, so an untagged triangle is a defect
	// even though it renders perfectly well.
	for (const int32 Tid : Mesh.TriangleIndicesItr())
	{
		if (Mesh.GetTriangleGroup(Tid) == 0)
		{
			AddError(TEXT("A plinth triangle was emitted without a surface-role polygroup."));
			break;
		}
	}

	// The one face anybody sees is the toe kick, and it has to be a finished face. The rails nobody
	// sees are carcass board, so retexturing a finish does not repaint the inside of the frame.
	FaceHasRole(*this, Mesh, FAxisAlignedBox3d(FVector3d(-1.0, 4.9, -1.0), FVector3d(241.0, 5.1, 11.0)),
		EHFSurfaceRole::ShutterLaminate, TEXT("The toe-kick face is finished board"));

	FaceHasRole(*this, Mesh, FAxisAlignedBox3d(FVector3d(-1.0, 59.9, -1.0), FVector3d(241.0, 60.1, 11.0)),
		EHFSurfaceRole::JoineryCarcass, TEXT("The rail against the wall is carcass board"));

	// An end that dies into a wall is carcass board too - the same face is finished only when the
	// end is on show, which PlinthRecess checks.
	FaceHasRole(*this, Mesh, FAxisAlignedBox3d(FVector3d(-0.1, 10.0, -1.0), FVector3d(0.1, 50.0, 11.0)),
		EHFSurfaceRole::JoineryCarcass, TEXT("A concealed end is carcass board"));

	TestTrue(TEXT("The plinth carries real-world-scale UVs"), HasWorldScaleUVs(Mesh, 100.0));

	return true;
}

/**
 * The recess itself - the whole point of the part.
 *
 * A plinth flush with the shutters is a box on the floor. These assertions are on where the face
 * actually ends up, because a recess that is applied to the wrong axis or to the carcass instead of
 * the plinth still produces a plausible-looking mesh.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFPlinthRecessTest, "HouseForge.Joinery.PlinthRecess", HF_TEST_FLAGS)

bool FHFPlinthRecessTest::RunTest(const FString& Parameters)
{
	const FHFPlinthParams Base = MakeBaseRunPlinth();
	const FDynamicMesh3 Recessed = FHFJoineryKit::GeneratePlinth(Base);

	// Flush: the face lands on the carcass front plane, and the frame gains exactly the board the
	// recess was costing it - the two end rails, each 5 cm longer.
	FHFPlinthParams FlushParams = Base;
	FlushParams.FrontRecess = 0.0;
	const FDynamicMesh3 Flush = FHFJoineryKit::GeneratePlinth(FlushParams);

	TestNearlyEqual(TEXT("A flush plinth reaches the carcass front plane"),
		Flush.GetBounds().Min.Y, 0.0, 0.001);
	TestNearlyEqual(TEXT("A recessed plinth stands back from it by the toe kick"),
		Recessed.GetBounds().Min.Y, 5.0, 0.001);
	TestNearlyEqual(TEXT("The recess costs exactly the board it removes"),
		Volume(Flush) - Volume(Recessed), 2.0 * 1.8 * 10.0 * 5.0, 0.01);

	// The recess must not disturb anything else: it is a setback, not a resize.
	TestNearlyEqual(TEXT("The recess does not move the back"),
		Recessed.GetBounds().Max.Y, Flush.GetBounds().Max.Y, 0.001);
	TestNearlyEqual(TEXT("The recess does not change the height"),
		Recessed.GetBounds().Max.Z, Flush.GetBounds().Max.Z, 0.001);
	TestNearlyEqual(TEXT("The recess does not shorten the run"),
		Recessed.GetBounds().Max.X - Recessed.GetBounds().Min.X,
		Flush.GetBounds().Max.X - Flush.GetBounds().Min.X, 0.001);

	// A deeper kick is a deeper kick, measurably.
	FHFPlinthParams DeepParams = Base;
	DeepParams.FrontRecess = 9.0;
	TestNearlyEqual(TEXT("A deeper toe kick sets the face further back"),
		FHFJoineryKit::GeneratePlinth(DeepParams).GetBounds().Min.Y, 9.0, 0.001);

	// The kick is measured from the SHUTTER face, and a run with shutters over it has to say how far
	// in front of the carcass those hang - or the recess it gets is that much deeper than the one it
	// asked for, silently. The frame is built off the carcass front plane either way; this is the one
	// conversion between the two datums, and the whole reason both are named.
	{
		FHFPlinthParams Overlaid = Base;
		Overlaid.ShutterOverlay = 2.0;
		Overlaid.FrontRecess = 5.0;

		const FDynamicMesh3 Mesh = FHFJoineryKit::GeneratePlinth(Overlaid);
		const double ShutterFaceY = -Overlaid.ShutterOverlay;

		TestNearlyEqual(TEXT("The kick measured from the shutter face is the kick that was asked for"),
			Mesh.GetBounds().Min.Y - ShutterFaceY, 5.0, 0.001);
		TestNearlyEqual(TEXT("Which puts the panel nearer the carcass front, not further from it"),
			Mesh.GetBounds().Min.Y, 3.0, 0.001);
		TestNearlyEqual(TEXT("And the back is untouched: an overlay is not a resize"),
			Mesh.GetBounds().Max.Y, Recessed.GetBounds().Max.Y, 0.001);

		// A kick shallower than the shutters stand proud would put the plinth out in front of them,
		// which is not a toe kick. Clamped to flush with the carcass and reported as such.
		FHFPlinthParams TooShallow = Overlaid;
		TooShallow.FrontRecess = 0.5;
		const FHFPlinthParams Fitted = FHFJoineryKit::SanitisePlinth(TooShallow);

		TestNearlyEqual(TEXT("A kick shallower than the overlay is clamped to it"),
			Fitted.FrontRecess, Fitted.ShutterOverlay, 1e-9);
		TestNearlyEqual(TEXT("So the panel lands on the carcass front plane rather than in front of it"),
			FHFJoineryKit::GeneratePlinth(TooShallow).GetBounds().Min.Y, 0.0, 0.001);
	}

	// An end on show is set back the same way, so the carcass reads as floating from the side too.
	FHFPlinthParams LeftOpen = Base;
	LeftOpen.bLeftEndExposed = true;
	const FDynamicMesh3 LeftMesh = FHFJoineryKit::GeneratePlinth(LeftOpen);

	TestNearlyEqual(TEXT("An exposed end is set back"), LeftMesh.GetBounds().Min.X, 5.0, 0.001);
	TestNearlyEqual(TEXT("The end against a wall still runs full width"),
		LeftMesh.GetBounds().Max.X, 240.0, 0.001);
	TestNearlyEqual(TEXT("An exposed end costs the board it removes"),
		Volume(Recessed) - Volume(LeftMesh), 2.0 * 1.8 * 10.0 * 5.0, 0.01);
	TestNearlyEqual(TEXT("Its board matches a frame of the shortened span"),
		Volume(LeftMesh), ExpectedFrameVolume(LeftOpen), ExpectedFrameVolume(LeftOpen) * 0.001);

	// An exposed end return is on show, so it is finished board where a concealed one is carcass.
	// The region isolates the end rail from the front and back rails, which present a face on the
	// same plane and would otherwise answer for it.
	FaceHasRole(*this, LeftMesh, FAxisAlignedBox3d(FVector3d(4.9, 10.0, -1.0), FVector3d(5.1, 50.0, 11.0)),
		EHFSurfaceRole::ShutterLaminate, TEXT("An exposed end is finished board"));

	FHFPlinthParams BothOpen = Base;
	BothOpen.bLeftEndExposed = true;
	BothOpen.bRightEndExposed = true;
	const FDynamicMesh3 BothMesh = FHFJoineryKit::GeneratePlinth(BothOpen);
	TestNearlyEqual(TEXT("Both ends set back on the left"), BothMesh.GetBounds().Min.X, 5.0, 0.001);
	TestNearlyEqual(TEXT("Both ends set back on the right"), BothMesh.GetBounds().Max.X, 235.0, 0.001);
	TestTrue(TEXT("A plinth open at both ends is still watertight"), FHFMeshOps::IsClosed(BothMesh));

	// An end setback on an end that is not on show would leave a gap against the wall.
	FHFPlinthParams Inert = Base;
	Inert.EndRecess = 20.0;
	const FDynamicMesh3 InertMesh = FHFJoineryKit::GeneratePlinth(Inert);
	TestNearlyEqual(TEXT("An end setback is inert where no end is on show"),
		Volume(InertMesh), Volume(Recessed), 0.01);
	TestNearlyEqual(TEXT("A concealed end still meets the wall"), InertMesh.GetBounds().Min.X, 0.0, 0.001);

	return true;
}

/**
 * Every parameter has to genuinely change the output, and impossible ones must not produce garbage.
 *
 * Each is varied on its own and checked against the specific change it should have caused, rather
 * than against "the mesh differs" - which passes when a parameter is wired to the wrong axis.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFPlinthParametersTest, "HouseForge.Joinery.PlinthParameters", HF_TEST_FLAGS)

bool FHFPlinthParametersTest::RunTest(const FString& Parameters)
{
	const FHFPlinthParams Base = MakeBaseRunPlinth();
	const double BaseVolume = Volume(FHFJoineryKit::GeneratePlinth(Base));

	// Height: a TV unit's 80 mm plinth against a kitchen's 100 mm. Nothing but the height changes,
	// so the board scales exactly with it.
	FHFPlinthParams Shorter = Base;
	Shorter.Height = 8.0;
	const FDynamicMesh3 ShorterMesh = FHFJoineryKit::GeneratePlinth(Shorter);
	TestNearlyEqual(TEXT("A shorter plinth stands lower"), ShorterMesh.GetBounds().Max.Z, 8.0, 0.001);
	TestNearlyEqual(TEXT("Height scales the board linearly"),
		Volume(ShorterMesh), BaseVolume * 0.8, FMath::Abs(BaseVolume) * 0.001);

	// Width: a longer run, gaining board on the two rails that run its length.
	FHFPlinthParams Wider = Base;
	Wider.Width = 300.0;
	const FDynamicMesh3 WiderMesh = FHFJoineryKit::GeneratePlinth(Wider);
	TestNearlyEqual(TEXT("A wider run is wider"), WiderMesh.GetBounds().Max.X, 300.0, 0.001);
	TestNearlyEqual(TEXT("Width adds board to the rails that run the length"),
		Volume(WiderMesh) - BaseVolume, 2.0 * 1.8 * 10.0 * 60.0, 0.01);

	// Depth: a deeper unit, gaining board on the two rails that close the ends.
	FHFPlinthParams Deeper = Base;
	Deeper.Depth = 90.0;
	const FDynamicMesh3 DeeperMesh = FHFJoineryKit::GeneratePlinth(Deeper);
	TestNearlyEqual(TEXT("A deeper unit reaches further back"), DeeperMesh.GetBounds().Max.Y, 90.0, 0.001);
	TestNearlyEqual(TEXT("Its face stays where the toe kick put it"),
		DeeperMesh.GetBounds().Min.Y, 5.0, 0.001);
	TestNearlyEqual(TEXT("Depth adds board to the rails that close the ends"),
		Volume(DeeperMesh) - BaseVolume, 2.0 * 1.8 * 10.0 * 30.0, 0.01);

	// Board thickness: 6 mm ply clad in aluminium instead of 18 mm faced ply. Same envelope, less
	// material - which is the only thing that changes, and a bounds check alone would never see it.
	FHFPlinthParams Thin = Base;
	Thin.PanelThickness = 0.6;
	const FDynamicMesh3 ThinMesh = FHFJoineryKit::GeneratePlinth(Thin);
	TestNearlyEqual(TEXT("Thinner board does not change the envelope"),
		ThinMesh.GetBounds().Max.X - ThinMesh.GetBounds().Min.X, 240.0, 0.001);
	TestNearlyEqual(TEXT("Thinner board is less material"),
		Volume(ThinMesh), ExpectedFrameVolume(Thin), ExpectedFrameVolume(Thin) * 0.001);
	TestTrue(TEXT("Thinner board really is less material"), Volume(ThinMesh) < BaseVolume);

	// Asking for no plinth is a real answer - a wall-hung unit has none - and must come back empty
	// rather than as a zero-height sliver that renders as a black line on the floor.
	FHFPlinthParams None = Base;
	None.Height = 0.0;
	TestEqual(TEXT("No height means no plinth"),
		FHFJoineryKit::GeneratePlinth(None).TriangleCount(), 0);

	FHFPlinthParams NoWidth = Base;
	NoWidth.Width = 0.0;
	TestEqual(TEXT("No width means no plinth"),
		FHFJoineryKit::GeneratePlinth(NoWidth).TriangleCount(), 0);

	FHFPlinthParams NoDepth = Base;
	NoDepth.Depth = 0.0;
	TestEqual(TEXT("No depth means no plinth"),
		FHFJoineryKit::GeneratePlinth(NoDepth).TriangleCount(), 0);

	// Nonsense in must not mean nonsense out. Negative dimensions come back as nothing, and a recess
	// deeper than the unit is clamped to leave a board rather than turning the frame inside out.
	FHFPlinthParams Negative = Base;
	Negative.Width = -240.0;
	Negative.Height = -10.0;
	TestEqual(TEXT("Negative dimensions produce nothing at all"),
		FHFJoineryKit::GeneratePlinth(Negative).TriangleCount(), 0);

	FHFPlinthParams Absurd = Base;
	Absurd.FrontRecess = 100.0;
	const FHFPlinthParams Clamped = FHFJoineryKit::SanitisePlinth(Absurd);
	TestNearlyEqual(TEXT("A recess deeper than the unit is clamped to leave a board"),
		Clamped.FrontRecess, 60.0 - 1.8, 0.001);

	const FDynamicMesh3 AbsurdMesh = FHFJoineryKit::GeneratePlinth(Absurd);
	TestTrue(TEXT("An over-deep recess still produces a solid"), FHFMeshOps::IsClosed(AbsurdMesh));
	TestTrue(TEXT("An over-deep recess still has positive volume"), Volume(AbsurdMesh) > 0.0);
	TestNearlyEqual(TEXT("It is clamped where the sanitised parameters say it is"),
		AbsurdMesh.GetBounds().Min.Y, Clamped.FrontRecess, 0.001);
	TestNearlyEqual(TEXT("What is left is a solid packer one board deep"),
		Volume(AbsurdMesh), 240.0 * 1.8 * 10.0, 1.0);

	// Board thicker than the unit it frames would have opposite rails passing through each other,
	// double-counting the same material. Clamped, and still a solid of the right envelope.
	FHFPlinthParams Chunky = Base;
	Chunky.PanelThickness = 200.0;
	const FDynamicMesh3 ChunkyMesh = FHFJoineryKit::GeneratePlinth(Chunky);
	TestTrue(TEXT("Absurd board thickness still yields a watertight solid"),
		FHFMeshOps::IsClosed(ChunkyMesh));
	TestNearlyEqual(TEXT("It cannot contain more material than its own envelope"),
		Volume(ChunkyMesh), 240.0 * 55.0 * 10.0, 1.0);

	return true;
}

namespace
{
	/** A 1800 mm run of kitchen wall units, capped with the standard 60 x 25 cornice. */
	FHFCorniceParams MakeWallUnitCornice()
	{
		FHFCorniceParams Params;
		Params.Width = 180.0;
		Params.Depth = 4.5;
		Params.Height = 6.0;
		Params.Projection = 2.5;
		Params.Profile = EHFCorniceProfile::Square;
		Params.ProfileSize = 2.0;
		Params.EdgeBevel = 0.2;
		Params.CoveSegments = 8;
		return Params;
	}

	/** The envelope a cornice declares, straight from its parameters. */
	FAxisAlignedBox3d DeclaredCorniceBox(const FHFCorniceParams& P)
	{
		return FAxisAlignedBox3d(
			FVector3d(0.0, P.FrontY(), 0.0),
			FVector3d(P.Width, P.BackY(), P.Height));
	}

	bool CorniceBoundsMatch(const FAxisAlignedBox3d& Actual, const FAxisAlignedBox3d& Expected, double Tolerance)
	{
		return Actual.Min.Equals(Expected.Min, Tolerance) && Actual.Max.Equals(Expected.Max, Tolerance);
	}

	/** Where a local box ends up under an anchor, by transforming its corners. */
	FAxisAlignedBox3d CorniceBoundsUnderAnchor(const FAxisAlignedBox3d& Local, const FTransform& Anchor)
	{
		FVector3d Min(TNumericLimits<double>::Max());
		FVector3d Max(-TNumericLimits<double>::Max());

		for (int32 Corner = 0; Corner < 8; ++Corner)
		{
			const FVector Point = Anchor.TransformPosition(FVector(
				(Corner & 1) ? Local.Max.X : Local.Min.X,
				(Corner & 2) ? Local.Max.Y : Local.Min.Y,
				(Corner & 4) ? Local.Max.Z : Local.Min.Z));

			Min = FVector3d(FMath::Min(Min.X, Point.X), FMath::Min(Min.Y, Point.Y), FMath::Min(Min.Z, Point.Z));
			Max = FVector3d(FMath::Max(Max.X, Point.X), FMath::Max(Max.Y, Point.Y), FMath::Max(Max.Z, Point.Z));
		}

		return FAxisAlignedBox3d(Min, Max);
	}

	/**
	 * The cross-section area a profile describes, worked out from the profile rather than measured.
	 *
	 * Stated independently of the generator so the test fails if a cove is quietly built as a
	 * bullnose or a splay as a step - all of which are watertight, positive, correctly bounded, and
	 * wrong. Assumes no chamfer and parameters already inside their limits.
	 */
	double CorniceSectionArea(const FHFCorniceParams& P)
	{
		const double Solid = P.Depth * P.Height;
		const double Size = P.ProfileSize;

		switch (P.Profile)
		{
		case EHFCorniceProfile::Splay:
			return Solid - Size * Size * 0.5;

		case EHFCorniceProfile::Stepped:
			return Solid - Size * Size;

		case EHFCorniceProfile::Cove:
		{
			// The chords of a coarse arc take away less than the quarter circle they approximate, so
			// the segment count is part of the exact answer rather than an error term on it.
			const double Segments = FMath::Clamp(P.CoveSegments, 2, 32);
			return Solid - 0.5 * Segments * Size * Size * FMath::Sin(FMath::DegreesToRadians(90.0 / Segments));
		}

		default:
			return Solid;
		}
	}

	/**
	 * Every triangle carries UVs, and none of them claims more world than the edge it came from.
	 *
	 * The weaker cousin of HasWorldScaleUVs, for the faces a moulding has and a box does not. A
	 * planar projection of a slanted face is foreshortened, so exact world scale is the wrong thing
	 * to demand of a chamfer or a cove - but a projection can only ever shorten an edge, and a UV
	 * that grew is a UV computed in some other space than the one the mesh is in.
	 */
	bool CorniceUVsNeverStretch(const FDynamicMesh3& Mesh, double TexelSizeCm)
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

			FVector3d P[3];
			Mesh.GetTriVertices(Tid, P[0], P[1], P[2]);

			FVector2f UV[3];
			UVs->GetTriElements(Tid, UV[0], UV[1], UV[2]);

			for (int32 i = 0; i < 3; ++i)
			{
				const int32 j = (i + 1) % 3;
				const double WorldEdge = (P[j] - P[i]).Length();
				const double UVEdge = (FVector2d(UV[j].X, UV[j].Y) - FVector2d(UV[i].X, UV[i].Y)).Length();

				if (UVEdge * TexelSizeCm > WorldEdge + 0.01)
				{
					return false;
				}
			}
		}
		return true;
	}
}

/**
 * The cornice as a solid: watertight, positive, the size it says it is, tagged, and chamfered.
 *
 * Asserted on volume, bounds and roles rather than on triangle counts. A count passes for a moulding
 * swept from the wrong section, which is exactly the failure worth catching here - a cornice is only
 * ever judged by its silhouette against a ceiling.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFCorniceTest, "HouseForge.Joinery.Cornice", HF_TEST_FLAGS)

bool FHFCorniceTest::RunTest(const FString& Parameters)
{
	const FHFCorniceParams Params = MakeWallUnitCornice();
	const FDynamicMesh3 Mesh = FHFJoineryKit::GenerateCornice(Params);

	if (!TestTrue(TEXT("A cornice of real dimensions produces geometry"), Mesh.TriangleCount() > 0))
	{
		return false;
	}

	TestTrue(TEXT("The cornice is watertight"), FHFMeshOps::IsClosed(Mesh));
	TestTrue(TEXT("The cornice faces outward"), Volume(Mesh) > 0.0);

	// The declared size, on every axis. The Y bounds are the pair that matter: a moulding that does
	// not cross the shutter plane is a strip of board with no shadow line, and one that never reaches
	// Depth - Projection is fixed to thin air.
	const FAxisAlignedBox3d Bounds = Mesh.GetBounds();
	TestNearlyEqual(TEXT("It starts at the left end of the run"), Bounds.Min.X, 0.0, 0.001);
	TestNearlyEqual(TEXT("It runs the full length of the run"), Bounds.Max.X, 180.0, 0.001);
	TestNearlyEqual(TEXT("It stands proud of the shutter face by the projection"), Bounds.Min.Y, -2.5, 0.001);
	TestNearlyEqual(TEXT("It reaches back onto the carcass"), Bounds.Max.Y, 2.0, 0.001);
	TestNearlyEqual(TEXT("Its underside sits on the top of the shutters"), Bounds.Min.Z, 0.0, 0.001);
	TestNearlyEqual(TEXT("It is as tall as it says it is"), Bounds.Max.Z, 6.0, 0.001);

	// The moulding it is really swept from: the full rectangle less the two chamfered arrises, each a
	// right angle giving up a triangle of half the bevel squared.
	const double ExpectedVolume = (4.5 * 6.0 - 0.2 * 0.2) * 180.0;
	TestNearlyEqual(TEXT("It contains the moulding its section describes"),
		Volume(Mesh), ExpectedVolume, FMath::Abs(ExpectedVolume) * 0.0001);

	// Untagged geometry cannot be re-materialled by the user, so an untagged triangle is a defect
	// even though it renders perfectly well.
	for (const int32 Tid : Mesh.TriangleIndicesItr())
	{
		if (Mesh.GetTriangleGroup(Tid) == 0)
		{
			AddError(TEXT("A cornice triangle was emitted without a surface-role polygroup."));
			break;
		}
	}

	// The faces on show are finished moulding and retexture with the shutters they cap; the one glued
	// to the carcass is not, so refinishing a kitchen does not repaint the inside of a glue joint.
	FaceHasRole(*this, Mesh, FAxisAlignedBox3d(FVector3d(-1.0, -2.51, -1.0), FVector3d(181.0, -2.49, 7.0)),
		EHFSurfaceRole::ShutterLaminate, TEXT("The cornice front face"));
	FaceHasRole(*this, Mesh, FAxisAlignedBox3d(FVector3d(-1.0, 1.99, -1.0), FVector3d(181.0, 2.01, 7.0)),
		EHFSurfaceRole::JoineryCarcass, TEXT("The cornice back face"));
	FaceHasRole(*this, Mesh, FAxisAlignedBox3d(FVector3d(-0.01, -3.0, -1.0), FVector3d(0.01, 3.0, 7.0)),
		EHFSurfaceRole::ShutterLaminate, TEXT("The cut end of the run"));

	TestTrue(TEXT("The cornice carries UVs that never claim more world than they cover"),
		CorniceUVsNeverStretch(Mesh, 100.0));

	// The chamfer is the difference between a moulding that catches a light and one that reads as CG.
	// Asserted as the material it removes, so a bevel wired to the wrong corner - or silently dropped -
	// shows up as a number rather than as something somebody has to notice in a render.
	FHFCorniceParams Sharp = Params;
	Sharp.EdgeBevel = 0.0;
	const FDynamicMesh3 SharpMesh = FHFJoineryKit::GenerateCornice(Sharp);

	TestNearlyEqual(TEXT("Unchamfered, it is exactly the section it declares"),
		Volume(SharpMesh), 4.5 * 6.0 * 180.0, 0.01);
	TestNearlyEqual(TEXT("The chamfer takes a triangle off each of the two front arrises"),
		Volume(SharpMesh) - Volume(Mesh), 2.0 * (0.2 * 0.2 * 0.5) * 180.0, 0.01);
	TestTrue(TEXT("An unchamfered run is a plain box and carries exact world-scale UVs"),
		HasWorldScaleUVs(SharpMesh, 100.0));

	// A chamfer is a cut, not a shrink: the moulding still measures what it was specified as.
	TestTrue(TEXT("The chamfer does not move the moulding off its declared envelope"),
		CorniceBoundsMatch(Bounds, SharpMesh.GetBounds(), 0.001));

	return true;
}

/**
 * The four sections, each cutting away exactly what it claims to.
 *
 * The profile IS the part - a cornice is nothing but its cross-section extruded - so this is where a
 * mistake actually lives. Every one of these produces a watertight, positive, correctly bounded
 * solid whether or not the section is the one asked for, which is why the assertion is on the area
 * cut away and not on the mesh being valid.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFCorniceProfileTest, "HouseForge.Joinery.CorniceProfile", HF_TEST_FLAGS)

bool FHFCorniceProfileTest::RunTest(const FString& Parameters)
{
	const EHFCorniceProfile Profiles[] = {
		EHFCorniceProfile::Square, EHFCorniceProfile::Splay,
		EHFCorniceProfile::Cove, EHFCorniceProfile::Stepped };
	const TCHAR* Names[] = { TEXT("A square profile"), TEXT("A splay"), TEXT("A cove"), TEXT("A step") };

	double Volumes[4] = {};

	for (int32 i = 0; i < 4; ++i)
	{
		FHFCorniceParams Params = MakeWallUnitCornice();
		Params.Profile = Profiles[i];
		Params.EdgeBevel = 0.0;		// so what is left is the profile's own section and nothing else

		const FDynamicMesh3 Mesh = FHFJoineryKit::GenerateCornice(Params);
		Volumes[i] = Volume(Mesh);

		TestTrue(*FString::Printf(TEXT("%s is watertight"), Names[i]), FHFMeshOps::IsClosed(Mesh));
		TestTrue(*FString::Printf(TEXT("%s faces outward"), Names[i]), Volumes[i] > 0.0);

		// Every profile fills the same envelope, which is what lets a run be restyled without the
		// cabinet under it or the ceiling over it having to move.
		TestTrue(*FString::Printf(TEXT("%s fills exactly the declared envelope"), Names[i]),
			CorniceBoundsMatch(Mesh.GetBounds(), DeclaredCorniceBox(Params), 0.001));

		const double Expected = CorniceSectionArea(Params) * Params.Width;
		TestNearlyEqual(*FString::Printf(TEXT("%s cuts away exactly what it describes"), Names[i]),
			Volumes[i], Expected, FMath::Abs(Expected) * 0.0001);
	}

	// And they are genuinely four different mouldings, in the order their sections say. The cove
	// sitting below the splay is also what proves it is a scoop rather than a bullnose: rounding that
	// arris off would leave more material than a straight splay, not less.
	TestTrue(TEXT("A splay removes more than a square profile"), Volumes[1] < Volumes[0]);
	TestTrue(TEXT("A cove is concave, so it removes more than a splay"), Volumes[2] < Volumes[1]);
	TestTrue(TEXT("A step removes more than a cove"), Volumes[3] < Volumes[2]);

	// The arc is drawn, not approximated away: more segments hug the true quarter circle more closely
	// and therefore scoop out more.
	FHFCorniceParams Coarse = MakeWallUnitCornice();
	Coarse.Profile = EHFCorniceProfile::Cove;
	Coarse.EdgeBevel = 0.0;
	Coarse.CoveSegments = 2;

	FHFCorniceParams Fine = Coarse;
	Fine.CoveSegments = 32;

	const double CoarseVolume = Volume(FHFJoineryKit::GenerateCornice(Coarse));
	const double FineVolume = Volume(FHFJoineryKit::GenerateCornice(Fine));

	TestTrue(TEXT("A finer cove scoops out more than a coarse one"), FineVolume < CoarseVolume);

	const double TrueQuarterCircle = (4.5 * 6.0 - UE_DOUBLE_PI * 2.0 * 2.0 * 0.25) * 180.0;
	TestNearlyEqual(TEXT("And a 32-segment cove is the quarter circle it was drawn from"),
		FineVolume, TrueQuarterCircle, FMath::Abs(TrueQuarterCircle) * 0.001);

	return true;
}

/**
 * Every parameter genuinely changes the output, and impossible ones do not produce garbage.
 *
 * Each is varied on its own and checked against the specific change it should have caused, rather
 * than against "the mesh differs" - which passes when a parameter is wired to the wrong axis.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFCorniceParametersTest, "HouseForge.Joinery.CorniceParameters", HF_TEST_FLAGS)

bool FHFCorniceParametersTest::RunTest(const FString& Parameters)
{
	const FHFCorniceParams Base = MakeWallUnitCornice();
	const double BaseVolume = Volume(FHFJoineryKit::GenerateCornice(Base));

	// Length: twice the run is twice the moulding, and nothing about the section moves.
	FHFCorniceParams Longer = Base;
	Longer.Width = 360.0;
	const FDynamicMesh3 LongerMesh = FHFJoineryKit::GenerateCornice(Longer);
	TestNearlyEqual(TEXT("A longer run is longer"), LongerMesh.GetBounds().Max.X, 360.0, 0.001);
	TestNearlyEqual(TEXT("Length scales the moulding linearly"),
		Volume(LongerMesh), BaseVolume * 2.0, FMath::Abs(BaseVolume) * 0.0001);
	TestNearlyEqual(TEXT("Length leaves the section alone"), LongerMesh.GetBounds().Min.Y, -2.5, 0.001);

	// Height: the 75 mm moulding that tops a wardrobe rather than a wall unit.
	FHFCorniceParams Taller = Base;
	Taller.Height = 7.5;
	const FDynamicMesh3 TallerMesh = FHFJoineryKit::GenerateCornice(Taller);
	TestNearlyEqual(TEXT("A taller moulding is taller"), TallerMesh.GetBounds().Max.Z, 7.5, 0.001);
	TestNearlyEqual(TEXT("Height adds exactly the section it lengthens"),
		Volume(TallerMesh) - BaseVolume, 1.5 * 4.5 * 180.0, 0.01);

	// Depth: reaches further back onto the carcass, without moving the face anybody sees.
	FHFCorniceParams Deeper = Base;
	Deeper.Depth = 6.0;
	const FDynamicMesh3 DeeperMesh = FHFJoineryKit::GenerateCornice(Deeper);
	TestNearlyEqual(TEXT("A deeper moulding reaches further onto the carcass"),
		DeeperMesh.GetBounds().Max.Y, 3.5, 0.001);
	TestNearlyEqual(TEXT("Depth does not move the front face"), DeeperMesh.GetBounds().Min.Y, -2.5, 0.001);
	TestNearlyEqual(TEXT("Depth adds exactly the section it widens"),
		Volume(DeeperMesh) - BaseVolume, 1.5 * 6.0 * 180.0, 0.01);

	// Projection is a setback and not a resize: the whole section slides forward off the shutter
	// plane and not a gram of material is gained or lost. Wire it to the depth instead - the easy
	// mistake, since both move the same face - and the result is still a plausible moulding, just one
	// that is wrong on every elevation.
	FHFCorniceParams Prouder = Base;
	Prouder.Projection = 4.0;
	const FDynamicMesh3 ProuderMesh = FHFJoineryKit::GenerateCornice(Prouder);
	TestNearlyEqual(TEXT("A prouder moulding stands further off the shutter face"),
		ProuderMesh.GetBounds().Min.Y, -4.0, 0.001);
	TestNearlyEqual(TEXT("And correspondingly less far back onto the carcass"),
		ProuderMesh.GetBounds().Max.Y, 0.5, 0.001);
	TestNearlyEqual(TEXT("Projection moves the moulding without resizing it"),
		Volume(ProuderMesh), BaseVolume, 0.01);

	// Profile size, on a profile that has a feature to size.
	FHFCorniceParams Splayed = Base;
	Splayed.Profile = EHFCorniceProfile::Splay;
	Splayed.EdgeBevel = 0.0;

	FHFCorniceParams Deeply = Splayed;
	Deeply.ProfileSize = 3.0;
	const FDynamicMesh3 DeeplyMesh = FHFJoineryKit::GenerateCornice(Deeply);

	TestTrue(TEXT("A bigger splay cuts more away"),
		Volume(DeeplyMesh) < Volume(FHFJoineryKit::GenerateCornice(Splayed)));
	TestTrue(TEXT("A bigger splay does not change the envelope"),
		CorniceBoundsMatch(DeeplyMesh.GetBounds(), DeclaredCorniceBox(Deeply), 0.001));

	// And is inert on one that has none. A square profile has no feature for it to size, and quietly
	// applying it anyway would put an unasked-for chamfer on the flat cornice most kitchens get.
	FHFCorniceParams SquareBig = Base;
	SquareBig.ProfileSize = 4.0;
	TestNearlyEqual(TEXT("Profile size is inert on a square profile"),
		Volume(FHFJoineryKit::GenerateCornice(SquareBig)), BaseVolume, 0.01);

	// A bigger chamfer takes correspondingly more off both arrises.
	FHFCorniceParams Softer = Base;
	Softer.EdgeBevel = 0.5;
	TestNearlyEqual(TEXT("A bigger chamfer takes more off the arrises"),
		BaseVolume - Volume(FHFJoineryKit::GenerateCornice(Softer)),
		(0.5 * 0.5 - 0.2 * 0.2) * 180.0, 0.01);

	// Nothing asked for is nothing built, rather than a sliver that renders as a black line under a
	// ceiling. A run with no wall units over it legitimately has no cornice.
	FHFCorniceParams NoRun = Base;
	NoRun.Width = 0.0;
	TestEqual(TEXT("No length means no cornice"), FHFJoineryKit::GenerateCornice(NoRun).TriangleCount(), 0);

	FHFCorniceParams NoHeight = Base;
	NoHeight.Height = 0.0;
	TestEqual(TEXT("No height means no cornice"), FHFJoineryKit::GenerateCornice(NoHeight).TriangleCount(), 0);

	FHFCorniceParams NoDepth = Base;
	NoDepth.Depth = 0.0;
	TestEqual(TEXT("No depth means no cornice"), FHFJoineryKit::GenerateCornice(NoDepth).TriangleCount(), 0);

	FHFCorniceParams Negative = Base;
	Negative.Width = -180.0;
	Negative.Height = -6.0;
	TestEqual(TEXT("Negative dimensions produce nothing at all"),
		FHFJoineryKit::GenerateCornice(Negative).TriangleCount(), 0);

	// Nonsense in must not mean nonsense out. A projection past the depth would hang the moulding off
	// the front of the shutters with its back in mid-air.
	FHFCorniceParams Overhung = Base;
	Overhung.Projection = 20.0;
	TestNearlyEqual(TEXT("A projection past the depth is clamped to land on the carcass"),
		FHFJoineryKit::SanitiseCornice(Overhung).Projection, 4.5, 0.001);

	const FDynamicMesh3 OverhungMesh = FHFJoineryKit::GenerateCornice(Overhung);
	TestTrue(TEXT("An over-projected moulding is still watertight"), FHFMeshOps::IsClosed(OverhungMesh));
	TestTrue(TEXT("An over-projected moulding still has positive volume"), Volume(OverhungMesh) > 0.0);
	TestNearlyEqual(TEXT("Its back lands on the shutter plane and goes no further"),
		OverhungMesh.GetBounds().Max.Y, 0.0, 0.001);

	// And a feature bigger than the section it is cut from would fold the cross-section through
	// itself, sweeping a self-intersecting shell that still reports a plausible volume.
	FHFCorniceParams Absurd = Base;
	Absurd.Profile = EHFCorniceProfile::Cove;
	Absurd.ProfileSize = 100.0;
	TestNearlyEqual(TEXT("A feature bigger than the section is clamped inside it"),
		FHFJoineryKit::SanitiseCornice(Absurd).ProfileSize, 4.05, 0.001);

	const FDynamicMesh3 AbsurdMesh = FHFJoineryKit::GenerateCornice(Absurd);
	TestTrue(TEXT("An absurd profile still yields a watertight solid"), FHFMeshOps::IsClosed(AbsurdMesh));
	TestTrue(TEXT("An absurd profile still yields positive volume"), Volume(AbsurdMesh) > 0.0);
	TestTrue(TEXT("An absurd profile still fills its declared envelope"),
		CorniceBoundsMatch(AbsurdMesh.GetBounds(), DeclaredCorniceBox(Base), 0.001));

	return true;
}

/**
 * Mounting: the same moulding, placed by an anchor in whatever space the target mesh is in.
 *
 * This is the whole reason a cornice comes back in a local frame rather than placing itself. A
 * cornice does not move, so it is not a part - but a cornice carried on something that DOES move has
 * to be generated in that part's local space and appended into that part's mesh, and then it travels
 * with the part for free. That only works if the geometry depends on the anchor and on nothing else.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFCorniceMountingTest, "HouseForge.Joinery.CorniceMounting", HF_TEST_FLAGS)

bool FHFCorniceMountingTest::RunTest(const FString& Parameters)
{
	const FHFCorniceParams Params = MakeWallUnitCornice();
	const FDynamicMesh3 Local = FHFJoineryKit::GenerateCornice(Params);
	const double LocalVolume = Volume(Local);

	const FTransform Anchor(FRotator(0.0, 90.0, 0.0), FVector(100.0, 50.0, 250.0));

	FDynamicMesh3 Mounted;
	FHFMeshOps::InitialiseMesh(Mounted);
	if (!TestTrue(TEXT("A cornice can be appended into a mesh somebody else owns"),
		FHFJoineryKit::AppendCornice(Mounted, Params, Anchor)))
	{
		return false;
	}

	TestTrue(TEXT("The mounted cornice is watertight"), FHFMeshOps::IsClosed(Mounted));
	TestTrue(TEXT("The mounted cornice faces outward"), Volume(Mounted) > 0.0);
	TestNearlyEqual(TEXT("Anchoring moves the moulding without resizing it"),
		Volume(Mounted), LocalVolume, FMath::Abs(LocalVolume) * 0.0001);
	TestTrue(TEXT("It lands exactly where the anchor puts it"),
		CorniceBoundsMatch(Mounted.GetBounds(), CorniceBoundsUnderAnchor(Local.GetBounds(), Anchor), 0.001));

	// Two runs in one mesh: how a cornice turns the corner onto its return, and how a whole row of
	// wall units ends up capped by one shell.
	FDynamicMesh3 Pair;
	FHFMeshOps::InitialiseMesh(Pair);
	FHFJoineryKit::AppendCornice(Pair, Params, FTransform::Identity);
	FHFJoineryKit::AppendCornice(Pair, Params, FTransform(FRotator(0.0, 90.0, 0.0), FVector(300.0, 0.0, 0.0)));

	TestTrue(TEXT("Two runs in one mesh are still watertight"), FHFMeshOps::IsClosed(Pair));
	TestNearlyEqual(TEXT("And hold exactly twice the moulding"),
		Volume(Pair), LocalVolume * 2.0, FMath::Abs(LocalVolume) * 0.0001);

	// Appended into a mesh that was never set up for roles, the roles must survive - otherwise the
	// moulding renders perfectly and cannot be re-materialled, which is a defect nobody sees.
	FDynamicMesh3 Bare;
	TestTrue(TEXT("A cornice can be appended into a bare mesh"),
		FHFJoineryKit::AppendCornice(Bare, Params, FTransform::Identity));

	for (const int32 Tid : Bare.TriangleIndicesItr())
	{
		if (Bare.GetTriangleGroup(Tid) == 0)
		{
			AddError(TEXT("Appending into a bare mesh dropped the surface-role polygroups."));
			break;
		}
	}

	// Nothing to append is not a failure, but it must leave the caller's mesh exactly as it was
	// rather than half-writing into it.
	FHFCorniceParams Nothing = Params;
	Nothing.Width = 0.0;
	const int32 Before = Pair.TriangleCount();
	TestFalse(TEXT("Appending nothing reports that it appended nothing"),
		FHFJoineryKit::AppendCornice(Pair, Nothing, FTransform::Identity));
	TestEqual(TEXT("And leaves the mesh untouched"), Pair.TriangleCount(), Before);

	return true;
}

#undef HF_TEST_FLAGS

#endif // WITH_DEV_AUTOMATION_TESTS
