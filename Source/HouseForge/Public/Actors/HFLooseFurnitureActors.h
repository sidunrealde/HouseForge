// Copyright Siddartha G. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Actors/HFElementActors.h"
#include "Geometry/HFFrameKit.h"
#include "Geometry/HFUpholsteryKit.h"
#include "Model/HFTypes.h"
#include "HFLooseFurnitureActors.generated.h"

/**
 * The living room's loose furniture: the sofa, the two tables and the dining chairs.
 *
 * Three actors in one pair of files, as HFFittingActors.h holds the sink, the hob and the chimney and
 * HFFurnitureActors.h holds the bed and the desk. Each is a thin seam between a pure kit and the
 * element framework, and splitting them would put more ceremony than content in each file.
 *
 * ## What LOOSE means here, and why all three derive from AHFElementActor
 *
 * Nothing in this group articulates. That is not an oversight and it is not laziness about the rule
 * in .claude/rules/04-conventions.md - it is the rule applied honestly. A recliner's footrest swings,
 * an extension table's leaf slides, a storage ottoman's lid lifts; the reference flat draws a plain
 * three-seater, two fixed-top tables and four dining chairs, and not one of them has a hinge, a
 * travel limit or an open amount that would mean anything. Deriving them from AHFArticulatedActor
 * would give each an empty Parts array and a master open amount that does nothing, which is a
 * control that lies about the object it is on.
 *
 * A chair is MOVED, which is a different thing from articulated: where it stands is a fact about the
 * room, so "can it be pulled out" is measured as a clearance in the composing layer rather than
 * modelled as a mechanism here.
 *
 * ## THESE ARE PLACEHOLDERS AND THEY ARE MEANT TO BE REPLACED
 *
 * Milestone 12 exists to swap generated fixtures for Content Browser assets, and loose furniture is
 * the first thing anybody will swap. So the bar these are built to is deliberately proportion,
 * height, footprint and SOFT EDGES rather than detail: piping on a sofa arm, a turned leg profile,
 * stitching, a seat that dips where somebody sits. Every one of those is work that gets thrown away
 * the moment a real asset lands, and none of them is what makes the room read from the doorway.
 *
 * What does make it read is that the sofa is 420 to the seat and not 500, that its arms stand proud
 * of its base, and that nothing on it has a mathematically sharp edge. See FHFUpholsteryKit.
 */

/**
 * A sofa in the level: legs, base, arms, back and two cushions per seat.
 *
 * ## Where it sits
 *
 * Back to its anchor wall, origin at the front-left corner of the drawn footprint on the floor -
 * FHFFixturePlacement::AgainstWall, the same rule a wardrobe, a run of base units and a bed are
 * placed by. A sofa has a front and a back, and a drawing states a yaw that is a one-in-two chance
 * of being the half turn that puts the back of it into the room.
 */
UCLASS()
class HOUSEFORGE_API AHFSofaActor : public AHFElementActor
{
	GENERATED_BODY()

public:
	/** Everything this sofa is, in centimetres, in its own local space. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ShowOnlyInnerProperties))
	FHFSofaParams Sofa;

	/** Seeds the project's construction figures. Called by the composing layer, not by generation. */
	void ApplyProjectDefaults();

	/** Reads a spec fixture into the parameters. Call after ApplyProjectDefaults. */
	void ApplyFixture(const FHFFixture& Fixture);

	/**
	 * What a sofa of this size is before anything else touches it.
	 *
	 * Static and public for the reason AHFBedActor::ParamsFor is: the composing layer has to be able
	 * to ask what one comes out as without spawning it.
	 */
	static FHFSofaParams ParamsFor(const FHFFixture& Fixture);

	static bool Builds(EHFFixtureType Type) { return Type == EHFFixtureType::Sofa; }

protected:
	virtual UE::Geometry::FDynamicMesh3 BuildMesh() const override;
};

/**
 * A table in the level: a dining table or a coffee table, which are the same object at two sizes.
 *
 * ## One actor, two types, one recipe switch
 *
 * The cased goods kit's argument, applied to the smaller case: a 1400 x 800 four-seater at 750 and a
 * 1100 x 600 coffee table at 400 differ in their proportions, in whether there is a shelf between the
 * legs, and in nothing the composing layer can see. What each of them IS lives in ParamsFor, in one
 * switch, where the two can be read against each other.
 *
 * ## Where it sits
 *
 * FHFFixturePlacement::FreeStanding - origin at the CENTRE of the footprint. A table has no back and
 * no set-out corner, and a drawing places one by where it sits rather than by a corner of it.
 */
UCLASS()
class HOUSEFORGE_API AHFTableActor : public AHFElementActor
{
	GENERATED_BODY()

public:
	/** Everything this table is, in centimetres, in its own local space. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ShowOnlyInnerProperties))
	FHFTableParams Table;

	void ApplyProjectDefaults();

	void ApplyFixture(const FHFFixture& Fixture);

	/** What a table of this type and size is. The recipe switch lives here. */
	static FHFTableParams ParamsFor(const FHFFixture& Fixture);

	static bool Builds(EHFFixtureType Type)
	{
		return Type == EHFFixtureType::DiningTable || Type == EHFFixtureType::CoffeeTable;
	}

protected:
	virtual UE::Geometry::FDynamicMesh3 BuildMesh() const override;
};

/**
 * A dining chair in the level.
 *
 * ## Where it sits
 *
 * FHFFixturePlacement::FreeStanding - origin at the CENTRE of the footprint, turned by the drawing's
 * own yaw. A chair's yaw is real information rather than a coin toss: which side of the table it is
 * on is what the yaw says, and unlike a run of joinery there is no wall to resolve it against.
 */
UCLASS()
class HOUSEFORGE_API AHFChairActor : public AHFElementActor
{
	GENERATED_BODY()

public:
	/** Everything this chair is, in centimetres, in its own local space. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ShowOnlyInnerProperties))
	FHFChairParams Chair;

	void ApplyProjectDefaults();

	void ApplyFixture(const FHFFixture& Fixture);

	static FHFChairParams ParamsFor(const FHFFixture& Fixture);

	static bool Builds(EHFFixtureType Type) { return Type == EHFFixtureType::Chair; }

protected:
	virtual UE::Geometry::FDynamicMesh3 BuildMesh() const override;
};
