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

	/**
	 * How far a mesh reaches from the spin axis, which for a fan is the only measurement of "how big"
	 * that means anything.
	 *
	 * NOT a bounding box, deliberately. An odd blade count is not symmetric about the axis - three
	 * tips at 0, 120 and 240 degrees put the box 95 wide on a 120 sweep - so no bounding box on a
	 * three-blade fan can ever equal its sweep, and an assertion written against one could only have
	 * passed by accident on an even count. The sweep is a circle and the radius is what it is about.
	 */
	double MaxRadiusAboutAxis(const FDynamicMesh3& Mesh)
	{
		double Max = 0.0;
		for (const int32 Vid : Mesh.VertexIndicesItr())
		{
			const FVector3d V = Mesh.GetVertex(Vid);
			Max = FMath::Max(Max, FMath::Sqrt(V.X * V.X + V.Y * V.Y));
		}
		return Max;
	}

	/** How deep the BLADES are along the axis, ignoring the housing they are bolted to. */
	double BladeDepthAlongAxis(const FDynamicMesh3& Mesh)
	{
		double Min = TNumericLimits<double>::Max();
		double Max = -TNumericLimits<double>::Max();

		for (const int32 Tid : Mesh.TriangleIndicesItr())
		{
			// The rotor carries two roles: the blades are metal hardware and the motor housing is an
			// appliance. Measuring the whole rotor measures the housing, which no pitch can change.
			if (FHFMeshOps::RoleForGroup(Mesh.GetTriangleGroup(Tid)) != EHFSurfaceRole::MetalHardware)
			{
				continue;
			}

			FVector3d A, B, C;
			Mesh.GetTriVertices(Tid, A, B, C);

			Min = FMath::Min(Min, FMath::Min3(A.Z, B.Z, C.Z));
			Max = FMath::Max(Max, FMath::Max3(A.Z, B.Z, C.Z));
		}

		return Max > Min ? Max - Min : -1.0;
	}

	/**
	 * Which way the blades drive air along the spin axis, at a positive rpm. Signed; the magnitude is
	 * arbitrary and only the sign is asserted on.
	 *
	 * A fan pitched the wrong way is perfect in every other measurable respect - watertight, the right
	 * volume, the right sweep, the right roles - and blows at the ceiling. Nothing else in this file
	 * can see it, so it is measured directly off the blade surfaces.
	 *
	 * For each blade triangle, take its outward normal N and the direction T that point of the blade
	 * is travelling in at a positive phase (Z cross radial). A surface that both faces the room (N.Z
	 * positive) and leans into its own motion (N dot T positive) is throwing air into the room; one
	 * that faces the room while leaning away from its motion is throwing air at the ceiling. The
	 * product of the two is that statement, and it is the same sign on BOTH faces of the blade - so it
	 * does not depend on which way the triangles happen to be wound - and exactly zero on a flat one.
	 */
	double AirflowAlongAxis(const FDynamicMesh3& Mesh)
	{
		double Sum = 0.0;

		for (const int32 Tid : Mesh.TriangleIndicesItr())
		{
			if (FHFMeshOps::RoleForGroup(Mesh.GetTriangleGroup(Tid)) != EHFSurfaceRole::MetalHardware)
			{
				continue;
			}

			FVector3d A, B, C;
			Mesh.GetTriVertices(Tid, A, B, C);

			const FVector3d Cross = (B - A).Cross(C - A);
			const double Area = Cross.Length() * 0.5;
			if (Area <= UE_KINDA_SMALL_NUMBER)
			{
				continue;
			}

			const FVector3d Normal = Cross / (Area * 2.0);
			const FVector3d Centroid = (A + B + C) / 3.0;

			const double Radius = FMath::Sqrt(Centroid.X * Centroid.X + Centroid.Y * Centroid.Y);
			if (Radius <= UE_KINDA_SMALL_NUMBER)
			{
				continue;
			}

			// Where this bit of blade is going at a positive phase: Z cross radial.
			const FVector3d Travel(-Centroid.Y / Radius, Centroid.X / Radius, 0.0);

			Sum += Area * Normal.Z * Normal.Dot(Travel);
		}

		return Sum;
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

	// THE BLADES ARE ON THE ROTOR, not on the shell. Measured by RADIAL REACH about the spin axis,
	// which is what "reaches the full sweep" means on a fan: the sweep is the circle the tips
	// describe. This was asserted on the rotor's bounding WIDTH and could not have passed - three
	// blades at 0, 120 and 240 degrees are not symmetric in X, so the box is 95 wide on a declared
	// 120 sweep and no blade length could make that number 120. See MaxRadiusAboutAxis.
	//
	// Tight, because the reach is exact by construction: MakeBlade runs the blade out until its outer
	// CORNER lands on the sweep circle, so this asserts that a fan's declared dimension is true rather
	// than nearly true.
	const double RotorReach = MaxRadiusAboutAxis(Rotor.Mesh);
	const double ShellReach = MaxRadiusAboutAxis(Built.Shell);

	TestTrue(*FString::Printf(TEXT("The rotor reaches the full sweep (%.3f across, against a declared %.2f)"),
		RotorReach * 2.0, P.SweepDiameter),
		FMath::IsNearlyEqual(RotorReach * 2.0, P.SweepDiameter, 0.5));
	TestTrue(*FString::Printf(TEXT("...and nothing on it stands outside the circle it declares (%.3f against %.2f)"),
		RotorReach, P.SweepRadius()),
		RotorReach <= P.SweepRadius() + 0.01);
	TestTrue(*FString::Printf(TEXT("The fixed shell is only the rod and canopy (%.2f across)"),
		ShellReach * 2.0),
		ShellReach * 2.0 < P.SweepDiameter * 0.25);

	// A fan hangs BELOW what it is fixed to, in its own frame: local +Z is the spin axis pointing
	// away from the mounting surface, so everything is at Z >= 0 and the caller aims it. A bounding
	// box is the right instrument for THIS question - it is about one axis, not about a circle.
	TestTrue(TEXT("Nothing sits behind the ceiling it is fixed to"),
		Built.Shell.GetBounds().Min.Z > -1e-6);
	TestTrue(TEXT("The blades hang below the canopy"), Built.BladePlaneZ > P.DropLength);

	// The starting phase reaches the part the actor will seed itself from. One hop of a chain that
	// was correct at every single step and still delivered zero to all six fans in the flat, so each
	// hop is now asserted on its own: this is the pure one, and the actor's is in
	// HouseForge.Editor.AFanIsNotBuiltBeforeItKnowsWhatItIs.
	FHFFanParams Posed = FHFFanKit::DefaultsFor(EHFFanKind::Ceiling);
	Posed.PhaseTurns = 0.25;

	const FHFFanBuild PosedBuild = FHFFanKit::Build(Posed);
	if (TestEqual(TEXT("A posed fan builds one part"), PosedBuild.Parts.Num(), 1))
	{
		TestNearlyEqual(TEXT("The phase it was given is what the part starts at"),
			PosedBuild.Parts[0].DefaultSpinTurns, 0.25, 1e-12);
	}

	return true;
}

/**
 * The blades are pitched, and the pitch is what a fan blade IS.
 *
 * A flat blade builds perfectly and is instantly readable as wrong under any light: it catches as a
 * uniform strip where a real blade has a bright edge and a dark one.
 *
 * Measured on THE BLADES, not on the rotor. This was asserted on the whole rotor's bounds, which
 * cannot see a pitch at all: the blade plane sits at 65% of the housing height and a pitched blade's
 * own depth - 5.8 cm at the steepest angle the kit allows - never reaches outside the 12 cm housing,
 * so the rotor's depth is the housing's at every pitch. The measurement also read Height(), the Y
 * extent, which is a PLAN dimension and therefore falls as the blade is pitched rather than rising.
 * Both mistakes pointed the same way, which is why the numbers looked almost right.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFFanBladePitchTest, "HouseForge.Fan.BladesArePitched", HF_TEST_FLAGS)

bool FHFFanBladePitchTest::RunTest(const FString& Parameters)
{
	auto BladeDepthAtPitch = [](double Degrees) -> double
	{
		FHFFanParams P = FHFFanKit::DefaultsFor(EHFFanKind::Ceiling);
		P.BladePitchDegrees = Degrees;

		const FHFFanBuild Built = FHFFanKit::Build(P);
		return Built.Parts.Num() == 1 ? BladeDepthAlongAxis(Built.Parts[0].Mesh) : -1.0;
	};

	const double Flat = BladeDepthAtPitch(0.0);
	const double Pitched = BladeDepthAtPitch(12.0);
	const double Steep = BladeDepthAtPitch(30.0);

	if (!TestTrue(TEXT("All three rotors build"), Flat > 0.0 && Pitched > 0.0 && Steep > 0.0))
	{
		return false;
	}

	// A flat blade is exactly its own stock thickness deep, which is the one figure here that is not
	// a comparison: it says the measurement is reading the blade and nothing else.
	TestNearlyEqual(TEXT("A flat blade is exactly its own stock deep"),
		Flat, FHFFanKit::DefaultsFor(EHFFanKind::Ceiling).BladeThickness, 1e-6);

	// A pitched blade stands taller than a flat one, and a steeper one taller still. Monotone rather
	// than a fixed figure, because the number depends on the chord and asserting one would be
	// asserting the current dimensions rather than the behaviour.
	TestTrue(*FString::Printf(TEXT("Pitching the blades deepens them (%.3f flat, %.3f at 12 degrees)"),
		Flat, Pitched), Pitched > Flat + 0.5);
	TestTrue(*FString::Printf(TEXT("...and a steeper pitch deepens them further (%.3f at 30 degrees)"), Steep),
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
 * The blades are set the RIGHT WAY ROUND, so the air goes where the fan is for.
 *
 * The failure this exists for is invisible to every other test in this file. A rotor pitched the
 * wrong way is watertight, has the right volume, reaches exactly its declared sweep, carries every
 * surface role, and blows at the ceiling - and it looks completely correct in a still, because a
 * still cannot see which way a blade is set unless you go and measure it. That is what this does.
 *
 * The direction is not a free parameter and BladePitchDegrees deliberately does not carry a sign.
 * A ceiling fan drives air INTO the room and an extract draws it OUT through the wall: those are
 * what the two things are, not settings of one thing. A fan reverses the way a real fan reverses -
 * by running its motor backwards - which is a negative RevolutionsPerMinute with the blades
 * untouched, exactly as on a real reversible fan.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFFanAirflowTest, "HouseForge.Fan.BladesBlowTheRightWay", HF_TEST_FLAGS)

bool FHFFanAirflowTest::RunTest(const FString& Parameters)
{
	// A ceiling fan's local +Z points straight DOWN in the world - AHFFanActor::PlacementFor turns it
	// half a turn about X - so driving air along +Z is driving it at the floor, which is what a
	// ceiling fan in a flat in India is doing every day of the year.
	const FHFFanBuild Ceiling = FHFFanKit::Build(FHFFanKit::DefaultsFor(EHFFanKind::Ceiling));
	double CeilingFlow = 0.0;

	if (TestTrue(TEXT("A ceiling fan builds"), Ceiling.bValid && Ceiling.Parts.Num() == 1))
	{
		CeilingFlow = AirflowAlongAxis(Ceiling.Parts[0].Mesh);
		TestTrue(*FString::Printf(TEXT("A ceiling fan blows down into the room, not up at the slab (%.2f)"), CeilingFlow),
			CeilingFlow > 0.0);
	}

	// An extract's local +Z points into the room it serves, and it has to move air the other way -
	// out through the wall. Same kit, opposite hand, and nothing else about it differs in direction.
	const FHFFanBuild Extract = FHFFanKit::Build(FHFFanKit::DefaultsFor(EHFFanKind::Exhaust));
	if (TestTrue(TEXT("An extract builds"), Extract.bValid && Extract.Parts.Num() == 1))
	{
		const double Flow = AirflowAlongAxis(Extract.Parts[0].Mesh);
		TestTrue(*FString::Printf(TEXT("An extract draws air out of the room rather than into it (%.2f)"), Flow),
			Flow < 0.0);
	}

	// And the two really are opposite, rather than both happening to satisfy their own assertion for
	// some reason that has nothing to do with the pitch.
	if (Ceiling.Parts.Num() == 1 && Extract.Parts.Num() == 1)
	{
		TestTrue(TEXT("The two kinds are set opposite ways, which is the only difference between them"),
			AirflowAlongAxis(Ceiling.Parts[0].Mesh) * AirflowAlongAxis(Extract.Parts[0].Mesh) < 0.0);
	}

	// A flat blade moves no air in either direction. The near-zero is what says the measurement is
	// reading the pitch rather than something else that happens to correlate with it.
	//
	// Judged AGAINST THE PITCHED FIGURE rather than against an absolute tolerance. A flat blade's
	// chamfer facets cancel exactly in arithmetic and only to about one part in a million once three
	// blades have been rotated to their stations in doubles, so an absolute epsilon here would be
	// asserting the accumulated rounding error of a yaw and not the geometry.
	FHFFanParams FlatParams = FHFFanKit::DefaultsFor(EHFFanKind::Ceiling);
	FlatParams.BladePitchDegrees = 0.0;

	const FHFFanBuild FlatFan = FHFFanKit::Build(FlatParams);
	if (TestTrue(TEXT("A flat fan builds"), FlatFan.bValid && FlatFan.Parts.Num() == 1)
		&& CeilingFlow > 0.0)
	{
		const double FlatFlow = AirflowAlongAxis(FlatFan.Parts[0].Mesh);
		TestTrue(*FString::Printf(TEXT("A flat blade drives air neither way (%.6f against a pitched %.2f)"),
			FlatFlow, CeilingFlow),
			FMath::Abs(FlatFlow) < CeilingFlow * 0.001);
	}

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
	Project.CeilingFanBladePitchDegrees = 20.0;
	Project.ExhaustFanBladePitchDegrees = 34.0;
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

	// AND ITS OWN PITCH. One figure covered both, so a ceiling fan's 12 degrees was stamped over
	// FHFFanKit::DefaultsFor(Exhaust)'s deliberate 22 and the kit's figure became unreachable from
	// anything the house built. Asserted as a DIFFERENCE between the two kinds rather than as a
	// number, because the defect was precisely that they were the same number.
	TestEqual(TEXT("...and its own pitch, which is not the ceiling fan's"),
		Exhaust.BladePitchDegrees, 34.0);
	TestNotEqual(TEXT("The two kinds are set at different angles"),
		Exhaust.BladePitchDegrees, Ceiling.BladePitchDegrees);

	TestEqual(TEXT("A rod length means nothing on an extract, so its case depth stands"),
		Exhaust.CaseDepth, 9.0);

	// The shipped figures, not just a pair invented for this test: an extract leaves the settings
	// page steeper than a ceiling fan, which is what the two objects are.
	const FHFFanDefaults Shipped;
	TestTrue(TEXT("Out of the box an extract is pitched steeper than a ceiling fan"),
		Shipped.ExhaustFanBladePitchDegrees > Shipped.CeilingFanBladePitchDegrees);
	TestEqual(TEXT("...and the extract's default is the kit's own figure for the object"),
		Shipped.ExhaustFanBladePitchDegrees, FHFFanKit::DefaultsFor(EHFFanKind::Exhaust).BladePitchDegrees);

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
