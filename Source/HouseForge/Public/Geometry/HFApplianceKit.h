// Copyright Siddartha G. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Model/HFArticulation.h"
#include "Model/HFTypes.h"
#include "HFApplianceKit.generated.h"

/**
 * A gas hob: a glass top set into the counter, burners on it, and knobs that turn.
 *
 * ## Frame
 *
 * Centimetres, origin at the CENTRE of the drawn footprint, and Z = 0 IS THE COUNTER TOP, exactly as
 * a sink's Z = 0 is its rim. The glass sits on the stone at Z = 0, the burners and their grates
 * stand above it, and the body drops through the cutout below.
 *
 * ## What moves
 *
 * The knobs, one per burner, each turning about its own axis. They are small and they are on the
 * front face where somebody walking past sees them, and a hob whose knobs are moulded into the
 * fascia is exactly the kind of thing that reads as a placeholder.
 */
USTRUCT(BlueprintType)
struct HOUSEFORGE_API FHFHobParams
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double Width = 58.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double Depth = 50.0;

	/** Toughened glass the burners are mounted through. 8 mm is what a hob is actually made of. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double GlassThickness = 0.8;

	/** How far the body hangs below the glass, through the counter's cutout. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double BodyDepth = 5.2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "1", ClampMax = "6"))
	int32 BurnerCount = 4;

	/** Height of the cast pan support over the glass. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double GrateHeight = 2.5;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double KnobRadius = 1.6;

	/** How far a knob turns from off to full. 270 is a gas tap's whole sweep. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double KnobSweepDegrees = 270.0;

	bool IsValid() const { return Width > 0.0 && Depth > 0.0 && GlassThickness > 0.0; }
};

/**
 * A cooker hood: a canopy over the hob and a duct running up into the ceiling.
 *
 * ## Frame
 *
 * Centimetres, origin at the front-left corner of the footprint at the underside of the canopy,
 * which is the same corner-and-base datum every wall-mounted run uses - see
 * FHFFixturePlacement::AgainstWall. +X along the wall, +Y back into the wall, +Z up.
 *
 * ## The duct is not decoration
 *
 * A chimney that stops at the top of its own canopy is a box on a wall. The duct has to reach the
 * SOFFIT, and the soffit is a false ceiling whose depth is a project setting - so DuctLength is
 * resolved by the composing layer against the actual ceiling over this chimney and handed in, in
 * exactly the way a ceiling fan's rod length is. It is the same failure in the same place: a fitting
 * sized from a fixed figure, standing in a room whose ceiling somebody moved.
 *
 * ## What moves
 *
 * The baffle filter, hinged along its lower front edge, dropping open for cleaning.
 */
USTRUCT(BlueprintType)
struct HOUSEFORGE_API FHFChimneyParams
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double Width = 60.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double Depth = 50.0;

	/** Height of the canopy itself, not counting the duct above it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double CanopyHeight = 70.0;

	/**
	 * How far the duct runs above the canopy, up to the finished soffit.
	 *
	 * Zero builds no duct at all, which is the honest answer for a chimney whose canopy already
	 * reaches the ceiling - and not a reason to build a stub of one.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double DuctLength = 0.0;

	/** Width of the rectangular duct casing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double DuctWidth = 24.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double DuctDepth = 20.0;

	/**
	 * How much of the canopy's height is the sloped taper up to the duct.
	 *
	 * A pyramidal hood is what a chimney IS. Built as a straight box it is a wall cupboard with a
	 * pipe on it, and no amount of correct dimensioning rescues that.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double TaperHeight = 40.0;

	/**
	 * How far the back of the canopy stands off the plaster, on its brackets.
	 *
	 * A cooker hood hangs on a wall bracket, so its back panel is NEAR the tiles rather than in the
	 * same plane as them. That distinction is a rendering fact, not a pedantry: with the back exactly
	 * coplanar with the wall, 1617 cm2 of the flat's kitchen z-fights - both faces are drawn, the depth
	 * test picks a different winner each frame, and the surface strobes as the camera moves.
	 * HouseForge.SampleHouse.NoTwoSurfacesShareAPlane measures exactly that and it is what caught this.
	 *
	 * It only appeared when FHFFixturePlacement::AgainstWall started putting a run's drawn back on the
	 * finished face instead of leaving it where the drawing put it; the drawing happened to carry a
	 * 10 mm gap here and the correction closed it. The wall owns the plane, so the fitting stands off.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double WallGap = 0.8;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "1", ClampMax = "6"))
	int32 FilterPanels = 3;

	/** How far the baffle filter drops when it is opened. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double FilterDropDegrees = 65.0;

	/** Overall height of what is actually built: the canopy plus whatever duct stands on it. */
	double BuiltHeight() const { return CanopyHeight + FMath::Max(DuctLength, 0.0); }

	/** The back of the hood in its own frame: the drawn depth, less the gap it hangs off the wall by. */
	double BackY() const { return FMath::Max(Depth - WallGap, 0.0); }

	bool IsValid() const { return Width > 0.0 && BackY() > 0.0 && CanopyHeight > 0.0; }
};

/**
 * A storage water heater: a horizontal pressure vessel on wall brackets, high up in a bathroom.
 *
 * ## Frame
 *
 * Centimetres, origin at the CENTRE of the drawn footprint at the BOTTOM of the drawn box, +Y back
 * towards the wall. The centre rather than a corner because a geyser is a bought cylinder hung on
 * two brackets and is set out from its own middle, not scribed to anything.
 *
 * ## Round, and it has to be
 *
 * A geyser is a welded steel cylinder inside a moulded shell, and the shell is the shape of the
 * cylinder. There is no version of this that reads correctly as a box: it is the one fitting in a
 * bathroom whose silhouette is entirely its curvature, it hangs at 2100 where it is seen against the
 * ceiling from every part of the room, and a rectangular one is the loudest possible statement that
 * nobody modelled it.
 *
 * The DIAMETER is derived rather than declared - the largest cylinder the drawn box can hold - so a
 * drawing that gave a 450 x 400 x 450 box gets a 400 vessel with its brackets in the difference,
 * which is what a 25 litre horizontal geyser actually is.
 *
 * ## What moves
 *
 * The thermostat dial on its end cap. Small, and a real control that a person turns.
 */
USTRUCT(BlueprintType)
struct HOUSEFORGE_API FHFGeyserParams
{
	GENERATED_BODY()

	/** Along the wall, which is the cylinder's own axis. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double Length = 45.0;

	/** Out from the wall. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double Depth = 40.0;

	/** Overall height of the drawn box, brackets included. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double Height = 45.0;

	/** Thickness of the plate the vessel is strapped to the wall on. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double BracketThickness = 1.6;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double DialRadius = 2.2;

	/** How far the thermostat turns from cold to hot. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double DialSweepDegrees = 270.0;

	/** Inlet and outlet, dropping out of the underside. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double PipeRadius = 0.85;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double PipeDrop = 9.0;

	/** The largest vessel the drawn box will hold, once the brackets have had their share. */
	double VesselDiameter() const
	{
		return FMath::Max(FMath::Min(Depth - BracketThickness, Height), 0.0);
	}

	bool IsValid() const { return Length > 0.0 && VesselDiameter() > 0.0; }
};

/**
 * A split air conditioner's indoor unit: a moulded casing hung high on a wall.
 *
 * ## Frame
 *
 * Centimetres, origin at the CENTRE of the drawn footprint in plan, Z = 0 at the BOTTOM of the drawn
 * box, +Y running BACK into the wall - the datum FHFFixturePlacement::OnWallFace places, and the same
 * one the mirror, the geyser and the switch plates use.
 *
 * ## IT IS NOT A BOX, AND THAT IS THE WHOLE FITTING
 *
 * A split head is 900 x 220 x 300 of moulded plastic whose entire reading is its SECTION: a flat back
 * on the plaster, a top that runs forward and domes over, a front that bulges and tucks under, and a
 * discharge channel cut into the underside with a vane lying in it. Built as a rectangular box it is a
 * shoebox screwed to a wall at head height in three rooms - the most conspicuous placeholder in the
 * flat, because it is above the furniture line where nothing else interrupts it.
 *
 * So the casing is an EXTRUDED SECTION swept along the wall, which is exactly what the object is: a
 * moulding of constant profile. The profile carries the discharge channel as a genuine concavity
 * rather than as a painted line, because the vane has to lie IN something.
 *
 * ## No top intake grille, said out loud
 *
 * A real one has slotted intake across the top. It is not built, and the reason is that this unit
 * hangs at 2200 and is seen from below by everybody in the room and by every camera: the top surface
 * is never in shot. Modelling it would spend triangles on the one face of the fitting nobody can look
 * at. What IS built is everything at and below the discharge, which is all that anybody sees.
 *
 * ## What moves
 *
 * The discharge vane, hinged on its rear axis so its tip drops down and back - which is the arc a real
 * one traces, and the reason an open louvre appears to withdraw as it falls. And the vertical
 * deflectors behind it, as ONE part, because a real set is ganged on a common linkage and swings
 * together.
 */
USTRUCT(BlueprintType)
struct HOUSEFORGE_API FHFSplitACParams
{
	GENERATED_BODY()

	/** Along the wall. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double Length = 90.0;

	/** Out from the wall. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double Depth = 22.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double Height = 30.0;

	/** How far the vane swings down from shut. 70 degrees is a split head's full sweep. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions",
		meta = (ClampMin = "0.0", ClampMax = "90.0"))
	double LouvreOpenDegrees = 70.0;

	/** How far the ganged vertical deflectors swing each way. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions",
		meta = (ClampMin = "0.0", ClampMax = "60.0"))
	double DeflectorSwingDegrees = 30.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions",
		meta = (ClampMin = "0", ClampMax = "24"))
	int32 DeflectorCount = 7;

	/**
	 * Overall height of what is actually built, which for this fitting is the drawn box.
	 *
	 * Supplied anyway, and that is the point of it: the casing is built INSIDE the drawn envelope by
	 * construction, and this is the statement of that fact in the one place FHFCeilingFit reads. A
	 * fitting whose built height silently exceeded its drawn one would be fitted under a soffit with
	 * the difference left inside the plasterboard - the exact defect the extract's bezel produced.
	 */
	double BuiltHeight() const { return Height; }

	bool IsValid() const { return Length > 0.0 && Depth > 0.0 && Height > 0.0; }
};

/**
 * A condensing unit: a louvred case on feet with a guarded axial fan.
 *
 * ## Frame
 *
 * Centimetres, origin at the FRONT-LEFT corner of the drawn footprint on the floor, +X along the run,
 * +Y back towards the wall, +Z up - the corner-and-base datum FHFFixturePlacement::AgainstWall places
 * and the one the chimney already uses. A condenser is pushed up near a wall rather than screwed to
 * it, so it keeps the gap the drawing gave it: the coil on its back rejects heat, and every one of
 * these ever installed has air behind it.
 *
 * ## What moves
 *
 * The fan, and it SPINS. Its collision is TraceOnly, exactly as a ceiling fan's rotor is, because
 * collision geometry does not turn with the render - see EHFPartCollision::TraceOnly for the whole
 * argument, which applies here word for word.
 */
USTRUCT(BlueprintType)
struct HOUSEFORGE_API FHFCondenserParams
{
	GENERATED_BODY()

	/** Along the wall. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double Width = 80.0;

	/** Out from the wall. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double Depth = 35.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double Height = 60.0;

	/** Height of the feet the case stands on, off the balcony floor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double FootHeight = 4.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "2", ClampMax = "8"))
	int32 BladeCount = 3;

	/** Horizontal slats over the coil on each side. Zero builds bare sides. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0", ClampMax = "40"))
	int32 CoilSlats = 11;

	/** How fast the fan turns. 850 rpm is a domestic condensing unit on its plate. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions")
	double FanRevolutionsPerMinute = 850.0;

	/** Radius of the fan aperture in the front panel, derived so a case of any size gets a real one. */
	double FanRadius() const
	{
		return FMath::Max(FMath::Min(Height - FootHeight, Width * 0.6) * 0.34, 0.0);
	}

	bool IsValid() const { return Width > 0.0 && Depth > 0.0 && Height > FootHeight; }
};

/**
 * A refrigerator: a cabinet on a plinth grille with a freezer over a fresh food compartment.
 *
 * ## Frame
 *
 * As the condenser - front-left corner of the footprint on the floor, +Y back towards the wall.
 * FHFFixturePlacement::AgainstWall.
 *
 * ## The setback is not optional
 *
 * A refrigerator is not scribed joinery, so the room's skirting board runs on BEHIND it, and the
 * appliance therefore has to stand in front of that board. Resolved by the composing layer from the
 * project's skirting depth and handed in - see FHFDeskParams::SupportSetback, which is the same figure
 * for the same reason, and which was added after a desk was found permanently 18 mm inside one.
 *
 * ## What moves
 *
 * Both doors. Hung on the SAME side, because that is what a two-door refrigerator is: you do not open
 * a freezer left-handed and a fridge right-handed on the same cabinet.
 */
USTRUCT(BlueprintType)
struct HOUSEFORGE_API FHFRefrigeratorParams
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double Width = 70.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double Depth = 70.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double Height = 180.0;

	/** A foam-filled appliance door. 65 mm is what one is. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double DoorThickness = 6.5;

	/** The vented plinth under the cabinet. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double PlinthHeight = 10.0;

	/** How much of the cabinet's height above the plinth the freezer takes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	double FreezerFraction = 0.34;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions",
		meta = (ClampMin = "0.0", ClampMax = "170.0"))
	double DoorSwingDegrees = 110.0;

	/**
	 * How far short of the drawn back plane the cabinet stops, leaving the skirting to run behind.
	 *
	 * Resolved by the composing layer, never read from a settings object here.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double SkirtingSetback = 0.0;

	/** How deep the cabinet actually is, once the skirting has had its share. */
	double BuiltDepth() const { return FMath::Max(Depth - SkirtingSetback, 0.0); }

	bool IsValid() const { return Width > 0.0 && BuiltDepth() > 0.0 && Height > PlinthHeight; }
};

/**
 * A front-loading washing machine: a porthole, a detergent drawer and a programme dial.
 *
 * Frame and setback as FHFRefrigeratorParams, and for the same reasons.
 *
 * ## What moves
 *
 * The porthole, the detergent drawer and the dial. All three are things a person operates, and the
 * dial is in the list because a programme selector turns - the same rule that gave the geyser its
 * thermostat rather than a moulded bump. See .claude/rules/04-conventions.md.
 */
USTRUCT(BlueprintType)
struct HOUSEFORGE_API FHFWashingMachineParams
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double Width = 60.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double Depth = 60.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double Height = 85.0;

	/** Diameter of the glass door. 320 mm is a domestic front loader's. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double PortholeDiameter = 32.0;

	/** Centre of the porthole above the machine's own base. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double PortholeCentreZ = 49.0;

	/** Height of the fascia carrying the drawer and the dial, measured down from the top. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double FasciaHeight = 14.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions",
		meta = (ClampMin = "0.0", ClampMax = "170.0"))
	double DoorSwingDegrees = 160.0;

	/** How far the detergent drawer pulls out. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double DrawerTravel = 13.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double DialRadius = 2.4;

	/** How far the programme selector turns end to end. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double DialSweepDegrees = 300.0;

	/** See FHFRefrigeratorParams::SkirtingSetback. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double SkirtingSetback = 0.0;

	double BuiltDepth() const { return FMath::Max(Depth - SkirtingSetback, 0.0); }

	bool IsValid() const { return Width > 0.0 && BuiltDepth() > 0.0 && Height > 0.0; }
};

/** A composed appliance. Plain data carrying meshes by value. */
struct HOUSEFORGE_API FHFApplianceBuild
{
	UE::Geometry::FDynamicMesh3 Shell;
	TArray<FHFMeshPart> Parts;
	bool bValid = false;
};

/**
 * Appliances: a box with a treated face, and whatever articulates on it.
 *
 * Pure - parameters in, meshes out, no world, no actor, no editor, no settings object. See
 * .claude/rules/04-conventions.md.
 */
class HOUSEFORGE_API FHFApplianceKit
{
public:
	static FHFHobParams SanitiseHob(const FHFHobParams& Params);
	static FHFChimneyParams SanitiseChimney(const FHFChimneyParams& Params);
	static FHFGeyserParams SanitiseGeyser(const FHFGeyserParams& Params);
	static FHFSplitACParams SanitiseSplitAC(const FHFSplitACParams& Params);
	static FHFCondenserParams SanitiseCondenser(const FHFCondenserParams& Params);
	static FHFRefrigeratorParams SanitiseRefrigerator(const FHFRefrigeratorParams& Params);
	static FHFWashingMachineParams SanitiseWashingMachine(const FHFWashingMachineParams& Params);

	/** Glass, burners, grates, body, and a part per knob. */
	static FHFApplianceBuild BuildHob(const FHFHobParams& Params);

	/** Canopy, duct, and the baffle filter as its own part. */
	static FHFApplianceBuild BuildChimney(const FHFChimneyParams& Params);

	/** Vessel, end caps, wall bracket, pipework, and the thermostat dial as its own part. */
	static FHFApplianceBuild BuildGeyser(const FHFGeyserParams& Params);

	/** Moulded casing, discharge channel, and the vane and deflectors that swing in it. */
	static FHFApplianceBuild BuildSplitAC(const FHFSplitACParams& Params);

	/** Louvred case, feet, fan guard, and the fan as its own spinning part. */
	static FHFApplianceBuild BuildCondenser(const FHFCondenserParams& Params);

	/** Cabinet, plinth grille, and a door per compartment. */
	static FHFApplianceBuild BuildRefrigerator(const FHFRefrigeratorParams& Params);

	/** Case, fascia, and the porthole, detergent drawer and dial as their own parts. */
	static FHFApplianceBuild BuildWashingMachine(const FHFWashingMachineParams& Params);

	/** Part id of a hob knob, left to right. */
	static FName KnobPartId(int32 Index);

	/** Part id of a refrigerator door: 0 is the freezer, 1 the fresh food compartment. */
	static FName FridgeDoorPartId(int32 Index);

	/** Part id of the chimney's baffle filter. */
	static FName FilterPartId() { return TEXT("Filter"); }

	/** Part id of the geyser's thermostat dial. */
	static FName ThermostatPartId() { return TEXT("Thermostat"); }

	/** Part id of the split AC's discharge vane. */
	static FName LouvrePartId() { return TEXT("Louvre"); }

	/**
	 * Part id of one of the split AC's vertical deflectors, left to right.
	 *
	 * ONE PART EACH, even though the set is ganged. Ganged means they turn together, not that they
	 * turn about a shared centre - a rigid rotation of the whole set swings the outer blades sideways
	 * out of the casing instead of turning them. See FHFApplianceKit::BuildSplitAC.
	 */
	static FName DeflectorPartId(int32 Index);

	/** Part id of the condensing unit's fan. */
	static FName CondenserFanPartId() { return TEXT("CondenserFan"); }

	/** Part id of the washing machine's porthole door. */
	static FName PortholePartId() { return TEXT("Porthole"); }

	/** Part id of the washing machine's detergent drawer. */
	static FName DetergentDrawerPartId() { return TEXT("DetergentDrawer"); }

	/** Part id of the washing machine's programme selector. */
	static FName ProgrammeDialPartId() { return TEXT("ProgrammeDial"); }
};
