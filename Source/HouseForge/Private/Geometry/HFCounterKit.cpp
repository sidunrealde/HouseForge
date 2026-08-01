// Copyright Siddartha G. All Rights Reserved.

#include "Geometry/HFCounterKit.h"

#include "Geometry/HFMeshOps.h"

using namespace UE::Geometry;

namespace
{
	/** Thinner than this is not a slab. */
	constexpr double MinSlabThickness = 0.5;

	/** Width and depth of the routed drip groove. 5 x 5 mm is what the cutter actually is. */
	constexpr double DripGrooveSize = 0.5;

	/** How far back from the front face the groove is routed. */
	constexpr double DripGrooveSetback = 1.5;

	/** Steps a bullnose is approximated in. Three chamfers read as a round at any sane camera range. */
	constexpr int32 BullnoseSteps = 3;

	/** A rectangle in plan, wound counter-clockwise. */
	TArray<FVector2D> PlanRect(double X0, double Y0, double X1, double Y1)
	{
		return { FVector2D(X0, Y0), FVector2D(X1, Y0), FVector2D(X1, Y1), FVector2D(X0, Y1) };
	}
}

FHFCounterParams FHFCounterKit::Sanitise(const FHFCounterParams& Params)
{
	FHFCounterParams P = Params;

	P.Width = FMath::Max(P.Width, 0.0);
	P.Depth = FMath::Max(P.Depth, 0.0);
	P.Thickness = FMath::Max(P.Thickness, MinSlabThickness);
	P.Overhang = FMath::Max(P.Overhang, 0.0);
	P.UpstandHeight = FMath::Max(P.UpstandHeight, 0.0);

	// An upstand cannot be deeper than the slab it stands on, or it hangs off the front of it.
	P.UpstandThickness = FMath::Clamp(P.UpstandThickness, 0.0, FMath::Max(P.SlabDepth(), 0.0));

	// ------------------------------------------------------------------------------- the apertures
	//
	// EVERY HOLE HAS TO LEAVE STONE ROUND IT. Granite cracks along the short grain at the corner of a
	// cutout, so a hole that comes within 50 mm of an edge is refused rather than cut - and refusing
	// is the honest answer, because a slab with a hole through its front edge is not a worktop and
	// would look exactly like one from above. The same test throws out a hole that has drifted off
	// the slab entirely, which is what a mis-set-out sink would be.

	const double X0 = 0.0;
	const double X1 = P.Width;
	const double Y0 = P.FrontY();

	// The back of the CLEAR stone, not of the slab: an upstand standing at the wall is stone too, and
	// a hole cut under it would be a hole through the splashback.
	const double Y1 = P.Depth - P.UpstandThickness;

	TArray<FHFCounterAperture> Kept;
	Kept.Reserve(P.Apertures.Num());

	for (const FHFCounterAperture& Aperture : P.Apertures)
	{
		if (!Aperture.IsValid())
		{
			continue;
		}

		const FVector2D Half = Aperture.Size * 0.5;

		const bool bFits =
			Aperture.Centre.X - Half.X >= X0 + MinApertureMargin &&
			Aperture.Centre.X + Half.X <= X1 - MinApertureMargin &&
			Aperture.Centre.Y - Half.Y >= Y0 + MinApertureMargin &&
			Aperture.Centre.Y + Half.Y <= Y1 - MinApertureMargin;

		if (bFits)
		{
			Kept.Add(Aperture);
		}
	}

	P.Apertures = MoveTemp(Kept);
	return P;
}

FHFCounterBuild FHFCounterKit::Build(const FHFCounterParams& Params)
{
	FHFCounterBuild Out;
	FHFMeshOps::InitialiseMesh(Out.Shell);

	const FHFCounterParams P = Sanitise(Params);
	Out.Used = P;

	if (!P.IsValid())
	{
		return Out;
	}

	// ---------------------------------------------------------------------------------- the slab
	//
	// TRIANGULATED WITH ITS HOLES RATHER THAN CUT BY A BOOLEAN. A mesh boolean can resolve this case
	// imperfectly and report failure while returning geometry that merely looks right - which is
	// exactly how the ceiling bands were left solid once already, and a counter whose sink hole did
	// not open is the same defect with a sink standing on top of it hiding the evidence. An annulus
	// with rectangular holes is exact, faster, and cannot half-succeed.

	TArray<TArray<FVector2D>> Holes;
	Holes.Reserve(P.Apertures.Num());

	for (const FHFCounterAperture& Aperture : P.Apertures)
	{
		const FVector2D Half = Aperture.Size * 0.5;
		Holes.Add(PlanRect(
			Aperture.Centre.X - Half.X, Aperture.Centre.Y - Half.Y,
			Aperture.Centre.X + Half.X, Aperture.Centre.Y + Half.Y));
	}

	const TArray<FVector2D> Outline = PlanRect(0.0, P.FrontY(), P.Width, P.Depth);

	if (!FHFMeshOps::AppendPrismWithHoles(Out.Shell, Outline, Holes, 0.0, P.TopZ(),
		EHFSurfaceRole::CounterStone))
	{
		return Out;
	}

	Out.CutApertures = P.Apertures;

	// -------------------------------------------------------------------------------- the upstand
	//
	// A bar of the same stone standing on the slab at the wall. Modelled as its own solid sitting on
	// top rather than as part of the slab's section, because that is how one is actually fitted -
	// cut from the offcut and siliconed down - and because it lets the slab keep a single flat top
	// that everything set into it can be levelled against.

	if (P.UpstandHeight > 0.0 && P.UpstandThickness > 0.0)
	{
		FDynamicMesh3 Upstand;
		FHFMeshOps::InitialiseMesh(Upstand);

		FHFMeshOps::AppendBox(Upstand,
			FVector3d(P.Width * 0.5,
				P.Depth - P.UpstandThickness * 0.5,
				P.TopZ() + P.UpstandHeight * 0.5),
			FVector3d(P.Width * 0.5, P.UpstandThickness * 0.5, P.UpstandHeight * 0.5),
			0.0, EHFSurfaceRole::CounterStone);

		FHFMeshOps::AppendPreservingRoles(Out.Shell, Upstand);
	}

	// ----------------------------------------------------------------------------- the front edge
	//
	// The detail that most decides whether a worktop reads as stone or as a grey box, because it is
	// the only part of it seen edge-on from standing height and it runs the whole length of the room.
	//
	// Both worked edges are cut rather than modelled: the cutter carries CounterStone so the faces
	// the subtraction exposes come out as stone - see FHFMeshOps::SubtractInPlace, where the roles on
	// the exposed faces come from the TOOL and not from the target.

	const double FrontY = P.FrontY();

	if (P.Edge == EHFCounterEdge::DripGroove && P.Thickness > DripGrooveSize * 2.0)
	{
		// A square groove routed into the UNDERSIDE, set back from the front face. What stops water
		// running back along the soffit of the slab and down the door faces - and, on camera, a hard
		// shadow line the length of the run sitting just under the highlight on the edge.
		FDynamicMesh3 Groove;
		FHFMeshOps::InitialiseMesh(Groove);

		FHFMeshOps::AppendBox(Groove,
			FVector3d(P.Width * 0.5, FrontY + DripGrooveSetback + DripGrooveSize * 0.5, DripGrooveSize * 0.5),
			// Over-long in X so the cutter passes cleanly out of both ends of the run rather than
			// leaving a sliver of stone at each end that no boolean resolves the same way twice.
			FVector3d(P.Width * 0.5 + 1.0, DripGrooveSize * 0.5, DripGrooveSize * 0.5),
			0.0, EHFSurfaceRole::CounterStone);

		FHFMeshOps::SubtractInPlace(Out.Shell, Groove);
	}
	else if (P.Edge == EHFCounterEdge::Bullnose)
	{
		// A half-round eased off the front arris in a few steps. Approximated rather than swept as a
		// true arc because at three steps the silhouette is already inside a millimetre of the real
		// thing at any range somebody stands at, and the render chamfer softens what is left.
		const double Radius = FMath::Min(P.Thickness * 0.5, 1.0);

		for (int32 Step = 1; Step <= BullnoseSteps; ++Step)
		{
			const double Alpha = static_cast<double>(Step) / static_cast<double>(BullnoseSteps + 1);

			// Walk in from the face as we walk down from the top and up from the bottom, so the two
			// chamfers together approximate a round rather than a single flat.
			const double In = Radius * (1.0 - FMath::Cos(Alpha * PI * 0.5));
			const double Down = Radius * FMath::Sin(Alpha * PI * 0.5);

			for (const bool bTop : { true, false })
			{
				const double Z = bTop ? P.TopZ() - Down : Down;

				FDynamicMesh3 Cutter;
				FHFMeshOps::InitialiseMesh(Cutter);

				FHFMeshOps::AppendBox(Cutter,
					FVector3d(P.Width * 0.5, FrontY + In * 0.5, bTop ? Z + Radius : Z - Radius),
					FVector3d(P.Width * 0.5 + 1.0, In * 0.5, Radius),
					0.0, EHFSurfaceRole::CounterStone);

				if (In > UE_KINDA_SMALL_NUMBER)
				{
					FHFMeshOps::SubtractInPlace(Out.Shell, Cutter);
				}
			}
		}
	}

	FHFMeshOps::ApplyWorldScaleUVs(Out.Shell);

	Out.bValid = Out.Shell.TriangleCount() > 0;
	return Out;
}
