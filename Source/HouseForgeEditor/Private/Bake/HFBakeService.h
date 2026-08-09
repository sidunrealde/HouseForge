// Copyright Siddartha G. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AssetRegistry/AssetData.h"

class AHFElementActor;
class AHFHouseActor;
class UStaticMesh;
class UWorld;

/**
 * What a bake did, in enough detail to say what went wrong and to which element.
 *
 * A bulk bake over a flat touches 150 elements; "12 failed" is not a report, it is a shrug. Every
 * failure and every skip carries the element that produced it.
 */
struct FHFBakeReport
{
	int32 ElementsBaked = 0;
	int32 ElementsUnbaked = 0;
	int32 ElementsFailed = 0;
	int32 ElementsSkipped = 0;
	int32 PartsBaked = 0;

	/** One line per failure or skip, naming the element. */
	TArray<FString> Messages;

	/** Assets written or rewritten by this operation, in the order they were produced. */
	TArray<TWeakObjectPtr<UStaticMesh>> Written;

	/** How many packages actually reached disk. Less than Written.Num() means something failed. */
	int32 PackagesSaved = 0;

	/**
	 * Assets whose part stopped existing during this operation.
	 *
	 * Reported, never deleted here. Deleting a user's assets from inside a bake is exactly the kind
	 * of silent loss this milestone exists to avoid; DeleteOrphans is a separate, confirmed action.
	 */
	TArray<FSoftObjectPath> Orphaned;

	FString Summary() const;
};

/**
 * Forces the save-to-disk decision for the duration of a scope, either way.
 *
 * ## The default is "save, unless this is an automation run"
 *
 * Baking has to write its packages in the real editor - see FHFBakeService::SaveBakedAssets for
 * why leaving it to the next Ctrl+S is not good enough. But the suite bakes real elements to real
 * assets dozens of times per gate run, and rule 01 is explicit that generated output belongs to
 * the user: a gate that left a drift of SM_Wall_W_Survive behind in the project's Content folder
 * on every pass would be writing user output as a side effect of testing.
 *
 * So the default is taken from GIsAutomationTesting, and this scope is how either side opts out of
 * it - HouseForge.Bake.BakedAssetsAreWrittenToDisk turns saving back ON deliberately so the
 * durability promise is actually exercised, and cleans up after itself.
 *
 * A scope guard rather than a bare flag because a test that failed early and left the flag set
 * would silently change what every test after it was measuring.
 */
struct FHFBakeSaveScope
{
	explicit FHFBakeSaveScope(bool bInAllowSave);
	~FHFBakeSaveScope();

	FHFBakeSaveScope(const FHFBakeSaveScope&) = delete;
	FHFBakeSaveScope& operator=(const FHFBakeSaveScope&) = delete;

private:
	bool bPreviousHasOverride = false;
	bool bPreviousAllow = false;
};

/**
 * The only code in HouseForge that creates assets.
 *
 * Editor-only because package creation is unreachable from a runtime module, and reached from the
 * runtime actors through FHFBakeHooks - one static delegate, bound in StartupModule. That
 * indirection is ugly and the alternative, making the runtime module depend on UnrealEd, is worse.
 *
 * ## What the bake actually produces, and why every line of it is load-bearing
 *
 * A UStaticMesh with:
 *
 * - **One material slot per surface role**, at the role's own index. This is what keeps a baked
 *   element reachable from the material panel. Measured in
 *   HouseForge.Bake.Probe.SurfaceRolesSurviveTheBake: FHFMeshOps::AssignMaterialIdsFromRoles writes
 *   MaterialIdForRole onto the dynamic mesh, FDynamicMeshToMeshDescription turns material ids into
 *   polygon groups one for one, and the sections come out dense from 0, so a non-empty section's
 *   INDEX IS THE ROLE INDEX. The PolyTriGroups attribute survives too, so both routes work; sections
 *   are used because they are what the renderer itself uses.
 *
 * - **Complex-as-simple collision**, so collision matches the visual mesh - rule 04, and the reason
 *   a walkthrough does not pass through an open door.
 *
 * - **No generated lightmap UVs.** Settled by measurement: bGenerateLightmapUVs unwraps UV0 into
 *   UV1 by default, and UV0 is the world-scale tiling channel, which by construction overlaps
 *   between rooms. It would write that over the packed, gutter-sized lightmap channel milestone 10
 *   already generates. The channel reaches the asset intact without it, so the flag stays off and
 *   LightMapCoordinateIndex is set to 1 explicitly.
 *
 * - **Distance fields left enabled.** FStaticMeshAssetOptions::bAllowDistanceField defaults true and
 *   MUST stay true: UE::AssetUtils sets DistanceFieldResolutionScale to 0 when it is false, which
 *   kills the distance field, and the mesh card build is chained off the distance field build
 *   (DistanceFieldAtlas.cpp:296/1050). No distance field means no cards means no surface cache means
 *   no radiance - on BOTH tracing paths, hardware included. That is the whole reason this milestone
 *   stopped being optional.
 *
 * ## Re-baking updates rather than re-creates
 *
 * Measured (HouseForge.Bake.Probe.RepeatCreateSameName): NewObject over an existing UStaticMesh of
 * the same name reuses the object in place - no duplicate, no rename - but re-runs its constructor,
 * which RESETS THE AssetUserData ARRAY and regenerates the LightingGuid. Since the provenance stamp
 * and the whole orphan story live in UHFBakedMeshUserData, a re-create silently erases them. So a
 * repeat bake writes a new FMeshDescription into the existing asset and commits it, and the stamp is
 * re-applied unconditionally either way.
 */
class FHFBakeService
{
public:
	/** Binds FHFBakeHooks. Called once from FHouseForgeEditorModule::StartupModule. */
	static void Register();
	static void Unregister();

	/**
	 * Bakes every part of one element and switches it to Baked.
	 *
	 * NEVER REGENERATES FIRST. A hand-edited wall bakes its sculpted form and unbakes back to that
	 * same sculpted form - bake bakes what is on screen, which is the only rule that makes bake safe
	 * on an element somebody has spent an afternoon on.
	 */
	static bool BakeElement(AHFElementActor* Element, FHFBakeReport& Report);

	/** Switches one element back to its live mesh. Keeps every asset. Cannot fail. */
	static void UnbakeElement(AHFElementActor* Element, FHFBakeReport& Report);

	/** Bake or unbake a set - the selection case. */
	static void SetRenderModeMany(TArrayView<AHFElementActor* const> Elements, bool bBaked, FHFBakeReport& Report);

	/** Bake or unbake every element of every house in the level - the whole-house case. */
	static void SetHouseRenderMode(UWorld* World, bool bBaked, FHFBakeReport& Report);

	/** Re-bakes every baked element whose geometry has moved on since. */
	static void RebakeStale(UWorld* World, FHFBakeReport& Report);

	/** Elements of the level, in a stable order. Baked or not. */
	static void GatherElements(UWorld* World, TArray<AHFElementActor*>& OutElements);

	/**
	 * Where a house's assets go, resolving and writing back the folder on first use.
	 *
	 * @return a long package path such as /Game/HouseForge/Baked/L_Sample2BHK, or empty on failure.
	 */
	static FString ResolveBakedAssetFolder(AHFHouseActor* House, UWorld* World);

	/**
	 * Baked assets in this level's folder that no element claims any more.
	 *
	 * Scoped to UHFBakedMeshUserData::LevelPackageName. A scan can only see the open level, so an
	 * asset stamped for a different level is never a candidate however unreferenced it looks - it
	 * belongs to a level that is not open and whose elements cannot be asked.
	 */
	static void FindOrphans(UWorld* World, TArray<FAssetData>& OutOrphans);

	/**
	 * Writes the packages a bake produced, so the bake survives closing the editor.
	 *
	 * ## Why this is not left to the user's next Ctrl+S
	 *
	 * A baked element holds a HARD reference to its UStaticMesh. Saving the LEVEL does not save the
	 * asset packages the level references - UEditorLoadingAndSavingUtils::SaveMap saves the map -
	 * and in the interactive editor the gap is covered by the Save Content dialog listing the
	 * dependencies. THERE IS NO DIALOG HERE. HouseForge is driven over MCP by a model, so the bake
	 * and the level save both happen with nobody at a prompt.
	 *
	 * Unsaved, the next editor start finds no asset, ReconcileBakeState falls the element back to
	 * Dynamic, and the flat quietly leaves the Lumen scene. That degradation is graceful in the
	 * sense that nothing renders as a hole - and deceptive in exactly the way the Lumen measurement
	 * showed is worst, because the broken configuration renders BRIGHTER than the correct one. A
	 * render taken the morning after a bake would look bright, cheerful and wrong.
	 *
	 * bOnlyDirty is false: an asset that was updated in place and has already been saved once is
	 * still the asset this bake is promising is on disk.
	 *
	 * @param FromIndex First entry of Report.Written to consider. NOT decoration: one report is
	 *                  shared across a whole bulk bake, so a call that always started at 0 would
	 *                  re-save every package produced so far on every element - about 11,000 package
	 *                  writes over a 150-element flat instead of 150.
	 * @return how many packages were written.
	 */
	static int32 SaveBakedAssets(FHFBakeReport& Report, int32 FromIndex, FString& OutError);

	/** Deletes the assets a FindOrphans result named. Not undoable; the caller confirms. */
	static int32 DeleteOrphans(const TArray<FAssetData>& Orphans, FString& OutError);

	/** A human-readable bake tally for the level: baked, total, stale. */
	static void CountBakeState(UWorld* World, int32& OutBaked, int32& OutTotal, int32& OutStale);

private:
	/** Bakes one source component into one asset, creating or updating it. */
	static UStaticMesh* BakeOnePart(AHFElementActor* Element, int32 PartIndex, const FString& Folder,
		FString& OutError, bool& bOutSourceWasEmpty);

	/** SM_<Kind>_<ElementId>[_p<N>] - stable across re-bakes, which is what makes them updates. */
	static FString AssetNameFor(const AHFElementActor* Element, int32 PartIndex);
};
