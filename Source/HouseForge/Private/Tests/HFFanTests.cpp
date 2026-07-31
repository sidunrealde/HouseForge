// Copyright Siddartha G. All Rights Reserved.

#include "HouseForge.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "DynamicMesh/DynamicMesh3.h"
#include "Geometry/HFFanKit.h"
#include "Geometry/HFMeshOps.h"
#include "MeshQueries.h"
#include "Misc/AutomationTest.h"
#include "Model/HFBuildDefaults.h"

using namespace UE::Geometry;

#define HF_TEST_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
	double VolumeOf(const FDynamicMesh3& Mesh)
	{
		return TMeshQueries<FDynamicMesh3>::GetVolumeArea(Mesh).X;
	}
}

/**
 * A ceiling fan is a fixed shell and one part that revolves.
 *
 * The assertion that would have failed for the whole of milestone 8, when EHFMotionType::Spin was
 * complete and nothing in the plugin produced one. Measured on the part's declared motion rather
 * than on a triangle count: a fan whose blades were merged into the canopy would have exactly the
 * same geometry and could never turn.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFFanSpinsTest, "HouseForge.Fan.CeilingFanHasARotorThatSpins", HF_TEST_FLAGS)

bool FHFFanSpinsTest::RunTest(const FString& Parameters)
{
	FHFFanParams P = FHFFanKit::DefaultsFor(EHFFanKind::Ceiling);
	P.SweepDiameter = 120.0;

	const FHFFanBuild Built = FHFFanKit::Build(P);

	if (!TestTrue(TEXT("A standard ceiling fan builds"), Built.bValid)
		|| !TestEqual(TEXT("It has exactly one moving part"), Built.Parts.Num(), 1))
	{
		return false;
	}

	const FHFMeshPart& Rotor = Built.Parts[0];

	TestEqual(TEXT("The part is the rotor"), Rotor.PartId, FHFFanKit::RotorPartId);
	TestTrue(TEXT("It revolves rather than opens"), Rotor.Motion.Revolves());
	TestFalse(TEXT("...so it is not an opening"), Rotor.Motion.Opens());
	TestTrue(TEXT("It has a speed"), FMath::Abs(Rotor.Motion.RevolutionsPerMinute) > 0.0);
	TestTrue(TEXT("It turns about its own axis"),
		Rotor.Motion.UnitAxis().Equals(FVector::ZAxisVector, 1e-9));

	// Both meshes are solids. A hole anywhere in either silently defeats every boolean the fan is
	// ever handed to, and shows up as a black facet the moment anything is lit.
	TestTrue(TEXT("The fixed shell is watertight"), FHFMeshOps::IsClosed(Built.Shell));
	TestTrue(TEXT("The rotor is watertight"), FHFMeshOps::IsClosed(Rotor.Mesh));
	TestTrue(TEXT("The shell is a solid, not an inside-out one"), VolumeOf(Built.Shell) > 0.0);
	TestTrue(TEXT("The rotor is a solid too"), VolumeOf(Rotor.Mesh) > 0.0);

	// THE BLADES ARE ON THE ROTOR, not on the shell. Measured by reach: the rotor spans the sweep
	// and the shell is a rod and a canopy, so if the two were the wrong way round - or if everything
	// had been merged - the radii would say so.
	const FAxisAlignedBox3d RotorBounds = Rotor.Mesh.GetBounds();
	const FAxisAlignedBox3d ShellBounds = Built.Shell.GetBounds();

	TestTrue(*FString::Printf(TEXT("The rotor reaches the full sweep (%.2f against a declared %.2f)"),
		RotorBounds.Width(), P.SweepDiameter),
		FMath::IsNearlyEqual(RotorBounds.Width(), P.SweepDiameter, 0.5));
	TestTrue(*FString::Printf(TEXT("The fixed shell is only the rod and canopy (%.2f wide)"),
		ShellBounds.Width()),
		ShellBounds.Width() < P.SweepDiameter * 0.25);

	// A fan hangs BELOW what it is fixed to, in its own frame: local +Z is the spin axis pointing
	// away from the mounting surface, so everything is at Z >= 0 and the caller aims it.
	TestTrue(TEXT("Nothing sits behind the ceiling it is fixed to"), ShellBounds.Min.Z > -1e-6);
	TestTrue(TEXT("The blades hang below the canopy"), Built.BladePlaneZ > P.DropLength);

	return true;
}

/**
 * The blades are pitched, and the pitch is what a fan blade IS.
 *
 * A flat blade builds perfectly and is instantly readable as wrong under any light: it catches as a
 * uniform strip where a real blade has a bright edge and a dark one. Asserted on the rotor's own
 * depth, because that is the only thing a pitch changes - a flat rotor is as deep as its housing and
 * a pitched one is deeper by the blade's chord swept through the angle.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFFanBladePitchTest, "HouseForge.Fan.BladesArePitched", HF_TEST_FLAGS)

bool FHFFanBladePitchTest::RunTest(const FString& Parameters)
{
	auto RotorDepthAtPitch = [this](double Degrees) -> double
	{
		FHFFanParams P = FHFFanKit::DefaultsFor(EHFFanKind::Ceiling);
		P.BladePitchDegrees = Degrees;

		const FHFFanBuild Built = FHFFanKit::Build(P);
		return Built.Parts.Num() == 1 ? Built.Parts[0].Mesh.GetBounds().Height() : -1.0;
	};

	const double Flat = RotorDepthAtPitch(0.0);
	const double Pitched = RotorDepthAtPitch(12.0);
	const double Steep = RotorDepthAtPitch(30.0);

	if (!TestTrue(TEXT("All three rotors build"), Flat > 0.0 && Pitched > 0.0 && Steep > 0.0))
	{
		return false;
	}

	// A pitched blade stands taller than a flat one, and a steeper one taller still. Monotone rather
	// than a fixed figure, because the number depends on chord and housing height and asserting one
	// would be asserting the current dimensions rather than the behaviour.
	TestTrue(*FString::Printf(TEXT("Pitching the blades deepens the rotor (%.3f flat, %.3f at 12 degrees)"),
		Flat, Pitched), Pitched > Flat + 0.5);
	TestTrue(*FString::Printf(TEXT("...and a steeper pitch deepens it further (%.3f at 30 degrees)"), Steep),
		Steep > Pitched + 0.5);

	// The default is never flat. A generator that shipped a zero pitch would pass every geometric
	// check there is and put a paper cut-out in every render.
	TestTrue(TEXT("The kit's own default is a pitched blade"),
		FHFFanKit::DefaultsFor(EHFFanKind::Ceiling).BladePitchDegrees > 0.0);
	TestTrue(TEXT("...and so is the extract's"),
		FHFFanKit::DefaultsFor(EHFFanKind::Exhaust).BladePitchDegrees > 0.0);

	return true;
}

/**
 * More blades is more rotor, and the count reaches the geometry.
 *
 * A blade count faithfully copied onto the params and then ignored by the generator is the exact
 * failure mode this plugin keeps finding, so it is measured on the mesh: volume, which only a real
 * extra blade can add.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFFanBladeCountTest, "HouseForge.Fan.BladeCountReachesTheMesh", HF_TEST_FLAGS)

bool FHFFanBladeCountTest::RunTest(const FString& Parameters)
{
	auto RotorVolumeAt = [](int32 Blades) -> double
	{
		FHFFanParams P = FHFFanKit::DefaultsFor(EHFFanKind::Ceiling);
		P.BladeCount = Blades;

		const FHFFanBuild Built = FHFFanKit::Build(P);
		return Built.Parts.Num() == 1 ? VolumeOf(Built.Parts[0].Mesh) : -1.0;
	};

	const double Three = RotorVolumeAt(3);
	const double Four = RotorVolumeAt(4);

	if (!TestTrue(TEXT("Both rotors build"), Three > 0.0 && Four > 0.0))
	{
		return false;
	}

	TestTrue(*FString::Printf(TEXT("A fourth blade is more material (%.1f against %.1f)"), Four, Three),
		Four > Three * 1.05);

	// One blade is not a fan, and neither is fifty. Clamped rather than refused, because a drawing
	// that gave a nonsense count still described a fan somewhere.
	TestEqual(TEXT("A one-blade fan is clamped to something that could turn"),
		FHFFanKit::Sanitise([]{ FHFFanParams P; P.BladeCount = 1; return P; }()).BladeCount, 2);

	return true;
}

/**
 * An extract is a different object, not a small ceiling fan.
 *
 * It sits IN a wall rather than hanging off a slab, so its case has an aperture through it and the
 * rotor lives inside that aperture. The aperture is what this measures: without it the blades turn
 * inside a solid block, which looks entirely correct in a still of the wall.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFExhaustFanTest, "HouseForge.Fan.ExtractHasAnApertureToBlowThrough", HF_TEST_FLAGS)

bool FHFExhaustFanTest::RunTest(const FString& Parameters)
{
	FHFFanParams P = FHFFanKit::DefaultsFor(EHFFanKind::Exhaust);
	P.SweepDiameter = 18.75;
	P.CaseDepth = 10.0;

	const FHFFanBuild Built = FHFFanKit::Build(P);

	if (!TestTrue(TEXT("An extract builds"), Built.bValid)
		|| !TestEqual(TEXT("It has one moving part"), Built.Parts.Num(), 1))
	{
		return false;
	}

	TestTrue(TEXT("Its rotor revolves"), Built.Parts[0].Motion.Revolves());
	TestTrue(TEXT("An extract turns far faster than a ceiling fan"),
		FMath::Abs(Built.Parts[0].Motion.RevolutionsPerMinute)
			> FMath::Abs(FHFFanKit::DefaultsFor(EHFFanKind::Ceiling).RevolutionsPerMinute) * 2.0);

	TestTrue(TEXT("The case is watertight after the aperture is cut"), FHFMeshOps::IsClosed(Built.Shell));

	// The aperture, measured. A solid case of these dimensions would have the full box volume; the
	// throat takes a cylinder out of it, and the difference is what the fan blows through.
	const FAxisAlignedBox3d Case = Built.Shell.GetBounds();
	const double Solid = Case.Width() * Case.Depth() * Case.Height();
	const double Actual = VolumeOf(Built.Shell);

	TestTrue(*FString::Printf(TEXT("The case has a hole through it (%.1f of a solid %.1f)"), Actual, Solid),
		Actual < Solid * 0.8);
	TestTrue(TEXT("...and is still a case rather than a ring of nothing"), Actual > 0.0);

	// A wall fan stands proud of what it is fixed to, in the same frame a ceiling fan hangs below it.
	TestTrue(TEXT("Nothing sits behind the wall face"), Case.Min.Z > -1e-6);
	TestTrue(TEXT("The rotor sits inside the case, not in front of it"),
		Built.BladePlaneZ > 0.0 && Built.BladePlaneZ < P.CaseDepth);

	return true;
}

/**
 * Every triangle a fan emits carries a surface role, and the roles survive being joined.
 *
 * The blades are built separately and merged, and the extract's aperture is a boolean, so both of
 * the operations that silently destroy roles are in this generator's path.
 * FHFMeshOps::AppendPreservingRoles and SubtractInPlace exist for exactly that, and a fan whose
 * roles were renumbered would look perfect in every screenshot and could never be re-materialled.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFFanRolesTest, "HouseForge.Fan.RolesSurviveComposition", HF_TEST_FLAGS)

bool FHFFanRolesTest::RunTest(const FString& Parameters)
{
	for (const EHFFanKind Kind : { EHFFanKind::Ceiling, EHFFanKind::Exhaust })
	{
		const FHFFanBuild Built = FHFFanKit::Build(FHFFanKit::DefaultsFor(Kind));
		const TCHAR* Which = Kind == EHFFanKind::Ceiling ? TEXT("ceiling fan") : TEXT("extract");

		if (!TestTrue(*FString::Printf(TEXT("The %s builds"), Which), Built.bValid))
		{
			continue;
		}

		const TSet<EHFSurfaceRole> ShellRoles = FHFMeshOps::RolesPresent(Built.Shell);
		const TSet<EHFSurfaceRole> RotorRoles = FHFMeshOps::RolesPresent(Built.Parts[0].Mesh);

		TestTrue(*FString::Printf(TEXT("The %s's shell is tagged"), Which), ShellRoles.Num() > 0);
		TestTrue(*FString::Printf(TEXT("The %s's rotor is tagged"), Which), RotorRoles.Num() > 0);

		// The blades are the merged part, and they are metal. If AppendPreservingRoles had been
		// skipped for the raw append, they would come back as WallPaint - the fallback RoleForGroup
		// returns for a group no role maps to.
		TestTrue(*FString::Printf(TEXT("The %s's blades are still metal after the merge"), Which),
			RotorRoles.Contains(EHFSurfaceRole::MetalHardware));

		// Every triangle carries one, not just most of them. A stray untagged group is a face the
		// material panel can never reach.
		TestFalse(*FString::Printf(TEXT("Nothing on the %s's rotor fell back to wall paint"), Which),
			RotorRoles.Contains(EHFSurfaceRole::WallPaint));
	}

	return true;
}

/**
 * A fan takes its speed and its blades from the project, and its sweep from the drawing.
 *
 * The split that makes a settings page mean something: a FIGURE is how this project builds fans and
 * a DIMENSION is what this particular fan measures. ApplyTo stamping a sweep over the drawing's own
 * would quietly make every fan in the flat the same size whatever the plan said.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFFanSettingsTest, "HouseForge.Fan.ProjectFiguresReachTheFan", HF_TEST_FLAGS)

bool FHFFanSettingsTest::RunTest(const FString& Parameters)
{
	FHFFanDefaults Project;
	Project.CeilingFanRpm = 210.0;
	Project.ExhaustFanRpm = 900.0;
	Project.CeilingFanBladeCount = 4;
	Project.ExhaustFanBladeCount = 6;
	Project.BladePitchDegrees = 20.0;
	Project.CeilingFanDropLength = 55.0;

	FHFFanParams Ceiling = FHFFanKit::DefaultsFor(EHFFanKind::Ceiling);
	Ceiling.SweepDiameter = 90.0;
	Project.ApplyTo(Ceiling);

	TestEqual(TEXT("The ceiling fan takes the project's speed"), Ceiling.RevolutionsPerMinute, 210.0);
	TestEqual(TEXT("...its blade count"), Ceiling.BladeCount, 4);
	TestEqual(TEXT("...its pitch"), Ceiling.BladePitchDegrees, 20.0);
	TestEqual(TEXT("...and its rod length"), Ceiling.DropLength, 55.0);
	TestEqual(TEXT("But the drawing's sweep is left alone"), Ceiling.SweepDiameter, 90.0);

	FHFFanParams Exhaust = FHFFanKit::DefaultsFor(EHFFanKind::Exhaust);
	Exhaust.CaseDepth = 9.0;
	Project.ApplyTo(Exhaust);

	TestEqual(TEXT("The extract takes its own speed, not the ceiling fan's"),
		Exhaust.RevolutionsPerMinute, 900.0);
	TestEqual(TEXT("...and its own blade count"), Exhaust.BladeCount, 6);
	TestEqual(TEXT("A rod length means nothing on an extract, so its case depth stands"),
		Exhaust.CaseDepth, 9.0);

	// And the speed reaches the part, which is the only place it can do any work.
	const FHFFanBuild Built = FHFFanKit::Build(Ceiling);
	if (TestEqual(TEXT("The fan builds one part"), Built.Parts.Num(), 1))
	{
		TestEqual(TEXT("The project's speed is on the motion the animator drives"),
			Built.Parts[0].Motion.RevolutionsPerMinute, 210.0);
		TestNearlyEqual(TEXT("A minute at 210 rpm is 210 revolutions"),
			Built.Parts[0].Motion.TurnsInSeconds(60.0), 210.0, 1e-9);
	}

	return true;
}

#undef HF_TEST_FLAGS

#endif // WITH_DEV_AUTOMATION_TESTS
