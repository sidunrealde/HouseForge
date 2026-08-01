// Copyright Siddartha G. All Rights Reserved.

#include "HouseForgeEditor.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Actors/HFElementActors.h"
#include "Actors/HFFanActor.h"
#include "Actors/HFHouseActor.h"
#include "Components/DynamicMeshComponent.h"
#include "Editor.h"
#include "Engine/World.h"
#include "Geometry/HFGenerators.h"
#include "Geometry/HFMeshOps.h"
#include "MeshQueries.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Model/HFBuildDefaults.h"
#include "Model/HFSampleHouse.h"
#include "Model/HFTypes.h"

using namespace UE::Geometry;

#define HF_TEST_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// ---------------------------------------------------------------------------------------------
//
// THE SEAM between the spin mechanism and the level.
//
// EHFMotionType::Spin landed complete and was left with no production caller for a whole milestone:
// an unbounded phase, a rate in rpm, a pose that survives a rebuild, an integrator on the actor, and
// nothing anywhere in the plugin that ever created a spinning part. It appeared in the articulation
// header, in its implementation and in test files. The reference flat's three ceiling fans and three
// extracts were lines in a spec that became no actor at all - AHFHouseActor read CeilingFan only to
// punch a rod hole in the false ceiling above a fan that was not there, and never read ExhaustFan.
//
// So these tests are about the flat, not about meshes. HFFanTests measures the geometry; what is
// left is whether any of it is in the level, on its own component, turning.
//
// ---------------------------------------------------------------------------------------------

namespace HouseForgeFan
{
	/** Every fan the house built, by fixture id. */
	TMap<FName, AHFFanActor*> FansIn(AHFHouseActor* House)
	{
		TMap<FName, AHFFanActor*> Out;
		for (AActor* Element : House->ElementActors)
		{
			if (AHFFanActor* Fan = Cast<AHFFanActor>(Element))
			{
				Out.Add(Fan->ElementId, Fan);
			}
		}
		return Out;
	}
}

/**
 * Every fan the reference flat declares is in it, and every one of them turns.
 *
 * The assertion the milestone claimed and never made. Written to match the wardrobe's: a house build
 * produces N spinning parts, each on its own component, and the count comes off the SPEC rather than
 * being a literal - a fan added to the flat has to be built without anybody remembering to edit a
 * number here.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFFansInTheFlatTest,
	"HouseForge.Editor.FansInTheReferenceFlatTurn", HF_TEST_FLAGS)

bool FHFFansInTheFlatTest::RunTest(const FString& Parameters)
{
	using namespace HouseForgeFan;

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("An editor world is open"), World))
	{
		return false;
	}

	AHFHouseActor* House = World->SpawnActor<AHFHouseActor>();
	if (!TestNotNull(TEXT("A house actor spawns"), House))
	{
		return false;
	}
	ON_SCOPE_EXIT{ if (IsValid(House)) { House->ClearGeometry(); House->Destroy(); } };

	House->SetSpec(FHFSampleHouse::Make2BHK());
	House->BuildGeometry();

	const TMap<FName, AHFFanActor*> Built = FansIn(House);

	int32 Declared = 0;
	int32 Ceiling = 0;
	int32 Extract = 0;

	for (const FHFFixture& Fixture : House->Spec.Fixtures)
	{
		if (Fixture.Type != EHFFixtureType::CeilingFan && Fixture.Type != EHFFixtureType::ExhaustFan)
		{
			continue;
		}

		++Declared;
		Fixture.Type == EHFFixtureType::CeilingFan ? ++Ceiling : ++Extract;

		TestTrue(*FString::Printf(TEXT("'%s' was built"), *Fixture.Id.ToString()),
			Built.Contains(Fixture.Id));
	}

	AddInfo(FString::Printf(TEXT("%d fans in the flat: %d ceiling, %d extract."), Declared, Ceiling, Extract));

	if (!TestEqual(TEXT("Every fan in the spec became an actor"), Built.Num(), Declared))
	{
		return false;
	}
	TestTrue(TEXT("The flat has ceiling fans to build at all"), Ceiling >= 3);
	TestTrue(TEXT("...and extracts"), Extract >= 2);

	// ------------------------------------------------------------------- and every one of them turns

	// Visible blade angles, in fractions of ONE BLADE PITCH, for the fans that are copies of each
	// other. See below for why both of those qualifications are load-bearing.
	TArray<double> CeilingAngles;

	for (const TPair<FName, AHFFanActor*>& Entry : Built)
	{
		AHFFanActor* Fan = Entry.Value;
		const FString Where = Entry.Key.ToString();

		if (!TestEqual(*FString::Printf(TEXT("'%s' has exactly one moving part"), *Where),
			Fan->NumParts(), 1))
		{
			continue;
		}

		const FHFPartState* Rotor = Fan->FindPart(AHFFanActor::RotorPartId());
		if (!TestNotNull(*FString::Printf(TEXT("'%s' has a rotor"), *Where), Rotor))
		{
			continue;
		}

		// The whole point. A part that merely exists is a decoration; one that revolves is a fan.
		TestTrue(*FString::Printf(TEXT("'%s' revolves"), *Where), Rotor->Motion.Revolves());
		TestTrue(*FString::Printf(TEXT("'%s' has a speed"), *Where),
			FMath::Abs(Rotor->Motion.RevolutionsPerMinute) > 0.0);

		// On its own component, or it could never move independently of the canopy holding it up.
		TestNotNull(*FString::Printf(TEXT("'%s' has the rotor on a component of its own"), *Where),
			Fan->GetPartComponent(AHFFanActor::RotorPartId()));

		// THE PHASE THE DRAWING'S ID DECIDES ACTUALLY REACHES THE PART. This is the assertion that
		// fails when a fan is built before it knows what it is: every individual hop of the chain -
		// the hash, the params, the kit's DefaultSpinTurns, the part state - was correct, and all six
		// fans still arrived at phase 0, because the actor generated itself once at spawn off the back
		// of being given a label and the rotor created by that ghost generation then beat the real
		// phase. Stated against the decided function of the id rather than against a literal, so it
		// says what the pose is SUPPOSED to be and not merely that it is not zero.
		if (Fan->Fan.BladeCount > 0)
		{
			TestNearlyEqual(*FString::Printf(TEXT("'%s' is stopped where its id says"), *Where),
				Rotor->SpinTurns, AHFFanActor::PhaseForId(Entry.Key) / Fan->Fan.BladeCount, 1e-9);

			if (Fan->Fan.Kind == EHFFanKind::Ceiling)
			{
				CeilingAngles.Add(FMath::Frac(Rotor->SpinTurns * Fan->Fan.BladeCount));
			}
		}
	}

	// THREE IDENTICAL FANS MUST NOT BE STOPPED ON THE SAME BLADE. They are three copies of one
	// object, which is exactly what a still must not show.
	//
	// Two things about how this is measured, and both were wrong before.
	//
	// IN BLADE PITCHES, NOT IN TURNS. A rotor repeats every 1/BladeCount of a turn, so a three-blade
	// fan at 0.10 turns and one at 0.4333 are the same picture. Asserting distinctness on the raw
	// phase would happily pass two fans that are pixel-identical in a render.
	//
	// OVER THE CEILING FANS ONLY. Those three are built from identical parameters and are the ones
	// that read as copies. The three extracts differ in sweep, blade count and case, so demanding
	// that a bathroom extract be stopped differently from a ceiling fan is not a claim about anything
	// anybody can see - and the old assertion, an exact-distinctness test over all six in 1/1000
	// buckets, was really a hash-collision test wearing a quality bar's clothes.
	//
	// The bar is 0.08 of a blade pitch, about ten degrees of blade. See AHFFanActor::PhaseForId for
	// why that is a bar and not a guarantee.
	for (int32 I = 0; I < CeilingAngles.Num(); ++I)
	{
		for (int32 J = I + 1; J < CeilingAngles.Num(); ++J)
		{
			// Circular: the blade angle wraps, so 0.02 and 0.99 are a fiftieth apart and not a whole
			// blade. A straight subtraction would call the two most similar fans in the flat the two
			// most different.
			const double Raw = FMath::Abs(CeilingAngles[I] - CeilingAngles[J]);
			const double Apart = FMath::Min(Raw, 1.0 - Raw);

			TestTrue(*FString::Printf(
				TEXT("No two of the flat's ceiling fans are stopped on the same blade (%.4f and %.4f of a blade, %.4f apart)"),
				CeilingAngles[I], CeilingAngles[J], Apart), Apart > 0.08);
		}
	}

	TestTrue(TEXT("There were ceiling fans to compare in the first place"), CeilingAngles.Num() >= 3);

	// ---------------------------------------------------------------- and they are not the same fan
	//
	// AN EXTRACT IS PITCHED LIKE AN EXTRACT. FHFFanKit::DefaultsFor(Exhaust) sets 22 degrees with a
	// comment saying an extract is a different object from a ceiling fan, and a single project-wide
	// BladePitchDegrees then stamped a ceiling fan's 12 over it - so the kit's figure was unreachable
	// from anything the house built, and every extract in the flat turned flat spokes in a case.
	//
	// ASSERTED ON THE FANS THE HOUSE SPAWNED, not on DefaultsFor. The kit's figure was correct the
	// whole time; the defect was entirely in what reached the level, so a test that asks the kit
	// cannot see it. HouseForge.Fan.ProjectFiguresReachTheFan covers the settings half.
	const FHFFanDefaults Project = FHFBuildDefaults::FromProjectSettings().Fan;

	for (const TPair<FName, AHFFanActor*>& Entry : Built)
	{
		const AHFFanActor* Fan = Entry.Value;
		const bool bCeiling = Fan->Fan.Kind == EHFFanKind::Ceiling;

		TestEqual(*FString::Printf(TEXT("'%s' is set at the angle its own kind is set at"),
			*Entry.Key.ToString()), Fan->Fan.BladePitchDegrees,
			bCeiling ? Project.CeilingFanBladePitchDegrees : Project.ExhaustFanBladePitchDegrees);

		// A pitch is a magnitude and never zero: a flat blade reads as a paper cut-out under any
		// lighting, which is the quality bar in .claude/rules/04-conventions.md.
		TestTrue(*FString::Printf(TEXT("'%s' has blades that are actually pitched"), *Entry.Key.ToString()),
			Fan->Fan.BladePitchDegrees > 0.0);
	}

	// And the variation is deterministic: two builds of one spec have to produce one flat, or two
	// renders of the same drawing would differ for no stated reason.
	TestEqual(TEXT("A fan's starting phase is a property of the fan, not of when it was built"),
		AHFFanActor::PhaseForId(TEXT("F_Fan_Living")), AHFFanActor::PhaseForId(TEXT("F_Fan_Living")));
	TestNotEqual(TEXT("...and two fans do not share one"),
		AHFFanActor::PhaseForId(TEXT("F_Fan_Living")), AHFFanActor::PhaseForId(TEXT("F_Fan_MBed")));

	return true;
}

/**
 * AN ELEMENT DOES NOT BUILD ITSELF BEFORE IT HAS BEEN TOLD WHAT IT IS.
 *
 * The defect this exists for cost six fans their phase while every step of the chain that carried
 * that phase was correct. AActor::SetActorLabel fires PostEditChangeProperty, the house labels every
 * element the instant it spawns one, and AHFElementActor::PostEditChangeProperty rebuilt on any
 * property change whatever - including the engine's own. So every element generated itself once
 * with default parameters before the composing layer had said a word to it.
 *
 * Harmless-looking, and on a wall it only wastes a generation. On anything with pose state it is
 * destructive, because that ghost generation CREATES THE PARTS: the rotor came into existence at
 * phase 0, and a part that already exists keeps its pose through the next regeneration - correctly,
 * that is what stops a rebuild slamming every shutter in the flat shut - so the real phase, applied
 * moments later, could never land.
 *
 * Written against the label specifically, because that is the trigger, and asserted on the phase,
 * because that is what it destroyed.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFFanNotBuiltEarlyTest,
	"HouseForge.Editor.AFanIsNotBuiltBeforeItKnowsWhatItIs", HF_TEST_FLAGS)

bool FHFFanNotBuiltEarlyTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("An editor world is open"), World))
	{
		return false;
	}

	AHFFanActor* Fan = World->SpawnActor<AHFFanActor>();
	if (!TestNotNull(TEXT("A fan actor spawns"), Fan))
	{
		return false;
	}
	ON_SCOPE_EXIT{ if (IsValid(Fan)) { Fan->Destroy(); } };

	FHFFixture Fixture;
	Fixture.Id = TEXT("F_Fan_Test");
	Fixture.Type = EHFFixtureType::CeilingFan;
	Fixture.Params.Diameter = 120.0;

	Fan->ApplyProjectDefaults(EHFFanKind::Ceiling);
	Fan->ApplyFixture(Fixture);

	// Exactly what AHFHouseActor::BuildGeometry does to every element it spawns, and exactly what
	// used to build the rotor at phase 0 before Regenerate was ever called.
	Fan->SetActorLabel(TEXT("Fan_F_Fan_Test"));

	TestEqual(TEXT("Being given a name does not build a fan"), Fan->NumParts(), 0);

	Fan->Regenerate();

	if (!TestEqual(TEXT("Regenerating does"), Fan->NumParts(), 1))
	{
		return false;
	}

	const double Expected = AHFFanActor::PhaseForId(Fixture.Id) / FMath::Max(Fan->Fan.BladeCount, 1);

	TestTrue(TEXT("...and the fan is stopped somewhere rather than at nothing"),
		Fan->GetPartSpinTurns(AHFFanActor::RotorPartId()) > 0.0);
	TestNearlyEqual(TEXT("...exactly where the id it was given says it should be"),
		Fan->GetPartSpinTurns(AHFFanActor::RotorPartId()), Expected, 1e-9);

	// And a phase only means anything folded by the blade count, which is the unit the whole
	// decision is stated in - see AHFFanActor::PhaseForId.
	TestNearlyEqual(TEXT("The blade angle a still shows is the one the id decides"),
		FMath::Frac(Fan->GetPartSpinTurns(AHFFanActor::RotorPartId()) * Fan->Fan.BladeCount),
		AHFFanActor::PhaseForId(Fixture.Id), 1e-9);

	return true;
}

/**
 * A ceiling fan hangs from the slab, above the floor and below the ceiling, with its axis down.
 *
 * Placement rather than geometry, and it is the half a pure kit test cannot reach. A fan built
 * perfectly and hung the wrong way up blows at the ceiling; one hung off the false-ceiling soffit
 * instead of the structural slab sits lower than it is drawn and leaves the rod hole above it
 * serving nothing.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFFanPlacementTest,
	"HouseForge.Editor.FansHangWhereTheDrawingPutsThem", HF_TEST_FLAGS)

bool FHFFanPlacementTest::RunTest(const FString& Parameters)
{
	using namespace HouseForgeFan;

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("An editor world is open"), World))
	{
		return false;
	}

	AHFHouseActor* House = World->SpawnActor<AHFHouseActor>();
	if (!TestNotNull(TEXT("A house actor spawns"), House))
	{
		return false;
	}
	ON_SCOPE_EXIT{ if (IsValid(House)) { House->ClearGeometry(); House->Destroy(); } };

	House->SetSpec(FHFSampleHouse::Make2BHK());
	House->BuildGeometry();

	const TMap<FName, AHFFanActor*> Built = FansIn(House);

	for (const FHFFixture& Fixture : House->Spec.Fixtures)
	{
		AHFFanActor* Fan = Built.FindRef(Fixture.Id);
		if (Fan == nullptr)
		{
			continue;
		}

		const FString Where = Fixture.Id.ToString();
		const FHFRoom* Room = House->Spec.FindRoom(Fixture.RoomId);
		const double FloorZ = Room != nullptr ? Room->FloorZ : 0.0;

		// The blade tips, in the world. Read off the component that actually carries them rather
		// than off the actor's transform, because a transform that is right about a mesh built in
		// the wrong place is still a fan in the wrong place.
		UDynamicMeshComponent* Rotor = Fan->GetPartComponent(AHFFanActor::RotorPartId());
		if (!TestNotNull(*FString::Printf(TEXT("'%s' has a rotor component"), *Where), Rotor))
		{
			continue;
		}

		// UPrimitiveComponent::CalcBounds is protected, so ask the component to refresh the bounds it
		// already publishes rather than recomputing them from outside.
		Rotor->UpdateBounds();
		const FBoxSphereBounds Blades = Rotor->Bounds;
		const double LowestBlade = Blades.Origin.Z - Blades.BoxExtent.Z;
		const double HighestBlade = Blades.Origin.Z + Blades.BoxExtent.Z;

		if (Fixture.Type == EHFFixtureType::CeilingFan)
		{
			const double CeilingZ = FloorZ + (Room != nullptr ? Room->CeilingHeight : 300.0);

			// Local +Z is the spin axis pointing away from what the fan is fixed to, so on a ceiling
			// it points straight down. A fan placed without turning it would have its axis up, its
			// blades above the slab, and its pitch blowing the wrong way.
			const FVector Axis = Fan->GetActorTransform().TransformVectorNoScale(FVector::ZAxisVector);
			TestTrue(*FString::Printf(TEXT("'%s' points its axis down (%s)"), *Where, *Axis.ToString()),
				Axis.Z < -0.99);

			TestTrue(*FString::Printf(TEXT("'%s' hangs below its slab (blades at %.1f, slab at %.1f)"),
				*Where, HighestBlade, CeilingZ), HighestBlade < CeilingZ + 0.01);
			TestTrue(*FString::Printf(TEXT("'%s' is well clear of the floor (%.1f above %.1f)"),
				*Where, LowestBlade, FloorZ), LowestBlade > FloorZ + 180.0);

			// ON A ROD, and at a height somebody would actually hang one at. A fan flush to the slab
			// is a different fitting - it reads as a light - and one hung off the false-ceiling soffit
			// instead of the structural slab sits lower than it is drawn while the hole cut for its rod
			// serves nothing. The gap between the slab and the top of the rotor IS the rod.
			TestTrue(*FString::Printf(TEXT("'%s' hangs on a rod rather than flush to the slab (%.1f of drop)"),
				*Where, CeilingZ - HighestBlade), CeilingZ - HighestBlade > 10.0);

			// Head height in a flat, and below whatever the false ceiling puts OVER THIS FAN.
			// Blades buried in plasterboard is the failure the rod length exists to prevent.
			//
			// Asked at the fan's own plan position, not off the ceiling's nominal drop, because for
			// a perimeter style those are different numbers and only one of them is about this fan.
			// A cove is a band round the edge of the room with the centre left open to the slab, and
			// every fan in the flat hangs in that open centre - so the drop that matters over it is
			// zero, however deep the band round the walls happens to be. Stated the other way this
			// passed for years on a coincidence: the bands were 200 deep, blades hang 270 up, and
			// 270 < 280 held. Deepening the bands to 500 to bury the beams broke the arithmetic
			// without moving a single fan, which is the tell that the assertion was measuring the
			// wrong thing. CeilingSoffitDropAt is the question the composing layer itself asks.
			for (const FHFFalseCeiling& Ceiling : House->Spec.FalseCeilings)
			{
				if (Ceiling.RoomId != Fixture.RoomId || Ceiling.Style == EHFCeilingStyle::None
					|| Room == nullptr)
				{
					continue;
				}

				const double DropOverTheFan =
					FHFGenerators::CeilingSoffitDropAt(Ceiling, *Room, Fixture.Position);

				const double Soffit = CeilingZ - DropOverTheFan;
				TestTrue(*FString::Printf(TEXT("'%s' hangs below the false ceiling, not inside it (%.1f under %.1f)"),
					*Where, HighestBlade, Soffit), HighestBlade < Soffit + 0.01);
			}

			// Over the spot the drawing marked, which is also the spot the false ceiling was cut for.
			//
			// READ OFF THE COMPONENT'S LOCATION, which is its pivot and therefore the spin axis - not
			// off the centre of its bounding box. This was asserted on Blades.Origin, and a three-blade
			// rotor's box centre is not on its axis at all: the tips sit at 0, 120 and 240 degrees, so
			// the box centre is about 13 cm off the axis in a direction that rotates with the phase.
			// Every fan in the flat was hung exactly where it was drawn and this said otherwise.
			const FVector OnAxis = Rotor->GetComponentLocation();
			TestTrue(*FString::Printf(TEXT("'%s' hangs where the drawing put it (%.1f, %.1f against %.1f, %.1f)"),
				*Where, OnAxis.X, OnAxis.Y, Fixture.Position.X, Fixture.Position.Y),
				FVector2D(OnAxis.X, OnAxis.Y).Equals(Fixture.Position, 0.1));

			// The box is still worth an assertion, just not that one: it says the MESH is centred on
			// the axis the component sits at, which is the thing reading the pivot alone cannot see.
			// A rotor generated 50 cm off its own origin would place perfectly and hang off to one side.
			TestTrue(*FString::Printf(TEXT("'%s' has its blades about that axis rather than off to one side"),
				*Where),
				FMath::Abs(Blades.Origin.X - OnAxis.X) < Fan->Fan.SweepRadius() * 0.5
					&& FMath::Abs(Blades.Origin.Y - OnAxis.Y) < Fan->Fan.SweepRadius() * 0.5);

			// And they reach: three tips on a circle present at worst 0.75 of the sweep RADIUS to
			// either side of the axis, whatever the phase, so anything under that is not a fan of the
			// size the drawing asked for.
			TestTrue(*FString::Printf(TEXT("'%s' sweeps what it was drawn at (%.1f x %.1f half-extents on a %.0f sweep)"),
				*Where, Blades.BoxExtent.X, Blades.BoxExtent.Y, Fan->Fan.SweepDiameter),
				Blades.BoxExtent.X > Fan->Fan.SweepRadius() * 0.7
					&& Blades.BoxExtent.Y > Fan->Fan.SweepRadius() * 0.7);
		}
		else if (Fixture.Type == EHFFixtureType::ExhaustFan)
		{
			// An extract's axis is horizontal: it blows through a wall, not through a ceiling.
			const FVector Axis = Fan->GetActorTransform().TransformVectorNoScale(FVector::ZAxisVector);
			TestTrue(*FString::Printf(TEXT("'%s' blows horizontally (%s)"), *Where, *Axis.ToString()),
				FMath::Abs(Axis.Z) < 0.01);

			// At the height it was drawn at, which for every extract in this flat is high on the wall
			// and clear of the beam soffits above it.
			//
			// Off the component's location for the same reason as the ceiling fan above. A five-blade
			// rotor's box centre is only 0.3 cm off its axis, so this passed on a coincidence rather
			// than on being right - and would have started failing the day somebody drew a
			// three-blade extract.
			const double Centre = FloorZ + Fixture.BaseZ + Fixture.Height * 0.5;
			const double AxisZ = Rotor->GetComponentLocation().Z;
			TestTrue(*FString::Printf(TEXT("'%s' sits at the height it was drawn (%.1f against %.1f)"),
				*Where, AxisZ, Centre),
				FMath::IsNearlyEqual(AxisZ, Centre, 0.1));

			// And it faces INTO the room it serves rather than into the wall it is screwed to. The
			// case would look identical either way in plan, which is the only view a drawing has.
			if (const FHFWall* Wall = House->Spec.FindWall(Fixture.AnchorWallId))
			{
				const FVector2D OnWall = FMath::ClosestPointOnSegment2D(
					Fixture.Position, Wall->Start, Wall->End);
				const FVector2D ToRoom = (Fixture.Position - OnWall).GetSafeNormal();

				if (!ToRoom.IsNearlyZero())
				{
					TestTrue(*FString::Printf(TEXT("'%s' faces the room it extracts from"), *Where),
						FVector2D::DotProduct(FVector2D(Axis.X, Axis.Y), ToRoom) > 0.9);
				}
			}
		}
	}

	return true;
}

/**
 * A FAN UNDER A FALSE CEILING HANGS BELOW IT, NOT INSIDE IT.
 *
 * The assertion in FHFFanPlacementTest that says this - "hangs below the false ceiling, not inside
 * it" - could never fire, and did not for the whole milestone. All three ceiling fans in the
 * reference flat sit in the middle of a Cove or Peripheral ceiling, whose centre is left open to the
 * structure, so the drop over each of them is zero and any rod length whatever passes. A dead
 * assertion guarding a real defect: DropLength was a fixed project figure of 30 and nothing anywhere
 * derived it from the room, so a fan in a room with a 40 cm full drop had its motor top at 30 -
 * INSIDE the plasterboard - with the blades as edge-on slivers lying in the panel. From underneath
 * it read as a bladed light fitting glued to the ceiling.
 *
 * So this test puts fans where the flat has none: under the kitchen's 300 mm full drop and the
 * master bath's 400 mm one. IN A COPY OF THE SPEC, not in the golden fixture - the fixture is what
 * the drawing says, and inventing a kitchen ceiling fan to make a test fire would be changing the
 * drawing to suit the test. Two different drops on purpose, because a single one passes anything
 * that adds a constant.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFFanUnderACeilingTest,
	"HouseForge.Editor.AFanHangsBelowAFalseCeiling", HF_TEST_FLAGS)

bool FHFFanUnderACeilingTest::RunTest(const FString& Parameters)
{
	using namespace HouseForgeFan;

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("An editor world is open"), World))
	{
		return false;
	}

	FHFHouseSpec Spec = FHFSampleHouse::Make2BHK();

	// Where a fan would go if one were drawn there: the middle of the room, which under a full drop
	// is panel wherever it is. Taken off the boundary rather than written down, so this follows the
	// fixture if the plan moves.
	auto CentreOf = [&Spec](const FName& RoomId) -> FVector2D
	{
		const FHFRoom* Room = Spec.FindRoom(RoomId);
		if (Room == nullptr || Room->Boundary.IsEmpty())
		{
			return FVector2D::ZeroVector;
		}

		FVector2D Sum = FVector2D::ZeroVector;
		for (const FVector2D& Point : Room->Boundary)
		{
			Sum += Point;
		}
		return Sum / Room->Boundary.Num();
	};

	// In the spec's own units - millimetres - because SetSpec converts exactly once at ingest.
	auto AddFan = [&Spec, &CentreOf](const FName& Id, const FName& RoomId)
	{
		FHFFixture Fan;
		Fan.Id = Id;
		Fan.RoomId = RoomId;
		Fan.Type = EHFFixtureType::CeilingFan;
		Fan.Label = TEXT("Ceiling fan, under a full drop");
		Fan.Position = CentreOf(RoomId);
		Fan.Footprint = FVector2D(1200.0, 1200.0);
		Fan.Height = 300.0;
		Fan.Params.Diameter = 1200.0;
		Spec.Fixtures.Add(Fan);
	};

	AddFan(TEXT("F_Fan_UnderKitchen"), TEXT("R_Kitchen"));
	AddFan(TEXT("F_Fan_UnderMBath"), TEXT("R_MBath"));

	// TWO DIFFERENT DROPS, SET HERE RATHER THAN BORROWED FROM THE FLAT'S DESIGN.
	//
	// The closing assertion is that a deeper ceiling gets a longer rod BY THE DIFFERENCE, which is
	// the one thing a constant cannot fake - and it needs two drops that differ. It used to read
	// them off the reference flat, where the kitchen happened to drop 300 and the master bath 400.
	// Both are 500 now, for a reason that has nothing to do with fans: every drop in that flat is
	// set by the 450 beams it has to bury. The difference went to zero and this test failed while
	// nothing about a fan had changed. What it wants is a controlled pair, so it makes its own.
	for (FHFFalseCeiling& Ceiling : Spec.FalseCeilings)
	{
		if (Ceiling.RoomId == TEXT("R_Kitchen")) { Ceiling.Drop = 300.0; }
		if (Ceiling.RoomId == TEXT("R_MBath"))   { Ceiling.Drop = 400.0; }
	}

	AHFHouseActor* House = World->SpawnActor<AHFHouseActor>();
	if (!TestNotNull(TEXT("A house actor spawns"), House))
	{
		return false;
	}
	ON_SCOPE_EXIT{ if (IsValid(House)) { House->ClearGeometry(); House->Destroy(); } };

	House->SetSpec(Spec);
	House->BuildGeometry();

	const TMap<FName, AHFFanActor*> Built = FansIn(House);
	int32 Checked = 0;

	for (const FHFFixture& Fixture : House->Spec.Fixtures)
	{
		if (Fixture.Type != EHFFixtureType::CeilingFan)
		{
			continue;
		}

		AHFFanActor* Fan = Built.FindRef(Fixture.Id);
		const FHFRoom* Room = House->Spec.FindRoom(Fixture.RoomId);

		if (Fan == nullptr || Room == nullptr)
		{
			continue;
		}

		const FString Where = Fixture.Id.ToString();
		const double SlabZ = Room->FloorZ + Room->CeilingHeight;

		// What the ceiling actually puts over this fan, asked of the same function the composing
		// layer asked. A fan in the open centre of a cove gets zero and is expected to be unchanged.
		double SoffitDrop = 0.0;
		for (const FHFFalseCeiling& Ceiling : House->Spec.FalseCeilings)
		{
			if (Ceiling.RoomId == Fixture.RoomId)
			{
				SoffitDrop = FMath::Max(SoffitDrop,
					FHFGenerators::CeilingSoffitDropAt(Ceiling, *Room, Fixture.Position));
			}
		}

		++Checked;
		AddInfo(FString::Printf(TEXT("'%s': %.1f cm of ceiling over it, %.1f cm of rod."),
			*Where, SoffitDrop, Fan->Fan.DropLength));

		// THE ROD CLEARS WHAT IS ABOVE IT. Stated against the drop the room actually has, so it says
		// something for a fan under bare slab as well as for one under 40 cm of plasterboard.
		TestTrue(*FString::Printf(TEXT("'%s' has a rod longer than the ceiling it passes through (%.1f through %.1f)"),
			*Where, Fan->Fan.DropLength, SoffitDrop), Fan->Fan.DropLength > SoffitDrop);

		// The canopy sits AT the soffit, covering the hole cut for the rod - not up at the slab
		// where it would be invisible in the void and the hole left showing its four corners.
		TestNearlyEqual(*FString::Printf(TEXT("'%s' puts its canopy at the soffit"), *Where),
			Fan->Fan.CanopyDrop, SoffitDrop, 1e-6);

		// And the rod below the canopy is the project's figure, unchanged by the ceiling: a fan
		// hangs at one height in a flat whatever is going on above it.
		TestNearlyEqual(*FString::Printf(TEXT("'%s' shows the same length of rod as every other fan"), *Where),
			Fan->Fan.DropLength - Fan->Fan.CanopyDrop,
			FHFBuildDefaults::FromProjectSettings().Fan.CeilingFanDropLength, 1e-6);

		// THE MEASURED ARTICLE. Everything above is arithmetic on parameters; this is where the
		// blades ended up, off the component that carries them.
		UDynamicMeshComponent* Rotor = Fan->GetPartComponent(AHFFanActor::RotorPartId());
		if (!TestNotNull(*FString::Printf(TEXT("'%s' has a rotor component"), *Where), Rotor))
		{
			continue;
		}

		Rotor->UpdateBounds();
		const FBoxSphereBounds Blades = Rotor->Bounds;
		const double HighestBlade = Blades.Origin.Z + Blades.BoxExtent.Z;
		const double SoffitZ = SlabZ - SoffitDrop;

		TestTrue(*FString::Printf(
			TEXT("'%s' turns in the room rather than in the plasterboard (top of rotor %.1f, soffit %.1f)"),
			*Where, HighestBlade, SoffitZ), HighestBlade < SoffitZ - 0.01);

		// Still at a height somebody would hang a fan at, which is the other half of the same
		// decision: a rod that cleared the ceiling by lowering the fan to head height would pass
		// every assertion above and be unusable.
		TestTrue(*FString::Printf(TEXT("'%s' is still well clear of the floor (%.1f above %.1f)"),
			*Where, Blades.Origin.Z - Blades.BoxExtent.Z, Room->FloorZ),
			Blades.Origin.Z - Blades.BoxExtent.Z > Room->FloorZ + 180.0);

		// And the canopy covers the hole the ceiling cut for the rod, corners and all. A square
		// reaches its half-diagonal, and the corners are exactly what showed.
		const FHFFanParams Resolved = AHFFanActor::ParamsFor(Fixture);
		TestTrue(*FString::Printf(TEXT("'%s' has a canopy that covers its own rod hole (%.2f against %.2f)"),
			*Where, Resolved.CanopyRadius(), Resolved.RodHoleHalfSide() * UE_DOUBLE_SQRT_2),
			Resolved.CanopyRadius() > Resolved.RodHoleHalfSide() * UE_DOUBLE_SQRT_2);

		// ...and the hole is still big enough for the rod to hang plumb in.
		TestTrue(*FString::Printf(TEXT("'%s' has a rod hole its rod fits through"), *Where),
			Resolved.RodHoleHalfSide() > Resolved.RodDiameter * 0.5);
	}

	// The flat's three plus the two invented, and both invented ones matter: one drop alone would
	// pass anything that adds a constant.
	TestTrue(TEXT("There were fans under real ceilings to check"), Checked >= 5);

	const AHFFanActor* Kitchen = Built.FindRef(TEXT("F_Fan_UnderKitchen"));
	const AHFFanActor* MBath = Built.FindRef(TEXT("F_Fan_UnderMBath"));

	if (TestNotNull(TEXT("The kitchen fan was built under its full drop"), Kitchen) &&
		TestNotNull(TEXT("...and the master bath's, under a deeper one"), MBath))
	{
		// The deeper ceiling gets the longer rod, by exactly the difference between the two drops -
		// 400 mm against 300, both set above. This is the assertion a constant cannot satisfy.
		TestNearlyEqual(TEXT("A deeper ceiling gets a longer rod, by the depth it is deeper"),
			MBath->Fan.DropLength - Kitchen->Fan.DropLength, 10.0, 1e-6);
	}

	return true;
}

/**
 * AN EXTRACT BLOWS THROUGH THE WALL, not into it.
 *
 * The fan's own case has an aperture and its blades turn inside that aperture, and none of it is
 * worth anything while the masonry behind is solid. All three extracts in the flat were bolted to
 * unbroken walls and discharging into them - and it is invisible from the room, because the case
 * covers exactly the spot where the hole is not, so every still of every bathroom looked right.
 *
 * Measured on the wall's own geometry rather than on the derived opening, because an opening added
 * to a list that nothing cuts would satisfy any assertion about the list.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFExtractDuctTest,
	"HouseForge.Editor.AnExtractHasSomethingToBlowThrough", HF_TEST_FLAGS)

bool FHFExtractDuctTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("An editor world is open"), World))
	{
		return false;
	}

	AHFHouseActor* House = World->SpawnActor<AHFHouseActor>();
	if (!TestNotNull(TEXT("A house actor spawns"), House))
	{
		return false;
	}
	ON_SCOPE_EXIT{ if (IsValid(House)) { House->ClearGeometry(); House->Destroy(); } };

	House->SetSpec(FHFSampleHouse::Make2BHK());
	House->BuildGeometry();

	int32 Extracts = 0;

	for (const FHFFixture& Fixture : House->Spec.Fixtures)
	{
		if (Fixture.Type != EHFFixtureType::ExhaustFan)
		{
			continue;
		}

		const FString Where = Fixture.Id.ToString();
		++Extracts;

		const FHFWall* Wall = House->Spec.FindWall(Fixture.AnchorWallId);
		if (!TestNotNull(*FString::Printf(TEXT("'%s' names a wall to blow through"), *Where), Wall))
		{
			continue;
		}

		// The wall actor that was actually built, and the openings it was actually built with.
		AHFWallActor* WallActor = nullptr;
		for (AActor* Element : House->ElementActors)
		{
			AHFWallActor* Candidate = Cast<AHFWallActor>(Element);
			if (Candidate != nullptr && Candidate->ElementId == Wall->Id)
			{
				WallActor = Candidate;
				break;
			}
		}

		if (!TestNotNull(*FString::Printf(TEXT("'%s' has a wall actor to be cut into"), *Where), WallActor))
		{
			continue;
		}

		const FHFRoom* Room = House->Spec.FindRoom(Fixture.RoomId);
		const FHFOpening Expected = AHFFanActor::DuctOpeningFor(Fixture, *Wall, Room);

		// ------------------------------------------------------- the hole is where the FAN is, not near it
		//
		// Measured against the rotor's own world position rather than against a figure recomputed
		// here, because the two are derived by different code down different datums and the whole
		// defect was that they disagreed. Volume alone cannot see this: a hole in the wrong place is
		// exactly as many cubic centimetres as one in the right place.
		{
			AHFFanActor* FanActor = nullptr;
			for (AActor* Element : House->ElementActors)
			{
				AHFFanActor* Candidate = Cast<AHFFanActor>(Element);
				if (Candidate != nullptr && Candidate->ElementId == Fixture.Id)
				{
					FanActor = Candidate;
					break;
				}
			}

			if (TestNotNull(*FString::Printf(TEXT("'%s' was built as an actor"), *Where), FanActor))
			{
				// THE DISCHARGE SIDE IS WIRED UP. A hole cored and left bare is the only opening in
				// the flat with no lining - the duct is deliberately kept out of Spec.Openings so no
				// ventilator sash is built in it, so it gets nothing from the opening system and the
				// sleeve and cowl have to come from the fan. Asserted at the seam, since
				// HouseForge.Fan.AnExtractIsLinedThroughTheWall already measures the geometry.
				TestNearlyEqual(*FString::Printf(TEXT("'%s' knows how thick the wall it discharges through is"),
					*Where), FanActor->Fan.HostWallThickness, Wall->Thickness, 1e-6);

				// Its case is SQUARE and its cowl louvred, so the roll matters: local Y has to be
				// world up on every wall, or the same fan comes out diamond-on with vertical weather
				// blades depending which way its wall happens to run.
				const FVector Up = FanActor->GetActorTransform().TransformVectorNoScale(FVector::YAxisVector);
				TestTrue(*FString::Printf(TEXT("'%s' is set square to the world (up is %s)"),
					*Where, *Up.ToString()), Up.Z > 0.99);

				UDynamicMeshComponent* Rotor = FanActor->GetPartComponent(AHFFanActor::RotorPartId());
				if (TestNotNull(*FString::Printf(TEXT("'%s' has a rotor to line up with"), *Where), Rotor))
				{
					// Wall.BaseZ + SillHeight is how GenerateWall, OpeningCentre and the validator all
					// resolve a sill, so it is the hole's real world centre.
					const double DuctCentreZ = Wall->BaseZ + Expected.SillHeight + Expected.Height * 0.5;
					const double RotorZ = Rotor->GetComponentLocation().Z;

					TestTrue(*FString::Printf(
						TEXT("'%s' cores its duct at the height the fan turns at (hole %.1f, rotor %.1f)"),
						*Where, DuctCentreZ, RotorZ), FMath::IsNearlyEqual(DuctCentreZ, RotorZ, 0.1));
				}
			}
		}

		TestTrue(*FString::Printf(TEXT("'%s' has a duct wide enough to be a duct (%.1f cm)"),
			*Where, Expected.Width), Expected.Width > 5.0);

		// It is smaller than the case that covers it, or the hole shows round the edge of the fan.
		// Corners included: a square hole reaches its half-diagonal, which is what actually pokes out.
		FHFFanParams AsBuilt = FHFFanKit::DefaultsFor(EHFFanKind::Exhaust);
		AsBuilt.SweepDiameter = FMath::Max(Fixture.Footprint.X, Fixture.Footprint.Y) * 0.75;
		AsBuilt = FHFFanKit::Sanitise(AsBuilt);

		TestTrue(*FString::Printf(TEXT("'%s' is covered by its own case (%.1f half-diagonal against %.1f)"),
			*Where, Expected.Width * 0.5 * UE_DOUBLE_SQRT_2, AsBuilt.CaseHalfWidth()),
			Expected.Width * 0.5 * UE_DOUBLE_SQRT_2 < AsBuilt.CaseHalfWidth());

		// THE HOLE IS REALLY CUT. Measured as the volume the wall lost: regenerate the same wall
		// from the same openings with the duct taken out of the list, and the difference is the
		// masonry the duct removed. A list entry nothing acted on would show a difference of zero.
		TArray<FHFOpening> WithoutDuct;
		int32 Ducts = 0;

		for (const FHFOpening& Opening : WallActor->Openings)
		{
			if (Opening.Id == Expected.Id)
			{
				++Ducts;
				continue;
			}
			WithoutDuct.Add(Opening);
		}

		if (!TestEqual(*FString::Printf(TEXT("'%s' put exactly one duct in its wall"), *Where), Ducts, 1))
		{
			continue;
		}

		const double Solid = TMeshQueries<FDynamicMesh3>::GetVolumeArea(
			FHFGenerators::GenerateWall(*Wall, WithoutDuct)).X;
		const double Cored = TMeshQueries<FDynamicMesh3>::GetVolumeArea(
			FHFGenerators::GenerateWall(*Wall, WallActor->Openings)).X;

		// What a hole of this size through this wall has to take out, allowing for the boolean
		// resolving the corners differently from an exact prism.
		const double Nominal = Expected.Width * Expected.Height * Wall->Thickness;

		TestTrue(*FString::Printf(TEXT("'%s' cored a real hole (%.0f cm3 of a nominal %.0f)"),
			*Where, Solid - Cored, Nominal), Solid - Cored > Nominal * 0.8);
		TestTrue(*FString::Printf(TEXT("'%s' took out no more than the hole"), *Where),
			Solid - Cored < Nominal * 1.2);

		TestTrue(*FString::Printf(TEXT("'%s' leaves its wall watertight"), *Where),
			FHFMeshOps::IsClosed(FHFGenerators::GenerateWall(*Wall, WallActor->Openings)));
	}

	TestTrue(TEXT("The flat has extracts to check"), Extracts >= 3);

	return true;
}

/**
 * A ROTOR IS NOT A BLENDER, and it is not invisible to a trace either.
 *
 * The one part in the plugin where "collision follows the visual mesh" and "collision blocks" pull
 * apart. Collision geometry does not spin with the render - the mesh never moves, only the
 * component's transform does - so a blocking rotor is a blade frozen at one azimuth: a pawn walks
 * cleanly through the gap between two blades and hits an invisible wall a few degrees later, at
 * whatever angle the fan was last posed at. Both halves of that are wrong, and which half you get
 * depends on where somebody stopped the fan.
 *
 * So the decision is query-only collision that blocks nothing, built complex-as-simple off the real
 * blades so that traces, editor picking and any measurement of the fan still see them. Asserted
 * against a wardrobe shutter, which is the opposite case and must still block: a door leaf you can
 * walk through is the failure the complex collision existed to prevent in the first place.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFFanCollisionTest,
	"HouseForge.Editor.AFanRotorDoesNotBlockAWalkthrough", HF_TEST_FLAGS)

bool FHFFanCollisionTest::RunTest(const FString& Parameters)
{
	using namespace HouseForgeFan;

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("An editor world is open"), World))
	{
		return false;
	}

	AHFHouseActor* House = World->SpawnActor<AHFHouseActor>();
	if (!TestNotNull(TEXT("A house actor spawns"), House))
	{
		return false;
	}
	ON_SCOPE_EXIT{ if (IsValid(House)) { House->ClearGeometry(); House->Destroy(); } };

	House->SetSpec(FHFSampleHouse::Make2BHK());
	House->BuildGeometry();

	int32 Checked = 0;

	for (const TPair<FName, AHFFanActor*>& Entry : FansIn(House))
	{
		UDynamicMeshComponent* Rotor = Entry.Value->GetPartComponent(AHFFanActor::RotorPartId());
		if (Rotor == nullptr)
		{
			continue;
		}

		++Checked;
		const FString Where = Entry.Key.ToString();

		// Nothing that moves through the room can hit it.
		TestEqual(*FString::Printf(TEXT("'%s' has no physics collision to walk into"), *Where),
			Rotor->GetCollisionEnabled(), ECollisionEnabled::QueryOnly);
		TestEqual(*FString::Printf(TEXT("'%s' does not block a pawn"), *Where),
			Rotor->GetCollisionResponseToChannel(ECC_Pawn), ECR_Ignore);
		TestEqual(*FString::Printf(TEXT("'%s' does not block a camera either"), *Where),
			Rotor->GetCollisionResponseToChannel(ECC_Camera), ECR_Ignore);

		// But it is still there to be traced against and picked in the editor, off the real blades
		// rather than off a box round them.
		TestEqual(*FString::Printf(TEXT("'%s' is still visible to a trace"), *Where),
			Rotor->GetCollisionResponseToChannel(ECC_Visibility), ECR_Block);
		TestEqual(*FString::Printf(TEXT("'%s' traces against its own mesh, not a hull"), *Where),
			Rotor->CollisionType, ECollisionTraceFlag::CTF_UseComplexAsSimple);
		TestTrue(*FString::Printf(TEXT("'%s' actually builds that complex collision"), *Where),
			Rotor->bEnableComplexCollision);

		// And the state says so, so somebody asking "why does the pawn walk through the fan" finds
		// an answer in the details panel rather than in this file.
		const FHFPartState* State = Entry.Value->FindPart(AHFFanActor::RotorPartId());
		if (State != nullptr)
		{
			TestEqual(*FString::Printf(TEXT("'%s' declares itself trace-only"), *Where),
				State->Collision, EHFPartCollision::TraceOnly);
		}
	}

	TestTrue(TEXT("There were rotors in the flat to check"), Checked >= 6);

	// THE OPPOSITE CASE. A wardrobe shutter is posed and then stands still, so collision that matches
	// it exactly is collision that stays true - and a leaf somebody can walk through is precisely
	// what complex-as-simple was added for. If the fan's treatment had leaked onto every part, this
	// is what would say so.
	int32 Blocking = 0;

	for (AActor* Element : House->ElementActors)
	{
		AHFArticulatedActor* Articulated = Cast<AHFArticulatedActor>(Element);
		if (Articulated == nullptr || Cast<AHFFanActor>(Element) != nullptr)
		{
			continue;
		}

		for (const FHFPartState& Part : Articulated->Parts)
		{
			UDynamicMeshComponent* Component = Articulated->GetPartComponent(Part.PartId);
			if (Component == nullptr || !Part.Motion.Opens())
			{
				continue;
			}

			++Blocking;
			TestEqual(*FString::Printf(TEXT("'%s' part '%s' still blocks, because it opens and then stands still"),
				*Articulated->GetName(), *Part.PartId.ToString()),
				Component->GetCollisionEnabled(), ECollisionEnabled::QueryAndPhysics);
		}
	}

	TestTrue(TEXT("There were opening parts in the flat to check against"), Blocking > 0);

	return true;
}

/**
 * A fan that was stopped somewhere goes back there after a rebuild, phase intact.
 *
 * The wardrobe's assertion, for the other kind of pose. A phase is unbounded and an open amount is
 * not, so it travels by a different route through CapturePartPoses - and a rebuild that quietly
 * folded fifty turns into a 0..1 amount would put every fan in the flat back at a different angle
 * with nothing saying so.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFFanSurvivesRebuildTest,
	"HouseForge.Editor.FanPhaseSurvivesAHouseRebuild", HF_TEST_FLAGS)

bool FHFFanSurvivesRebuildTest::RunTest(const FString& Parameters)
{
	using namespace HouseForgeFan;

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("An editor world is open"), World))
	{
		return false;
	}

	AHFHouseActor* House = World->SpawnActor<AHFHouseActor>();
	if (!TestNotNull(TEXT("A house actor spawns"), House))
	{
		return false;
	}
	ON_SCOPE_EXIT{ if (IsValid(House)) { House->ClearGeometry(); House->Destroy(); } };

	House->SetSpec(FHFSampleHouse::Make2BHK());
	House->BuildGeometry();

	const FName FanId(TEXT("F_Fan_Living"));
	const FName RotorId = AHFFanActor::RotorPartId();

	AHFFanActor* Fan = FansIn(House).FindRef(FanId);
	if (!TestNotNull(TEXT("The living room's fan was built"), Fan))
	{
		return false;
	}

	// Twelve and a half turns: past a full revolution on purpose, because that is the case an open
	// amount cannot express and the reason a spin is its own motion type.
	TestTrue(TEXT("A fan takes a phase"), Fan->SetPartSpinTurns(RotorId, 12.5));

	UDynamicMeshComponent* Rotor = Fan->GetPartComponent(RotorId);
	if (!TestNotNull(TEXT("The rotor has a component"), Rotor))
	{
		return false;
	}
	const FQuat Stopped = Rotor->GetRelativeRotation().Quaternion();

	House->BuildGeometry();

	Fan = FansIn(House).FindRef(FanId);
	if (!TestNotNull(TEXT("The fan survives a house rebuild"), Fan))
	{
		return false;
	}

	TestNearlyEqual(TEXT("...with its phase carried whole rather than clamped"),
		Fan->GetPartSpinTurns(RotorId), 12.5, 1e-6);

	Rotor = Fan->GetPartComponent(RotorId);
	if (TestNotNull(TEXT("...and its rotor back on a component"), Rotor))
	{
		// Read off the component, not the number. A phase restored into the state and never pushed
		// into the transform leaves the fan drawn at its start while the details panel says 12.5.
		TestTrue(TEXT("...and the blades drawn back where they were stopped"),
			Rotor->GetRelativeRotation().Quaternion().Equals(Stopped, 0.001));
	}

	// A fan is not an opening, so the flat's "open everything" must not stop one somewhere arbitrary.
	Fan->OpenAllParts();
	TestNearlyEqual(TEXT("Open All leaves a fan's phase exactly where it was"),
		Fan->GetPartSpinTurns(RotorId), 12.5, 1e-6);
	TestNearlyEqual(TEXT("...and gives it no open amount"), Fan->GetPartOpenAmount(RotorId), 0.0, 1e-9);

	return true;
}

#undef HF_TEST_FLAGS

#endif // WITH_DEV_AUTOMATION_TESTS
