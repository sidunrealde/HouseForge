// Copyright Siddartha G. All Rights Reserved.

#include "HouseForge.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Actors/HFLightFixtureActor.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Geometry/HFGenerators.h"
#include "Geometry/HFLuminaireKit.h"
#include "Geometry/HFMeshOps.h"
#include "Misc/AutomationTest.h"
#include "Model/HFCeilingTemplates.h"
#include "Model/HFTypes.h"

#define HF_TEST_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The lighting a generator can be held to, with no world and no actors anywhere near it.
 *
 * Everything here is a pure function of a spec: does a cove produce a strip to see, and does a
 * luminaire produce a lens to put a lamp behind. What the LEVEL does with those - spawning
 * components, counting them, aiming them - is HouseForgeEditor's HFLightingActorTests, because it
 * needs a world.
 */
namespace HouseForgeLighting
{
	using namespace UE::Geometry;

	/** How many triangles of a mesh carry a given surface role. */
	int32 TrianglesWithRole(const FDynamicMesh3& Mesh, EHFSurfaceRole Role)
	{
		int32 Count = 0;
		for (const int32 Tid : Mesh.TriangleIndicesItr())
		{
			if (FHFMeshOps::RoleForGroup(Mesh.GetTriangleGroup(Tid)) == Role)
			{
				++Count;
			}
		}
		return Count;
	}

	/** Bounds of just the triangles carrying a role. Empty when the role is not present. */
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
			Box.Contain(Mesh.GetVertex(Tri.A));
			Box.Contain(Mesh.GetVertex(Tri.B));
			Box.Contain(Mesh.GetVertex(Tri.C));
		}
		return Box;
	}

	/** A plain rectangular room, in centimetres, with a ceiling to hang things from. */
	FHFRoom Room(double SizeX = 400.0, double SizeY = 350.0)
	{
		FHFRoom R;
		R.Id = TEXT("R1");
		R.Type = EHFRoomType::Bedroom;
		R.FloorZ = 0.0;
		R.CeilingHeight = 300.0;
		R.Boundary = { FVector2D(0, 0), FVector2D(SizeX, 0), FVector2D(SizeX, SizeY), FVector2D(0, SizeY) };
		return R;
	}

	/** A cove ceiling over that room, with its figures filled in by the template resolver. */
	FHFFalseCeiling CoveCeiling(const FHFRoom& R)
	{
		FHFFalseCeiling C;
		C.Id = TEXT("FC1");
		C.RoomId = R.Id;
		C.Template = EHFCeilingTemplate::Cove;

		// Through the template resolver rather than by hand, because that is how a spec gets its
		// figures - FHFCeilingTemplates::Apply stamps them once, before anything validates or
		// builds - and a cove assembled from hand-picked numbers here would be testing numbers this
		// plugin never actually generates.
		FHFHouseSpec Spec;
		Spec.Units = EHFUnits::Centimeters;
		Spec.Rooms.Add(R);
		Spec.FalseCeilings.Add(C);
		FHFCeilingTemplates::Apply(Spec, FHFCeilingDefaults());

		return Spec.FalseCeilings[0];
	}
}

// ---------------------------------------------------------------------------------------------
//
// A COVE THAT DOES NOT GLOW IS NOT A COVE.
//
// The characteristic detail of every one of these ceilings, and for a whole milestone it generated
// as a painted line at a step: the strip was tagged MetalHardware, the lens Glass, and nothing
// anywhere emitted. EHFSurfaceRole::LightSource exists because of that, and this is the assertion
// that the role actually lands on geometry rather than merely existing in the enum.
//
// Two halves, and both are needed. The STRIP is what a still shows; the RUNS are what a light gets
// parented to. A cove with a strip and no runs is a bright line and no wash; a cove with runs and no
// strip is a wash coming out of nothing.
//
// ---------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFCoveEmissiveStripTest,
	"HouseForge.Lighting.ACoveProducesAnEmissiveStrip", HF_TEST_FLAGS)

bool FHFCoveEmissiveStripTest::RunTest(const FString& Parameters)
{
	using namespace HouseForgeLighting;

	const FHFRoom R = Room();
	const FHFFalseCeiling Cove = CoveCeiling(R);

	if (!TestEqual(TEXT("The template resolved to a cove"), Cove.Style, EHFCeilingStyle::Cove))
	{
		return false;
	}
	if (!TestTrue(TEXT("...with an LED strip in it"), Cove.Cove.bHasLedStrip))
	{
		return false;
	}

	// ------------------------------------------------------------------------- the strip itself
	//
	// WITH THE DOWNLIGHTS SWITCHED OFF, so that the only emissive geometry in the mesh is the cove
	// strip. A cove band also carries a run of recessed downlights, and their lenses are LightSource
	// too - measured, the two together span 4.4 cm in section, which is the strip plus the depth the
	// apertures sit up their cans. Bounding both and calling the result a strip would have been a
	// shape assertion about two objects.
	FHFFalseCeiling StripOnly = Cove;
	StripOnly.Downlight.bRecessed = false;

	const FDynamicMesh3 Mesh = FHFGenerators::GenerateCeiling(StripOnly, R, {}, 0.0);

	const int32 Emitting = TrianglesWithRole(Mesh, EHFSurfaceRole::LightSource);
	AddInfo(FString::Printf(TEXT("The cove strip is %d triangles out of %d in the ceiling."),
		Emitting, Mesh.TriangleCount()));

	if (!TestTrue(TEXT("A cove ceiling carries emissive geometry"), Emitting > 0))
	{
		return false;
	}

	// IT IS A STRIP, NOT A SLAB. Without this the assertion above would be satisfied by tagging the
	// whole soffit LightSource, which would light the room from its entire ceiling and look nothing
	// like a cove. The strip lies in the channel, so it is thin in section and long in plan.
	const FAxisAlignedBox3d Strip = BoundsOfRole(Mesh, EHFSurfaceRole::LightSource);

	TestTrue(FString::Printf(TEXT("The strip is thin in section: %.2f cm tall against a %.2f profile"),
		Strip.Depth(), Cove.Cove.StripHeight),
		Strip.Depth() > 0.0 && Strip.Depth() <= Cove.Cove.StripHeight + 0.01);

	TestTrue(TEXT("...and runs the length of the room, not a patch of it"),
		Strip.Width() > R.Boundary[1].X * 0.5);

	// AND IT IS UP IN THE TROUGH, above the soffit somebody standing in the room can see. A strip
	// hanging below the lip is a strip in plain view, which is the one thing a cove detail exists
	// to avoid.
	const double SoffitZ = R.FloorZ + R.CeilingHeight - Cove.Drop;
	TestTrue(FString::Printf(TEXT("The strip sits above the band soffit at %.1f (strip base %.1f)"),
		SoffitZ, Strip.Min.Z), Strip.Min.Z > SoffitZ);

	// The two emitting things in a cove band are separate, and the full ceiling has both. Worth one
	// line, because it is what justifies having had to switch the downlights off above.
	const FDynamicMesh3 WithDownlights = FHFGenerators::GenerateCeiling(Cove, R, {}, 0.0);
	TestTrue(TEXT("A cove band also emits from its downlight lenses, over and above the strip"),
		TrianglesWithRole(WithDownlights, EHFSurfaceRole::LightSource) > Emitting);

	// ------------------------------------------------------------------------------- the runs
	const TArray<FHFCoveLightRun> Runs = FHFGenerators::CeilingCoveLights(Cove, R);

	AddInfo(FString::Printf(TEXT("%d cove light runs."), Runs.Num()));

	if (!TestTrue(TEXT("A cove produces light runs for something to be parented to"), Runs.Num() > 0))
	{
		return false;
	}

	double TotalLength = 0.0;
	for (const FHFCoveLightRun& Run : Runs)
	{
		TestTrue(TEXT("Every run has a length"), Run.Length > 0.0);
		TestTrue(TEXT("Every run has an aperture to throw through"), Run.Width > 0.0);

		// The whole point of a cove: the light goes UP, at the surface above it. A run with no
		// throw height would be a light with nothing to wash.
		TestTrue(TEXT("Every run has something above it to wash"), Run.ThrowHeight > 0.0);

		TotalLength += Run.Length;
	}

	// Four straight runs round a rectangular room. Compared against the room's own perimeter rather
	// than against a recorded number, so a change to the room size cannot make this pass vacuously.
	const double Perimeter = 2.0 * (R.Boundary[1].X + R.Boundary[2].Y);
	TestTrue(FString::Printf(TEXT("The runs go round the room: %.0f cm against a %.0f cm perimeter"),
		TotalLength, Perimeter), TotalLength > Perimeter * 0.5);

	// ------------------------------------------------------- and a ceiling that is not a cove has none
	//
	// The control. Without it every assertion above is satisfied by a generator that tags something
	// LightSource unconditionally.
	//
	// Stripped of its downlights as well as of its cove, because a plain band legitimately DOES emit
	// from its aperture lenses - that is not the cove leaking through, it is the other lighting
	// detail doing its job, and leaving it in would make this control say only that two things emit.
	FHFFalseCeiling Plain = Cove;
	Plain.Style = EHFCeilingStyle::FullDrop;
	Plain.Template = EHFCeilingTemplate::Custom;
	Plain.Downlight.bRecessed = false;
	Plain.LightPositions.Reset();

	const FDynamicMesh3 PlainMesh = FHFGenerators::GenerateCeiling(Plain, R, {}, 0.0);

	TestEqual(TEXT("A flat ceiling with no cove and no downlights emits nothing at all"),
		TrianglesWithRole(PlainMesh, EHFSurfaceRole::LightSource), 0);

	// AND THE COVE IS WHAT MAKES THE RUNS, not the band. This one keeps its downlights, so the only
	// difference from the ceiling under test is the style itself.
	FHFFalseCeiling NotACove = Cove;
	NotACove.Style = EHFCeilingStyle::FullDrop;

	TestEqual(TEXT("...and a ceiling that is not a cove produces no cove runs"),
		FHFGenerators::CeilingCoveLights(NotACove, R).Num(), 0);

	return true;
}

// ---------------------------------------------------------------------------------------------
//
// A LIGHT FITTING HAS SOMETHING TO SEE AND SOMEWHERE TO PUT THE LAMP.
//
// EHFFixtureType::LightFixture built nothing at all before this milestone, so there is no previous
// behaviour to protect - what this pins down is the contract AHFLightFixtureActor depends on:
// LensCentre is inside the fitting it describes, and a drop actually moves it.
//
// ---------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFLuminaireLensTest,
	"HouseForge.Lighting.ALuminaireHasALensToPutALampBehind", HF_TEST_FLAGS)

bool FHFLuminaireLensTest::RunTest(const FString& Parameters)
{
	using namespace HouseForgeLighting;

	// ---------------------------------------------------------------- a panel screwed to the soffit
	FHFLuminaireParams Panel;
	Panel.Diameter = 24.0;
	Panel.BodyHeight = 5.0;
	Panel.DropLength = 0.0;

	const FDynamicMesh3 PanelMesh = FHFLuminaireKit::Build(Panel);

	if (!TestTrue(TEXT("A luminaire builds geometry"), PanelMesh.TriangleCount() > 0))
	{
		return false;
	}

	const int32 Emitting = TrianglesWithRole(PanelMesh, EHFSurfaceRole::LightSource);
	TestTrue(TEXT("It has an emitting lens"), Emitting > 0);

	// AND IT IS A LENS, NOT THE WHOLE FITTING. The bezel around it is what makes a fitting read as
	// an object rather than as a glowing disc, and tagging everything LightSource would pass the
	// assertion above.
	TestTrue(FString::Printf(TEXT("The lens is part of the fitting, not all of it: %d of %d triangles"),
		Emitting, PanelMesh.TriangleCount()), Emitting < PanelMesh.TriangleCount());

	// ------------------------------------------------------- the lamp position is inside the fitting
	//
	// This is the figure AHFLightFixtureActor puts a real light at, so it is the one worth pinning.
	// Reasoned about in the fitting's own frame: the origin is the mounting point and +Z goes into
	// the room, so the lens is at the far end and everything is at Z >= 0.
	const FVector3d Lens = FHFLuminaireKit::LensCentre(Panel);

	TestTrue(TEXT("The lamp is on the axis"),
		FMath::IsNearlyZero(Lens.X, 1e-6) && FMath::IsNearlyZero(Lens.Y, 1e-6));

	TestTrue(FString::Printf(TEXT("The lamp is at the room-facing end, past the body rim at %.2f"),
		Panel.RimZ()), Lens.Z >= Panel.RimZ());

	const FAxisAlignedBox3d PanelBox = PanelMesh.GetBounds();
	TestTrue(FString::Printf(TEXT("...and no deeper than the fitting itself, which reaches %.2f"),
		PanelBox.Max.Z), Lens.Z <= PanelBox.Max.Z + 0.01);

	// The fitting hangs off the surface it is screwed to and does not go up into it. A luminaire
	// modelled at negative Z would be built inside the plasterboard.
	TestTrue(FString::Printf(TEXT("Nothing is above the mounting surface: lowest point %.2f"),
		PanelBox.Min.Z), PanelBox.Min.Z >= -0.01);

	const double LensRadius = FHFLuminaireKit::LensRadius(Panel);
	TestTrue(TEXT("The lens has a radius for a source size to be taken from"), LensRadius > 0.0);
	TestTrue(TEXT("...and it is narrower than the fitting, leaving a bezel"),
		LensRadius < Panel.Diameter * 0.5);

	// ---------------------------------------------------------------------------- and a pendant
	//
	// The one thing that separates the two fittings this kit builds. A drop has to move the lamp
	// down by the drop, not merely make the mesh bigger - a pendant whose lamp stayed at the ceiling
	// would light the ceiling.
	FHFLuminaireParams Pendant = Panel;
	Pendant.DropLength = 60.0;

	const FVector3d PendantLens = FHFLuminaireKit::LensCentre(Pendant);

	TestEqual(TEXT("A drop moves the lamp down by exactly the drop"),
		PendantLens.Z - Lens.Z, Pendant.DropLength, 1e-6);

	const FDynamicMesh3 PendantMesh = FHFLuminaireKit::Build(Pendant);

	TestTrue(TEXT("A pendant is more geometry than a panel: it has a canopy and a flex"),
		PendantMesh.TriangleCount() > PanelMesh.TriangleCount());

	TestTrue(TEXT("A pendant still has a lens"),
		TrianglesWithRole(PendantMesh, EHFSurfaceRole::LightSource) > 0);

	// The canopy covers the outlet box at the ceiling, so a pendant reaches the mounting surface
	// even though its body does not.
	TestTrue(FString::Printf(TEXT("The canopy is at the ceiling: pendant starts at %.2f"),
		PendantMesh.GetBounds().Min.Z), PendantMesh.GetBounds().Min.Z <= 0.01);

	// -------------------------------------------------------------------------- and it survives zero
	//
	// A spec that leaves every figure at its default zero must produce a fitting rather than a
	// degenerate revolve. Sanitise is what guarantees that, and this is the check on it.
	FHFLuminaireParams Empty;
	Empty.Diameter = 0.0;
	Empty.BodyHeight = 0.0;
	Empty.LensProud = 0.0;
	Empty.LensFraction = 0.0;

	const FDynamicMesh3 EmptyMesh = FHFLuminaireKit::Build(Empty);

	TestTrue(TEXT("A fitting with every figure at zero still builds something"),
		EmptyMesh.TriangleCount() > 0);
	TestTrue(TEXT("...with a lens on it"),
		TrianglesWithRole(EmptyMesh, EHFSurfaceRole::LightSource) > 0);

	// The proudness floor is not decoration: at zero the diffuser degenerates into a disc lying in
	// the plane of the rim, which is the coplanar pair the whole construction avoids.
	const FHFLuminaireParams Sane = FHFLuminaireKit::Sanitise(Empty);
	TestTrue(TEXT("The lens is still held proud of the rim"), Sane.LensProud > 0.0);
	TestTrue(TEXT("...and still narrower than it"), Sane.LensFraction < 1.0);

	return true;
}

// ---------------------------------------------------------------------------------------------
//
// THE LAMP IN A FITTING IS A REAL LAMP.
//
// The brief's own condition: intensities plausible for a domestic interior, in lumens, not arbitrary
// numbers. Asserted against the fitting the figure is chosen for rather than as a pair of literals,
// so that changing what a fitting is changes what it is asked to be.
//
// ---------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFLuminaireLumensTest,
	"HouseForge.Lighting.LuminaireOutputIsADomesticLamp", HF_TEST_FLAGS)

bool FHFLuminaireLumensTest::RunTest(const FString& Parameters)
{
	FHFLuminaireParams Panel;
	Panel.DropLength = 0.0;

	FHFLuminaireParams Pendant = Panel;
	Pendant.DropLength = 60.0;

	const double PanelLumens = AHFLightFixtureActor::DefaultLumensFor(Panel);
	const double PendantLumens = AHFLightFixtureActor::DefaultLumensFor(Pendant);

	AddInfo(FString::Printf(TEXT("Ceiling panel %.0f lm, pendant %.0f lm."), PanelLumens, PendantLumens));

	// The band a domestic lamp lives in. A 5 W night light is about 400 lm at the bottom; the
	// largest single fitting anybody puts in a room of this size is a 30 W panel at about 3000. A
	// figure outside this is not a lamp, and the point of the assertion is to catch a watt/lumen
	// mix-up or a stray zero rather than to pin a preference.
	TestTrue(TEXT("A ceiling panel is a real domestic lamp"),
		PanelLumens >= 400.0 && PanelLumens <= 3000.0);
	TestTrue(TEXT("A pendant is a real domestic lamp"),
		PendantLumens >= 400.0 && PendantLumens <= 3000.0);

	// AND THE ORDER IS RIGHT. A pendant carries one lamp; a panel is a whole array of LEDs. A
	// pendant brighter than a ceiling panel would mean somebody had swapped the two figures, which
	// both range checks above would happily accept.
	TestTrue(TEXT("A ceiling panel is brighter than a single-lamp pendant"),
		PanelLumens > PendantLumens);

	// ------------------------------------------------------------ and what a drawing asks for is read
	//
	// ParamsFor is the whole of the panel-or-pendant decision, so it is worth showing that a drawing
	// can actually express both. A light marked 8 cm deep is screwed to the soffit; one marked 70 is
	// hanging on 60-odd centimetres of flex.
	FHFFixture Flush;
	Flush.Type = EHFFixtureType::LightFixture;
	Flush.Footprint = FVector2D(24.0, 24.0);
	Flush.Height = 8.0;

	FHFFixture Hung = Flush;
	Hung.Height = 70.0;

	const FHFLuminaireParams FlushParams = AHFLightFixtureActor::ParamsFor(Flush);
	const FHFLuminaireParams HungParams = AHFLightFixtureActor::ParamsFor(Hung);

	TestFalse(TEXT("A shallow light fixture is a panel on the soffit"), FlushParams.HasDrop());
	TestTrue(TEXT("A deep one is a pendant"), HungParams.HasDrop());

	// The drop is the excess over the fitting, so the whole thing occupies the depth that was drawn.
	TestEqual(TEXT("A pendant reaches the depth the drawing marked"),
		HungParams.RimZ(), Hung.Height, 0.5);

	// And the diameter comes off the narrow side of the drawn box, so a fitting inscribed in an
	// oblong box does not come out as wide as the box.
	FHFFixture Oblong = Flush;
	Oblong.Footprint = FVector2D(40.0, 18.0);

	TestEqual(TEXT("The diameter is the narrow side of the drawn box"),
		AHFLightFixtureActor::ParamsFor(Oblong).Diameter, 18.0, 1e-6);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
