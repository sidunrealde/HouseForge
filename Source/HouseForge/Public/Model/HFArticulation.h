// Copyright Siddartha G. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "HFArticulation.generated.h"

/**
 * How a part moves.
 *
 * Only the motions this domain actually contains. A door swings, a drawer pulls out, a wardrobe
 * shutter swings or slides, a sliding sash slides, and a fan turns - nothing is served by making
 * the framework general enough to express motions no flat has.
 *
 * The first two are OPENINGS and the third is not, and that distinction runs through the whole
 * framework. A hinge and a slide both go from shut to fully open and stop, so "how far open, 0 to
 * 1" describes them completely. A fan has no such state: it revolves, it has no limit to reach,
 * and there is no sense in which it is 40% open. See EHFMotionType::Spin.
 */
UENUM(BlueprintType)
enum class EHFMotionType : uint8
{
	/** Fixed. A part with no motion still gets its own component if it needs its own material. */
	None,
	/** Rotates about Axis, through the part's own origin, between shut and MaxAngleDegrees. */
	Hinge,
	/** Translates along Axis. */
	Slide,

	/**
	 * Revolves about Axis without limit: a ceiling fan, an exhaust fan.
	 *
	 * Its own motion type rather than a hinge with the angle clamp let out, which was the other
	 * option and is worse in three ways that all show up in a walkthrough.
	 *
	 * A fan is not open. OpenAmount runs 0 to 1 between two end stops, and every mechanism built on
	 * it reads it that way: MasterOpenAmount drives everything on a fixture to "open" to check it
	 * articulates, CapturePartPoses treats a non-zero amount as a deliberate pose to carry across a
	 * rebuild, and the sequencing below asks whether a part is far enough open to let another move.
	 * A fan expressed as a hinge to 36000 degrees answers all three nonsensically - "open all" would
	 * leave every fan in the flat stopped at fifty turns, and half open would be a meaningless angle
	 * rather than a fan at half speed.
	 *
	 * A fan has no limit to reach. The pose of a spinning part is a PHASE that keeps counting, so
	 * FHFPartState::SpinTurns is unbounded and is never clamped - which is exactly what "turns past
	 * 360 degrees" needs, and what a 0..1 amount against a fixed maximum cannot give at all.
	 *
	 * And a fan has a speed, which no opening has. RevolutionsPerMinute is the thing an artist
	 * actually knows about a ceiling fan, and it is what turns a phase into an animation.
	 *
	 * The angle clamp on MaxAngleDegrees therefore stays where it is. It is right for a hinge: a
	 * door or a shutter past half a turn is a mistake worth catching in the details panel, and
	 * relaxing it to let a fan through would have removed the guard from every door to serve a part
	 * that should never have been a hinge in the first place.
	 */
	Spin
};

/**
 * The motion one part is capable of.
 *
 * Axis and travel are expressed in the part's own local space, whose origin is the pivot - the
 * hinge line for a hinge, any point on the line of travel for a slide. That is what lets a
 * generator stay pure: it returns a part's mesh in that part's own space, and knows nothing about
 * where in the house the part ends up.
 *
 * Travel is signed. The sign is the direction of opening, so a left-hung and a right-hung shutter
 * differ only by the sign of MaxAngleDegrees rather than by a separate handedness flag.
 */
USTRUCT(BlueprintType)
struct HOUSEFORGE_API FHFPartMotion
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	EHFMotionType Type = EHFMotionType::None;

	/** Axis of rotation or direction of travel, in the part's local space. Normalised on use. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	FVector Axis = FVector::ZAxisVector;

	/** Rotation at OpenAmount 1, in degrees. Signed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge",
		meta = (EditCondition = "Type == EHFMotionType::Hinge", ClampMin = "-180.0", ClampMax = "180.0"))
	double MaxAngleDegrees = 90.0;

	/** Travel at OpenAmount 1, in centimetres. Signed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge",
		meta = (EditCondition = "Type == EHFMotionType::Slide"))
	double MaxTravelCm = 0.0;

	/**
	 * How fast a spinning part turns, in revolutions per minute. Signed: the sign is the direction.
	 *
	 * A rate rather than a limit, because a fan has no limit. 300 rpm is a ceiling fan at speed 5 and
	 * 1350 an exhaust fan; both are real numbers off a real fan's plate, which is the point of
	 * expressing this as a speed rather than as an angle somebody has to work out.
	 *
	 * Nothing here spins on its own - this is an editor plugin and these actors do not tick. It is
	 * the figure that turns elapsed time into revolutions, for a Sequencer track, a walkthrough
	 * pawn, or AHFArticulatedActor::AdvanceSpinningParts.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge",
		meta = (EditCondition = "Type == EHFMotionType::Spin"))
	double RevolutionsPerMinute = 0.0;

	/**
	 * The part this one is geared to, if any. Empty for a part that moves on its own.
	 *
	 * A driven part takes the driver's open amount rather than carrying one of its own; the gearing
	 * is already expressed by its own travel. The case this exists for is the intermediate member of
	 * a full-extension drawer runner, which travels exactly half as far as the drawer it carries -
	 * and which cannot be left to a separate open amount, because a drawer pulled out while its
	 * intermediate stayed behind is a drawer hanging on nothing.
	 *
	 * It is a real motion, not a bookkeeping trick: the member slides, so it is its own component
	 * with its own pivot and travel limit, per .claude/rules/04-conventions.md.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HouseForge")
	FName DrivenByPartId;

	/**
	 * The part that has to be out of the way before this one may move. Empty for a part free to
	 * move whenever it likes.
	 *
	 * GEARING AND SEQUENCING ARE DIFFERENT RELATIONSHIPS, and DrivenByPartId above is the other one.
	 * A geared part copies its driver's open amount - it has no say. A sequenced part keeps its own
	 * open amount and is merely PREVENTED from running until the part named here is clear: it is an
	 * ordering, not a linkage.
	 *
	 * The case this exists for is a drawer inside a wardrobe. Its runners are screwed to the
	 * CARCASS, so it does not ride on the leaf and must not be parented to one - a drawer riding the
	 * leaf would swing out of the cabinet with it, which is a worse lie than the one being fixed.
	 * But it still cannot come out through a shut leaf. Open the shutter, then pull the drawer: an
	 * ordering between two independent parts, which is exactly what this expresses and what
	 * MasterOpenAmount needs in order to be a physically valid pose rather than a diagnostic.
	 *
	 * @see SequenceThreshold, AllowanceFrom, FHFArticulation::ResolvePartAmounts.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HouseForge")
	FName SequencedAfterPartId;

	/**
	 * How far open that part must be before this one may start to move at all.
	 *
	 * The composer's number, because only the composer knows the geometry: it is the shutter opening
	 * at which the leaf has swung clear of everything behind it, which depends on the leaf's
	 * thickness, its hinge position and how far the drawer front is inset. A fixture generator
	 * measures it once and states it here.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HouseForge",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	double SequenceThreshold = 0.5;

	/** True for anything that is not fixed, spinning parts included. */
	bool Moves() const { return Type != EHFMotionType::None; }

	/** True for the motions that go from shut to open and stop, which is what OpenAmount describes. */
	bool Opens() const { return Type == EHFMotionType::Hinge || Type == EHFMotionType::Slide; }

	/** True for a part whose pose is a phase rather than an amount: a fan. */
	bool Revolves() const { return Type == EHFMotionType::Spin; }

	/** Unit axis, falling back to +Z rather than producing a degenerate rotation. */
	FVector UnitAxis() const;

	/**
	 * The offset a given open amount applies, in the part's own local space.
	 *
	 * Identity at 0, so a closed part sits exactly where its generator put it and the closed pose
	 * of an assembly is bit-identical to the same assembly generated as one fixed mesh.
	 *
	 * Identity for a spinning part at any amount: a fan is not open, and its pose comes from
	 * SpinOffsetAt instead.
	 */
	FTransform OffsetAt(double OpenAmount) const;

	/**
	 * The offset a given phase applies, in the part's own local space.
	 *
	 * Turns are REVOLUTIONS, and deliberately unbounded - never clamped, never wrapped. 2.5 is two
	 * and a half turns, and -1 is one turn the other way. That is the whole difference between a
	 * part that revolves and one that opens: a fan asked to turn past 360 degrees does, and keeps
	 * going, where an open amount would have stopped at its limit.
	 *
	 * Identity for every motion that is not a spin, so a door cannot be turned by a stray phase.
	 */
	FTransform SpinOffsetAt(double Turns) const;

	/** Where a point given in the part's local space ends up at that open amount, still local. */
	FVector SweptLocalPoint(const FVector& LocalPoint, double OpenAmount) const;

	/** Revolutions turned in a span of seconds at the declared rate. Signed with it. */
	double TurnsInSeconds(double Seconds) const { return RevolutionsPerMinute * Seconds / 60.0; }

	/**
	 * How far this part may open, given how far the part it is sequenced after has.
	 *
	 * 1 - no restriction at all - for a part that declares no ordering, so an unsequenced assembly
	 * behaves exactly as it did before there was such a thing.
	 *
	 * Past the threshold it opens PROPORTIONALLY rather than being released all at once: 0 at the
	 * threshold, rising to 1 when the blocker is fully open. A gate would jump the drawer from shut
	 * to wherever the master amount had got to the instant the leaf cleared, which is a pop in a
	 * walkthrough and reads as a glitch rather than as a mechanism. Ramping it means one master
	 * track opens the leaf and then draws the drawer out behind it, which is the order somebody
	 * actually does it in.
	 */
	double AllowanceFrom(double BlockerOpenAmount) const;
};

/**
 * Live state of one moving part on an actor.
 *
 * Split from the generated description on purpose. Everything the generator decides - pivot,
 * motion, travel - is rebuilt on every regeneration, while OpenAmount and the hand-edited flag are
 * user state and survive it. A wardrobe whose shutters were left open to be photographed must not
 * snap shut because its carcass depth changed.
 */
USTRUCT(BlueprintType)
struct HOUSEFORGE_API FHFPartState
{
	GENERATED_BODY()

	/** Stable across regeneration; this is how a part is matched back to itself. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HouseForge")
	FName PartId;

	/** 0 fully closed, 1 at the travel limit. Meaningless on a spinning part, which has SpinTurns. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category = "HouseForge",
		meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	double OpenAmount = 0.0;

	/**
	 * Revolutions turned, for a part that spins. Unbounded, and deliberately not clamped.
	 *
	 * This is what lets a fan turn past 360 degrees rather than stopping at a limit: 3.75 is three
	 * and three quarter turns and keeps counting, where an open amount would have run out at 1.
	 *
	 * Interp, so a Sequencer track drives it. A linear ramp of turns is constant speed, which is all
	 * a fan animation is - 30 turns over 6 seconds is the 300 rpm on the fan's plate, and
	 * FHFPartMotion::TurnsInSeconds is where that conversion lives.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category = "HouseForge")
	double SpinTurns = 0.0;

	/**
	 * True once this part's mesh has been modified outside of generation.
	 *
	 * Per part rather than per actor: hand-detailing one shutter must not freeze the other five,
	 * and it must not be silently overwritten when they rebuild.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HouseForge")
	bool bArtistEdited = false;

	/** Closed pose, relative to the actor. Regenerated, shown so a pivot can be checked. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HouseForge")
	FTransform PivotTransform;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HouseForge")
	FHFPartMotion Motion;

	/** Relative transform this part's component should carry as it currently stands. */
	FTransform CurrentPose() const { return PoseAt(OpenAmount); }

	/**
	 * Relative transform this part's component would carry at a given open amount.
	 *
	 * A spinning part ignores the amount and uses its own SpinTurns, because it has no open amount
	 * to be given one - posing a fan means setting its phase.
	 */
	FTransform PoseAt(double InOpenAmount) const;
};

/**
 * How an articulated element was posed, lifted clear of the actor holding it.
 *
 * A house rebuild respawns its elements, so a pose held only on the actor dies with it and every
 * door in the flat slams shut. Posing is user state in exactly the way a hand edit is - someone
 * opened those doors on purpose, usually to photograph them - so it is captured by part id before
 * the rebuild and put back after it.
 */
struct HOUSEFORGE_API FHFPartPoses
{
	/** Only parts posed away from closed; a closed part has nothing worth carrying. */
	TMap<FName, double> OpenAmountsByPartId;

	/**
	 * Phases of the spinning parts, in revolutions.
	 *
	 * Carried separately because a phase is not an open amount and must not be clamped into one. A
	 * fan stopped with a blade in a particular place for a still is posed exactly as deliberately as
	 * an open wardrobe, and a rebuild that reset it to zero would jump every fan in the flat.
	 */
	TMap<FName, double> SpinTurnsByPartId;

	double MasterOpenAmount = 0.0;

	bool IsEmpty() const
	{
		return OpenAmountsByPartId.IsEmpty() && SpinTurnsByPartId.IsEmpty() && MasterOpenAmount <= 0.0;
	}
};

/**
 * One moving part as produced by generation: a mesh plus where it hangs and how it moves.
 *
 * A plain struct rather than a USTRUCT because it carries an FDynamicMesh3 by value. It is still
 * pure data with no world or asset access, so a generator can hand back a whole articulated kit
 * without ever touching an actor - articulation is the actor's job, but describing it is not.
 *
 * Mesh is in the part's own local space with the origin on the pivot.
 */
struct HOUSEFORGE_API FHFMeshPart
{
	/** Unique within one actor. Survives regeneration and is what open amounts key off. */
	FName PartId;

	/** The part's geometry, in part-local space. Roles and UVs already applied. */
	UE::Geometry::FDynamicMesh3 Mesh;

	/** Where the part's origin sits, relative to the actor, when closed. */
	FTransform PivotTransform = FTransform::Identity;

	FHFPartMotion Motion;

	/** Open amount a freshly generated part starts at. Almost always 0. */
	double DefaultOpenAmount = 0.0;

	/**
	 * Phase a freshly generated spinning part starts at, in revolutions.
	 *
	 * Worth having rather than always starting at zero: three ceiling fans generated identically and
	 * all stopped with a blade at the same angle read as three copies of one object, which is
	 * exactly what they are and exactly what a still should not show.
	 */
	double DefaultSpinTurns = 0.0;
};

/**
 * A part naming a dependency that is not in the assembly.
 *
 * Not an error the solve can refuse - a fixture still has to pose - but it is never harmless. The
 * part resolves completely UNCONSTRAINED, so a typo in a composer, or a shutter part renamed without
 * its drawer's ordering being renamed with it, silently turns the interlock off: the drawer comes
 * straight out through a shut leaf and every measurement of the pose agrees that it should have.
 *
 * So it is reported rather than dropped. The solve stays permissive and the caller says so out loud.
 */
struct HOUSEFORGE_API FHFUnresolvedDependency
{
	/** The part that declared it. */
	FName PartId;

	/** The id it named, which no part in the assembly carries. */
	FName MissingPartId;

	/** True when the missing id was DrivenByPartId; false when it was SequencedAfterPartId. */
	bool bGearing = false;
};

/**
 * Resolving a whole assembly's open amounts at once.
 *
 * The two relationships a part can declare - geared to another part, or sequenced after one - are
 * both about a part OTHER than itself, so neither can be settled by looking at one part in
 * isolation. Both are therefore resolved here, over the whole set, before anything is posed.
 *
 * Free of the actor on purpose. Composition tests build fixtures out of FHFMeshParts and never
 * spawn anything, and they have to be able to ask the same question the actor asks - "where does
 * this assembly actually stand at this master amount" - and get the same answer. A copy of these
 * rules living in the actor is a copy that drifts.
 */
struct HOUSEFORGE_API FHFArticulation
{
	/**
	 * Settles every part's open amount against the parts it depends on, in place.
	 *
	 * Each part's own OpenAmount is read as what was ASKED FOR, and what it is left holding is what
	 * it may actually have: a geared part takes its driver's amount, and a sequenced part is capped
	 * by how far the part blocking it has opened. The resolved amount is written back rather than
	 * kept alongside, so what the details panel shows is always where the part really is - a slider
	 * reading 1 on a drawer that is visibly shut is a bug report waiting to be filed.
	 *
	 * ITERATED TO A FIXED POINT, which is the part that matters. A single pass in array order
	 * resolves A -> B -> C only if the array happens to be in that order, and lags a call behind
	 * whenever it is not: the fault is invisible in a still, shows up in motion as a part trailing
	 * its driver by one frame, and depends on nothing more than the order a generator emitted its
	 * parts in. Passes stop as soon as nothing changes, so the ordinary case - a wardrobe with no
	 * dependencies at all - costs exactly one pass.
	 *
	 * A CYCLE IS REFUSED RATHER THAN ITERATED FOREVER. Parts that depend on each other, directly or
	 * round a longer loop, have no consistent answer; they keep the amounts they were asked for, and
	 * their names come back so the caller can say which fixture is at fault. The alternative is an
	 * editor that hangs on a generator bug.
	 *
	 * A DANGLING DEPENDENCY IS PERMITTED AND REPORTED. A part naming an id no part in the assembly
	 * carries resolves unconstrained, because freezing every drawer in a fixture over one bad name
	 * would be a second and more confusing failure than the first. But unconstrained is exactly the
	 * pre-interlock behaviour - the drawer goes straight through the shut leaf - so it comes back in
	 * OutUnresolved rather than being dropped on the floor. Silence here would let a renamed part
	 * revert the whole ordering guarantee with nothing in any log to say so.
	 *
	 * @param OutCyclicPartIds Optional: the parts found on a cycle, whose dependencies were ignored.
	 * @param OutUnresolved Optional: the parts whose declared dependency id is not in the assembly.
	 * @return false if a cycle was found, in which case the parts on it were left unresolved. A
	 *         dangling dependency does NOT make this false - the assembly still poses.
	 */
	static bool ResolvePartAmounts(TArrayView<FHFPartState> Parts, TArray<FName>* OutCyclicPartIds = nullptr,
		TArray<FHFUnresolvedDependency>* OutUnresolved = nullptr);
};
