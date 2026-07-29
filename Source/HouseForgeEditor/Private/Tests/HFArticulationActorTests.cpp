// Copyright Siddartha G. All Rights Reserved.

#include "HouseForgeEditor.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Actors/HFArticulatedActor.h"
#include "Actors/HFHouseActor.h"
#include "Actors/HFOpeningActor.h"
#include "Components/DynamicMeshComponent.h"
#include "Editor.h"
#include "Engine/World.h"
#include "MeshQueries.h"
#include "Misc/AutomationTest.h"
#include "Model/HFTypes.h"

using namespace UE::Geometry;

#define HF_TEST_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
	/** A door in a 400 cm wall running along +X, so the wall normal is +Y. */
	AHFOpeningActor* SpawnTestDoor(UWorld* World, EHFOpeningKind Kind = EHFOpeningKind::Door,
		EHFSwing Swing = EHFSwing::InwardLeft)
	{
		AHFOpeningActor* Actor = World->SpawnActor<AHFOpeningActor>();
		if (Actor == nullptr)
		{
			return nullptr;
		}

		Actor->HostWall.Id = TEXT("W1");
		Actor->HostWall.Start = FVector2D(0.0, 0.0);
		Actor->HostWall.End = FVector2D(400.0, 0.0);
		Actor->HostWall.Thickness = 20.0;
		Actor->HostWall.Height = 300.0;

		Actor->Opening.Id = TEXT("D1");
		Actor->Opening.WallId = TEXT("W1");
		Actor->Opening.OffsetAlongWall = 200.0;
		Actor->Opening.Width = 90.0;
		Actor->Opening.Height = 210.0;
		Actor->Opening.Kind = Kind;
		Actor->Opening.Swing = Swing;

		Actor->Regenerate();
		return Actor;
	}

	/** Adds a triangle through the component, exactly as a modelling tool would. */
	void SimulateArtistEdit(UDynamicMeshComponent* Component, double AtZ)
	{
		Component->GetDynamicMesh()->EditMesh([AtZ](FDynamicMesh3& EditMesh)
		{
			const int32 A = EditMesh.AppendVertex(FVector3d(0, 0, AtZ));
			const int32 B = EditMesh.AppendVertex(FVector3d(50, 0, AtZ));
			const int32 C = EditMesh.AppendVertex(FVector3d(0, 50, AtZ));
			EditMesh.AppendTriangle(A, B, C, 1);
		});
		Component->NotifyMeshUpdated();
	}
}

/**
 * A hinged part must sit closed at 0 and at its declared limit at 1.
 *
 * Asserted on where the leaf's far edge actually ends up in the world, because that is the only
 * thing a walkthrough sees. A component whose relative transform is right but whose pivot is at
 * the mesh centre passes any test that reads the transform back instead of a point through it.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFArticulatedHingeTest,
	"HouseForge.Editor.ArticulatedHingeSweep", HF_TEST_FLAGS)

bool FHFArticulatedHingeTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("An editor world is open"), World))
	{
		return false;
	}

	AHFOpeningActor* Door = SpawnTestDoor(World);
	if (!TestNotNull(TEXT("A door actor spawns"), Door))
	{
		return false;
	}
	ON_SCOPE_EXIT{ if (IsValid(Door)) { Door->Destroy(); } };

	if (!TestEqual(TEXT("The door has one moving part"), Door->NumParts(), 1))
	{
		return false;
	}

	UDynamicMeshComponent* Leaf = Door->GetPartComponent(AHFOpeningActor::LeafPartId);
	if (!TestNotNull(TEXT("The leaf has its own component"), Leaf))
	{
		return false;
	}

	// Its own component, not a slice of the shell: that is what lets it move, be baked on its own,
	// and carry its own collision.
	TestTrue(TEXT("The leaf component hangs off the element's root"),
		Leaf->GetAttachParent() == Door->GetMeshComponent());
	TestTrue(TEXT("The leaf component carries geometry"),
		Leaf->GetDynamicMesh()->GetMeshRef().TriangleCount() > 0);
	TestTrue(TEXT("A fresh leaf is not marked as hand-edited"),
		!Door->IsPartArtistEdited(AHFOpeningActor::LeafPartId));

	// The far top corner of the leaf, in leaf-local space.
	const FVector LocalTip(89.5, 0.0, 105.0);

	TestNearlyEqual(TEXT("A new part starts closed"),
		Door->GetPartOpenAmount(AHFOpeningActor::LeafPartId), 0.0, 0.0001);

	const FVector ClosedTip = Leaf->GetComponentTransform().TransformPosition(LocalTip);
	TestTrue(TEXT("Closed, the leaf fills the opening in the plane of the wall"),
		ClosedTip.Equals(FVector(244.5, 0.0, 105.0), 0.05));

	// At the limit, the leaf stands square to the wall, hinged at the near jamb, swung inward.
	TestTrue(TEXT("Opening a part by id succeeds"),
		Door->SetPartOpenAmount(AHFOpeningActor::LeafPartId, 1.0));

	const FVector OpenTip = Leaf->GetComponentTransform().TransformPosition(LocalTip);
	TestTrue(TEXT("Open, the leaf sits at its declared limit"),
		OpenTip.Equals(FVector(155.0, 89.5, 105.0), 0.05));

	// A hinge is a rotation about a fixed line: the leaf's far edge keeps its distance from the
	// hinge line and its height, which a part translated instead of rotated would not.
	const FVector2D HingeLine(155.0, 0.0);
	TestNearlyEqual(TEXT("The leaf stays attached to its hinge line"),
		FVector2D(FVector2D(OpenTip.X, OpenTip.Y) - HingeLine).Size(),
		FVector2D(FVector2D(ClosedTip.X, ClosedTip.Y) - HingeLine).Size(), 0.05);
	TestNearlyEqual(TEXT("Swinging does not change the leaf's height"), OpenTip.Z, ClosedTip.Z, 0.001);

	// Half open is half the angle, so a slider reads linearly rather than snapping.
	Door->SetPartOpenAmount(AHFOpeningActor::LeafPartId, 0.5);
	const FVector HalfTip = Leaf->GetComponentTransform().TransformPosition(LocalTip);
	TestNearlyEqual(TEXT("Half open is 45 degrees off the wall"),
		FMath::RadiansToDegrees(FMath::Atan2(HalfTip.Y - 0.0, HalfTip.X - 155.0)), 45.0, 0.1);

	// Closing puts it back exactly, not approximately.
	Door->CloseAllParts();
	TestTrue(TEXT("Closing returns the leaf exactly to where it was generated"),
		Leaf->GetComponentTransform().TransformPosition(LocalTip).Equals(ClosedTip, 0.001));

	// The actor-level control drives every part at once, which is how a fixture gets checked.
	Door->OpenAllParts();
	TestNearlyEqual(TEXT("Open All drives every part"),
		Door->GetPartOpenAmount(AHFOpeningActor::LeafPartId), 1.0, 0.0001);
	TestNearlyEqual(TEXT("Open All reports itself on the actor"), Door->MasterOpenAmount, 1.0, 0.0001);

	// Unknown ids are refused rather than silently ignored.
	TestFalse(TEXT("Opening a part that does not exist fails"),
		Door->SetPartOpenAmount(TEXT("NoSuchPart"), 1.0));

	return true;
}

/** A sliding part must travel exactly its declared distance, in its own direction. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFArticulatedSlideTest,
	"HouseForge.Editor.ArticulatedSlideTravel", HF_TEST_FLAGS)

bool FHFArticulatedSlideTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("An editor world is open"), World))
	{
		return false;
	}

	AHFOpeningActor* Door = SpawnTestDoor(World, EHFOpeningKind::SlidingDoor, EHFSwing::None);
	if (!TestNotNull(TEXT("A sliding door spawns"), Door))
	{
		return false;
	}
	ON_SCOPE_EXIT{ if (IsValid(Door)) { Door->Destroy(); } };

	UDynamicMeshComponent* Leaf = Door->GetPartComponent(AHFOpeningActor::LeafPartId);
	UDynamicMeshComponent* Fixed = Door->GetPartComponent(AHFOpeningActor::FixedPanelPartId);
	if (!TestNotNull(TEXT("The running panel has its own component"), Leaf) ||
		!TestNotNull(TEXT("The fixed panel has its own component"), Fixed))
	{
		return false;
	}

	const double Travel = Door->FindPart(AHFOpeningActor::LeafPartId)->Motion.MaxTravelCm;
	TestTrue(TEXT("The running panel actually travels"), Travel > 1.0);

	const FVector LocalCentre(24.0, 2.5, 105.0);
	const FVector Closed = Leaf->GetComponentTransform().TransformPosition(LocalCentre);
	const FVector FixedClosed = Fixed->GetComponentTransform().TransformPosition(LocalCentre);

	Door->OpenAllParts();
	const FVector Open = Leaf->GetComponentTransform().TransformPosition(LocalCentre);

	// Travel along the wall, with nothing else disturbed.
	TestNearlyEqual(TEXT("The panel travels its declared distance"), FVector::Distance(Open, Closed), Travel, 0.01);
	TestTrue(TEXT("It travels along the wall and nowhere else"),
		Open.Equals(Closed + FVector(Travel, 0.0, 0.0), 0.01));
	TestTrue(TEXT("A slide does not rotate the part"),
		Leaf->GetComponentTransform().GetRotation().Equals(FQuat::Identity, 0.0001));

	// The fixed panel is a part in its own right, and "open everything" leaves it exactly alone.
	TestTrue(TEXT("Opening the unit does not move its fixed panel"),
		Fixed->GetComponentTransform().TransformPosition(LocalCentre).Equals(FixedClosed, 0.0001));

	// The whole unit stays inside the 155..245 opening; a panel that left it would be in the wall.
	{
		const FTransform& ToWorld = Leaf->GetComponentTransform();
		double MinX = TNumericLimits<double>::Max();
		double MaxX = -TNumericLimits<double>::Max();

		for (const FVector3d& Vertex : Leaf->GetDynamicMesh()->GetMeshRef().VerticesItr())
		{
			const double X = ToWorld.TransformPosition(FVector(Vertex)).X;
			MinX = FMath::Min(MinX, X);
			MaxX = FMath::Max(MaxX, X);
		}

		TestTrue(TEXT("Open, the running panel is still inside the opening"),
			MinX >= 154.99 && MaxX <= 245.01);
	}

	// Half the open amount is half the travel.
	Door->SetPartOpenAmount(AHFOpeningActor::LeafPartId, 0.5);
	TestNearlyEqual(TEXT("Travel is linear in the open amount"),
		FVector::Distance(Leaf->GetComponentTransform().TransformPosition(LocalCentre), Closed), Travel * 0.5, 0.01);

	return true;
}

/**
 * Regeneration rebuilds parts without losing what the user did to them.
 *
 * A wardrobe left open to be photographed must not slam shut because its carcass depth changed,
 * and a hand-detailed shutter must not be quietly replaced.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFArticulatedRegenerationTest,
	"HouseForge.Editor.ArticulatedRegenerationPreservesState", HF_TEST_FLAGS)

bool FHFArticulatedRegenerationTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("An editor world is open"), World))
	{
		return false;
	}

	AHFOpeningActor* Door = SpawnTestDoor(World);
	if (!TestNotNull(TEXT("A door actor spawns"), Door))
	{
		return false;
	}
	ON_SCOPE_EXIT{ if (IsValid(Door)) { Door->Destroy(); } };

	UDynamicMeshComponent* Leaf = Door->GetPartComponent(AHFOpeningActor::LeafPartId);
	if (!TestNotNull(TEXT("The leaf has a component"), Leaf))
	{
		return false;
	}

	Door->SetPartOpenAmount(AHFOpeningActor::LeafPartId, 0.6);

	// A parameter change: the leaf must be rebuilt taller but stay 60% open.
	Door->Opening.Height = 240.0;
	Door->Regenerate();

	TestNearlyEqual(TEXT("An open part stays open across a regeneration"),
		Door->GetPartOpenAmount(AHFOpeningActor::LeafPartId), 0.6, 0.0001);

	UDynamicMeshComponent* Rebuilt = Door->GetPartComponent(AHFOpeningActor::LeafPartId);
	if (!TestNotNull(TEXT("The leaf still has a component after regeneration"), Rebuilt))
	{
		return false;
	}
	TestTrue(TEXT("Regeneration reuses the part's component rather than replacing it"), Rebuilt == Leaf);

	// The geometry did change, measured as the solid it is rather than as a triangle count.
	const double ExpectedVolume = (90.0 - 1.0) * 4.0 * (240.0 - 1.0);
	TestNearlyEqual(TEXT("The part rebuilt at the new height"),
		TMeshQueries<FDynamicMesh3>::GetVolumeArea(Rebuilt->GetDynamicMesh()->GetMeshRef()).X,
		ExpectedVolume, ExpectedVolume * 0.01);

	// And the pose still corresponds to 60% of a right angle, using the new pivot.
	const FVector Tip = Rebuilt->GetComponentTransform().TransformPosition(FVector(89.5, 0.0, 10.0));
	TestNearlyEqual(TEXT("The part is still posed where it was left"),
		FMath::RadiansToDegrees(FMath::Atan2(Tip.Y - 0.0, Tip.X - 155.0)), 54.0, 0.1);

	// Regeneration on its own does not make anything look hand-edited.
	TestFalse(TEXT("Regeneration does not mark parts as hand-edited"),
		Door->IsPartArtistEdited(AHFOpeningActor::LeafPartId));
	TestFalse(TEXT("An untouched element is not preserved on rebuild"), Door->ShouldPreserveOnRebuild());

	return true;
}

/** A hand-edited part opts out of regeneration, exactly as a hand-edited element does. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFArticulatedPartEditTest,
	"HouseForge.Editor.ArticulatedPartEditsSurvive", HF_TEST_FLAGS)

bool FHFArticulatedPartEditTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("An editor world is open"), World))
	{
		return false;
	}

	AHFOpeningActor* Door = SpawnTestDoor(World);
	if (!TestNotNull(TEXT("A door actor spawns"), Door))
	{
		return false;
	}
	ON_SCOPE_EXIT{ if (IsValid(Door)) { Door->Destroy(); } };

	UDynamicMeshComponent* Leaf = Door->GetPartComponent(AHFOpeningActor::LeafPartId);
	if (!TestNotNull(TEXT("The leaf has a component"), Leaf))
	{
		return false;
	}

	// Stand in for the Modeling Tools, adding geometry 900 cm up so it is unmistakable in bounds.
	SimulateArtistEdit(Leaf, 900.0);

	TestTrue(TEXT("Editing a part's mesh marks that part as hand-edited"),
		Door->IsPartArtistEdited(AHFOpeningActor::LeafPartId));
	TestFalse(TEXT("Editing a part does not mark the fixed shell as hand-edited"), Door->bArtistEdited);
	TestTrue(TEXT("An element with a hand-edited part survives a house rebuild"),
		Door->ShouldPreserveOnRebuild());

	const FAxisAlignedBox3d Edited = Leaf->GetDynamicMesh()->GetMeshRef().GetBounds();
	TestTrue(TEXT("The edit is present in the part's geometry"), Edited.Max.Z > 899.0);

	// The load-bearing assertion: a parameter change must not take the modelling work with it.
	Door->Opening.Width = 120.0;
	Door->Regenerate();

	const FAxisAlignedBox3d AfterRegenerate = Leaf->GetDynamicMesh()->GetMeshRef().GetBounds();
	TestTrue(TEXT("Regeneration does not overwrite a hand-edited part"),
		AfterRegenerate.Min.Equals(Edited.Min, 0.01) && AfterRegenerate.Max.Equals(Edited.Max, 0.01));

	// A part that generation stops producing is kept if it was hand-edited. Dropping it would be
	// the same unrecoverable loss as regenerating over it.
	Door->Opening.Kind = EHFOpeningKind::Archway;
	Door->Regenerate();

	TestTrue(TEXT("A hand-edited part is kept even when it is no longer generated"),
		Door->GetPartComponent(AHFOpeningActor::LeafPartId) != nullptr);
	TestTrue(TEXT("It is still marked as hand-edited"),
		Door->IsPartArtistEdited(AHFOpeningActor::LeafPartId));

	// Reverting is the only thing allowed to discard it, and it does so completely.
	Door->Opening.Kind = EHFOpeningKind::Door;
	Door->RevertToGenerated();

	TestFalse(TEXT("Reverting clears the part's hand-edited flag"),
		Door->IsPartArtistEdited(AHFOpeningActor::LeafPartId));

	UDynamicMeshComponent* Reverted = Door->GetPartComponent(AHFOpeningActor::LeafPartId);
	if (TestNotNull(TEXT("Reverting leaves a generated part behind"), Reverted))
	{
		const double ExpectedVolume = (120.0 - 1.0) * 4.0 * (210.0 - 1.0);
		TestNearlyEqual(TEXT("Reverting restores generated geometry at the current parameters"),
			TMeshQueries<FDynamicMesh3>::GetVolumeArea(Reverted->GetDynamicMesh()->GetMeshRef()).X,
			ExpectedVolume, ExpectedVolume * 0.01);
		TestTrue(TEXT("Reverting discards the hand-modelled geometry"),
			Reverted->GetDynamicMesh()->GetMeshRef().GetBounds().Max.Z < 300.0);
	}

	TestFalse(TEXT("A reverted element no longer needs preserving"), Door->ShouldPreserveOnRebuild());

	return true;
}

/**
 * Collision follows the leaf, so a walkthrough cannot walk through an open door.
 *
 * The requirement in .claude/rules/04-conventions.md is explicit about open doors, and it is easy
 * to fail without noticing: the leaf renders where it swung to while its collision stays behind in
 * the doorway, and every screenshot looks right while the flat is unwalkable. Asserted by tracing
 * against the part's own body rather than by reading its settings back, because settings that look
 * correct and a body that never moved is exactly the failure.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFOpenDoorCollisionTest,
	"HouseForge.Editor.OpenDoorBlocksAWalkthrough", HF_TEST_FLAGS)

bool FHFOpenDoorCollisionTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("An editor world is open"), World))
	{
		return false;
	}

	AHFOpeningActor* Door = SpawnTestDoor(World);
	if (!TestNotNull(TEXT("A door actor spawns"), Door))
	{
		return false;
	}
	ON_SCOPE_EXIT{ if (IsValid(Door)) { Door->Destroy(); } };

	UDynamicMeshComponent* Leaf = Door->GetPartComponent(AHFOpeningActor::LeafPartId);
	if (!TestNotNull(TEXT("The leaf has a component"), Leaf))
	{
		return false;
	}

	// Complex-as-simple is what makes collision the visual mesh rather than a hull approximating it.
	TestTrue(TEXT("The leaf collides with its own triangles"),
		Leaf->CollisionType == ECollisionTraceFlag::CTF_UseComplexAsSimple);
	TestTrue(TEXT("The leaf blocks queries and physics"),
		Leaf->GetCollisionEnabled() == ECollisionEnabled::QueryAndPhysics);

	// Movable, or moving it would leave its body behind - and it is only ever moved.
	TestTrue(TEXT("A part that moves is not static"), Leaf->Mobility == EComponentMobility::Movable);

	const FCollisionQueryParams TraceParams(TEXT("HFDoorCollision"), /*bTraceComplex*/ true);

	// The doorway spans 155..245 along the wall; the leaf hangs at 155 and swings to +Y.
	const FVector ThroughDoorwayStart(200.0, -60.0, 100.0);
	const FVector ThroughDoorwayEnd(200.0, 60.0, 100.0);

	// Where the leaf stands once it is open: across the room, 45 cm off the wall.
	const FVector AcrossSwingStart(100.0, 45.0, 100.0);
	const FVector AcrossSwingEnd(260.0, 45.0, 100.0);

	FHitResult Hit;
	TestTrue(TEXT("Closed, the leaf blocks the doorway"),
		Leaf->LineTraceComponent(Hit, ThroughDoorwayStart, ThroughDoorwayEnd, TraceParams));
	TestFalse(TEXT("Closed, nothing is standing out in the room"),
		Leaf->LineTraceComponent(Hit, AcrossSwingStart, AcrossSwingEnd, TraceParams));

	Door->OpenAllParts();

	// The load-bearing pair: collision left the doorway and arrived where the leaf now is. Either
	// half on its own passes with a body that never moved or with one that vanished.
	TestFalse(TEXT("Open, the doorway is walkable"),
		Leaf->LineTraceComponent(Hit, ThroughDoorwayStart, ThroughDoorwayEnd, TraceParams));

	if (TestTrue(TEXT("Open, the leaf blocks where it now stands"),
		Leaf->LineTraceComponent(Hit, AcrossSwingStart, AcrossSwingEnd, TraceParams)))
	{
		// And it blocks at the hinge jamb, not somewhere a stale body happens to overlap.
		TestNearlyEqual(TEXT("It blocks at the leaf's real position"), Hit.ImpactPoint.X, 155.0, 5.0);
	}

	// Halfway is blocked too: a walkthrough must not slip past a door that is merely ajar.
	Door->SetPartOpenAmount(AHFOpeningActor::LeafPartId, 0.5);
	TestTrue(TEXT("Half open, the leaf still blocks the doorway"),
		Leaf->LineTraceComponent(Hit, ThroughDoorwayStart, ThroughDoorwayEnd, TraceParams));

	return true;
}

/**
 * Open amounts survive a house rebuild.
 *
 * A rebuild destroys and respawns every element that is not hand-edited, so a pose held only on the
 * actor dies with it: every door in the flat slams shut the moment anything about the spec changes.
 * Posing is user state in the same way a hand edit is - someone opened those doors deliberately -
 * and this is the test that says so.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFRebuildKeepsOpenAmountsTest,
	"HouseForge.Editor.OpenAmountsSurviveAHouseRebuild", HF_TEST_FLAGS)

bool FHFRebuildKeepsOpenAmountsTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("An editor world is open"), World))
	{
		return false;
	}

	FHFHouseSpec Spec;
	Spec.SchemaVersion = 1;
	Spec.Name = TEXT("Rebuild Pose Test");
	Spec.Units = EHFUnits::Centimeters;
	Spec.UnitsSource = TEXT("test fixture");

	FHFWall Wall;
	Wall.Id = TEXT("W_S");
	Wall.Start = FVector2D(0.0, 0.0);
	Wall.End = FVector2D(400.0, 0.0);
	Wall.Thickness = 20.0;
	Wall.Height = 300.0;
	Spec.Walls.Add(Wall);

	FHFOpening Door;
	Door.Id = TEXT("D1");
	Door.WallId = TEXT("W_S");
	Door.OffsetAlongWall = 200.0;
	Door.Width = 90.0;
	Door.Height = 210.0;
	Door.Kind = EHFOpeningKind::Door;
	Door.Swing = EHFSwing::InwardLeft;
	Spec.Openings.Add(Door);

	AHFHouseActor* House = World->SpawnActor<AHFHouseActor>();
	if (!TestNotNull(TEXT("A house actor spawns"), House))
	{
		return false;
	}
	ON_SCOPE_EXIT{ if (IsValid(House)) { House->ClearGeometry(); House->Destroy(); } };

	House->SetSpec(Spec);
	House->BuildGeometry();

	auto FindDoorActor = [House]() -> AHFOpeningActor*
	{
		for (AActor* Element : House->ElementActors)
		{
			if (AHFOpeningActor* Opening = Cast<AHFOpeningActor>(Element))
			{
				if (Opening->ElementId == FName(TEXT("D1")))
				{
					return Opening;
				}
			}
		}
		return nullptr;
	};

	AHFOpeningActor* Posed = FindDoorActor();
	if (!TestNotNull(TEXT("The house built a door"), Posed))
	{
		return false;
	}

	TestTrue(TEXT("The door can be posed"), Posed->SetPartOpenAmount(AHFOpeningActor::LeafPartId, 0.7));

	UDynamicMeshComponent* PosedLeaf = Posed->GetPartComponent(AHFOpeningActor::LeafPartId);
	if (!TestNotNull(TEXT("The posed door has a leaf"), PosedLeaf))
	{
		return false;
	}
	const FVector PosedTip = PosedLeaf->GetComponentTransform().TransformPosition(FVector(89.5, 0.0, 105.0));

	House->BuildGeometry();

	AHFOpeningActor* Rebuilt = FindDoorActor();
	if (!TestNotNull(TEXT("The door is still there after a rebuild"), Rebuilt))
	{
		return false;
	}

	// The point of the test: this is a different actor. An untouched element is respawned, so the
	// pose had to be carried across rather than merely left alone.
	TestTrue(TEXT("An untouched element really is respawned by a rebuild"), Rebuilt != Posed);

	TestNearlyEqual(TEXT("The door is still 70% open after a house rebuild"),
		Rebuilt->GetPartOpenAmount(AHFOpeningActor::LeafPartId), 0.7, 0.0001);

	// Asserted on where the leaf actually is, not only on the number: a restored open amount that
	// was never pushed into the component leaves the door drawn shut.
	UDynamicMeshComponent* RebuiltLeaf = Rebuilt->GetPartComponent(AHFOpeningActor::LeafPartId);
	if (TestNotNull(TEXT("The rebuilt door has a leaf"), RebuiltLeaf))
	{
		TestTrue(TEXT("The rebuilt leaf is standing where the posed one was"),
			RebuiltLeaf->GetComponentTransform().TransformPosition(FVector(89.5, 0.0, 105.0)).Equals(PosedTip, 0.01));
	}

	return true;
}

/**
 * A geared part follows its driver, whichever way the driver was moved.
 *
 * This is what makes a three-member drawer runner work in the editor rather than only in a test
 * that poses both by hand. The intermediate member travels half as far as the drawer it carries,
 * and it must travel that half whenever the drawer moves at all - a drawer pulled out while its
 * intermediate stayed behind is a drawer hanging on nothing, and nothing about it would log.
 *
 * Set up here on a sliding door rather than on a drawer, because the gearing lives in the actor and
 * not in the kit: what is being tested is that every route into a pose honours it.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFGearedPartFollowsDriverTest,
	"HouseForge.Editor.GearedPartFollowsItsDriver", HF_TEST_FLAGS)

bool FHFGearedPartFollowsDriverTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("An editor world is open"), World))
	{
		return false;
	}

	AHFOpeningActor* Door = SpawnTestDoor(World, EHFOpeningKind::SlidingDoor);
	if (!TestNotNull(TEXT("A sliding door spawns"), Door))
	{
		return false;
	}

	ON_SCOPE_EXIT{ if (IsValid(Door)) { Door->Destroy(); } };

	if (!TestEqual(TEXT("A sliding door has two parts"), Door->Parts.Num(), 2))
	{
		return false;
	}

	// Find the panel that actually slides, and gear the other one to it at half its travel.
	int32 Driver = INDEX_NONE;
	for (int32 Index = 0; Index < Door->Parts.Num(); ++Index)
	{
		if (Door->Parts[Index].Motion.Type == EHFMotionType::Slide)
		{
			Driver = Index;
			break;
		}
	}
	if (!TestTrue(TEXT("One of them slides"), Driver != INDEX_NONE))
	{
		return false;
	}

	const int32 Geared = 1 - Driver;
	const double DriverTravel = Door->Parts[Driver].Motion.MaxTravelCm;
	const double GearedTravel = DriverTravel * 0.5;

	Door->Parts[Geared].Motion = Door->Parts[Driver].Motion;
	Door->Parts[Geared].Motion.MaxTravelCm = GearedTravel;
	Door->Parts[Geared].Motion.DrivenByPartId = Door->Parts[Driver].PartId;

	const FName DriverId = Door->Parts[Driver].PartId;
	const FName GearedId = Door->Parts[Geared].PartId;

	UDynamicMeshComponent* GearedComponent = Door->GetPartComponent(GearedId);
	if (!TestNotNull(TEXT("The geared part has a component"), GearedComponent))
	{
		return false;
	}

	Door->SetPartOpenAmount(DriverId, 0.0);
	const FVector Shut = GearedComponent->GetRelativeLocation();

	// Moving the DRIVER, and reading the geared part's component - not its number.
	Door->SetPartOpenAmount(DriverId, 1.0);
	TestNearlyEqual(TEXT("Opening the driver carries the geared part with it"),
		Door->GetPartOpenAmount(GearedId), 1.0, 1e-9);
	TestNearlyEqual(TEXT("And it travels exactly its own share of the way, not the driver's"),
		FVector::Distance(GearedComponent->GetRelativeLocation(), Shut), GearedTravel, 1e-3);

	// Halfway, so this cannot pass on the two endpoints alone.
	Door->SetPartOpenAmount(DriverId, 0.5);
	TestNearlyEqual(TEXT("It follows the driver partway too"),
		FVector::Distance(GearedComponent->GetRelativeLocation(), Shut), GearedTravel * 0.5, 1e-3);

	// A geared part must not be posable on its own: whatever is written to it, the driver wins.
	Door->SetPartOpenAmount(GearedId, 1.0);
	TestNearlyEqual(TEXT("A geared part cannot be posed away from its driver"),
		Door->GetPartOpenAmount(GearedId), 0.5, 1e-9);

	// And the other routes into a pose honour it as well, or a rebuild would leave it behind.
	Door->SetAllPartsOpenAmount(0.0);
	TestNearlyEqual(TEXT("A master close takes the geared part home"),
		FVector::Distance(GearedComponent->GetRelativeLocation(), Shut), 0.0, 1e-3);

	FHFPartPoses Poses;
	Poses.OpenAmountsByPartId.Add(DriverId, 1.0);
	Door->RestorePartPoses(Poses);
	TestNearlyEqual(TEXT("Restoring a pose after a rebuild carries the geared part too"),
		FVector::Distance(GearedComponent->GetRelativeLocation(), Shut), GearedTravel, 1e-3);

	return true;
}

#undef HF_TEST_FLAGS

#endif // WITH_DEV_AUTOMATION_TESTS
