// Copyright Siddartha G. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Model/HFTypes.h"

class UDynamicMeshComponent;
class UMaterialInterface;

/**
 * The placeholder material for each surface role, and the wiring that puts it on a component.
 *
 * This is a default look, not the material library. It exists so a generated flat can be read at
 * all: without it every surface renders as Unreal's default checkerboard and a screenshot of a
 * room is unusable for judging whether the room is right. Per-role texture maps, tiling in
 * millimetres and a panel to edit any of it are milestone 10 and are deliberately absent here -
 * anything added now would have to be designed around later.
 *
 * The composing layer, not a generator. Resolving a role to a UMaterialInterface loads an asset,
 * which a generator is not allowed to do (see .claude/rules/04-conventions.md): generators emit
 * surface-role polygroups and nothing else, FHFMeshOps::AssignMaterialIdsFromRoles turns those
 * into material ids, and this turns material ids into materials on a component.
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
