// Copyright Siddartha G. All Rights Reserved.

#include "HouseForgeEditor.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Actors/HFElementActors.h"
#include "Actors/HFHouseActor.h"
#include "Actors/HFOpeningActor.h"
#include "Components/DynamicMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Editor.h"
#include "UDynamicMesh.h"
#include "VectorUtil.h"
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
	using namespace UE::Geometry;

	/**
	 * The trace a character makes: the world, a pawn's channel, and simple collision.
	 *
	 * Multi rather than single throughout, because every assertion here needs to know WHICH body
	 * answered, not merely that one did.
	 *
	 * Multi is not "every body on the line", though, and the difference matters. A multi trace
	 * returns overlaps plus the FIRST BLOCKING hit, and everything HouseForge builds is BlockAll -
	 * so a trace aimed through a window frame is answered by the frame and stops there, and the sash
	 * behind it looks as though it has no collision at all. Aim these traces so the body being asked
	 * about is the first solid thing on the line.
	 */
	void WalkTraceMulti(UWorld* World, const FVector& Start, const FVector& End, TArray<FHitResult>& OutHits)
	{
		FCollisionQueryParams Params(TEXT("HFWalkthrough"), /*bTraceComplex*/ false);
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

	/** The actor carrying one named element of this house, or null. */
	AHFElementActor* ElementNamed(const AHFHouseActor* House, const FName& Id)
	{
		if (House == nullptr)
		{
			return nullptr;
		}

		for (const TObjectPtr<AActor>& Actor : House->ElementActors)
		{
			AHFElementActor* Element = Cast<AHFElementActor>(Actor);
			if (Element != nullptr && Element->ElementId == Id)
			{
				return Element;
			}
		}

		return nullptr;
	}

	/**
	 * Which way the floor faces at one point in a room - the RENDERED answer, not the traced one.
	 *
	 * The other half of "is there a floor". A downward trace is answered by collision, and collision
	 * on a UDynamicMeshComponent is the triangle soup with no notion of a front side: a slab whose
	 * triangles all face DOWN blocks a pawn perfectly and is invisible from above under backface
	 * culling. That is a floor you stand on and cannot see, and it has bitten this codebase twice.
	 * Rendering and collision are therefore asked separately, because they fail separately.
	 *
	 * Winding is the authority here, not the normal overlay, because winding is what culls. It is
	 * read with FDynamicMesh3::GetTriNormal - the engine's own function, on the engine's own vertex
	 * order - rather than a cross product written out here, so there is no second convention to get
	 * backwards.
	 *
	 * Triangles are selected by covering the sample point IN PLAN and sitting at the finished floor
	 * level, so skirting undersides and the slab's own soffit 15 cm below are not counted as votes
	 * about the walking surface.
	 */
	void FloorFacingAt(const AHFElementActor* Element, const FVector2D& Plan, double FloorZ,
		int32& OutUp, int32& OutDown)
	{
		OutUp = 0;
		OutDown = 0;

		UDynamicMeshComponent* Component = Element != nullptr ? Element->GetMeshComponent() : nullptr;
		if (Component == nullptr || Component->GetDynamicMesh() == nullptr)
		{
			return;
		}

		const FTransform ToWorld = Component->GetComponentTransform();

		Component->GetDynamicMesh()->ProcessMesh([&](const FDynamicMesh3& Mesh)
		{
			for (const int32 TriangleId : Mesh.TriangleIndicesItr())
			{
				FVector3d A, B, C;
				Mesh.GetTriVertices(TriangleId, A, B, C);

				A = ToWorld.TransformPosition(A);
				B = ToWorld.TransformPosition(B);
				C = ToWorld.TransformPosition(C);

				// At the walking surface, and level enough to be part of it.
				const double MeanZ = (A.Z + B.Z + C.Z) / 3.0;
				if (FMath::Abs(MeanZ - FloorZ) > 0.5
					|| FMath::Max3(A.Z, B.Z, C.Z) - FMath::Min3(A.Z, B.Z, C.Z) > 0.5)
				{
					continue;
				}

				// Under the sample point in plan. Barycentric, so a point on a shared edge is
				// answered by both triangles rather than by neither.
				const FVector2D P0(A.X, A.Y);
				const FVector2D P1(B.X, B.Y);
				const FVector2D P2(C.X, C.Y);

				const double Area2 = (P1.X - P0.X) * (P2.Y - P0.Y) - (P2.X - P0.X) * (P1.Y - P0.Y);
				if (FMath::Abs(Area2) < UE_KINDA_SMALL_NUMBER)
				{
					continue;
				}

				const double W0 = ((P1.X - Plan.X) * (P2.Y - Plan.Y) - (P2.X - Plan.X) * (P1.Y - Plan.Y)) / Area2;
				const double W1 = ((P2.X - Plan.X) * (P0.Y - Plan.Y) - (P0.X - Plan.X) * (P2.Y - Plan.Y)) / Area2;
				const double W2 = 1.0 - W0 - W1;

				if (W0 < -1e-6 || W1 < -1e-6 || W2 < -1e-6)
				{
					continue;
				}

				// The engine's own winding normal: what the renderer culls on.
				const FVector3d Normal = VectorUtil::Normal(A, B, C);
				if (Normal.Z > 0.5)
				{
					++OutUp;
				}
				else if (Normal.Z < -0.5)
				{
					++OutDown;
				}
			}
		});
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

		// ------------------------------------------------------------------ and you can SEE it
		//
		// Asserted separately from the trace above, and deliberately so. The two are different
		// failures with one symptom: a slab wound face-down blocks a pawn and vanishes under
		// backface culling, and a slab that renders but has no cooked body is a hole you fall
		// through while looking at a floor. Neither can be inferred from the other, so neither is
		// allowed to stand in for it.
		AHFElementActor* Slab = ElementNamed(House, Room.Id);
		if (!TestNotNull(*FString::Printf(TEXT("Room '%s' has a floor actor"), *Room.Id.ToString()), Slab))
		{
			continue;
		}

		int32 Up = 0;
		int32 Down = 0;
		FloorFacingAt(Slab, Plan, Room.FloorZ, Up, Down);

		if (Up == 0)
		{
			AddError(FString::Printf(
				TEXT("Room '%s' has no floor to LOOK at over (%.0f, %.0f): %d triangle(s) at Z=%.1f, none of them facing up and %d facing down. Wound face-down, a slab still stops a pawn - so the walking trace above passes while the room reads as having no floor at all."),
				*Room.Id.ToString(), Plan.X, Plan.Y, Up + Down, Room.FloorZ, Down));
		}

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

	// How much of each end of a wall belongs to the wall it butts against.
	//
	// Walls are set out on their CENTRELINES, so where two meet they both own the footprint of the
	// junction. One of them is built through and the other stops at its face - see the build order
	// in AHFHouseActor - so the last half-thickness of the wall that stops is masonry, but it is the
	// OTHER wall's masonry. Traced there, this test asked whether a wall blocks at a point that wall
	// does not occupy, and the honest answer is that the flat is solid and the question was wrong.
	//
	// The thickest wall in the spec bounds how much any junction can take, so no wall has to be
	// looked up and nothing has to be assumed about which one wins.
	double JunctionMargin = 0.0;
	for (const FHFWall& Wall : Spec.Walls)
	{
		JunctionMargin = FMath::Max(JunctionMargin, Wall.Thickness * 0.5);
	}
	JunctionMargin += 5.0;

	for (const FHFWall& Wall : Spec.Walls)
	{
		const double Length = Wall.Length();
		if (Length <= UE_KINDA_SMALL_NUMBER || Wall.Thickness <= 0.0)
		{
			continue;
		}

		if (Length <= JunctionMargin * 2.0)
		{
			// Shorter than its own two junctions. Nothing here is unambiguously this wall's.
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
		const double Span = Length - JunctionMargin * 2.0;
		for (int32 i = 0; i <= Steps; ++i)
		{
			const double Along = JunctionMargin + Span * double(i) / Steps;
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
 * A window's collision follows its sash, so a walkthrough cannot step through an open one.
 *
 * Both halves are needed and neither is sufficient. That the sash blocks where it stands proves it
 * has collision at all; that it stops blocking where it USED to stand would also pass with a body
 * that was simply thrown away. So the sash is asked at both ends of its travel and in both halves
 * of the opening: it must let go of the half it left AND take hold of the half it moved into.
 *
 * The ventilator is asked the same question about a rotation, in the one place a translation and a
 * rotation differ: out in front of the wall, where only a swung sash can reach.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFWalkthroughSashTest,
	"HouseForge.Walkthrough.CollisionFollowsAnOpenSash", HF_TEST_FLAGS)

bool FHFWalkthroughSashTest::RunTest(const FString& Parameters)
{
	using namespace HouseForgeWalkthrough;

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("An editor world is open"), World))
	{
		return false;
	}

	ClearHouseForgeActors(World);

	// A 400 cm wall running along +X, so the wall normal is +Y.
	auto SpawnOpening = [World](EHFOpeningKind Kind, double Width, double Height, double Sill) -> AHFOpeningActor*
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

		Actor->Opening.Id = TEXT("Win1");
		Actor->Opening.WallId = TEXT("W1");
		Actor->Opening.OffsetAlongWall = 200.0;
		Actor->Opening.Width = Width;
		Actor->Opening.Height = Height;
		Actor->Opening.SillHeight = Sill;
		Actor->Opening.Kind = Kind;
		Actor->Regenerate();
		return Actor;
	};

	// ------------------------------------------------------------------------ sliding window
	{
		AHFOpeningActor* Window = SpawnOpening(EHFOpeningKind::SlidingWindow, 150.0, 135.0, 90.0);
		if (!TestNotNull(TEXT("A sliding window spawns"), Window))
		{
			return false;
		}
		ON_SCOPE_EXIT{ if (IsValid(Window)) { Window->Destroy(); } };

		UDynamicMeshComponent* Sash = Window->GetPartComponent(AHFOpeningActor::NearSashPartId);
		if (!TestNotNull(TEXT("The running sash has a component"), Sash))
		{
			return false;
		}

		// Across the wall at mid-pane height, in one half of the opening or the other. The opening
		// spans 125..275, so 150 is well inside the near half and 250 well inside the far one.
		//
		// Run from +Y inward, which is the side the running sash's track is on. The other way round
		// the fixed sash is the first blocking body on the line and the trace never reaches the one
		// being asked about - which reads exactly like a sash with no collision.
		auto SashBlocksAt = [&](double X)
		{
			TArray<FHitResult> Hits;
			WalkTraceMulti(World, FVector(X, 60.0, 157.5), FVector(X, -60.0, 157.5), Hits);

			for (const FHitResult& Hit : Hits)
			{
				if (Hit.GetComponent() == Sash)
				{
					return true;
				}
			}
			return false;
		};

		Window->SetPartOpenAmount(AHFOpeningActor::NearSashPartId, 0.0);
		TestTrue(TEXT("Shut, the sash blocks the half of the window it covers"), SashBlocksAt(150.0));
		TestFalse(TEXT("Shut, it does not block the half its partner covers"), SashBlocksAt(250.0));

		// Ajar is still shut as far as walking through that half goes.
		Window->SetPartOpenAmount(AHFOpeningActor::NearSashPartId, 0.5);
		TestTrue(TEXT("Half open, the sash is still in the way partway across"), SashBlocksAt(200.0));

		Window->SetPartOpenAmount(AHFOpeningActor::NearSashPartId, 1.0);
		TestFalse(TEXT("Open, the half the sash left is clear to walk through"), SashBlocksAt(150.0));
		TestTrue(TEXT("Open, the sash blocks where it has moved TO"), SashBlocksAt(250.0));

		Window->SetPartOpenAmount(AHFOpeningActor::NearSashPartId, 0.0);
	}

	// -------------------------------------------------------------------- top-hung ventilator
	{
		AHFOpeningActor* Vent = SpawnOpening(EHFOpeningKind::Ventilator, 60.0, 45.0, 210.0);
		if (!TestNotNull(TEXT("A ventilator spawns"), Vent))
		{
			return false;
		}
		ON_SCOPE_EXIT{ if (IsValid(Vent)) { Vent->Destroy(); } };

		UDynamicMeshComponent* Sash = Vent->GetPartComponent(AHFOpeningActor::SashPartId);
		if (!TestNotNull(TEXT("The ventilator sash has a component"), Sash))
		{
			return false;
		}

		auto HitTheSash = [&](const FVector& Start, const FVector& End)
		{
			TArray<FHitResult> Hits;
			WalkTraceMulti(World, Start, End, Hits);

			for (const FHitResult& Hit : Hits)
			{
				if (Hit.GetComponent() == Sash)
				{
					return true;
				}
			}
			return false;
		};

		// Across the wall, at mid-span and mid-height where the frame's own members are not in the
		// way. This is the half that says the sash has collision at all.
		auto SashBlocksAcross = [&]()
		{
			return HitTheSash(FVector(200.0, 60.0, 232.5), FVector(200.0, -60.0, 232.5));
		};

		// Straight down, 6 cm out from the wall. The closed sash and its pull lie within 1.5 cm of
		// the frame centreline and the frame itself within 3, so nothing is on this line until a sash
		// has actually swung out onto it - which is the whole difference between a rotation and a
		// body that stayed where it was cooked.
		auto SashBlocksOutInFront = [&]()
		{
			return HitTheSash(FVector(200.0, 6.0, 270.0), FVector(200.0, 6.0, 200.0));
		};

		Vent->SetPartOpenAmount(AHFOpeningActor::SashPartId, 0.0);
		TestTrue(TEXT("Shut, the ventilator sash blocks its own opening"), SashBlocksAcross());
		TestFalse(TEXT("Shut, it does not reach out into the room"), SashBlocksOutInFront());

		Vent->SetPartOpenAmount(AHFOpeningActor::SashPartId, 1.0);
		TestTrue(TEXT("Open, the sash hangs out into the room and is solid there"), SashBlocksOutInFront());

		Vent->SetPartOpenAmount(AHFOpeningActor::SashPartId, 0.0);
		TestFalse(TEXT("Shut again, the collision comes back with it"), SashBlocksOutInFront());
		TestTrue(TEXT("Shut again, it is back across its own opening"), SashBlocksAcross());
	}

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
