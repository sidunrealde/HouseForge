// Copyright Siddartha G. All Rights Reserved.

#include "Actors/HFElementActors.h"

#include "Components/DynamicMeshComponent.h"
#include "Components/RectLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Geometry/HFGenerators.h"
#include "Geometry/HFMeshOps.h"
#include "HouseForge.h"
#include "Materials/HFMaterialLibrary.h"

using namespace UE::Geometry;

FHFEditableWriteScope::FHFEditableWriteScope(UDynamicMeshComponent* InComponent)
	: Component(InComponent)
	, bWasEditable(InComponent == nullptr || InComponent->IsEditable())
{
	if (Component != nullptr && !bWasEditable)
	{
		Component->SetIsEditable(true);
	}
}

FHFEditableWriteScope::~FHFEditableWriteScope()
{
	if (Component != nullptr && !bWasEditable)
	{
		Component->SetIsEditable(false);
	}
}

AHFElementActor::AHFElementActor()
{
	PrimaryActorTick.bCanEverTick = false;

	Mesh = CreateDefaultSubobject<UDynamicMeshComponent>(TEXT("Mesh"));
	SetRootComponent(Mesh);

	// Complex collision only: these are thin boxed shapes, and simple collision would fill the
	// door openings back in - you could not walk through a doorway that had been cut out.
	//
	// Both flags, and they are not the same flag. CollisionType says which collision to use;
	// bEnableComplexCollision says whether to build any. Setting only the first asks for complex
	// collision that was never cooked, and a component with no simple shapes either then has no
	// collision at all - it renders correctly and a walkthrough falls straight through it.
	Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Mesh->SetCollisionProfileName(TEXT("BlockAll"));
	Mesh->CollisionType = ECollisionTraceFlag::CTF_UseComplexAsSimple;
	Mesh->bEnableComplexCollision = true;
	Mesh->SetGenerateOverlapEvents(false);

	// Tangents derived from the mesh's own UVs and normals rather than taken from it.
	//
	// The default is "From Dynamic Mesh", and nothing here ever calls EnableTangents, so
	// HasTangentSpace() is false and the component silently falls back to MakePerpVectors - an
	// arbitrary basis with no relationship to the surface's UVs. Nothing fails and nothing logs;
	// with the flat colours that exist today the output is indistinguishable from correct, and it
	// only becomes visible when the materials milestone puts normal maps on top of it, at which
	// point it reads as a material bug rather than a geometry-attribute one.
	Mesh->SetTangentsType(EDynamicMeshComponentTangentsMode::AutoCalculated);
}

void AHFElementActor::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	WatchForEdits();
}

void AHFElementActor::PostRegisterAllComponents()
{
	Super::PostRegisterAllComponents();

	// Edit detection has to be armed here, not only in PostInitializeComponents.
	//
	// AActor::PostActorConstruction gates PostInitializeComponents on World->AreActorsInitialized(),
	// which is false for an editor world - so that path runs in PIE only. Element actors are also
	// deliberately not regenerated on load, so CommitMesh does not run either. Between the two,
	// an element that came back from a saved level had no binding at all: take the Modeling Tools
	// to a wall, press Build Geometry, and the modelling work is gone without a word. That is the
	// silent, unrecoverable loss .claude/rules/04-conventions.md calls out.
	//
	// WatchForEdits is idempotent, so this sits safely alongside the CommitMesh path.
	WatchForEdits();
}

void AHFElementActor::PostLoad()
{
	Super::PostLoad();

	// A level saved in Baked mode has to come back baked, and a level whose assets went missing in
	// the meantime has to come back VISIBLE. Both are ReconcileBakeState's job. Deferred to
	// PostRegisterAllComponents would be too late for nothing and too early here would be too soon
	// for the component list; PostLoad is where the actor's own serialised state is complete.
	ReconcileBakeState();
}

void AHFElementActor::WatchForEdits()
{
	if (bWatching || Mesh == nullptr)
	{
		return;
	}

	// These are dynamic meshes so an artist can take Unreal's Modeling Tools to them after
	// generation. Watching for changes is what makes that safe - anything that edits the mesh and
	// is not us marks the element as hand-edited, and it then opts out of regeneration.
	Mesh->OnMeshChanged.AddUObject(this, &AHFElementActor::HandleMeshChanged);
	bWatching = true;
}

void AHFElementActor::HandleMeshChanged()
{
	if (bGenerating)
	{
		return;
	}

	// BEFORE the bArtistEdited early-out, and that ordering is the point. A second sculpt on an
	// already-edited element still changes the geometry, so it still has to mark the bake stale -
	// gating the revision on the flag would leave every edit after the first invisible to the bake.
	MarkMeshRevisionChanged();

	// AN INVISIBLE MESH MUST NOT BE THE THING BEING SCULPTED.
	//
	// In Baked mode the dynamic component is hidden, so an artist who somehow reaches it edits
	// something they cannot see, watches nothing happen, and undoes work that in fact applied. The
	// switch comes back to Dynamic so what they are editing is what they are looking at.
	//
	// Belt and braces rather than the safety mechanism: ApplyRenderMode clears the baked component's
	// UStaticMesh while Dynamic, which is what actually keeps the Modeling Tools pointed at the live
	// mesh (HouseForge.Bake.Probe.ToolTargetSelection). This is here for the reverse case - Baked
	// mode, where the tools correctly target the baked asset and an edit there would be thrown away
	// by the next re-bake.
	if (bUnbakeOnHandEdit && RenderMode == EHFRenderMode::Baked)
	{
		UE_LOG(LogHouseForge, Log,
			TEXT("'%s' was edited by hand while baked, so it is showing its live mesh again. The baked asset is kept and can be re-baked."),
			*GetName());
		SetRenderMode(EHFRenderMode::Dynamic);
	}

	if (bArtistEdited)
	{
		return;
	}

	bArtistEdited = true;

	UE_LOG(LogHouseForge, Log,
		TEXT("'%s' was edited by hand; it will keep those edits and no longer regenerate. Use Revert To Generated to undo that."),
		*GetName());
}

void AHFElementActor::Regenerate()
{
	if (bArtistEdited)
	{
		// Silently refusing is deliberate. Rebuilding here would throw away modelling work, and
		// that loss tends to be noticed only long after it happened.
		UE_LOG(LogHouseForge, Verbose,
			TEXT("Skipping regeneration of '%s': it has been edited by hand."), *GetName());
		return;
	}

	CommitMesh(BuildMesh());
	FlushPendingRebake();
}

void AHFElementActor::RevertToGenerated()
{
	bArtistEdited = false;
	CommitMesh(BuildMesh());
	FlushPendingRebake();
}

void AHFElementActor::CommitMesh(FDynamicMesh3&& Generated)
{
	if (Mesh == nullptr)
	{
		return;
	}

	WatchForEdits();

	// Our own write must not look like an artist edit.
	TGuardValue<bool> Guard(bGenerating, true);

	// And it must not be REFUSED by the editable flag the bake sets. See FHFEditableWriteScope: a
	// baked element that could not regenerate would come back "baked and current" holding the
	// previous plan's geometry, with nothing said about it.
	FHFEditableWriteScope Editable(Mesh);

	// The composing layer's single pass over generated geometry: chamfer the arrises, re-project
	// UV0 over the facets that produced, and lay out the lightmap channel. Here rather than in the
	// generators because a bevel is the one operation in this plugin that is NOT idempotent, and a
	// generator that beveled its own output would have its chamfers chamfered again by every
	// composition it was appended into. See FHFRenderFinish.
	FHFMeshOps::FinishForRender(Generated, RenderFinish, FlushVolumes);

	// The last thing done to a generated mesh, after every boolean and every append. The material
	// id is a pure function of the polygroup, so deriving it here rather than inside the generators
	// means no mesh operation has to be trusted to carry it - and no generator has to reach for an
	// asset to know what it is being materialled with.
	FHFMeshOps::AssignMaterialIdsFromRoles(Generated);

	Mesh->SetMesh(MoveTemp(Generated));
	UHFMaterialLibrary::Get()->ApplyTo(Mesh);
	Mesh->NotifyMeshUpdated();
	Mesh->UpdateCollision(false);

	// Unconditionally, and including under the bGenerating guard that stops the write above reading
	// as a hand edit. The guard is about AUTHORSHIP; the revision is about whether the geometry is
	// still the geometry the baked asset was made from, and a generated change breaks that just as
	// completely as a sculpted one.
	MarkMeshRevisionChanged();
}

// =================================================================================== the bake
//
// Everything from here to the end of AHFElementActor is the reversible bake. Read
// Docs/PanelAndBakeDesign.md 4 alongside it. The one rule the whole section serves:
//
//     THE FDynamicMesh3 IS NEVER READ FOR ANYTHING BUT A COPY, AND NEVER WRITTEN AT ALL.
//
// Bake creates an asset and flips component state. Unbake flips it back. That is why unbake is
// instant, why it is exact, and why there is no confirmation dialog anywhere near it.

void AHFElementActor::GetBakeSourceComponents(TArray<UDynamicMeshComponent*>& OutComponents) const
{
	OutComponents.Reset();
	if (Mesh != nullptr)
	{
		OutComponents.Add(Mesh);
	}
}

const FGuid& AHFElementActor::EnsureBakeOwnerGuid()
{
	if (!BakeOwnerGuid.IsValid())
	{
		BakeOwnerGuid = FGuid::NewGuid();
	}
	return BakeOwnerGuid;
}

void AHFElementActor::MarkMeshRevisionChanged()
{
	++MeshRevision;
}

bool AHFElementActor::HasAnyBakedAsset() const
{
	for (const FHFBakedPart& Part : BakedParts)
	{
		if (Part.BakedMesh != nullptr)
		{
			return true;
		}
	}
	return false;
}

bool AHFElementActor::HasAllBakedAssets() const
{
	if (BakedParts.IsEmpty())
	{
		return false;
	}

	bool bAnyReal = false;

	for (const FHFBakedPart& Part : BakedParts)
	{
		if (Part.BakedMesh != nullptr)
		{
			bAnyReal = true;
			continue;
		}

		// A part whose source held no triangles was baked correctly by producing nothing. See
		// FHFBakedPart::bSourceWasEmpty - the alternative is one degenerate wall turning "Bake all"
		// over a whole flat red.
		if (!Part.bSourceWasEmpty)
		{
			return false;
		}
	}

	// Every part empty is not a baked element, it is an element with no geometry. Switching such a
	// thing to Baked would be a switch with nothing on either side of it.
	return bAnyReal;
}

bool AHFElementActor::IsBakeStale() const
{
	for (const FHFBakedPart& Part : BakedParts)
	{
		// Empty parts count. A wall that had eaten itself and has since been given its geometry back
		// has a part that baked to nothing at an older revision, and that part is exactly the one that
		// needs re-baking.
		const bool bBaked = Part.BakedMesh != nullptr || Part.bSourceWasEmpty;
		if (bBaked && Part.BakedAtMeshRevision != MeshRevision)
		{
			return true;
		}
	}
	return false;
}

UStaticMeshComponent* AHFElementActor::EnsureBakedComponent(int32 PartIndex, UDynamicMeshComponent* Source)
{
	if (!BakedParts.IsValidIndex(PartIndex) || !IsValid(Source))
	{
		return nullptr;
	}

	FHFBakedPart& Part = BakedParts[PartIndex];
	if (IsValid(Part.Component))
	{
		return Part.Component;
	}

	// Created lazily rather than as a constructor default subobject, which is a deliberate departure
	// from Docs/PanelAndBakeDesign.md 4.3. The design gave part 0 a CreateDefaultSubobject so it
	// would exist without being made; but AHFArticulatedActor already creates every one of its mesh
	// components this way and they round-trip through save and load perfectly well, so the subobject
	// buys nothing except a second code path and one always-present component on all 150-odd elements
	// of a flat that may never be baked at all.
	const FName ComponentName = MakeUniqueObjectName(this, UStaticMeshComponent::StaticClass(),
		*FString::Printf(TEXT("Baked_%d"), PartIndex));

	UStaticMeshComponent* Component = NewObject<UStaticMeshComponent>(this, ComponentName);
	if (Component == nullptr)
	{
		return nullptr;
	}

	// PARENTED TO THE COMPONENT IT STANDS IN FOR, not to the actor root. That single line is what
	// keeps rule 04's "a bake must not weld a chest of drawers into a block" true: a baked shutter
	// hangs off the dynamic shutter component, so it inherits that part's live articulated pose for
	// free and opening the wardrobe still opens it.
	Component->SetupAttachment(Source);

	// MOVABLE, and this is a Lumen decision rather than a gameplay one. Mobility appears nowhere in
	// the chain that decides Lumen scene membership - FDistanceFieldSceneData::AddPrimitive gates on
	// eight proxy flags and mobility is not one of them, and mesh cards live on the asset - so a
	// movable static mesh is in the Lumen scene exactly as a static one is. What mobility DOES change
	// is the cost of moving: UStaticMeshComponent::ShouldRecreateProxyOnUpdateTransform returns true
	// for anything that is not Movable, so a Static-mobility baked shutter would destroy and rebuild
	// its proxy every time it opened, forcing LumenRemovePrimitive + LumenAddPrimitive and a full
	// surface-cache re-capture. Movable re-transforms the cards and keeps the captured pages.
	Component->SetMobility(EComponentMobility::Movable);

	// The whole slot table, matching the dynamic side, so a baked element is reachable from the
	// material panel by surface role. The bake service writes one material slot per role into the
	// asset; this is the component-side override that a live finish change writes through.
	Component->SetGenerateOverlapEvents(false);

	Component->RegisterComponent();
	AddInstanceComponent(Component);

	Part.Component = Component;
	Part.SourceComponentName = (Source == Mesh) ? NAME_None : Source->GetFName();

	return Component;
}

int32 AHFElementActor::SyncBakedPartsToSources(TArray<FSoftObjectPath>* OutOrphaned)
{
	TArray<UDynamicMeshComponent*> Sources;
	GetBakeSourceComponents(Sources);

	int32 Dropped = 0;

	// Parts past the end of the source list belong to geometry that no longer exists - a drawer the
	// parameters stopped calling for, most likely. The COMPONENT goes; the ASSET is left on disk and
	// reported as an orphan, because deleting a user's assets from inside a regeneration path is not
	// a thing this plugin does. The orphan scan offers it with the user looking at it.
	for (int32 Index = BakedParts.Num() - 1; Index >= Sources.Num(); --Index)
	{
		if (OutOrphaned != nullptr && BakedParts[Index].BakedAssetPath.IsValid())
		{
			OutOrphaned->Add(BakedParts[Index].BakedAssetPath);
		}

		if (IsValid(BakedParts[Index].Component))
		{
			RemoveInstanceComponent(BakedParts[Index].Component);
			BakedParts[Index].Component->DestroyComponent();
		}

		BakedParts.RemoveAt(Index);
		++Dropped;
	}

	// New parts get an empty slot, so a bake can fill it. An empty slot is not an asset, so
	// HasAllBakedAssets() is false until it is filled and the element stays Dynamic in the meantime -
	// which is the correct answer for a wardrobe that just grew a drawer nobody has baked.
	while (BakedParts.Num() < Sources.Num())
	{
		BakedParts.AddDefaulted();
	}

	for (int32 Index = 0; Index < Sources.Num(); ++Index)
	{
		if (IsValid(Sources[Index]))
		{
			BakedParts[Index].SourceComponentName = (Sources[Index] == Mesh) ? NAME_None : Sources[Index]->GetFName();
		}
	}

	return Dropped;
}

void AHFElementActor::AdoptBakedMesh(int32 PartIndex, FName InSourceComponentName, UStaticMesh* InBakedMesh, int32 AtRevision,
	bool bInSourceWasEmpty)
{
	TArray<UDynamicMeshComponent*> Sources;
	GetBakeSourceComponents(Sources);

	if (!Sources.IsValidIndex(PartIndex))
	{
		return;
	}

	SyncBakedPartsToSources();
	if (!BakedParts.IsValidIndex(PartIndex))
	{
		return;
	}

	FHFBakedPart& Part = BakedParts[PartIndex];
	Part.SourceComponentName = InSourceComponentName;
	Part.BakedMesh = InBakedMesh;
	Part.BakedAssetPath = (InBakedMesh != nullptr) ? FSoftObjectPath(InBakedMesh) : FSoftObjectPath();
	Part.bSourceWasEmpty = bInSourceWasEmpty;
	Part.BakedAtMeshRevision = (InBakedMesh != nullptr || bInSourceWasEmpty) ? AtRevision : INDEX_NONE;

	if (InBakedMesh == nullptr)
	{
		// An empty part still has a component to switch off, if a previous bake left one there.
		if (BakedParts[PartIndex].Component != nullptr)
		{
			BakedParts[PartIndex].Component->SetStaticMesh(nullptr);
		}
		return;
	}

	UDynamicMeshComponent* Source = Sources[PartIndex];
	UStaticMeshComponent* Component = EnsureBakedComponent(PartIndex, Source);
	if (Component == nullptr)
	{
		return;
	}

	// Captured while the dynamic side is still live, which is the only moment it can be read
	// correctly. See FHFBakedPart::SourceCollisionEnabled - a fan rotor is QueryOnly on purpose and
	// must not come back from a bake as a wall.
	//
	// Guarded exactly as ApplyRenderMode guards it, and for the same reason: on a RE-BAKE this runs
	// while the element is already baked, so the source is sitting at the NoCollision this feature
	// put there. Recording that would make unbake restore "blocks nothing".
	if (IsValid(Source))
	{
		if (Source->GetCollisionEnabled() != ECollisionEnabled::NoCollision)
		{
			Part.SourceCollisionEnabled = Source->GetCollisionEnabled();
		}
		Component->SetCollisionProfileName(Source->GetCollisionProfileName());
	}

	// The asset itself carries CTF_UseComplexAsSimple, set at creation by FHFBakeService, so
	// collision matches the visual mesh - including on an open door, which rule 04 names explicitly.
	// Nothing is written to the body setup from here: it belongs to the asset and is shared.
	Component->SetStaticMesh(InBakedMesh);
	UHFMaterialLibrary::Get()->ApplyTo(Component);
}

void AHFElementActor::ApplyRenderMode(EHFRenderMode Mode)
{
	const bool bWantsBaked = (Mode == EHFRenderMode::Baked);
	const bool bBaked = bWantsBaked && HasAllBakedAssets();

	// NEVER RENDER NOTHING. A request to show a bake that is not there falls back to the live mesh
	// and says so through bBakeAssetMissing, rather than leaving a hole in the flat.
	bBakeAssetMissing = bWantsBaked && !bBaked;

	// AN ELEMENT THAT HAS NEVER BEEN BAKED IS NOT TOUCHED AT ALL.
	//
	// This is called at the end of every generation path, so it runs on all 150-odd elements of a
	// flat whether or not anybody has ever baked anything. Restoring collision from a record that
	// does not exist would mean restoring a DEFAULT - and a fan rotor is QueryOnly on purpose
	// (AHFArticulatedActor::ApplyPartCollision), so a default of QueryAndPhysics would turn every
	// rotor in the flat into a frozen blade a pawn walks into.
	if (BakedParts.IsEmpty() && !bBaked)
	{
		RenderMode = EHFRenderMode::Dynamic;
		return;
	}

	TArray<UDynamicMeshComponent*> Sources;
	GetBakeSourceComponents(Sources);

	for (int32 Index = 0; Index < Sources.Num(); ++Index)
	{
		UDynamicMeshComponent* Source = Sources[Index];
		if (!IsValid(Source))
		{
			continue;
		}

		// WHAT THIS PART BLOCKS IS RECORDED BEFORE ANYTHING IS SWITCHED, and only ever from a
		// component that is currently showing its own collision rather than ours.
		//
		// NoCollision is never something a generator declares. Exactly two values reach a bake
		// source - QueryAndPhysics from AHFElementActor's constructor, and QueryOnly for a rotor
		// from AHFArticulatedActor::ApplyPartCollision - and NoCollision is written to a source by
		// precisely one thing: this function, suppressing it while baked. So the guard below is not
		// a heuristic, it is the exact complement of our own write.
		//
		// Without it, a RE-BAKE reads the source while the element is already baked, records
		// "blocks nothing" as the value to restore, and the next unbake hands that back faithfully.
		// The element is then visible, live, editable, correct-looking in both modes - and
		// completely passable, with nothing logged and nothing to see. A whole flat loses its
		// collision the first time a misread is corrected after baking. Found by
		// HouseForge.Bake.RebakingKeepsTheCollisionUnbakeRestores, not by reading.
		//
		// Recording in both modes rather than only on the way in is what keeps the record current
		// when a regeneration re-declares a part: AHFArticulatedActor::ApplyPartCollision writes the
		// fresh value onto the dynamic component, and this reads it on the way past.
		if (BakedParts.IsValidIndex(Index) && Source->GetCollisionEnabled() != ECollisionEnabled::NoCollision)
		{
			BakedParts[Index].SourceCollisionEnabled = Source->GetCollisionEnabled();
		}

		Source->SetVisibility(!bBaked);
		Source->SetHiddenInGame(bBaked);

		// COLLISION SWITCHES WITH VISIBILITY. Leaving both on double-traces every wall in the flat
		// and leaves complex-as-simple collision sitting under a mesh the user believes is the only
		// thing there.
		//
		// Restored only from what was actually RECORDED at bake time. A part with no baked twin -
		// one the parameters have just grown, say - is left exactly as its generator set it up,
		// because this code has no idea what that part is meant to block.
		const bool bRecorded = BakedParts.IsValidIndex(Index) && BakedParts[Index].Component != nullptr;
		if (bBaked)
		{
			Source->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		}
		else if (bRecorded)
		{
			Source->SetCollisionEnabled(BakedParts[Index].SourceCollisionEnabled.GetValue());
		}

		// UDynamicMeshComponentToolTargetFactory::CanBuildTarget tests IsEditable() explicitly
		// (DynamicMeshComponentToolTarget.cpp:279), so this correctly drops the dynamic mesh out of
		// the candidate list in Baked mode. It is the ONE part of the design's original mitigation
		// that measured as doing what it claimed.
		Source->SetIsEditable(!bBaked);
	}

	for (int32 Index = 0; Index < BakedParts.Num(); ++Index)
	{
		FHFBakedPart& Part = BakedParts[Index];
		if (!IsValid(Part.Component))
		{
			continue;
		}

		Part.Component->SetVisibility(bBaked);
		Part.Component->SetHiddenInGame(!bBaked);
		Part.Component->SetCollisionEnabled(bBaked ? Part.SourceCollisionEnabled.GetValue() : ECollisionEnabled::NoCollision);

		// ============================================================ THE TOOL TARGET FIX
		//
		// MEASURED, NOT ASSUMED, and it is the one place this implementation contradicts the design
		// as originally written. Docs/PanelAndBakeDesign.md 4.4 said to UNREGISTER the baked
		// component while Dynamic. HouseForge.Bake.Probe.ToolTargetSelection built an actor with both
		// components and asked a UToolTargetManager loaded with exactly the factories
		// UModelingToolsEditorMode::Enter loads, in its order, what a Modeling Tool would be handed:
		//
		//   registered + visible      2 candidates -> the BAKED ASSET
		//   registered + hidden       2 candidates -> the BAKED ASSET
		//   UNREGISTERED              2 candidates -> the BAKED ASSET      <- the proposed fix
		//   SetStaticMesh(nullptr)    1 candidate  -> the live dynamic mesh
		//   component destroyed       1 candidate  -> the live dynamic mesh
		//
		// Hiding does nothing and unregistering does nothing, because
		// ToolBuilderUtil::FindAllComponents resolves a selected actor through AActor::GetComponents,
		// which walks OwnedComponents with no registration test (Actor.h:3865), and
		// UStaticMeshComponentToolTargetFactory::CanBuildTarget asks only whether the component holds
		// a writable non-cooked UStaticMesh (StaticMeshComponentToolTarget.cpp:304). Neither
		// registration nor visibility is ever consulted, and the static factory is registered first
		// so it wins every tie.
		//
		// The candidate COUNT matters as much as the winner: USingleSelectionMeshEditingToolBuilder
		// requires exactly one, so in all three two-candidate rows PolyEdit, Sculpt, Displace and
		// Remesh refuse to start at all. A baked element left that way is both dangerous and dead.
		//
		// Clearing the mesh is free and lossless because FHFBakedPart::BakedMesh is a hard reference:
		// the asset stays alive and loaded, so switching back is a pointer assignment and not a load.
		Part.Component->SetStaticMesh(bBaked ? Part.BakedMesh.Get() : nullptr);

		if (bBaked)
		{
			UHFMaterialLibrary::Get()->ApplyTo(Part.Component);
		}
	}

	RenderMode = bBaked ? EHFRenderMode::Baked : EHFRenderMode::Dynamic;
}

void AHFElementActor::SetRenderMode(EHFRenderMode Mode)
{
	ApplyRenderMode(Mode);

	if (bBakeAssetMissing)
	{
		UE_LOG(LogHouseForge, Warning,
			TEXT("'%s' was asked to show baked geometry but has no baked asset for every part, so it is showing its live mesh. Bake it first."),
			*GetName());
	}
}

void AHFElementActor::ReconcileBakeState()
{
	// The source list is authoritative. A level saved before a parameter change that dropped a part
	// comes back with one baked part too many, and reconciling here is what stops ApplyRenderMode
	// indexing past the end of it.
	SyncBakedPartsToSources();

	for (FHFBakedPart& Part : BakedParts)
	{
		// A force-deleted asset leaves a null pointer and a path that still names it, which is the
		// only reason the path is stored separately at all.
		if (Part.BakedMesh == nullptr && Part.BakedAssetPath.IsValid())
		{
			UE_LOG(LogHouseForge, Warning,
				TEXT("The baked asset '%s' for '%s' is missing. The element is showing its live mesh; re-bake to restore it."),
				*Part.BakedAssetPath.ToString(), *GetName());
		}
	}

	// ApplyRenderMode does the falling back, including setting bBakeAssetMissing. Asking for the mode
	// the level was saved in rather than for Dynamic is what makes a level that WAS fine come back
	// baked, and a level that is not come back visible.
	ApplyRenderMode(RenderMode);
}

void AHFElementActor::FlushPendingRebake()
{
	// Read BEFORE re-applying, because ApplyRenderMode falls back to Dynamic when a part has appeared
	// that has no asset yet - and that is precisely the case that needs a re-bake rather than a
	// silent demotion.
	const EHFRenderMode Desired = RenderMode;

	// Regeneration can create components. A wardrobe that has just grown a drawer has a brand new
	// dynamic component, visible and editable, and nothing else would ever tell it that this element
	// is currently showing baked geometry - so it would draw straight through its baked neighbours.
	ApplyRenderMode(Desired);

	if (Desired != EHFRenderMode::Baked || !bAutoRebakeOnRegenerate)
	{
		return;
	}

	if (!IsBakeStale() && !bBakeAssetMissing)
	{
		return;
	}

	if (!FHFBakeHooks::CanBake())
	{
		// Nothing to shout about: a cooked build, or a test that never bound the hook. The element
		// stays baked and stale, which the panel reports and RebakeStale fixes.
		return;
	}

	FString Error;
	if (!FHFBakeHooks::BakeElement.Execute(this, Error))
	{
		UE_LOG(LogHouseForge, Warning,
			TEXT("'%s' changed while baked and could not be re-baked: %s. It is showing its live mesh."),
			*GetName(), *Error);
		SetRenderMode(EHFRenderMode::Dynamic);
	}
}

#if WITH_EDITOR
void AHFElementActor::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// FLIPPING A RENDER SWITCH MUST NOT REBUILD GEOMETRY.
	//
	// Intercepted before the catch-all below, which regenerates for any property declared on an
	// element actor - and RenderMode is one. Without this, ticking the box in the details panel would
	// regenerate the element, bump MeshRevision, and mark the bake it was just asked to show stale.
	const FName Changed = PropertyChangedEvent.GetPropertyName();
	const FName ChangedMember = PropertyChangedEvent.MemberProperty != nullptr
		? PropertyChangedEvent.MemberProperty->GetFName()
		: NAME_None;

	if (Changed == GET_MEMBER_NAME_CHECKED(AHFElementActor, RenderMode)
		|| ChangedMember == GET_MEMBER_NAME_CHECKED(AHFElementActor, RenderMode))
	{
		// Asking for Baked on an element that has never been baked bakes it, rather than refusing.
		// The switch is the user's whole vocabulary here; a toggle that silently does nothing the
		// first time it is used is not a toggle.
		if (RenderMode == EHFRenderMode::Baked && !HasAllBakedAssets() && FHFBakeHooks::CanBake())
		{
			FString Error;
			if (!FHFBakeHooks::BakeElement.Execute(this, Error))
			{
				UE_LOG(LogHouseForge, Warning, TEXT("'%s' could not be baked: %s"), *GetName(), *Error);
			}
		}

		SetRenderMode(RenderMode);
		return;
	}

	// The rest of the bake block is bookkeeping, and none of it describes geometry. Falling through
	// to the catch-all would regenerate the element for ticking a checkbox about re-baking - which
	// would bump MeshRevision and mark the bake stale, so a setting about staleness would create it.
	if (Changed == GET_MEMBER_NAME_CHECKED(AHFElementActor, bAutoRebakeOnRegenerate)
		|| Changed == GET_MEMBER_NAME_CHECKED(AHFElementActor, bUnbakeOnHandEdit)
		|| ChangedMember == GET_MEMBER_NAME_CHECKED(AHFElementActor, BakedParts))
	{
		return;
	}

	// Clearing the flag by hand is a deliberate request to go back to generated geometry.
	if (Changed == GET_MEMBER_NAME_CHECKED(AHFElementActor, bArtistEdited))
	{
		if (!bArtistEdited)
		{
			RevertToGenerated();
		}
		return;
	}

	// ONLY OUR OWN PROPERTIES REBUILD ANYTHING.
	//
	// PostEditChangeProperty is not only the details panel. The engine fires it for its own
	// properties too, and AActor::SetActorLabel is the one that matters here: naming an actor sends a
	// property change for AActor::ActorLabel, which used to land on the Regenerate below. Every
	// element the house builds is labelled the instant it is spawned, so EVERY element generated
	// itself once with default parameters before the composing layer had told it what it was, and
	// again properly a moment later.
	//
	// Wasteful on a wall. Destructive on anything with pose state, because the ghost generation
	// creates the parts: a fan's rotor came into existence at phase 0, and the real phase applied
	// straight afterwards then lost to the rule that an existing part's pose beats a generated
	// default. All six fans in the reference flat came out stopped on the same blade, and every
	// individual step in the chain was correct.
	//
	// MemberProperty first, so a change inside a nested struct - Fan.SweepDiameter, Wall.Thickness -
	// is attributed to the struct's owner rather than to the struct. A null property is the engine
	// saying "assume everything changed", which undo does, so that still rebuilds.
	const FProperty* Edited = PropertyChangedEvent.MemberProperty != nullptr
		? PropertyChangedEvent.MemberProperty
		: PropertyChangedEvent.Property;

	if (Edited != nullptr)
	{
		const UClass* DeclaredOn = Edited->GetOwnerClass();
		if (DeclaredOn == nullptr || !DeclaredOn->IsChildOf(AHFElementActor::StaticClass()))
		{
			return;
		}
	}

	// Editing any parameter rebuilds only this element, which is the point of one actor per
	// element rather than one mesh for the whole house.
	Regenerate();
}
#endif

FDynamicMesh3 AHFWallActor::BuildMesh() const
{
	return FHFGenerators::GenerateWall(Wall, Openings, Structure);
}

FDynamicMesh3 AHFRoomActor::BuildMesh() const
{
	// A ROOM ACTOR CAN EXIST WITHOUT A HOUSE. Dropped into a level by hand, or with its boundary
	// retyped in the details panel, it has no composed plan and no walls to compose one from - and
	// the honest answer for a room with nothing known round it is to skirt the whole perimeter, which
	// is exactly what the resolver returns when handed no walls, openings or fixtures.
	//
	// Keyed on the edge count because that is the one way the stored plan can be wrong without being
	// absent: a boundary edited to a different number of corners leaves runs measured along edges
	// that no longer exist.
	FHFSkirtingParams Section;
	Section.Depth = Skirting.Depth;

	const FHFSkirtingPlan Resolved = (Skirting.Edges.Num() == Room.Boundary.Num())
		? Skirting
		: FHFSkirting::For(Room, {}, {}, {}, {}, Section);

	FDynamicMesh3 Result = FHFGenerators::GenerateFloor(Room, SlabThickness, Resolved);

	if (bGenerateCeilingSlab)
	{
		FHFMeshOps::AppendPreservingRoles(Result, FHFGenerators::GenerateCeilingSlab(Room, SlabThickness));
	}

	return Result;
}

TArray<FVector> AHFCeilingActor::DownlightPositions() const
{
	TArray<FVector> Local = FHFGenerators::CeilingDownlights(Ceiling, Room);

	const FTransform ToWorld = GetActorTransform();
	for (FVector& Position : Local)
	{
		Position = ToWorld.TransformPosition(Position);
	}

	return Local;
}

FDynamicMesh3 AHFCeilingActor::BuildMesh() const
{
	// The mesh is what a bare BuildMesh call is for, but a ceiling is the one element whose design
	// IS partly its lighting, so the two are rebuilt together. See RebuildLights.
	const_cast<AHFCeilingActor*>(this)->RebuildLights();

	return FHFGenerators::GenerateCeiling(Ceiling, Room, FanDrops, FanDropRadius);
}

int32 AHFCeilingActor::RebuildLights()
{
	// Thrown away and rebuilt rather than adjusted: a ceiling that changes template changes how
	// many lights it has and where they are, and reconciling two lists is how a level ends up with
	// the previous design's downlights still burning in the plasterboard.
	for (const TObjectPtr<ULightComponent>& Light : Lights)
	{
		if (Light != nullptr)
		{
			Light->DestroyComponent();
		}
	}
	Lights.Reset();

	if (!bBuildLights)
	{
		return 0;
	}

	const FTransform ToWorld = GetActorTransform();

	auto Common = [this](ULightComponent* Light)
	{
		// Movable, because everything here is regenerated on a property change and a static light
		// would need its lighting rebuilt to notice. The bake milestone is where that changes.
		Light->SetMobility(EComponentMobility::Movable);
		Light->SetUseTemperature(true);
		Light->SetTemperature(static_cast<float>(LightTemperatureKelvin));
		Light->SetCastShadows(true);
		Light->RegisterComponent();
		Light->AttachToComponent(GetRootComponent(),
			FAttachmentTransformRules::KeepWorldTransform);
		Lights.Add(Light);
	};

	// ---------------------------------------------------------------------------- the cove
	//
	// A rect light per straight run, lying in the trough and facing UP - which is the direction a
	// cove throws and the reason its light is worth having. A point light in the middle of the room
	// would light the middle of the room.
	for (const FHFCoveLightRun& Run : FHFGenerators::CeilingCoveLights(Ceiling, Room))
	{
		URectLightComponent* Rect = NewObject<URectLightComponent>(this);
		if (Rect == nullptr)
		{
			continue;
		}

		// X is the direction a rect light emits and Y is its width, so the frame is built from
		// "up" and the direction the run travels.
		const FVector Direction(FMath::Cos(FMath::DegreesToRadians(Run.YawDegrees)),
			FMath::Sin(FMath::DegreesToRadians(Run.YawDegrees)), 0.0);

		Rect->SetWorldTransform(FTransform(
			FRotationMatrix::MakeFromXY(FVector::UpVector, Direction).Rotator(),
			ToWorld.TransformPosition(Run.Centre)));

		Rect->SetSourceWidth(static_cast<float>(Run.Length));
		Rect->SetSourceHeight(static_cast<float>(Run.Width));

		// Barn doors down to the channel, so the wash stays in the trough's own aperture instead of
		// spilling out over the lip into the room - which is the difference between a cove and a
		// bright line at the ceiling.
		Rect->SetBarnDoorAngle(60.0f);
		Rect->SetBarnDoorLength(static_cast<float>(Run.Width * 0.5));

		Rect->SetIntensityUnits(ELightUnits::Lumens);
		Rect->SetIntensity(static_cast<float>(CoveLumensPerMetre * Run.Length / 100.0));

		// It only ever has to reach the surface above it, and a cove that lights the far wall is a
		// cove nobody would recognise.
		Rect->SetAttenuationRadius(static_cast<float>(FMath::Max(Run.ThrowHeight * 6.0, 100.0)));

		Common(Rect);
	}

	// ---------------------------------------------------------------------- the downlights
	//
	// At the APERTURE, up inside the can, which is what CeilingDownlights has always returned and
	// what nothing has ever asked it for. Parented at the soffit instead, a spotlight is shaded by
	// its own trim ring.
	const double ConeDegrees = 45.0;

	for (const FVector& Position : FHFGenerators::CeilingDownlights(Ceiling, Room))
	{
		USpotLightComponent* Spot = NewObject<USpotLightComponent>(this);
		if (Spot == nullptr)
		{
			continue;
		}

		Spot->SetWorldTransform(FTransform(
			FRotator(-90.0, 0.0, 0.0), ToWorld.TransformPosition(Position)));

		Spot->SetInnerConeAngle(static_cast<float>(ConeDegrees * 0.5));
		Spot->SetOuterConeAngle(static_cast<float>(ConeDegrees));
		Spot->SetIntensityUnits(ELightUnits::Lumens);
		Spot->SetIntensity(static_cast<float>(DownlightLumens));
		Spot->SetAttenuationRadius(1200.0f);

		// A real COB has a lens a few centimetres across, and that width is most of what makes the
		// scallop on the wall soft rather than a hard-edged circle.
		Spot->SetSourceRadius(static_cast<float>(Ceiling.Downlight.CutoutRadius()));

		Common(Spot);
	}

	return Lights.Num();
}

FDynamicMesh3 AHFBeamActor::BuildMesh() const
{
	return FHFGenerators::GenerateBeam(Beam, Structure);
}

FDynamicMesh3 AHFColumnActor::BuildMesh() const
{
	return FHFGenerators::GenerateColumn(Column);
}
