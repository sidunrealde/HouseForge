// Copyright Siddartha G. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DynamicMesh/DynamicMesh3.h"

class UDynamicMeshComponent;
class UMaterialInterface;
struct FPostProcessSettings;

/**
 * A PLAN IS A DRAWING, NOT A PHOTOGRAPH OF ONE.
 *
 * ## What was wrong, measured rather than guessed
 *
 * Three separate review packages came back with the same complaint about the section-cut plan:
 * clipped to pure white, a heavy bloom halo around the flat, walls barely separating from the floor,
 * furniture reading as faint outlines. The arithmetic says why, and it is not subtle.
 *
 * The plan was rendered LIT, through the placeholder rig, at the rig's own exposure. That exposure
 * is FHFViewingLight::InteriorEV100 - EV100 8 - and it is correct for an interior, where the only
 * thing reaching a wall is the ambient cubemap fill, because a scene capture runs no global
 * illumination. A PLAN HAS NO CEILING: the section throws away everything above 1.2 m, so the
 * placeholder sun - 10,000 lux at 48 degrees - lands on every floor slab in the flat with nothing in
 * the way. Horizontal illuminance is 10000 x sin(48) = 7431 lux, a 0.78-albedo floor returns
 * 7431 x 0.78 / pi = 1845 cd/m2, and the exposure that puts that at middle grey is
 * log2(1845 x 100 / 12.5) = EV100 13.9.
 *
 * The plan was being exposed six stops hot. Six stops is not "a bit bright"; it is every surface in
 * the frame pinned against the top of the tone curve, which is exactly a spike at 1.0 with no
 * contrast under it - and bloom, left at the post-process default, then took the clipped white and
 * spread it into the black around the flat as the halo.
 *
 * ## Why the answer is not a better exposure
 *
 * Because a correctly exposed photograph of a section is still a photograph of a section: sun
 * shadows raking across the floor from one corner, one side of every wall brighter than the other,
 * and the tone of a surface decided by which way it faces rather than by what it is. A drawing is
 * the opposite of that. A drawing is flat tone by what a thing IS, and the cut is filled solid.
 *
 * So the plan is drawn UNLIT, through SCS_BaseColor - the deferred base-colour buffer, taken before
 * lighting, before exposure, before the tonemapper and before bloom. Not one of the four things that
 * blew the image out is in that path at all, which is a stronger guarantee than turning each of them
 * down would be: there is no exposure to drift, so a finish edit in milestone 10's library cannot
 * put the plan back in the shoulder of the curve the way it did to the interiors.
 *
 * ## And the tones are the drawing's, not the library's
 *
 * The section is materialled from the palette below rather than from UHFMaterialLibrary. That is a
 * deliberate break: a plan's job is to be compared against sheet 01, and if it took its tones from
 * the finish library then changing the wall paint would change the drawing. The palette is a small
 * fixed set chosen for separation on the black ground a base-colour capture leaves - see
 * PlanTones - with the cut faces filled solid, which is what poche is and what makes a wall read as
 * a wall from directly above.
 */
class FHFPlanDraw
{
public:
	/**
	 * Material slot the cut faces are drawn through: one past the last surface role.
	 *
	 * A SLOT RATHER THAN A NEW EHFSurfaceRole, and that is the whole reason this is contained inside
	 * the capture path. Material ids are role indices and every component in the plugin indexes its
	 * slots by them, so a role added for the benefit of one throwaway render would renumber nothing
	 * but would put a finish nobody can ever specify into the material panel, the bake's slot table
	 * and the library asset. The section geometry exists for one frame and is owned entirely by this
	 * file, so it can carry an extra slot that means nothing anywhere else.
	 */
	static int32 PocheSlot();

	/** How many slots a section component has: every role, plus the poche. */
	static int32 SlotCount();

	/**
	 * Retags the faces the cut exposed so they draw through PocheSlot().
	 *
	 * Identified geometrically rather than by asking the cut to tag them, because the cut is a pure
	 * generator shared with everything else and a cap face IS identifiable: every vertex of it lies
	 * in the cut plane. A horizontal surface that happens to sit exactly at the cut height comes out
	 * poche too, which is not a defect - it is at the cut, and a plan fills what is at the cut.
	 *
	 * @param CutZ  The plane, in the mesh's own space. FHFPlanSection cuts in world space, so this is
	 *              the world cut height.
	 * @return How many triangles were retagged.
	 */
	static int32 PocheTheCut(UE::Geometry::FDynamicMesh3& Mesh, double CutZ);

	/**
	 * Fills a section component's slots with the drawing palette.
	 *
	 * The materials are transient instances outered to the component, so they live exactly as long as
	 * the throwaway section does and go with it.
	 */
	static void ApplyPaletteTo(UDynamicMeshComponent* Component);

	/**
	 * The tone a slot is drawn in, as the sRGB grey a reader would measure off the image.
	 *
	 * Published so the test can assert the picture it measures against the palette it was drawn from
	 * rather than against numbers typed twice.
	 */
	static uint8 ToneForSlot(int32 Slot);

	/** Tone of the floor, and of the poche. The two the plan is read by. */
	static uint8 FloorTone();
	static uint8 PocheTone();

	/**
	 * Post-process for a drawing: nothing that can add light the geometry did not have.
	 *
	 * Belt and braces. A base-colour capture never reaches the tonemapper, so none of this is
	 * consulted on the path the plan actually takes - but a request is a public struct and the next
	 * caller to ask for a drawing through some other capture source must not inherit the bloom that
	 * put a halo round the last one.
	 */
	static void ApplyDrawingPostProcess(FPostProcessSettings& Settings);
};
