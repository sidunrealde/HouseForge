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
 * ## Two limits of that model, both deliberate
 *
 * **Parts hang off the shell, never off each other.** Every part component is parented to the fixed
 * mesh, so a part cannot ride on another part's moving frame. The case that would want it is a
 * pull-out unit carrying its own baskets or drawers, and there is nothing in the joinery kit that
 * produces one yet. It is a small change to make when there is - an optional parent id on
 * FHFMeshPart, resolved to the parent's component here - and adding it before there is a fixture to
 * test it against would be an untested mechanism, not a feature.
 *
 * **Parts declare their own orderings, and MasterOpenAmount honours them.** A drawer inside a
 * wardrobe cannot come out until its shutter is open. Parenting the drawer to the leaf would NOT
 * fix that, and it is worth being precise about why: an internal drawer's runners are screwed to
 * the CARCASS, so a drawer riding on the leaf would swing out of the cabinet with it - a worse lie
 * than the one being corrected. The constraint is an ORDERING between two independent parts, and it
 * is expressed as one, on FHFPartMotion::SequencedAfterPartId. Every route into a pose resolves it,
 * so opening everything on a wardrobe opens the leaves and then draws the drawers out behind them,
 * which is a pose the wardrobe could really be in.
 * HouseForge.Joinery.InternalDrawerInterlock builds that composition and measures it.
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

	/**
	 * Sets a spinning part's phase, in revolutions. Unbounded: 2.5 is two and a half turns.
	 *
	 * @return false if no part carries that id, and false if it carries one that does not spin - a
	 *         phase means nothing to a door, and silently accepting it would hide the mix-up.
	 */
	UFUNCTION(BlueprintCallable, Category = "HouseForge|Articulation")
	bool SetPartSpinTurns(FName PartId, double Turns);

	/** Revolutions a part has turned, or 0 if there is no such part. */
	UFUNCTION(BlueprintPure, Category = "HouseForge|Articulation")
	double GetPartSpinTurns(FName PartId) const;

	/**
	 * Turns every spinning part on by however far its own rate carries it in that long.
	 *
	 * The entry point for anything that wants a fan actually running - a walkthrough pawn, a
	 * Sequencer event, an editor preview - without these actors having to tick. Phases accumulate,
	 * so calling it repeatedly keeps a fan turning past any number of revolutions.
	 */
	UFUNCTION(BlueprintCallable, Category = "HouseForge|Articulation")
	void AdvanceSpinningParts(double DeltaSeconds);

	/**
	 * Asks every part for the same open amount, then lets the orderings between them settle it.
	 *
	 * Spinning parts are left alone: a fan has no open amount, and "open everything" must not stop
	 * one at some arbitrary angle.
	 */
	UFUNCTION(BlueprintCallable, Category = "HouseForge|Articulation")
	void SetAllPartsOpenAmount(double OpenAmount);

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "HouseForge|Articulation")
	void OpenAllParts();

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "HouseForge|Articulation")
	void CloseAllParts();

	/**
	 * Every open amount worth carrying, keyed by part id.
	 *
	 * Closed parts are left out, so an untouched element captures nothing and a rebuild has nothing
	 * to put back for it.
	 */
	FHFPartPoses CapturePartPoses() const;

	/**
	 * Restores open amounts captured before a rebuild, matched by part id.
	 *
	 * Ids the current build no longer produces are ignored rather than resurrected: the pose of a
	 * part that no longer exists is not state, it is a stale number.
	 */
	void RestorePartPoses(const FHFPartPoses& Poses);

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

	/**
	 * Settles gearing and sequencing across the parts, then pushes the result into the components.
	 *
	 * Every route into a pose ends here, which is what makes the orderings impossible to bypass. It
	 * also carries the spinning parts' phases, since they are posed by the same components.
	 */
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
