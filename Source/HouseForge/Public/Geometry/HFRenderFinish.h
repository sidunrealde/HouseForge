// Copyright Siddartha G. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Model/HFTypes.h"
#include "HFRenderFinish.generated.h"

/**
 * The chamfer put on a generated arris.
 *
 * .claude/rules/04-conventions.md asks for this by name: "No perfectly sharp edges. A real edge has
 * a small chamfer that catches light; a mathematically sharp one reads as CG under any lighting."
 * Everything in this plugin is built from AppendBox, AppendPrism and AppendExtrudedSection, all of
 * which emit mathematically perfect arrises, so until now every wall corner, door reveal, beam
 * soffit, column arris, skirting top and shutter edge in the flat was razor sharp. That is the most
 * pervasive CG tell there is, because it is on every silhouette in every frame.
 *
 * ## Width follows the material, not the element
 *
 * A plasterer's arris bead is 2-3 mm and a joiner's edge-banded shutter is barely 1; a stone counter
 * nose is pencil-rounded at 2 and a glass arris is seamed at well under 1. So the figure is chosen
 * per surface role rather than per element, and an edge between two roles takes the SMALLER of the
 * two - if either side has to stay sharp, the edge stays sharp.
 *
 * Fabric is deliberately zero. A cushion has no arris to catch light, and chamfering one would spend
 * triangles making soft goods look machined.
 */
USTRUCT(BlueprintType)
struct HOUSEFORGE_API FHFBevelParams
{
	GENERATED_BODY()

	/** Off leaves every arris mathematically sharp, which is what the plugin did before this existed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	bool bEnabled = true;

	/** Plaster, paint and concrete: walls, ceilings, coves, slabs, exposed beams and columns. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ClampMin = "0.0"))
	double PlasterWidth = 0.15;

	/** Timber, laminate, ply and uPVC: carcasses, shutters, door leaves, window frames, appliances. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ClampMin = "0.0"))
	double JoineryWidth = 0.10;

	/** Stone and vitreous: counter noses, skirting tops, sanitaryware. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ClampMin = "0.0"))
	double StoneWidth = 0.20;

	/** Metal hardware - handles, tracks, flanges. Small, because the stock itself is small. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ClampMin = "0.0"))
	double MetalWidth = 0.05;

	/** A seamed glass arris. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ClampMin = "0.0"))
	double GlassWidth = 0.05;

	/**
	 * Dihedral angle above which an edge is chamfered at all.
	 *
	 * Deliberately the same figure FHFMeshOps::ComputeShadingNormals splits normals at, so the rule
	 * is one rule: EVERY EDGE THAT STAYS HARD GETS A CHAMFER. Below it, the edge is a facet seam on
	 * something meant to read as curved - a rail tube, a knob dome, a cove arc - and those weld
	 * smooth and must not be chamfered, or the smoothing they exist for would be cut back apart.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ClampMin = "1.0", ClampMax = "179.0"))
	double MinAngleDegrees = 40.0;

	/**
	 * How much wider than the chamfer a face has to be before its edges are chamfered.
	 *
	 * The guard that stops a bevel eating a feature. This kit is full of deliberate 3 mm shadow gaps
	 * between shutters and 1 mm clearances behind them; chamfering both arrises of a 3 mm reveal at
	 * 1.5 mm consumes the reveal entirely and welds the shutters back into the unbroken slab the
	 * reveal exists to prevent. Measured as the triangle's altitude over the candidate edge, which
	 * for the two-triangle faces AppendBox emits is exactly the face's width across that edge.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ClampMin = "1.0"))
	double MinFeatureFactor = 3.0;

	/** Chamfer for a role, in centimetres. Zero leaves that role's edges sharp. */
	double WidthFor(EHFSurfaceRole Role) const
	{
		switch (Role)
		{
		case EHFSurfaceRole::WallPaint:
		case EHFSurfaceRole::FloorFinish:
		case EHFSurfaceRole::CeilingSoffit:
		case EHFSurfaceRole::CoveInterior:
		case EHFSurfaceRole::Structure:
			return PlasterWidth;

		case EHFSurfaceRole::JoineryCarcass:
		case EHFSurfaceRole::ShutterLaminate:
		case EHFSurfaceRole::DoorLeaf:
		case EHFSurfaceRole::WindowFrame:
		case EHFSurfaceRole::Appliance:
			return JoineryWidth;

		case EHFSurfaceRole::Skirting:
		case EHFSurfaceRole::CounterStone:
		case EHFSurfaceRole::Sanitary:
			return StoneWidth;

		case EHFSurfaceRole::MetalHardware:
			return MetalWidth;

		case EHFSurfaceRole::Glass:
			return GlassWidth;

		case EHFSurfaceRole::Fabric:
		default:
			return 0.0;
		}
	}
};

/**
 * The second UV channel, for baked lighting.
 *
 * .claude/rules/04-conventions.md asks for "a second UV channel for lightmaps, so baked lighting
 * stays an option alongside Lumen". None existed: every generated mesh carried exactly one UV
 * layer, and a lightmap needs a non-overlapping unwrap that the world-scale UV0 is by construction
 * not - UV0 is world position over texel size, so two walls at the same coordinates in different
 * rooms occupy the same UV space, which is the whole point of it and fatal for a bake.
 *
 * UV0 is left alone. Nothing here touches it, and the material panel's tiling-in-millimetres
 * depends on that.
 */
USTRUCT(BlueprintType)
struct HOUSEFORGE_API FHFLightmapParams
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	bool bEnabled = true;

	/**
	 * Lightmap resolution the gutter is sized against.
	 *
	 * The packer works in UV space and converts a pixel gutter into it with this, so getting it
	 * wrong does not misplace an island - it makes the gutter the wrong number of texels wide, and
	 * a gutter under one texel is what bleeds one surface's bounce onto another.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ClampMin = "16"))
	int32 TextureResolution = 128;

	/** Gutter between islands, in texels at the resolution above. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ClampMin = "1"))
	int32 GutterPixels = 2;
};

/**
 * Everything done to a generated mesh between the generator finishing and the component getting it.
 *
 * The composing layer's job, not a generator's: .claude/rules/04-conventions.md keeps generators
 * pure functions of their parameters, and a chamfer that varied with a project setting inside one
 * would end that. It also has to be a funnel rather than a rule, because bevelling is the one mesh
 * operation here that is NOT idempotent - a generator that beveled its own output and was then
 * composed into a fixture that beveled again would chamfer the chamfers.
 *
 * So it runs exactly once per component mesh, in AHFElementActor::CommitMesh and
 * AHFArticulatedActor::RegenerateParts, and generators end where they always did.
 */
USTRUCT(BlueprintType)
struct HOUSEFORGE_API FHFRenderFinish
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	FHFBevelParams Bevel;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	FHFLightmapParams Lightmap;

	/** World size one UV0 tile covers. The figure the material panel expresses tiling against. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ClampMin = "0.1"))
	double TexelSizeCm = 100.0;
};
