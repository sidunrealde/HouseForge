// Copyright Siddartha G. All Rights Reserved.

#include "Geometry/HFWallPlateKit.h"

#include "DynamicMesh/MeshTransforms.h"
#include "Geometry/HFMeshOps.h"

using namespace UE::Geometry;

namespace
{
	/** Corner steps a lofted plate's outline is drawn at. */
	constexpr int32 PlateCornerSteps = 1;
}

FHFMirrorParams FHFWallPlateKit::SanitiseMirror(const FHFMirrorParams& Params)
{
	FHFMirrorParams P = Params;

	P.Width = FMath::Max(P.Width, 0.0);
	P.Height = FMath::Max(P.Height, 0.0);
	P.Depth = FMath::Max(P.Depth, 0.0);

	// The glass may not be thicker than the whole build-up. Everything the glass does not take is the
	// backing - see FHFMirrorParams::BackingThickness - so this one clamp settles both.
	P.GlassThickness = FMath::Clamp(P.GlassThickness, 0.2, FMath::Max(P.Depth, 0.2));

	// A bevel wider than a quarter of the plate is not a bevel, it is a pyramid.
	P.BevelWidth = FMath::Clamp(P.BevelWidth, 0.0, FMath::Min(P.Width, P.Height) * 0.25);

	return P;
}

FHFWallPlateBuild FHFWallPlateKit::BuildMirror(const FHFMirrorParams& Params)
{
	FHFWallPlateBuild Out;
	FHFMeshOps::InitialiseMesh(Out.Shell);

	const FHFMirrorParams P = SanitiseMirror(Params);

	if (!P.IsValid())
	{
		return Out;
	}

	const double WallY = P.Depth * 0.5;

	// ---------------------------------------------------------------------------- the backing board
	//
	// What holds the glass off the plaster. Held IN from the glass on every side so it is invisible
	// from in front - a backing flush with the mirror would show as a dark line round the edge from
	// any oblique view, which is the one view a bathroom mirror is always seen from.

	if (P.BackingThickness() > 0.0)
	{
		const double Inset = FMath::Min(1.5, FMath::Min(P.Width, P.Height) * 0.08);

		FDynamicMesh3 Backing;
		FHFMeshOps::InitialiseMesh(Backing);

		FHFMeshOps::AppendBox(Backing,
			FVector3d(0.0, WallY - P.BackingThickness() * 0.5, P.Height * 0.5),
			FVector3d(P.Width * 0.5 - Inset, P.BackingThickness() * 0.5, P.Height * 0.5 - Inset),
			0.0, EHFSurfaceRole::JoineryCarcass);

		FHFMeshOps::AppendPreservingRoles(Out.Shell, Backing);
	}

	// ------------------------------------------------------------------------------------ the glass
	//
	// A LOFT, NOT A BOX. See FHFMirrorParams: the bevel is the only thing about a frameless mirror
	// that catches light, and without it the plate renders as a grey rectangle that reads as a hole in
	// the wall rather than as an object on it.
	//
	// Built with its sections stacked up +Z - which is the only way FHFMeshOps::AppendLoft stacks
	// them - and then TURNED so the stack runs out of the wall. Building it the other way round would
	// mean a second loft primitive whose only difference is an axis.

	{
		const double GlassBackY = WallY - P.BackingThickness();

		const TArray<TArray<FVector2D>> Rings = {
			FHFMeshOps::RoundedRectangle(FVector2D::ZeroVector,
				FVector2D(P.Width * 0.5, P.Height * 0.5), 0.0, PlateCornerSteps),
			FHFMeshOps::RoundedRectangle(FVector2D::ZeroVector,
				FVector2D(FMath::Max(P.Width * 0.5 - P.BevelWidth, 0.01),
					FMath::Max(P.Height * 0.5 - P.BevelWidth, 0.01)), 0.0, PlateCornerSteps)
		};

		const TArray<double> Heights = { 0.0, P.GlassThickness };

		FDynamicMesh3 Glass;
		FHFMeshOps::InitialiseMesh(Glass);

		if (!FHFMeshOps::AppendLoft(Glass, Rings, Heights, true, true, EHFSurfaceRole::Glass))
		{
			return Out;
		}

		// Stack axis +Z becomes -Y (out of the wall), and the section's own +Y becomes +Z (up the
		// wall). A quarter turn about +X does exactly that, and the translation puts the plate's back
		// on the wall face with its bottom edge on the origin.
		const FTransformSRT3d ToWall(FQuaterniond(FVector3d::UnitX(), 90.0, /*bAngleIsDegrees*/ true),
			FVector3d(0.0, GlassBackY, P.Height * 0.5), FVector3d::One());

		MeshTransforms::ApplyTransform(Glass, ToWall, /*bReverseOrientationIfNeeded*/ true);

		FHFMeshOps::AppendPreservingRoles(Out.Shell, Glass);
	}

	FHFMeshOps::ApplyWorldScaleUVs(Out.Shell);

	Out.bValid = Out.Shell.TriangleCount() > 0;
	return Out;
}
