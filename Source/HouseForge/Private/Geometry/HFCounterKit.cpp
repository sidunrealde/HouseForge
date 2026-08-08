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

	// A BULLNOSE IS MODELLED RATHER THAN CUT, so the slab body stops short of the front and the worked
	// edge is a swept strip in front of it - see the front edge below for why. Every other edge keeps
	// the full-depth slab it always had.
	const double EdgeStripDepth = P.Edge == EHFCounterEdge::Bullnose
		? FMath::Min(P.Thickness * 0.4, 1.0) : 0.0;

	const TArray<FVector2D> Outline =
		PlanRect(0.0, P.FrontY() + EdgeStripDepth, P.Width, P.Depth);

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

		const double UpstandZ = P.TopZ() + P.UpstandHeight * 0.5;
		const double HalfHeight = P.UpstandHeight * 0.5;
		const double HalfThickness = P.UpstandThickness * 0.5;

		FHFMeshOps::AppendBox(Upstand,
			FVector3d(P.Width * 0.5, P.Depth - HalfThickness, UpstandZ),
			FVector3d(P.Width * 0.5, HalfThickness, HalfHeight),
			0.0, EHFSurfaceRole::CounterStone);

		// AND ROUND THE ENDS THAT DIE INTO A WALL. See FHFCounterParams::bUpstandReturnsAtStart: two
		// runs meeting at an internal corner are ONE splashback, and built without this the corner is
		// bare plaster standing on stone with a square open end on each side of it.
		//
		// The return runs from the back to the front of the SLAB rather than to the drawn footprint, so
		// it finishes flush with the worked edge instead of stopping short of it with a nib of stone
		// left over. It is deliberately allowed to overlap the back bar in the corner square: that is
		// one mitred piece of stone in reality, and two solids sharing a corner is what a boolean is
		// for, not something to avoid by leaving a gap.
		const double ReturnY0 = P.FrontY();
		const double ReturnY1 = P.Depth;
		const double ReturnDepth = ReturnY1 - ReturnY0;

		if (ReturnDepth > 0.0)
		{
			auto AppendReturn = [&Upstand, &P, UpstandZ, HalfHeight, HalfThickness, ReturnY0, ReturnDepth](double CentreX)
			{
				FHFMeshOps::AppendBox(Upstand,
					FVector3d(CentreX, ReturnY0 + ReturnDepth * 0.5, UpstandZ),
					FVector3d(HalfThickness, ReturnDepth * 0.5, HalfHeight),
					0.0, EHFSurfaceRole::CounterStone);
			};

			if (P.bUpstandReturnsAtStart)
			{
				AppendReturn(HalfThickness);
			}
			if (P.bUpstandReturnsAtEnd)
			{
				AppendReturn(P.Width - HalfThickness);
			}
		}

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
	else if (EdgeStripDepth > 0.0)
	{
		// ------------------------------------------------------- a real arc, MODELLED and not cut
		//
		// ## Why this is no longer a boolean at all
		//
		// It was three box cuts per arris, approximating the round in steps - and the profile was
		// written the WRONG WAY ROUND, insetting further as it went DOWN from the arris rather than
		// less. That undercuts: each step removed strictly more than the one above it, the steps
		// subsumed one another instead of stacking, and what came out was a single square notch under
		// an untouched square arris. Nothing had ever seen it, because every worktop in the flat is a
		// drip groove and nothing had asked this kit for a bullnose until a vanity did.
		//
		// Corrected, and then reduced to one swept cutter per arris, it STILL failed one boolean of the
		// two every time. A bullnose meets the faces it eases TANGENTIALLY - that is what makes it a
		// bullnose - and a tool touching its target at zero degrees along a whole edge is the hardest
		// case a mesh boolean has. What it left behind was a slab worked on one arris and square on the
		// other, with a line in a log and nothing whatever to see in a render.
		//
		// So the edge is built rather than removed: the slab body stops one radius short of the front
		// and this strip is swept in front of it with the round already in its section. Exact, one
		// primitive, and it cannot half-succeed. The strip laps the body rather than butting to it,
		// because two solids meeting in a shared plane is the same tangency problem in another form.
		const double Radius = EdgeStripDepth;
		const double Lap = FMath::Min(Radius * 0.5, 0.3);

		constexpr int32 ArcSteps = 6;

		TArray<FVector2D> Section;
		Section.Reserve(2 * ArcSteps + 6);

		// Bottom, from behind the lap forward to where the lower round begins.
		Section.Add(FVector2D(FrontY + Radius + Lap, 0.0));

		// The lower quadrant: centre one radius in and one radius up, so the arris pulls back the full
		// radius and the profile returns to the front face a radius above the underside.
		for (int32 Step = 0; Step <= ArcSteps; ++Step)
		{
			const double Angle = HALF_PI * static_cast<double>(Step) / static_cast<double>(ArcSteps);

			Section.Add(FVector2D(
				FrontY + Radius - Radius * FMath::Sin(Angle),
				Radius - Radius * FMath::Cos(Angle)));
		}

		// The face between the two rounds, and then the upper quadrant back onto the top.
		for (int32 Step = 0; Step <= ArcSteps; ++Step)
		{
			const double Angle = HALF_PI * static_cast<double>(Step) / static_cast<double>(ArcSteps);

			Section.Add(FVector2D(
				FrontY + Radius - Radius * FMath::Cos(Angle),
				P.TopZ() - Radius + Radius * FMath::Sin(Angle)));
		}

		Section.Add(FVector2D(FrontY + Radius + Lap, P.TopZ()));

		FDynamicMesh3 Edge;
		FHFMeshOps::InitialiseMesh(Edge);

		if (FHFMeshOps::AppendExtrudedSection(Edge, Section,
			FVector3d(0.0, 0.0, 0.0), FVector3d::UnitY(), FVector3d::UnitX(),
			P.Width, EHFSurfaceRole::CounterStone))
		{
			FHFMeshOps::AppendPreservingRoles(Out.Shell, Edge);
		}
	}

	FHFMeshOps::ApplyWorldScaleUVs(Out.Shell);

	Out.bValid = Out.Shell.TriangleCount() > 0;
	return Out;
}
