// Copyright Siddartha G. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Actors/HFArticulatedActor.h"
#include "Geometry/HFFanKit.h"
#include "Model/HFTypes.h"
#include "HFFanActor.generated.h"

/**
 * A fan in the level, whose blades actually turn.
 *
 * THE FIRST PRODUCTION CONSUMER OF EHFMotionType::Spin. The spin mechanism landed complete - an
 * unbounded phase, a rate in revolutions per minute, a pose that survives a rebuild, an integrator
 * on the actor - and then nothing in the plugin ever created a spinning part. Spin appeared in the
 * articulation header, in its implementation, and in test files, and nowhere else; the reference
 * flat's three ceiling fans and three extracts were lines in a spec that produced no actor at all.
 * AHFHouseActor read EHFFixtureType::CeilingFan only to punch a rod hole in the false ceiling above
 * a fan that was not there, and never read ExhaustFan.
 *
 * That is the same shape of failure the joinery kit had before AHFWardrobeActor: a mechanism proven
 * on meshes and in unit tests, with the seam to the level entirely untested. This actor is that seam
 * for the spin.
 *
 * ## What it owns
 *
 * Its parameters, like every other element actor. The spec's FHFFixture is read ONCE by ApplyFixture
 * and the actor is what gets edited afterwards - the house spec is the import and export format, not
 * a live second source of truth.
 *
 * ## What moves
 *
 * One part, Rotor, spinning about the fan's own axis with the motor housing and the blades on it.
 * The canopy, the down rod and the extract's case do not turn and stay on the fixed shell. A fan
 * baked into one mesh is a fan that cannot run, which .claude/rules/04-conventions.md rules out.
 *
 * ## Why nothing here ticks
 *
 * This is an editor plugin and these actors do not tick, deliberately. A phase is a POSE, and it is
 * advanced by whoever wants motion - a Sequencer track, a walkthrough pawn, or
 * AHFArticulatedActor::AdvanceSpinningParts - exactly as a door's open amount is. A fan that span on
 * its own would rewrite the level's transforms every frame in the editor for no one's benefit.
 */
UCLASS()
class HOUSEFORGE_API AHFFanActor : public AHFArticulatedActor
{
	GENERATED_BODY()

public:
	/** Everything this fan is, in centimetres, about its own axis. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ShowOnlyInnerProperties))
	FHFFanParams Fan;

	/**
	 * Seeds the project's figures. Called by the composing layer, not by generation.
	 *
	 * The only function in this class that knows a settings object could exist.
	 *
	 * @param Kind Which catalogue of figures to take, since a ceiling fan and an extract share none.
	 */
	void ApplyProjectDefaults(EHFFanKind Kind);

	/**
	 * Reads a spec fixture into the parameters. Call after ApplyProjectDefaults.
	 *
	 * The spec is in Unreal centimetres by the time it reaches an actor - AHFHouseActor::SetSpec
	 * converts exactly once, at ingest - so nothing here converts anything.
	 */
	void ApplyFixture(const FHFFixture& Fixture);

	/**
	 * Where a fan at this fixture hangs, and which way its axis points.
	 *
	 * The two kinds mount on different surfaces, so they are placed by different things. A CEILING
	 * fan hangs from the STRUCTURAL slab with its axis straight down - not from the false ceiling
	 * soffit, which is why AHFCeilingActor cuts a hole for the rod to pass through in the first
	 * place. An EXTRACT is fixed to the face of the wall it discharges through, with its axis on that
	 * wall's normal, pointing into the room it serves.
	 *
	 * @param Room The room the fixture stands in, for its floor and ceiling levels. May be null.
	 * @param AnchorWall The wall an extract is set into. Ignored for a ceiling fan; may be null.
	 */
	static FTransform PlacementFor(const FHFFixture& Fixture, const FHFRoom* Room, const FHFWall* AnchorWall);

	/**
	 * A stable, well-spread starting phase for a fan with this id, in revolutions.
	 *
	 * Three fans generated from identical parameters and all stopped with a blade at the same angle
	 * read as three copies of one object, which is exactly what they are and exactly what a still
	 * must not show. The generator cannot fix that - it is pure, and has no idea how many fans exist -
	 * so the composing layer varies it, and varies it DETERMINISTICALLY: a rebuild of the same house
	 * has to produce the same flat, or two renders of one spec would differ for no stated reason.
	 */
	static double PhaseForId(FName FixtureId);

	/** Part id of the spinning assembly: motor housing and blades. */
	static FName RotorPartId() { return FHFFanKit::RotorPartId; }

protected:
	virtual UE::Geometry::FDynamicMesh3 BuildMesh() const override;
	virtual void BuildParts(TArray<FHFMeshPart>& OutParts) const override;
};
