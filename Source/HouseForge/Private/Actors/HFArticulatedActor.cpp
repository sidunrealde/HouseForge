// Copyright Siddartha G. All Rights Reserved.

#include "Actors/HFArticulatedActor.h"

#include "Components/DynamicMeshComponent.h"
#include "HouseForge.h"

using namespace UE::Geometry;

AHFArticulatedActor::AHFArticulatedActor()
{
	PrimaryActorTick.bCanEverTick = false;
}

// ------------------------------------------------------------------------------- part lookup

const FHFPartState* AHFArticulatedActor::FindPart(FName PartId) const
{
	return Parts.FindByPredicate([PartId](const FHFPartState& Part) { return Part.PartId == PartId; });
}

UDynamicMeshComponent* AHFArticulatedActor::GetPartComponent(FName PartId) const
{
	for (int32 Index = 0; Index < Parts.Num(); ++Index)
	{
		if (Parts[Index].PartId == PartId && PartComponents.IsValidIndex(Index))
		{
			return PartComponents[Index];
		}
	}
	return nullptr;
}

bool AHFArticulatedActor::IsPartArtistEdited(FName PartId) const
{
	const FHFPartState* Part = FindPart(PartId);
	return Part != nullptr && Part->bArtistEdited;
}

bool AHFArticulatedActor::HasAnyArtistEdits() const
{
	if (bArtistEdited)
	{
		return true;
	}

	for (const FHFPartState& Part : Parts)
	{
		if (Part.bArtistEdited)
		{
			return true;
		}
	}
	return false;
}

bool AHFArticulatedActor::ShouldPreserveOnRebuild() const
{
	// A hand-edited part is just as unrecoverable as a hand-edited shell, and a house rebuild that
	// only checked the actor-level flag would destroy the actor and take the part with it.
	return HasAnyArtistEdits();
}

// --------------------------------------------------------------------------------- opening

bool AHFArticulatedActor::SetPartOpenAmount(FName PartId, double OpenAmount)
{
	for (FHFPartState& Part : Parts)
	{
		if (Part.PartId == PartId)
		{
			Part.OpenAmount = FMath::Clamp(OpenAmount, 0.0, 1.0);
			ApplyOpenAmounts();
			return true;
		}
	}
	return false;
}

double AHFArticulatedActor::GetPartOpenAmount(FName PartId) const
{
	const FHFPartState* Part = FindPart(PartId);
	return Part != nullptr ? Part->OpenAmount : 0.0;
}

void AHFArticulatedActor::SetMasterOpenAmount(double NewMasterOpenAmount)
{
	SetAllPartsOpenAmount(NewMasterOpenAmount);
}

void AHFArticulatedActor::SetAllPartsOpenAmount(double OpenAmount)
{
	const double Clamped = FMath::Clamp(OpenAmount, 0.0, 1.0);
	MasterOpenAmount = Clamped;

	for (FHFPartState& Part : Parts)
	{
		Part.OpenAmount = Clamped;
	}
	ApplyOpenAmounts();
}

void AHFArticulatedActor::OpenAllParts()
{
	SetAllPartsOpenAmount(1.0);
}

void AHFArticulatedActor::CloseAllParts()
{
	SetAllPartsOpenAmount(0.0);
}

FHFPartPoses AHFArticulatedActor::CapturePartPoses() const
{
	FHFPartPoses Poses;
	Poses.MasterOpenAmount = MasterOpenAmount;

	for (const FHFPartState& Part : Parts)
	{
		if (Part.OpenAmount > 0.0)
		{
			Poses.OpenAmountsByPartId.Add(Part.PartId, Part.OpenAmount);
		}
	}

	return Poses;
}

void AHFArticulatedActor::RestorePartPoses(const FHFPartPoses& Poses)
{
	MasterOpenAmount = FMath::Clamp(Poses.MasterOpenAmount, 0.0, 1.0);

	for (FHFPartState& Part : Parts)
	{
		if (const double* OpenAmount = Poses.OpenAmountsByPartId.Find(Part.PartId))
		{
			Part.OpenAmount = FMath::Clamp(*OpenAmount, 0.0, 1.0);
		}
	}

	ApplyOpenAmounts();
}

void AHFArticulatedActor::ApplyOpenAmounts()
{
	for (int32 Index = 0; Index < Parts.Num(); ++Index)
	{
		UDynamicMeshComponent* Component = PartComponents.IsValidIndex(Index) ? PartComponents[Index].Get() : nullptr;
		if (IsValid(Component))
		{
			// The mesh never moves; only the component does. That is what keeps collision matching
			// the visual mesh on an open door, and what lets a bake ride along unchanged.
			Component->SetRelativeTransform(Parts[Index].CurrentPose());
		}
	}
}

// ------------------------------------------------------------------------------ generation

void AHFArticulatedActor::Regenerate()
{
	if (!bArtistEdited)
	{
		CommitMesh(BuildMesh());
	}
	else
	{
		UE_LOG(LogHouseForge, Verbose,
			TEXT("Keeping the hand-edited fixed geometry of '%s'; its parts still rebuild."), *GetName());
	}

	RegenerateParts(/*bForce*/ false);
	ApplyOpenAmounts();
}

void AHFArticulatedActor::RevertToGenerated()
{
	bArtistEdited = false;
	CommitMesh(BuildMesh());
	RegenerateParts(/*bForce*/ true);
	ApplyOpenAmounts();
}

void AHFArticulatedActor::RegenerateParts(bool bForce)
{
	TArray<FHFMeshPart> Built;
	BuildParts(Built);

	TMap<FName, int32> ExistingByPartId;
	for (int32 Index = 0; Index < Parts.Num(); ++Index)
	{
		ExistingByPartId.Add(Parts[Index].PartId, Index);
	}

	TArray<FHFPartState> NewParts;
	TArray<TObjectPtr<UDynamicMeshComponent>> NewComponents;
	TSet<int32> Reused;
	TSet<FName> SeenIds;
	NewParts.Reserve(Built.Num());
	NewComponents.Reserve(Built.Num());

	for (FHFMeshPart& Part : Built)
	{
		if (Part.PartId.IsNone())
		{
			// Without a stable id the part could never be matched back to its open amount or its
			// hand-edit flag, so it would silently reset on every rebuild. Refuse it loudly.
			UE_LOG(LogHouseForge, Warning,
				TEXT("'%s' produced a moving part with no id; it was dropped."), *GetName());
			continue;
		}

		bool bAlreadySeen = false;
		SeenIds.Add(Part.PartId, &bAlreadySeen);
		if (bAlreadySeen)
		{
			// Two parts sharing an id would fight over one component and over one open amount, and
			// the loser would silently disappear. Better to name the generator bug than to build
			// half a chest of drawers.
			UE_LOG(LogHouseForge, Warning,
				TEXT("'%s' produced two moving parts called '%s'; the second was dropped. Part ids must be unique."),
				*GetName(), *Part.PartId.ToString());
			continue;
		}

		FHFPartState State;
		State.PartId = Part.PartId;
		State.PivotTransform = Part.PivotTransform;
		State.Motion = Part.Motion;
		State.OpenAmount = FMath::Clamp(Part.DefaultOpenAmount, 0.0, 1.0);

		UDynamicMeshComponent* Component = nullptr;

		if (const int32* Index = ExistingByPartId.Find(Part.PartId))
		{
			Reused.Add(*Index);

			// A part the artist posed stays posed. Regeneration is a shape change, not a reason to
			// slam every shutter shut.
			State.OpenAmount = Parts[*Index].OpenAmount;
			State.bArtistEdited = !bForce && Parts[*Index].bArtistEdited;

			Component = PartComponents.IsValidIndex(*Index) ? PartComponents[*Index].Get() : nullptr;
		}

		if (!IsValid(Component))
		{
			Component = CreatePartComponent(Part.PartId);
			State.bArtistEdited = false;
		}

		if (Component == nullptr)
		{
			continue;
		}

		if (!State.bArtistEdited)
		{
			// Our own write must not read as an artist edit.
			TGuardValue<bool> Guard(bGenerating, true);

			Component->SetMesh(MoveTemp(Part.Mesh));
			Component->NotifyMeshUpdated();
			Component->UpdateCollision(false);
		}

		NewParts.Add(State);
		NewComponents.Add(Component);
	}

	// Parts the new build no longer has.
	for (int32 Index = 0; Index < Parts.Num(); ++Index)
	{
		if (Reused.Contains(Index))
		{
			continue;
		}

		UDynamicMeshComponent* Component = PartComponents.IsValidIndex(Index) ? PartComponents[Index].Get() : nullptr;

		if (!bForce && Parts[Index].bArtistEdited && IsValid(Component))
		{
			// Dropping a hand-edited part because the parameters no longer call for it is the same
			// unrecoverable loss as regenerating over it. Keep it and say so; RevertToGenerated is
			// the only thing allowed to discard modelling work.
			UE_LOG(LogHouseForge, Log,
				TEXT("Part '%s' of '%s' is no longer generated but was edited by hand, so it was kept. Use Revert To Generated to remove it."),
				*Parts[Index].PartId.ToString(), *GetName());

			NewParts.Add(Parts[Index]);
			NewComponents.Add(Component);
			continue;
		}

		if (IsValid(Component))
		{
			Component->OnMeshChanged.RemoveAll(this);
			RemoveInstanceComponent(Component);
			Component->DestroyComponent();
		}
	}

	Parts = MoveTemp(NewParts);
	PartComponents = MoveTemp(NewComponents);
}

UDynamicMeshComponent* AHFArticulatedActor::CreatePartComponent(FName PartId)
{
	if (Mesh == nullptr)
	{
		return nullptr;
	}

	const FName ComponentName = MakeUniqueObjectName(this, UDynamicMeshComponent::StaticClass(),
		*FString::Printf(TEXT("Part_%s"), *PartId.ToString()));

	UDynamicMeshComponent* Component = NewObject<UDynamicMeshComponent>(this, ComponentName);
	if (Component == nullptr)
	{
		return nullptr;
	}

	// Parented to the fixed geometry, so a part's transform is read directly as its pose relative
	// to the carcass it hangs on.
	Component->SetupAttachment(Mesh);

	// Same collision treatment as the fixed shell: complex-as-simple, so an open door leaf blocks
	// where it actually is rather than where a convex hull says it is. bEnableComplexCollision is
	// what actually builds that collision - see AHFElementActor's constructor; without it the leaf
	// swings and a walkthrough walks straight through it.
	Component->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Component->SetCollisionProfileName(TEXT("BlockAll"));
	Component->CollisionType = ECollisionTraceFlag::CTF_UseComplexAsSimple;
	Component->bEnableComplexCollision = true;
	Component->SetGenerateOverlapEvents(false);

	// And the same tangent treatment, for the same reason: the default takes tangents from the
	// FDynamicMesh3 attribute set, nothing here ever enables them, and the component then falls back
	// to an arbitrary perpendicular basis. See AHFElementActor's constructor.
	Component->SetTangentsType(EDynamicMeshComponentTangentsMode::AutoCalculated);

	Component->RegisterComponent();
	AddInstanceComponent(Component);

	Component->OnMeshChanged.AddUObject(this, &AHFArticulatedActor::HandlePartMeshChanged, PartId);

	return Component;
}

// --------------------------------------------------------------------------- edit detection

void AHFArticulatedActor::WatchParts()
{
	for (int32 Index = 0; Index < Parts.Num(); ++Index)
	{
		UDynamicMeshComponent* Component = PartComponents.IsValidIndex(Index) ? PartComponents[Index].Get() : nullptr;
		if (!IsValid(Component))
		{
			continue;
		}

		// Rebind rather than add: a component loaded from a saved level has no live binding, and
		// rebinding an already-bound one twice would report every edit twice.
		Component->OnMeshChanged.RemoveAll(this);
		Component->OnMeshChanged.AddUObject(this, &AHFArticulatedActor::HandlePartMeshChanged, Parts[Index].PartId);
	}
}

void AHFArticulatedActor::HandlePartMeshChanged(FName PartId)
{
	if (bGenerating)
	{
		return;
	}

	for (FHFPartState& Part : Parts)
	{
		if (Part.PartId != PartId || Part.bArtistEdited)
		{
			continue;
		}

		Part.bArtistEdited = true;
		UE_LOG(LogHouseForge, Log,
			TEXT("Part '%s' of '%s' was edited by hand; it keeps those edits and no longer regenerates. Use Revert To Generated to undo that."),
			*PartId.ToString(), *GetName());
		return;
	}
}

// ------------------------------------------------------------------------------- lifecycle

void AHFArticulatedActor::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	WatchParts();
}

void AHFArticulatedActor::PostRegisterAllComponents()
{
	Super::PostRegisterAllComponents();

	// A level that was saved with a wardrobe left open reopens it, and edit detection resumes on
	// parts that came back from disk rather than from generation.
	WatchParts();
	ApplyOpenAmounts();
}

#if WITH_EDITOR
void AHFArticulatedActor::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	const FName Member = PropertyChangedEvent.MemberProperty != nullptr
		? PropertyChangedEvent.MemberProperty->GetFName()
		: NAME_None;

	// Posing is not a parameter change: it must move parts without rebuilding any geometry, or
	// dragging the slider would rebuild the whole fixture on every mouse move.
	if (Member == GET_MEMBER_NAME_CHECKED(AHFArticulatedActor, MasterOpenAmount))
	{
		SetAllPartsOpenAmount(MasterOpenAmount);
		return;
	}

	if (Member == GET_MEMBER_NAME_CHECKED(AHFArticulatedActor, Parts))
	{
		ApplyOpenAmounts();
		return;
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif
