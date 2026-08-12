// Copyright Siddartha G. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Model/HFTypes.h"
#include "UObject/SoftObjectPtr.h"
#include "HFAssetOverrideTypes.generated.h"

class UStaticMesh;
class UStaticMeshComponent;

/**
 * How a Content Browser asset is sized into the box the generated fixture occupies.
 *
 * AN ASSET WILL NEVER MATCH THE DRAWING EXACTLY, and that is the whole reason this enum exists
 * rather than one bool. A vendor's 2100 wardrobe standing where the drawing says 2400 is a real
 * situation with four defensible answers, and which one is right is the user's judgement about that
 * object and not something this code can infer: a sofa may be left at its authored size and simply
 * be the size it is, while a run of kitchen units has to fill its opening or there is a gap in the
 * kitchen.
 *
 * The cost of the stretching modes is real and is stated at the point of use rather than hidden -
 * see FHFAssetFitResult::WorstAxisRatio. A wardrobe stretched 34% in depth stops reading as the
 * object the vendor modelled: an authored 3 mm shadow gap becomes a 4 mm one, a chamfer stops
 * catching light at the angle it was built for. The panel shows the per-axis figure before anything
 * is committed.
 */
UENUM(BlueprintType)
enum class EHFAssetFitMode : uint8
{
	/**
	 * The asset is placed at the size its author made it. No scaling at all.
	 *
	 * The honest default for anything bought as a real product - a WC, a basin, a washing machine -
	 * where the drawn box was an approximation of a catalogue item in the first place and the
	 * catalogue item is the better number.
	 */
	KeepAssetSize,

	/**
	 * One scale factor on all three axes, the largest that still fits inside the generated box.
	 *
	 * Keeps the object's proportions exactly - nothing an author modelled is distorted - and pays for
	 * it with slack on two axes. The right answer for loose furniture, and the default a mapping
	 * table starts with.
	 */
	UniformFit,

	/**
	 * Per-axis scale so the asset fills the generated box exactly.
	 *
	 * Distorts. Correct for built-in joinery, which is manufactured to the opening it goes in: a
	 * wardrobe run IS the width of its recess, and one that does not fill it leaves a gap no real
	 * carpenter would hand over.
	 */
	StretchToFootprint,

	/**
	 * Plan dimensions stretched to the drawn footprint; the authored height kept.
	 *
	 * The mixed case, and it is the commonest one for kitchen units: the run has to fill its wall,
	 * but the height of a base unit is an ergonomic figure the manufacturer already got right and
	 * stretching it moves the worktop away from where a person's hands go.
	 */
	FitPlanKeepHeight
};

/**
 * ONE CONTENT BROWSER ASSET STANDING IN FOR ONE GENERATED ELEMENT.
 *
 * NON-DESTRUCTIVE, AND THAT IS THE ENTIRE POINT. .claude/rules/04-conventions.md: "Replacing a
 * procedural fixture with a Content Browser asset never discards its parameter struct. Clearing the
 * override must restore the generated mesh exactly." Nothing in the override path reads, writes,
 * clears or rebuilds the FDynamicMesh3, and nothing in it calls Regenerate, CommitMesh or
 * RevertToGenerated. Setting an override hides components and shows one more; clearing it does the
 * reverse. The generated mesh is restored exactly because it never went anywhere.
 *
 * ONE PER ELEMENT, NOT ONE PER PART - which is the opposite of the bake, deliberately.
 *
 * A bake stands in for what HouseForge generated, so it has to keep the articulation HouseForge
 * generated: rule 04's "a bake must not weld a chest of drawers into a block". An override stands in
 * for the whole object with something a person chose instead, and a vendor's wardrobe is one mesh
 * with no drawers to keep separate. Splitting it across parts would mean guessing which of the
 * asset's geometry is a shutter, which is not a question a soft object pointer can answer.
 *
 * The consequence is stated rather than hidden: WHILE AN OVERRIDE IS ACTIVE THE FIXTURE DOES NOT
 * ARTICULATE. Its drawers do not pull out, because the asset the user picked has no drawers that
 * this code can find. Clearing the override brings the articulated generated fixture back, poses and
 * all, because the parts were only ever hidden.
 */
USTRUCT(BlueprintType)
struct HOUSEFORGE_API FHFAssetOverride
{
	GENERATED_BODY()

	/**
	 * The asset that draws instead of the generated mesh. Null means no override.
	 *
	 * SOFT, following UHFSettings::MaterialLibrary and for the reason its comment gives: a hard
	 * reference on the actor would drag the mesh and every texture it uses into memory whenever the
	 * actor is loaded, whether or not the override is showing. Resolved with LoadSynchronous() at
	 * apply time, after which the UStaticMeshComponent holds the hard reference the cooker and the
	 * reference viewer need - so the actor never has to.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Asset",
		meta = (AllowedClasses = "/Script/Engine.StaticMesh"))
	TSoftObjectPtr<UStaticMesh> OverrideMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Asset")
	EHFAssetFitMode FitMode = EHFAssetFitMode::UniformFit;

	/**
	 * Correction for an asset authored facing a different way from HouseForge's convention.
	 *
	 * HouseForge's own convention is stated by FHFFixturePlacement::FacingYaw - local +Y runs BACK
	 * into the unit - so the values that come up in practice are 0, 90, 180 and 270 about Z. It
	 * belongs on the mapping table entry rather than only on the instance, so a vendor pack whose
	 * whole catalogue faces -X is corrected once for the type instead of once for every wardrobe in
	 * the flat.
	 *
	 * Applied BEFORE the fit scale. Rotating after a non-uniform scale shears the mesh.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Asset")
	FRotator AssetRotationOffset = FRotator::ZeroRotator;

	/**
	 * A nudge, in centimetres, after the fit has placed the asset.
	 *
	 * The escape hatch, and it exists because the fit's alignment rule is deliberately simple:
	 * centred on the generated box in plan, standing on its base. That is right for almost
	 * everything and wrong for a wall-hung object smaller than the box it replaces, which ends up
	 * floating off the wall by half the slack. Rather than infer an anchor from a fixture type - a
	 * guess that would be wrong silently - the mismatch is shown in the preview and this is how the
	 * user closes it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Asset")
	FVector AdditionalOffset = FVector::ZeroVector;

	/**
	 * Which mapping table put this here, or NAME_None for a hand-picked one.
	 *
	 * LOAD-BEARING ON A REBUILD, not a label. A table-driven override is re-derived from the table
	 * every time the house is rebuilt, so it does not need to survive one; a hand-picked override is
	 * a decision the user made about one instance and there is nothing to re-derive it from. This
	 * field is what tells the two apart, and it is what stops a batch pass from quietly overwriting
	 * work somebody did by hand.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HouseForge|Asset")
	FName SourceTable;

	bool IsSet() const { return !OverrideMesh.IsNull(); }
};

/**
 * What a fit worked out, before or after it was committed.
 *
 * Returned by the pure solver and shown by the panel, which is the point: the user sees the numbers
 * on a preview and the numbers they see are the ones that get applied, because there is only one
 * function that produces them.
 */
USTRUCT(BlueprintType)
struct HOUSEFORGE_API FHFAssetFitResult
{
	GENERATED_BODY()

	/** The transform to put on the override component, relative to the element actor. */
	UPROPERTY(BlueprintReadOnly, Category = "HouseForge|Asset")
	FTransform RelativeTransform = FTransform::Identity;

	/** The box the generated geometry occupies, in the element actor's own local space. */
	UPROPERTY(BlueprintReadOnly, Category = "HouseForge|Asset")
	FVector GeneratedSize = FVector::ZeroVector;

	/** The asset's own size after the rotation offset, before any fit scale. */
	UPROPERTY(BlueprintReadOnly, Category = "HouseForge|Asset")
	FVector AssetSize = FVector::ZeroVector;

	/** The size the asset ends up at. */
	UPROPERTY(BlueprintReadOnly, Category = "HouseForge|Asset")
	FVector FittedSize = FVector::ZeroVector;

	/**
	 * How far the largest scaled axis is from 1.0, as a ratio never less than 1.
	 *
	 * One number for "how much is this being distorted", so a panel row can say `stretched 1.34x`
	 * without the user reading three figures and doing the comparison themselves. 1.0 is an
	 * undistorted placement.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "HouseForge|Asset")
	double WorstAxisRatio = 1.0;

	/** Signed slack per axis: generated size minus fitted size. Negative means the asset overhangs. */
	UPROPERTY(BlueprintReadOnly, Category = "HouseForge|Asset")
	FVector Slack = FVector::ZeroVector;

	/** False when there was nothing to fit - no asset, or a degenerate box on either side. */
	UPROPERTY(BlueprintReadOnly, Category = "HouseForge|Asset")
	bool bValid = false;

	/** Why, when bValid is false, or what the user should know when it is true. */
	UPROPERTY(BlueprintReadOnly, Category = "HouseForge|Asset")
	FString Note;
};

/**
 * THE FIT, AS A PURE FUNCTION. No world, no actor, no asset loading.
 *
 * .claude/rules/04-conventions.md holds generators to `(params) -> FDynamicMesh3` with no world
 * access, for a reason that applies here word for word: it is what makes this testable without an
 * editor, a level, or a Content Browser asset to point at. Every number the panel shows and every
 * transform the component gets comes through Solve, so the preview cannot disagree with the result.
 */
struct HOUSEFORGE_API FHFAssetFit
{
	/**
	 * Works out where a Content Browser asset goes in place of a generated element.
	 *
	 * WHY THE TARGET IS A BOX RATHER THAN A FIXTURE AND A PLACEMENT DATUM. FHFFixturePlacement
	 * defines five datums and each puts a different origin on the actor - AgainstWall puts it at the
	 * front-left of the footprint on the floor, OnWallFace puts it at the centre with the back plane
	 * on the plaster, UnderSoffit drives the top into the ceiling. Fitting against the spec's
	 * footprint would mean re-deriving which datum placed this actor, and getting that wrong moves
	 * the object - the single largest correctness risk in this code, and one that disappears
	 * entirely by taking the box the GENERATED MESH occupies in the actor's own local space. Whatever
	 * datum placed the actor, the generated geometry is already sitting in the right place relative
	 * to it, so the box is right by construction and there is nothing to re-derive.
	 *
	 * @param GeneratedLocal Box the generated geometry occupies, in element-actor local space.
	 * @param AssetLocal     Box the asset occupies, in its own local space, pivot included.
	 * @param Override       Mode, rotation offset and nudge.
	 */
	static FHFAssetFitResult Solve(const FBox& GeneratedLocal, const FBox& AssetLocal,
		const FHFAssetOverride& Override);
};
