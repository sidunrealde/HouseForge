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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "1", ClampMax = "6"))
	int32 FilterPanels = 3;

	/** How far the baffle filter drops when it is opened. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double FilterDropDegrees = 65.0;

	/** Overall height of what is actually built: the canopy plus whatever duct stands on it. */
	double BuiltHeight() const { return CanopyHeight + FMath::Max(DuctLength, 0.0); }

	bool IsValid() const { return Width > 0.0 && Depth > 0.0 && CanopyHeight > 0.0; }
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

	/** Glass, burners, grates, body, and a part per knob. */
	static FHFApplianceBuild BuildHob(const FHFHobParams& Params);

	/** Canopy, duct, and the baffle filter as its own part. */
	static FHFApplianceBuild BuildChimney(const FHFChimneyParams& Params);

	/** Part id of a hob knob, left to right. */
	static FName KnobPartId(int32 Index);

	/** Part id of the chimney's baffle filter. */
	static FName FilterPartId() { return TEXT("Filter"); }
};
