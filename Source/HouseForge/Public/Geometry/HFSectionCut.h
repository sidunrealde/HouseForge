// Copyright Siddartha G. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Model/HFTypes.h"

/**
 * Parameters for one horizontal section cut.
 *
 * The height is in the MESH's own space, not the world's. Resolving a world cut height into each
 * mesh's space is the composing layer's job - see FHFSectionCut below for why that split is not
 * negotiable.
 */
struct HOUSEFORGE_API FHFSectionCutParams
{
	/** Everything at or above this height is removed. In the mesh's own space, in centimetres. */
	double CutZ = 120.0;

	/**
	 * Close the opening the cut leaves behind.
	 *
	 * On by default, and it is the whole point of the operation rather than a nicety. A wall is a
	 * closed box; cut its top off and the only face left pointing at a camera overhead is the
	 * box's own underside, whose normal points away - so it is backface-culled and the wall
	 * renders as nothing at all. A plan drawn from an uncapped section shows bare floor where
	 * every wall should be, which is worse than the roof it replaced: it looks like a plan.
	 */
	bool bCap = true;

	/**
	 * Surface role given to the faces the cut exposes.
	 *
	 * Stated by the caller rather than guessed. A cut mesh carries several roles - a wall with its
	 * skirting, a wardrobe with its carcass and its shutters - so there is no single right answer
	 * to read off the mesh, and an untagged cap is a face the material panel can never reach
	 * (.claude/rules/04-conventions.md).
	 */
	EHFSurfaceRole CapRole = EHFSurfaceRole::WallPaint;

	/** World size one UV tile covers on the cap, matching the rest of the plugin's projections. */
	double TexelSizeCm = 100.0;
};

/**
 * A horizontal section through generated geometry - the cut an architectural plan is drawn at.
 *
 * A top-down view of a house shows the top of its ceilings, which is a featureless slab and tells
 * nobody anything. A plan is a section: the building is cut through at about 1.2 m, the part above
 * is thrown away, and what is left reads as walls, doorways and rooms. This is that cut.
 *
 * It has to be geometry rather than a rendering trick. Hiding the ceilings would be the obvious
 * shortcut and it is not available: the structural ceiling slab is appended into AHFRoomActor's own
 * mesh alongside the floor slab, so hiding the ceiling hides the floor with it. Clipping at the
 * camera's near plane is not available either - an orthographic scene capture already clips at
 * z = 0, so a camera sitting on the cut plane clips correctly and still renders nothing, because
 * the cut leaves no forward-facing surface. Only a real cut, capped, produces a plan.
 *
 * Pure: (mesh, params) -> mesh. No world, no actor, no editor, no asset, no settings. That is what
 * lets the whole of this be tested headlessly, which matters more here than anywhere else in the
 * plugin - the thing it feeds is a renderer, and a renderer cannot be asked anything under -nullrhi.
 * Every property a plan needs to be correct is a property of this mesh, and every one of them is
 * measurable without a GPU.
 */
class HOUSEFORGE_API FHFSectionCut
{
public:
	/**
	 * The part of Source below the cut plane, capped where the plane passes through solid.
	 *
	 * Roles below the cut are carried through untouched; only the newly exposed faces take
	 * Params.CapRole. Geometry entirely above the plane disappears, which is exactly what makes
	 * a room's ceiling slab vanish while its floor stays.
	 *
	 * @param bOutClosed  Optional: true when the result has no open boundary, i.e. the cap
	 *                    succeeded everywhere. False on an empty result.
	 */
	static UE::Geometry::FDynamicMesh3 CutBelow(const UE::Geometry::FDynamicMesh3& Source,
		const FHFSectionCutParams& Params, bool* bOutClosed = nullptr);
};
