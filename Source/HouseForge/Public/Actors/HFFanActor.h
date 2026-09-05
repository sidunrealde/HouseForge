// Copyright Siddartha G. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Actors/HFArticulatedActor.h"
#include "Geometry/HFFanKit.h"
#include "Model/HFTypes.h"
#include "HFFanActor.generated.h"

class UPointLightComponent;

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
	 * Lengthens the rod so the fan hangs BELOW a false ceiling rather than inside it.
	 *
	 * Called by the composing layer after ApplyProjectDefaults and ApplyFixture, because what a rod
	 * has to clear is a property of the ROOM and a generator may not read one.
	 * FHFGenerators::CeilingSoffitDropAt is what answers the question; this puts the answer on the
	 * fan, moving the canopy down to the soffit so it covers the hole cut for the rod.
	 *
	 * @param SoffitDrop How far below the structural slab the panel over this fan hangs. 0 for a fan
	 *                   under bare slab, which includes the open centre of a peripheral or cove
	 *                   ceiling - and therefore every fan in the reference flat.
	 */
	void ApplyCeilingAbove(double SoffitDrop);

	/**
	 * The parameters a fan at this fixture will be built from, dimensions and project figures both.
	 *
	 * For the composing layer to size things that have to AGREE with a fan it has not spawned yet -
	 * the hole a false ceiling cuts for a rod, and the duct cored through a wall for an extract.
	 * Both were derived independently once and both drifted.
	 */
	static FHFFanParams ParamsFor(const FHFFixture& Fixture);

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
	 * Where a fan with this id is stopped, as a fraction of ONE BLADE PITCH. 0 to 1, never in turns.
	 *
	 * Three fans generated from identical parameters and all stopped with a blade at the same angle
	 * read as three copies of one object, which is exactly what they are and exactly what a still
	 * must not show. The generator cannot fix that - it is pure, and has no idea how many fans exist -
	 * so the composing layer varies it, and varies it DETERMINISTICALLY: a rebuild of the same house
	 * has to produce the same flat, or two renders of one spec would differ for no stated reason.
	 *
	 * ## The three things decided here
	 *
	 * WHERE IT COMES FROM: a hash of the fixture id, and not the spec. A phase is a POSE, and poses
	 * live on the actor and survive a rebuild through CapturePartPoses - the spec is the import and
	 * export format, not a live second source of truth (.claude/rules/04-conventions.md). Somebody who
	 * wants a particular fan pinned for a particular still poses that fan, and it stays posed. A hash
	 * rather than a counter so that inserting a fan into the living room does not shift every fan
	 * after it in the file.
	 *
	 * WHAT UNIT IT IS IN: fractions of a blade pitch, because that is the only range in which two
	 * fans can look different. ApplyFixture divides by the blade count to get the phase.
	 *
	 * HOW FAR APART IS FAR ENOUGH, AND OVER WHICH FANS: at least 0.08 of a blade pitch - about ten
	 * degrees of blade on a three-blade fan - and only over fans built from IDENTICAL parameters.
	 * The flat's three ceiling fans are the ones that read as copies of each other; its three
	 * extracts differ in sweep, blade count and case and never could. That bar is asserted in
	 * HouseForge.Editor.FansInTheReferenceFlatTurn against the flat's own ids. It is a real bar and
	 * not a guarantee of the hash: a future set of ids could cluster inside it, and if it does, the
	 * honest answer is that those two fans really would look alike and one of them should be posed.
	 */
	static double PhaseForId(FName FixtureId);

	/**
	 * The hole an extract needs cored through the wall it is fixed to.
	 *
	 * AN EXTRACT HAS TO BLOW THROUGH SOMETHING. Its case carries an aperture and its blades turn
	 * inside that aperture, and none of that is worth anything if the masonry behind it is solid -
	 * which it was, for every extract in the reference flat. The failure is invisible from the room:
	 * the case covers exactly the spot where the hole is not.
	 *
	 * DERIVED HERE RATHER THAN ASKED OF THE DRAWING. A plan marks an extract, not the duct cored for
	 * it; the hole is a consequence of the fan's own size and putting it in the spec would be asking
	 * a drawing for something no drawing has, and letting the two disagree.
	 *
	 * Returns an opening in the wall's own terms - an offset along it, a width, a height and a sill -
	 * so the wall cuts it exactly as it cuts a window. Not added to the spec's openings, so no
	 * ventilator sash is built in it: this is a duct, not a window.
	 *
	 * ## Two datums
	 *
	 * A fixture's BaseZ is measured above the ROOM FLOOR and an opening's SillHeight above the WALL'S
	 * BASE, so the room has to be here to convert between them. Without it the hole sits at
	 * Wall.BaseZ + BaseZ + Height/2 while the fan sits at Room.FloorZ + BaseZ + Height/2, and the two
	 * differ by (Wall.BaseZ - Room.FloorZ) - both zero throughout the reference flat, so they agreed
	 * by coincidence until the first raised floor.
	 *
	 * @param Fixture The extract. A fixture of any other type gives a zero-width opening.
	 * @param Wall The wall it is anchored to, for the offset the hole is measured along and the base
	 *             its sill is measured from.
	 * @param Room The room the extract serves, for its floor level. Null is read as a floor at zero.
	 *             NOT DEFAULTED: a caller that forgets it is a caller that puts the hole back in the
	 *             wrong datum, and defaulting to null would let that happen silently.
	 */
	static FHFOpening DuctOpeningFor(const FHFFixture& Fixture, const FHFWall& Wall, const FHFRoom* Room);

	/** Part id of the spinning assembly: motor housing and blades. */
	static FName RotorPartId() { return FHFFanKit::RotorPartId; }

	/**
	 * Puts the light kit under the motor, and takes away the one that was there.
	 *
	 * ## Why a fan carries a light at all
	 *
	 * Because in these flats it is very often the only thing on the ceiling. Every bedroom in the
	 * reference 2BHK has a fan; the light fittings are drawn on the electrical layer, and a spec
	 * that has not had that layer read into it produces a bedroom whose ceiling has a fan on it and
	 * nothing else. Lit only by a cove or a run of downlights around the edge of the room, the
	 * middle of that room is dark - and the fan hanging in it is the fitting a person would reach
	 * for the switch of.
	 *
	 * A ceiling fan with an integral light kit is a real and common product, so this is not an
	 * invention. It is a default rather than a certainty, which is why bHasLightKit exists and why
	 * a drawing that does mark separate light fixtures can be built with it switched off.
	 *
	 * ## What is NOT done here, and why
	 *
	 * No geometry. The lamp is a light component placed just under the motor housing, and the
	 * housing is what stands in for the lamp glass. Modelling a separate bowl would change the mesh
	 * of every fan in the flat - its volume, its bounds, its part fingerprints - and the fan is
	 * finished, tested work from the fixtures milestone. That is a change for whoever extends the
	 * fixture kit, not for the lighting pass, and doing it here would have meant re-recording a
	 * dozen measured figures to add a light.
	 *
	 * ## Extracts get nothing
	 *
	 * A bathroom extract has no lamp in it. Called on one, this removes any light and returns 0.
	 *
	 * Idempotent, and safe on a hand-edited actor: it does not touch the mesh. See
	 * AHFLightFixtureActor for the same argument at greater length.
	 *
	 * @return How many lights the fan now has: 1, or 0 when there is no kit or it is switched off.
	 */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "HouseForge")
	int32 RebuildLights();

	/**
	 * Whether this fan has a light kit in it. Seeded true for a ceiling fan, false for an extract.
	 *
	 * Seeded rather than fixed, so that a flat whose drawing DOES carry its light fixtures can have
	 * the fans switched back to being only fans.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Lighting")
	bool bHasLightKit = false;

	/**
	 * Output of the kit, in lumens.
	 *
	 * A fan light kit is a modest thing - 12 W or so, around 900 lm - and deliberately dimmer than
	 * the 1600 lm AHFLightFixtureActor gives a dedicated ceiling panel. It is the lamp that comes
	 * bolted to a fan rather than the one that lights the room properly, and it should not read as
	 * brighter than a fitting somebody chose.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Lighting", meta = (ClampMin = "0.0"))
	double Lumens = 900.0;

	/** Colour temperature in kelvin. 3000 is the warm white these flats are lit with. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Lighting", meta = (ClampMin = "1700.0", ClampMax = "12000.0"))
	double TemperatureKelvin = 3000.0;

	/** How far the light is allowed to reach, in centimetres. A hard cull, not a falloff. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Lighting", meta = (ClampMin = "0.0"))
	double AttenuationRadius = 900.0;

	/** The light this fan owns, so a rebuild can take it away again. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HouseForge|Lighting")
	TObjectPtr<UPointLightComponent> Light;

protected:
	virtual UE::Geometry::FDynamicMesh3 BuildMesh() const override;
	virtual void BuildParts(TArray<FHFMeshPart>& OutParts) const override;
};
