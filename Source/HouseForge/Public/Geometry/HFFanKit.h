// Copyright Siddartha G. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Model/HFArticulation.h"
#include "Model/HFTypes.h"
#include "HFFanKit.generated.h"

/**
 * A fan: the one thing in a flat that revolves rather than opens.
 *
 * EHFMotionType::Spin existed for a whole milestone with nothing in production emitting one. The
 * mechanism was complete and tested - a phase that counts past a full turn, a rate in rpm, a pose
 * that survives a rebuild - and every fan in the reference flat was a line in the spec that became
 * no actor at all. Three ceiling fans and three exhaust fans, none of which turned, because none of
 * them existed. This kit is what makes the spin reachable from a drawing, the same way
 * FHFWardrobeKit made the joinery kit reachable from one.
 *
 * ## Frame
 *
 * Centimetres, in the fan's own local space. THE ORIGIN IS THE MOUNTING POINT - the spot on the
 * ceiling soffit or the wall face the fan is fixed to - and +Z IS THE SPIN AXIS, pointing away from
 * that surface and into the room. Everything the fan is therefore lies at Z >= 0.
 *
 * One rule for both kinds, which is worth stating because the two hang differently in the world: a
 * ceiling fan's +Z points DOWN and a wall extract's points horizontally into the room. Aiming it is
 * the caller's job, and AHFFanActor::PlacementFor is where that happens. Nothing in here knows which
 * way up the fan ends up, which is what keeps this a pure function of its parameters.
 *
 * ## What moves
 *
 * One part, called Rotor, with EHFMotionType::Spin about local +Z. The blades and the housing they
 * are bolted to are on it; the canopy, the down rod and the case are not, because none of those
 * turn. A fan merged into one mesh would be a fan that cannot run - see
 * .claude/rules/04-conventions.md, which is not negotiable about this.
 */

/** Which fan. The two are built differently enough that a single parameterisation would lie. */
UENUM(BlueprintType)
enum class EHFFanKind : uint8
{
	/** Hangs from the slab on a down rod: the ceiling fan of every Indian room. */
	Ceiling,

	/** Sits in a wall or a window, blowing out: a bathroom, kitchen or utility extract. */
	Exhaust
};

/**
 * A fan, described.
 *
 * Dimensions are what this particular fan measures - off the drawing - and the rest are figures the
 * project sets once. FHFBuildDefaults::ApplyTo fills the figures in; nothing here reads a settings
 * object, per .claude/rules/04-conventions.md.
 */
USTRUCT(BlueprintType)
struct HOUSEFORGE_API FHFFanParams
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions")
	EHFFanKind Kind = EHFFanKind::Ceiling;

	/**
	 * Diameter the blades sweep. 120 is the standard Indian ceiling fan; a bathroom extract is 25.
	 *
	 * The figure a drawing actually carries, which is why it is the sweep rather than a blade length:
	 * a fan is specified and bought by its sweep.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double SweepDiameter = 120.0;

	/**
	 * Ceiling fan only: the MOUNTING SURFACE to the top of the motor, along the axis.
	 *
	 * Measured from where the fan is fixed, which for a ceiling fan is the structural slab - NOT from
	 * the canopy, which may sit some way down the rod. See CanopyDrop.
	 *
	 * What sets how far the blades hang below the slab, and it is a real decision rather than a
	 * constant: a room with a false ceiling in it needs the rod long enough to clear the soffit, and
	 * a fixed project figure built the entire rotor inside the plasterboard of every full-drop room.
	 * The composing layer resolves it - the room's ceiling is not something a generator may read.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double DropLength = 30.0;

	/**
	 * Ceiling fan only: how far below the mounting surface the canopy sits. 0 hangs it at the slab.
	 *
	 * THE THICKNESS OF WHAT THE ROD PASSES THROUGH. A fan hangs from the structural slab, so in a
	 * room with a false ceiling the rod goes through the void and through a hole cut in the panel,
	 * and the canopy belongs at the SOFFIT covering that hole - not up at the slab where nothing can
	 * see it and the hole is left showing its four corners.
	 *
	 * A dimension, not a figure: it is the drop of the particular ceiling over this particular fan,
	 * which is why FHFGenerators::CeilingSoffitDropAt answers it and the composing layer puts the
	 * answer here. Zero for a fan under an open slab or in the open centre of a peripheral or cove
	 * ceiling, which is every fan in the reference flat.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double CanopyDrop = 0.0;

	/** Exhaust fan only: how far the case stands proud of the wall it is set into. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double CaseDepth = 12.0;

	/**
	 * Exhaust fan only: thickness of the wall it discharges through. 0 builds nothing behind it.
	 *
	 * WHAT THE FAR SIDE OF THE HOLE LOOKS LIKE. The duct is cored through the masonry and then, with
	 * no discharge-side treatment, left as a bare square opening in a finished wall - the only
	 * opening in the flat with no lining, where every door and window gets a frame. From the corridor
	 * F_CBath_Exhaust read as a raw 15 cm hole at head height with the impeller visible inside it and
	 * the blade tips clipped by the masonry; on an external wall the same hole opens straight to the
	 * sky. A real extract has a sleeve through the wall and a louvred cowl on the discharge face.
	 *
	 * A DIMENSION, not a figure, and the wall's - which is why it is passed in rather than read.
	 * A generator that reached for the wall it stands in would stop being a pure function of its
	 * parameters, so the composing layer resolves it here.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double HostWallThickness = 0.0;

	/** Blades on the rotor. Three on a ceiling fan, five or six on an extract. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Blades", meta = (ClampMin = "2", ClampMax = "12"))
	int32 BladeCount = 3;

	/**
	 * Angle the blade is set at, in degrees. A MAGNITUDE, never zero, and never signed.
	 *
	 * A fan blade is pitched because that is what moves air, and a flat one is instantly readable as
	 * wrong: it catches the light as a uniform strip where a real blade has a bright edge and a dark
	 * one. This is a lighting decision as much as a mechanical one - see the quality bar in
	 * .claude/rules/04-conventions.md.
	 *
	 * WHICH WAY THE BLADE IS SET IS NOT STATED HERE. The direction of airflow is the product of the
	 * pitch handedness and the direction of rotation, and only one of those two is a free choice.
	 * See AirflowSign: a ceiling fan drives air into the room and an extract draws it out, which is
	 * what those two things ARE rather than something a drawing gets to vary. Leaving the sign on
	 * this figure would have let a settings page build an extract that blew into the bathroom.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Blades",
		meta = (ClampMin = "0.0", ClampMax = "45.0"))
	double BladePitchDegrees = 12.0;

	/** Chord of the blade - its width across the airflow. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Blades", meta = (ClampMin = "0.0"))
	double BladeChord = 11.0;

	/** Blade stock thickness. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Blades", meta = (ClampMin = "0.0"))
	double BladeThickness = 0.5;

	/**
	 * Chamfer on the blade's long edges.
	 *
	 * Small, and not decoration. A mathematically sharp edge reads as CG under any lighting, and a
	 * ceiling fan is the one object in a room that is both at eye level from a sofa and moving.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Blades", meta = (ClampMin = "0.0"))
	double BladeEdgeBevel = 0.15;

	/** Diameter of the motor housing, or of the extract's hub. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Motor", meta = (ClampMin = "0.0"))
	double MotorDiameter = 15.0;

	/** Height of the motor housing along the axis. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Motor", meta = (ClampMin = "0.0"))
	double MotorHeight = 12.0;

	/** Down rod diameter, and the canopy is set out from it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Motor", meta = (ClampMin = "0.0"))
	double RodDiameter = 2.2;

	/**
	 * How fast it turns, in revolutions per minute. Signed: the sign is the direction.
	 *
	 * 300 is a ceiling fan on speed 5 and 1350 an extract, both off a real fan's plate. It goes
	 * straight onto the part's FHFPartMotion, which is what turns elapsed time into revolutions for
	 * a Sequencer track or a walkthrough pawn.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Motor")
	double RevolutionsPerMinute = 300.0;

	/**
	 * Where this fan's blades are stopped, in revolutions, as generated.
	 *
	 * Not cosmetic. Three ceiling fans built from identical parameters and all stopped with a blade
	 * at the same angle read as three copies of one object, which is what they are and what a still
	 * must not show. The generator cannot vary it - it is pure, and knows nothing about how many fans
	 * there are - so the composing layer sets it per instance and it lands on
	 * FHFMeshPart::DefaultSpinTurns from here.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Motor")
	double PhaseTurns = 0.0;

	/**
	 * Which way this fan drives air along its own axis when it turns at a POSITIVE rpm.
	 *
	 * +1 along local +Z, which is away from the mounting surface and into the room; -1 the other way,
	 * out through the surface it is fixed to.
	 *
	 * Derived from the kind rather than stated, because it is not a free choice. A ceiling fan that
	 * blew at the slab and an extract that blew into the bathroom are not two settings of one object;
	 * they are the two things being wrong. What IS free is the direction of rotation - a real fan
	 * reverses for winter by running its motor backwards, and that is exactly what a negative
	 * RevolutionsPerMinute expresses here, with the blades unchanged as they are on a real fan.
	 *
	 * Read by the generator to decide which way to set the blade. See MakeBlade for the derivation of
	 * the sign; the short version is the rule of thumb every fan is checked against - in downdraft
	 * the LEADING EDGE OF THE BLADE IS THE HIGHER ONE.
	 */
	double AirflowSign() const { return Kind == EHFFanKind::Ceiling ? 1.0 : -1.0; }

	/** How far the blades sit below the top of the motor housing, as a share of its height. */
	double BladePlaneFraction() const { return 0.65; }

	/** Radius the blades reach. */
	double SweepRadius() const { return FMath::Max(SweepDiameter, 0.0) * 0.5; }

	/**
	 * Ceiling fan only: half the side of the square hole a false ceiling must cut for this fan's rod.
	 *
	 * DERIVED FROM THE FAN, exactly as the extract's duct is, and for the same reason: a plan marks a
	 * fan, never the hole cut for it, and a hole sized independently is a hole that drifts. It was a
	 * project constant of 8 - a 16 cm square opening for a 2.2 cm rod - whose corners showed past the
	 * 15 cm motor housing as four bright wedges.
	 *
	 * Big enough that a rod passes through it with room to hang plumb, and small enough that
	 * CanopyRadius covers it. The composing layer reads this onto AHFCeilingActor::FanDropRadius.
	 */
	double RodHoleHalfSide() const;

	/**
	 * Radius of the canopy: the plate that covers the rose, or the hole in a false ceiling.
	 *
	 * Sized to cover RodHoleHalfSide's square INCLUDING ITS CORNERS - a square reaches its
	 * half-diagonal, and the corners are what actually show - so a canopy at a soffit always hides
	 * the opening it sits over.
	 */
	double CanopyRadius() const;

	/** Exhaust fan only: half the width of the square case that stands proud of the wall. */
	double CaseHalfWidth() const;

	/** Exhaust fan only: radius of the aperture through the case, which the blades turn inside. */
	double ThroatRadius() const;

	/**
	 * Exhaust fan only: the side of the square hole cored through the wall behind it.
	 *
	 * An extract has to blow THROUGH the wall it is screwed to, and the fan's own case having an
	 * aperture is not the same thing as the masonry having one. Every extract in the reference flat
	 * was bolted to a solid wall and discharging into it, which is invisible in plan and invisible in
	 * any still of the room, because the case covers the spot where the hole is not.
	 *
	 * A square, because a wall opening is rectangular, and smaller than the fan - which is what a
	 * real installation is: a cored duct of 150 behind a 250 fan. Inscribed in the case with a margin
	 * so the case flange always hides it, since a hole peeking out from behind its own fan reads as a
	 * modelling mistake; and never wider than the blades, because a duct wider than the impeller is
	 * not a duct this fan is moving air through.
	 */
	double DuctSide() const;

	/** Overall depth from the mounting surface: rod plus motor, or the case. */
	double OverallDepth() const;

	/** False when the parameters describe nothing that can be built. */
	bool IsValid() const;
};

/**
 * A composed fan: the fixed part and the rotor.
 *
 * A plain struct rather than a USTRUCT because it carries meshes by value, exactly as
 * FHFWardrobeBuild does.
 */
struct HOUSEFORGE_API FHFFanBuild
{
	/** Canopy, down rod and mounting bracket, or the extract's case. What BuildMesh returns. */
	UE::Geometry::FDynamicMesh3 Shell;

	/** Exactly one: the rotor, spinning about local +Z. */
	TArray<FHFMeshPart> Parts;

	/** The parameters actually used, after clamping. */
	FHFFanParams Used;

	/** Distance from the mounting surface to the blade plane, along the axis. */
	double BladePlaneZ = 0.0;

	bool bValid = false;
};

/**
 * Composing a fan.
 *
 * Pure, like every other generator here: parameters in, meshes and a part out, no world, no actor,
 * no editor, no asset loading and no settings object.
 */
class HOUSEFORGE_API FHFFanKit
{
public:
	/** The parameters actually used, clamped so the fan they describe can be built. */
	static FHFFanParams Sanitise(const FHFFanParams& Params);

	/** The whole fan: a fixed shell and one spinning rotor. */
	static FHFFanBuild Build(const FHFFanParams& Params);

	/** The one moving part. Stable, because an open amount and a phase key off it across rebuilds. */
	static const FName RotorPartId;

	/** Sensible figures for a kind of fan, before a drawing's own dimensions go on. */
	static FHFFanParams DefaultsFor(EHFFanKind Kind);
};
