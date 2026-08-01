// Copyright Siddartha G. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Geometry/HFRenderFinish.h"
#include "Model/HFTypes.h"

/**
 * Mesh primitives shared by every generator.
 *
 * Everything here is a pure function over FDynamicMesh3 - no world, no actor, no asset loading -
 * which is what lets the generators be unit-tested without an editor or a level. See
 * .claude/rules/04-conventions.md.
 *
 * Every triangle emitted carries a polygroup identifying its surface role. That is load-bearing:
 * the material panel targets faces by role, so untagged geometry cannot be re-materialled later.
 */
class HOUSEFORGE_API FHFMeshOps
{
public:
	/** Polygroup id for a role. Offset by one so group 0 never means a real role. */
	static int32 GroupForRole(EHFSurfaceRole Role) { return static_cast<int32>(Role) + 1; }

	/** The role a polygroup came from, or WallPaint if the group is not a role. */
	static EHFSurfaceRole RoleForGroup(int32 GroupId);

	/** Prepares a mesh for generation: attributes, triangle groups, empty. */
	static void InitialiseMesh(UE::Geometry::FDynamicMesh3& Mesh);

	/** Material slot index a role renders through. Slot index IS the role index - see below. */
	static int32 MaterialIdForRole(EHFSurfaceRole Role) { return static_cast<int32>(Role); }

	/** The role a material slot renders, or WallPaint if the slot is not a role. */
	static EHFSurfaceRole RoleForMaterialId(int32 MaterialId);

	/** Number of material slots a fully-dressed HouseForge component carries: one per role. */
	static int32 NumSurfaceRoles() { return static_cast<int32>(EHFSurfaceRole::LightSource) + 1; }

	/**
	 * Writes each triangle's material id from the surface role its polygroup already carries.
	 *
	 * A UDynamicMeshComponent does NOT render by polygroup. It splits into render sections by the
	 * per-triangle MaterialID attribute and indexes its own material slot array with it
	 * (DynamicMeshSceneProxy::InitializeByMaterial), and that attribute is off by default -
	 * InitialiseMesh enables triangle groups and attributes, neither of which brings it. So a mesh
	 * covered in perfectly good surface-role polygroups still renders as one undifferentiated blob
	 * of the default material. This is the bridge between the two.
	 *
	 * Slot index is the role's own enum index, so every component carries the same slot table and
	 * a slot means the same thing on a wall as on a wardrobe. Compacting to only the roles a given
	 * mesh happens to use would save nothing measurable - the scene proxy skips a section with no
	 * triangles - and would cost a per-mesh remap table that the material panel would then have to
	 * consult before it could point at anything.
	 *
	 * Reads the polygroups and does not write them. That is deliberate and it is tested: the roles
	 * are what every later material operation targets, and a pass that renumbered them while
	 * assigning materials would undo the append fix that keeps them intact in the first place.
	 *
	 * Pure and idempotent - the material id is a function of the polygroup, so this can be re-run at
	 * any point after any boolean or append without needing the ids to have survived it.
	 */
	static void AssignMaterialIdsFromRoles(UE::Geometry::FDynamicMesh3& Mesh);

	/** The distinct surface roles present in a mesh, by polygroup. */
	static TSet<EHFSurfaceRole> RolesPresent(const UE::Geometry::FDynamicMesh3& Mesh);

	/**
	 * Points a mesh's attribute set back at the mesh it belongs to.
	 *
	 * An FDynamicMesh3 is self-referential: its attribute set holds a raw back-pointer to the mesh,
	 * and every overlay reaches through it - ClearElements sizes itself from
	 * ParentMesh->MaxTriangleID(), CreateFromPredicate walks ParentMesh's vertices. The copy and
	 * move constructors reparent, so ordinary use is safe.
	 *
	 * TArray is not ordinary use. UE relocates same-type elements with a raw Memmove
	 * (TCanBitwiseRelocate_V is unconditionally true when source and destination types match), so
	 * growing a TArray<FHFMeshPart> moves every mesh's bytes to a new address WITHOUT running a
	 * constructor - and the back-pointer is left aimed at the freed buffer. Nothing crashes at the
	 * time and nothing logs; the next overlay operation reads whatever is now at that address, which
	 * is usually plausible enough to carry on with. It is the exact shape of failure this plugin
	 * keeps finding: correct-looking output over undefined behaviour.
	 *
	 * Cheap and idempotent, so it is applied at the funnel every generator already goes through
	 * rather than left as a rule to remember at each array append.
	 */
	static void AdoptAttributes(UE::Geometry::FDynamicMesh3& Mesh);

	/**
	 * Appends an axis-aligned box, rotated about Z.
	 *
	 * @param Centre   Centre of the box in world centimetres.
	 * @param Extents  Half-size on each axis.
	 * @param YawDegrees Rotation about Z, about the centre.
	 */
	static void AppendBox(UE::Geometry::FDynamicMesh3& Mesh, const FVector3d& Centre,
		const FVector3d& Extents, double YawDegrees, EHFSurfaceRole Role);

	/**
	 * Appends a prism: a closed 2D polygon triangulated and extruded between two heights.
	 *
	 * Handles the concave, L-shaped rooms these layouts are full of; a convex-only triangulation
	 * would produce geometry spilling outside the room boundary.
	 *
	 * @param Polygon  Closed boundary, closing edge implicit. Winding is normalised internally.
	 */
	static bool AppendPrism(UE::Geometry::FDynamicMesh3& Mesh, const TArray<FVector2D>& Polygon,
		double BottomZ, double TopZ, EHFSurfaceRole Role);

	/**
	 * A prism with holes through it - the shape every perimeter ceiling band is.
	 *
	 * Triangulated directly rather than built by subtracting one prism from another. A mesh
	 * boolean can resolve that case imperfectly and report failure while returning geometry that
	 * merely looks right, which silently left ceiling bands solid. Triangulating the annulus is
	 * exact, faster, and cannot half-succeed.
	 */
	static bool AppendPrismWithHoles(UE::Geometry::FDynamicMesh3& Mesh, const TArray<FVector2D>& Outer,
		const TArray<TArray<FVector2D>>& Holes, double BottomZ, double TopZ, EHFSurfaceRole Role);

	/**
	 * Sweeps a closed 2D cross-section along a direction and appends the resulting solid.
	 *
	 * The workhorse for extruded joinery profiles - a gola channel, a cornice, a plinth, the cutter
	 * that routs a finger recess into a shutter edge. AppendPrism is the special case of this where
	 * the section lies in XY and the sweep runs up Z; this one takes its frame as an argument, so a
	 * profile can be swept along a shutter's top edge without the caller building a scratch mesh
	 * and transforming it into place.
	 *
	 * Only the section's first in-plane axis is taken; the second is derived as SweepDir x SectionU.
	 * Accepting both would let a caller hand over a mirrored frame and get a solid wound inside out,
	 * which still looks right in the viewport while subtracting nothing at all.
	 *
	 * @param Section      Closed cross-section in (u, v), closing edge implicit. Winding normalised.
	 * @param Origin       Where the section's (0, 0) sits at the start of the sweep.
	 * @param SectionU     The section's u axis. Orthogonalised against SweepDir; need not be unit.
	 * @param SweepLength  Distance swept. A negative length sweeps the other way rather than failing.
	 */
	static bool AppendExtrudedSection(UE::Geometry::FDynamicMesh3& Mesh, const TArray<FVector2D>& Section,
		const FVector3d& Origin, const FVector3d& SectionU, const FVector3d& SweepDir,
		double SweepLength, EHFSurfaceRole Role);

	/**
	 * Revolves a profile about an axis and appends the resulting solid.
	 *
	 * Profile points are (distance along the axis from Origin, radius). A zero radius at either end
	 * is an apex rather than a flat cap, which is what lets a knob be domed instead of reading as a
	 * disc on a stick - see the edge-quality bar in .claude/rules/04-conventions.md. An interior
	 * point must have a radius, because a zero one pinches the solid rather than closing it.
	 *
	 * SideCount is rounded up to a multiple of four, so the result carries vertices on both in-plane
	 * axes and its bounds are exactly its diameter rather than a chord short of it. A bar handle
	 * whose bounds are quietly 2% under its declared stock size is a bar handle nothing can be
	 * dimensioned against.
	 */
	static bool AppendRevolvedProfile(UE::Geometry::FDynamicMesh3& Mesh, const TArray<FVector2D>& Profile,
		const FVector3d& Origin, const FVector3d& Axis, int32 SideCount, EHFSurfaceRole Role);

	/**
	 * Appends Source into Target with every triangle's surface role intact.
	 *
	 * The only safe way to join two generated meshes. FDynamicMesh3::AppendWithOffsets shifts every
	 * appended triangle's polygroup by the target's MaxGroupID, which is right for a mesh whose
	 * groups are arbitrary partitions and catastrophic for one where the group IS the surface role:
	 * a shutter appended onto a carcass comes out tagged with a group no role maps to, RoleForGroup
	 * falls back to WallPaint, and the material panel can never reach it again.
	 *
	 * The failure is invisible - the geometry is in the right place and looks right in every
	 * screenshot - which is exactly why the raw append must not be called on role-tagged meshes.
	 */
	static void AppendPreservingRoles(UE::Geometry::FDynamicMesh3& Target,
		const UE::Geometry::FDynamicMesh3& Source);

	/**
	 * Subtracts Tool from Target in place, with the roles on both sides of the cut intact.
	 *
	 * The faces a subtraction exposes come from the TOOL, not from the target: the reveal of a
	 * window opening is the cutter's own side wall, and the inside of a gola channel is the cutter's
	 * profile. So the tool is tagged with the role those faces should end up carrying, and the cut
	 * has to preserve it - for the same reason AppendPreservingRoles exists, and against the same
	 * failure. FMeshBoolean appends the tool's surviving triangles through FDynamicMeshEditor,
	 * which allocates each of their groups a fresh id, so a routed recess comes out tagged with a
	 * group no role maps to and the material panel can never reach the inside of it again.
	 *
	 * Invisible, once more: the geometry is exactly right and every screenshot of it is correct.
	 *
	 * @return false if the boolean failed, in which case Target is left untouched rather than
	 *         half-cut - a partially subtracted wall is worse than an uncut one, because it looks
	 *         plausible in a screenshot.
	 */
	static bool SubtractInPlace(UE::Geometry::FDynamicMesh3& Target, const UE::Geometry::FDynamicMesh3& Tool);

	/**
	 * Projects real-world-scale UVs onto every triangle, grouped by polygroup.
	 *
	 * Box projection per group rather than per mesh, so a wall's faces and its reveals do not
	 * share a stretched projection. TexelSizeCm is the world size one UV tile covers, which is
	 * what lets the material panel express tiling in millimetres rather than arbitrary numbers.
	 *
	 * Also computes shading normals, and that is not a naming accident: a generator that unwrapped
	 * its mesh and forgot the normals would produce geometry that is right in every measurable way
	 * and shades as a flat blob under the first light put on it. Doing both in the one call every
	 * generator already ends with is what makes that impossible to forget.
	 */
	static void ApplyWorldScaleUVs(UE::Geometry::FDynamicMesh3& Mesh, double TexelSizeCm = 100.0);

	/**
	 * Fills the primary normal overlay, splitting it hard at any edge sharper than the threshold.
	 *
	 * Public so it can be asserted on. A mesh with no normal elements renders with the constant
	 * normal (0, 1, 0) and passes every geometric check there is, so the only way to know this ran
	 * is to read the overlay back.
	 *
	 * @param HardEdgeAngleDegrees Dihedral angle above which an edge stays hard. The default keeps a
	 *        box's arrises and a chamfer's facets crisp while welding a tube, a dome and a cove arc
	 *        smooth; see the implementation for why those particular numbers.
	 */
	static void ComputeShadingNormals(UE::Geometry::FDynamicMesh3& Mesh,
		double HardEdgeAngleDegrees = 40.0);

	/**
	 * Chamfers every convex arris the parameters ask for, with the surface roles intact.
	 *
	 * The ONLY way a bevel is applied here, exactly as AppendPreservingRoles is the only way meshes
	 * are joined, and against the same failure: FMeshBevel calls Mesh.AllocateTriangleGroup() for
	 * every strip and junction polygon it emits, so the chamfer facets come out in groups no role
	 * maps to, RoleForGroup falls to WallPaint, and the material panel can never reach the chamfers.
	 * Invisible in a screenshot and fatal to the material system. This puts them back by flooding the
	 * role in from the original faces each new triangle touches.
	 *
	 * ## What is selected, and what deliberately is not
	 *
	 * FMeshBevel's own InitializeFromGroupTopology is useless here. It bevels group-boundary edges,
	 * and in HouseForge the polygroup IS the surface role - a box's six faces are one group with no
	 * interior group edges, so it would bevel nothing at all. The edge set is chosen here instead:
	 *
	 *   - CONVEX only. Face normals alone cannot tell a convex arris from a concave internal corner
	 *     (the dot product is the same either way), so convexity is computed as the side of the first
	 *     triangle's plane the second triangle's far vertex falls on. Chamfering concave edges eats
	 *     material out of every junction, and a real internal corner is filled, not chamfered.
	 *   - Above the hard-edge angle. See FHFBevelParams::MinAngleDegrees.
	 *   - Only where the faces either side are wide enough to lose the chamfer without vanishing.
	 *     See FHFBevelParams::MinFeatureFactor - this kit's 3 mm shadow gaps depend on it.
	 *
	 * Runs one pass per distinct chamfer width present, widest first, and never re-bevels a facet an
	 * earlier pass created: a fresh chamfer meets its parent faces at 45 degrees, which is above the
	 * threshold, so without that guard the second pass would chamfer the first pass's chamfers.
	 *
	 * NOT IDEMPOTENT, which is what makes it a composing-layer operation rather than a generator one.
	 * See FHFRenderFinish.
	 *
	 * ## AND NOT EVERY CONVEX ARRIS IS AN ARRIS
	 *
	 * Each element is its own closed solid, so where one element's material STOPS because another
	 * element's material starts, the boundary between them is convex within the first solid's own
	 * mesh and is not an edge of the building at all. A partition butting into a wall ends in that
	 * wall's face; the plaster runs straight through. Chamfered anyway, both sides retreat and the
	 * two 45-degree strips meet as a 3 mm V-notch scored down what should be one flat plane - two
	 * full-height hairlines down the corridor's far wall, and the same at every wall butt, at all
	 * eighteen column-in-wall faces, and along the floor line wherever no skirting covers it. It was
	 * the first thing anybody would have seen, and it was new with the chamfer.
	 *
	 * So the composing layer says where the material continues. FlushVolumes are the volumes this
	 * element's geometry was built AROUND - which for a wall is exactly the FHFStructuralCut list it
	 * already carries, plus what it stands on - and a candidate edge lying inside one of them is
	 * dropped. Bounded volumes rather than infinite planes: a column flush in a wall shares the
	 * wall's face plane, and suppressing that plane would take every door reveal on the wall with it.
	 *
	 * In the same space as the mesh, which for a HouseForge element is world centimetres.
	 *
	 * @return true if any edge was chamfered. On failure the mesh is left exactly as it arrived - a
	 *         partially beveled mesh is worse than a sharp one, because it looks plausible.
	 */
	static bool BevelConvexEdges(UE::Geometry::FDynamicMesh3& Mesh, const FHFBevelParams& Params,
		const TArray<FHFStructuralCut>& FlushVolumes = TArray<FHFStructuralCut>());

	/**
	 * Builds a non-overlapping second UV channel for baked lighting, leaving UV0 untouched.
	 *
	 * UV0 cannot be used for a lightmap and that is not a defect in it: it is world position over
	 * texel size, so it is deliberately shared between every surface at the same coordinates, and
	 * two rooms' walls land on top of each other. A lightmap needs the opposite property.
	 *
	 * Islands are the mesh's own planar-projection regions - the same dominant-axis grouping UV0
	 * uses - packed into the unit square by the engine's own UV packer, the one static mesh lightmap
	 * generation uses. World-scale projection first means the islands arrive at a consistent
	 * texel-to-world ratio before packing, so a big wall gets proportionally more lightmap than a
	 * door handle instead of every island being scaled to fit its own slot.
	 *
	 * @return false if the mesh has no triangles or the packer could not lay the islands out, in
	 *         which case no second layer is left half-built.
	 */
	static bool BuildLightmapUVs(UE::Geometry::FDynamicMesh3& Mesh, const FHFLightmapParams& Params);

	/**
	 * Everything between a generator finishing and a component receiving the mesh: bevel, UVs, normals.
	 *
	 * Ordered, and the order is the whole point. The bevel runs FIRST so the chamfer facets are
	 * present when UV0 is projected and the normals are computed - a chamfer with no UVs and no
	 * normal elements renders as an untextured band shading off the constant normal, which is
	 * precisely the invisible-in-a-screenshot failure this file keeps guarding against. The lightmap
	 * unwrap runs LAST, because it packs the islands the projection produced.
	 *
	 * @param FlushVolumes What this element's material dies into. See BevelConvexEdges.
	 */
	static void FinishForRender(UE::Geometry::FDynamicMesh3& Mesh, const FHFRenderFinish& Finish,
		const TArray<FHFStructuralCut>& FlushVolumes = TArray<FHFStructuralCut>());

	/**
	 * Insets a closed polygon inward by Amount, returning the resulting loops.
	 *
	 * Uses a proper polygon offset rather than shifting each edge along its normal: on a concave
	 * corner - and these layouts are full of L-shaped rooms - naive offsetting produces
	 * self-intersecting garbage, and a deep enough inset legitimately splits one room into two
	 * separate loops or collapses it to nothing. Both are returned honestly.
	 *
	 * @return Empty when the inset consumes the polygon entirely, which is a real answer, not a
	 *         failure - it means the band is wider than the room.
	 */
	static TArray<TArray<FVector2D>> InsetPolygon(const TArray<FVector2D>& Polygon, double Amount);

	/**
	 * Subject minus the union of Cutters, as closed loops.
	 *
	 * WHAT MAKES A PERIMETER TREATMENT ANSWER TO THE ROOM RATHER THAN GO ROUND IT. A beam bulkhead
	 * belongs on the edges a beam actually shows along - two of the living room's four - and an inset
	 * cannot express that, because an inset is the same on every side by construction. Cutting the
	 * ceiling's outline with a strip per boundary edge can.
	 *
	 * Holes in the result are dropped: nothing in this domain subtracts an island out of the middle
	 * of a ceiling, and a loop with a hole in it is not something AppendPrism could build anyway.
	 *
	 * @return Empty when the cutters consume the subject entirely, which is a real answer.
	 */
	static TArray<TArray<FVector2D>> SubtractPolygons(const TArray<FVector2D>& Subject,
		const TArray<TArray<FVector2D>>& Cutters);

	/** Subject clipped to the union of Clips, as closed loops. The other half of SubtractPolygons. */
	static TArray<TArray<FVector2D>> IntersectPolygons(const TArray<FVector2D>& Subject,
		const TArray<TArray<FVector2D>>& Clips);

	/** True if the mesh is closed - every edge shared by exactly two triangles. */
	static bool IsClosed(const UE::Geometry::FDynamicMesh3& Mesh);

	/** Signed area of a 2D polygon; positive when wound counter-clockwise. */
	static double SignedArea(const TArray<FVector2D>& Polygon);
};
