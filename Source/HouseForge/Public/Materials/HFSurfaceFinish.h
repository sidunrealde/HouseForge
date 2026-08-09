// Copyright Siddartha G. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/Texture.h"
#include "Materials/MaterialInterface.h"
#include "HFSurfaceFinish.generated.h"

/**
 * Which master a finish instances, and therefore which half of the parameter set means anything.
 *
 * Not a cosmetic distinction: a transmissive surface needs a blend mode, an index of refraction and
 * a path length, and an opaque one needs a clear coat lobe. Neither master carries the other's
 * parameters, so writing a coat weight onto a glazed instance would leave a stale override the
 * parent has no pin for. This enum is what stops that being possible.
 */
UENUM(BlueprintType)
enum class EHFFinishShading : uint8
{
	/** Everything that is not a window pane. Clear coat over a diffuse or metallic base. */
	Opaque,

	/** Glazing: translucent, refracting, and absorbing across its own thickness. */
	Glazed
};

/**
 * WHAT ONE SURFACE IS: the full specification of a finish, in the units a person would state it in.
 *
 * One of these per EHFSurfaceRole, held by UHFMaterialLibrary. Every field maps to exactly one
 * parameter on M_HF_Surface or M_HF_SurfaceGlazed, and the library writes the whole struct onto the
 * role's material instance in one pass - so this struct, not the instance, is where a finish is
 * decided. The instance is the library compiled for the renderer.
 *
 *
 * TILING IS IN MILLIMETRES, AND THAT IS ONLY POSSIBLE BECAUSE UV0 IS WORLD-SCALE
 * ------------------------------------------------------------------------------
 * FHFMeshOps::ApplyWorldScaleUVs unwraps so that one UV unit is exactly FHFRenderFinish::TexelSizeCm
 * of surface along every edge of every triangle. That is what lets a tile be stated as "600" and
 * mean six hundred millimetres of floor rather than a repeat count that means nothing off the
 * drawing board:
 *
 *     repeats per UV unit = UVWorldSizeCm * 10 / TilingMM
 *
 * See UVRepeatsPerUnit, which is that formula and the only place it is written on this side of the
 * boundary. This project converts millimetres to centimetres exactly once, at spec ingest, and has
 * been bitten at that boundary - so the conversion is spelled out at the site it happens rather than
 * folded into a constant, here and in the material graph both.
 *
 *
 * COLOURS ARE LINEAR
 * ------------------
 * Unreal's vector parameters are linear and the details-panel colour picker converts for display.
 * The default table states its colours in sRGB, because that is what a paint chart and a tile
 * catalogue are quoted in, and converts once through FromSRGB. Do not paste an sRGB triple straight
 * into BaseColor: a mid-grey typed as linear renders about two stops too dark and looks merely
 * "a bit off" rather than obviously wrong, which is the worst kind of mistake to make here.
 *
 *
 * THE TEXTURE SLOTS ARE EMPTY ON PURPOSE
 * --------------------------------------
 * There is no texture library. The plugin ships without one, so the five map slots below are the
 * hook rather than the content: assign one and its UseXMap switch turns on, leave it null and the
 * sampler is compiled out of the shader entirely. What ships instead is a correct BRDF - a clear
 * coat where the real object has a glaze - plus analytic pattern where the real pattern is analytic
 * (a grout grid IS a grid) and statistical noise where the real surface is statistical (plaster
 * tooth, granite speckle, roller-sheen drift). What that cannot be is a specific real product;
 * albedo with authored structure - veneer figure, a tile's face patterning, a fabric print - is
 * exactly what a real library would add, and plugging one in later is filling in these slots.
 */
USTRUCT(BlueprintType)
struct HOUSEFORGE_API FHFSurfaceFinish
{
	GENERATED_BODY()

	/**
	 * What this surface IS, in a sentence, in the terms a site engineer would use.
	 *
	 * A field rather than a code comment because it is the only thing on this struct that tells a
	 * user why the numbers are what they are - "double-charge vitrified tile laid with 2 mm spacers"
	 * explains a 600 and a 2.0 that would otherwise look arbitrary. Empty is a defect, and
	 * HouseForge.Materials.EveryRoleHasALibraryFinish says so.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "00 What This Is",
		meta = (MultiLine = "true"))
	FString Description;

	/** Which master this finish instances. Decides whether the coat or the glazing block applies. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "00 What This Is")
	EHFFinishShading Shading = EHFFinishShading::Opaque;

	// ============================================================================== 01 surface

	/** Diffuse albedo, LINEAR. Under Lumen this is what every bounce in the room is tinted by. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "01 Surface")
	FLinearColor BaseColor = FLinearColor(0.5f, 0.5f, 0.5f, 1.0f);

	/** Microfacet roughness of the base lobe. Matt emulsion 0.85, polished tile body 0.35. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "01 Surface",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Roughness = 0.5f;

	/** 0 for every dielectric. 1 for a real metal, and nothing in between is a physical surface. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "01 Surface",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Metallic = 0.0f;

	/** Dielectric F0, as Unreal parameterises it. 0.5 is the 4% every ordinary material has. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "01 Surface",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Specular = 0.5f;

	/**
	 * Weight of the clear coat lobe. THE SINGLE BIGGEST PHOTOREAL WIN AVAILABLE WITHOUT ASSETS.
	 *
	 * A glazed vitrified tile, a high-gloss acrylic shutter and a vitreous-china WC are all a
	 * pigmented, rough-ish body under a thin smooth clear layer. That is two lobes. Modelled as one
	 * low roughness number instead, all three read as painted metal.
	 *
	 * Opaque finishes only; zero costs nothing.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "01 Surface",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float CoatWeight = 0.0f;

	/** Roughness of the coat itself. Near-zero: a glaze is smooth even when its body is not. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "01 Surface",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float CoatRoughness = 0.06f;

	// =============================================================================== 02 tiling

	/**
	 * World size of one repeat of the pattern, in MILLIMETRES.
	 *
	 * A tile module, a laminate sheet, a weave pitch. Means a real length because UV0 is world-scale;
	 * see the class comment and UVRepeatsPerUnit.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "02 Tiling",
		meta = (ClampMin = "1.0", UIMin = "50.0", UIMax = "2000.0"))
	float TilingMM = 1000.0f;

	/** Rotation of the pattern about the surface, in degrees. A tile laid on the diagonal is 45. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "02 Tiling",
		meta = (ClampMin = "-180.0", ClampMax = "180.0"))
	float TilingRotationDegrees = 0.0f;

	/**
	 * Slide of the pattern, in WHOLE TILES rather than in UV units.
	 *
	 * Deliberate: the useful thing to do with this is put a whole tile in the room's corner, and in
	 * tiles that reads as "a third of a tile" rather than as a number whose meaning changes every
	 * time the tiling does.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "02 Tiling")
	FVector2D TilingOffsetTiles = FVector2D::ZeroVector;

	/**
	 * Project from world position instead of from the mesh's own UV0.
	 *
	 * Off everywhere by default, because UV0 is unwrapped to world scale already and an unwrap
	 * follows the surface round a curve where a world projection cannot. A STATIC SWITCH: changing
	 * it compiles a shader, so it is a considered choice and never a drag.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "02 Tiling")
	bool bWorldAlignedProjection = false;

	// ========================================================================= 03 texture maps

	/** Albedo map, multiplied by BaseColor. Null compiles the sampler out. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "03 Texture Maps")
	TSoftObjectPtr<UTexture> AlbedoMap;

	/** Roughness map, multiplied by Roughness. Null compiles the sampler out. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "03 Texture Maps")
	TSoftObjectPtr<UTexture> RoughnessMap;

	/** Metallic map, multiplied by Metallic. Null compiles the sampler out. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "03 Texture Maps")
	TSoftObjectPtr<UTexture> MetallicMap;

	/** Ambient occlusion map. Null compiles the sampler out. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "03 Texture Maps")
	TSoftObjectPtr<UTexture> AOMap;

	/**
	 * Tangent-space normal map. Null compiles the sampler out AND leaves the procedural bump in.
	 *
	 * The two are alternatives rather than a blend: combining two normals correctly needs a
	 * reorientation, and someone who supplied a real map does not want filtered noise added under it
	 * - their map already carries the surface's own tooth.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "03 Texture Maps")
	TSoftObjectPtr<UTexture> NormalMap;

	/** How far NormalMap is taken towards flat. Only read when a normal map is assigned. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "03 Texture Maps",
		meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float NormalMapStrength = 1.0f;

	// ==================================================================== 04 procedural detail

	/**
	 * Low-frequency roughness drift. IF ONLY ONE OF THESE SHIPS, IT IS THIS ONE.
	 *
	 * A four-metre wall at perfectly uniform roughness is the strongest CG tell in the whole render
	 * - stronger than the missing albedo map, because it is a property of the sheen rather than of
	 * the colour and the eye reads it at every angle.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "04 Procedural Detail",
		meta = (ClampMin = "0.0", ClampMax = "0.3"))
	float MacroRoughnessAmount = 0.03f;

	/** Low-frequency albedo drift, as a fraction. Small: this is shade batching, not a pattern. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "04 Procedural Detail",
		meta = (ClampMin = "0.0", ClampMax = "0.5"))
	float MacroAlbedoAmount = 0.010f;

	/** Wavelength of that drift, in millimetres. Metres for a wall; millimetres for granite speckle. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "04 Procedural Detail",
		meta = (ClampMin = "1.0"))
	float MacroVariationMM = 1500.0f;

	/**
	 * TYPICAL slope of the procedural bump, as a tangent. NOT AN AMPLITUDE, AND NOT THE STEEPEST.
	 *
	 * It is a slope because that is the only thing a normal perturbation can honestly be: 0.02 means
	 * the surface tilts about a degree on average, whatever the feature size. Read as an amplitude it
	 * was once about a hundred times too strong, which was invisible at a 2 mm plaster tooth and
	 * turned every 30-60 mm orange-peel surface in the flat into hammered metal.
	 *
	 * "On average" is the part that took a second correction. This said STEEPEST until the noise
	 * field's gradient was actually measured, and that field is not normalised - its rms gradient is
	 * 1.6 and its peak 8.4, so the delivered slope was up to eight times the number quoted here and
	 * Fabric read as popcorn. It is now divided by the measured rms, so this really is the typical
	 * slope, with peaks about five times steeper as any rough surface has. See
	 * Scripts/measure_noise_gradient.py, which is where the constant came from.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "04 Procedural Detail",
		meta = (ClampMin = "0.0", ClampMax = "0.5"))
	float DetailBumpStrength = 0.0f;

	/** Feature size of that bump, in millimetres. Plaster tooth 2, powder-coat orange peel 30. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "04 Procedural Detail",
		meta = (ClampMin = "0.1"))
	float DetailBumpMM = 2.0f;

	/**
	 * Brightness of an ADDED mineral fleck, in linear albedo. Zero on every role but the stones.
	 *
	 * SEPARATE FROM MacroAlbedoAmount BECAUSE ADDING AND MULTIPLYING ARE NOT THE SAME PICTURE.
	 * MacroAlbedoAmount scales what is already there, which is exactly right for the roller-sheen
	 * drift on a pale wall. A fleck of bronzite in Black Galaxy is not a percentage of the black
	 * around it - it is a bright grain sitting on it. CounterStone's base is 0.0222 linear, so the
	 * 11% drift it used to carry came to +/-0.0024 and the granite rendered as flat grey metal.
	 *
	 * 0.06 puts a fleck about three times the brightness of the ground on roughly a tenth of the
	 * area, which is what a polished speckled granite looks like at arm's length.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "04 Procedural Detail",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float SpeckleAmount = 0.0f;

	/** Grain size of that fleck, in millimetres. Granite reads at 4-8; a quartz composite finer. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "04 Procedural Detail",
		meta = (ClampMin = "0.1"))
	float SpeckleSizeMM = 6.0f;

	/** Colour of the fleck, LINEAR. Warm off-white for feldspar and mica; grey for a quartz chip. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "04 Procedural Detail")
	FLinearColor SpeckleColor = FLinearColor(1.0f, 0.95f, 0.85f, 1.0f);

	/**
	 * Compile the procedural bump out entirely rather than multiply it by a zero.
	 *
	 * A STATIC SWITCH, and worth its own field: with it on and DetailBumpStrength at zero the noise
	 * is still evaluated and thrown away. Off for the four roles that want no relief at all.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "04 Procedural Detail")
	bool bUseProceduralBump = true;

	// ================================================================================ 05 grout

	/** Joint width, in millimetres. Zero draws no grid at all, which is most roles. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "05 Grout",
		meta = (ClampMin = "0.0", ClampMax = "30.0"))
	float GroutWidthMM = 0.0f;

	/** Joint colour, LINEAR. Slightly greyer and darker than the tile it separates. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "05 Grout")
	FLinearColor GroutColor = FLinearColor(0.45f, 0.42f, 0.39f, 1.0f);

	/**
	 * Joint roughness. THE JOINT READS BY ITS SHEEN FAR MORE THAN BY ITS COLOUR.
	 *
	 * Cement grout beside a polished tile is a matt line next to a gloss one, and that contrast
	 * survives at grazing angles and at distance where a colour difference of the same size does
	 * not. Get this right before touching GroutColor.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "05 Grout",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float GroutRoughness = 0.70f;

	/**
	 * Per-tile albedo and roughness jitter, hashed off the tile index.
	 *
	 * Real vitrified tiles are shade-batched and visibly vary from one to the next. Nearly free, and
	 * it is what stops a floor reading as one infinite sheet clipped by the walls.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "05 Grout",
		meta = (ClampMin = "0.0", ClampMax = "0.5"))
	float TileShadeVariation = 0.0f;

	// ============================================================================= 06 glazing

	/** Glazed only. How much of the pane is opaque at all; float glass is almost none of it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "06 Glazing",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Opacity = 0.06f;

	/** Glazed only. 1.52 is soda-lime float glass. What displaces the view through a pane. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "06 Glazing",
		meta = (ClampMin = "1.0", ClampMax = "3.0"))
	float IndexOfRefraction = 1.52f;

	/**
	 * Glazed only. Path length the tint is absorbed across, in millimetres.
	 *
	 * Beer-Lambert, not a uniform tint: 6 is standard window float, 4 a ventilator, 12 a shower
	 * screen, and the green deepens with the pane instead of the whole thing shifting together.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "06 Glazing",
		meta = (ClampMin = "0.1", ClampMax = "50.0"))
	float GlassThicknessMM = 6.0f;

	// ============================================================================ 07 emissive

	/** Colour of the light coming off the surface, LINEAR. Only read where EmissiveStrength is set. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "07 Emissive")
	FLinearColor EmissiveColor = FLinearColor::White;

	/**
	 * How brightly it emits. Zero for every finish; non-zero only for LightSource.
	 *
	 * Separate from the colour so a fitting's brightness can be set without restating its colour.
	 * This is the hook a cove strip and a downlight lens hang on - the wash on the slab is the
	 * subject, so it sits a stop below where bloom takes over the frame.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "07 Emissive",
		meta = (ClampMin = "0.0", ClampMax = "200.0"))
	float EmissiveStrength = 0.0f;

	// =============================================================================== resolving

	/**
	 * Repeats of the pattern per UV unit: UVWorldSizeCm * 10 / TilingMM.
	 *
	 * THE MILLIMETRE-TO-CENTIMETRE STEP, WRITTEN OUT. The same expression the material graph builds
	 * out of nodes, on this side of the boundary, so a test can assert that halving a tile doubles
	 * the repeat without a renderer - and so there is exactly one place on the C++ side where mm and
	 * cm meet.
	 *
	 * @param UVWorldSizeCm  World size one UV unit covers. FHFRenderFinish::TexelSizeCm; 100 today.
	 */
	double UVRepeatsPerUnit(double UVWorldSizeCm) const
	{
		if (TilingMM <= 0.0f)
		{
			return 0.0;
		}
		const double UVWorldSizeMM = UVWorldSizeCm * 10.0;
		return UVWorldSizeMM / static_cast<double>(TilingMM);
	}

	/** World size of one repeat, in centimetres - the unit everything downstream of a spec is in. */
	double TileSizeCm() const { return static_cast<double>(TilingMM) * 0.1; }

	/**
	 * An sRGB triple as the linear colour a vector parameter wants.
	 *
	 * The default table quotes colours in sRGB because a paint chart and a tile catalogue do, and
	 * pasting one straight in would render it about two stops dark - wrong in a way that reads as
	 * merely "a bit off".
	 */
	static FLinearColor FromSRGB(float R, float G, float B)
	{
		auto Channel = [](float C)
		{
			return (C <= 0.04045f) ? (C / 12.92f) : FMath::Pow((C + 0.055f) / 1.055f, 2.4f);
		};
		return FLinearColor(Channel(R), Channel(G), Channel(B), 1.0f);
	}
};
