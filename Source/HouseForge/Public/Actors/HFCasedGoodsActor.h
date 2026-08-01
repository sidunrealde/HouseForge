// Copyright Siddartha G. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Actors/HFArticulatedActor.h"
#include "Geometry/HFCasedGoodsKit.h"
#include "Model/HFTypes.h"
#include "HFCasedGoodsActor.generated.h"

/**
 * A run of cased goods in the level: carcasses, plinth, shelves, cornice, and fronts that open.
 *
 * ONE ACTOR FOR SEVEN FIXTURE TYPES, exactly as AHFFanActor is one actor for a ceiling fan and an
 * extract. A kitchen base unit, a kitchen wall unit, a TV unit, a nightstand, a shoe rack, a vanity
 * and a study table's pedestal differ in their proportions and in which front each bay carries, and
 * in nothing else - so seven actor classes would be seven copies of BuildMesh, BuildParts and a
 * settings hook, with the differences hidden in ApplyFixture where nobody could compare them.
 *
 * What the type DOES decide is the recipe: how a drawing's ShutterCount and DrawerCount become bays,
 * whether the run stands on a plinth or hangs on the wall, and which motion its fronts get. That is
 * ReadFixture, and it is the whole of the per-type knowledge in this file.
 *
 * ## What it owns
 *
 * Its parameters, like every other element actor - see .claude/rules/04-conventions.md. The spec's
 * FHFFixture is read ONCE by ApplyFixture and the actor is what gets edited afterwards.
 *
 * ## Where the project's figures come in
 *
 * ApplyProjectDefaults, called by the composing layer before the first generation - never inside a
 * generator. FHFCasedGoodsKit::Build is a pure function of its parameters and reaches for nothing.
 */
UCLASS()
class HOUSEFORGE_API AHFCasedGoodsActor : public AHFArticulatedActor
{
	GENERATED_BODY()

public:
	/**
	 * Everything this run is, in centimetres, in its own local space.
	 *
	 * The actor's origin is the front-left corner of the carcass footprint at the underside of
	 * whatever the run stands on - the corner a fitted run is actually set out from on site.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ShowOnlyInnerProperties))
	FHFCasedGoodsParams Case;

	/**
	 * Puts the drawer bank at the -X end of the run instead of the +X end.
	 *
	 * A PULL-OUT NEEDS SOMEWHERE TO PULL OUT TO, and in an L-shaped kitchen one end of each run has
	 * the return run standing in front of it. The west run's corner drawer had 2.5 cm of clear travel
	 * out of 55 before it drove into the north run's carcasses - a drawer that reports full travel,
	 * sweeps its whole declared distance, satisfies every assertion about motion, and cannot be
	 * opened. Exactly the failure mode the wardrobe's cancelling leaf pair was.
	 *
	 * Set by the composing layer, because only it can see the run standing in the way. Call before
	 * ApplyFixture, which is what reads it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	bool bBankAtRunStart = false;

	/** Seeds the project's construction figures. Called by the composing layer, not by generation. */
	void ApplyProjectDefaults();

	/**
	 * Reads a spec fixture into the parameters. Call after ApplyProjectDefaults.
	 *
	 * The spec is in Unreal centimetres by the time it reaches an actor: AHFHouseActor::SetSpec
	 * converts exactly once, at ingest. Nothing here converts anything.
	 */
	void ApplyFixture(const FHFFixture& Fixture);

	/**
	 * What a run of this type is, before the drawing's own dimensions go on it.
	 *
	 * Static and public because the composing layer has to be able to ask how tall one comes out
	 * without spawning it - a wall unit with a cornice stands 60 mm higher than it is drawn, and
	 * FHFCeilingFit needs the built envelope rather than the drawn box. The same reason
	 * AHFFanActor::ParamsFor is static and public.
	 */
	static FHFCasedGoodsParams ParamsFor(const FHFFixture& Fixture);

	/** True for the fixture types this actor builds. */
	static bool Builds(EHFFixtureType Type);

	/** Part id of a leaf. See FHFCasedGoodsKit::ShutterPartId. */
	static FName ShutterPartId(int32 Unit, int32 Bay, int32 Leaf)
	{
		return FHFCasedGoodsKit::ShutterPartId(Unit, Bay, Leaf);
	}

protected:
	virtual UE::Geometry::FDynamicMesh3 BuildMesh() const override;
	virtual void BuildParts(TArray<FHFMeshPart>& OutParts) const override;
};
