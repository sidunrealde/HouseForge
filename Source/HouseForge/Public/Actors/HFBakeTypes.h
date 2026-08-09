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
	 * Diagnostic rather than a key: BakedParts is index-parallel to GetBakeSourceComponents(), and
	 * a part component's object name is only unique, not stable, across a regeneration that drops and
	 * re-adds a part. The name is what lets a report say which drawer went missing.
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
	 * What the SOURCE component blocked before the bake took its collision away.
	 *
	 * Recorded rather than assumed, and it is not pedantry: AHFArticulatedActor::ApplyPartCollision
	 * deliberately leaves a fan rotor on QueryOnly, blocking nothing that moves through the room,
	 * because collision cannot spin with the render. Restoring every part to QueryAndPhysics on
	 * unbake would put a frozen blade back across a third of the sweep as a wall - a defect that
	 * exists nowhere in the articulation code and would have been introduced entirely by the bake.
	 * Whatever the source declared is also what the baked component gets, for the same reason.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HouseForge|Bake")
	TEnumAsByte<ECollisionEnabled::Type> SourceCollisionEnabled = ECollisionEnabled::QueryAndPhysics;
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
