// Copyright Siddartha G. All Rights Reserved.

#include "HouseForgeEditor.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Actors/HFElementActors.h"
#include "Actors/HFHouseActor.h"
#include "Actors/HFOpeningActor.h"
#include "Components/DynamicMeshComponent.h"
#include "Editor.h"
#include "Engine/World.h"
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

	/** Builds the reference flat and hands back the house actor holding it. */
	AHFHouseActor* BuildReferenceFlat(UWorld* World, FHFHouseSpec& OutSpec)
	{
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
		if (!WalkTrace(World, Start, End, Hit))
		{
			AddError(FString::Printf(
				TEXT("Nothing to stand on in room '%s': a pawn-channel trace with simple collision found no floor at (%.0f, %.0f). A walkthrough falls through it."),
				*Room.Id.ToString(), Plan.X, Plan.Y));
			continue;
		}

		// And it is the floor, at the floor's level - not a fixture the trace happened to clip.
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
		if (!WalkTrace(World, Start, End, Hit))
		{
			AddError(FString::Printf(
				TEXT("Wall '%s' does not block a walking trace at (%.0f, %.0f, %.0f). It renders but a walkthrough passes through it."),
				*Wall.Id.ToString(), Plan.X, Plan.Y, Z));
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

#undef HF_TEST_FLAGS

#endif // WITH_DEV_AUTOMATION_TESTS
