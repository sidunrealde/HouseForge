// Copyright Siddartha G. All Rights Reserved.

#include "HouseForgeEditor.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Actors/HFHouseActor.h"
#include "Actors/HFWardrobeActor.h"
#include "Components/DynamicMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Editor.h"
#include "UDynamicMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"
#include "Model/HFSampleHouse.h"
#include "Model/HFTypes.h"

using namespace UE::Geometry;

#define HF_TEST_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// ---------------------------------------------------------------------------------------------
//
// THE SEAM between the joinery kit and the articulation framework.
//
// Both sides of it were already proven, and separately. Every part of the kit is measured as a mesh
// by the tests under Private/Tests, and AHFArticulatedActor is exercised end to end by doors and
// sliding sashes. Nothing had ever joined the two: no UDynamicMeshComponent had been built from a
// kit mesh, no collision cooked from one, no hand-edit flag raised on a shutter, no pose carried
// across a rebuild by a fixture. A wardrobe could have been correct in every mesh it produced and
// still have been a decoration you walk through.
//
// So these tests are deliberately not about geometry. They are about what happens to that geometry
// once it is on an actor in a world: components, physics bodies, poses and edit protection.
//
// ---------------------------------------------------------------------------------------------

namespace HouseForgeWardrobe
{
	/**
	 * Somewhere nothing else in the suite builds.
	 *
	 * Every test in a run shares one editor world, and an actor destroyed by an earlier test keeps
	 * its physics body until the world next ticks. Several test files build geometry around the
	 * origin, so a trace there can be answered by a door that was destroyed two tests ago - which
	 * happened while the walkthrough tests were being written and read convincingly as a collision
	 * defect. Three thousand centimetres away, nothing else has ever stood.
	 */
	const FVector TestSite(-3000.0, -3000.0, 0.0);

	/** A three-bay hinged wardrobe on its own, out of everybody's way. */
	AHFWardrobeActor* SpawnTestWardrobe(UWorld* World, EHFShutterMotion Motion = EHFShutterMotion::SideHung)
	{
		AHFWardrobeActor* Actor = World->SpawnActor<AHFWardrobeActor>(TestSite, FRotator::ZeroRotator);
		if (Actor == nullptr)
		{
			return nullptr;
		}

		Actor->Wardrobe.Width = 180.0;
		Actor->Wardrobe.Depth = 60.0;
		Actor->Wardrobe.Height = 210.0;
		Actor->Wardrobe.BayCount = 3;
		Actor->Wardrobe.PlinthHeight = 10.0;
		Actor->Wardrobe.bHangingRail = true;
		Actor->Wardrobe.MotionKind = Motion;
		Actor->Wardrobe.HandleStyle = EHFHandleStyle::Bar;

		Actor->Regenerate();
		return Actor;
	}

	/** Adds a triangle through the component, exactly as a modelling tool would. */
	void SimulateArtistEdit(UDynamicMeshComponent* Component, double AtZ)
	{
		Component->GetDynamicMesh()->EditMesh([AtZ](FDynamicMesh3& EditMesh)
		{
			const int32 A = EditMesh.AppendVertex(FVector3d(0, 0, AtZ));
			const int32 B = EditMesh.AppendVertex(FVector3d(30, 0, AtZ));
			const int32 C = EditMesh.AppendVertex(FVector3d(0, 30, AtZ));
			EditMesh.AppendTriangle(A, B, C, 1);
		});
		Component->NotifyMeshUpdated();
	}

	/**
	 * The trace a walking character makes: the world, a pawn's channel, and SIMPLE collision.
	 *
	 * Both halves are deliberate, and both are what the rest of this plugin learned the hard way.
	 *
	 * UPrimitiveComponent::LineTraceComponent goes straight to one component's body and never asks
	 * the world, so a body absent from the physics scene - or one whose profile does not block the
	 * channel a character sweeps on - passes it while a character walks straight through.
	 *
	 * And bTraceComplex FALSE is the bigger one. A UDynamicMeshComponent has no simple shapes at all,
	 * so what makes a shutter solid is CTF_UseComplexAsSimple redirecting the simple query onto the
	 * triangle mesh. Ask for the complex geometry explicitly and the assertion passes whether or not
	 * that redirect works, which is to say it proves nothing about a walkthrough.
	 */
	bool WalkTraceHits(UWorld* World, const FVector& Start, const FVector& End, FHitResult& OutHit)
	{
		const FCollisionQueryParams Params(TEXT("HFWardrobe"), /*bTraceComplex*/ false);
		return World->LineTraceSingleByChannel(OutHit, Start, End, ECC_Pawn, Params);
	}

	/**
	 * A short line straight at the middle of a leaf, from outside it.
	 *
	 * Taken off the component's LIVE transform rather than worked out from the wardrobe's dimensions,
	 * so it aims at wherever the leaf has actually swung to and needs no arithmetic of its own. It
	 * stops half a centimetre inside the leaf's front face, which keeps the whole line in front of the
	 * carcass while the leaf is shut - so a shelf or a partition cannot answer for it.
	 */
	void AimAtLeaf(const UDynamicMeshComponent& Leaf, double AlongLeaf, double UpLeaf,
		FVector& OutStart, FVector& OutEnd)
	{
		const FTransform Pose = Leaf.GetComponentTransform();

		// Mid-board, so the point is inside the leaf whichever face is nearer.
		const FVector Middle = Pose.TransformPosition(FVector(AlongLeaf, 0.95, UpLeaf));

		// Out of the wardrobe is the leaf's own -Y, for a leaf of either hand: every panel this kit
		// generates carries its board on +Y of its pivot.
		const FVector Outward = Pose.TransformVectorNoScale(FVector(0.0, -1.0, 0.0)).GetSafeNormal();

		OutStart = Middle + Outward * 8.0;
		OutEnd = Middle - Outward * 0.5;
	}
}

using namespace HouseForgeWardrobe;

// ---------------------------------------------------------------------------------------------

/**
 * Every leaf is a real component, built from a kit mesh.
 *
 * The first thing that had never happened. FHFJoineryKit produced meshes and FHFMeshParts, and
 * nothing ever turned one into a UDynamicMeshComponent - so "a wardrobe's shutters are separate
 * components with their own pivots" was a design statement rather than a fact about anything.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFWardrobePartComponentsTest,
	"HouseForge.Editor.WardrobePartComponents", HF_TEST_FLAGS)

bool FHFWardrobePartComponentsTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("An editor world is open"), World))
	{
		return false;
	}

	AHFWardrobeActor* Wardrobe = SpawnTestWardrobe(World);
	if (!TestNotNull(TEXT("A wardrobe actor spawns"), Wardrobe))
	{
		return false;
	}
	ON_SCOPE_EXIT{ if (IsValid(Wardrobe)) { Wardrobe->Destroy(); } };

	if (!TestEqual(TEXT("A three-bay hinged wardrobe has three moving parts"), Wardrobe->NumParts(), 3))
	{
		return false;
	}

	// The fixed shell is there too, and is not merely the leaves in disguise.
	UDynamicMeshComponent* Shell = Wardrobe->GetMeshComponent();
	if (!TestNotNull(TEXT("The wardrobe has a shell component"), Shell))
	{
		return false;
	}
	TestTrue(TEXT("The shell carries the carcass, plinth and shelves"),
		Shell->GetDynamicMesh()->GetMeshRef().TriangleCount() > 0);

	for (int32 Bay = 0; Bay < 3; ++Bay)
	{
		const FName PartId = AHFWardrobeActor::ShutterPartId(Bay);
		const FString Where = FString::Printf(TEXT("bay %d"), Bay);

		UDynamicMeshComponent* Leaf = Wardrobe->GetPartComponent(PartId);
		if (!TestNotNull(*FString::Printf(TEXT("The leaf in %s has its own component"), *Where), Leaf))
		{
			return false;
		}

		// Its own component, not a slice of the shell: that is what lets it move, be baked on its own
		// and carry its own collision.
		TestTrue(*FString::Printf(TEXT("The leaf in %s hangs off the element's root"), *Where),
			Leaf->GetAttachParent() == Shell);
		TestTrue(*FString::Printf(TEXT("The leaf in %s carries geometry"), *Where),
			Leaf->GetDynamicMesh()->GetMeshRef().TriangleCount() > 0);
		TestFalse(*FString::Printf(TEXT("A freshly generated leaf in %s is not hand-edited"), *Where),
			Wardrobe->IsPartArtistEdited(PartId));
		TestNearlyEqual(*FString::Printf(TEXT("A new leaf in %s starts closed"), *Where),
			Wardrobe->GetPartOpenAmount(PartId), 0.0, 1e-6);

		// And it is the leaf, not a copy of the shell that happens to be parented separately.
		TestTrue(*FString::Printf(TEXT("The leaf in %s is a leaf rather than the whole carcass"), *Where),
			Leaf->GetDynamicMesh()->GetMeshRef().TriangleCount()
				< Shell->GetDynamicMesh()->GetMeshRef().TriangleCount());
	}

	// Every leaf is somewhere different, which is the cheapest way to catch three parts sharing one
	// pivot - an assembly that looks like one shutter and reports three.
	const FVector First = Wardrobe->GetPartComponent(AHFWardrobeActor::ShutterPartId(0))->GetComponentLocation();
	const FVector Last = Wardrobe->GetPartComponent(AHFWardrobeActor::ShutterPartId(2))->GetComponentLocation();
	TestTrue(TEXT("The leaves are hung at different places along the run"),
		FMath::Abs(Last.X - First.X) > 100.0);

	// Opening one leaf leaves the others where they are. Per-part posing is the whole point of a part.
	Wardrobe->SetPartOpenAmount(AHFWardrobeActor::ShutterPartId(0), 1.0);

	TestNearlyEqual(TEXT("The leaf that was opened is open"),
		Wardrobe->GetPartOpenAmount(AHFWardrobeActor::ShutterPartId(0)), 1.0, 1e-6);
	TestNearlyEqual(TEXT("Its neighbour stayed shut"),
		Wardrobe->GetPartOpenAmount(AHFWardrobeActor::ShutterPartId(1)), 0.0, 1e-6);

	return true;
}

/**
 * A closed shutter blocks a walking trace, and the volume that blocks it MOVES when it opens.
 *
 * The second half is the one worth having. Collision that is cooked once and left behind at the
 * closed pose passes every "is the wardrobe solid" check ever written, and produces a walkthrough
 * where an open wardrobe still has an invisible door across it and the leaf itself is a ghost.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFWardrobeShutterBlocksTest,
	"HouseForge.Editor.WardrobeShutterBlocks", HF_TEST_FLAGS)

bool FHFWardrobeShutterBlocksTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("An editor world is open"), World))
	{
		return false;
	}

	AHFWardrobeActor* Wardrobe = SpawnTestWardrobe(World);
	if (!TestNotNull(TEXT("A wardrobe actor spawns"), Wardrobe))
	{
		return false;
	}
	ON_SCOPE_EXIT{ if (IsValid(Wardrobe)) { Wardrobe->Destroy(); } };

	const FName PartId = AHFWardrobeActor::ShutterPartId(0);
	UDynamicMeshComponent* Leaf = Wardrobe->GetPartComponent(PartId);
	if (!TestNotNull(TEXT("The first leaf has a component"), Leaf))
	{
		return false;
	}

	// Halfway along the leaf and at hand height, clear of the handle on its leading edge.
	constexpr double AlongLeaf = 25.0;
	constexpr double UpLeaf = 100.0;

	FVector ClosedStart, ClosedEnd;
	AimAtLeaf(*Leaf, AlongLeaf, UpLeaf, ClosedStart, ClosedEnd);

	FHitResult Hit;
	if (!TestTrue(TEXT("A walking trace at a closed shutter is blocked"),
		WalkTraceHits(World, ClosedStart, ClosedEnd, Hit)))
	{
		return false;
	}

	// The leaf itself answered, not the carcass behind it and not something left over from another
	// test. Collision is cooked from the KIT MESH on the leaf's own component, which is the thing
	// that had never been true.
	TestEqual(TEXT("It is the shutter that blocks, not the carcass"), Hit.GetActor(), Cast<AActor>(Wardrobe));
	TestEqual(TEXT("The blocking body is the shutter's own component"),
		Hit.GetComponent(), Cast<UPrimitiveComponent>(Leaf));

	// ------------------------------------------------------------------------------ and it moves

	Wardrobe->SetPartOpenAmount(PartId, 1.0);

	FHitResult AfterOpening;
	const bool bStillBlocked = WalkTraceHits(World, ClosedStart, ClosedEnd, AfterOpening);
	TestFalse(TEXT("With the shutter open, where it used to be is walk-through"),
		bStillBlocked && AfterOpening.GetComponent() == Leaf);

	// And it is solid where it has swung to, rather than having become a ghost.
	FVector OpenStart, OpenEnd;
	AimAtLeaf(*Leaf, AlongLeaf, UpLeaf, OpenStart, OpenEnd);

	TestTrue(TEXT("The open leaf stands somewhere else entirely"),
		FVector::Dist(OpenEnd, ClosedEnd) > 20.0);

	FHitResult AtNewPlace;
	if (!TestTrue(TEXT("An open shutter blocks where it now stands"),
		WalkTraceHits(World, OpenStart, OpenEnd, AtNewPlace)))
	{
		return false;
	}
	TestEqual(TEXT("And it is still the shutter that blocks"),
		AtNewPlace.GetComponent(), Cast<UPrimitiveComponent>(Leaf));

	// Shut it again and the leaf's new position is empty, which is the other half of "the volume
	// moved": a body that was merely ADDED at the open pose would still answer here.
	Wardrobe->SetPartOpenAmount(PartId, 0.0);

	FHitResult AfterClosing;
	const bool bGhostLeftBehind = WalkTraceHits(World, OpenStart, OpenEnd, AfterClosing);
	TestFalse(TEXT("Closing the shutter takes its collision back with it"),
		bGhostLeftBehind && AfterClosing.GetComponent() == Leaf);

	return true;
}

/**
 * A wardrobe left open stays open through a regeneration and through a house rebuild.
 *
 * Posing is user state in exactly the way a hand edit is - somebody opened those shutters on
 * purpose, usually to photograph them. A rebuild that reset them would be a small silent loss every
 * time a parameter changed.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFWardrobePosesSurviveTest,
	"HouseForge.Editor.WardrobePosesSurviveRegeneration", HF_TEST_FLAGS)

bool FHFWardrobePosesSurviveTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("An editor world is open"), World))
	{
		return false;
	}

	AHFWardrobeActor* Wardrobe = SpawnTestWardrobe(World);
	if (!TestNotNull(TEXT("A wardrobe actor spawns"), Wardrobe))
	{
		return false;
	}
	ON_SCOPE_EXIT{ if (IsValid(Wardrobe)) { Wardrobe->Destroy(); } };

	const FName Posed = AHFWardrobeActor::ShutterPartId(1);
	Wardrobe->SetPartOpenAmount(Posed, 0.6);

	UDynamicMeshComponent* Leaf = Wardrobe->GetPartComponent(Posed);
	if (!TestNotNull(TEXT("The posed leaf has a component"), Leaf))
	{
		return false;
	}
	const FVector Swung = Leaf->GetComponentTransform().TransformPosition(FVector(40.0, 0.0, 100.0));

	// A shape change, which is what a regeneration is. The pose is not a shape.
	Wardrobe->Wardrobe.Depth = 65.0;
	Wardrobe->Regenerate();

	TestNearlyEqual(TEXT("A shutter left open is still open after a regeneration"),
		Wardrobe->GetPartOpenAmount(Posed), 0.6, 1e-6);

	// Asserted on where the leaf actually is, not on the number. A part whose open amount survived
	// while its component snapped back to the closed transform would pass the reading and be shut.
	UDynamicMeshComponent* Rebuilt = Wardrobe->GetPartComponent(Posed);
	if (!TestNotNull(TEXT("The posed leaf still has a component"), Rebuilt))
	{
		return false;
	}
	TestTrue(TEXT("And the leaf itself is still swung out to where it was"),
		Rebuilt->GetComponentTransform().TransformPosition(FVector(40.0, 0.0, 100.0)).Equals(Swung, 0.5));

	TestNearlyEqual(TEXT("Its neighbours are still shut"),
		Wardrobe->GetPartOpenAmount(AHFWardrobeActor::ShutterPartId(0)), 0.0, 1e-6);

	return true;
}

/**
 * The same, through the house rebuild - which destroys and respawns its elements.
 *
 * A different mechanism entirely: the actor holding the pose does not survive at all, so the pose is
 * captured by element id before the rebuild and put back after it.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFWardrobeSurvivesHouseRebuildTest,
	"HouseForge.Editor.WardrobePosesSurviveHouseRebuild", HF_TEST_FLAGS)

bool FHFWardrobeSurvivesHouseRebuildTest::RunTest(const FString& Parameters)
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
	ON_SCOPE_EXIT{ if (IsValid(House)) { House->Destroy(); } };

	House->SetSpec(FHFSampleHouse::Make2BHK());
	House->BuildGeometry();

	auto FindWardrobe = [House](FName ElementId) -> AHFWardrobeActor*
	{
		for (AActor* Element : House->ElementActors)
		{
			AHFWardrobeActor* Wardrobe = Cast<AHFWardrobeActor>(Element);
			if (IsValid(Wardrobe) && Wardrobe->ElementId == ElementId)
			{
				return Wardrobe;
			}
		}
		return nullptr;
	};

	AHFWardrobeActor* Wardrobe = FindWardrobe(TEXT("F_MBed_Wardrobe"));
	if (!TestNotNull(TEXT("The master bedroom's wardrobe was built"), Wardrobe))
	{
		return false;
	}

	const FName Posed = AHFWardrobeActor::ShutterPartId(0);
	if (!TestTrue(TEXT("The wardrobe has leaves to open"), Wardrobe->NumParts() > 0))
	{
		return false;
	}
	Wardrobe->SetPartOpenAmount(Posed, 0.75);

	// The whole house again, which respawns every element that is not hand-edited.
	House->BuildGeometry();

	AHFWardrobeActor* Rebuilt = FindWardrobe(TEXT("F_MBed_Wardrobe"));
	if (!TestNotNull(TEXT("The wardrobe is still in the flat after a rebuild"), Rebuilt))
	{
		return false;
	}

	TestNearlyEqual(TEXT("The shutter somebody opened is still open after a house rebuild"),
		Rebuilt->GetPartOpenAmount(Posed), 0.75, 1e-6);

	return true;
}

/**
 * Hand-detailing ONE shutter must not freeze the other two, and must not be overwritten.
 *
 * Per part rather than per actor, which is the whole reason FHFPartState carries its own flag. The
 * failure this prevents is silent and unrecoverable: somebody models a moulding onto one leaf, the
 * wardrobe's bay count changes, and the modelling is gone with no way back.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFWardrobeArtistEditTest,
	"HouseForge.Editor.WardrobeArtistEditOnOnePart", HF_TEST_FLAGS)

bool FHFWardrobeArtistEditTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("An editor world is open"), World))
	{
		return false;
	}

	AHFWardrobeActor* Wardrobe = SpawnTestWardrobe(World);
	if (!TestNotNull(TEXT("A wardrobe actor spawns"), Wardrobe))
	{
		return false;
	}
	ON_SCOPE_EXIT{ if (IsValid(Wardrobe)) { Wardrobe->Destroy(); } };

	const FName Edited = AHFWardrobeActor::ShutterPartId(1);
	const FName Untouched = AHFWardrobeActor::ShutterPartId(0);

	UDynamicMeshComponent* Leaf = Wardrobe->GetPartComponent(Edited);
	UDynamicMeshComponent* Other = Wardrobe->GetPartComponent(Untouched);
	if (!TestNotNull(TEXT("The middle leaf has a component"), Leaf)
		|| !TestNotNull(TEXT("Its neighbour has one too"), Other))
	{
		return false;
	}

	TestFalse(TEXT("Generating a wardrobe does not read as hand-editing it"),
		Wardrobe->HasAnyArtistEdits());

	const int32 GeneratedTriangles = Leaf->GetDynamicMesh()->GetMeshRef().TriangleCount();
	SimulateArtistEdit(Leaf, 150.0);
	const int32 EditedTriangles = Leaf->GetDynamicMesh()->GetMeshRef().TriangleCount();

	if (!TestTrue(TEXT("The edit reached the mesh"), EditedTriangles > GeneratedTriangles))
	{
		return false;
	}

	TestTrue(TEXT("Editing one leaf marks that leaf"), Wardrobe->IsPartArtistEdited(Edited));
	TestFalse(TEXT("And marks nothing else"), Wardrobe->IsPartArtistEdited(Untouched));
	TestFalse(TEXT("The shell was not touched"), Wardrobe->bArtistEdited);

	// A parameter change, which regenerates everything that is allowed to be regenerated.
	Wardrobe->Wardrobe.Depth = 65.0;
	Wardrobe->Regenerate();

	TestEqual(TEXT("The hand-edited leaf keeps the mesh that was modelled on it"),
		Wardrobe->GetPartComponent(Edited)->GetDynamicMesh()->GetMeshRef().TriangleCount(),
		EditedTriangles);
	TestTrue(TEXT("And is still flagged, so the next rebuild leaves it alone as well"),
		Wardrobe->IsPartArtistEdited(Edited));

	// Its neighbour rebuilt, which is what makes this protection worth having rather than a freeze
	// on the whole wardrobe.
	TestFalse(TEXT("Its neighbour is still generated"), Wardrobe->IsPartArtistEdited(Untouched));

	// And the whole actor now claims protection, so a house-level rebuild will preserve it rather
	// than destroying it and taking the modelling with it.
	TestTrue(TEXT("One hand-edited part protects the actor from a house rebuild"),
		Wardrobe->ShouldPreserveOnRebuild());

	// Only Revert To Generated is allowed to discard modelling work.
	Wardrobe->RevertToGenerated();

	TestFalse(TEXT("Reverting clears the flag"), Wardrobe->IsPartArtistEdited(Edited));
	TestTrue(TEXT("And the leaf is generated geometry again"),
		Wardrobe->GetPartComponent(Edited)->GetDynamicMesh()->GetMeshRef().TriangleCount() < EditedTriangles);

	return true;
}

/**
 * The reference flat's wardrobes are actually in it, and facing the right way.
 *
 * A fixture's rotation says which way the RUN lies, and a run lies the same way whichever of its two
 * faces is against the wall - so the drawing's angle alone cannot say which way a wardrobe faces.
 * Both wardrobes in this flat are drawn at 90 degrees against the east wall, and taking that angle
 * at face value stands both of them with their shutters against the wall and their backs to the
 * room: a defect that is invisible in plan, which is the only view a drawing has.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFWardrobesInTheFlatTest,
	"HouseForge.Editor.WardrobesInTheReferenceFlat", HF_TEST_FLAGS)

bool FHFWardrobesInTheFlatTest::RunTest(const FString& Parameters)
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
	ON_SCOPE_EXIT{ if (IsValid(House)) { House->Destroy(); } };

	House->SetSpec(FHFSampleHouse::Make2BHK());
	House->BuildGeometry();

	TMap<FName, AHFWardrobeActor*> Built;
	for (AActor* Element : House->ElementActors)
	{
		if (AHFWardrobeActor* Wardrobe = Cast<AHFWardrobeActor>(Element))
		{
			Built.Add(Wardrobe->ElementId, Wardrobe);
		}
	}

	// Every wardrobe the spec declares, and only those.
	int32 Declared = 0;
	for (const FHFFixture& Fixture : House->Spec.Fixtures)
	{
		if (Fixture.Type == EHFFixtureType::Wardrobe)
		{
			++Declared;
			TestTrue(*FString::Printf(TEXT("'%s' was built"), *Fixture.Id.ToString()),
				Built.Contains(Fixture.Id));
		}
	}

	if (!TestEqual(TEXT("Every wardrobe in the spec became an actor"), Built.Num(), Declared))
	{
		return false;
	}
	TestTrue(TEXT("The reference flat has wardrobes to build at all"), Declared >= 2);

	for (const FHFFixture& Fixture : House->Spec.Fixtures)
	{
		if (Fixture.Type != EHFFixtureType::Wardrobe)
		{
			continue;
		}

		AHFWardrobeActor* Wardrobe = Built.FindRef(Fixture.Id);
		if (Wardrobe == nullptr)
		{
			continue;
		}

		const FString Where = Fixture.Id.ToString();

		// THE SHUTTER MOTION CAME OFF THE DRAWING. Until FHFFixtureParams carried it, every wardrobe
		// the pipeline built was side-hung whatever the plan said - the kit could express a sliding
		// run and a top-hung flap and no spec could ask for either, so both were reachable only by
		// hand-editing an actor after generation.
		TestEqual(*FString::Printf(TEXT("'%s' moves the way the drawing says"), *Where),
			static_cast<int32>(Wardrobe->Wardrobe.MotionKind),
			static_cast<int32>(Fixture.Params.ShutterMotion));
		TestEqual(*FString::Printf(TEXT("'%s' hangs its loft the way the drawing says"), *Where),
			static_cast<int32>(Wardrobe->Wardrobe.LoftMotionKind),
			static_cast<int32>(Fixture.Params.LoftShutterMotion));

		// A leaf per bay the drawing counted, each on a component of its own - unless the run slides,
		// in which case it is two leaves on two tracks whatever the carcass behind them is divided
		// into, because a sliding leaf passes its neighbour rather than swinging clear of it. Not a
		// total either way: a wardrobe with a loft has a second row of leaves above the first.
		const bool bSliding = Fixture.Params.ShutterMotion == EHFShutterMotion::Sliding;
		const int32 ExpectedLeaves = bSliding ? 2 : Fixture.Params.ShutterCount;

		for (int32 Bay = 0; Bay < ExpectedLeaves; ++Bay)
		{
			TestNotNull(*FString::Printf(TEXT("'%s' has a leaf on bay %d"), *Where, Bay),
				Wardrobe->GetPartComponent(AHFWardrobeActor::ShutterPartId(Bay)));
		}

		// And a sliding run really is two leaves rather than four that happen to slide. Asserted as
		// an absence, because that is the half a positive check cannot see.
		if (bSliding && Fixture.Params.ShutterCount > 2)
		{
			TestNull(*FString::Printf(TEXT("'%s' slides on two tracks, not one leaf per bay"), *Where),
				Wardrobe->GetPartComponent(AHFWardrobeActor::ShutterPartId(2)));
		}

		// The dimensions off the drawing reached the geometry, in centimetres. The spec is converted
		// exactly once, at ingest, and a wardrobe ten times too big would be the sign it happened twice.
		TestNearlyEqual(*FString::Printf(TEXT("'%s' is as long as the drawing says"), *Where),
			Wardrobe->Wardrobe.Width, Fixture.Footprint.X, 1e-6);
		TestNearlyEqual(*FString::Printf(TEXT("'%s' is as deep as the drawing says"), *Where),
			Wardrobe->Wardrobe.Depth, Fixture.Footprint.Y, 1e-6);

		// ------------------------------------------------------------------- and it faces the room

		const FHFWall* Wall = House->Spec.FindWall(Fixture.AnchorWallId);
		if (!TestNotNull(*FString::Printf(TEXT("'%s' names a wall it backs onto"), *Where), Wall))
		{
			continue;
		}

		// The back of the wardrobe is its local +Y. It has to point at the wall, or the whole run is
		// facing into it.
		const FVector Back = Wardrobe->GetActorTransform().TransformVectorNoScale(FVector::YAxisVector);
		const FVector2D OnWall = FMath::ClosestPointOnSegment2D(Fixture.Position, Wall->Start, Wall->End);
		const FVector2D ToWall = (OnWall - Fixture.Position).GetSafeNormal();

		TestTrue(*FString::Printf(TEXT("'%s' has its back to the wall it is anchored to"), *Where),
			FVector2D::DotProduct(FVector2D(Back.X, Back.Y), ToWall) > 0.9);

		// The bottom-front-left corner is where the actor stands, so the run still covers the
		// footprint the drawing put it on rather than being half a length out.
		const FVector Corner = Wardrobe->GetActorLocation();
		const FVector Centre = Wardrobe->GetActorTransform().TransformPosition(
			FVector(Wardrobe->Wardrobe.Width * 0.5, Wardrobe->Wardrobe.Depth * 0.5, 0.0));

		TestTrue(*FString::Printf(TEXT("'%s' is centred where the drawing put it (%s against %s)"),
			*Where, *Centre.ToString(), *Fixture.Position.ToString()),
			FVector2D(Centre.X, Centre.Y).Equals(Fixture.Position, 0.01));
		const FHFRoom* Room = House->Spec.FindRoom(Fixture.RoomId);
		const double FloorZ = Room != nullptr ? Room->FloorZ : 0.0;
		TestNearlyEqual(*FString::Printf(TEXT("'%s' stands on the floor of its room"), *Where),
			Corner.Z, FloorZ + Fixture.BaseZ, 1e-6);
	}

	return true;
}

/**
 * Every wardrobe in the flat actually OPENS - measured as aperture, not as parts that move.
 *
 * THE DEFECT THIS EXISTS FOR PASSED EVERY MOTION ASSERTION IN THE SUITE. The master bedroom's 2400
 * sliding run had two body leaves, both of which travelled their full 118.45 cm when swept, in
 * opposite directions, off one open amount. They exchanged tracks. The front elevation of the body
 * was 100% covered at open 0.0, at 1.0 and at every value between, and the four top-hung loft flaps
 * above it lifted perfectly - which is why it was reported as "the bottom section is not opening"
 * rather than as a wardrobe that does nothing.
 *
 * So a sweep that asks "did this part move" cannot catch it and is not what is asserted here. What
 * is asserted is the thing a person in the room can see: HOW MUCH OF THE RUN IS STILL COVERED. Both
 * sections are measured separately, because the whole point of the report was that one of them
 * opened and the other did not, and a figure for the wardrobe as a whole would have averaged the
 * defect away.
 *
 * The per-part sweep is kept alongside it, because the two catch different things: a leaf that
 * silently stops articulating is invisible to a coverage figure its neighbour already accounts for.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFWardrobeApertureTest,
	"HouseForge.Editor.EveryWardrobeSectionOpens", HF_TEST_FLAGS)

namespace HouseForgeWardrobe
{
	/** A leaf's footprint on the front elevation of the run, in the wardrobe's own local space. */
	struct FElevationRect
	{
		double MinX = 0.0;
		double MaxX = 0.0;
		double MinZ = 0.0;
		double MaxZ = 0.0;
	};

	/**
	 * Where a part stands on its wardrobe's front elevation, right now.
	 *
	 * Read off the LIVE component transform and the part's own mesh, so it measures where the leaf
	 * has actually gone rather than where the parameters say it should be - which is the difference
	 * between testing the geometry and testing the arithmetic that produced it.
	 */
	bool ElevationOf(const AHFWardrobeActor& Wardrobe, UDynamicMeshComponent& Part, FElevationRect& Out)
	{
		if (Part.GetDynamicMesh() == nullptr)
		{
			return false;
		}

		FAxisAlignedBox3d Local = FAxisAlignedBox3d::Empty();
		Part.GetDynamicMesh()->ProcessMesh([&Local](const FDynamicMesh3& Mesh)
		{
			Local = Mesh.GetBounds();
		});

		if (Local.IsEmpty())
		{
			return false;
		}

		// Component space to the wardrobe's own space, corner by corner: the leaf may have swung, so
		// its box does not stay axis-aligned through the transform and a min/max pair would be wrong.
		const FTransform ToWardrobe = Part.GetComponentTransform().GetRelativeTransform(Wardrobe.GetActorTransform());

		FAxisAlignedBox3d InRun = FAxisAlignedBox3d::Empty();
		for (int32 Corner = 0; Corner < 8; ++Corner)
		{
			InRun.Contain(ToWardrobe.TransformPosition(Local.GetCorner(Corner)));
		}

		Out.MinX = InRun.Min.X;
		Out.MaxX = InRun.Max.X;
		Out.MinZ = InRun.Min.Z;
		Out.MaxZ = InRun.Max.Z;
		return true;
	}

	/**
	 * The fraction of one band of the front elevation that leaves still cover.
	 *
	 * Sampled on a grid rather than summed, because leaves overlap - a slider laps its neighbour when
	 * shut and parks wholly over it when open - and added areas would count the lap twice and report
	 * a run as more than fully covered.
	 */
	double CoveredFraction(const TArray<FElevationRect>& Leaves, double Width, double ZMin, double ZMax)
	{
		constexpr int32 Steps = 60;
		if (Width <= 0.0 || ZMax - ZMin <= 0.0)
		{
			return 0.0;
		}

		int32 Covered = 0;
		for (int32 i = 0; i < Steps; ++i)
		{
			const double X = Width * (i + 0.5) / Steps;
			for (int32 j = 0; j < Steps; ++j)
			{
				const double Z = FMath::Lerp(ZMin, ZMax, (j + 0.5) / Steps);

				for (const FElevationRect& Leaf : Leaves)
				{
					if (X >= Leaf.MinX && X <= Leaf.MaxX && Z >= Leaf.MinZ && Z <= Leaf.MaxZ)
					{
						++Covered;
						break;
					}
				}
			}
		}

		return double(Covered) / double(Steps * Steps);
	}
}

bool FHFWardrobeApertureTest::RunTest(const FString& Parameters)
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

	int32 Measured = 0;

	for (AActor* Element : House->ElementActors)
	{
		AHFWardrobeActor* Wardrobe = Cast<AHFWardrobeActor>(Element);
		if (Wardrobe == nullptr)
		{
			continue;
		}

		const FString Where = Wardrobe->ElementId.ToString();
		const FHFWardrobeParams& P = Wardrobe->Wardrobe;

		// The two sections, by the part ids the kit fixes them to. Named rather than inferred from
		// height, because "the bottom section" is exactly what the report was about.
		TArray<UDynamicMeshComponent*> Body;
		TArray<UDynamicMeshComponent*> Loft;
		for (int32 Index = 0; Index < 8; ++Index)
		{
			if (UDynamicMeshComponent* Part = Wardrobe->GetPartComponent(AHFWardrobeActor::ShutterPartId(Index)))
			{
				Body.Add(Part);
			}
			if (UDynamicMeshComponent* Part = Wardrobe->GetPartComponent(AHFWardrobeActor::LoftPartId(Index)))
			{
				Loft.Add(Part);
			}
		}

		if (!TestTrue(*FString::Printf(TEXT("'%s' has body leaves at all"), *Where), Body.Num() > 0))
		{
			continue;
		}

		auto CoverageNow = [&](const TArray<UDynamicMeshComponent*>& Parts, double ZMin, double ZMax)
		{
			TArray<FElevationRect> Rects;
			for (UDynamicMeshComponent* Part : Parts)
			{
				FElevationRect Rect;
				if (Part != nullptr && ElevationOf(*Wardrobe, *Part, Rect))
				{
					Rects.Add(Rect);
				}
			}
			return CoveredFraction(Rects, P.Width, ZMin, ZMax);
		};

		const double BodyBottom = P.BodyBottomZ();
		const double BodyTop = P.BodyTopZ();

		Wardrobe->SetMasterOpenAmount(0.0);
		const double BodyShut = CoverageNow(Body, BodyBottom, BodyTop);
		const double LoftShut = Loft.Num() > 0 ? CoverageNow(Loft, BodyTop, P.Height) : 0.0;

		Wardrobe->SetMasterOpenAmount(1.0);
		const double BodyOpen = CoverageNow(Body, BodyBottom, BodyTop);
		const double LoftOpen = Loft.Num() > 0 ? CoverageNow(Loft, BodyTop, P.Height) : 0.0;

		Wardrobe->SetMasterOpenAmount(0.0);

		// Shut means shut. A run that was already open has nothing to prove by opening.
		TestTrue(*FString::Printf(TEXT("'%s' body is covered when shut (%.0f%%)"), *Where, BodyShut * 100.0),
			BodyShut > 0.9);

		// A REAL APERTURE, not merely movement. A two-leaf slider gives up about half its elevation;
		// a side-hung run gives up nearly all of it. A quarter is the floor, and the defect scored
		// zero: the master bedroom's body was 100% covered at full open.
		if (BodyShut - BodyOpen < 0.25)
		{
			AddError(FString::Printf(
				TEXT("'%s' does not open its BODY: %.0f%% of the front elevation is covered shut and %.0f%% at full open, so it uncovers %.0f%%. Leaves that all move and cancel each other out pass every part-motion assertion and leave a wardrobe nobody can get into."),
				*Where, BodyShut * 100.0, BodyOpen * 100.0, (BodyShut - BodyOpen) * 100.0));
		}

		if (Loft.Num() > 0)
		{
			TestTrue(*FString::Printf(TEXT("'%s' loft is covered when shut (%.0f%%)"), *Where, LoftShut * 100.0),
				LoftShut > 0.9);

			if (LoftShut - LoftOpen < 0.25)
			{
				AddError(FString::Printf(
					TEXT("'%s' does not open its LOFT: %.0f%% covered shut, %.0f%% at full open."),
					*Where, LoftShut * 100.0, LoftOpen * 100.0));
			}
		}

		// ------------------------------------------------------------------ and every leaf sweeps
		//
		// Kept alongside the aperture measurement rather than instead of it. A leaf that quietly
		// stops articulating in a run where its neighbours already uncover the elevation would not
		// move the coverage figure at all.
		//
		// EXACTLY ONE LEAF OF A SLIDING PAIR IS ALLOWED TO SIT OUT A MASTER OPEN, and it is not
		// because it cannot move - both leaves of a run slide, and either can be the one you push.
		// It is because two leaves driven by ONE amount exchange tracks and uncover nothing; see
		// FHFPartMotion::bMasterOpens. HouseForge.Editor.WardrobeOpensBothWays is where the other
		// leaf is proved to move when it is asked directly.
		//
		// At most one, because a pair with both leaves sitting out is a wardrobe that does not open
		// and would otherwise be silent.
		int32 Standing = 0;
		TArray<UDynamicMeshComponent*> AllLeaves = Body;
		AllLeaves.Append(Loft);

		for (UDynamicMeshComponent* Part : AllLeaves)
		{
			FElevationRect Shut;
			FElevationRect Open;

			Wardrobe->SetMasterOpenAmount(0.0);
			if (!ElevationOf(*Wardrobe, *Part, Shut))
			{
				continue;
			}

			Wardrobe->SetMasterOpenAmount(1.0);
			ElevationOf(*Wardrobe, *Part, Open);
			Wardrobe->SetMasterOpenAmount(0.0);

			const double Moved = FMath::Max3(
				FMath::Abs(Open.MinX - Shut.MinX), FMath::Abs(Open.MinZ - Shut.MinZ),
				FMath::Abs(Open.MaxX - Shut.MaxX));

			if (Moved < 1.0)
			{
				++Standing;
			}
		}

		TestTrue(*FString::Printf(TEXT("'%s' has at most one leaf that stands still (%d of %d)"),
			*Where, Standing, AllLeaves.Num()), Standing <= 1);

		++Measured;
	}

	TestTrue(TEXT("The reference flat has wardrobes to open"), Measured >= 2);

	return true;
}

/**
 * A sliding wardrobe opens from EITHER end, and the aperture is measured in centimetres.
 *
 * The user's report put the wardrobes and the balcony doors in one sentence, and they had the same
 * fault for the same reason: one leaf of the pair had been made furniture so that the pair could not
 * cancel, which cured a wardrobe that never opened by giving it a wardrobe that opened one way.
 *
 * Both leaves run now, and what separates them is only which one a single control drives. So the
 * assertions here are the ones that can tell those two situations apart, and there are four,
 * because each of them passes on a different broken wardrobe:
 *
 *   - EITHER LEAF MOVES WHEN ASKED. Fails on a leaf built as furniture; passes on the canceller.
 *   - THE APERTURE IS AT THE END THE MOVED LEAF CAME FROM. Fails on a run that always opens the
 *     same end; passes on anything that opens at all.
 *   - OPENING ONE LEAF DOES NOT MOVE THE OTHER. Fails on the canceller; passes on furniture.
 *   - THE APERTURE IS AT LEAST HALF THE RUN LESS THE LAP, IN CENTIMETRES. Fails on a leaf that
 *     travels a token distance; passes on anything that moves at all, which is precisely what the
 *     original "does this part move" assertion did while the run stayed 100% covered.
 *
 * Measured on the reference flat's own wardrobes rather than on a fixture built for the test.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFWardrobeOpensBothWaysTest,
	"HouseForge.Editor.WardrobeOpensBothWays", HF_TEST_FLAGS)

bool FHFWardrobeOpensBothWaysTest::RunTest(const FString& Parameters)
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

	int32 Measured = 0;

	for (AActor* Element : House->ElementActors)
	{
		AHFWardrobeActor* Wardrobe = Cast<AHFWardrobeActor>(Element);
		if (Wardrobe == nullptr || Wardrobe->Wardrobe.MotionKind != EHFShutterMotion::Sliding)
		{
			continue;
		}

		const FString Where = Wardrobe->ElementId.ToString();

		// A sliding run is two leaves whatever the carcass behind it is divided into: a leaf can only
		// run to where another leaf is NOT, so leaves and tracks are the same count.
		const FName LeftId = AHFWardrobeActor::ShutterPartId(0);
		const FName RightId = AHFWardrobeActor::ShutterPartId(1);

		UDynamicMeshComponent* LeftLeaf = Wardrobe->GetPartComponent(LeftId);
		UDynamicMeshComponent* RightLeaf = Wardrobe->GetPartComponent(RightId);
		const FHFPartState* LeftState = Wardrobe->FindPart(LeftId);
		const FHFPartState* RightState = Wardrobe->FindPart(RightId);

		if (!TestNotNull(*FString::Printf(TEXT("'%s' has a left leaf"), *Where), LeftLeaf)
			|| !TestNotNull(*FString::Printf(TEXT("'%s' has a right leaf"), *Where), RightLeaf)
			|| LeftState == nullptr || RightState == nullptr)
		{
			continue;
		}

		TestTrue(*FString::Printf(TEXT("'%s': both leaves are gear, not one leaf and one panel"), *Where),
			LeftState->Motion.Type == EHFMotionType::Slide
				&& RightState->Motion.Type == EHFMotionType::Slide);
		TestTrue(*FString::Printf(TEXT("'%s': the pair names itself, so a run can be shut from either end"), *Where),
			LeftState->Motion.AlternateToPartId == RightId
				&& RightState->Motion.AlternateToPartId == LeftId);

		// Where each leaf stands along the run, right now, in the wardrobe's own space.
		auto SpanOf = [Wardrobe](UDynamicMeshComponent& Part, double& OutMin, double& OutMax)
		{
			FElevationRect Rect;
			if (!ElevationOf(*Wardrobe, Part, Rect))
			{
				return false;
			}
			OutMin = Rect.MinX;
			OutMax = Rect.MaxX;
			return true;
		};

		Wardrobe->CloseAllParts();

		double LeftShutMin = 0.0, LeftShutMax = 0.0, RightShutMin = 0.0, RightShutMax = 0.0;
		if (!SpanOf(*LeftLeaf, LeftShutMin, LeftShutMax) || !SpanOf(*RightLeaf, RightShutMin, RightShutMax))
		{
			AddError(FString::Printf(TEXT("'%s' has leaves with no mesh to measure."), *Where));
			continue;
		}

		const double RunMin = FMath::Min(LeftShutMin, RightShutMin);
		const double RunMax = FMath::Max(LeftShutMax, RightShutMax);
		const double RunWidth = RunMax - RunMin;

		// The lap: sliding leaves overlap on separate tracks rather than being separated by a reveal,
		// because a reveal between two sliding leaves is a hole straight into the wardrobe. It is
		// also exactly what the aperture is allowed to fall short of half the run by.
		const double Lap = LeftShutMax - RightShutMin;
		const double Required = RunWidth * 0.5 - Lap;

		TestTrue(*FString::Printf(TEXT("'%s' laps at the meeting line when shut (%.1f cm)"), *Where, Lap),
			Lap > 0.0);

		// ---------------------------------------------------------------- run the left-hand leaf
		Wardrobe->OpenRunFrom(LeftId, 1.0);

		double LeftOpenMin = 0.0, LeftOpenMax = 0.0, RightHeldMin = 0.0, RightHeldMax = 0.0;
		SpanOf(*LeftLeaf, LeftOpenMin, LeftOpenMax);
		SpanOf(*RightLeaf, RightHeldMin, RightHeldMax);

		TestTrue(*FString::Printf(TEXT("'%s': running the left leaf leaves the right one where it was"), *Where),
			FMath::IsNearlyEqual(RightHeldMin, RightShutMin, 0.01)
				&& FMath::IsNearlyEqual(RightHeldMax, RightShutMax, 0.01));

		const double LeftAperture = FMath::Min(LeftOpenMin, RightHeldMin) - RunMin;
		TestTrue(*FString::Printf(
				TEXT("'%s' opens %.1f cm at the LEFT end, and half the run less the lap is %.1f cm"),
				*Where, LeftAperture, Required),
			LeftAperture >= Required - 0.01);

		// --------------------------------------------------------------- run the right-hand leaf
		Wardrobe->OpenRunFrom(RightId, 1.0);

		double RightOpenMin = 0.0, RightOpenMax = 0.0, LeftHeldMin = 0.0, LeftHeldMax = 0.0;
		SpanOf(*RightLeaf, RightOpenMin, RightOpenMax);
		SpanOf(*LeftLeaf, LeftHeldMin, LeftHeldMax);

		TestTrue(*FString::Printf(TEXT("'%s': running the right leaf leaves the left one where it was"), *Where),
			FMath::IsNearlyEqual(LeftHeldMin, LeftShutMin, 0.01)
				&& FMath::IsNearlyEqual(LeftHeldMax, LeftShutMax, 0.01));

		const double RightAperture = RunMax - FMath::Max(RightOpenMax, LeftHeldMax);
		TestTrue(*FString::Printf(
				TEXT("'%s' opens %.1f cm at the RIGHT end, and half the run less the lap is %.1f cm"),
				*Where, RightAperture, Required),
			RightAperture >= Required - 0.01);

		// Neither leaf ever leaves the carcass. A leaf running past the end of the run is a leaf
		// hanging in the room off nothing, which is what a leaf slid its own width does.
		for (const double Edge : { LeftOpenMin, LeftOpenMax, RightOpenMin, RightOpenMax })
		{
			TestTrue(*FString::Printf(TEXT("'%s' keeps every leaf inside the run (%.1f in %.1f..%.1f)"),
					*Where, Edge, RunMin, RunMax),
				Edge >= RunMin - 0.01 && Edge <= RunMax + 0.01);
		}

		Wardrobe->CloseAllParts();
		++Measured;
	}

	TestTrue(TEXT("The reference flat has a sliding wardrobe to open"), Measured >= 1);

	return true;
}

#undef HF_TEST_FLAGS

#endif // WITH_DEV_AUTOMATION_TESTS
