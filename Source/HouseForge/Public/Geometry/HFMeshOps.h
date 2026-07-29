// Copyright Siddartha G. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DynamicMesh/DynamicMesh3.h"
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
	 * Subtracts Tool from Target in place.
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
	 */
	static void ApplyWorldScaleUVs(UE::Geometry::FDynamicMesh3& Mesh, double TexelSizeCm = 100.0);

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

	/** True if the mesh is closed - every edge shared by exactly two triangles. */
	static bool IsClosed(const UE::Geometry::FDynamicMesh3& Mesh);

	/** Signed area of a 2D polygon; positive when wound counter-clockwise. */
	static double SignedArea(const TArray<FVector2D>& Polygon);
};
