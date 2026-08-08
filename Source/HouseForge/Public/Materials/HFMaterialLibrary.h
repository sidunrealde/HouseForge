// Copyright Siddartha G. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Model/HFTypes.h"

class UDynamicMeshComponent;
class UMaterialInterface;

/**
 * The material for each surface role, and the wiring that puts it on a component.
 *
 * WHAT THESE MATERIALS ARE HAS CHANGED; HOW THEY ARE ASSIGNED HAS NOT. Each role resolves to a
 * UMaterialInstanceConstant under MaterialFolder(), authored by Scripts/gen_materials.py, and the
 * slot index is the role index on every component uniformly. Those instances used to be flat
 * colours that sampled nothing; they now sample texture maps when a user assigns any, carry
 * procedural detail when a user has none, and express tiling in millimetres against the world-scale
 * UV0 that FHFMeshOps::ApplyWorldScaleUVs unwraps. None of that is visible from here, and that is
 * the point: this file resolves roles to materials, and a material's own graph is the material's
 * business.
 *
 * THE COMPOSING LAYER, NOT A GENERATOR. Resolving a role to a UMaterialInterface loads an asset,
 * which a generator is not allowed to do (see .claude/rules/04-conventions.md): generators emit
 * surface-role polygroups and nothing else, FHFMeshOps::AssignMaterialIdsFromRoles turns those into
 * material ids, and this turns material ids into materials on a component.
 *
 * NOTHING HERE TOUCHES A MESH. Re-materialling is entirely component-side - ConfigureMaterialSet
 * sets slots on a UDynamicMeshComponent and never writes a vertex - which is what makes changing a
 * finish incapable of regenerating geometry or discarding a hand edit. See
 * HouseForge.Materials.ReMaterialisingDoesNotTouchGeometry.
 */
class HOUSEFORGE_API FHFMaterialLibrary
{
public:
	/** Package path the placeholder instances live in. */
	static const TCHAR* MaterialFolder() { return TEXT("/HouseForge/Materials"); }

	/** Object path of the placeholder instance for a role, derived from the enumerator's own name. */
	static FString AssetPathForRole(EHFSurfaceRole Role);

	/**
	 * The placeholder material for a role, loading it on first use and caching it thereafter.
	 *
	 * Null when the asset is missing rather than substituting something: a role rendering as the
	 * default checkerboard is a legible failure, and quietly handing back a neighbouring role's
	 * material would make a missing asset look like a working one.
	 */
	static UMaterialInterface* GetPlaceholder(EHFSurfaceRole Role);

	/** Every placeholder, indexed by material slot - that is, by role index. */
	static TArray<UMaterialInterface*> GetPlaceholderSet();

	/**
	 * Fills a component's material slots so each surface role renders through its own.
	 *
	 * Applied to the component rather than the mesh, and applied whether or not the mesh was
	 * written this pass: a hand-edited shutter still has to have a material, and the slot table is
	 * a property of the component. Slot index is the role index, uniformly on every component -
	 * see FHFMeshOps::AssignMaterialIdsFromRoles for why that is worth more than a compact table.
	 */
	static void ApplyPlaceholders(UDynamicMeshComponent* Component);

	/** Drops the cache. For tests that reload or re-author the assets underneath it. */
	static void InvalidateCache();
};
