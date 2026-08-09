// Copyright Siddartha G. All Rights Reserved.

#include "HouseForge.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "DynamicMesh/DynamicMeshAABBTree3.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Geometry/HFJoineryKit.h"
#include "Geometry/HFMeshOps.h"
#include "MeshQueries.h"
#include "Misc/AutomationTest.h"
#include "Model/HFArticulation.h"
#include "Model/HFTypes.h"
#include "Spatial/FastWinding.h"

using namespace UE::Geometry;

#define HF_TEST_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Can a hand actually use this handle?
 *
 * Every assertion in HouseForge.Joinery.HandleRecess is about what a recess REMOVES, and all of them
 * passed on geometry the fixtures milestone summed up as "every recessed style reads as the reveal
 * gap". They were not wrong; they were measuring the wrong thing. A cut of the right depth in the
 * right face is not a handle, and neither is a solid of the right length standing off a shutter. A
 * handle is a VOID a hand goes into and a LIP it pulls on, and until this file nothing in the suite
 * measured either.
 *
 * So everything here is stated in centimetres of air. The tests place probe points in the space a
 * finger occupies and require it to be empty, then place one in the material that has to overhang
 * that space and require it to be solid. "Some geometry appeared" cannot satisfy them, and neither
 * can a bar screwed flat to a door.
 */
namespace
{
	/** A 450 x 19 x 2100 shutter-sized panel, sitting on the origin. */
	FBox ShutterPanelBox()
	{
		return FBox(FVector(0.0, 0.0, 0.0), FVector(45.0, 1.9, 210.0));
	}

	/**
	 * A handle on the TOP edge of that panel, looking out along +Y.
	 *
	 * The one configuration the probe arithmetic below is written for, chosen because its frame comes
	 * out axis-aligned and legible: see SectionPoint. Handedness and facing are covered where they
	 * belong, in HouseForge.Joinery.HandleOnEitherHand.
	 */
	FHFHandleParams MakeTopEdgeHandle(EHFHandleStyle Style)
	{
		FHFHandleParams Params;
		Params.Style = Style;
		Params.PanelBox = ShutterPanelBox();
		Params.Facing = EHFPanelFacing::PositiveY;
		Params.Edge = EHFHandleEdge::Top;
		return Params;
	}

	/** A solid board filling a box, as the thing a handle is fitted to. */
	FDynamicMesh3 MakePanelMesh(const FBox& Box)
	{
		FDynamicMesh3 Mesh;
		FHFMeshOps::InitialiseMesh(Mesh);
		FHFMeshOps::AppendBox(Mesh, FVector3d(Box.GetCenter()), FVector3d(Box.GetExtent()), 0.0,
			EHFSurfaceRole::ShutterLaminate);
		FHFMeshOps::ApplyWorldScaleUVs(Mesh);
		return Mesh;
	}

	/**
	 * A point of a routed handle's cross-section, in the panel's own space.
	 *
	 * The kit authors both the cutter and the fitted section in (U out along the edge served, V out
	 * of the panel face), negative inside the board. For a handle on the TOP edge of a panel looking
	 * out along +Y, that frame lands axis-aligned: U runs up +Z from the top edge and V runs out +Y
	 * from the face, with the sweep along -X. Written out here so every probe below reads as the
	 * section drawing it came from rather than as three coordinates.
	 */
	FVector3d SectionPoint(const FBox& Panel, double U, double V)
	{
		return FVector3d(Panel.GetCenter().X, Panel.Max.Y + V, Panel.Max.Z + U);
	}

	/** Solid-or-not at a point, which is the only question any of this asks. */
	struct FSolidProbe
	{
		explicit FSolidProbe(const FDynamicMesh3& Mesh)
			: Tree(&Mesh, true)
			, Winding(&Tree, true)
		{
		}

		bool IsSolidAt(const FVector3d& Point) const { return Winding.IsInside(Point); }

		FDynamicMeshAABBTree3 Tree;
		TFastWindingTree<FDynamicMesh3> Winding;
	};

	double Volume(const FDynamicMesh3& Mesh)
	{
		return TMeshQueries<FDynamicMesh3>::GetVolumeArea(Mesh).X;
	}

	/** Bounds of just the triangles carrying one surface role. */
	FAxisAlignedBox3d BoundsOfRole(const FDynamicMesh3& Mesh, EHFSurfaceRole Role)
	{
		const int32 Group = FHFMeshOps::GroupForRole(Role);
		FAxisAlignedBox3d Box = FAxisAlignedBox3d::Empty();
		for (const int32 Tid : Mesh.TriangleIndicesItr())
		{
			if (Mesh.GetTriangleGroup(Tid) != Group)
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
}

/**
 * The two routed styles, measured as the void they leave and the lip that overhangs it.
 *
 * THE TEST THIS WHOLE CHANGE EXISTS FOR. A J-profile and a gola are not cuts - they are aluminium
 * sections, and the section is what a hand touches. Routed alone into a 19 mm shutter keeping a 5 mm
 * web, the deepest channel possible is 13.5 mm, open on every side and with nothing to pull on: a
 * wide shadow at the edge of a leaf, which is what the 3 mm reveal beside it already is.
 *
 * Three things are asserted, in this order, because each is worthless without the one before it:
 *
 *   1. a box of stated size, in centimetres, is empty - a finger fits;
 *   2. material overhangs that box - there is a lip, so the finger has something to pull;
 *   3. the void underneath the lip is still void - the lip is a RETURN and not a filled corner.
 *
 * A generator that merely produced "some geometry near the edge" fails 1. One that filled the
 * channel with a solid moulding fails 1 and 3. One that put a flat strip on the face passes 1 and
 * fails 2. The old routed-only shape passes 1 and fails 2 - which is exactly the defect.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFHandleGripTest, "HouseForge.Joinery.HandleGrip", HF_TEST_FLAGS)

bool FHFHandleGripTest::RunTest(const FString& Parameters)
{
	const FBox Panel = ShutterPanelBox();

	// The reveal between two shutters, which is the thing a recessed handle was being mistaken for.
	const double RevealGap = FHFShutterParams().RevealGap;
	TestTrue(TEXT("A shutter reveal is the 3 mm shadow line it has always been"),
		FMath::IsNearlyEqual(RevealGap, 0.3, 1e-9));

	const EHFHandleStyle Routed[] = { EHFHandleStyle::JProfile, EHFHandleStyle::HandlelessGroove };

	for (const EHFHandleStyle Style : Routed)
	{
		const bool bJ = Style == EHFHandleStyle::JProfile;
		const TCHAR* Which = bJ ? TEXT("J-profile") : TEXT("gola");

		const FHFHandleParams Asked = MakeTopEdgeHandle(Style);
		const FHFHandleParams P = FHFJoineryKit::SanitiseHandle(Asked);

		if (!TestTrue(*FString::Printf(TEXT("A %s is fitted with a section, not merely routed"), Which),
			P.HasReturnProfile()))
		{
			return false;
		}

		// ------------------------------------------------------------ what it says it leaves a hand

		const double Aperture = P.FingerApertureCm();
		const double Depth = P.FingerDepthCm();
		const double Overhang = P.ReturnOverhangCm();

		TestTrue(*FString::Printf(
			TEXT("A %s declares an aperture a finger fits through: %.2f cm, floor %.2f"),
			Which, Aperture, FHFJoineryKit::MinFingerAperture),
			Aperture >= FHFJoineryKit::MinFingerAperture);
		TestTrue(*FString::Printf(
			TEXT("A %s declares a channel a finger reaches into: %.2f cm, floor %.2f"),
			Which, Depth, FHFJoineryKit::MinFingerDepth),
			Depth >= FHFJoineryKit::MinFingerDepth);
		TestTrue(*FString::Printf(TEXT("A %s declares a return to pull on: %.2f cm"), Which, Overhang),
			Overhang > 0.0);

		// THE DIAGNOSIS, AS A NUMBER. "Reads as the reveal gap" is a claim about proportion, so it is
		// refuted with one: the channel a hand goes into is several times the shadow line beside it.
		// A cut that merely matched the reveal would satisfy every dimensional assertion in
		// HouseForge.Joinery.HandleRecess and fail here.
		TestTrue(*FString::Printf(
			TEXT("A %s cannot be mistaken for the %.1f cm reveal beside it: %.2f cm across, %.1f times it"),
			Which, RevealGap, Aperture, Aperture / RevealGap),
			Aperture >= 6.0 * RevealGap);

		// And the depth is out of reach of routing alone, which is why the section exists at all. A
		// 19 mm board keeping its web cannot give this up.
		TestTrue(*FString::Printf(
			TEXT("A %s is deeper than the board could ever be routed: %.2f cm against %.2f of board"),
			Which, Depth, Panel.GetSize().Y - P.MinWeb),
			Depth > Panel.GetSize().Y - P.MinWeb);

		// ------------------------------------------------------------------ and what it really left

		FDynamicMesh3 Fitted = MakePanelMesh(Panel);
		if (!TestTrue(*FString::Printf(TEXT("A %s applies to a panel"), Which),
			FHFJoineryKit::ApplyHandle(Fitted, Asked)))
		{
			return false;
		}

		TestTrue(*FString::Printf(TEXT("A panel with a %s on it is watertight"), Which),
			FHFMeshOps::IsClosed(Fitted));
		TestTrue(*FString::Printf(TEXT("A panel with a %s on it faces outward"), Which),
			Volume(Fitted) > 0.0);

		const FSolidProbe Probe(Fitted);

		// Where the void is, in the section frame. The channel runs from its far wall to the inner
		// end of the return across U, and from the floor lining up to the return's underside across
		// V - which is Aperture by Depth, the two figures declared above.
		const double MouthU = bJ
			? -P.ReturnLip
			: -P.GrooveEdgeMargin + FHFJoineryKit::ProfileBed - P.ReturnLip;
		const double FarU = MouthU - Aperture;
		const double FloorV = P.ProfileProjection - P.ProfileStock - Depth;
		const double UnderReturnV = P.ProfileProjection - P.ProfileStock;

		// 1. THE FINGER FITS. Sampled over the whole declared box rather than at its centre: a
		//    section fitted the wrong way round, or a moulding filling the channel, leaves a centre
		//    point that is still air.
		int32 Solid = 0;
		FVector3d FirstSolid = FVector3d::Zero();
		constexpr int32 Steps = 6;

		for (int32 Ui = 0; Ui <= Steps; ++Ui)
		{
			for (int32 Vi = 0; Vi <= Steps; ++Vi)
			{
				// Inset a tenth of the box, so a probe on the boundary is not deciding the test.
				const double Alpha = 0.1 + 0.8 * (Ui / static_cast<double>(Steps));
				const double Beta = 0.1 + 0.8 * (Vi / static_cast<double>(Steps));
				const FVector3d At = SectionPoint(Panel,
					FMath::Lerp(FarU, MouthU, Alpha), FMath::Lerp(FloorV, UnderReturnV, Beta));

				if (Probe.IsSolidAt(At))
				{
					if (Solid == 0)
					{
						FirstSolid = At;
					}
					++Solid;
				}
			}
		}

		TestEqual(*FString::Printf(
			TEXT("The %.2f x %.2f cm void a finger goes into is empty, all of it (%s)"),
			Aperture, Depth, Which), Solid, 0);
		if (Solid > 0)
		{
			AddError(FString::Printf(
				TEXT("%s: %d of %d probes inside the declared finger void hit material, first at %s."),
				Which, Solid, (Steps + 1) * (Steps + 1), *FVector(FirstSolid).ToString()));
		}

		// 2. THERE IS A LIP OVER IT. Halfway along the return's own overhang, in the middle of its
		//    stock: material, or there is nothing to hook a fingertip under and the channel is a slot.
		const FVector3d InTheReturn = SectionPoint(Panel,
			MouthU + Overhang * 0.5, P.ProfileProjection - P.ProfileStock * 0.5);
		TestTrue(*FString::Printf(TEXT("A %s has a return standing over its channel"), Which),
			Probe.IsSolidAt(InTheReturn));

		// 3. AND THE SPACE UNDER THE LIP IS STILL SPACE. Directly beneath the point just proved
		//    solid, a fingertip's worth down. A "return" with the channel filled in behind it is a
		//    chamfer, and it would pass assertion 2 on its own.
		const FVector3d UnderTheReturn = SectionPoint(Panel,
			MouthU + Overhang * 0.5, UnderReturnV - FMath::Min(Depth * 0.5, 0.8));
		TestFalse(*FString::Printf(TEXT("A %s leaves the space under its return open"), Which),
			Probe.IsSolidAt(UnderTheReturn));

		// The floor is real too, so the channel is a channel and not a hole through the leaf.
		const FVector3d InTheFloor = SectionPoint(Panel,
			(FarU + MouthU) * 0.5, FloorV - P.ProfileStock * 0.5);
		TestTrue(*FString::Printf(TEXT("A %s has a floor under its channel"), Which),
			Probe.IsSolidAt(InTheFloor));

		// The web survives: whatever else happened, the panel was not routed in two.
		const FVector3d InTheWeb = SectionPoint(Panel,
			(FarU + MouthU) * 0.5, -Panel.GetSize().Y + P.MinWeb * 0.5);
		TestTrue(*FString::Printf(TEXT("A %s leaves the board's web behind it"), Which),
			Probe.IsSolidAt(InTheWeb));

		// ------------------------------------------------------------------ the section is a part

		const FDynamicMesh3 Profile = FHFJoineryKit::GenerateHandleProfile(Asked);
		if (!TestTrue(*FString::Printf(TEXT("A %s produces a section on its own"), Which),
			Profile.TriangleCount() > 0))
		{
			return false;
		}

		TestTrue(*FString::Printf(TEXT("A %s section is watertight"), Which),
			FHFMeshOps::IsClosed(Profile));
		TestTrue(*FString::Printf(TEXT("A %s section faces outward"), Which), Volume(Profile) > 0.0);

		// Continuous along the run, and stopping dead at both ends of the panel. A section overshot
		// like its own cutter would hang in mid-air past the end of the leaf it is fitted to.
		const FAxisAlignedBox3d Section = Profile.GetBounds();
		TestNearlyEqual(*FString::Printf(TEXT("A %s section runs the whole panel"), Which),
			Section.Width(), Panel.GetSize().X, 0.001);
		TestNearlyEqual(*FString::Printf(TEXT("A %s section stops dead at the far end of it"), Which),
			Section.Min.X, Panel.Min.X, 0.001);
		TestNearlyEqual(*FString::Printf(TEXT("A %s section stops dead at the near end of it"), Which),
			Section.Max.X, Panel.Max.X, 0.001);

		// It stands exactly the declared projection proud, which is the shadow the run reads by.
		TestNearlyEqual(*FString::Printf(TEXT("A %s section stands its declared projection proud"), Which),
			Section.Max.Y - Panel.Max.Y, P.ProfileProjection, 0.001);
		TestTrue(*FString::Printf(TEXT("A %s section is mostly air within its own bounds"), Which),
			Volume(Profile) < Section.Width() * Section.Depth() * Section.Height() * 0.5);
	}

	// ------------------------------------------------------------------------------ the applied pair
	//
	// A bar and a knob had the opposite failure available to them: plenty of solid, no room behind
	// it. The floor used to be BarDiameter * 1.5, which on the standard 12 mm stock allowed a pull
	// standing 18 mm off the face with 6 mm behind it - correct in every dimension a caller could
	// read back, and impossible to close a hand round.

	{
		FHFHandleParams Flat = MakeTopEdgeHandle(EHFHandleStyle::Bar);
		Flat.Projection = Flat.BarDiameter;

		const FHFHandleParams Fixed = FHFJoineryKit::SanitiseHandle(Flat);
		TestTrue(*FString::Printf(
			TEXT("A bar asked to sit flat on the shutter is pushed out to a grip: %.2f cm behind it"),
			Fixed.GripClearanceCm()),
			Fixed.GripClearanceCm() >= FHFJoineryKit::MinGripClearance);

		const FHFHandleParams Bar = FHFJoineryKit::SanitiseHandle(MakeTopEdgeHandle(EHFHandleStyle::Bar));
		TestTrue(*FString::Printf(TEXT("A standard bar already clears it: %.2f cm"), Bar.GripClearanceCm()),
			Bar.GripClearanceCm() >= FHFJoineryKit::MinGripClearance);

		// And the air is really there. Handle-local space: +X along the run, +Y out of the face,
		// +Z towards the edge. Probed at mid-run, between the two standoffs.
		const FDynamicMesh3 Mesh = FHFJoineryKit::GenerateHandle(MakeTopEdgeHandle(EHFHandleStyle::Bar));
		const FSolidProbe Probe(Mesh);

		int32 Blocked = 0;
		for (int32 Step = 1; Step < 8; ++Step)
		{
			const double Y = Bar.GripClearanceCm() * (Step / 8.0);
			if (Probe.IsSolidAt(FVector3d(0.0, Y, 0.0)))
			{
				++Blocked;
			}
		}
		TestEqual(*FString::Printf(TEXT("The %.2f cm behind a bar is air for all of it"),
			Bar.GripClearanceCm()), Blocked, 0);

		TestTrue(TEXT("And the bar itself is where the fingers stop"),
			Probe.IsSolidAt(FVector3d(0.0, Bar.Projection - Bar.BarDiameter * 0.5, 0.0)));
	}

	{
		const FHFHandleParams Knob = FHFJoineryKit::SanitiseHandle(MakeTopEdgeHandle(EHFHandleStyle::Knob));

		// A knob is pinched rather than gripped, so what it needs is a neck to get behind and a
		// shoulder to pull on. Both are asserted in centimetres before either is looked for.
		TestTrue(*FString::Printf(TEXT("A knob has a neck to pinch behind: %.2f cm"),
			Knob.GripClearanceCm()), Knob.GripClearanceCm() >= FHFJoineryKit::MinKnobNeck);
		TestTrue(*FString::Printf(TEXT("A knob's head overhangs its stem: %.2f cm"),
			Knob.KnobUndercutCm()), Knob.KnobUndercutCm() >= FHFJoineryKit::MinKnobUndercut);

		const FDynamicMesh3 Mesh = FHFJoineryKit::GenerateHandle(MakeTopEdgeHandle(EHFHandleStyle::Knob));
		const FSolidProbe Probe(Mesh);

		// Halfway out into the undercut, all round the stem. Empty under the head and solid at it -
		// which is the difference between a knob and a boss, and the difference does not show in a
		// still taken from the front.
		const double Radius = (Knob.KnobStemDiameter + Knob.KnobDiameter) * 0.25;
		int32 Blocked = 0;
		int32 Missing = 0;

		for (int32 Step = 0; Step < 8; ++Step)
		{
			const double Angle = 2.0 * UE_DOUBLE_PI * (Step / 8.0);
			const FVector3d Round(Radius * FMath::Cos(Angle), 0.0, Radius * FMath::Sin(Angle));

			if (Probe.IsSolidAt(Round + FVector3d(0.0, Knob.GripClearanceCm() * 0.5, 0.0)))
			{
				++Blocked;
			}
			if (!Probe.IsSolidAt(Round + FVector3d(0.0, Knob.Projection * 0.7, 0.0)))
			{
				++Missing;
			}
		}

		TestEqual(*FString::Printf(TEXT("A knob's neck is clear all round, %.2f cm out"), Radius),
			Blocked, 0);
		TestEqual(TEXT("And its head is over that neck, all round"), Missing, 0);
	}

	return true;
}

/**
 * A handle that is a hole in a leaf still has to travel with the leaf.
 *
 * HouseForge.Joinery.HandleRidesWithPart proves it for a bar, and HandleOnEitherHand proves the
 * routed cut comes off the right face of either hand. Neither could say anything about the fitted
 * section, because until now there wasn't one - and a section is the part of a recessed handle that
 * a bake, an asset override or a material assignment can actually reach.
 *
 * Followed by the point furthest out in front of the leaf, which after this change is the face of
 * the return: the one surface a hand touches, on the one part that used not to exist.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFHandleSectionRidesWithLeafTest,
	"HouseForge.Joinery.HandleSectionRidesWithLeaf", HF_TEST_FLAGS)

bool FHFHandleSectionRidesWithLeafTest::RunTest(const FString& Parameters)
{
	FHFShutterParams Shutter;
	Shutter.ModuleWidth = 45.0;
	Shutter.ModuleHeight = 210.0;
	Shutter.Hinge = EHFShutterHinge::Left;

	FHFMeshPart Part = FHFJoineryKit::BuildShutterPart(Shutter, TEXT("Shutter"));
	if (!TestTrue(TEXT("The shutter produces a leaf"), Part.Mesh.TriangleCount() > 0))
	{
		return false;
	}

	const FAxisAlignedBox3d Bare = Part.Mesh.GetBounds();

	const FHFHandleParams Asked = FHFJoineryKit::ShutterHandle(Shutter, EHFHandleStyle::JProfile);
	const FHFHandleParams P = FHFJoineryKit::SanitiseHandle(Asked);

	if (!TestTrue(TEXT("A J-profile fits the leaf"), FHFJoineryKit::ApplyHandle(Part.Mesh, Asked)))
	{
		return false;
	}

	TestTrue(TEXT("The leaf is still watertight"), FHFMeshOps::IsClosed(Part.Mesh));

	// The section is IN the leaf's own mesh, standing its projection out in front of the leaf's
	// outward face. Left on the carcass instead, this would be exactly the bounds of the bare leaf.
	TestNearlyEqual(TEXT("The section stands proud of the leaf's outward face"),
		Bare.Min.Y - Part.Mesh.GetBounds().Min.Y, P.ProfileProjection, 0.001);

	const FAxisAlignedBox3d Metal = BoundsOfRole(Part.Mesh, P.HandleRole);
	if (!TestTrue(TEXT("The section is findable by its own surface role"), Metal.Volume() > 0.0))
	{
		return false;
	}
	TestNearlyEqual(TEXT("Its face is the furthest thing out in front of the leaf"),
		Metal.Min.Y, Part.Mesh.GetBounds().Min.Y, 0.001);

	// A vertical section on a vertical edge runs the height of the leaf, which is what "continuous
	// along the run" means on a wardrobe door.
	TestNearlyEqual(TEXT("It runs the full height of the leaf"),
		Metal.Height(), Shutter.LeafHeight(), 0.01);

	// And it swings. Followed at the return's outer face, furthest from the hinge in the direction
	// that matters, exactly as the bar is in HandleRidesWithPart.
	FVector3d Tip = FVector3d::Zero();
	double Furthest = TNumericLimits<double>::Max();
	for (const int32 Vid : Part.Mesh.VertexIndicesItr())
	{
		const FVector3d V = Part.Mesh.GetVertex(Vid);
		if (V.Y < Furthest)
		{
			Furthest = V.Y;
			Tip = V;
		}
	}

	FHFPartState State;
	State.PartId = Part.PartId;
	State.PivotTransform = Part.PivotTransform;
	State.Motion = Part.Motion;

	const FVector Closed = State.PoseAt(0.0).TransformPosition(FVector(Tip));
	const FVector Open = State.PoseAt(1.0).TransformPosition(FVector(Tip));
	const FVector Axis = Part.PivotTransform.GetTranslation();

	auto RadiusFromHinge = [&Axis](const FVector& Point)
	{
		return FVector2D(Point.X - Axis.X, Point.Y - Axis.Y).Size();
	};

	TestTrue(TEXT("Opening the leaf carries its section with it"), FVector::Dist(Closed, Open) > 10.0);
	TestNearlyEqual(TEXT("The section keeps its distance from the hinge line"),
		RadiusFromHinge(Open), RadiusFromHinge(Closed), 0.01);
	TestNearlyEqual(TEXT("And its height"), Open.Z, Closed.Z, 0.01);

	return true;
}

/**
 * The sliding wardrobe, which had a handle you could neither see nor reach.
 *
 * Milestone 9, on the master bedroom's 2400 run: "the sliding groove is routed into the lapped edge
 * so it is both invisible and unreachable". Two leaves on two tracks lap at the meeting line, and
 * the edge each leaf LEADS with is the edge that goes under - or over - its partner. Routing the
 * channel there put the back leaf's handle behind 29 mm of the front leaf for the whole of its
 * travel.
 *
 * Both halves of that are measured here, and both are about the pair rather than the leaf, which is
 * why neither could be caught by any of the single-panel tests above:
 *
 *   reachable   the handle is clear of the leaf that laps it, at every open amount;
 *   passable    the section it stands proud by fits under the running clearance of the track in
 *               front, so opening the run does not drive one leaf through the other's handle.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFSlidingHandleReachTest, "HouseForge.Joinery.SlidingHandleReach",
	HF_TEST_FLAGS)

bool FHFSlidingHandleReachTest::RunTest(const FString& Parameters)
{
	constexpr double Bay = 120.0;

	auto MakeLeaf = [](EHFShutterHinge Hand, int32 Track)
	{
		FHFShutterParams Params;
		Params.MotionKind = EHFShutterMotion::Sliding;
		Params.ModuleWidth = Bay;
		Params.ModuleHeight = 210.0;
		Params.Hinge = Hand;
		Params.Track = Track;
		return Params;
	};

	const FHFShutterParams BackParams = MakeLeaf(EHFShutterHinge::Left, 0);
	const FHFShutterParams FrontParams = MakeLeaf(EHFShutterHinge::Right, 1);

	// -------------------------------------------------------------------------- which edge, and why

	TestTrue(TEXT("A sliding leaf still LEADS with the edge it runs towards"),
		FHFJoineryKit::ShutterLeadingEdge(BackParams) == EHFHandleEdge::MaxX
			&& FHFJoineryKit::ShutterLeadingEdge(FrontParams) == EHFHandleEdge::MinX);

	// And takes its handle on the other one. This is the whole fix, in two lines: the leading edge is
	// the lapped edge, and a handle on a lapped edge is under the other leaf.
	TestTrue(TEXT("But it takes its handle on the jamb edge, which is the exposed one"),
		FHFJoineryKit::ShutterHandleEdge(BackParams) == EHFHandleEdge::MinX
			&& FHFJoineryKit::ShutterHandleEdge(FrontParams) == EHFHandleEdge::MaxX);

	// Everything that swings is unchanged: for a hinged leaf the two edges are the same edge.
	FHFShutterParams Hinged = BackParams;
	Hinged.MotionKind = EHFShutterMotion::SideHung;
	TestTrue(TEXT("A hinged leaf takes its handle on the edge it opens from, as it always did"),
		FHFJoineryKit::ShutterHandleEdge(Hinged) == FHFJoineryKit::ShutterLeadingEdge(Hinged));

	// --------------------------------------------------------------------------- it clears the track

	const FHFHandleParams BackHandle =
		FHFJoineryKit::SanitiseHandle(FHFJoineryKit::ShutterHandle(BackParams, EHFHandleStyle::HandlelessGroove));

	TestTrue(*FString::Printf(
		TEXT("A sliding leaf's section ducks under the %.1f cm running clearance: %.2f cm proud"),
		BackParams.TrackGap, BackHandle.ProfileProjection),
		BackHandle.ProfileProjection <= BackParams.TrackGap - FHFJoineryKit::MinSlidingProfileClearance
			+ 1e-9);

	// Ducking under it is only worth having if what is left is still a handle. This is the assertion
	// that stops the clamp being satisfied by taking the projection to zero.
	TestTrue(*FString::Printf(TEXT("And is still a handle afterwards: %.2f cm deep, %.2f across"),
		BackHandle.FingerDepthCm(), BackHandle.FingerApertureCm()),
		BackHandle.FingerDepthCm() >= FHFJoineryKit::MinFingerDepth
			&& BackHandle.FingerApertureCm() >= FHFJoineryKit::MinFingerAperture);

	// --------------------------------------------------------------- and nothing ever passes over it

	FHFMeshPart Back = FHFJoineryKit::BuildShutterPart(BackParams, TEXT("SlideBack"));
	FHFMeshPart Front = FHFJoineryKit::BuildShutterPart(FrontParams, TEXT("SlideFront"));

	if (!TestTrue(TEXT("Both leaves take their handles"),
		FHFJoineryKit::ApplyHandle(Back.Mesh, FHFJoineryKit::ShutterHandle(BackParams, EHFHandleStyle::HandlelessGroove))
			&& FHFJoineryKit::ApplyHandle(Front.Mesh,
				FHFJoineryKit::ShutterHandle(FrontParams, EHFHandleStyle::HandlelessGroove))))
	{
		return false;
	}

	auto PosedBounds = [](const FHFMeshPart& Part, double Amount)
	{
		FHFPartState State;
		State.PivotTransform = Part.PivotTransform;
		State.Motion = Part.Motion;
		return FBox(Part.Mesh.GetBounds()).TransformBy(State.PoseAt(Amount));
	};

	// The back leaf's frontmost point is now its section's face. The front leaf's board runs behind
	// its own face by its thickness; the two must not meet.
	const FBox BackShut = PosedBounds(Back, 0.0);
	const FBox FrontShut = PosedBounds(Front, 0.0);

	TestTrue(*FString::Printf(
		TEXT("The back leaf's section stops short of the front leaf's board: %.3f cm of air"),
		BackShut.Min.Y - FrontShut.Max.Y),
		BackShut.Min.Y > FrontShut.Max.Y + 1e-6);

	// At every open amount, because the front leaf travels over the back one for the whole of its
	// run - which is the case that made a proud section a question at all.
	for (int32 Step = 0; Step <= 20; ++Step)
	{
		const double Alpha = Step / 20.0;
		const FBox B = PosedBounds(Back, Alpha);
		const FBox F = PosedBounds(Front, Alpha);

		if (B.Min.Y <= F.Max.Y + 1e-6)
		{
			AddError(FString::Printf(
				TEXT("At %.2f open the back leaf's handle reaches Y %.3f and the front leaf's board reaches %.3f; the run grinds."),
				Alpha, B.Min.Y, F.Max.Y));
			break;
		}
	}

	// -------------------------------------------------------------- and it is on the side you can see

	// The pair in one frame: the right leaf's module starts one bay along. Closed, the two lap at
	// the meeting line and everything between the lap and each jamb is that leaf's own face. The
	// handle has to sit in the part nobody else covers.
	const FAxisAlignedBox3d BackMetal = BoundsOfRole(Back.Mesh, BackHandle.HandleRole);
	if (!TestTrue(TEXT("The back leaf has a section to find"), BackMetal.Volume() > 0.0))
	{
		return false;
	}

	const double MeetingLine = Bay;
	const double LapStart = MeetingLine - BackParams.SlideOverlap;

	TestTrue(*FString::Printf(
		TEXT("The back leaf's handle is clear of the lap: it ends at %.2f, the lap starts at %.2f"),
		BackMetal.Max.X, LapStart),
		BackMetal.Max.X < LapStart);

	// The travelling case, which is the one that was actually broken. Fully open, the back leaf has
	// run out from under nothing and the FRONT leaf has come to rest over the back leaf's closed
	// position - so a handle set out from the lapped edge would spend the whole run underneath it.
	// Set out from the jamb it does not: it travels to where the front leaf came from.
	const FBox BackOpen = PosedBounds(Back, 1.0);
	const FBox FrontOpen = PosedBounds(Front, 1.0);
	const double FrontOpenMinX = FrontOpen.Min.X + Bay;

	TestTrue(*FString::Printf(
		TEXT("Open, the back leaf's handle edge at %.2f is still clear of the front leaf at %.2f"),
		BackOpen.Min.X, FrontOpenMinX),
		BackOpen.Min.X >= FrontOpenMinX - 1e-6);

	return true;
}

#undef HF_TEST_FLAGS

#endif // WITH_DEV_AUTOMATION_TESTS
