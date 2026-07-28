// Copyright Siddartha G. All Rights Reserved.

#include "HouseForgeEditor.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Actors/HFArticulatedActor.h"
#include "Actors/HFOpeningActor.h"
#include "Components/DynamicMeshComponent.h"
#include "Editor.h"
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
	if (!TestNotNull(TEXT("The sliding leaf has its own component"), Leaf))
	{
		return false;
	}

	const FVector LocalCentre(45.0, 0.0, 105.0);
	const FVector Closed = Leaf->GetComponentTransform().TransformPosition(LocalCentre);

	Door->SetPartOpenAmount(AHFOpeningActor::LeafPartId, 1.0);
	const FVector Open = Leaf->GetComponentTransform().TransformPosition(LocalCentre);

	// 90 cm of travel, along the wall, with nothing else disturbed.
	TestNearlyEqual(TEXT("The leaf travels its declared distance"), FVector::Distance(Open, Closed), 90.0, 0.01);
	TestTrue(TEXT("It travels along the wall and nowhere else"),
		Open.Equals(Closed + FVector(90.0, 0.0, 0.0), 0.01));
	TestTrue(TEXT("A slide does not rotate the part"),
		Leaf->GetComponentTransform().GetRotation().Equals(FQuat::Identity, 0.0001));

	// Half the open amount is half the travel.
	Door->SetPartOpenAmount(AHFOpeningActor::LeafPartId, 0.5);
	TestNearlyEqual(TEXT("Travel is linear in the open amount"),
		FVector::Distance(Leaf->GetComponentTransform().TransformPosition(LocalCentre), Closed), 45.0, 0.01);

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

#undef HF_TEST_FLAGS

#endif // WITH_DEV_AUTOMATION_TESTS
