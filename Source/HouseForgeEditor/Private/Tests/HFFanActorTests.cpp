// Copyright Siddartha G. All Rights Reserved.

#include "HouseForgeEditor.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Actors/HFFanActor.h"
#include "Actors/HFHouseActor.h"
#include "Components/DynamicMeshComponent.h"
#include "Editor.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
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

	TArray<double> Phases;

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

		Phases.Add(Rotor->SpinTurns);
	}

	// THREE IDENTICAL FANS MUST NOT BE STOPPED ON THE SAME BLADE. They are three copies of one
	// object, which is exactly what a still must not show, and the only cure is a phase that varies
	// per instance. Asserted as a spread rather than against particular numbers: what matters is
	// that they differ, not what they differ by.
	if (Phases.Num() >= 3)
	{
		TSet<double> Distinct;
		for (const double Phase : Phases)
		{
			Distinct.Add(FMath::RoundToDouble(Phase * 1000.0));
		}

		TestEqual(TEXT("No two fans in the flat are stopped on the same blade"),
			Distinct.Num(), Phases.Num());
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

			// Over the spot the drawing marked, which is also the spot the false ceiling was cut for.
			TestTrue(*FString::Printf(TEXT("'%s' hangs where the drawing put it"), *Where),
				FVector2D(Blades.Origin.X, Blades.Origin.Y).Equals(Fixture.Position, 1.0));
		}
		else if (Fixture.Type == EHFFixtureType::ExhaustFan)
		{
			// An extract's axis is horizontal: it blows through a wall, not through a ceiling.
			const FVector Axis = Fan->GetActorTransform().TransformVectorNoScale(FVector::ZAxisVector);
			TestTrue(*FString::Printf(TEXT("'%s' blows horizontally (%s)"), *Where, *Axis.ToString()),
				FMath::Abs(Axis.Z) < 0.01);

			// At the height it was drawn at, which for every extract in this flat is high on the wall
			// and clear of the beam soffits above it.
			const double Centre = FloorZ + Fixture.BaseZ + Fixture.Height * 0.5;
			TestTrue(*FString::Printf(TEXT("'%s' sits at the height it was drawn (%.1f against %.1f)"),
				*Where, Blades.Origin.Z, Centre),
				FMath::IsNearlyEqual(Blades.Origin.Z, Centre, 1.0));

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
