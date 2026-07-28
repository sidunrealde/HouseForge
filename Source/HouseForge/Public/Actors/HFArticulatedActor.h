// Copyright Siddartha G. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Actors/HFElementActors.h"
#include "Model/HFArticulation.h"
#include "HFArticulatedActor.generated.h"

class UDynamicMeshComponent;

/**
 * An element with moving parts.
 *
 * Anything that moves in the real thing moves here: doors swing, sashes slide, drawers pull out,
 * wardrobe shutters open. See .claude/rules/04-conventions.md - this is a requirement, not a
 * nicety, and it is the reason a fixture is never merged into one mesh.
 *
 * The split is: everything fixed shares the inherited root mesh component, and every moving part
 * gets its own UDynamicMeshComponent parented to it. A chest of drawers is therefore one carcass
 * mesh plus one component per drawer front - not five carcass meshes, and never one welded block.
 *
 * A subclass provides:
 *   BuildMesh()   the fixed geometry, in actor space, exactly as any other element.
 *   BuildParts()  the moving parts, each mesh in its own local space with the pivot at the origin.
 *
 * Everything else - components, pivots, open amounts, edit protection, persistence - is handled
 * here, so a joinery generator never has to touch an actor.
 */
UCLASS(Abstract, HideCategories = (Replication, Networking, Input, HLOD))
class HOUSEFORGE_API AHFArticulatedActor : public AHFElementActor
{
	GENERATED_BODY()

public:
	AHFArticulatedActor();

	/**
	 * Live state of every moving part, in build order.
	 *
	 * Fixed size on purpose: parts come from generation, so adding a row by hand would create a
	 * part with no geometry that the next rebuild would silently drop. OpenAmount is the one field
	 * meant to be edited here, and it is what an artist reaches for to pose a single shutter.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, EditFixedSize, Category = "HouseForge|Articulation")
	TArray<FHFPartState> Parts;

	/**
	 * Drives every part at once - the "open everything" used to check that a fixture actually
	 * articulates rather than merely claiming to.
	 *
	 * Marked Interp and given a setter so it is animatable from a Level Sequence: an artist scrubs
	 * one track and the whole run of wardrobes opens. Sequencer drives the property through
	 * SetMasterOpenAmount, which is what makes the parts actually move rather than just the number
	 * change. Per-part posing is still done on Parts.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, BlueprintSetter = SetMasterOpenAmount,
		Category = "HouseForge|Articulation",
		meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	double MasterOpenAmount = 0.0;

	/** Sets MasterOpenAmount and moves every part to match. The animatable entry point. */
	UFUNCTION(BlueprintSetter, Category = "HouseForge|Articulation")
	void SetMasterOpenAmount(double NewMasterOpenAmount);

	/** Sets one part's open amount and moves it. False if no part carries that id. */
	UFUNCTION(BlueprintCallable, Category = "HouseForge|Articulation")
	bool SetPartOpenAmount(FName PartId, double OpenAmount);

	/** Current open amount of a part, or 0 if there is no such part. */
	UFUNCTION(BlueprintPure, Category = "HouseForge|Articulation")
	double GetPartOpenAmount(FName PartId) const;

	/** Sets every part to the same open amount. */
	UFUNCTION(BlueprintCallable, Category = "HouseForge|Articulation")
	void SetAllPartsOpenAmount(double OpenAmount);

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "HouseForge|Articulation")
	void OpenAllParts();

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "HouseForge|Articulation")
	void CloseAllParts();

	/**
	 * The component carrying a part's mesh, or null.
	 *
	 * Its transform is the part's live articulated pose, which is what makes baking simple: a
	 * baked static mesh parented to this component inherits the articulation for free.
	 */
	UFUNCTION(BlueprintPure, Category = "HouseForge|Articulation")
	UDynamicMeshComponent* GetPartComponent(FName PartId) const;

	/** Components in the same order as Parts. */
	const TArray<TObjectPtr<UDynamicMeshComponent>>& GetPartComponents() const { return PartComponents; }

	int32 NumParts() const { return Parts.Num(); }

	/** State of a part by id, or null. */
	const FHFPartState* FindPart(FName PartId) const;

	/** True if that part's mesh has been modified outside of generation. */
	bool IsPartArtistEdited(FName PartId) const;

	/** True if the fixed mesh or any part has been hand-edited. */
	bool HasAnyArtistEdits() const;

	virtual void Regenerate() override;
	virtual void RevertToGenerated() override;
	virtual bool ShouldPreserveOnRebuild() const override;

	virtual void PostInitializeComponents() override;
	virtual void PostRegisterAllComponents() override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

protected:
	/**
	 * Subclass hook: the moving parts of this element.
	 *
	 * Each part's mesh is in that part's own local space with the origin on the pivot - the hinge
	 * line for a hinge, any point on the line of travel for a slide - which is what keeps the
	 * generator producing it pure. Ids must be stable across calls, because open amounts and
	 * hand-edit flags are matched back by id.
	 */
	virtual void BuildParts(TArray<FHFMeshPart>& OutParts) const {}

	/**
	 * Rebuilds part components from BuildParts.
	 *
	 * Open amounts and hand-edit flags carry over by part id, and a hand-edited part keeps its
	 * mesh. Pivot and motion are always refreshed, including on a hand-edited part: that moves the
	 * modelled geometry rigidly with the carcass it hangs on rather than discarding any of it.
	 *
	 * @param bForce discards hand-edit protection, as RevertToGenerated does for the fixed mesh.
	 */
	void RegenerateParts(bool bForce);

	/** Pushes the current open amounts into the part components' relative transforms. */
	void ApplyOpenAmounts();

private:
	/** Index-parallel to Parts. */
	UPROPERTY()
	TArray<TObjectPtr<UDynamicMeshComponent>> PartComponents;

	UDynamicMeshComponent* CreatePartComponent(FName PartId);

	/** Binds edit detection on every part component, including ones loaded from a saved level. */
	void WatchParts();

	void HandlePartMeshChanged(FName PartId);
};
