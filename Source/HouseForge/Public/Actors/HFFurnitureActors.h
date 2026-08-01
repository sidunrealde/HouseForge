// Copyright Siddartha G. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Actors/HFArticulatedActor.h"
#include "Actors/HFElementActors.h"
#include "Geometry/HFBedKit.h"
#include "Geometry/HFDeskKit.h"
#include "Model/HFTypes.h"
#include "HFFurnitureActors.generated.h"

/**
 * The two bedroom pieces that are not cased goods: the bed and the study table.
 *
 * Two actors in one pair of files, exactly as HFFittingActors.h holds the sink, the hob and the
 * chimney. Each is a thin seam between a pure kit and the element framework - parameters, a settings
 * hook, and the generation overrides - and splitting them would put more ceremony than content in
 * each file.
 *
 * They do NOT share a base beyond the framework's own, and that is the point of them being separate
 * from AHFCasedGoodsActor: a bed has no moving part at all and a desk has a drawer pedestal, so one
 * derives from AHFElementActor and the other from AHFArticulatedActor. Forcing both through the
 * articulated base would give the bed an empty Parts array and a master open amount that does
 * nothing, which is a control that lies about the object it is on.
 */

/**
 * A bed in the level: a headboard, a frame and a mattress, in three different materials.
 *
 * ## Where it sits
 *
 * Back to its anchor wall, origin at the front-left corner of the drawn footprint on the floor -
 * FHFFixturePlacement::AgainstWall, the same rule a wardrobe and a run of base units are placed by.
 * The drawn box is the object, headboard included, so a bed cannot grow through the wall behind it.
 *
 * ## What moves
 *
 * Nothing, and deliberately. See FHFBedParams - the reference flat draws a plain double bed rather
 * than a storage bed, and a lid that opens onto a hollow nobody modelled would be a mechanism
 * invented to satisfy a rule instead of to match an object.
 */
UCLASS()
class HOUSEFORGE_API AHFBedActor : public AHFElementActor
{
	GENERATED_BODY()

public:
	/** Everything this bed is, in centimetres, in its own local space. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ShowOnlyInnerProperties))
	FHFBedParams Bed;

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
	 * What a bed of this size is before anything else touches it.
	 *
	 * Static and public because the composing layer has to be able to ask how tall one comes out
	 * without spawning it: a bed drawn 600 high stands 1050 with its headboard on, and FHFCeilingFit
	 * takes the BUILT envelope rather than the drawn box. The same reason AHFFanActor::ParamsFor is
	 * static and public.
	 */
	static FHFBedParams ParamsFor(const FHFFixture& Fixture);

	static bool Builds(EHFFixtureType Type) { return Type == EHFFixtureType::Bed; }

protected:
	virtual UE::Geometry::FDynamicMesh3 BuildMesh() const override;
};

/**
 * A study table in the level: a top on a drawer pedestal and a panel gable, with a knee hole between.
 *
 * ## Where it sits
 *
 * Back to its anchor wall, like every other run - but NOT scribed into the skirting. The board runs
 * on behind a desk, because a desk has 700 mm of clear wall under it, and the supports are set off
 * the plaster to let it. That setback is resolved in ApplyProjectDefaults from the project's own
 * skirting depth, because only the composing layer knows what section this house is skirted in.
 *
 * ## What moves
 *
 * The pedestal drawers, on full-extension runners with their geared intermediate members.
 */
UCLASS()
class HOUSEFORGE_API AHFDeskActor : public AHFArticulatedActor
{
	GENERATED_BODY()

public:
	/** Everything this desk is, in centimetres, in its own local space. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ShowOnlyInnerProperties))
	FHFDeskParams Desk;

	/**
	 * Seeds the project's construction figures, INCLUDING the skirting the supports have to clear.
	 *
	 * The skirting figure is the unusual one and it is the reason this hook matters here: a desk that
	 * took the joinery defaults and nothing else would stand 18 mm inside the board behind it, in
	 * every room, for ever, and no test of the desk on its own could see it.
	 */
	void ApplyProjectDefaults();

	/** Reads a spec fixture into the parameters. Call after ApplyProjectDefaults. */
	void ApplyFixture(const FHFFixture& Fixture);

	/** What a desk of this size is before the project's figures go on it. */
	static FHFDeskParams ParamsFor(const FHFFixture& Fixture);

	static bool Builds(EHFFixtureType Type) { return Type == EHFFixtureType::StudyTable; }

protected:
	virtual UE::Geometry::FDynamicMesh3 BuildMesh() const override;
	virtual void BuildParts(TArray<FHFMeshPart>& OutParts) const override;
};
