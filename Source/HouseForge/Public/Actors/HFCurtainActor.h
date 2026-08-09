// Copyright Siddartha G. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Actors/HFArticulatedActor.h"
#include "Geometry/HFCurtainKit.h"
#include "Geometry/HFWallPlateKit.h"
#include "Model/HFTypes.h"
#include "HFCurtainActor.generated.h"

/**
 * A curtain in the level: cloth on a track, which draws.
 *
 * ## Why this is not the pelmet
 *
 * HFTrimActors.h says of the pelmet that "a CURTAIN slides, and it is the one thing in this
 * milestone that a rigid part genuinely cannot represent... The pelmet builds the track and the
 * slot; EHFFixtureType::Curtain is a type of its own and this flat declares none of them."
 *
 * That is still the right split and it is the one kept. The pelmet is joinery - four boards and an
 * aluminium section, painted with the ceiling, fixed and correct - and turning it into an articulated
 * actor to carry somebody else's fabric would put the cloth's hand-edit flags, poses and part
 * components onto a box that has none of its own. The curtain is a separate fixture that HANGS on the
 * pelmet's track, which is exactly how the two are related on site.
 *
 * What makes it one mechanism rather than two is ApplyPelmet. A curtain does not declare where its
 * track is, how long it is, how far it may hang out from the wall, or how far off the floor it
 * finishes: it is TOLD, by the composing layer, out of the pelmet it hangs in and the room it hangs
 * in - the same rule a railing follows for its parapet and a sink for its counter. A curtain with no
 * pelmet falls back on its own drawn box, which is the honest answer for a curtain on a bare pole.
 *
 * ## What moves
 *
 * Every fold, on its own glider, geared to the leading fold of its leaf. See FHFCurtainParams: the
 * folds converging along the track IS the gather, and one part per leaf sliding would be a blind.
 *
 * A pair does NOT cancel the way a two-track slider does, and the difference is worth stating because
 * the shapes are so alike. A slider's two leaves share one aperture and run into each other's bays,
 * so one control driving both exchanges tracks and opens nothing - see FHFPartMotion::bMasterOpens.
 * A curtain pair stacks at OPPOSITE ends, so one control opens the middle and keeps opening it. Both
 * leaves are therefore master-driven, and HouseForge.Curtain.Draws measures the aperture in
 * centimetres rather than trusting the argument.
 */
UCLASS()
class HOUSEFORGE_API AHFCurtainActor : public AHFArticulatedActor
{
	GENERATED_BODY()

public:
	/**
	 * Everything this curtain is, in centimetres.
	 *
	 * Origin at the centre of the track run, ON the track line, with the cloth hanging into -Z. See
	 * FHFCurtainParams for why the datum is the track and not the hem.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ShowOnlyInnerProperties))
	FHFCurtainParams Curtain;

	/** Seeds the project's construction figures. Called by the composing layer, not by generation. */
	void ApplyProjectDefaults();

	/** Reads a spec fixture into the parameters. Call after ApplyProjectDefaults. */
	void ApplyFixture(const FHFFixture& Fixture);

	/**
	 * The pelmet this curtain hangs in, as dimensions.
	 *
	 * THE TRACK, THE STACK ALLOWANCE AND THE HANGING DEPTH ALL COME FROM HERE. A generator may not go
	 * looking for the box above it, so the composing layer measures the pelmet and hands over its
	 * clear width and the depth its slot has left once the track is in it. Without this the curtain
	 * would be sized off a drawn footprint that says nothing about either.
	 *
	 * @param Pelmet The pelmet as built, after its own sanitise.
	 */
	void ApplyPelmet(const FHFPelmetParams& Pelmet);

	/**
	 * How far the hem finishes above the floor it hangs over. FLOOR LENGTH.
	 *
	 * A DROP IS A MEASUREMENT, NOT A DRAWING FIGURE, and it is the same argument the pelmet's own
	 * height makes: the track's height depends on the false ceiling, which is a project setting, so
	 * a drop taken from the drawing is stale the moment anybody changes the ceiling. The composing
	 * layer knows where the track ended up and where the floor is, and subtracts.
	 *
	 * @param TrackToFloor Clear height from the glider line to the finished floor.
	 * @param FloorClearance Air left under the hem. 15 mm, so it does not sweep the tiles.
	 */
	void ApplyDrop(double TrackToFloor, double FloorClearance = 1.5);

	/**
	 * The other length a curtain is made to: hem below the SILL rather than at the floor.
	 *
	 * ## Not a style choice. It is what a window with something under it takes
	 *
	 * A floor-length curtain wants clear floor under it, and half the windows in a flat do not have
	 * any: the master bedroom's bed stands against its window wall and bedroom 2's does the same. Hung
	 * to the floor, the cloth hangs THROUGH the bed - and it does so at every open amount, because the
	 * folds sweep along the wall the bed is against. The whole-flat sweep measured 71 mm of curtain
	 * inside 'F_MBed_Bed' and 70 inside 'F_Bed2_Bed' the first time these were built.
	 *
	 * A curtain-maker's answer to that is not a shorter floor-length curtain, it is APRON LENGTH: the
	 * hem finishes 100-150 mm below the sill, which covers the sill line and the frame under it and
	 * stops well clear of whatever the room has put there. So the choice is made by what is standing
	 * under the window rather than by a flag in the drawing - the same shape of answer as
	 * AHFCasedGoodsActor::bBankAtRunStart, which asks what is beside a run before deciding which end
	 * its bank of drawers goes.
	 *
	 * @param TrackToSill Clear height from the glider line down to the top of the sill.
	 * @param BelowSill How far the hem hangs past the sill. 120 mm is apron length.
	 */
	void ApplyDropToSill(double TrackToSill, double BelowSill = 12.0);

	/** What a curtain of this size comes out as before anything else touches it. */
	static FHFCurtainParams ParamsFor(const FHFFixture& Fixture);

	static bool Builds(EHFFixtureType Type) { return Type == EHFFixtureType::Curtain; }

	/** Part id of one fold. Leaf 0 stacks at -X for a pair. */
	static FName FoldPartId(int32 LeafIndex, int32 FoldIndex)
	{
		return FHFCurtainKit::FoldPartId(LeafIndex, FoldIndex);
	}

	/** The fold a single control runs, which every other fold in its leaf is geared to. */
	FName LeadFoldPartId(int32 LeafIndex) const
	{
		return FHFCurtainKit::LeadFoldPartId(LeafIndex, Curtain.FoldsPerLeaf());
	}

	/**
	 * Draws one leaf without touching the other. The verb an artist actually wants.
	 *
	 * Setting the leading fold's amount and letting the gearing carry the rest, so there is no way to
	 * pose half a leaf. On a single-draw curtain leaf 0 is the only one there is.
	 *
	 * @return false if there is no such leaf.
	 */
	UFUNCTION(BlueprintCallable, Category = "HouseForge|Articulation")
	bool DrawLeaf(int32 LeafIndex, double OpenAmount);

protected:
	virtual UE::Geometry::FDynamicMesh3 BuildMesh() const override;
	virtual void BuildParts(TArray<FHFMeshPart>& OutParts) const override;
};
