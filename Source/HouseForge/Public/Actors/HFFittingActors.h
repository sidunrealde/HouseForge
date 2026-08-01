// Copyright Siddartha G. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Actors/HFArticulatedActor.h"
#include "Geometry/HFApplianceKit.h"
#include "Geometry/HFSanitaryKit.h"
#include "Model/HFTypes.h"
#include "HFFittingActors.generated.h"

/**
 * The bought fittings of a kitchen: the sink, the hob and the chimney.
 *
 * Three actors in one pair of files, exactly as HFElementActors.h holds the walls, rooms, beams and
 * columns. Each is a thin seam between a pure kit and the articulation framework - parameters, a
 * settings hook, and the two generation overrides - and splitting them across six files would put
 * more ceremony than content in each one.
 *
 * All three are articulated, because all three have something on them that a person turns, lifts or
 * drops open. See .claude/rules/04-conventions.md.
 */

/**
 * A sink in the level: a pressed rim, hollow bowls, and a tap whose spout swings and lever lifts.
 *
 * ## Where it sits
 *
 * At the finished top of whatever it is set into, which is NOT the BaseZ the drawing gave it. That
 * figure was arrived at by adding up a carcass, a plinth and a slab, and it goes stale the moment
 * any of the three changes on the settings page - so AHFHouseActor resolves the host counter's built
 * top and places the sink there. A sink with no counter under it, like the utility one, keeps its
 * own drawn rim height, which is all there is to go on.
 */
UCLASS()
class HOUSEFORGE_API AHFSinkActor : public AHFArticulatedActor
{
	GENERATED_BODY()

public:
	/** Everything this sink is, in centimetres. Origin at the footprint centre, Z = 0 at the rim. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ShowOnlyInnerProperties))
	FHFSinkParams Sink;

	/** Seeds the project's construction figures. Called by the composing layer, not by generation. */
	void ApplyProjectDefaults();

	/** Reads a spec fixture into the parameters. Call after ApplyProjectDefaults. */
	void ApplyFixture(const FHFFixture& Fixture);

	static bool Builds(EHFFixtureType Type) { return Type == EHFFixtureType::Sink; }

	static FName TapLeverPartId() { return FHFSanitaryKit::TapLeverPartId(); }
	static FName TapSpoutPartId() { return FHFSanitaryKit::TapSpoutPartId(); }

protected:
	virtual UE::Geometry::FDynamicMesh3 BuildMesh() const override;
	virtual void BuildParts(TArray<FHFMeshPart>& OutParts) const override;
};

/**
 * A hob in the level: glass on the stone, burners over it, a body through the cutout, turning knobs.
 */
UCLASS()
class HOUSEFORGE_API AHFHobActor : public AHFArticulatedActor
{
	GENERATED_BODY()

public:
	/** Everything this hob is, in centimetres. Origin at the footprint centre, Z = 0 at the stone. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ShowOnlyInnerProperties))
	FHFHobParams Hob;

	void ApplyProjectDefaults();
	void ApplyFixture(const FHFFixture& Fixture);

	static bool Builds(EHFFixtureType Type) { return Type == EHFFixtureType::Hob; }

	static FName KnobPartId(int32 Index) { return FHFApplianceKit::KnobPartId(Index); }

protected:
	virtual UE::Geometry::FDynamicMesh3 BuildMesh() const override;
	virtual void BuildParts(TArray<FHFMeshPart>& OutParts) const override;
};

/**
 * A cooker hood in the level: a pyramidal canopy, a duct up to the soffit, and a filter that drops.
 *
 * ## The duct length is resolved, never declared
 *
 * ApplyCeilingAbove is the whole point of this actor existing separately from a box. A chimney that
 * stops at the top of its own canopy is a box on a wall; one built to a fixed duct length in a room
 * whose false ceiling somebody deepened has its flue buried in plasterboard. Both of those are the
 * ceiling-fan rod defect in a different fitting, and the answer is the same one: the composing layer
 * measures the actual soffit over this chimney and hands the length in.
 */
UCLASS()
class HOUSEFORGE_API AHFChimneyActor : public AHFArticulatedActor
{
	GENERATED_BODY()

public:
	/** Everything this chimney is, in centimetres. Origin at the front-left corner of the canopy. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ShowOnlyInnerProperties))
	FHFChimneyParams Chimney;

	void ApplyProjectDefaults();
	void ApplyFixture(const FHFFixture& Fixture);

	/**
	 * How far the duct has to run to reach the finished soffit above this chimney.
	 *
	 * @param SoffitZAboveCanopyTop Height of the finished ceiling above the top of the canopy. Zero
	 *        or less builds no duct, which is the honest answer for a canopy that already reaches
	 *        the ceiling rather than a reason to build a stub.
	 */
	void ApplyCeilingAbove(double SoffitZAboveCanopyTop);

	/** What a chimney of this type is before the drawing's dimensions go on it. */
	static FHFChimneyParams ParamsFor(const FHFFixture& Fixture);

	static bool Builds(EHFFixtureType Type) { return Type == EHFFixtureType::Chimney; }

	static FName FilterPartId() { return FHFApplianceKit::FilterPartId(); }

protected:
	virtual UE::Geometry::FDynamicMesh3 BuildMesh() const override;
	virtual void BuildParts(TArray<FHFMeshPart>& OutParts) const override;
};
