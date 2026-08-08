// Copyright Siddartha G. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Materials/HFSurfaceFinish.h"
#include "Model/HFTypes.h"
#include "HFMaterialLibrary.generated.h"

class UDynamicMeshComponent;
class UMaterialInterface;

/**
 * How hard a push to the render side is allowed to work.
 *
 * The two-tier split exists because UMaterialInstanceConstant::PostEditChange runs a
 * FMaterialUpdateContext whose defaults are RecreateRenderStates | SyncWithRenderingThread - a
 * render-state recreate plus a rendering-thread flush across every primitive using the material.
 * With 155 element actors and 217,826 triangles in the reference flat, paying that on every
 * mouse-move of a roughness slider is a hitch per frame of the drag.
 */
UENUM()
enum class EHFMaterialPush : uint8
{
	/**
	 * Mid-gesture. Pushes numeric values to the render thread and nothing else.
	 *
	 * Scalars and vectors ARE uniform expressions, so RecacheUniformExpressions(false) is the whole
	 * job: no shader map changes, no static permutation, no draw-list rebuild. Static switches and
	 * texture assignments are deliberately SKIPPED here - they cost a shader compile, and a control
	 * that silently costs four seconds mid-drag reads as a hang.
	 */
	Interactive,

	/**
	 * Gesture finished, or a load-time sync. Writes everything, including static switches and
	 * texture slots, and marks the instance's package dirty so the change is savable.
	 */
	Commit
};

/**
 * WHAT EVERY SURFACE ROLE IS MADE OF: one FHFSurfaceFinish per EHFSurfaceRole, as an asset.
 *
 * This replaces the flat placeholder set. The finishes are real specifications of real Indian
 * residential surfaces - acrylic emulsion over gypsum putty, double-charge vitrified tile laid with
 * 2 mm spacers, pre-laminated ply carcasses, speckled granite, powder-coated aluminium sections -
 * with the colour, roughness, metallic, coat and detail each of those actually has.
 *
 *
 * WHAT IS AUTHORITATIVE, AND WHAT IS DERIVED
 * ------------------------------------------
 * THIS ASSET IS THE RECORD. The MI_HF_<Role> material instances are the record COMPILED FOR THE
 * RENDERER, the way a shader map is a material graph compiled for the GPU. The direction is strictly
 * one way - library to instance, never back - so this is not the two-way sync
 * .claude/rules/04-conventions.md forbids between a spec and its actors. PushFinish overwrites the
 * instance; nothing ever reads a value out of it and calls that the library's opinion.
 *
 * The honest consequence, stated because it is a real trade: editing MI_HF_FloorFinish by hand in
 * Epic's Material Instance Editor works and renders, and the next push from here overwrites it. The
 * place to change a finish is this asset.
 *
 *
 * WHY THE INSTANCES ARE STILL THE THING COMPONENTS REFERENCE
 * ----------------------------------------------------------
 * A UMaterialInstanceConstant is simultaneously the thing every component's slot already points at,
 * a durable asset reference that survives a save and a reload, and - through
 * RecacheUniformExpressions - a live update path. A UMaterialInstanceDynamic is none of those: it is
 * transient, so a component slot holding one deserialises as null and 155 components would have to
 * be re-materialled on every map open; and milestone 12 bakes these elements to static meshes, whose
 * slots need a durable UMaterialInterface* rather than an anonymous per-session object.
 *
 *
 * NOTHING HERE CAN REACH A VERTEX
 * -------------------------------
 * Re-materialling is entirely component-side and asset-side. ApplyTo sets slots on a
 * UDynamicMeshComponent; PushFinish sets parameters on a material instance. Neither touches
 * FDynamicMesh3, neither calls Regenerate, and neither reads or writes bArtistEdited - so changing a
 * colour cannot destroy a hand edit, and cannot renumber the surface-role polygroups the whole
 * assignment mechanism is built on. Asserted, not assumed:
 * HouseForge.Materials.ReMaterialisingDoesNotTouchGeometry,
 * HouseForge.Materials.AssignmentLeavesSurfaceRolesIntact and
 * HouseForge.Materials.ChangingAFinishLeavesArtistEditsAlone.
 *
 *
 * THE MAPPING FROM POLYGROUP TO SLOT IS NOT REBUILT HERE
 * ------------------------------------------------------
 * It already works and is proven. A generator tags every triangle with a surface-role polygroup,
 * FHFMeshOps::AssignMaterialIdsFromRoles turns those into material ids, and the slot index IS the
 * role index on every component uniformly. This class only decides what material sits in slot N.
 */
UCLASS(BlueprintType)
class HOUSEFORGE_API UHFMaterialLibrary : public UDataAsset
{
	GENERATED_BODY()

public:
	UHFMaterialLibrary();

	/**
	 * One finish per surface role.
	 *
	 * A map rather than an array so that adding a role to EHFSurfaceRole leaves a saved asset
	 * loadable and merely incomplete, instead of silently shifting every finish up by one. A role
	 * with no entry falls back to the shipped default rather than to nothing - see FinishForRole.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HouseForge",
		meta = (TitleProperty = "Description"))
	TMap<EHFSurfaceRole, FHFSurfaceFinish> Finishes;

	// =============================================================================== resolving

	/** Package path the role instances live in. */
	static const TCHAR* MaterialFolder() { return TEXT("/HouseForge/Materials"); }

	/** Object path of the material instance for a role, derived from the enumerator's own name. */
	static FString AssetPathForRole(EHFSurfaceRole Role);

	/**
	 * The library this project builds with.
	 *
	 * The asset named by UHFSettings::MaterialLibrary when there is one, and the class default
	 * object otherwise. THE FALLBACK IS THE POINT: the CDO carries the full default table, compiled
	 * in, so a freshly generated flat looks like a flat before anyone has opened a panel or created
	 * an asset. Never null.
	 */
	static UHFMaterialLibrary* Get();

	/**
	 * The finish for a role: this library's entry, or the shipped default if it has none.
	 *
	 * Falling back rather than returning a blank struct, because a blank one is mid-grey plastic at
	 * a one-metre tile and would look like a decision somebody made.
	 */
	const FHFSurfaceFinish& FinishForRole(EHFSurfaceRole Role) const;

	/** The compiled-in default finish for a role. What a fresh library, and the CDO, are made of. */
	static const FHFSurfaceFinish& DefaultFinishForRole(EHFSurfaceRole Role);

	/**
	 * The material a role renders through, loading it on first use and caching it thereafter.
	 *
	 * Null when the asset is missing rather than substituting something: a role rendering as the
	 * default checkerboard is a legible failure, and quietly handing back a neighbouring role's
	 * material would make a missing asset look like a working one.
	 */
	UMaterialInterface* ResolveMaterial(EHFSurfaceRole Role) const;

	/** Every material, indexed by material slot - that is, by role index. */
	TArray<UMaterialInterface*> ResolveMaterialSet() const;

	/**
	 * Fills a component's material slots so each surface role renders through its own.
	 *
	 * Applied to the component rather than to the mesh, and applied whether or not the mesh was
	 * written this pass: a hand-edited shutter still has to have a material, and the slot table is a
	 * property of the component.
	 */
	void ApplyTo(UDynamicMeshComponent* Component) const;

	// ============================================================================= live update

#if WITH_EDITOR
	/**
	 * Writes one role's finish onto its material instance. Every actor using that role re-renders.
	 *
	 * No actor is visited and no mesh is touched: the instance is a shared asset, so writing it once
	 * updates all 155 elements of the reference flat at once. That is the whole reason the assignment
	 * side was left alone.
	 *
	 * @return true when there was an instance to write to.
	 */
	bool PushFinish(EHFSurfaceRole Role, EHFMaterialPush Mode) const;

	/** Every role, in enum order. @return how many instances were written. */
	int32 PushAllFinishes(EHFMaterialPush Mode) const;

	/** Editing a finish in the details panel pushes it. Interactive while dragging, full on release. */
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	/** Drops the resolved-material cache. For tests that reload or re-author the assets underneath it. */
	static void InvalidateCache();

private:
	/** Fills Finishes with the compiled-in table. Called by the constructor, so the CDO carries it. */
	void ResetToDefaults();
};
