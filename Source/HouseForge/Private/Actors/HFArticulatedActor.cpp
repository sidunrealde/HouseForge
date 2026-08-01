// Copyright Siddartha G. All Rights Reserved.

#include "Actors/HFArticulatedActor.h"

#include "Components/DynamicMeshComponent.h"
#include "Geometry/HFMeshOps.h"
#include "HouseForge.h"
#include "Materials/HFMaterialLibrary.h"

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

bool AHFArticulatedActor::OpenRunFrom(FName PartId, double OpenAmount)
{
	const FHFPartState* Leaf = FindPart(PartId);
	if (Leaf == nullptr)
	{
		return false;
	}

	// Read the link before anything is written, because the loop below writes through Parts and the
	// pointer above does not survive being reasoned about afterwards.
	const FName Partner = Leaf->Motion.AlternateToPartId;
	const double Clamped = FMath::Clamp(OpenAmount, 0.0, 1.0);

	for (FHFPartState& Part : Parts)
	{
		if (Part.PartId == PartId)
		{
			Part.OpenAmount = Clamped;
		}
		else if (!Partner.IsNone() && Part.PartId == Partner)
		{
			// THE OTHER LEAF IS SHUT, not left where it was. Two leaves of one run driven out
			// together exchange tracks and uncover nothing, which is the exact shape of the defect
			// this whole mechanism exists to keep out - see FHFPartMotion::bMasterOpens.
			Part.OpenAmount = 0.0;
		}
	}

	ApplyOpenAmounts();
	return true;
}

double AHFArticulatedActor::GetPartOpenAmount(FName PartId) const
{
	const FHFPartState* Part = FindPart(PartId);
	return Part != nullptr ? Part->OpenAmount : 0.0;
}

bool AHFArticulatedActor::SetPartSpinTurns(FName PartId, double Turns)
{
	for (FHFPartState& Part : Parts)
	{
		if (Part.PartId != PartId)
		{
			continue;
		}

		if (!Part.Motion.Revolves())
		{
			// Refused rather than silently ignored. A phase written to a door would do nothing at
			// all, and a caller that had confused the two would have no way of finding out.
			return false;
		}

		// Deliberately unclamped. Past a full turn is the whole point.
		Part.SpinTurns = Turns;
		ApplyOpenAmounts();
		return true;
	}
	return false;
}

double AHFArticulatedActor::GetPartSpinTurns(FName PartId) const
{
	const FHFPartState* Part = FindPart(PartId);
	return Part != nullptr ? Part->SpinTurns : 0.0;
}

void AHFArticulatedActor::AdvanceSpinningParts(double DeltaSeconds)
{
	bool bAnySpun = false;

	for (FHFPartState& Part : Parts)
	{
		if (!Part.Motion.Revolves())
		{
			continue;
		}

		// Accumulated, never wrapped: this is what makes a fan turn past 360 degrees and keep going
		// rather than snapping back to where it started every second.
		Part.SpinTurns += Part.Motion.TurnsInSeconds(DeltaSeconds);
		bAnySpun = true;
	}

	if (bAnySpun)
	{
		ApplyOpenAmounts();
	}
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
		// A fan has no open amount to set. Writing one would leave every fan on the fixture looking
		// posed to CapturePartPoses while nothing about it had actually moved.
		if (Part.Motion.Revolves())
		{
			continue;
		}

		// ONE OF A PAIR OF SLIDING LEAVES IS NOT WHAT "OPEN EVERYTHING" REACHES FOR, and it is driven
		// SHUT rather than left alone. Both leaves of a slider can run, and both driven to the same
		// amount exchange tracks and uncover nothing - see FHFPartMotion::bMasterOpens, which is the
		// whole of why the master bedroom wardrobe passed every motion assertion while never opening.
		//
		// Shut rather than untouched, because a master amount has to be a definite pose: a partner
		// leaf left half open from an earlier hand pose would halve the aperture asked for.
		Part.OpenAmount = Part.Motion.bMasterOpens ? Clamped : 0.0;
	}

	// Every part ASKED for the same amount; what they are left holding is whatever the orderings
	// between them allow. That is what makes "open everything" a pose a wardrobe could really be in
	// rather than a diagnostic that drives a drawer through its own shutter.
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
		if (Part.Motion.Revolves())
		{
			// A stopped fan is at phase 0 and has nothing to carry; one stopped anywhere else was
			// stopped there deliberately, usually to photograph a blade in a particular place.
			if (Part.SpinTurns != 0.0)
			{
				Poses.SpinTurnsByPartId.Add(Part.PartId, Part.SpinTurns);
			}
			continue;
		}

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

		if (const double* Turns = Poses.SpinTurnsByPartId.Find(Part.PartId))
		{
			// Not clamped: a phase is a count of revolutions, and clamping it into 0..1 would put
			// every fan back to its first turn.
			Part.SpinTurns = *Turns;
		}
	}

	ApplyOpenAmounts();
}

void AHFArticulatedActor::ApplyOpenAmounts()
{
	// Gearing and sequencing are settled over the whole assembly before anything is posed: a geared
	// part takes its driver's amount, and a sequenced part gets no further than the part in front of
	// it allows. Done here rather than in the setters because every route into a pose ends up here -
	// a details-panel edit, a master open, a rebuild, and a restore after one.
	TArray<FName> Cyclic;
	TArray<FHFUnresolvedDependency> Unresolved;
	if (!FHFArticulation::ResolvePartAmounts(Parts, &Cyclic, &Unresolved))
	{
		TArray<FString> Names;
		Names.Reserve(Cyclic.Num());
		for (const FName& Id : Cyclic)
		{
			Names.Add(Id.ToString());
		}

		// Named rather than hung on. A generator that geared two parts to each other produces a
		// fixture that cannot be posed consistently, and the only useful thing to do about it is say
		// which parts and carry on with the amounts they were asked for.
		UE_LOG(LogHouseForge, Warning,
			TEXT("'%s' declares a circular relationship between its parts (%s); their gearing and sequencing were ignored. A part cannot drive, or wait for, something that waits for it."),
			*GetName(), *FString::Join(Names, TEXT(", ")));
	}

	// A dependency naming a part that does not exist is not refused - the fixture still poses - but
	// it is never harmless, and until this log existed it was completely silent. An ordering that
	// resolves to nothing leaves the part it constrained entirely free, which is the behaviour the
	// interlock was added to remove: the drawer comes out through the shut leaf, and every pose
	// measurement agrees it should have. So it is named, per part, per bad id.
	for (const FHFUnresolvedDependency& Bad : Unresolved)
	{
		UE_LOG(LogHouseForge, Warning,
			TEXT("Part '%s' of '%s' is %s '%s', which is not a part of this fixture; the relationship was ignored and the part moves unconstrained."),
			*Bad.PartId.ToString(), *GetName(),
			Bad.bGearing ? TEXT("geared to") : TEXT("sequenced after"),
			*Bad.MissingPartId.ToString());
	}

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
		State.Collision = Part.Collision;
		State.OpenAmount = FMath::Clamp(Part.DefaultOpenAmount, 0.0, 1.0);
		State.SpinTurns = Part.DefaultSpinTurns;

		UDynamicMeshComponent* Component = nullptr;

		if (const int32* Index = ExistingByPartId.Find(Part.PartId))
		{
			Reused.Add(*Index);

			// A part the artist posed stays posed. Regeneration is a shape change, not a reason to
			// slam every shutter shut - or to jerk every fan back to its starting blade.
			//
			// SO THE POSE OF AN EXISTING PART ALWAYS BEATS A FRESHLY GENERATED DEFAULT, and that is the
			// decision, not an accident of ordering. DefaultSpinTurns seeds a part the first time it
			// comes into existence and never again; after that the phase is the actor's, because
			// somebody may have stopped that fan on a particular blade to photograph it.
			//
			// It is only a safe rule while a part cannot be built before its parameters are known. It
			// could - every element regenerated once at spawn, from AActor::SetActorLabel firing
			// PostEditChangeProperty - and a rotor created at phase 0 by that ghost generation then beat
			// the real phase applied a moment later. Every fan in the flat came out stopped on the same
			// blade. See AHFElementActor::PostEditChangeProperty, which no longer regenerates for a
			// property that is not ours.
			State.OpenAmount = Parts[*Index].OpenAmount;
			State.SpinTurns = Parts[*Index].SpinTurns;
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

			// The same render finish the fixed shell gets. A part that missed it would be the one
			// piece of the flat with sharp arrises and no lightmap channel, which is every shutter,
			// every drawer front and every door leaf in the room - the things closest to camera.
			FHFMeshOps::FinishForRender(Part.Mesh, RenderFinish);

			FHFMeshOps::AssignMaterialIdsFromRoles(Part.Mesh);

			Component->SetMesh(MoveTemp(Part.Mesh));
			Component->NotifyMeshUpdated();
			Component->UpdateCollision(false);
		}

		// Outside the guard above, and outside the bArtistEdited check: the slot table belongs to
		// the component, not to the mesh, so a hand-edited shutter has to be dressed too. A part
		// that opted out of regeneration is still a part of the room being looked at.
		FHFMaterialLibrary::ApplyPlaceholders(Component);

		// Same reasoning for collision: it belongs to the component, and what a part blocks is a
		// property of what that part IS rather than of the mesh currently on it. A hand-modelled fan
		// blade must not become something a pawn can walk into.
		ApplyPartCollision(Component, Part.Collision);

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

void AHFArticulatedActor::ApplyPartCollision(UDynamicMeshComponent* Component, EHFPartCollision Collision)
{
	if (!IsValid(Component))
	{
		return;
	}

	// Complex-as-simple either way. COLLISION FOLLOWS THE VISUAL MESH is not negotiable - an open
	// door leaf has to block where the leaf is and not where a hull says it is - and what varies here
	// is only what is allowed to hit it. bEnableComplexCollision is what actually builds that
	// geometry; without it the leaf swings and a walkthrough goes straight through.
	Component->CollisionType = ECollisionTraceFlag::CTF_UseComplexAsSimple;
	Component->bEnableComplexCollision = true;
	Component->SetGenerateOverlapEvents(false);

	if (Collision == EHFPartCollision::TraceOnly)
	{
		// A rotor. Query only, and blocking nothing that moves through the room: the collision cannot
		// spin with the render, so anything it blocked would be a blade frozen at one azimuth - a
		// wall across part of the sweep and thin air across the rest. Traces still hit the real
		// blades, which is what editor picking and any line-of-sight query need. See
		// EHFPartCollision::TraceOnly for the whole argument.
		Component->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Component->SetCollisionResponseToAllChannels(ECR_Ignore);
		Component->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
		return;
	}

	Component->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Component->SetCollisionProfileName(TEXT("BlockAll"));
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
	// where it actually is rather than where a convex hull says it is. Blocking to begin with;
	// RegenerateParts applies what the part itself declares immediately afterwards, so a rotor never
	// exists as a blocker even for the moment between the two.
	ApplyPartCollision(Component, EHFPartCollision::Blocking);

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
	//
	// BUT IT STILL HAS TO PUT THE COMPONENTS BACK, and skipping the whole chain did not.
	//
	// The engine's contract is a pair: AActor::PreEditChange calls UnregisterAllComponents whenever
	// ReregisterComponentsWhenModified is true - which is every actor in a level - and
	// AActor::PostEditChangeProperty is what registers them again. Returning here without reaching
	// AActor left every component of the fixture unregistered: no render state, no physics state,
	// and actor bounds of exactly zero.
	//
	// So dragging MasterOpenAmount in the details panel - which is the single most obvious thing
	// anybody does to check that a fixture opens - made the whole wardrobe VANISH from the viewport.
	// Not the leaves: the carcass, the shelves, the plinth and the cornice with them. Every part
	// reported the pose it had been asked for, every automation test passed, and the wardrobe was
	// gone. It was found by rendering the master bedroom, not by any assertion.
	//
	// AActor:: rather than Super:: on purpose. AHFElementActor::PostEditChangeProperty regenerates
	// for any property declared on an element actor, and both of these are, so going through it
	// would rebuild the fixture on every frame of a slider drag - the thing this branch exists to
	// avoid. What is wanted is the engine's half of the contract and nothing else.
	if (Member == GET_MEMBER_NAME_CHECKED(AHFArticulatedActor, MasterOpenAmount))
	{
		SetAllPartsOpenAmount(MasterOpenAmount);
		AActor::PostEditChangeProperty(PropertyChangedEvent);
		return;
	}

	if (Member == GET_MEMBER_NAME_CHECKED(AHFArticulatedActor, Parts))
	{
		ApplyOpenAmounts();
		AActor::PostEditChangeProperty(PropertyChangedEvent);
		return;
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif
