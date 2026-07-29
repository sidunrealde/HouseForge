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
	/** A 450 x 19 x 2100 shutter-sized panel, sitting on the origin. */
	FBox ShutterPanelBox()
	{
		return FBox(FVector(0.0, 0.0, 0.0), FVector(45.0, 1.9, 210.0));
	}

	FHFHandleParams MakeHandleParams(EHFHandleStyle Style, EHFHandleEdge Edge = EHFHandleEdge::MaxX)
	{
		FHFHandleParams Params;
		Params.Style = Style;
		Params.PanelBox = ShutterPanelBox();
		Params.Facing = EHFPanelFacing::PositiveY;
		Params.Edge = Edge;
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

	double Volume(const FDynamicMesh3& Mesh)
	{
		return TMeshQueries<FDynamicMesh3>::GetVolumeArea(Mesh).X;
	}

	/** True when every triangle carries a polygroup that decodes to Role. */
	bool AllTrianglesHaveRole(const FDynamicMesh3& Mesh, EHFSurfaceRole Role)
	{
		for (const int32 Tid : Mesh.TriangleIndicesItr())
		{
			if (FHFMeshOps::RoleForGroup(Mesh.GetTriangleGroup(Tid)) != Role)
			{
				return false;
			}
		}
		return Mesh.TriangleCount() > 0;
	}

	/** No triangle left on group 0, which is the "never tagged" value GroupForRole reserves. */
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

	bool HasUVs(const FDynamicMesh3& Mesh)
	{
		return Mesh.HasAttributes() && Mesh.Attributes()->PrimaryUV() != nullptr
			&& Mesh.Attributes()->PrimaryUV()->ElementCount() > 0;
	}
}

/**
 * A bar pull, as the handle everything else is measured against.
 *
 * The dimensional assertions are on the bounds rather than on the triangle count, because the bar's
 * round sections are an approximation and a count would pass for whichever approximation happened to
 * be in the code that day. Its length, its stock and how far it stands proud are the three numbers a
 * caller sets a run out against, and all three have to come out exactly as declared.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFBarHandleTest, "HouseForge.Joinery.BarHandle", HF_TEST_FLAGS)

bool FHFBarHandleTest::RunTest(const FString& Parameters)
{
	const FHFHandleParams Asked = MakeHandleParams(EHFHandleStyle::Bar);
	const FHFHandleParams P = FHFJoineryKit::SanitiseHandle(Asked);

	// Nothing about a 128 mm bar on a wardrobe shutter is unreasonable, so nothing should be clamped.
	TestNearlyEqual(TEXT("A standard bar is taken as asked"), P.BarLength, Asked.BarLength, 0.0001);
	TestNearlyEqual(TEXT("Its projection is taken as asked"), P.Projection, Asked.Projection, 0.0001);

	const FDynamicMesh3 Bar = FHFJoineryKit::GenerateHandle(Asked);
	if (!TestTrue(TEXT("A bar handle produces geometry"), Bar.TriangleCount() > 0))
	{
		return false;
	}

	TestTrue(TEXT("A bar handle is watertight"), FHFMeshOps::IsClosed(Bar));
	TestTrue(TEXT("A bar handle faces outward"), Volume(Bar) > 0.0);
	TestTrue(TEXT("Every triangle carries the hardware role"),
		AllTrianglesHaveRole(Bar, EHFSurfaceRole::MetalHardware));
	TestTrue(TEXT("A bar handle is unwrapped"), HasUVs(Bar));

	// Handle-local space: +X along the run, +Y out of the face, +Z towards the edge served.
	const FAxisAlignedBox3d Local = Bar.GetBounds();

	TestNearlyEqual(TEXT("The bar is exactly its declared length"), Local.Width(), P.BarLength, 0.001);
	TestNearlyEqual(TEXT("It is centred on its own origin"), Local.Center().X, 0.0, 0.001);

	TestNearlyEqual(TEXT("It stands exactly its declared projection proud"), Local.Max.Y, P.Projection, 0.001);
	TestNearlyEqual(TEXT("Its pads sink exactly the embed into the face"), Local.Min.Y, -P.Embed, 0.001);

	TestNearlyEqual(TEXT("It is exactly its declared stock across"), Local.Depth(), P.BarDiameter, 0.001);
	TestNearlyEqual(TEXT("The stock is centred on the run"), Local.Center().Z, 0.0, 0.001);

	// Volume against what the bar and its two posts actually are, not against a remembered figure.
	// The polygon sections come in a couple of per cent under the circles they stand for and the
	// chamfered ends take a little more, which is what the tolerance is for.
	const double Radius = P.BarDiameter * 0.5;
	const double Section = UE_DOUBLE_PI * Radius * Radius;
	const double Expected = Section * P.BarLength + 2.0 * Section * (P.Projection - Radius + P.Embed);

	TestTrue(TEXT("The bar has the volume of a bar on two posts"),
		FMath::Abs(Volume(Bar) - Expected) < FMath::Abs(Expected) * 0.1);
	TestTrue(TEXT("A bar is mostly air within its own bounds"),
		Volume(Bar) < Local.Width() * Local.Depth() * Local.Height());

	// Where it lands on the panel. The leading edge of a wardrobe shutter, 5 cm in, halfway up.
	const FTransform Placement = FHFJoineryKit::HandlePlacement(Asked);
	TestTrue(TEXT("The bar sits an edge inset in from the edge it serves"),
		Placement.GetTranslation().Equals(FVector(40.0, 1.9, 105.0), 0.001));
	TestTrue(TEXT("It projects out of the panel's front face"),
		Placement.GetUnitAxis(EAxis::Y).Equals(FVector(0.0, 1.0, 0.0), 0.001));
	TestTrue(TEXT("It runs parallel to the edge it serves"),
		FMath::Abs(Placement.GetUnitAxis(EAxis::X).Z) > 0.999);

	return true;
}

/** A knob: one revolved solid, domed rather than capped flat, sized by its head. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFKnobHandleTest, "HouseForge.Joinery.KnobHandle", HF_TEST_FLAGS)

bool FHFKnobHandleTest::RunTest(const FString& Parameters)
{
	const FHFHandleParams Asked = MakeHandleParams(EHFHandleStyle::Knob);
	const FHFHandleParams P = FHFJoineryKit::SanitiseHandle(Asked);

	const FDynamicMesh3 Knob = FHFJoineryKit::GenerateHandle(Asked);
	if (!TestTrue(TEXT("A knob produces geometry"), Knob.TriangleCount() > 0))
	{
		return false;
	}

	TestTrue(TEXT("A knob is watertight"), FHFMeshOps::IsClosed(Knob));
	TestTrue(TEXT("A knob faces outward"), Volume(Knob) > 0.0);
	TestTrue(TEXT("Every triangle carries the hardware role"),
		AllTrianglesHaveRole(Knob, EHFSurfaceRole::MetalHardware));
	TestTrue(TEXT("A knob is unwrapped"), HasUVs(Knob));

	const FAxisAlignedBox3d Local = Knob.GetBounds();

	// A knob is its head diameter across, both ways: it is a solid of revolution, so anything else
	// would mean the section is not actually round.
	// Depth(), not Height(). FAxisAlignedBox3d spells its extents Width/Height/Depth for X/Y/Z, so
	// Height() is the projection axis - the one direction across which a knob is emphatically NOT its
	// head diameter. The BarHandle test above measures the same stock with Depth() for the same reason.
	TestNearlyEqual(TEXT("A knob is its head diameter across the run"), Local.Width(), P.KnobDiameter, 0.001);
	TestNearlyEqual(TEXT("And the same across the other way"), Local.Depth(), P.KnobDiameter, 0.001);
	TestNearlyEqual(TEXT("It is centred on its own origin"), Local.Center().X, 0.0, 0.001);
	TestNearlyEqual(TEXT("Squarely so"), Local.Center().Z, 0.0, 0.001);

	TestNearlyEqual(TEXT("It stands exactly its declared projection proud"), Local.Max.Y, P.Projection, 0.001);
	TestNearlyEqual(TEXT("Its stem sinks exactly the embed into the face"), Local.Min.Y, -P.Embed, 0.001);

	// A dome, not a disc on a stick. The head is well under the cylinder it is inscribed in, and
	// well over the stem alone, which is a shape assertion rather than a triangle-count one.
	const double HeadRadius = P.KnobDiameter * 0.5;
	const double StemRadius = P.KnobStemDiameter * 0.5;
	const double HeadCylinder = UE_DOUBLE_PI * HeadRadius * HeadRadius * P.Projection;
	const double StemCylinder = UE_DOUBLE_PI * StemRadius * StemRadius * P.Projection;

	TestTrue(TEXT("A knob is more than its stem"), Volume(Knob) > StemCylinder);
	TestTrue(TEXT("A knob is less than the cylinder around it"), Volume(Knob) < HeadCylinder);

	// And it is a genuinely different object from a bar, not the same solid relabelled.
	const FDynamicMesh3 Bar = FHFJoineryKit::GenerateHandle(MakeHandleParams(EHFHandleStyle::Bar));
	TestTrue(TEXT("A knob is not shaped like a bar"),
		FMath::Abs(Knob.GetBounds().Width() - Bar.GetBounds().Width()) > 1.0);

	return true;
}

/**
 * The two routed styles, which are holes in a panel rather than parts fitted to one.
 *
 * Asserted on what the panel loses, because that is the only thing a recess is. The distinguishing
 * property is where the cut stops: a J-profile breaks out through the panel edge and a handleless
 * groove leaves a rail of board between itself and that edge, and a generator that got those the
 * same way round would produce two identical shutters under two different names.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFHandleRecessTest, "HouseForge.Joinery.HandleRecess", HF_TEST_FLAGS)

bool FHFHandleRecessTest::RunTest(const FString& Parameters)
{
	const FBox Box = ShutterPanelBox();
	const double PanelVolume = Box.GetSize().X * Box.GetSize().Y * Box.GetSize().Z;
	const double RunSpan = Box.GetSize().X;

	TestTrue(TEXT("A J-profile is routed, not applied"),
		FHFJoineryKit::IsRecessedHandle(EHFHandleStyle::JProfile));
	TestTrue(TEXT("A handleless groove is routed, not applied"),
		FHFJoineryKit::IsRecessedHandle(EHFHandleStyle::HandlelessGroove));
	TestFalse(TEXT("A bar is not"), FHFJoineryKit::IsRecessedHandle(EHFHandleStyle::Bar));

	// A recess is not a part, and must not come back looking like one.
	TestEqual(TEXT("A J-profile produces no applied solid"),
		FHFJoineryKit::GenerateHandle(MakeHandleParams(EHFHandleStyle::JProfile, EHFHandleEdge::Top)).TriangleCount(), 0);
	TestEqual(TEXT("Nor does a handleless groove"),
		FHFJoineryKit::GenerateHandle(MakeHandleParams(EHFHandleStyle::HandlelessGroove, EHFHandleEdge::Top)).TriangleCount(), 0);
	TestEqual(TEXT("A bar produces no cutter"),
		FHFJoineryKit::GenerateHandleRecessCutter(MakeHandleParams(EHFHandleStyle::Bar)).TriangleCount(), 0);

	// ------------------------------------------------------------------------------- J-profile

	const FHFHandleParams JAsked = MakeHandleParams(EHFHandleStyle::JProfile, EHFHandleEdge::Top);
	const FHFHandleParams J = FHFJoineryKit::SanitiseHandle(JAsked);

	const FDynamicMesh3 JCutter = FHFJoineryKit::GenerateHandleRecessCutter(JAsked);
	if (!TestTrue(TEXT("A J-profile produces a cutter"), JCutter.TriangleCount() > 0))
	{
		return false;
	}

	TestTrue(TEXT("The cutter is watertight"), FHFMeshOps::IsClosed(JCutter));
	TestTrue(TEXT("The cutter faces outward"), Volume(JCutter) > 0.0);

	const FAxisAlignedBox3d JBox = JCutter.GetBounds();

	// Overshooting every face it means to break out of is what keeps it from leaving the boolean a
	// pair of coplanar faces to resolve.
	TestTrue(TEXT("The cutter runs past both ends of the panel"),
		JBox.Min.X < Box.Min.X - 0.5 && JBox.Max.X > Box.Max.X + 0.5);
	TestTrue(TEXT("It stands off the panel face"), JBox.Max.Y > Box.Max.Y + 0.5);
	TestNearlyEqual(TEXT("It cuts exactly the declared depth into the board"),
		JBox.Min.Y, Box.Max.Y - J.RecessDepth, 0.001);

	// The thing that makes it a J: the cut goes out through the edge.
	TestTrue(TEXT("A J-profile breaks out through the panel edge"), JBox.Max.Z > Box.Max.Z + 0.5);
	TestNearlyEqual(TEXT("Its profile reaches exactly its declared height down the face"),
		JBox.Min.Z, Box.Max.Z - J.ProfileHeight - J.LipChamfer, 0.001);

	FDynamicMesh3 JPanel = MakePanelMesh(Box);
	if (!TestTrue(TEXT("The J-profile applies"), FHFJoineryKit::ApplyHandle(JPanel, JAsked)))
	{
		return false;
	}

	TestTrue(TEXT("The routed panel is still watertight"), FHFMeshOps::IsClosed(JPanel));
	TestTrue(TEXT("The routed panel still faces outward"), Volume(JPanel) > 0.0);
	TestTrue(TEXT("No routed face was left without a surface role"), EveryTriangleTagged(JPanel));
	TestTrue(TEXT("The routed panel is unwrapped"), HasUVs(JPanel));

	// The corner notch, plus the little wedge the chamfer takes off the lip. Measured, not counted.
	const double JRemoved = RunSpan * (J.ProfileHeight * J.RecessDepth + J.LipChamfer * J.LipChamfer * 0.5);
	TestNearlyEqual(TEXT("A J-profile removes exactly the corner it describes"),
		PanelVolume - Volume(JPanel), JRemoved, FMath::Abs(JRemoved) * 0.01);

	// A notch off one corner leaves the panel's overall extents alone: board survives behind the
	// recess and below it, so nothing has actually got smaller.
	TestTrue(TEXT("Routing a J-profile does not shrink the panel"),
		JPanel.GetBounds().Max.Equals(FVector3d(Box.Max), 0.01)
		&& JPanel.GetBounds().Min.Equals(FVector3d(Box.Min), 0.01));

	// ------------------------------------------------------------------------ handleless groove

	const FHFHandleParams GAsked = MakeHandleParams(EHFHandleStyle::HandlelessGroove, EHFHandleEdge::Top);
	const FHFHandleParams G = FHFJoineryKit::SanitiseHandle(GAsked);

	const FDynamicMesh3 GCutter = FHFJoineryKit::GenerateHandleRecessCutter(GAsked);
	if (!TestTrue(TEXT("A handleless groove produces a cutter"), GCutter.TriangleCount() > 0))
	{
		return false;
	}

	TestTrue(TEXT("The groove cutter is watertight"), FHFMeshOps::IsClosed(GCutter));
	TestTrue(TEXT("The groove cutter faces outward"), Volume(GCutter) > 0.0);

	const FAxisAlignedBox3d GBox = GCutter.GetBounds();

	// The whole difference between the two styles, stated as a measurement: this one stops short of
	// the edge, so a rail of board survives and each front keeps its own outline.
	TestTrue(TEXT("A groove stops short of the panel edge"), GBox.Max.Z < Box.Max.Z);
	TestNearlyEqual(TEXT("It leaves exactly its declared margin, less the chamfer"),
		Box.Max.Z - GBox.Max.Z, G.GrooveEdgeMargin - G.LipChamfer, 0.001);
	TestNearlyEqual(TEXT("It cuts exactly the declared depth into the board"),
		GBox.Min.Y, Box.Max.Y - G.RecessDepth, 0.001);

	FDynamicMesh3 GPanel = MakePanelMesh(Box);
	if (!TestTrue(TEXT("The groove applies"), FHFJoineryKit::ApplyHandle(GPanel, GAsked)))
	{
		return false;
	}

	TestTrue(TEXT("The grooved panel is still watertight"), FHFMeshOps::IsClosed(GPanel));
	TestTrue(TEXT("The grooved panel still faces outward"), Volume(GPanel) > 0.0);
	TestTrue(TEXT("No grooved face was left without a surface role"), EveryTriangleTagged(GPanel));

	// The channel, plus a chamfer wedge on each of its two lips - one more than the J-profile gets.
	const double GRemoved = RunSpan * (G.ProfileHeight * G.RecessDepth + G.LipChamfer * G.LipChamfer);
	TestNearlyEqual(TEXT("A groove removes exactly the channel it describes"),
		PanelVolume - Volume(GPanel), GRemoved, FMath::Abs(GRemoved) * 0.01);

	TestTrue(TEXT("Routing a groove does not shrink the panel"),
		GPanel.GetBounds().Max.Equals(FVector3d(Box.Max), 0.01)
		&& GPanel.GetBounds().Min.Equals(FVector3d(Box.Min), 0.01));

	// ---------------------------------------------------------------- a recess deeper than the board

	FHFHandleParams TooDeep = JAsked;
	TooDeep.RecessDepth = 10.0;
	const FHFHandleParams Clamped = FHFJoineryKit::SanitiseHandle(TooDeep);

	TestNearlyEqual(TEXT("A recess deeper than the board is clamped to leave the web"),
		Clamped.RecessDepth, Box.GetSize().Y - Clamped.MinWeb, 0.001);

	FDynamicMesh3 DeepPanel = MakePanelMesh(Box);
	TestTrue(TEXT("The clamped recess applies"), FHFJoineryKit::ApplyHandle(DeepPanel, TooDeep));
	TestTrue(TEXT("A clamped recess leaves a watertight panel"), FHFMeshOps::IsClosed(DeepPanel));
	TestTrue(TEXT("A clamped recess does not rout the panel in two"), Volume(DeepPanel) > 0.0);
	TestNearlyEqual(TEXT("The web behind it is exactly what was reserved"),
		FHFJoineryKit::GenerateHandleRecessCutter(TooDeep).GetBounds().Min.Y - Box.Min.Y,
		Clamped.MinWeb, 0.001);

	return true;
}

/**
 * The rule this whole part exists to satisfy: a handle rides with what it is screwed to.
 *
 * A handle does not move on its own, which makes it tempting to drop into the carcass mesh with
 * everything else fixed. On a shutter that is wrong, and wrong in the way that is hardest to catch -
 * closed and photographed from the front it is indistinguishable from correct, and it only comes
 * apart when the leaf swings. So the assertion is on the swept transform: the handle's outermost
 * point has to travel the shutter's arc, about the shutter's hinge, at the shutter's angle.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFHandleRidesWithPartTest, "HouseForge.Joinery.HandleRidesWithPart", HF_TEST_FLAGS)

bool FHFHandleRidesWithPartTest::RunTest(const FString& Parameters)
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

	const double BareVolume = Volume(Part.Mesh);

	// A left-hung leaf carries its thickness on +Y from the hinge axis, so the face that looks out of
	// the cupboard is the one at local Y = 0, facing -Y. Fitting a handle to the other one puts it
	// inside the wardrobe.
	FHFHandleParams Handle = MakeHandleParams(EHFHandleStyle::Bar);
	Handle.PanelBox = FBox(FVector(0.0, 0.0, 0.0),
		FVector(Shutter.LeafWidth(), Shutter.Thickness, Shutter.LeafHeight()));
	Handle.Facing = EHFPanelFacing::NegativeY;
	Handle.Edge = EHFHandleEdge::MaxX;

	const FHFHandleParams P = FHFJoineryKit::SanitiseHandle(Handle);

	if (!TestTrue(TEXT("The handle applies to the leaf"), FHFJoineryKit::ApplyHandle(Part.Mesh, Handle)))
	{
		return false;
	}

	TestTrue(TEXT("The leaf is still watertight with a handle on it"), FHFMeshOps::IsClosed(Part.Mesh));
	TestTrue(TEXT("No triangle on the leaf lost its surface role"), EveryTriangleTagged(Part.Mesh));
	TestTrue(TEXT("The leaf is unwrapped"), HasUVs(Part.Mesh));

	// The handle really is in the PART's mesh - the leaf gained exactly the handle's own volume, and
	// its bounds now reach the handle's projection out in front of the leaf face.
	const double HandleVolume = Volume(FHFJoineryKit::GenerateHandle(Handle));
	TestTrue(TEXT("A handle is a solid in its own right"), HandleVolume > 0.0);
	TestNearlyEqual(TEXT("The leaf gained exactly the handle"),
		Volume(Part.Mesh) - BareVolume, HandleVolume, FMath::Abs(HandleVolume) * 0.001);
	TestNearlyEqual(TEXT("The handle stands proud of the leaf's front face"),
		Part.Mesh.GetBounds().Min.Y, -P.Projection, 0.001);

	// The handle's outermost point, in the part's own local space. Anything screwed to a shutter has
	// to swing with it, and this is the point furthest from the hinge in the direction that matters.
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
	TestNearlyEqual(TEXT("The tip is on the handle's outer face"), LocalTip.Y, -P.Projection, 0.001);

	FHFPartState State;
	State.PartId = Part.PartId;
	State.PivotTransform = Part.PivotTransform;
	State.Motion = Part.Motion;

	const FVector Closed = State.PoseAt(0.0).TransformPosition(FVector(LocalTip));
	const FVector Open = State.PoseAt(1.0).TransformPosition(FVector(LocalTip));

	TestTrue(TEXT("Closed, the handle sits where the pivot puts it"),
		Closed.Equals(Part.PivotTransform.TransformPosition(FVector(LocalTip)), 0.001));
	TestTrue(TEXT("Opening the shutter moves the handle with it"),
		FVector::Dist(Closed, Open) > 10.0);

	// A hinge is a rotation, so the handle's distance from the hinge axis cannot change. A handle
	// left behind in the carcass mesh would not move at all; one welded on at the wrong pivot would
	// move and fail this.
	const FVector Axis = Part.PivotTransform.GetTranslation();
	auto RadiusFromHinge = [&Axis](const FVector& Point)
	{
		return FVector2D(Point.X - Axis.X, Point.Y - Axis.Y).Size();
	};
	TestNearlyEqual(TEXT("The handle keeps its distance from the hinge line"),
		RadiusFromHinge(Open), RadiusFromHinge(Closed), 0.01);
	TestNearlyEqual(TEXT("And its height"), Open.Z, Closed.Z, 0.01);

	// And it swings through exactly the leaf's own angle, in the leaf's own direction.
	auto AngleAboutHinge = [&Axis](const FVector& Point)
	{
		return FMath::RadiansToDegrees(FMath::Atan2(Point.Y - Axis.Y, Point.X - Axis.X));
	};
	TestNearlyEqual(TEXT("The handle sweeps the shutter's declared angle"),
		FMath::UnwindDegrees(AngleAboutHinge(Open) - AngleAboutHinge(Closed)),
		Part.Motion.MaxAngleDegrees, 0.01);

	// Half open is half the angle, so a Sequencer track on the shutter drags the handle linearly
	// rather than snapping it at the ends.
	const FVector Half = State.PoseAt(0.5).TransformPosition(FVector(LocalTip));
	TestNearlyEqual(TEXT("Half open carries the handle halfway round"),
		FMath::UnwindDegrees(AngleAboutHinge(Half) - AngleAboutHinge(Closed)),
		Part.Motion.MaxAngleDegrees * 0.5, 0.01);

	return true;
}

/**
 * Every parameter has to actually reach the geometry.
 *
 * A generator that quietly ignores half its struct still passes a watertightness check and still
 * produces something plausible; the failure only surfaces when someone changes a number and nothing
 * moves. So each one is changed on its own and the result measured.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFHandleParametersTest, "HouseForge.Joinery.HandleParameters", HF_TEST_FLAGS)

bool FHFHandleParametersTest::RunTest(const FString& Parameters)
{
	const FHFHandleParams Base = MakeHandleParams(EHFHandleStyle::Bar);
	const FDynamicMesh3 Reference = FHFJoineryKit::GenerateHandle(Base);
	const FAxisAlignedBox3d RefBounds = Reference.GetBounds();

	// Length, stock and projection are the three dimensions, and each moves exactly one of them.
	{
		FHFHandleParams Longer = Base;
		Longer.BarLength = Base.BarLength * 2.0;
		const FDynamicMesh3 Mesh = FHFJoineryKit::GenerateHandle(Longer);

		TestNearlyEqual(TEXT("A longer bar is longer by exactly that much"),
			Mesh.GetBounds().Width(), Longer.BarLength, 0.001);
		TestTrue(TEXT("A longer bar holds more metal"), Volume(Mesh) > Volume(Reference));
		TestNearlyEqual(TEXT("Length does not change the stock"),
			Mesh.GetBounds().Depth(), RefBounds.Depth(), 0.001);
	}
	{
		FHFHandleParams Thicker = Base;
		Thicker.BarDiameter = Base.BarDiameter * 2.0;
		const FDynamicMesh3 Mesh = FHFJoineryKit::GenerateHandle(Thicker);

		TestNearlyEqual(TEXT("Thicker stock measures thicker"),
			Mesh.GetBounds().Depth(), Thicker.BarDiameter, 0.001);
		TestTrue(TEXT("Thicker stock holds more metal"), Volume(Mesh) > Volume(Reference));
	}
	{
		FHFHandleParams Prouder = Base;
		Prouder.Projection = Base.Projection * 2.0;
		const FDynamicMesh3 Mesh = FHFJoineryKit::GenerateHandle(Prouder);

		TestNearlyEqual(TEXT("A prouder handle reaches further off the face"),
			Mesh.GetBounds().Max.Y, Prouder.Projection, 0.001);
		TestNearlyEqual(TEXT("Projection does not change the length"),
			Mesh.GetBounds().Width(), RefBounds.Width(), 0.001);
	}
	{
		FHFHandleParams Rounder = Base;
		Rounder.SideCount = 48;
		const FDynamicMesh3 Coarse = FHFJoineryKit::GenerateHandle(Base);
		const FDynamicMesh3 Fine = FHFJoineryKit::GenerateHandle(Rounder);

		// More sides means a section closer to the circle it stands for - so more volume, inside the
		// same bounds. Asserting on the approximation rather than on a triangle count.
		TestTrue(TEXT("More sides fill more of the circle"), Volume(Fine) > Volume(Coarse));
		TestNearlyEqual(TEXT("More sides do not change the declared stock"),
			Fine.GetBounds().Depth(), Coarse.GetBounds().Depth(), 0.001);
	}

	// Which edge is served changes both where the handle lands and which way it runs.
	{
		const FTransform OnLeading = FHFJoineryKit::HandlePlacement(Base);
		const FTransform OnTop =
			FHFJoineryKit::HandlePlacement(MakeHandleParams(EHFHandleStyle::Bar, EHFHandleEdge::Top));

		TestTrue(TEXT("A different edge is a different place"),
			FVector::Dist(OnLeading.GetTranslation(), OnTop.GetTranslation()) > 10.0);
		TestTrue(TEXT("A bar on the top edge runs horizontally"),
			FMath::Abs(OnTop.GetUnitAxis(EAxis::X).X) > 0.999);
		TestTrue(TEXT("A bar on the leading edge runs vertically"),
			FMath::Abs(OnLeading.GetUnitAxis(EAxis::X).Z) > 0.999);
	}
	{
		const FTransform MinEdge =
			FHFJoineryKit::HandlePlacement(MakeHandleParams(EHFHandleStyle::Bar, EHFHandleEdge::MinX));
		TestNearlyEqual(TEXT("The far edge is measured from the other end"),
			MinEdge.GetTranslation().X, Base.EdgeInset, 0.001);
	}

	// Facing decides which side of the board the handle is screwed to, and getting it wrong fits the
	// handle inside the cupboard.
	{
		FHFHandleParams Behind = Base;
		Behind.Facing = EHFPanelFacing::NegativeY;
		const FTransform Front = FHFJoineryKit::HandlePlacement(Base);
		const FTransform Back = FHFJoineryKit::HandlePlacement(Behind);

		TestNearlyEqual(TEXT("A front-facing handle mounts on the front face"),
			Front.GetTranslation().Y, Base.PanelBox.Max.Y, 0.001);
		TestNearlyEqual(TEXT("A back-facing one mounts on the back face"),
			Back.GetTranslation().Y, Base.PanelBox.Min.Y, 0.001);
		TestTrue(TEXT("And projects the other way"),
			Back.GetUnitAxis(EAxis::Y).Equals(-Front.GetUnitAxis(EAxis::Y), 0.001));
	}

	// The routed styles are driven by their own numbers, and by different ones from each other.
	{
		const FBox Box = ShutterPanelBox();
		const double PanelVolume = Box.GetSize().X * Box.GetSize().Y * Box.GetSize().Z;

		auto RoutedLoss = [&Box, PanelVolume](const FHFHandleParams& Params)
		{
			FDynamicMesh3 Panel = MakePanelMesh(Box);
			return FHFJoineryKit::ApplyHandle(Panel, Params) ? PanelVolume - Volume(Panel) : 0.0;
		};

		const FHFHandleParams J = MakeHandleParams(EHFHandleStyle::JProfile, EHFHandleEdge::Top);
		FHFHandleParams Taller = J;
		Taller.ProfileHeight = J.ProfileHeight * 1.5;

		// Depth is measured DOWNWARDS from the default rather than upwards from it, because there is no
		// room upwards: on a 19 mm board reserving a 5 mm web, the deepest legal recess is 14 mm and the
		// 12 mm default is already all but against that stop. Asking for 18 mm gets 14 mm back - which is
		// the clamp doing its job, asserted as such in HouseForge.Joinery.HandleRecess - so an assertion
		// that 1.5x the depth removes 1.4x the board could never be satisfied by any correct generator.
		FHFHandleParams Shallower = J;
		Shallower.RecessDepth = J.RecessDepth / 1.5;

		const double BaseLoss = RoutedLoss(J);
		TestTrue(TEXT("A routed profile removes something"), BaseLoss > 0.0);
		TestTrue(TEXT("A deeper recess removes more"), BaseLoss > RoutedLoss(Shallower) * 1.4);
		TestTrue(TEXT("A taller profile removes more"), RoutedLoss(Taller) > BaseLoss * 1.4);

		// The margin belongs to the groove alone: it is what stops the cut short of the edge, and a
		// J-profile has no use for it because a J-profile is defined by going through the edge.
		FHFHandleParams Groove = MakeHandleParams(EHFHandleStyle::HandlelessGroove, EHFHandleEdge::Top);
		FHFHandleParams WiderMargin = Groove;
		WiderMargin.GrooveEdgeMargin = Groove.GrooveEdgeMargin * 3.0;

		const FAxisAlignedBox3d Near = FHFJoineryKit::GenerateHandleRecessCutter(Groove).GetBounds();
		const FAxisAlignedBox3d Far = FHFJoineryKit::GenerateHandleRecessCutter(WiderMargin).GetBounds();
		TestTrue(TEXT("A wider margin pushes the groove further from the edge"), Far.Max.Z < Near.Max.Z);
		TestTrue(TEXT("Both grooves still stop short of it"), Near.Max.Z < Box.Max.Z);
	}

	// And no handle is genuinely no handle, not an empty solid quietly welded on.
	{
		FHFHandleParams None = Base;
		None.Style = EHFHandleStyle::None;

		TestEqual(TEXT("No handle generates nothing"),
			FHFJoineryKit::GenerateHandle(None).TriangleCount(), 0);

		FDynamicMesh3 Panel = MakePanelMesh(ShutterPanelBox());
		const double Before = Volume(Panel);
		TestTrue(TEXT("Applying no handle succeeds"), FHFJoineryKit::ApplyHandle(Panel, None));
		TestNearlyEqual(TEXT("Applying no handle changes nothing"), Volume(Panel), Before, 0.0001);
	}

	return true;
}

#undef HF_TEST_FLAGS

#endif // WITH_DEV_AUTOMATION_TESTS
