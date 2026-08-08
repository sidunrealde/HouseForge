// Copyright Siddartha G. All Rights Reserved.

#include "HouseForge.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Geometry/HFApplianceKit.h"
#include "Geometry/HFCounterKit.h"
#include "Geometry/HFMeshOps.h"
#include "Geometry/HFSanitaryKit.h"
#include "MeshQueries.h"
#include "Misc/AutomationTest.h"
#include "Model/HFArticulation.h"

using namespace UE::Geometry;

#define HF_TEST_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// ---------------------------------------------------------------------------------------------
//
// The kitchen group's three new kits, measured on the properties that decide whether the thing is
// what it claims to be - volume, bounds, roles, hollowness, cut apertures, swept travel - and never
// on a triangle count. See .claude/rules and the milestone brief: a count changes the moment
// anything is chamfered and says nothing about whether the geometry is right.
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

	/** Where a point given in a part's local space ends up, in the actor, at a given open amount. */
	FVector PosedPoint(const FHFMeshPart& Part, const FVector& LocalPoint, double OpenAmount)
	{
		FHFPartState State;
		State.PivotTransform = Part.PivotTransform;
		State.Motion = Part.Motion;
		State.OpenAmount = OpenAmount;

		return State.CurrentPose().TransformPosition(LocalPoint);
	}

	FHFCounterParams MakeCounter()
	{
		FHFCounterParams P;
		P.Width = 214.0;
		P.Depth = 60.0;
		P.Thickness = 4.0;
		P.Overhang = 4.0;
		P.UpstandHeight = 10.0;
		P.Edge = EHFCounterEdge::DripGroove;
		return P;
	}

	FHFSinkParams MakeSink()
	{
		FHFSinkParams P;
		P.Width = 80.0;
		P.Depth = 45.0;
		P.BowlDepth = 20.0;
		P.BowlCount = 2;
		return P;
	}

	FHFHobParams MakeHob()
	{
		FHFHobParams P;
		P.Width = 58.0;
		P.Depth = 50.0;
		return P;
	}

	FHFChimneyParams MakeChimney()
	{
		FHFChimneyParams P;
		P.Width = 60.0;
		P.Depth = 50.0;
		P.CanopyHeight = 70.0;
		P.TaperHeight = 38.5;
		P.DuctLength = 65.0;
		return P;
	}
}

// ------------------------------------------------------------------------------------- counter

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFCounterSlabTest, "HouseForge.Kitchen.CounterIsASlabWithAnEdge",
	HF_TEST_FLAGS)

bool FHFCounterSlabTest::RunTest(const FString& Parameters)
{
	const FHFCounterBuild Built = FHFCounterKit::Build(MakeCounter());

	TestTrue(TEXT("A counter builds"), Built.bValid);
	TestTrue(TEXT("It is closed"), FHFMeshOps::IsClosed(Built.Shell));
	TestTrue(TEXT("It has positive volume"), Volume(Built.Shell) > 0.0);
	TestTrue(TEXT("Every triangle carries a surface role"), EveryTriangleHasARole(Built.Shell));

	const FAxisAlignedBox3d Bounds = Built.Shell.GetBounds();

	// The run's length is the drawn one exactly - a worktop that is not the length of its run is a
	// worktop with a gap at one end.
	TestNearlyEqual(TEXT("It is as long as the run"), Bounds.Max.X - Bounds.Min.X, 214.0, 0.01);

	// THE STONE OVERSAILS THE DOORS. The front edge stands in front of the drawn footprint by the
	// overhang, which is the figure the composing layer resolves against the shutter's own face.
	TestNearlyEqual(TEXT("The front edge stands proud of the drawn footprint"),
		Bounds.Min.Y, -4.0, 0.01);
	TestNearlyEqual(TEXT("The back is on the wall line"), Bounds.Max.Y, 60.0, 0.01);

	// And the upstand stands on the slab rather than being counted inside its thickness.
	TestNearlyEqual(TEXT("Slab plus upstand is the built height"),
		Bounds.Max.Z - Bounds.Min.Z, 14.0, 0.01);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFCounterApertureTest, "HouseForge.Kitchen.CounterIsActuallyCut",
	HF_TEST_FLAGS)

bool FHFCounterApertureTest::RunTest(const FString& Parameters)
{
	// THE CUTOUT MUST ACTUALLY BE CUT, NOT IMPLIED. A sink standing on an uncut slab looks perfect
	// from every angle somebody photographs a kitchen from, and the hole is the whole reason the
	// set-in resolution exists. Measured as VOLUME REMOVED rather than as anything about the mesh's
	// structure, because that is the property that fails when the boolean quietly does nothing.
	const FHFCounterParams Plain = MakeCounter();

	FHFCounterParams Cut = Plain;
	FHFCounterAperture Sink;
	Sink.Centre = FVector2D(104.0, 30.0);
	Sink.Size = FVector2D(77.0, 42.0);
	Sink.FixtureId = TEXT("F_Kitchen_Sink");
	Cut.Apertures.Add(Sink);

	const FHFCounterBuild Uncut = FHFCounterKit::Build(Plain);
	const FHFCounterBuild Holed = FHFCounterKit::Build(Cut);

	TestTrue(TEXT("Both build"), Uncut.bValid && Holed.bValid);
	TestEqual(TEXT("The hole was accepted"), Holed.CutApertures.Num(), 1);
	TestTrue(TEXT("The cut slab is still closed"), FHFMeshOps::IsClosed(Holed.Shell));

	// 77 x 42 through 4 cm of stone.
	const double Expected = 77.0 * 42.0 * 4.0;
	const double Removed = Volume(Uncut.Shell) - Volume(Holed.Shell);

	AddInfo(FString::Printf(TEXT("Removed %.0f cm3, expected %.0f."), Removed, Expected));
	TestNearlyEqual(TEXT("Exactly the aperture's worth of stone came out"), Removed, Expected, 1.0);

	// A HOLE THAT CANNOT LEAVE STONE ROUND IT IS REFUSED RATHER THAN CUT. Granite cracks at the
	// corner of a cutout, and a slab with a hole through its front edge is not a worktop.
	FHFCounterParams TooClose = Plain;
	FHFCounterAperture Overhanging = Sink;
	Overhanging.Centre = FVector2D(104.0, 1.0);
	TooClose.Apertures.Add(Overhanging);

	const FHFCounterBuild Refused = FHFCounterKit::Build(TooClose);
	TestEqual(TEXT("A hole through the front edge is refused"), Refused.CutApertures.Num(), 0);
	TestTrue(TEXT("And the slab is still built"), Refused.bValid);

	return true;
}

// ---------------------------------------------------------------------------------------- sink

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFSinkBowlTest, "HouseForge.Kitchen.SinkBowlsAreHollow",
	HF_TEST_FLAGS)

bool FHFSinkBowlTest::RunTest(const FString& Parameters)
{
	const FHFSinkBuild Built = FHFSanitaryKit::BuildSink(MakeSink());

	TestTrue(TEXT("A sink builds"), Built.bValid);
	TestTrue(TEXT("Every triangle carries a surface role"), EveryTriangleHasARole(Built.Shell));

	const FAxisAlignedBox3d Bounds = Built.Shell.GetBounds();

	TestNearlyEqual(TEXT("The rim is the drawn footprint"), Bounds.Max.X - Bounds.Min.X, 80.0, 0.01);

	// THE RIM SITS ON THE STONE, NOT IN IT. Z = 0 is the worktop, so nothing of the flange may be
	// below it - a rim modelled the other way up put 318 cm2 of steel in the same plane as the
	// granite and z-fought with it, which is what this figure exists to hold.
	TestNearlyEqual(TEXT("Nothing of the sink but the bowls is below the worktop"),
		Bounds.Min.Z, -21.2, 0.3);

	// AND EVERY SURFACE OF IT IS SANITARYWARE OR THE TAP'S METAL. A role is not decoration: it is
	// what the material panel targets and what decides the thing's colour, so a bowl floor that came
	// out tagged as joinery carcass is a bowl floor rendered in cabinet laminate - present, solid,
	// facing the right way, measurably correct in area and volume, and visibly the wrong material at
	// the bottom of a stainless sink. Nothing but looking at it, or this, catches that.
	{
		const TSet<EHFSurfaceRole> Present = FHFMeshOps::RolesPresent(Built.Shell);

		for (const EHFSurfaceRole Role : Present)
		{
			const bool bExpected = Role == EHFSurfaceRole::Sanitary
				|| Role == EHFSurfaceRole::MetalHardware;

			TestTrue(*FString::Printf(TEXT("A sink carries only sanitary and metal, not role %d"),
				static_cast<int32>(Role)), bExpected);
		}

		TestTrue(TEXT("The pressing is sanitaryware"), Present.Contains(EHFSurfaceRole::Sanitary));
	}

	// AND THE BOWL IS A BOWL. A solid block of the same outline would have roughly the rim's
	// footprint times its depth in volume; a hollow pressing has a small fraction of that. This is
	// the assertion that fails if a bowl is ever built as a box, which is the single most obvious
	// thing this fixture can get wrong.
	const double Envelope = 80.0 * 45.0 * 20.0;
	const double Actual = Volume(Built.Shell);

	AddInfo(FString::Printf(TEXT("Sink solid volume %.0f cm3 in an envelope of %.0f."), Actual, Envelope));
	TestTrue(TEXT("The bowls are hollow, not solid"), Actual < Envelope * 0.25);
	TestTrue(TEXT("But there is real material there"), Actual > 0.0);

	// AND IT HAS A BOTTOM. Hollow is not enough, and this is not a hypothetical: the first version
	// that passed the volume test above was two neat tubes looking straight through into the cabinet
	// below, because the cavity's floor and the base's top wanted to be the same plane and the
	// boolean resolved that by taking the base. Hollow, correct volume, correct bounds, no bottom.
	//
	// Measured by counting the mesh's downward-facing area at the bowl's floor level: a bowl with a
	// base has a floor there, and a tube has nothing at all.
	{
		const double FloorZ = -Built.Used.BowlDepth;
		double FloorArea = 0.0;

		for (const int32 Tri : Built.Shell.TriangleIndicesItr())
		{
			FVector3d A, B, C;
			Built.Shell.GetTriVertices(Tri, A, B, C);

			const double MidZ = (A.Z + B.Z + C.Z) / 3.0;
			if (FMath::Abs(MidZ - FloorZ) > 0.6)
			{
				continue;
			}

			// FACING UP, not merely horizontal. A floor whose winding is inverted is culled from
			// above: the bowl looks straight through into whatever is under the sink, while every
			// measurement of area, volume and bounds still passes. Accepting any horizontal face
			// here would accept exactly that.
			const FVector3d Normal = VectorUtil::Normal(A, B, C);
			if (Normal.Z > 0.9)
			{
				FloorArea += VectorUtil::Area(A, B, C);
			}
		}

		AddInfo(FString::Printf(TEXT("Upward-facing bowl floor at the base level: %.0f cm2."), FloorArea));

		// Two bowls of roughly 33 x 38, so well over one bowl's footprint of floor looking up.
		TestTrue(TEXT("The bowls have a bottom, and it faces up"), FloorArea > 1200.0);
	}

	// A single-bowl utility sink is the same object at a different count, not a different fixture.
	FHFSinkParams Single = MakeSink();
	Single.Width = 60.0;
	Single.BowlCount = 1;

	const FHFSinkBuild Utility = FHFSanitaryKit::BuildSink(Single);
	TestTrue(TEXT("A single-bowl sink builds too"), Utility.bValid);
	TestTrue(TEXT("Parameters genuinely change the output"),
		FMath::Abs(Volume(Utility.Shell) - Actual) > 1.0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFTapMovesTest, "HouseForge.Kitchen.TheTapActuallyMoves",
	HF_TEST_FLAGS)

bool FHFTapMovesTest::RunTest(const FString& Parameters)
{
	const FHFSinkBuild Built = FHFSanitaryKit::BuildSink(MakeSink());

	const FHFMeshPart* Spout = Built.Parts.FindByPredicate(
		[](const FHFMeshPart& P) { return P.PartId == FHFSanitaryKit::TapSpoutPartId(); });
	const FHFMeshPart* Lever = Built.Parts.FindByPredicate(
		[](const FHFMeshPart& P) { return P.PartId == FHFSanitaryKit::TapLeverPartId(); });

	if (!TestNotNull(TEXT("The tap has a spout"), Spout)
		|| !TestNotNull(TEXT("The tap has a lever"), Lever))
	{
		return false;
	}

	TestEqual(TEXT("The spout swivels"), Spout->Motion.Type, EHFMotionType::Hinge);
	TestEqual(TEXT("The lever hinges"), Lever->Motion.Type, EHFMotionType::Hinge);

	// ASSERT THE SWEPT TRANSFORM, NOT THAT SOMETHING MOVED. A spout that turns about the wrong axis
	// still "moves"; what makes it a swivel spout is that its nose ends up over the OTHER bowl, and
	// that is a distance in centimetres.
	const FAxisAlignedBox3d SpoutBox = Spout->Mesh.GetBounds();
	const FVector Nose(SpoutBox.Center().X, SpoutBox.Min.Y, SpoutBox.Center().Z);

	const FVector Travelled = PosedPoint(*Spout, Nose, 1.0) - PosedPoint(*Spout, Nose, 0.0);

	AddInfo(FString::Printf(TEXT("The spout's nose travels %.1f cm at full swivel."), Travelled.Size()));

	// A 20 cm reach swung through 90 degrees puts the nose about 28 cm away, and the sink's two bowls
	// are 40 cm apart on centres - so a swivel worth having moves the nose most of a bowl's width.
	TestTrue(TEXT("The spout reaches across the sink"), Travelled.Size() > 20.0);

	// And it swings in PLAN rather than lifting: a spout that hinged about a horizontal axis would
	// travel just as far and point at the ceiling.
	TestTrue(TEXT("It swings in plan rather than tipping up"),
		FMath::Abs(Travelled.Z) < 1.0);

	// The lever lifts, which is the other way round: it must gain height.
	const FAxisAlignedBox3d LeverBox = Lever->Mesh.GetBounds();
	const FVector Tip(LeverBox.Center().X, LeverBox.Max.Y, LeverBox.Center().Z);

	const double Lift = PosedPoint(*Lever, Tip, 1.0).Z - PosedPoint(*Lever, Tip, 0.0).Z;
	AddInfo(FString::Printf(TEXT("The lever's tip lifts %.1f cm."), Lift));
	TestTrue(TEXT("The lever lifts rather than merely turning"), Lift > 1.0);

	return true;
}

// ----------------------------------------------------------------------------------------- hob

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFHobTest, "HouseForge.Kitchen.HobHasBurnersAndTurningKnobs",
	HF_TEST_FLAGS)

bool FHFHobTest::RunTest(const FString& Parameters)
{
	const FHFApplianceBuild Built = FHFApplianceKit::BuildHob(MakeHob());

	TestTrue(TEXT("A hob builds"), Built.bValid);
	TestTrue(TEXT("It has positive volume"), Volume(Built.Shell) > 0.0);
	TestTrue(TEXT("Every triangle carries a surface role"), EveryTriangleHasARole(Built.Shell));

	// The glass is the drawn footprint, because it is what laps the counter's cutout.
	const FAxisAlignedBox3d Bounds = Built.Shell.GetBounds();
	TestNearlyEqual(TEXT("The glass is the drawn width"), Bounds.Max.X - Bounds.Min.X, 58.0, 0.01);

	// THE BURNERS STAND ABOVE THE STONE AND THE BODY DROPS BELOW IT, which is the whole reason the
	// counter has to be cut at all. Z = 0 is the worktop.
	TestTrue(TEXT("The pan supports stand above the worktop"), Bounds.Max.Z > 2.0);
	TestTrue(TEXT("The body drops below it"), Bounds.Min.Z < 0.0);

	// A knob per burner, each turning about its own axis.
	TestEqual(TEXT("One knob per burner"), Built.Parts.Num(), 4);

	for (const FHFMeshPart& Knob : Built.Parts)
	{
		TestEqual(TEXT("A knob turns"), Knob.Motion.Type, EHFMotionType::Hinge);
		TestTrue(TEXT("Through a gas tap's full sweep"),
			FMath::Abs(Knob.Motion.MaxAngleDegrees) > 180.0);
	}

	// A DROP-IN HOB'S KNOBS STAND ON THE GLASS AND TURN ABOUT THE VERTICAL. They used to be discs on
	// a vertical front face turning about Y, which is a freestanding cooker's controls - and this hob
	// is cut into stone, so there is nowhere below the glass line for anything to be except inside
	// the granite. Each of the four had 12 mm of itself in there. Z = 0 is the worktop.
	for (const FHFMeshPart& Knob : Built.Parts)
	{
		TestTrue(TEXT("A knob turns about the vertical"),
			FMath::Abs(Knob.Motion.Axis.Z) > 0.99);

		const FVector Base = Knob.PivotTransform.TransformPosition(
			FVector(Knob.Mesh.GetBounds().Min.X, 0.0, Knob.Mesh.GetBounds().Min.Z));
		TestTrue(*FString::Printf(TEXT("And stands on the stone rather than in it (%.2f cm)"), Base.Z),
			Base.Z > -0.01);
	}

	// ASSERT THE SWEPT TRANSFORM. A knob is round, so a rotation about its own axis moves nothing
	// measurable unless the thing that indexes the turn moves - which is exactly why it has a
	// pointer flag. Measured on the flag rather than on the knob's bounds.
	//
	// OFF THE AXIS, which the vertical made matter: the old probe was the top centre of the knob, and
	// the top centre of a knob turning about the vertical is the one point on it that does not move.
	// A test that kept it would have gone green over a knob welded solid.
	const FHFMeshPart& First = Built.Parts[0];
	const FAxisAlignedBox3d KnobBox = First.Mesh.GetBounds();
	const FVector Flag(KnobBox.Center().X, KnobBox.Min.Y, KnobBox.Max.Z);

	const double Swept = (PosedPoint(First, Flag, 1.0) - PosedPoint(First, Flag, 0.0)).Size();
	AddInfo(FString::Printf(TEXT("The knob's pointer sweeps %.2f cm."), Swept));
	TestTrue(TEXT("The turn is visible on the pointer"), Swept > 0.5);

	// Degenerate input produces nothing rather than garbage.
	FHFHobParams Nothing;
	Nothing.Width = 0.0;
	TestFalse(TEXT("A hob with no width builds nothing"), FHFApplianceKit::BuildHob(Nothing).bValid);

	return true;
}

// ------------------------------------------------------------------------------------- chimney

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFChimneyTest, "HouseForge.Kitchen.ChimneyReachesTheSoffit",
	HF_TEST_FLAGS)

bool FHFChimneyTest::RunTest(const FString& Parameters)
{
	const FHFChimneyParams P = MakeChimney();
	const FHFApplianceBuild Built = FHFApplianceKit::BuildChimney(P);

	TestTrue(TEXT("A chimney builds"), Built.bValid);
	TestTrue(TEXT("Every triangle carries a surface role"), EveryTriangleHasARole(Built.Shell));

	const FAxisAlignedBox3d Bounds = Built.Shell.GetBounds();

	TestNearlyEqual(TEXT("The canopy is the drawn width"), Bounds.Max.X - Bounds.Min.X, 60.0, 0.01);

	// THE DUCT IS NOT DECORATION. The whole point of resolving its length in the composing layer is
	// that the flue reaches the finished ceiling, so the built height must be the canopy PLUS the
	// duct it was handed - a chimney that stops at the top of its own canopy is a box on a wall.
	TestNearlyEqual(TEXT("It stands the canopy plus its duct"),
		Bounds.Max.Z - Bounds.Min.Z, P.BuiltHeight(), 0.01);

	// And a chimney whose canopy already reaches the ceiling gets no stub of duct.
	FHFChimneyParams NoDuct = P;
	NoDuct.DuctLength = 0.0;
	const FHFApplianceBuild Short = FHFApplianceKit::BuildChimney(NoDuct);
	TestNearlyEqual(TEXT("No duct means no duct"),
		Short.Shell.GetBounds().Max.Z, P.CanopyHeight, 0.01);
	TestTrue(TEXT("Parameters genuinely change the output"),
		Volume(Short.Shell) < Volume(Built.Shell) - 1.0);

	// ------------------------------------------------------------------------------ the filter

	const FHFMeshPart* Filter = Built.Parts.FindByPredicate(
		[](const FHFMeshPart& Part) { return Part.PartId == FHFApplianceKit::FilterPartId(); });

	if (!TestNotNull(TEXT("The chimney has a baffle filter"), Filter))
	{
		return false;
	}

	TestEqual(TEXT("It hinges"), Filter->Motion.Type, EHFMotionType::Hinge);

	// IT DROPS OUT OF THE HOOD, NOT INTO IT. The direction is the whole content of the motion: a
	// filter whose top tips backwards travels exactly as far, satisfies every assertion about
	// movement, and goes through the wall behind the canopy instead of into the hand reaching for it.
	const FAxisAlignedBox3d FilterBox = Filter->Mesh.GetBounds();
	const FVector TopEdge(FilterBox.Center().X, FilterBox.Center().Y, FilterBox.Max.Z);

	const FVector Was = PosedPoint(*Filter, TopEdge, 0.0);
	const FVector Now = PosedPoint(*Filter, TopEdge, 1.0);

	AddInfo(FString::Printf(TEXT("The filter's top edge moves (%.1f, %.1f, %.1f) cm."),
		Now.X - Was.X, Now.Y - Was.Y, Now.Z - Was.Z));

	TestTrue(TEXT("The filter's top swings forward, out of the hood"), Now.Y < Was.Y - 5.0);
	TestTrue(TEXT("And drops"), Now.Z < Was.Z - 5.0);

	return true;
}

#undef HF_TEST_FLAGS

#endif // WITH_DEV_AUTOMATION_TESTS
