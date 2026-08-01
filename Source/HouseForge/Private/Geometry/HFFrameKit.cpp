// Copyright Siddartha G. All Rights Reserved.

#include "Geometry/HFFrameKit.h"

#include "Geometry/HFMeshOps.h"

using namespace UE::Geometry;

namespace
{
	/** Sides a thin round member is drawn in. Sixteen is round at the distance a rail is seen from. */
	constexpr int32 TubeSides = 16;

	/**
	 * How far a member runs INTO what it lands on.
	 *
	 * Members that stop exactly on another member's surface leave two faces in one plane, and a rail
	 * is thin enough that the resulting seam is a visible dark line at any distance the fitting is
	 * seen from. Overlapping is not sloppiness here; it is the joint.
	 */
	constexpr double JointOverlap = 0.3;
}

FHFTowelRailParams FHFFrameKit::SanitiseTowelRail(const FHFTowelRailParams& Params)
{
	FHFTowelRailParams P = Params;

	P.Width = FMath::Max(P.Width, 0.0);
	P.Depth = FMath::Max(P.Depth, 0.0);
	P.Height = FMath::Max(P.Height, 0.0);

	// The tube may not be thicker than the box it was drawn in, in either direction. A drawing that
	// gave a 40 mm deep rail has not asked for a 25 mm tube standing 15 mm off the wall.
	P.RailDiameter = FMath::Clamp(P.RailDiameter, 0.1,
		FMath::Max(FMath::Min(P.Depth, P.Height), 0.1));

	P.FlangeThickness = FMath::Clamp(P.FlangeThickness, 0.0, FMath::Max(P.Depth * 0.4, 0.0));
	P.FlangeDiameter = FMath::Clamp(P.FlangeDiameter, P.RailDiameter,
		FMath::Max(FMath::Min(P.Height, P.Width * 0.3), P.RailDiameter));

	return P;
}

FHFFrameBuild FHFFrameKit::BuildTowelRail(const FHFTowelRailParams& Params)
{
	FHFFrameBuild Out;
	FHFMeshOps::InitialiseMesh(Out.Shell);

	const FHFTowelRailParams P = SanitiseTowelRail(Params);

	if (!P.IsValid())
	{
		return Out;
	}

	const double WallY = P.Depth * 0.5;
	const double RailRadius = P.RailDiameter * 0.5;

	// The rail's centre line: as far forward as the drawn depth allows, and centred in the drawn
	// height. Both are set out from the box rather than chosen, so a drawing that says 40 x 60 gets a
	// rail whose front face is exactly on the front of the box.
	const double RailY = -WallY + RailRadius;
	const double RailZ = P.Height * 0.5;

	// Brackets stand in from the ends by their own radius, so the rail's ends are the run's ends -
	// which is what the drawn width means and where a rail's finials actually are.
	const double BracketX = FMath::Max(P.Width * 0.5 - P.FlangeDiameter * 0.5, 0.0);

	const double Ends[2] = { -BracketX, BracketX };

	for (const double EndX : Ends)
	{
		// -------------------------------------------------------------------------- the flange

		if (P.FlangeThickness > 0.0 && P.FlangeDiameter > 0.0)
		{
			FDynamicMesh3 Flange;
			FHFMeshOps::InitialiseMesh(Flange);

			// Domed rather than a disc on a stick: a flange is spun, and its edge is where every
			// highlight on the fitting sits.
			const TArray<FVector2D> Profile = {
				FVector2D(0.0, 0.0),
				FVector2D(0.0, P.FlangeDiameter * 0.5),
				FVector2D(P.FlangeThickness * 0.55, P.FlangeDiameter * 0.47),
				FVector2D(P.FlangeThickness, P.FlangeDiameter * 0.34),
				FVector2D(P.FlangeThickness, 0.0)
			};

			if (FHFMeshOps::AppendRevolvedProfile(Flange, Profile,
				FVector3d(EndX, WallY, RailZ), -FVector3d::UnitY(), TubeSides,
				EHFSurfaceRole::MetalHardware))
			{
				FHFMeshOps::AppendPreservingRoles(Out.Shell, Flange);
			}
		}

		// ---------------------------------------------------------------------------- the stem
		//
		// From inside the flange out to the rail's centre line, so both joints are overlaps.

		FDynamicMesh3 Stem;
		FHFMeshOps::InitialiseMesh(Stem);

		const double StemLength = FMath::Max(WallY - RailY + JointOverlap, 0.01);

		const TArray<FVector2D> StemProfile = {
			FVector2D(0.0, 0.0),
			FVector2D(0.0, RailRadius * 0.8),
			FVector2D(StemLength, RailRadius * 0.8),
			FVector2D(StemLength, 0.0)
		};

		if (FHFMeshOps::AppendRevolvedProfile(Stem, StemProfile,
			FVector3d(EndX, WallY, RailZ), -FVector3d::UnitY(), TubeSides,
			EHFSurfaceRole::MetalHardware))
		{
			FHFMeshOps::AppendPreservingRoles(Out.Shell, Stem);
		}
	}

	// ------------------------------------------------------------------------------- the rail
	//
	// Full width, with domed ends, running THROUGH both stems rather than between them.

	{
		FDynamicMesh3 Rail;
		FHFMeshOps::InitialiseMesh(Rail);

		const double Cap = FMath::Min(RailRadius, P.Width * 0.05);

		const TArray<FVector2D> Profile = {
			FVector2D(0.0, 0.0),
			FVector2D(0.0, RailRadius * 0.55),
			FVector2D(Cap * 0.5, RailRadius * 0.92),
			FVector2D(Cap, RailRadius),
			FVector2D(P.Width - Cap, RailRadius),
			FVector2D(P.Width - Cap * 0.5, RailRadius * 0.92),
			FVector2D(P.Width, RailRadius * 0.55),
			FVector2D(P.Width, 0.0)
		};

		if (FHFMeshOps::AppendRevolvedProfile(Rail, Profile,
			FVector3d(-P.Width * 0.5, RailY, RailZ), FVector3d::UnitX(), TubeSides,
			EHFSurfaceRole::MetalHardware))
		{
			FHFMeshOps::AppendPreservingRoles(Out.Shell, Rail);
		}
	}

	FHFMeshOps::ApplyWorldScaleUVs(Out.Shell);

	Out.bValid = Out.Shell.TriangleCount() > 0;
	return Out;
}
