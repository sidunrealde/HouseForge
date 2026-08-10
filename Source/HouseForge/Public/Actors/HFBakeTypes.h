// Copyright Siddartha G. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/AssetUserData.h"
#include "Engine/EngineTypes.h"
#include "UObject/SoftObjectPath.h"
#include "HFBakeTypes.generated.h"

class AHFElementActor;
class UStaticMesh;
class UStaticMeshComponent;

/**
 * How an element is currently RENDERED. Not what it is - the dynamic mesh exists in both modes.
 *
 * .claude/rules/04-conventions.md is unambiguous about this: "baking is a rendering choice rather
 * than a one-way door". So there is no Convert, no Flatten and no Replace anywhere in this file.
 * Bake creates an asset and flips which component draws; the FDynamicMesh3 is never read for
 * anything but a copy, and never written at all.
 *
 * WHAT THE SWITCH IS ACTUALLY FOR, and why this stopped being optional polish:
 *
 * Lumen cannot see a UDynamicMeshComponent. FBaseDynamicMeshSceneProxy hardcodes
 * bSupportsDistanceFieldRepresentation and bAffectDistanceFieldLighting to false
 * (BaseDynamicMeshSceneProxy.cpp:65-68) and ComputeDistanceFieldForMesh returns nothing, so software
 * tracing has no geometry at all. Hardware tracing does submit the triangles as a BLAS, but radiance
 * comes from the surface cache and card capture iterates PrimitiveSceneInfo->StaticMeshRelevances -
 * which a dynamic mesh on the default DynamicDraw path never fills. Cards allocated, never captured.
 *
 * The measured consequence is the dangerous kind: the broken configuration renders BRIGHTER
 * (whole-frame luminance 0.260 against 0.083 baked), because unoccluded sky floods through walls
 * Lumen cannot see. A wrong render looks bright and cheerful. Evidence: Saved/Review/lumen/.
 *
 * So Baked is what puts the flat into the Lumen scene, and that is a correctness requirement rather
 * than a performance option.
 */
UENUM(BlueprintType)
enum class EHFRenderMode : uint8
{
	/** The generated UDynamicMeshComponent draws and collides. Editable with the Modeling Tools. */
	Dynamic,

	/** A baked UStaticMesh draws and collides. The dynamic mesh is still there, untouched. */
	Baked
};

/**
 * One baked static mesh, standing in for one dynamic mesh component.
 *
 * ONE PER SOURCE COMPONENT, NOT ONE PER ACTOR. Rule 04: "Baking a fixture bakes each part separately
 * and keeps the articulation; a bake must not weld a chest of drawers into a block." A wardrobe with
 * four shutters bakes to five assets on five components, each parented to the dynamic component it
 * stands in for, so it inherits that part's live articulated pose for free and opening a shutter
 * still opens it.
 */
USTRUCT(BlueprintType)
struct HOUSEFORGE_API FHFBakedPart
{
	GENERATED_BODY()

	/**
	 * The dynamic mesh component this was baked from. NAME_None means the actor's root mesh.
	 *
	 * BakedParts is index-parallel to GetBakeSourceComponents(), so this is not the primary key -
	 * but it is a real fallback one, and not merely a label. AHFElementActor::SyncBakedPartsToSources
	 * re-matches parts to sources by ATTACHMENT first, because a baked component hangs off the dynamic
	 * component it stands in for and that is the one record a reordering of the part list cannot
	 * falsify; this name is what settles the remaining case, where an asset has been loaded from a
	 * saved level and no component exists yet to be attached by. It is also what lets a report say
	 * which drawer went missing.
	 *
	 * A part component's object name is unique but not stable across a regeneration that destroys and
	 * re-adds the same part id, which is why it is second and not first.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HouseForge|Bake")
	FName SourceComponentName;

	/**
	 * The asset. A HARD reference on purpose, against the usual instinct to make it soft.
	 *
	 * Three jobs: the cooker and the reference viewer have to see it; it keeps the asset loaded so
	 * switching back to Baked is a pointer assignment rather than a synchronous load; and it is the
	 * thing that makes clearing Component->SetStaticMesh(nullptr) in Dynamic mode free (see
	 * AHFElementActor::ApplyRenderMode, and the tool-target measurement behind it).
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HouseForge|Bake")
	TObjectPtr<UStaticMesh> BakedMesh;

	/** The component that draws BakedMesh, parented to the dynamic component it stands in for. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HouseForge|Bake")
	TObjectPtr<UStaticMeshComponent> Component;

	/**
	 * Where the asset lives, kept separately from the pointer above.
	 *
	 * Survives a force-delete of the asset, which the pointer does not. Without it a missing bake can
	 * only be reported as "something is missing"; with it the report can name the package, which is
	 * the difference between a user finding their asset in the recycle bin and not.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HouseForge|Bake")
	FSoftObjectPath BakedAssetPath;

	/** AHFElementActor::MeshRevision at the moment this was baked. Mismatch means stale. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HouseForge|Bake")
	int32 BakedAtMeshRevision = INDEX_NONE;

	/**
	 * A fingerprint of the ASSET's own geometry as this bake left it. Zero means never taken.
	 *
	 * ## The one hole MeshRevision cannot see, and why this field exists
	 *
	 * In Baked mode the Modeling Tools are handed the BAKED ASSET, not the live mesh. That is
	 * measured, not feared: HouseForge.Bake.Probe.ToolTargetSelection row D - the shipped Baked state
	 * - reports one candidate and it is StaticMeshComponentToolTarget. It is also the correct answer
	 * for the engine to give, because ApplyRenderMode deliberately marks the dynamic component
	 * non-editable while baked so that a tool cannot edit something the artist cannot see.
	 *
	 * So an artist CAN sculpt a baked element, and the sculpt lands in the asset. Every guard in the
	 * plugin was structurally unable to notice: bArtistEdited and bUnbakeOnHandEdit are both driven by
	 * UDynamicMeshComponent::OnMeshChanged, which a static-mesh edit never raises, and MeshRevision
	 * never moves, so IsBakeStale stays false. The next parameter change then re-baked over it - the
	 * update path replaces the whole mesh description from the dynamic mesh - and the afternoon's work
	 * was gone with no log line, no flag and no dialog. Exactly the loss rule 04 calls "silent,
	 * unrecoverable".
	 *
	 * This is the fingerprint that closes it. FHFBakeService takes it from the asset immediately after
	 * writing it, and re-takes it before every subsequent overwrite. A mismatch means somebody else
	 * wrote to the asset, and the bake REFUSES rather than overwriting. See
	 * FHFBakeService::AdoptBakedAssetEdits for the way to bring such an edit back into the live mesh.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HouseForge|Bake")
	int64 BakedContentHash = 0;

	/**
	 * The asset no longer matches the fingerprint above: it has been edited outside the bake.
	 *
	 * Set by FHFBakeService when it declines to overwrite. The element stays BAKED and keeps showing
	 * that edited asset, because that edit is the artist's work and showing it is the only answer that
	 * loses nothing. Cleared by a successful bake - which means either the edit was adopted into the
	 * live mesh, or the user explicitly discarded it.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HouseForge|Bake")
	bool bBakedAssetHandEdited = false;

	/**
	 * What the SOURCE component blocked before the bake took its collision away.
	 *
	 * Recorded rather than assumed, and it is not pedantry: AHFArticulatedActor::ApplyPartCollision
	 * deliberately leaves a fan rotor on QueryOnly, blocking nothing that moves through the room,
	 * because collision cannot spin with the render. Restoring every part to QueryAndPhysics on
	 * unbake would put a frozen blade back across a third of the sweep as a wall - a defect that
	 * exists nowhere in the articulation code and would have been introduced entirely by the bake.
	 * Whatever the source declared is also what the baked component gets, for the same reason.
	 *
	 * ## THE INVARIANT: this is never written from a suppressed component
	 *
	 * NoCollision is not a value any generator declares. Only two ever reach a bake source -
	 * QueryAndPhysics from AHFElementActor's constructor and QueryOnly for a rotor from
	 * AHFArticulatedActor::ApplyPartCollision - and NoCollision is written to a source by exactly
	 * one thing: AHFElementActor::ApplyRenderMode, suppressing it while baked.
	 *
	 * So every write to this field is guarded on the source not currently reading NoCollision. Drop
	 * the guard and a RE-BAKE records the suppression as the thing to restore: the next unbake
	 * hands back "blocks nothing", and the element is left visible, live, editable and completely
	 * passable in both modes with nothing logged. A whole flat loses its collision the first time a
	 * misread is corrected after baking, and the only symptom is a walkthrough falling out of the
	 * building. Guarded by HouseForge.Bake.RebakingKeepsTheCollisionUnbakeRestores and
	 * HouseForge.Bake.RebakingKeepsEachPartsOwnCollision.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HouseForge|Bake")
	TEnumAsByte<ECollisionEnabled::Type> SourceCollisionEnabled = ECollisionEnabled::QueryAndPhysics;

	/**
	 * The source component held no triangles, so there is deliberately no asset for this part.
	 *
	 * Without it, one degenerate element - a wall whose openings have eaten all of it, which the
	 * validator warns about but does not forbid - would report a failed bake, and "Bake all" over a
	 * flat would come back red because of a wall nobody can see either way. An empty part is baked
	 * correctly by producing nothing.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HouseForge|Bake")
	bool bSourceWasEmpty = false;
};

/**
 * Provenance stamped onto every asset HouseForge bakes.
 *
 * LevelPackageName is the load-bearing field, and it is what makes orphan deletion safe: a scan can
 * only ever see the open level, so an asset belonging to a level that is not open must never be
 * offered for deletion however unreferenced it looks from here.
 *
 * NOTE, measured (HouseForge.Bake.Probe.RepeatCreateSameName): re-creating a UStaticMesh over an
 * existing one of the same name reuses the object in place and re-runs its constructor, which resets
 * the AssetUserData array. So this stamp has to be re-applied after every create, not merely on the
 * first one. FHFBakeService does that unconditionally.
 */
UCLASS()
class HOUSEFORGE_API UHFBakedMeshUserData : public UAssetUserData
{
	GENERATED_BODY()

public:
	/** AHFElementActor::BakeOwnerGuid of the element that produced this. Stable across undo. */
	UPROPERTY(VisibleAnywhere, Category = "HouseForge|Bake")
	FGuid OwnerGuid;

	UPROPERTY(VisibleAnywhere, Category = "HouseForge|Bake")
	FName ElementId;

	UPROPERTY(VisibleAnywhere, Category = "HouseForge|Bake")
	FName ElementClassName;

	/** Which source component of that element. NAME_None is the root mesh. */
	UPROPERTY(VisibleAnywhere, Category = "HouseForge|Bake")
	FName SourceComponentName;

	/** The level this belongs to. An orphan scan is scoped to this and nothing else. */
	UPROPERTY(VisibleAnywhere, Category = "HouseForge|Bake")
	FName LevelPackageName;

	UPROPERTY(VisibleAnywhere, Category = "HouseForge|Bake")
	int32 SourceMeshRevision = INDEX_NONE;

	/**
	 * The asset's own geometry fingerprint as this bake left it. See FHFBakedPart::BakedContentHash.
	 *
	 * Carried on the ASSET as well as on the element because the two answer different questions. The
	 * element's copy is what a re-bake checks; this one is what makes the check survive a level that
	 * was duplicated, a level whose actor was deleted and re-added, and an asset found at a path some
	 * other level's element also wants.
	 */
	UPROPERTY(VisibleAnywhere, Category = "HouseForge|Bake")
	int64 ContentHash = 0;

	UPROPERTY(VisibleAnywhere, Category = "HouseForge|Bake")
	FDateTime BakedAtUtc;
};

/**
 * How a runtime element actor reaches the editor-only code that creates assets.
 *
 * Creating a UPackage is unreachable from a runtime module, and making HouseForge depend on UnrealEd
 * to fix that would be far worse than one static delegate. Bound in
 * FHouseForgeEditorModule::StartupModule and inert when unbound, so a cooked build - or a test that
 * deliberately never binds it - simply cannot bake, rather than crashing.
 *
 * Docs/PanelAndBakeDesign.md 8 calls this indirection ugly and it is. The alternative is worse.
 */
/** Bake (or re-bake) every part of one element. False with a reason in OutError. */
DECLARE_DELEGATE_RetVal_TwoParams(bool, FHFBakeElementDelegate, AHFElementActor*, FString&);

struct HOUSEFORGE_API FHFBakeHooks
{
	static FHFBakeElementDelegate BakeElement;

	/** True when the editor module is loaded and has bound the hook above. */
	static bool CanBake() { return BakeElement.IsBound(); }
};
