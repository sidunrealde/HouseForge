// Copyright Siddartha G. All Rights Reserved.

#include "HouseForge.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "DynamicMesh/MeshTransforms.h"
#include "Geometry/HFJoineryKit.h"
#include "Geometry/HFMeshOps.h"
#include "MeshQueries.h"
#include "Misc/AutomationTest.h"
#include "Model/HFArticulation.h"
#include "Model/HFTypes.h"

using namespace UE::Geometry;

#define HF_TEST_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
	/** A 450 x 2100 wardrobe bay with the default 19 mm leaf and 3 mm reveal. */
	FHFShutterParams MakeShutterParams()
	{
		FHFShutterParams Params;
		Params.ModuleWidth = 45.0;
		Params.ModuleHeight = 210.0;
		Params.Thickness = 1.9;
		Params.RevealGap = 0.3;
		Params.BackClearance = 0.1;
		Params.OpenAngleDegrees = 100.0;
		Params.Hinge = EHFShutterHinge::Left;
		return Params;
	}

	double Volume(const FDynamicMesh3& Mesh)
	{
		return TMeshQueries<FDynamicMesh3>::GetVolumeArea(Mesh).X;
	}

	/** A part's mesh moved into the module frame at a given open amount. */
	FDynamicMesh3 PosedMesh(const FHFMeshPart& Part, double OpenAmount)
	{
		FHFPartState State;
		State.PartId = Part.PartId;
		State.PivotTransform = Part.PivotTransform;
		State.Motion = Part.Motion;
		State.OpenAmount = OpenAmount;

		FDynamicMesh3 Posed = Part.Mesh;
		MeshTransforms::ApplyTransform(Posed, FTransformSRT3d(State.CurrentPose()), true);
		return Posed;
	}

	FAxisAlignedBox3d PosedBounds(const FHFMeshPart& Part, double OpenAmount)
	{
		return PosedMesh(Part, OpenAmount).GetBounds();
	}

	// Named for the axis rather than for the thing measured. FAxisAlignedBox3d's own Width/Height/
	// Depth are X/Y/Z, which reads as X/Z/Y to anyone thinking about a shutter, and a test that
	// measures the wrong axis fails loudly here but would pass silently the moment two axes
	// happened to match.
	double SpanX(const FAxisAlignedBox3d& Box) { return Box.Max.X - Box.Min.X; }
	double SpanY(const FAxisAlignedBox3d& Box) { return Box.Max.Y - Box.Min.Y; }
	double SpanZ(const FAxisAlignedBox3d& Box) { return Box.Max.Z - Box.Min.Z; }

	/** Bounds of just the triangles carrying one surface role. */
	FAxisAlignedBox3d BoundsOfRole(const FDynamicMesh3& Mesh, EHFSurfaceRole Role)
	{
		FAxisAlignedBox3d Box = FAxisAlignedBox3d::Empty();
		for (const int32 Tid : Mesh.TriangleIndicesItr())
		{
			if (FHFMeshOps::RoleForGroup(Mesh.GetTriangleGroup(Tid)) != Role)
			{
				continue;
			}
			const FIndex3i Tri = Mesh.GetTriangle(Tid);
			for (int32 Corner = 0; Corner < 3; ++Corner)
			{
				Box.Contain(Mesh.GetVertex(Tri[Corner]));
			}
		}
		return Box;
	}

	bool HasRole(const FDynamicMesh3& Mesh, EHFSurfaceRole Role)
	{
		for (const int32 Tid : Mesh.TriangleIndicesItr())
		{
			if (FHFMeshOps::RoleForGroup(Mesh.GetTriangleGroup(Tid)) == Role)
			{
				return true;
			}
		}
		return false;
	}

	/**
	 * The leaf's leading edge on the hinge axis, in the leaf's own local space.
	 *
	 * Read off the leaf's own box rather than assumed to be at +LeafWidth: a right-hung leaf is cut
	 * on -X of its axis, so a hard-coded +X tip measures a point in mid-air beside the leaf, which
	 * turns out to move plausibly enough to pass a direction test the leaf itself would fail.
	 */
	FVector LeafTipLocal(const FHFShutterParams& Params)
	{
		const FBox Panel = FHFJoineryKit::ShutterPanelBox(Params);
		const bool bLeft = FHFJoineryKit::ShutterLeadingEdge(Params) == EHFHandleEdge::MaxX;
		return FVector(bLeft ? Panel.Max.X : Panel.Min.X, 0.0, 0.0);
	}

	/** Plan direction from the hinge axis to the leaf's leading edge, at a given open amount. */
	FVector2D LeafAxisDirection(const FHFShutterParams& Params, const FHFMeshPart& Part, double OpenAmount)
	{
		FHFPartState State;
		State.PivotTransform = Part.PivotTransform;
		State.Motion = Part.Motion;

		const FVector Hinge = State.PoseAt(OpenAmount).TransformPosition(FVector::ZeroVector);
		const FVector Tip = State.PoseAt(OpenAmount).TransformPosition(LeafTipLocal(Params));

		return FVector2D(Tip.X - Hinge.X, Tip.Y - Hinge.Y).GetSafeNormal();
	}
}

/**
 * A solid shutter leaf: the reveal it leaves, the solid it is, and where it sits in its module.
 *
 * Bounds are checked in the module frame rather than the leaf's own, because the module frame is
 * what a carcass composes against. A leaf that is the right size in its own space and half a reveal
 * out in its module is exactly the failure that only shows up once a run of them is placed.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFShutterLeafTest, "HouseForge.Joinery.ShutterLeaf", HF_TEST_FLAGS)

bool FHFShutterLeafTest::RunTest(const FString& Parameters)
{
	const FHFShutterParams Params = MakeShutterParams();
	const FDynamicMesh3 Leaf = FHFJoineryKit::GenerateShutter(Params);

	if (!TestTrue(TEXT("A shutter leaf has geometry"), Leaf.TriangleCount() > 0))
	{
		return false;
	}

	TestTrue(TEXT("The leaf is watertight"), FHFMeshOps::IsClosed(Leaf));
	TestTrue(TEXT("The leaf faces outward"), Volume(Leaf) > 0.0);

	// The declared size, as a volume rather than a triangle count: 447 x 19 x 2097 mm.
	const double Expected = Params.LeafWidth() * Params.Thickness * Params.LeafHeight();
	TestNearlyEqual(TEXT("The leaf is a solid of the declared size"), Volume(Leaf), Expected, Expected * 1e-6);

	// Part-local: the hinge axis is the origin, the leaf runs out along +X and up from its bottom.
	const FAxisAlignedBox3d Local = Leaf.GetBounds();
	TestNearlyEqual(TEXT("The leaf starts on its own hinge axis"), Local.Min.X, 0.0, 1e-9);
	TestNearlyEqual(TEXT("The leaf runs its width along local X"), Local.Max.X, Params.LeafWidth(), 1e-9);
	TestNearlyEqual(TEXT("The leaf sits on its own bottom edge"), Local.Min.Z, 0.0, 1e-9);
	TestNearlyEqual(TEXT("The leaf runs its height along local Z"), Local.Max.Z, Params.LeafHeight(), 1e-9);
	TestNearlyEqual(TEXT("The leaf is its declared thickness"), Local.Max.Y - Local.Min.Y, Params.Thickness, 1e-9);

	// The axis lies ON one face rather than at mid-thickness. That is the property the swing test
	// turns into a clearance guarantee, so it is worth pinning down here rather than inferring it.
	TestTrue(TEXT("The hinge axis lies on a face of the leaf, not inside it"),
		FMath::IsNearlyZero(Local.Min.Y, 1e-9) || FMath::IsNearlyZero(Local.Max.Y, 1e-9));

	// A solid leaf is one material all over; a stray untagged triangle cannot be re-materialled.
	TestTrue(TEXT("The leaf is laminate"), HasRole(Leaf, EHFSurfaceRole::ShutterLaminate));
	TestFalse(TEXT("A solid leaf has no glass"), HasRole(Leaf, EHFSurfaceRole::Glass));
	for (const int32 Tid : Leaf.TriangleIndicesItr())
	{
		if (FHFMeshOps::RoleForGroup(Leaf.GetTriangleGroup(Tid)) != EHFSurfaceRole::ShutterLaminate)
		{
			AddError(TEXT("A solid shutter triangle was emitted without the ShutterLaminate role."));
			break;
		}
	}

	TestTrue(TEXT("The leaf carries UVs"),
		Leaf.HasAttributes() && Leaf.Attributes()->PrimaryUV() != nullptr
			&& Leaf.Attributes()->PrimaryUV()->ElementCount() > 0);

	// Module frame: half a reveal in from every edge of the bay, hanging in front of the carcass.
	const FHFMeshPart Part = FHFJoineryKit::BuildShutterPart(Params, TEXT("Shutter"));
	const FAxisAlignedBox3d Closed = PosedBounds(Part, 0.0);
	const double HalfReveal = Params.RevealGap * 0.5;

	TestNearlyEqual(TEXT("Closed, the leaf starts half a reveal in"), Closed.Min.X, HalfReveal, 1e-9);
	TestNearlyEqual(TEXT("Closed, the leaf stops half a reveal short"),
		Closed.Max.X, Params.ModuleWidth - HalfReveal, 1e-9);
	TestNearlyEqual(TEXT("Closed, the leaf clears the bay bottom"), Closed.Min.Z, HalfReveal, 1e-9);
	TestNearlyEqual(TEXT("Closed, the leaf clears the bay top"),
		Closed.Max.Z, Params.ModuleHeight - HalfReveal, 1e-9);

	// The carcass is everything at Y >= 0, so a closed leaf hangs entirely at negative Y with the
	// clearance its hinge leaves.
	TestNearlyEqual(TEXT("Closed, the leaf back stands off the carcass"),
		Closed.Max.Y, -Params.BackClearance, 1e-9);
	TestNearlyEqual(TEXT("Closed, the leaf front is its thickness proud of that"),
		Closed.Min.Y, -(Params.BackClearance + Params.Thickness), 1e-9);

	TestEqual(TEXT("The part carries the id it was given"), Part.PartId, FName(TEXT("Shutter")));
	TestNearlyEqual(TEXT("A freshly generated shutter starts closed"), Part.DefaultOpenAmount, 0.0, 1e-9);

	return true;
}

/**
 * The reveal, which is the whole reason a run of shutters reads as separate leaves.
 *
 * Measured as the actual gap between two adjacent leaves placed in adjacent modules, because that
 * is the thing being bought - a leaf that is the right size but centred wrong closes the gap on one
 * side and doubles it on the other, and no single-leaf assertion catches it.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFShutterRevealTest, "HouseForge.Joinery.ShutterReveal", HF_TEST_FLAGS)

bool FHFShutterRevealTest::RunTest(const FString& Parameters)
{
	const FHFShutterParams Params = MakeShutterParams();

	// Two bays side by side. The second module's frame is one module width along, so its closed
	// leaf is the first one's shifted by exactly ModuleWidth.
	const FHFMeshPart Part = FHFJoineryKit::BuildShutterPart(Params, TEXT("Shutter"));
	const FAxisAlignedBox3d First = PosedBounds(Part, 0.0);

	const double NeighbourMinX = First.Min.X + Params.ModuleWidth;
	TestNearlyEqual(TEXT("Adjacent leaves are separated by exactly one reveal"),
		NeighbourMinX - First.Max.X, Params.RevealGap, 1e-9);

	// Half from each side, so a run stays centred on its bays instead of creeping along.
	TestNearlyEqual(TEXT("The reveal is taken half from each side"),
		First.Min.X, Params.ModuleWidth - First.Max.X, 1e-9);

	// And it is genuinely the parameter doing the work, not a constant that happens to match.
	FHFShutterParams Wider = Params;
	Wider.RevealGap = 1.2;
	const FAxisAlignedBox3d WiderClosed =
		PosedBounds(FHFJoineryKit::BuildShutterPart(Wider, TEXT("Shutter")), 0.0);

	TestNearlyEqual(TEXT("A wider reveal is a wider gap"),
		(WiderClosed.Min.X + Wider.ModuleWidth) - WiderClosed.Max.X, 1.2, 1e-9);
	TestTrue(TEXT("A wider reveal cuts a narrower leaf"), SpanX(WiderClosed) < SpanX(First));

	// Zero reveal is a legitimate answer - a single leaf in its own bay has no neighbour to clear -
	// and it must actually fill the module rather than quietly keeping a default gap.
	FHFShutterParams Flush = Params;
	Flush.RevealGap = 0.0;
	const FAxisAlignedBox3d FlushClosed =
		PosedBounds(FHFJoineryKit::BuildShutterPart(Flush, TEXT("Shutter")), 0.0);

	TestNearlyEqual(TEXT("No reveal fills the bay"), SpanX(FlushClosed), Flush.ModuleWidth, 1e-9);
	TestNearlyEqual(TEXT("No reveal starts at the bay edge"), FlushClosed.Min.X, 0.0, 1e-9);

	return true;
}

/** A glazed leaf: a stile-and-rail frame with a pane of real thickness set into its rebate. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFShutterGlassTest, "HouseForge.Joinery.ShutterGlassInsert", HF_TEST_FLAGS)

bool FHFShutterGlassTest::RunTest(const FString& Parameters)
{
	FHFShutterParams Params = MakeShutterParams();
	Params.bGlassInsert = true;
	Params.StileWidth = 6.0;
	Params.GlassThickness = 0.5;
	Params.GlassRebate = 0.6;

	const FDynamicMesh3 Glazed = FHFJoineryKit::GenerateShutter(Params);

	TestTrue(TEXT("A glazed leaf is watertight"), FHFMeshOps::IsClosed(Glazed));
	TestTrue(TEXT("A glazed leaf faces outward"), Volume(Glazed) > 0.0);

	const double W = Params.LeafWidth();
	const double H = Params.LeafHeight();
	const double T = Params.Thickness;
	const double S = Params.StileWidth;

	// Set out from the leaf's own dimensions rather than from the generator's construction, because
	// the defect this replaces was a construction and an expectation that agreed with each other.
	//
	// Three regions, and no two of them may occupy the same space:
	//   - outside the pane's footprint, the frame is the full leaf thickness;
	//   - over the rebate strip, the frame is two shoulders with the glass groove between them;
	//   - inside the frame's opening, the pane and nothing else.
	const double PaneInset = S - Params.GlassRebate;
	const double PaneWidth = W - 2.0 * PaneInset;
	const double PaneHeight = H - 2.0 * PaneInset;

	const double OuterBandArea = W * H - PaneWidth * PaneHeight;
	const double RebateRingArea = PaneWidth * PaneHeight - (W - 2.0 * S) * (H - 2.0 * S);

	const double Expected = OuterBandArea * T
		+ RebateRingArea * (T - Params.GlassThickness)
		+ PaneWidth * PaneHeight * Params.GlassThickness;

	TestNearlyEqual(TEXT("A glazed leaf is a rebated frame with a pane in the groove"),
		Volume(Glazed), Expected, Expected * 1e-6);

	// Named explicitly, because a frame emitted at full thickness across the rebate would report
	// exactly this much board it does not contain - and would pass every other check here, since
	// each box is closed on its own and the bounds are unchanged.
	const double DoubleCounted = RebateRingArea * Params.GlassThickness;
	TestTrue(TEXT("The rebate is really cut, and there is board to cut"), DoubleCounted > 0.0);
	TestNearlyEqual(TEXT("No part of the leaf is counted as both ply and glass"),
		(Expected + DoubleCounted) - Volume(Glazed), DoubleCounted, Expected * 1e-6);

	// Less material than a solid leaf, which is the point of glazing one.
	const FDynamicMesh3 Solid = FHFJoineryKit::GenerateShutter(MakeShutterParams());
	TestTrue(TEXT("A glazed leaf holds less board than a solid one"), Volume(Glazed) < Volume(Solid));

	// Same outside dimensions, so a glazed leaf drops into a run of solid ones without a step.
	TestTrue(TEXT("Glazing does not change the leaf's size"),
		Glazed.GetBounds().Min.Equals(Solid.GetBounds().Min, 1e-9)
			&& Glazed.GetBounds().Max.Equals(Solid.GetBounds().Max, 1e-9));

	TestTrue(TEXT("A glazed leaf has glass"), HasRole(Glazed, EHFSurfaceRole::Glass));
	TestTrue(TEXT("A glazed leaf still has a laminate frame"),
		HasRole(Glazed, EHFSurfaceRole::ShutterLaminate));

	// Glass with thickness, never a plane, or refraction and reflection read wrong.
	const FAxisAlignedBox3d Pane = BoundsOfRole(Glazed, EHFSurfaceRole::Glass);
	TestNearlyEqual(TEXT("The pane is a solid of the declared thickness"),
		Pane.Max.Y - Pane.Min.Y, Params.GlassThickness, 1e-9);
	TestTrue(TEXT("The pane sits within the leaf's thickness"),
		Pane.Min.Y >= Glazed.GetBounds().Min.Y - 1e-9 && Pane.Max.Y <= Glazed.GetBounds().Max.Y + 1e-9);

	// It runs under the frame by the rebate, so the join is a shadow and not a hole.
	TestNearlyEqual(TEXT("The pane runs under the frame by the rebate"), Pane.Min.X, PaneInset, 1e-9);
	TestNearlyEqual(TEXT("The pane runs under the far stile too"), Pane.Max.X, W - PaneInset, 1e-9);
	TestTrue(TEXT("The pane is captured by the frame"), Pane.Min.X < S && Pane.Max.X > W - S);

	// The downstream consequence of getting the rebate wrong, and the reason it is not merely a
	// bookkeeping error: SubtractInPlace refuses any boolean whose result is not closed and leaves
	// the target uncut, so a self-intersecting glazed leaf takes no handle recess at all - and the
	// leaf that comes back looks, from the front and closed, exactly like one that did.
	{
		FHFHandleParams Routed;
		Routed.Style = EHFHandleStyle::JProfile;
		Routed.PanelBox = FHFJoineryKit::ShutterPanelBox(Params);
		Routed.Facing = EHFPanelFacing::NegativeY;
		Routed.Edge = FHFJoineryKit::ShutterLeadingEdge(Params);

		FDynamicMesh3 WithHandle = Glazed;
		TestTrue(TEXT("A glazed leaf takes a routed handle"),
			FHFJoineryKit::ApplyHandle(WithHandle, Routed));
		TestTrue(TEXT("Routing a glazed leaf actually cuts board out of it"),
			Volume(WithHandle) < Volume(Glazed) - 0.1);
	}

	// A frame with no room for a pane is a solid leaf, not a broken one.
	FHFShutterParams Impossible = Params;
	Impossible.StileWidth = 40.0;
	const FDynamicMesh3 Fallback = FHFJoineryKit::GenerateShutter(Impossible);

	TestTrue(TEXT("A frame too wide to glaze falls back to a solid leaf"), FHFMeshOps::IsClosed(Fallback));
	TestNearlyEqual(TEXT("The fallback is the solid leaf"), Volume(Fallback), Volume(Solid), Volume(Solid) * 1e-6);
	TestFalse(TEXT("The fallback has no stray glass"), HasRole(Fallback, EHFSurfaceRole::Glass));

	return true;
}

/** Every parameter has to move the geometry, or it is decoration on a details panel. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFShutterParametersTest, "HouseForge.Joinery.ShutterParameters", HF_TEST_FLAGS)

bool FHFShutterParametersTest::RunTest(const FString& Parameters)
{
	const FHFShutterParams Base = MakeShutterParams();
	const FDynamicMesh3 BaseMesh = FHFJoineryKit::GenerateShutter(Base);
	const double BaseVolume = Volume(BaseMesh);

	{
		FHFShutterParams Wide = Base;
		Wide.ModuleWidth = 60.0;
		const FDynamicMesh3 Mesh = FHFJoineryKit::GenerateShutter(Wide);
		TestNearlyEqual(TEXT("A wider module cuts a wider leaf"),
			SpanX(Mesh.GetBounds()), Wide.LeafWidth(), 1e-9);
		TestTrue(TEXT("A wider leaf holds more board"), Volume(Mesh) > BaseVolume);
	}

	{
		FHFShutterParams Tall = Base;
		Tall.ModuleHeight = 240.0;
		const FDynamicMesh3 Mesh = FHFJoineryKit::GenerateShutter(Tall);
		TestNearlyEqual(TEXT("A taller module cuts a taller leaf"),
			SpanZ(Mesh.GetBounds()), Tall.LeafHeight(), 1e-9);
		TestTrue(TEXT("A taller leaf holds more board"), Volume(Mesh) > BaseVolume);
	}

	{
		FHFShutterParams Thick = Base;
		Thick.Thickness = 2.5;
		const FDynamicMesh3 Mesh = FHFJoineryKit::GenerateShutter(Thick);
		TestNearlyEqual(TEXT("Thickness is honoured"), SpanY(Mesh.GetBounds()), 2.5, 1e-9);
		TestNearlyEqual(TEXT("A thicker leaf holds proportionally more board"),
			Volume(Mesh), BaseVolume * (2.5 / Base.Thickness), BaseVolume * 1e-6);
	}

	{
		// Back clearance moves the leaf without resizing it: it is a standoff, not a dimension.
		FHFShutterParams Standoff = Base;
		Standoff.BackClearance = 0.5;
		const FAxisAlignedBox3d Moved =
			PosedBounds(FHFJoineryKit::BuildShutterPart(Standoff, TEXT("S")), 0.0);
		TestNearlyEqual(TEXT("Back clearance stands the leaf off the carcass"), Moved.Max.Y, -0.5, 1e-9);
		TestNearlyEqual(TEXT("Back clearance does not resize the leaf"),
			SpanY(Moved), Base.Thickness, 1e-9);
	}

	{
		// Handedness changes where the leaf hangs from and which way it turns, and nothing else. A
		// run of alternating shutters has to line up, so the closed pose must be identical.
		FHFShutterParams Right = Base;
		Right.Hinge = EHFShutterHinge::Right;

		const FHFMeshPart LeftPart = FHFJoineryKit::BuildShutterPart(Base, TEXT("S"));
		const FHFMeshPart RightPart = FHFJoineryKit::BuildShutterPart(Right, TEXT("S"));

		const FAxisAlignedBox3d LeftClosed = PosedBounds(LeftPart, 0.0);
		const FAxisAlignedBox3d RightClosed = PosedBounds(RightPart, 0.0);

		TestTrue(TEXT("Both hands close to the same place"),
			LeftClosed.Min.Equals(RightClosed.Min, 1e-9) && LeftClosed.Max.Equals(RightClosed.Max, 1e-9));
		TestNearlyEqual(TEXT("Both hands cut the same leaf"),
			Volume(FHFJoineryKit::GenerateShutter(Right)), BaseVolume, BaseVolume * 1e-6);

		TestTrue(TEXT("The hands turn opposite ways"),
			LeftPart.Motion.MaxAngleDegrees * RightPart.Motion.MaxAngleDegrees < 0.0);
		TestNearlyEqual(TEXT("A left-hung leaf hinges on the left edge"),
			LeftPart.PivotTransform.GetLocation().X, Base.RevealGap * 0.5, 1e-9);
		TestNearlyEqual(TEXT("A right-hung leaf hinges on the right edge"),
			RightPart.PivotTransform.GetLocation().X, Base.ModuleWidth - Base.RevealGap * 0.5, 1e-9);

		// ------------------------------------------------------- the frame anything mounted relies on
		//
		// Handedness must not reach into the leaf's own local Y. Both hands carry their board on +Y
		// of the hinge axis and therefore look out along -Y, which is what lets a handle, a routed
		// groove or a glued-on mirror be described once and fitted to either hand. A leaf that
		// presented its outward face at +Y for one hand would take every mounted part with it, and
		// closed - which is how a wardrobe is photographed - the result is indistinguishable.
		const FAxisAlignedBox3d LeftLocal = LeftPart.Mesh.GetBounds();
		const FAxisAlignedBox3d RightLocal = RightPart.Mesh.GetBounds();

		TestNearlyEqual(TEXT("A left-hung leaf looks out of the cupboard along its own -Y"),
			LeftLocal.Min.Y, 0.0, 1e-9);
		TestNearlyEqual(TEXT("A right-hung leaf looks out of the cupboard along its own -Y too"),
			RightLocal.Min.Y, 0.0, 1e-9);
		TestNearlyEqual(TEXT("A left-hung leaf's board is behind that face"),
			LeftLocal.Max.Y, Base.Thickness, 1e-9);
		TestNearlyEqual(TEXT("A right-hung leaf's board is behind that face"),
			RightLocal.Max.Y, Right.Thickness, 1e-9);

		// The pivot is a pure translation for both, which is the mechanism that guarantees it: a
		// half-turn would take one hand's -Y to the module's +Y and flip its outward face.
		TestTrue(TEXT("A leaf hangs on a translation, not a turn"),
			LeftPart.PivotTransform.GetRotation().IsIdentity(1e-9)
				&& RightPart.PivotTransform.GetRotation().IsIdentity(1e-9));

		// What handedness does change: which side of the axis the leaf is cut on, and therefore
		// which edge it opens from. Stated as the box a mounted part is fitted to.
		TestTrue(TEXT("A left-hung leaf is cut on +X of its hinge"),
			LeftLocal.Min.X > -1e-9 && FMath::IsNearlyEqual(LeftLocal.Max.X, Base.LeafWidth(), 1e-9));
		TestTrue(TEXT("A right-hung leaf is cut on -X of its hinge"),
			RightLocal.Max.X < 1e-9 && FMath::IsNearlyEqual(RightLocal.Min.X, -Right.LeafWidth(), 1e-9));

		TestTrue(TEXT("The leaf's declared panel box is the leaf that was cut"),
			FHFJoineryKit::ShutterPanelBox(Base).Min.Equals(FVector(LeftLocal.Min), 1e-9)
				&& FHFJoineryKit::ShutterPanelBox(Base).Max.Equals(FVector(LeftLocal.Max), 1e-9)
				&& FHFJoineryKit::ShutterPanelBox(Right).Min.Equals(FVector(RightLocal.Min), 1e-9)
				&& FHFJoineryKit::ShutterPanelBox(Right).Max.Equals(FVector(RightLocal.Max), 1e-9));
		TestTrue(TEXT("The declared leading edge is the one away from the hinge"),
			FHFJoineryKit::ShutterLeadingEdge(Base) == EHFHandleEdge::MaxX
				&& FHFJoineryKit::ShutterLeadingEdge(Right) == EHFHandleEdge::MinX);
	}

	{
		// The open angle is the swing, so it has to change where the leaf ends up at open amount 1.
		FHFShutterParams Narrow = Base;
		Narrow.OpenAngleDegrees = 45.0;

		const FVector2D Wide = LeafAxisDirection(Base, FHFJoineryKit::BuildShutterPart(Base, TEXT("S")), 1.0);
		const FVector2D Less = LeafAxisDirection(Narrow, FHFJoineryKit::BuildShutterPart(Narrow, TEXT("S")), 1.0);
		const FVector2D Shut = LeafAxisDirection(Base, FHFJoineryKit::BuildShutterPart(Base, TEXT("S")), 0.0);

		TestNearlyEqual(TEXT("The declared angle is the angle swung"),
			FMath::RadiansToDegrees(FMath::Acos(FVector2D::DotProduct(Shut, Wide))), 100.0, 0.001);
		TestNearlyEqual(TEXT("A smaller angle swings less"),
			FMath::RadiansToDegrees(FMath::Acos(FVector2D::DotProduct(Shut, Less))), 45.0, 0.001);
	}

	{
		// Nonsense in, nothing out. A degenerate leaf that renders is worse than no leaf.
		FHFShutterParams Eaten = Base;
		Eaten.RevealGap = 100.0;
		TestEqual(TEXT("A reveal wider than the module produces no leaf"),
			FHFJoineryKit::GenerateShutter(Eaten).TriangleCount(), 0);

		FHFShutterParams Flat = Base;
		Flat.Thickness = 0.0;
		TestEqual(TEXT("A leaf with no thickness produces nothing"),
			FHFJoineryKit::GenerateShutter(Flat).TriangleCount(), 0);
	}

	return true;
}

/**
 * The swing itself: closed at 0, at the declared limit at 1, and clear of everything in between.
 *
 * The clearance assertions are the ones that matter. A shutter that ends up in the right place at
 * both ends of its travel and passes through the carcass on the way reads as correct in every still
 * and wrong the moment anything moves - and there is no rendering artefact to notice, because the
 * geometry really does intersect.
 *
 * Both are proved by a separating plane over every vertex of the swept leaf rather than by sampling
 * for overlap, so a pass means disjoint and not merely "no sample landed inside".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFShutterSwingTest, "HouseForge.Joinery.ShutterSwing", HF_TEST_FLAGS)

bool FHFShutterSwingTest::RunTest(const FString& Parameters)
{
	const EHFShutterHinge Hands[] = { EHFShutterHinge::Left, EHFShutterHinge::Right };

	for (const EHFShutterHinge Hand : Hands)
	{
		for (int32 Variant = 0; Variant < 2; ++Variant)
		{
			FHFShutterParams Params = MakeShutterParams();
			Params.Hinge = Hand;
			Params.bGlassInsert = (Variant == 1);

			const bool bLeft = Hand == EHFShutterHinge::Left;
			const FHFMeshPart Part = FHFJoineryKit::BuildShutterPart(Params, TEXT("Shutter"));

			// It moves, and it moves the way a shutter moves.
			TestTrue(TEXT("A shutter hinges"), Part.Motion.Type == EHFMotionType::Hinge);
			TestTrue(TEXT("A shutter hinges about a vertical axis"),
				Part.Motion.UnitAxis().Equals(FVector::ZAxisVector, 1e-9));
			TestNearlyEqual(TEXT("It swings its declared angle"),
				FMath::Abs(Part.Motion.MaxAngleDegrees), Params.OpenAngleDegrees, 1e-9);

			// Closed at 0 means exactly where the generator put it, not merely near it.
			const FAxisAlignedBox3d Closed = PosedBounds(Part, 0.0);
			const double HalfReveal = Params.RevealGap * 0.5;
			TestNearlyEqual(TEXT("At 0 the leaf fills its module"), Closed.Min.X, HalfReveal, 1e-9);
			TestNearlyEqual(TEXT("At 0 the leaf fills its module"),
				Closed.Max.X, Params.ModuleWidth - HalfReveal, 1e-9);
			TestNearlyEqual(TEXT("At 0 the leaf lies flat on the carcass front"),
				SpanY(Closed), Params.Thickness, 1e-9);

			// At 1 it stands at exactly the declared angle, measured on where the leaf actually
			// points rather than read back off the transform it was built from.
			const FVector2D Shut = LeafAxisDirection(Params, Part, 0.0);
			const FVector2D Open = LeafAxisDirection(Params, Part, 1.0);
			TestNearlyEqual(TEXT("At 1 the leaf stands at its declared angle"),
				FMath::RadiansToDegrees(FMath::Acos(FVector2D::DotProduct(Shut, Open))),
				Params.OpenAngleDegrees, 0.001);

			// Opening carries the leading edge out of the cabinet, never back into it.
			TestTrue(TEXT("Opening swings the leaf out of the unit"), Open.Y < 0.0);
			TestTrue(TEXT("The leaf swings away from its own hinge"),
				bLeft ? Open.X < 0.0 : Open.X > 0.0);

			// A hinge is a rotation, so the leading edge stays exactly a leaf-width from the axis.
			// A pivot placed at the leaf's centre passes the end-pose tests and fails this one.
			FHFPartState State;
			State.PivotTransform = Part.PivotTransform;
			State.Motion = Part.Motion;

			const FVector Axis = Part.PivotTransform.GetLocation();
			for (int32 Step = 0; Step <= 40; ++Step)
			{
				const double Alpha = Step / 40.0;
				const FVector Tip = State.PoseAt(Alpha).TransformPosition(LeafTipLocal(Params));
				TestNearlyEqual(TEXT("The leaf stays a leaf-width from its hinge"),
					FVector2D(Tip.X - Axis.X, Tip.Y - Axis.Y).Size(), Params.LeafWidth(), 1e-6);
			}

			// ------------------------------------------------------- clearance, at every angle
			//
			// The module frame puts the carcass front plane at Y = 0 with the carcass behind it, so
			// any carcass whatsoever - whatever its depth, however thick its sides - lies entirely
			// at Y >= 0. A leaf that stays strictly at Y < 0 through its whole travel therefore
			// cannot intersect it, and the plane Y = 0 is the certificate.
			//
			// The neighbour is the harder case. Its closed leaf occupies the strip between
			// X = hinge edge and the next module along, at the leaf's own depth. Below 90 degrees
			// the swinging leaf never crosses its own hinge plane, so that plane separates them;
			// beyond 90 its trailing corner has come forward of the neighbour's front face, so
			// that face's plane does. One or the other holds at every angle, and the test insists
			// on it rather than assuming which.
			const double HingeX = Part.PivotTransform.GetLocation().X;
			const double FrontY = -(Params.BackClearance + Params.Thickness);

			double WorstCarcassClearance = TNumericLimits<double>::Max();
			bool bClearOfNeighbourEverywhere = true;

			for (int32 Step = 0; Step <= 200; ++Step)
			{
				const double Alpha = Step / 200.0;
				const FDynamicMesh3 Swept = PosedMesh(Part, Alpha);

				double MaxY = -TNumericLimits<double>::Max();
				double MinY = TNumericLimits<double>::Max();
				double MinX = TNumericLimits<double>::Max();
				double MaxX = -TNumericLimits<double>::Max();

				for (const int32 Vid : Swept.VertexIndicesItr())
				{
					const FVector3d P = Swept.GetVertex(Vid);
					MaxY = FMath::Max(MaxY, P.Y);
					MinY = FMath::Min(MinY, P.Y);
					MinX = FMath::Min(MinX, P.X);
					MaxX = FMath::Max(MaxX, P.X);
				}

				// Carcass: the whole leaf stays in front of the carcass front plane.
				WorstCarcassClearance = FMath::Min(WorstCarcassClearance, -MaxY);

				// Neighbour: separated either by the hinge plane or by its own front face.
				const bool bClearInX = bLeft ? (MinX >= HingeX - 1e-9) : (MaxX <= HingeX + 1e-9);
				const bool bClearInY = MaxY <= FrontY + 1e-9;
				bClearOfNeighbourEverywhere &= (bClearInX || bClearInY);
			}

			TestTrue(TEXT("The leaf never touches its carcass at any open amount"),
				WorstCarcassClearance > 0.0);
			TestNearlyEqual(TEXT("The leaf keeps its hinge clearance off the carcass throughout"),
				WorstCarcassClearance, Params.BackClearance, 1e-9);
			TestTrue(TEXT("The leaf never touches its neighbour at any open amount"),
				bClearOfNeighbourEverywhere);
		}
	}

	// And the pivot is a hinge line, not a bounding-box centre: at 90 degrees a leaf standing square
	// to its carcass has its hinge edge still in the module and its leading edge a full leaf-width
	// out in front. Bounds alone would be satisfied by a leaf rotated about the wrong point.
	FHFShutterParams Square = MakeShutterParams();
	Square.OpenAngleDegrees = 90.0;

	const FHFMeshPart SquarePart = FHFJoineryKit::BuildShutterPart(Square, TEXT("Shutter"));
	const FAxisAlignedBox3d Standing = PosedBounds(SquarePart, 1.0);

	TestNearlyEqual(TEXT("Square open, the leaf is edge-on to the carcass"),
		SpanX(Standing), Square.Thickness, 1e-6);
	TestNearlyEqual(TEXT("Square open, the leaf reaches a full width out"),
		-Standing.Min.Y, Square.LeafWidth() + Square.BackClearance + Square.Thickness, 1e-6);
	TestNearlyEqual(TEXT("Square open, the leaf still hangs from its own hinge"),
		Standing.Min.X, Square.RevealGap * 0.5, 1e-6);
	TestNearlyEqual(TEXT("Square open, the leaf does not change height"),
		SpanZ(Standing), Square.LeafHeight(), 1e-9);

	return true;
}

/**
 * A top-hung flap: a loft shutter, or the lift-up door of a kitchen wall cabinet.
 *
 * The property that matters is the same one the ventilator sash turns on, and it is easy to get
 * backwards: a flap hinged along its head must swing OUT of the unit and up. Hung the other way it
 * turns straight into the carcass behind it, which is invisible in elevation - the closed pose is
 * identical - and obvious the moment anything opens it.
 *
 * Measured on where the bottom edge actually goes, and on the leaf never crossing the plane of the
 * carcass front at any angle, rather than on the sign of an angle read back off the motion.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFTopHungShutterTest, "HouseForge.Joinery.ShutterTopHungFlap", HF_TEST_FLAGS)

bool FHFTopHungShutterTest::RunTest(const FString& Parameters)
{
	// A loft flap over a wardrobe: 900 wide, 600 high, on a lift-up stay opening 100 degrees.
	FHFShutterParams Flap = MakeShutterParams();
	Flap.MotionKind = EHFShutterMotion::TopHung;
	Flap.ModuleWidth = 90.0;
	Flap.ModuleHeight = 60.0;

	const FHFMeshPart Part = FHFJoineryKit::BuildShutterPart(Flap, TEXT("Flap"));

	TestTrue(TEXT("A flap is a solid leaf"), FHFMeshOps::IsClosed(Part.Mesh));
	TestTrue(TEXT("A flap is cut to its module like any other leaf"),
		FMath::IsNearlyEqual(SpanX(Part.Mesh.GetBounds()), Flap.LeafWidth(), 1e-9)
			&& FMath::IsNearlyEqual(SpanZ(Part.Mesh.GetBounds()), Flap.LeafHeight(), 1e-9));

	// ------------------------------------------------------------------- it turns the right way
	//
	// About the horizontal axis running ALONG the run. A vertical axis here would be a side-hung
	// leaf wearing a flap's name, and an axis across the unit is geometry no flap has.
	TestTrue(TEXT("A flap hinges"), Part.Motion.Type == EHFMotionType::Hinge);
	TestTrue(TEXT("It pivots about the horizontal axis along its head"),
		Part.Motion.UnitAxis().Equals(FVector::XAxisVector, 1e-9));
	TestNearlyEqual(TEXT("It opens its declared angle"),
		FMath::Abs(Part.Motion.MaxAngleDegrees), Flap.OpenAngleDegrees, 1e-9);

	// The leaf hangs BELOW its hinge in its own space, which is what makes the head the pivot.
	const FBox Panel = FHFJoineryKit::ShutterPanelBox(Flap);
	TestNearlyEqual(TEXT("The leaf hangs below its own origin"), Panel.Max.Z, 0.0, 1e-9);
	TestNearlyEqual(TEXT("...by its whole height"), Panel.Min.Z, -Flap.LeafHeight(), 1e-9);
	TestTrue(TEXT("A flap's leading edge is its bottom one, where the handle and the stay go"),
		FHFJoineryKit::ShutterLeadingEdge(Flap) == EHFHandleEdge::Bottom);

	// The hinge is at the head of the module, half a reveal down, and on the leaf's front face.
	const FVector Axis = Part.PivotTransform.GetLocation();
	TestNearlyEqual(TEXT("The hinge runs along the head of the module"),
		Axis.Z, Flap.ModuleHeight - Flap.RevealGap * 0.5, 1e-9);
	TestNearlyEqual(TEXT("...on the leaf's front face, clear of the carcass"),
		Axis.Y, -(Flap.BackClearance + Flap.Thickness), 1e-9);
	TestTrue(TEXT("A flap hangs on a translation, not a turn"),
		Part.PivotTransform.GetRotation().IsIdentity(1e-9));

	// Closed, it fills its module exactly like a side-hung leaf does. A run of loft flaps over a
	// run of wardrobes has to line up with them.
	const FAxisAlignedBox3d Closed = PosedBounds(Part, 0.0);
	const double HalfReveal = Flap.RevealGap * 0.5;
	TestNearlyEqual(TEXT("Closed, the flap fills its module across"), Closed.Min.X, HalfReveal, 1e-9);
	TestNearlyEqual(TEXT("Closed, the flap fills its module across"),
		Closed.Max.X, Flap.ModuleWidth - HalfReveal, 1e-9);
	TestNearlyEqual(TEXT("Closed, the flap fills its module up"), Closed.Min.Z, HalfReveal, 1e-9);
	TestNearlyEqual(TEXT("Closed, the flap fills its module up"),
		Closed.Max.Z, Flap.ModuleHeight - HalfReveal, 1e-9);

	// ------------------------------------------------------------ out and up, never in and down
	//
	// A point on the bottom edge of the leaf, tracked through the swing. At 100 degrees it has come
	// out in front of the unit and risen just past its own hinge - which is what a lift-up flap
	// does, and the opposite of what one hung on the wrong face does.
	FHFPartState State;
	State.PivotTransform = Part.PivotTransform;
	State.Motion = Part.Motion;

	const FVector BottomEdgeLocal(Panel.Max.X * 0.5, 0.0, Panel.Min.Z);
	const FVector Shut = State.PoseAt(0.0).TransformPosition(BottomEdgeLocal);
	const FVector Open = State.PoseAt(1.0).TransformPosition(BottomEdgeLocal);

	TestTrue(TEXT("Opening carries the bottom edge out of the unit"), Open.Y < Shut.Y - 1.0);
	TestTrue(TEXT("Opening lifts the bottom edge"), Open.Z > Shut.Z + 1.0);
	TestNearlyEqual(TEXT("It does not travel along the run at all"), Open.X, Shut.X, 1e-9);

	// Past square, so the leaf has lifted above the hinge line rather than stopping level with it.
	TestTrue(TEXT("At 100 degrees the leaf has come up past its own hinge"), Open.Z > Axis.Z);

	// A hinge is a rotation: the bottom edge keeps its distance from the hinge line the whole way.
	for (int32 Step = 0; Step <= 40; ++Step)
	{
		const FVector At = State.PoseAt(Step / 40.0).TransformPosition(BottomEdgeLocal);
		TestNearlyEqual(TEXT("The flap stays a leaf-height from its hinge"),
			FVector2D(At.Y - Axis.Y, At.Z - Axis.Z).Size(), Flap.LeafHeight(), 1e-6);
	}

	// And the whole leaf stays in front of the carcass at every angle. The module frame puts the
	// carcass front plane at Y = 0 with everything the carcass owns behind it, so a leaf that never
	// reaches Y = 0 cannot touch it whatever is in there - the plane is the certificate.
	double WorstClearance = TNumericLimits<double>::Max();
	for (int32 Step = 0; Step <= 200; ++Step)
	{
		const FDynamicMesh3 Swept = PosedMesh(Part, Step / 200.0);

		double MaxY = -TNumericLimits<double>::Max();
		for (const int32 Vid : Swept.VertexIndicesItr())
		{
			MaxY = FMath::Max(MaxY, Swept.GetVertex(Vid).Y);
		}
		WorstClearance = FMath::Min(WorstClearance, -MaxY);
	}

	TestTrue(TEXT("The flap never reaches the carcass at any angle"), WorstClearance > 0.0);
	TestNearlyEqual(TEXT("It keeps its hinge clearance throughout"),
		WorstClearance, Flap.BackClearance, 1e-9);

	// The other kinds are unaffected: changing the motion kind must not have moved a side-hung leaf.
	{
		const FHFShutterParams Side = MakeShutterParams();
		TestTrue(TEXT("A side-hung leaf still hinges about a vertical axis"),
			FHFJoineryKit::ShutterMotion(Side).UnitAxis().Equals(FVector::ZAxisVector, 1e-9));
	}

	return true;
}

/**
 * A sliding wardrobe: two leaves on two tracks, lapping rather than leaving a reveal.
 *
 * The commonest wardrobe in a modern Indian flat, and the one the kit could not build. It is not a
 * hinged run with a different motion bolted on - the set-out is genuinely different:
 *
 *   - the leaves OVERLAP at the meeting line instead of being separated by a reveal, because they
 *     pass one another on separate tracks. A reveal there would be a hole into the wardrobe.
 *   - each leaf is therefore WIDER than its module, not narrower.
 *   - and each runs until it comes to rest exactly over its neighbour, which is what keeps every
 *     leaf inside the carcass at every open amount.
 *
 * The rule comes from FHFSlidingSetOut, which is the same one the sliding doors use.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFSlidingShutterTest, "HouseForge.Joinery.ShutterSliding", HF_TEST_FLAGS)

bool FHFSlidingShutterTest::RunTest(const FString& Parameters)
{
	// A 1800 two-door sliding wardrobe: two 900 bays, 2100 high.
	constexpr double Bay = 90.0;

	auto MakeLeaf = [](EHFShutterHinge Hand, int32 Track)
	{
		FHFShutterParams Params;
		Params.MotionKind = EHFShutterMotion::Sliding;
		Params.ModuleWidth = Bay;
		Params.ModuleHeight = 210.0;
		Params.Thickness = 1.9;
		Params.RevealGap = 0.3;
		Params.BackClearance = 0.1;
		Params.SlideOverlap = 1.25;
		Params.TrackGap = 1.0;
		Params.Hinge = Hand;
		Params.Track = Track;
		return Params;
	};

	// The left leaf runs on the track against the carcass, the right one in front of it.
	const FHFShutterParams LeftParams = MakeLeaf(EHFShutterHinge::Left, 0);
	const FHFShutterParams RightParams = MakeLeaf(EHFShutterHinge::Right, 1);

	const FHFMeshPart Left = FHFJoineryKit::BuildShutterPart(LeftParams, TEXT("SlideL"));
	const FHFMeshPart Right = FHFJoineryKit::BuildShutterPart(RightParams, TEXT("SlideR"));

	TestTrue(TEXT("Both leaves are solids"),
		FHFMeshOps::IsClosed(Left.Mesh) && FHFMeshOps::IsClosed(Right.Mesh));

	// ------------------------------------------------------------------------------ it slides
	TestTrue(TEXT("A sliding shutter slides rather than swinging"),
		Left.Motion.Type == EHFMotionType::Slide);
	TestTrue(TEXT("It runs along the carcass, not through it"),
		Left.Motion.UnitAxis().Equals(FVector::XAxisVector, 1e-9));
	TestTrue(TEXT("The two leaves run opposite ways"),
		Left.Motion.MaxTravelCm * Right.Motion.MaxTravelCm < 0.0);
	TestNearlyEqual(TEXT("Each runs a bay less its lap and its clearance"),
		FMath::Abs(Left.Motion.MaxTravelCm), Bay - 1.25 - 0.3, 1e-9);

	// ------------------------------------------------------------- wider than its module, lapping
	//
	// The difference from a hinged run, as a measurement. A hinged leaf is its module LESS a reveal;
	// a sliding one is its module PLUS its lap, less the running clearance at the jamb.
	FHFShutterParams AsHinged = LeftParams;
	AsHinged.MotionKind = EHFShutterMotion::SideHung;

	TestTrue(TEXT("A sliding leaf is wider than its module, where a hinged one is narrower"),
		LeftParams.LeafWidth() > Bay && AsHinged.LeafWidth() < Bay);
	TestNearlyEqual(TEXT("It is a bay plus its lap, less the running clearance"),
		LeftParams.LeafWidth(), Bay + 1.25 - 0.3, 1e-9);
	TestNearlyEqual(TEXT("The cut leaf is that width"),
		SpanX(Left.Mesh.GetBounds()), LeftParams.LeafWidth(), 1e-9);

	// Closed, the pair covers the whole run and LAPS in the middle. No daylight, no reveal: a gap
	// between two sliding leaves is a hole straight into the wardrobe.
	const FAxisAlignedBox3d LeftShut = PosedBounds(Left, 0.0);
	const FAxisAlignedBox3d RightShutInOwnModule = PosedBounds(Right, 0.0);

	// The right leaf's module starts one bay along, so its pose is offset by that to read the pair
	// in one frame - which is what a carcass does when it places the second module.
	const double ModuleOffset = Bay;
	const double RightMinX = RightShutInOwnModule.Min.X + ModuleOffset;
	const double RightMaxX = RightShutInOwnModule.Max.X + ModuleOffset;

	TestTrue(TEXT("Closed, the leaves lap at the meeting line rather than leaving a reveal"),
		LeftShut.Max.X > RightMinX + 1e-9);
	TestNearlyEqual(TEXT("They lap by the declared overlap from each side"),
		LeftShut.Max.X - RightMinX, 2.0 * 1.25, 1e-9);
	TestNearlyEqual(TEXT("Closed, the pair reaches the left jamb"), LeftShut.Min.X, 0.3, 1e-9);
	TestNearlyEqual(TEXT("Closed, the pair reaches the right jamb"), RightMaxX, 2.0 * Bay - 0.3, 1e-9);

	// ------------------------------------------------------------------ two tracks, never one
	//
	// They overlap in elevation, so if they shared a track they would share a volume. The track
	// offset is what makes the lap possible at all.
	TestTrue(TEXT("The leaves are on separate tracks"),
		LeftShut.Min.Y >= RightShutInOwnModule.Max.Y - 1e-9
			|| LeftShut.Max.Y <= RightShutInOwnModule.Min.Y + 1e-9);
	TestNearlyEqual(TEXT("The front track stands a leaf and a running clearance proud of the back one"),
		LeftShut.Min.Y - RightShutInOwnModule.Min.Y, LeftParams.Thickness + LeftParams.TrackGap, 1e-9);
	TestTrue(TEXT("Both leaves hang in front of the carcass"),
		LeftShut.Max.Y < 0.0 && RightShutInOwnModule.Max.Y < 0.0);

	// ------------------------------------------------------ inside the carcass at every amount
	//
	// The property the single full-width leaf broke on the balcony doors: a leaf that runs its own
	// width leaves the unit. Every leaf here stays within the run of the carcass throughout.
	constexpr double RunMin = 0.0;
	constexpr double RunMax = 2.0 * Bay;

	for (int32 Step = 0; Step <= 40; ++Step)
	{
		const double Alpha = Step / 40.0;

		const FAxisAlignedBox3d L = PosedBounds(Left, Alpha);
		const FAxisAlignedBox3d R = PosedBounds(Right, Alpha);
		const double RMin = R.Min.X + ModuleOffset;
		const double RMax = R.Max.X + ModuleOffset;

		if (L.Min.X < RunMin - 1e-9 || L.Max.X > RunMax + 1e-9 || RMin < RunMin - 1e-9 || RMax > RunMax + 1e-9)
		{
			AddError(FString::Printf(
				TEXT("At %.2f open the leaves span %.2f..%.2f and %.2f..%.2f, outside the 0..%.0f carcass; a shutter is running out of the wardrobe."),
				Alpha, L.Min.X, L.Max.X, RMin, RMax, RunMax));
			break;
		}

		// A slide is a translation: nothing changes size, height or depth on the way.
		TestNearlyEqual(TEXT("A sliding leaf keeps its height"), L.Max.Z - L.Min.Z,
			LeftShut.Max.Z - LeftShut.Min.Z, 1e-9);
		TestNearlyEqual(TEXT("A sliding leaf stays on its own track"), L.Min.Y, LeftShut.Min.Y, 1e-9);
	}

	// Fully open, each leaf has come to rest exactly over its neighbour's closed position - as far
	// as it can go without leaving the carcass, and the whole point of the two-track set-out.
	const FAxisAlignedBox3d LeftOpen = PosedBounds(Left, 1.0);
	TestNearlyEqual(TEXT("Open, the left leaf stacks on where the right one closed"),
		LeftOpen.Max.X, RightMaxX, 1e-9);
	TestTrue(TEXT("Open, its own bay is clear to walk into"),
		LeftOpen.Min.X >= Bay - 1.25 - 1e-9);

	// It travelled, and it travelled exactly its declared distance.
	TestNearlyEqual(TEXT("It travels its declared distance"),
		LeftOpen.Min.X - LeftShut.Min.X, Left.Motion.MaxTravelCm, 1e-9);

	// A sliding leaf has no swing, so nothing about it should be read off the open angle. Changing
	// that angle must not move it at all.
	{
		FHFShutterParams Angled = LeftParams;
		Angled.OpenAngleDegrees = 45.0;
		TestNearlyEqual(TEXT("The open angle means nothing to a slider"),
			FHFJoineryKit::ShutterMotion(Angled).MaxTravelCm, Left.Motion.MaxTravelCm, 1e-9);
	}

	// A leaf with nowhere to run is refused rather than generated as a panel that cannot move: a
	// lap as wide as the bay leaves no travel at all.
	{
		FHFShutterParams Stuck = LeftParams;
		Stuck.SlideOverlap = Bay;
		TestFalse(TEXT("A sliding leaf with no travel is not a sliding leaf"), Stuck.IsValid());
		TestEqual(TEXT("...and produces no geometry"),
			FHFJoineryKit::GenerateShutter(Stuck).TriangleCount(), 0);
	}

	// The handle goes on the edge it runs towards, which is the same edge a hinged leaf of that
	// hand opens from - so a run of sliders and a run of hinged leaves are described the same way.
	TestTrue(TEXT("A left-set slider leads with its right edge"),
		FHFJoineryKit::ShutterLeadingEdge(LeftParams) == EHFHandleEdge::MaxX);
	TestTrue(TEXT("A right-set slider leads with its left edge"),
		FHFJoineryKit::ShutterLeadingEdge(RightParams) == EHFHandleEdge::MinX);

	// And a slider still presents its outward face at local Y = 0, like every other leaf in the
	// kit, so anything mounted on one is described exactly as it is on a hinged leaf.
	TestNearlyEqual(TEXT("A sliding leaf looks out of the wardrobe along its own -Y"),
		Left.Mesh.GetBounds().Min.Y, 0.0, 1e-9);
	TestNearlyEqual(TEXT("...with its board behind that face"),
		Left.Mesh.GetBounds().Max.Y, LeftParams.Thickness, 1e-9);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
