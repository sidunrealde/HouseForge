// Copyright Siddartha G. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Model/HFTypes.h"
#include "HFLuminaireKit.generated.h"

/**
 * A luminaire: the fitting an EHFFixtureType::LightFixture is, and the thing its light comes out of.
 *
 * ## Why this exists at all
 *
 * EHFFixtureType::LightFixture has been in the enum since the beginning. FHFCeilingFit::RuleFor
 * answers for it (HangsFromSoffit), FHFFixture::IsCeilingMounted answers for it, the validator lets
 * it through - and AHFHouseActor's recipe table has no row for it, so every light fixture in every
 * spec became nothing at all. A room whose drawing marks a pendant over the dining table got a
 * dining table and no pendant, and nothing in the build report said so.
 *
 * A light with no fitting would be worse than no light. A point of illumination hanging in mid-air
 * under a bare soffit reads as a rendering fault in every still, and the first thing anybody would
 * do is go looking for the mesh that failed to generate. So the fitting is modelled, and the light
 * is put where its lens is.
 *
 * ## Frame
 *
 * Centimetres, in the luminaire's own local space, and the SAME convention FHFFanKit uses so that
 * the two ceiling-mounted things in this plugin are not set out two different ways: THE ORIGIN IS
 * THE MOUNTING POINT - the spot on the finished soffit or the wall face the fitting is screwed to -
 * and +Z POINTS AWAY FROM THAT SURFACE, into the room. Everything the fitting is lies at Z >= 0.
 *
 * Nothing here knows which way up it ends up in the world. A ceiling fitting's local +Z points
 * straight down and a wall bracket's points horizontally; aiming it is AHFLightFixtureActor's job,
 * which is what keeps this a pure function of its parameters (.claude/rules/04-conventions.md).
 *
 * ## One construction, not a catalogue of kinds
 *
 * There is no EHFLuminaireKind, deliberately. A surface-mounted LED panel and a pendant are the
 * same object with and without a drop: canopy, flex, body, lens. Splitting them into two enum
 * branches would be two constructions that had to agree about where the lens is, and the lens
 * position is the one figure the LIGHT is placed from - see LensCentre, which exists precisely so
 * the actor does not derive it a second time and drift.
 *
 *     DropLength == 0  ->  a drum screwed flat to the surface: the LED panel of every Indian room
 *     DropLength >  0  ->  a canopy, a flex, and the drum on the end of it: a pendant
 *
 * ## Why the lens is a separate solid that pokes out of the body
 *
 * The body is a closed revolve with a flat cap facing the room, and the lens is a shallow dome
 * whose base is narrower than that cap and whose top is buried inside it. So what is seen from the
 * room is a rim of body around a diffuser standing slightly proud of it, which is what a real
 * fitting looks like - and, the reason it is built this way rather than as a disc lying on the cap,
 * NO TWO FACES ARE COPLANAR. A lens flush on the cap is two surfaces at the same Z fighting for
 * every pixel of the brightest object in the room. FHFCoplanarScan exists because this plugin has
 * made that mistake before.
 *
 * The lens carries EHFSurfaceRole::LightSource, so the material library gives it the emissive it
 * gives a cove strip and the fitting reads as lit in a still. The emissive is not the light:
 * AHFLightFixtureActor puts a real one at the same coordinates, for the same reason
 * AHFCeilingActor does.
 */
USTRUCT(BlueprintType)
struct HOUSEFORGE_API FHFLuminaireParams
{
	GENERATED_BODY()

	/** Diameter of the body drum - the visible size of the fitting. A 15 W LED panel is about 22. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double Diameter = 22.0;

	/** Depth of the body along the axis. A flat panel is 4; a drum pendant is 15. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double BodyHeight = 4.5;

	/**
	 * Mounting surface to the TOP of the body. Zero screws the body flat to the surface.
	 *
	 * Non-zero is what makes the fitting a pendant, and it is the whole difference between the two
	 * things this kit builds. Measured from the surface rather than to the lens, because that is
	 * the figure a drawing carries: a pendant is hung at a drop.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double DropLength = 0.0;

	/** Diameter of the canopy covering the outlet box. Only built when there is a drop. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double CanopyDiameter = 9.0;

	/** Depth of the canopy along the axis. Only built when there is a drop. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double CanopyHeight = 2.4;

	/** Diameter of the flex or stem the body hangs on. Only built when there is a drop. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double FlexDiameter = 1.2;

	/**
	 * How far the diffuser stands proud of the body rim. Small - a few millimetres.
	 *
	 * Not zero, and not free to become zero: at zero the dome degenerates to a disc lying in the
	 * plane of the rim, which is the coplanar pair the whole construction is arranged to avoid.
	 * Sanitise floors it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double LensProud = 0.35;

	/** Lens diameter as a fraction of the room-facing rim. The rest of the rim is the bezel. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.1", ClampMax = "1.0"))
	double LensFraction = 0.82;

	/** Sides on every revolve. Rounded up to a multiple of four by AppendRevolvedProfile. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Detail", meta = (ClampMin = "8", ClampMax = "64"))
	int32 SideCount = 24;

	/** True when the fitting hangs on a flex rather than sitting on the surface. */
	bool HasDrop() const { return DropLength > 0.0; }

	/** Local Z of the room-facing rim of the body: the deepest solid part of the fitting. */
	double RimZ() const { return DropLength + BodyHeight; }
};

/**
 * The pure generator. No world, no actor, no assets - see .claude/rules/04-conventions.md.
 */
class HOUSEFORGE_API FHFLuminaireKit
{
public:
	/** Clamps every figure into a range that can actually be built. Idempotent. */
	static FHFLuminaireParams Sanitise(const FHFLuminaireParams& In);

	/** The whole fitting: canopy, flex, body and lens, roles tagged, in the frame described above. */
	static UE::Geometry::FDynamicMesh3 Build(const FHFLuminaireParams& Params);

	/**
	 * Where the light belongs, in the luminaire's own frame: on the axis, at the face of the lens.
	 *
	 * PUBLISHED SO IT IS DERIVED ONCE. The actor has to put a real light at the emitting surface,
	 * and the emitting surface is a consequence of the drop, the body depth and how far the dome
	 * stands proud. Working that out a second time in the actor is exactly the drift FHFFanKit's
	 * own published figures were introduced to stop.
	 */
	static FVector3d LensCentre(const FHFLuminaireParams& Params);

	/** Radius of the lens dome base. What a light SourceRadius should be, so the shadow softens right. */
	static double LensRadius(const FHFLuminaireParams& Params);

	/** How far the fitting reaches along its axis, from the mounting surface to the lens face. */
	static double AxisExtent(const FHFLuminaireParams& Params);
};
