// Copyright Siddartha G. All Rights Reserved.

#include "HouseForgeEditor.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Actors/HFElementActors.h"
#include "Actors/HFHouseActor.h"
#include "Actors/HFOpeningActor.h"
#include "Components/DynamicMeshComponent.h"
#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"
#include "Model/HFSampleHouse.h"
#include "Model/HFTypes.h"

#define HF_TEST_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Can the flat actually be walked?
 *
 * Every other collision test in this plugin traces with UPrimitiveComponent::LineTraceComponent and
 * with bTraceComplex TRUE. Both of those quietly avoid the two things a walkthrough depends on.
 *
 * LineTraceComponent goes straight to that one component's body, so it proves the body exists and
 * moved with the mesh - worth proving, and it is what caught the missing bEnableComplexCollision -
 * but it never asks the world. A body that is present and correct but absent from the physics scene,
 * or one whose profile does not block the channel a character sweeps on, passes every one of those
 * tests while a character walks straight through it.
 *
 * bTraceComplex TRUE is the bigger gap. Character movement sweeps a capsule against SIMPLE
 * collision. A UDynamicMeshComponent has no simple shapes at all, so what makes the flat solid is
 * CTF_UseComplexAsSimple redirecting those simple queries onto the triangle mesh. Ask for the
 * complex geometry explicitly and it answers whether or not that redirect works - which is to say
 * every existing collision assertion would still pass with the flat set to CTF_UseDefault and
 * nothing to stand on.
 *
 * So this file traces the WORLD, on the channel a pawn moves on, with bTraceComplex FALSE, against
 * the reference 2BHK as it is actually built. That is the query a walkthrough makes.
 */
namespace HouseForgeWalkthrough
{
	/** The trace a character makes: the world, a pawn's channel, and simple collision. */
	bool WalkTrace(UWorld* World, const FVector& Start, const FVector& End, FHitResult& OutHit)
	{
		FCollisionQueryParams Params(TEXT("HFWalkthrough"), /*bTraceComplex*/ false);
		return World->LineTraceSingleByChannel(OutHit, Start, End, ECC_Pawn, Params);
	}

	/** Every component a walking trace runs into, so a specific one can be looked for among them. */
	void WalkTraceMulti(UWorld* World, const FVector& Start, const FVector& End, TArray<FHitResult>& OutHits)
	{
		FCollisionQueryParams Params(TEXT("HFWalkthroughMulti"), /*bTraceComplex*/ false);
		World->LineTraceMultiByChannel(OutHits, Start, End, ECC_Pawn, Params);
	}

	/**
	 * The element of THIS house that a walking trace ran into, and what it hit if none.
	 *
	 * Identified rather than merely counted. Every test in a run shares one editor world, an actor
	 * destroyed by a previous test keeps its physics body until the world next ticks, and several of
	 * these tests build geometry on the same coordinates - so "something blocked" can be answered by
	 * a door that was destroyed two tests ago. That happened while this file was being written and
	 * read convincingly as a collision defect.
	 *
	 * Membership of this house's own elements is the check, NOT a particular element id. Asking for
	 * a specific id overreaches: rooms share a slab edge where they meet, and walls meet at columns,
	 * so the nearest solid thing under a point in the living room is quite legitimately the adjoining
	 * room's slab, and the nearest solid thing through a wall by a junction is the column on it.
	 * Both are construction, both stop a walkthrough, and demanding one of them by name fails on
	 * geometry that is entirely correct.
	 */
	AHFElementActor* FirstHouseElementHit(UWorld* World, const FVector& Start, const FVector& End,
		const AHFHouseActor* House, FHitResult& OutHit, FString& OutWhatWasHit)
	{
		TArray<FHitResult> Hits;
		WalkTraceMulti(World, Start, End, Hits);

		TArray<FString> Described;
		for (const FHitResult& Hit : Hits)
		{
			AHFElementActor* Element = Cast<AHFElementActor>(Hit.GetActor());
			if (Element != nullptr && House != nullptr && House->ElementActors.Contains(Element))
			{
				OutHit = Hit;
				return Element;
			}

			const AActor* Actor = Hit.GetActor();
			Described.Add(Actor != nullptr
				? FString::Printf(TEXT("%s '%s' at Z=%.1f"), *Actor->GetClass()->GetName(), *Actor->GetName(), Hit.ImpactPoint.Z)
				: TEXT("<no actor>"));
		}

		OutWhatWasHit = Described.IsEmpty() ? TEXT("nothing at all") : FString::Join(Described, TEXT(", "));
		return nullptr;
	}

	/**
	 * A point comfortably inside a room's boundary.
	 *
	 * The centroid of the vertices is outside an L-shaped room, so it is only used when the room
	 * actually contains it; otherwise the bounding box is sampled until a point inside turns up.
	 */
	bool InteriorPoint(const FHFRoom& Room, FVector2D& OutPoint)
	{
		if (Room.Boundary.Num() < 3)
		{
			return false;
		}

		FVector2D Centroid = FVector2D::ZeroVector;
		FVector2D Min = Room.Boundary[0];
		FVector2D Max = Room.Boundary[0];

		for (const FVector2D& Vertex : Room.Boundary)
		{
			Centroid += Vertex;
			Min = FVector2D(FMath::Min(Min.X, Vertex.X), FMath::Min(Min.Y, Vertex.Y));
			Max = FVector2D(FMath::Max(Max.X, Vertex.X), FMath::Max(Max.Y, Vertex.Y));
		}
		Centroid /= double(Room.Boundary.Num());

		if (Room.ContainsPoint(Centroid))
		{
			OutPoint = Centroid;
			return true;
		}

		constexpr int32 Steps = 16;
		for (int32 i = 1; i < Steps; ++i)
		{
			for (int32 j = 1; j < Steps; ++j)
			{
				const FVector2D Candidate(
					FMath::Lerp(Min.X, Max.X, double(i) / Steps),
					FMath::Lerp(Min.Y, Max.Y, double(j) / Steps));

				if (Room.ContainsPoint(Candidate))
				{
					OutPoint = Candidate;
					return true;
				}
			}
		}

		return false;
	}

	/**
	 * Removes every HouseForge actor already in the world.
	 *
	 * The editor world is shared by the whole run and is not torn down between tests, so geometry
	 * from an earlier one is still standing - and several tests build the same reference flat on the
	 * same coordinates. Two coincident floor slabs make "which one did the trace hit" arbitrary, and
	 * a stale one can answer for a missing real one. Cleared up front so each test traces against
	 * only what it built itself.
	 */
	void ClearHouseForgeActors(UWorld* World)
	{
		TArray<AActor*> Doomed;

		for (TActorIterator<AHFHouseActor> It(World); It; ++It)
		{
			It->ClearGeometry();
			Doomed.Add(*It);
		}

		// Element actors spawned directly by other tests, with no house owning them.
		for (TActorIterator<AHFElementActor> It(World); It; ++It)
		{
			Doomed.Add(*It);
		}

		for (AActor* Actor : Doomed)
		{
			if (IsValid(Actor))
			{
				Actor->Destroy();
			}
		}
	}

	/** Builds the reference flat and hands back the house actor holding it. */
	AHFHouseActor* BuildReferenceFlat(UWorld* World, FHFHouseSpec& OutSpec)
	{
		ClearHouseForgeActors(World);

		OutSpec = FHFSampleHouse::Make2BHK();
		FHFUnits::ConvertToCentimeters(OutSpec);

		AHFHouseActor* House = World->SpawnActor<AHFHouseActor>();
		if (House == nullptr)
		{
			return nullptr;
		}

		// SetSpec converts on the way in, so it is handed the spec in its original units.
		House->SetSpec(FHFSampleHouse::Make2BHK());
		House->BuildGeometry();
		return House;
	}
}

/**
 * There is a floor under every room, and it is solid to the query a character makes.
 *
 * This is the one that fails if CTF_UseComplexAsSimple ever stops being honoured: the slab renders,
 * every complex trace finds it, and a character falls through it into nothing.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFWalkthroughFloorTest,
	"HouseForge.Walkthrough.EveryRoomHasAFloorToStandOn", HF_TEST_FLAGS)

bool FHFWalkthroughFloorTest::RunTest(const FString& Parameters)
{
	using namespace HouseForgeWalkthrough;

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("An editor world is open"), World))
	{
		return false;
	}

	FHFHouseSpec Spec;
	AHFHouseActor* House = BuildReferenceFlat(World, Spec);
	if (!TestNotNull(TEXT("The reference flat builds"), House))
	{
		return false;
	}
	ON_SCOPE_EXIT{ if (IsValid(House)) { House->ClearGeometry(); House->Destroy(); } };

	int32 Checked = 0;

	for (const FHFRoom& Room : Spec.Rooms)
	{
		FVector2D Plan;
		if (!InteriorPoint(Room, Plan))
		{
			continue;
		}

		// Straight down from head height onto the finished floor level.
		const FVector Start(Plan.X, Plan.Y, Room.FloorZ + 150.0);
		const FVector End(Plan.X, Plan.Y, Room.FloorZ - 50.0);

		FHitResult Hit;
		FString WhatWasHit;
		AHFElementActor* Stood = FirstHouseElementHit(World, Start, End, House, Hit, WhatWasHit);

		if (Stood == nullptr)
		{
			AddError(FString::Printf(
				TEXT("Nothing to stand on in room '%s': a pawn-channel trace with simple collision found no floor at (%.0f, %.0f). It hit %s. A walkthrough falls through it."),
				*Room.Id.ToString(), Plan.X, Plan.Y, *WhatWasHit));
			continue;
		}

		// A slab, not a fixture the trace clipped on the way down. It may belong to the adjoining
		// room where two slabs meet, which is why the room is not named - but it must be a floor.
		if (!TestTrue(*FString::Printf(TEXT("What is underfoot in room '%s' is a floor slab"), *Room.Id.ToString()),
			Stood->IsA<AHFRoomActor>()))
		{
			continue;
		}

		// And it is that room's own slab, at the finished floor level - not a fixture the trace
		// happened to clip on the way down.
		TestNearlyEqual(
			*FString::Printf(TEXT("Room '%s' is walkable at its finished floor level"), *Room.Id.ToString()),
			Hit.ImpactPoint.Z, Room.FloorZ, 2.0);

		++Checked;
	}

	TestTrue(TEXT("Every room in the reference flat was stood on"), Checked >= Spec.Rooms.Num());
	TestTrue(TEXT("The reference flat has rooms to stand in"), Checked > 0);

	return true;
}

/**
 * The walls are solid, to the same query.
 *
 * Traced at a height and a position clear of every opening in that wall, so what is being asked is
 * whether the masonry blocks - not whether a door leaf happens to be in the way.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFWalkthroughWallTest,
	"HouseForge.Walkthrough.WallsBlockAWalkingTrace", HF_TEST_FLAGS)

bool FHFWalkthroughWallTest::RunTest(const FString& Parameters)
{
	using namespace HouseForgeWalkthrough;

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("An editor world is open"), World))
	{
		return false;
	}

	FHFHouseSpec Spec;
	AHFHouseActor* House = BuildReferenceFlat(World, Spec);
	if (!TestNotNull(TEXT("The reference flat builds"), House))
	{
		return false;
	}
	ON_SCOPE_EXIT{ if (IsValid(House)) { House->ClearGeometry(); House->Destroy(); } };

	int32 Checked = 0;

	for (const FHFWall& Wall : Spec.Walls)
	{
		const double Length = Wall.Length();
		if (Length <= UE_KINDA_SMALL_NUMBER || Wall.Thickness <= 0.0)
		{
			continue;
		}

		const FVector2D Direction = (Wall.End - Wall.Start) / Length;
		const FVector2D Normal(-Direction.Y, Direction.X);

		// Somewhere along this wall that no opening reaches, with a margin either side so the trace
		// is into masonry rather than down the edge of a reveal.
		auto IsClear = [&Spec, &Wall](double Along)
		{
			for (const FHFOpening& Opening : Spec.Openings)
			{
				if (Opening.WallId != Wall.Id || Opening.Width <= 0.0)
				{
					continue;
				}

				if (FMath::Abs(Along - Opening.OffsetAlongWall) < Opening.Width * 0.5 + 25.0)
				{
					return false;
				}
			}
			return true;
		};

		double SolidAlong = -1.0;
		constexpr int32 Steps = 40;
		for (int32 i = 1; i < Steps; ++i)
		{
			const double Along = Length * double(i) / Steps;
			if (IsClear(Along))
			{
				SolidAlong = Along;
				break;
			}
		}

		if (SolidAlong < 0.0)
		{
			// A wall that is opening from end to end has no masonry to test. Rare, and not a defect.
			continue;
		}

		const FVector2D Plan = Wall.Start + Direction * SolidAlong;
		const double Z = Wall.BaseZ + FMath::Min(100.0, Wall.Height * 0.5);
		const double Reach = Wall.Thickness * 0.5 + 30.0;

		const FVector Start(Plan.X + Normal.X * Reach, Plan.Y + Normal.Y * Reach, Z);
		const FVector End(Plan.X - Normal.X * Reach, Plan.Y - Normal.Y * Reach, Z);

		FHitResult Hit;
		FString WhatWasHit;
		if (FirstHouseElementHit(World, Start, End, House, Hit, WhatWasHit) == nullptr)
		{
			AddError(FString::Printf(
				TEXT("Wall '%s' does not block a walking trace at (%.0f, %.0f, %.0f). It hit %s. It renders but a walkthrough passes through it."),
				*Wall.Id.ToString(), Plan.X, Plan.Y, Z, *WhatWasHit));
			continue;
		}

		++Checked;
	}

	// Every failure above is an AddError naming the wall; this only guards against the whole loop
	// finding nothing to trace and reporting success by default.
	TestTrue(TEXT("The reference flat has solid walls to walk into"), Checked > 0);

	return true;
}

/**
 * A closed door blocks its doorway and an open one does not - asked of the world, not the component.
 *
 * Both halves matter and neither is sufficient alone. Only checking that a closed door blocks passes
 * with a leaf whose collision never moves; only checking that an open one clears passes with a leaf
 * that has no collision at all. Looked up by which component the trace hit, so other geometry
 * standing in the line - a floor edge, a fixture - cannot make either half pass by accident.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFWalkthroughDoorTest,
	"HouseForge.Walkthrough.ClosedDoorsBlockAndOpenOnesDoNot", HF_TEST_FLAGS)

bool FHFWalkthroughDoorTest::RunTest(const FString& Parameters)
{
	using namespace HouseForgeWalkthrough;

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("An editor world is open"), World))
	{
		return false;
	}

	FHFHouseSpec Spec;
	AHFHouseActor* House = BuildReferenceFlat(World, Spec);
	if (!TestNotNull(TEXT("The reference flat builds"), House))
	{
		return false;
	}
	ON_SCOPE_EXIT{ if (IsValid(House)) { House->ClearGeometry(); House->Destroy(); } };

	int32 Checked = 0;

	for (const FHFOpening& Opening : Spec.Openings)
	{
		if (Opening.Kind != EHFOpeningKind::Door || Opening.Swing == EHFSwing::None)
		{
			continue;
		}

		const FHFWall* Wall = Spec.FindWall(Opening.WallId);
		if (Wall == nullptr || Wall->Length() <= UE_KINDA_SMALL_NUMBER)
		{
			continue;
		}

		// The door actor the house built for this opening, and the leaf hanging on it.
		AHFOpeningActor* Door = nullptr;
		for (AActor* Element : House->ElementActors)
		{
			if (AHFOpeningActor* Candidate = Cast<AHFOpeningActor>(Element))
			{
				if (Candidate->ElementId == Opening.Id)
				{
					Door = Candidate;
					break;
				}
			}
		}

		if (Door == nullptr)
		{
			AddError(FString::Printf(TEXT("The house built no door actor for opening '%s'."), *Opening.Id.ToString()));
			continue;
		}

		UDynamicMeshComponent* Leaf = Door->GetPartComponent(AHFOpeningActor::LeafPartId);
		if (Leaf == nullptr)
		{
			AddError(FString::Printf(TEXT("Door '%s' has no leaf component to collide with."), *Opening.Id.ToString()));
			continue;
		}

		const double Length = Wall->Length();
		const FVector2D Direction = (Wall->End - Wall->Start) / Length;
		const FVector2D Normal(-Direction.Y, Direction.X);
		const FVector2D Centre = Wall->Start + Direction * Opening.OffsetAlongWall;

		// Straight through the middle of the doorway at waist height, and on out into the room the
		// door opens into - the line somebody walks.
		//
		// It has to run the whole depth of the swing, not just across the reveal. A leaf at 45 degrees
		// crosses the doorway's centreline at half its own width out from the wall: 52.5 cm for the
		// 105 cm main door. A trace that stops at the reveal misses it there and reports an ajar door
		// as walkable - and on the 90 cm internal doors it would clear the leaf by under a centimetre,
		// which is to say it would pass by luck rather than because anything is in the way.
		const double Z = Wall->BaseZ + Opening.SillHeight + FMath::Min(100.0, Opening.Height * 0.5);
		const double Side =
			(Opening.Swing == EHFSwing::InwardLeft || Opening.Swing == EHFSwing::InwardRight) ? 1.0 : -1.0;

		// Started just clear of the far face so a closed leaf is ahead of the ray rather than on its
		// origin, and run out past the leaf's full reach on the side it actually swings to.
		const FVector2D From = Centre - Normal * Side * (Wall->Thickness * 0.5 + 5.0);
		const FVector2D To = Centre + Normal * Side * (Opening.Width + 15.0);

		const FVector Start(From.X, From.Y, Z);
		const FVector End(To.X, To.Y, Z);

		auto LeafIsInTheWay = [&]()
		{
			TArray<FHitResult> Hits;
			WalkTraceMulti(World, Start, End, Hits);

			for (const FHitResult& Hit : Hits)
			{
				if (Hit.GetComponent() == Leaf)
				{
					return true;
				}
			}
			return false;
		};

		Door->SetPartOpenAmount(AHFOpeningActor::LeafPartId, 0.0);
		if (!LeafIsInTheWay())
		{
			AddError(FString::Printf(
				TEXT("Closed, door '%s' does not block its own doorway. A walkthrough walks through a shut door."),
				*Opening.Id.ToString()));
		}

		// Ajar is still shut as far as walking through it goes.
		Door->SetPartOpenAmount(AHFOpeningActor::LeafPartId, 0.5);
		if (!LeafIsInTheWay())
		{
			AddError(FString::Printf(
				TEXT("Half open, door '%s' no longer blocks its doorway; a walkthrough slips past a door that is merely ajar."),
				*Opening.Id.ToString()));
		}

		Door->SetPartOpenAmount(AHFOpeningActor::LeafPartId, 1.0);
		if (LeafIsInTheWay())
		{
			AddError(FString::Printf(
				TEXT("Open, door '%s' still blocks its doorway. Its collision stayed behind in the opening while the leaf swung away."),
				*Opening.Id.ToString()));
		}

		Door->SetPartOpenAmount(AHFOpeningActor::LeafPartId, 0.0);
		++Checked;
	}

	TestTrue(TEXT("Every swing door in the reference flat was walked into"), Checked >= 7);

	return true;
}

/**
 * Collision follows the mesh when the mesh is rebuilt, rather than staying where it was cooked.
 *
 * A parameter change rewrites the geometry, and the body has to be recooked with it. If it is not,
 * the element renders at its new shape and collides at its old one - the flat looks right in every
 * screenshot and a walkthrough catches on doors that are not there any more. Nothing else asserts
 * this: the existing regeneration tests measure the rebuilt mesh's volume, which is the visual half
 * only, and the collision tests never regenerate.
 *
 * Measured by shortening a door until a trace that used to hit it passes over its head.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFWalkthroughRegeneratedCollisionTest,
	"HouseForge.Walkthrough.CollisionFollowsARegeneratedMesh", HF_TEST_FLAGS)

bool FHFWalkthroughRegeneratedCollisionTest::RunTest(const FString& Parameters)
{
	using namespace HouseForgeWalkthrough;

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("An editor world is open"), World))
	{
		return false;
	}

	ClearHouseForgeActors(World);

	// A door in a 400 cm wall running along +X, so the wall normal is +Y.
	AHFOpeningActor* Door = World->SpawnActor<AHFOpeningActor>();
	if (!TestNotNull(TEXT("A door actor spawns"), Door))
	{
		return false;
	}
	ON_SCOPE_EXIT{ if (IsValid(Door)) { Door->Destroy(); } };

	Door->HostWall.Id = TEXT("W1");
	Door->HostWall.Start = FVector2D(0.0, 0.0);
	Door->HostWall.End = FVector2D(400.0, 0.0);
	Door->HostWall.Thickness = 20.0;
	Door->HostWall.Height = 300.0;

	Door->Opening.Id = TEXT("D1");
	Door->Opening.WallId = TEXT("W1");
	Door->Opening.OffsetAlongWall = 200.0;
	Door->Opening.Width = 90.0;
	Door->Opening.Height = 210.0;
	Door->Opening.Kind = EHFOpeningKind::Door;
	Door->Opening.Swing = EHFSwing::InwardLeft;
	Door->Regenerate();

	// Still a world trace on the pawn channel - what is being proved is that this body is in the
	// physics scene and answers a character's query - but resolved to the leaf's own component.
	// The editor world is shared by every test in the run, and several of them spawn a door on
	// exactly this wall at exactly this offset; "something blocked" would be answering for whichever
	// actor happened to be standing there.
	auto LeafBlocksAt = [&](double Z)
	{
		UDynamicMeshComponent* Leaf = Door->GetPartComponent(AHFOpeningActor::LeafPartId);

		TArray<FHitResult> Hits;
		WalkTraceMulti(World, FVector(200.0, -60.0, Z), FVector(200.0, 60.0, Z), Hits);

		for (const FHitResult& Hit : Hits)
		{
			if (Hit.GetComponent() == Leaf)
			{
				return true;
			}
		}
		return false;
	};

	// 180 cm is inside a 210 cm leaf and over the head of a 120 cm one.
	if (!TestTrue(TEXT("The full-height door blocks a walking trace at 180 cm"), LeafBlocksAt(180.0)))
	{
		return false;
	}

	Door->Opening.Height = 120.0;
	Door->Regenerate();

	TestFalse(TEXT("Shortened, its collision comes down with it rather than staying at full height"),
		LeafBlocksAt(180.0));

	// And the leaf that is left still collides, so the check above cannot pass by the body simply
	// having been thrown away.
	TestTrue(TEXT("The shortened door still blocks below its new head"), LeafBlocksAt(60.0));

	return true;
}

#undef HF_TEST_FLAGS

#endif // WITH_DEV_AUTOMATION_TESTS
