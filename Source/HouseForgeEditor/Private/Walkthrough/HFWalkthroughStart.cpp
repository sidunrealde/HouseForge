// Copyright Siddartha G. All Rights Reserved.

#include "Walkthrough/HFWalkthroughStart.h"

#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerStart.h"
#include "HouseForgeEditor.h"
#include "Model/HFTypes.h"

const FName& FHFWalkthroughStart::Tag()
{
	static const FName Value(TEXT("HouseForge.WalkthroughStart"));
	return Value;
}

namespace
{
	/** Area-weighted centroid of a closed polygon, falling back to the vertex mean on a degenerate one. */
	FVector2D PolygonCentroid(const TArray<FVector2D>& Boundary)
	{
		const int32 Count = Boundary.Num();
		if (Count == 0)
		{
			return FVector2D::ZeroVector;
		}

		double TwiceArea = 0.0;
		FVector2D Weighted = FVector2D::ZeroVector;

		for (int32 Index = 0; Index < Count; ++Index)
		{
			const FVector2D& A = Boundary[Index];
			const FVector2D& B = Boundary[(Index + 1) % Count];

			const double Cross = A.X * B.Y - B.X * A.Y;
			TwiceArea += Cross;
			Weighted += (A + B) * Cross;
		}

		if (FMath::IsNearlyZero(TwiceArea))
		{
			// A zero-area outline - a room drawn as a line, or one point repeated. The vertex mean is
			// the only answer left, and it is at least inside the convex hull of what was drawn.
			FVector2D Mean = FVector2D::ZeroVector;
			for (const FVector2D& P : Boundary)
			{
				Mean += P;
			}
			return Mean / static_cast<double>(Count);
		}

		return Weighted / (3.0 * TwiceArea);
	}

	/**
	 * True when a capsule standing at this plan point would be inside the room OUTLINE with clearance.
	 *
	 * Checked at the four compass points as well as at the centre, so a point near a re-entrant
	 * corner of an L-shaped room is rejected before anything is spawned. This is the cheap test; the
	 * capsule overlap against what is actually built is the expensive one, and it runs second.
	 */
	bool ClearOfTheOutline(const FHFRoom& Room, const FVector2D& Point, double Margin)
	{
		if (!Room.ContainsPoint(Point))
		{
			return false;
		}

		const FVector2D Offsets[] =
		{
			FVector2D(Margin, 0.0), FVector2D(-Margin, 0.0),
			FVector2D(0.0, Margin), FVector2D(0.0, -Margin)
		};

		for (const FVector2D& Offset : Offsets)
		{
			if (!Room.ContainsPoint(Point + Offset))
			{
				return false;
			}
		}

		return true;
	}

	/** True when a character-sized capsule standing here runs into nothing solid. */
	bool CapsuleFits(UWorld* World, const FVector& FeetAt)
	{
		if (World == nullptr)
		{
			// No world to ask. The outline test above is all there is, and saying so by returning
			// true is honest: this function's answer is "nothing objected", not "it is definitely
			// clear".
			return true;
		}

		const double Radius = FHFWalkthroughStart::CapsuleRadius();
		const double HalfHeight = FHFWalkthroughStart::CapsuleHalfHeight();

		// Lifted clear of the slab, or the capsule intersects the floor it is standing on and every
		// candidate in the room is rejected. FloorClearance rather than a local constant because the
		// placement below has to lift it by the SAME amount - see the header.
		const FVector Centre(FeetAt.X, FeetAt.Y,
			FeetAt.Z + HalfHeight + FHFWalkthroughStart::FloorClearance());

		FCollisionQueryParams Params(TEXT("HFWalkthroughStart"), /*bTraceComplex*/ false);

		// ECC_Pawn and simple collision, because that is the query character movement makes. Asking
		// with bTraceComplex true would answer whether the triangles are there rather than whether
		// the complex-as-simple redirect that a capsule sweep depends on is working - which is the
		// distinction HFWalkthroughTests exists to make.
		return !World->OverlapAnyTestByChannel(Centre, FQuat::Identity, ECC_Pawn,
			FCollisionShape::MakeCapsule(static_cast<float>(Radius), static_cast<float>(HalfHeight)),
			Params);
	}
}

const FHFRoom* FHFWalkthroughStart::StartRoom(const FHFHouseSpec& Spec)
{
	// The foyer, which is where a person comes in. First rather than largest, because a plan with
	// two rooms typed Foyer has an entrance lobby and something the drawing also called a foyer, and
	// the first is the one the front door is in.
	for (const FHFRoom& Room : Spec.Rooms)
	{
		if (Room.Type == EHFRoomType::Foyer && Room.Boundary.Num() >= 3)
		{
			return &Room;
		}
	}

	// No foyer: a studio, or a partial plan. The largest room is the one most likely to have space
	// to stand in, and refusing to place a start at all would leave the level unplayable over a
	// missing room type.
	const FHFRoom* Largest = nullptr;
	double LargestArea = 0.0;

	for (const FHFRoom& Room : Spec.Rooms)
	{
		if (Room.Boundary.Num() < 3)
		{
			continue;
		}

		const double Area = Room.Area();
		if (Area > LargestArea)
		{
			LargestArea = Area;
			Largest = &Room;
		}
	}

	return Largest;
}

bool FHFWalkthroughStart::ResolveStart(const FHFHouseSpec& Spec, UWorld* World,
	FTransform& OutTransform, FName& OutRoomId)
{
	const FHFRoom* Room = StartRoom(Spec);
	if (Room == nullptr)
	{
		return false;
	}

	OutRoomId = Room->Id;

	const FVector2D Centre = PolygonCentroid(Room->Boundary);
	const double Margin = CapsuleRadius();

	// ------------------------------------------------------------------ which way to look
	//
	// INTO THE FLAT. A start point in the foyer facing the front door starts the walkthrough looking
	// at the inside of a door, which is the one view of the dwelling nobody wants. Aimed at the mean
	// of every other room's centre, so it faces the body of the flat whichever side of it the
	// entrance happens to be on.
	FVector2D Inward = FVector2D::ZeroVector;
	int32 OtherRooms = 0;

	for (const FHFRoom& Other : Spec.Rooms)
	{
		if (Other.Id == Room->Id || Other.Boundary.Num() < 3)
		{
			continue;
		}

		Inward += PolygonCentroid(Other.Boundary);
		++OtherRooms;
	}

	FVector2D Facing(1.0, 0.0);
	if (OtherRooms > 0)
	{
		const FVector2D Direction = (Inward / static_cast<double>(OtherRooms)) - Centre;
		if (!Direction.IsNearlyZero())
		{
			Facing = Direction.GetSafeNormal();
		}
	}

	const FRotator Rotation(0.0, FMath::RadiansToDegrees(FMath::Atan2(Facing.Y, Facing.X)), 0.0);

	// ------------------------------------------------------------------ where to stand
	//
	// The centre first, because that is where somebody would expect to be put and it is right for
	// every clear room. Everything after it is the search for somewhere else when the centre is
	// occupied - which in the reference flat's 1.8 m foyer, with a shoe rack and a front door leaf
	// in it, is a real possibility rather than a defensive flourish.
	TArray<FVector2D> Candidates;
	Candidates.Add(Centre);

	FBox2D Bounds(ForceInit);
	for (const FVector2D& Point : Room->Boundary)
	{
		Bounds += Point;
	}

	// A grid over the room, coarse enough to be quick and fine enough to find a gap a person fits
	// through. One capsule radius between candidates: any free space large enough to stand in is
	// large enough to contain a grid point.
	const double Step = FMath::Max(CapsuleRadius(), 10.0);
	const FVector2D Size = Bounds.GetSize();

	const int32 StepsX = FMath::Clamp(FMath::FloorToInt32(Size.X / Step), 1, 40);
	const int32 StepsY = FMath::Clamp(FMath::FloorToInt32(Size.Y / Step), 1, 40);

	for (int32 IX = 0; IX <= StepsX; ++IX)
	{
		for (int32 IY = 0; IY <= StepsY; ++IY)
		{
			Candidates.Add(FVector2D(
				Bounds.Min.X + Size.X * (IX / static_cast<double>(StepsX)),
				Bounds.Min.Y + Size.Y * (IY / static_cast<double>(StepsY))));
		}
	}

	// Nearest the centre first, so a room with plenty of space still puts the start somewhere a
	// person would have chosen rather than in whichever corner the grid happened to reach first.
	Candidates.Sort([&Centre](const FVector2D& A, const FVector2D& B)
	{
		return FVector2D::DistSquared(A, Centre) < FVector2D::DistSquared(B, Centre);
	});

	const double FloorZ = Room->FloorZ;

	for (const FVector2D& Candidate : Candidates)
	{
		if (!ClearOfTheOutline(*Room, Candidate, Margin))
		{
			continue;
		}

		const FVector Feet(Candidate.X, Candidate.Y, FloorZ);
		if (!CapsuleFits(World, Feet))
		{
			continue;
		}

		// The ACTOR sits at the capsule's centre, not at the feet - a PlayerStart placed with its
		// origin on the slab spawns a character half inside the floor, and the character movement
		// component resolves that by pushing it somewhere unpredictable.
		//
		// AND AT EXACTLY THE HEIGHT THE CANDIDATE WAS TESTED AT. StandingHeight is the same lift
		// CapsuleFits used; placing a half-height lower would put the start somewhere that had
		// never been proved clear, which is how a start point ends up standing in its own floor.
		OutTransform = FTransform(Rotation,
			FVector(Candidate.X, Candidate.Y, FloorZ + StandingHeight()));

		return true;
	}

	// NOTHING FITS ANYWHERE IN THE ROOM. Reported rather than papered over with the centre: a start
	// point put down where a capsule demonstrably does not fit is the exact failure this class is
	// written to prevent, and returning false lets the caller say so.
	UE_LOG(LogHouseForgeEditor, Warning,
		TEXT("HouseForge found nowhere in room '%s' for a walkthrough to start: every candidate was ")
		TEXT("either outside the outline or blocked. No PlayerStart placed."), *Room->Id.ToString());

	return false;
}

TArray<APlayerStart*> FHFWalkthroughStart::FindIn(UWorld* World)
{
	TArray<APlayerStart*> Found;
	if (World == nullptr)
	{
		return Found;
	}

	for (TActorIterator<APlayerStart> It(World); It; ++It)
	{
		if (IsValid(*It) && It->Tags.Contains(Tag()))
		{
			Found.Add(*It);
		}
	}
	return Found;
}

APlayerStart* FHFWalkthroughStart::EnsureIn(UWorld* World, const FHFHouseSpec& Spec, bool* bOutSpawned)
{
	if (bOutSpawned != nullptr)
	{
		*bOutSpawned = false;
	}

	if (World == nullptr)
	{
		return nullptr;
	}

	FTransform Where;
	FName RoomId;
	if (!ResolveStart(Spec, World, Where, RoomId))
	{
		return nullptr;
	}

	TArray<APlayerStart*> Existing = FindIn(World);

	// More than one is a level that has been built twice by a version of this that did not look
	// first. Keep the first and take the rest away, rather than leaving a level where which start
	// point wins is up to actor iteration order.
	for (int32 Index = 1; Index < Existing.Num(); ++Index)
	{
		if (IsValid(Existing[Index]))
		{
			World->DestroyActor(Existing[Index]);
		}
	}

	APlayerStart* Start = Existing.IsEmpty() ? nullptr : Existing[0];

	if (Start == nullptr)
	{
		FActorSpawnParameters Params;
		Params.ObjectFlags = RF_Transactional;

		Start = World->SpawnActor<APlayerStart>(Where.GetLocation(), Where.Rotator(), Params);
		if (Start == nullptr)
		{
			return nullptr;
		}

		Start->Tags.AddUnique(Tag());

#if WITH_EDITOR
		Start->SetActorLabel(TEXT("HF_WalkthroughStart"));
		Start->SetFolderPath(OutlinerFolder());
#endif

		if (bOutSpawned != nullptr)
		{
			*bOutSpawned = true;
		}
	}
	else
	{
		// MOVED rather than replaced. A start point somebody has nudged in the viewport is still
		// theirs; what a rebuild owes them is that it ends up somewhere a person fits, and moving
		// the existing actor keeps every reference to it intact.
		Start->SetActorTransform(Where);
	}

	UE_LOG(LogHouseForgeEditor, Log,
		TEXT("HouseForge walkthrough start in room '%s' at (%.0f, %.0f, %.0f)."),
		*RoomId.ToString(), Where.GetLocation().X, Where.GetLocation().Y, Where.GetLocation().Z);

	return Start;
}

int32 FHFWalkthroughStart::RemoveFrom(UWorld* World)
{
	int32 Removed = 0;
	for (APlayerStart* Start : FindIn(World))
	{
		if (IsValid(Start) && World->DestroyActor(Start))
		{
			++Removed;
		}
	}
	return Removed;
}
