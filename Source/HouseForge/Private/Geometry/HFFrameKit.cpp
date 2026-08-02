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

	/** Below this a leg is a pencil line and an apron is a sheet of paper. */
	constexpr double MinSolid = 0.2;

	/** A soft box, skipped rather than degenerate. */
	void AppendSoft(FDynamicMesh3& Mesh, const FVector3d& Min, const FVector3d& Max,
		const FHFSoftBoxParams& Soft, EHFSurfaceRole Role)
	{
		const FVector3d Size = Max - Min;
		if (Size.X <= MinSolid || Size.Y <= MinSolid || Size.Z <= MinSolid)
		{
			return;
		}

		FHFMeshOps::AppendSoftBox(Mesh, Min, Max, Soft, Role);
	}

	/**
	 * The arris radius a square timber member carries. Clamped so it cannot swallow the member.
	 *
	 * The plan radius is kept at or above both rolls so the three meet in one surface at the corner -
	 * see FHFSoftBoxParams::CornerRadius.
	 */
	FHFSoftBoxParams TimberArris(double Roll, double Section)
	{
		FHFSoftBoxParams Soft;
		Soft.CornerRadius = FMath::Min(Roll, Section * 0.4);
		Soft.TopRadius = Soft.CornerRadius * 0.5;
		Soft.BottomRadius = Soft.CornerRadius * 0.5;
		Soft.CornerSteps = 2;
		Soft.RollSteps = 2;
		return Soft;
	}
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

// ---------------------------------------------------------------------------------------- table

FHFTableParams FHFFrameKit::SanitiseTable(const FHFTableParams& Params)
{
	FHFTableParams P = Params;

	P.Width = FMath::Max(P.Width, 0.0);
	P.Depth = FMath::Max(P.Depth, 0.0);
	P.Height = FMath::Max(P.Height, 0.0);

	// A TOP CANNOT BE THE WHOLE TABLE. Clamped rather than refused, for the reason every kit here
	// clamps: a drawing that gave a table 40 mm of height has made a units mistake, and the honest
	// answer is a table with a thin top rather than no table.
	P.TopThickness = FMath::Clamp(P.TopThickness, 0.0, P.Height * 0.5);

	// Four legs have to leave a table between them in both directions.
	P.LegSection = FMath::Clamp(P.LegSection, 0.0, FMath::Min(P.Width, P.Depth) * 0.25);
	P.LegInset = FMath::Clamp(P.LegInset, 0.0,
		FMath::Max(FMath::Min(P.Width, P.Depth) * 0.5 - P.LegSection, 0.0));

	// The apron may not eat the knee hole. Two thirds of the clear height is the limit, and past that
	// the object is a sideboard whatever the drawing called it.
	P.ApronDepth = FMath::Clamp(P.ApronDepth, 0.0, P.TopUnderZ() * 0.66);
	P.ApronThickness = FMath::Clamp(P.ApronThickness, 0.0, FMath::Max(P.LegSection, 0.0));
	P.ApronSetback = FMath::Clamp(P.ApronSetback, 0.0,
		FMath::Max(P.LegSection - P.ApronThickness, 0.0));

	// A shelf sits between the floor and the underside of the apron, or it is not a shelf.
	P.ShelfThickness = FMath::Max(P.ShelfThickness, 0.0);
	P.ShelfTopZ = FMath::Clamp(P.ShelfTopZ, 0.0, FMath::Max(P.KneeClearance() - MinSolid, 0.0));

	P.EdgeRoll = FMath::Clamp(P.EdgeRoll, 0.0, P.TopThickness * 0.45);
	P.LegRoll = FMath::Clamp(P.LegRoll, 0.0, P.LegSection * 0.4);

	return P;
}

FHFTableBuild FHFFrameKit::BuildTable(const FHFTableParams& Params)
{
	FHFTableBuild Out;

	FHFMeshOps::InitialiseMesh(Out.Shell);
	FHFMeshOps::InitialiseMesh(Out.Top);
	FHFMeshOps::InitialiseMesh(Out.Legs);
	FHFMeshOps::InitialiseMesh(Out.Apron);
	FHFMeshOps::InitialiseMesh(Out.Shelf);

	const FHFTableParams P = SanitiseTable(Params);
	Out.Used = P;

	if (!P.IsValid())
	{
		return Out;
	}

	const double HalfW = P.Width * 0.5;
	const double HalfD = P.Depth * 0.5;
	const double TopUnderZ = P.TopUnderZ();

	// -------------------------------------------------------------------------------------- top
	//
	// The one ShutterLaminate surface on the object, and the only one anybody looks at. Its edge is
	// rolled rather than chamfered because a table edge is what a hand runs along and what the light
	// catches across a room: a 1 mm chamfer at that scale is a hard line with a highlight on it.
	{
		FHFSoftBoxParams Soft;
		Soft.CornerRadius = FMath::Max(
			FMath::Min(P.EdgeRoll * 2.0, FMath::Min(HalfW, HalfD) * 0.2), P.EdgeRoll);
		Soft.TopRadius = P.EdgeRoll;
		Soft.BottomRadius = P.EdgeRoll;
		Soft.CornerSteps = 3;

		AppendSoft(Out.Top,
			FVector3d(-HalfW, -HalfD, TopUnderZ),
			FVector3d(HalfW, HalfD, P.Height),
			Soft, EHFSurfaceRole::ShutterLaminate);

		FHFMeshOps::ApplyWorldScaleUVs(Out.Top);
	}

	// ------------------------------------------------------------------------------------- legs
	//
	// Run INTO the top rather than up to it: a leg that stopped on the underside would share that
	// plane with it, and two coplanar faces at a joint flicker at any distance. The kit's rule.
	{
		const FHFSoftBoxParams Soft = TimberArris(P.LegRoll, P.LegSection);

		const double LegX[2] = { -HalfW + P.LegInset, HalfW - P.LegInset - P.LegSection };
		const double LegY[2] = { -HalfD + P.LegInset, HalfD - P.LegInset - P.LegSection };

		for (const double X : LegX)
		{
			for (const double Y : LegY)
			{
				AppendSoft(Out.Legs,
					FVector3d(X, Y, 0.0),
					FVector3d(X + P.LegSection, Y + P.LegSection, TopUnderZ + JointOverlap),
					Soft, EHFSurfaceRole::JoineryCarcass);
			}
		}

		FHFMeshOps::ApplyWorldScaleUVs(Out.Legs);
	}

	// ------------------------------------------------------------------------------------ apron
	//
	// Four rails between the legs, set back inside their outer faces. The setback is the whole reason
	// it reads as a frame: flush with the legs, the four rails and the four legs are one continuous
	// skirt round the table and the legs stop existing.
	if (P.ApronDepth > MinSolid && P.ApronThickness > MinSolid)
	{
		const FHFSoftBoxParams Soft = TimberArris(P.LegRoll, P.ApronThickness);

		const double ApronZ0 = TopUnderZ - P.ApronDepth;
		const double ApronZ1 = TopUnderZ + JointOverlap;

		const double OuterX = HalfW - P.LegInset - P.ApronSetback;
		const double OuterY = HalfD - P.LegInset - P.ApronSetback;
		const double InnerX = OuterX - P.ApronThickness;
		const double InnerY = OuterY - P.ApronThickness;

		// The long rails run the full span; the short ones die into them, so every junction is a lap.
		AppendSoft(Out.Apron, FVector3d(-OuterX, -OuterY, ApronZ0),
			FVector3d(OuterX, -InnerY, ApronZ1), Soft, EHFSurfaceRole::JoineryCarcass);
		AppendSoft(Out.Apron, FVector3d(-OuterX, InnerY, ApronZ0),
			FVector3d(OuterX, OuterY, ApronZ1), Soft, EHFSurfaceRole::JoineryCarcass);

		AppendSoft(Out.Apron, FVector3d(-OuterX, -InnerY, ApronZ0),
			FVector3d(-InnerX, InnerY, ApronZ1), Soft, EHFSurfaceRole::JoineryCarcass);
		AppendSoft(Out.Apron, FVector3d(InnerX, -InnerY, ApronZ0),
			FVector3d(OuterX, InnerY, ApronZ1), Soft, EHFSurfaceRole::JoineryCarcass);

		FHFMeshOps::ApplyWorldScaleUVs(Out.Apron);
	}

	// ------------------------------------------------------------------------------------ shelf
	//
	// What separates a coffee table from a small dining table once the proportions are set. Runs into
	// the legs on all four sides.
	if (P.ShelfTopZ > MinSolid && P.ShelfThickness > MinSolid)
	{
		FHFSoftBoxParams Soft;
		Soft.CornerRadius = FMath::Min(P.EdgeRoll, P.ShelfThickness * 0.4);
		Soft.TopRadius = Soft.CornerRadius * 0.5;
		Soft.BottomRadius = Soft.TopRadius;
		Soft.CornerSteps = 2;
		Soft.RollSteps = 2;

		const double ShelfX = FMath::Max(HalfW - P.LegInset - P.LegSection * 0.5, MinSolid);
		const double ShelfY = FMath::Max(HalfD - P.LegInset - P.LegSection * 0.5, MinSolid);

		AppendSoft(Out.Shelf,
			FVector3d(-ShelfX, -ShelfY, FMath::Max(P.ShelfTopZ - P.ShelfThickness, 0.0)),
			FVector3d(ShelfX, ShelfY, P.ShelfTopZ),
			Soft, EHFSurfaceRole::JoineryCarcass);

		FHFMeshOps::ApplyWorldScaleUVs(Out.Shelf);
	}

	FHFMeshOps::AppendPreservingRoles(Out.Shell, Out.Legs);
	FHFMeshOps::AppendPreservingRoles(Out.Shell, Out.Apron);
	FHFMeshOps::AppendPreservingRoles(Out.Shell, Out.Shelf);
	FHFMeshOps::AppendPreservingRoles(Out.Shell, Out.Top);

	Out.bValid = Out.Shell.TriangleCount() > 0;
	return Out;
}

// ---------------------------------------------------------------------------------------- chair

FHFChairParams FHFFrameKit::SanitiseChair(const FHFChairParams& Params)
{
	FHFChairParams P = Params;

	P.Width = FMath::Max(P.Width, 0.0);
	P.Depth = FMath::Max(P.Depth, 0.0);
	P.Height = FMath::Max(P.Height, 0.0);

	P.LegSection = FMath::Clamp(P.LegSection, 0.0, FMath::Min(P.Width, P.Depth) * 0.2);
	P.LegInset = FMath::Clamp(P.LegInset, 0.0, FMath::Max(P.Width * 0.5 - P.LegSection, 0.0));

	// THE LEAN CANNOT TAKE THE SEAT. A chair whose rake ate half its depth would have 200 mm to sit
	// on, which is a stool with a backrest standing behind it.
	P.BackRake = FMath::Clamp(P.BackRake, 0.0, P.Depth * 0.25);

	P.SeatHeight = FMath::Clamp(P.SeatHeight, 0.0, P.Height * 0.75);
	P.CushionThickness = FMath::Clamp(P.CushionThickness, 0.0, P.SeatHeight * 0.4);
	P.SeatThickness = FMath::Clamp(P.SeatThickness, MinSolid,
		FMath::Max(P.SeatHeight - P.CushionThickness - MinSolid, MinSolid));

	// The back rest has to fit between the seat and the top of the stiles with both reveals showing.
	const double BackSpan = FMath::Max(P.Height - P.SeatHeight, 0.0);
	const double RevealsAsked = P.BackRestGap + P.BackRestReveal;
	if (RevealsAsked > BackSpan * 0.8 && RevealsAsked > 0.0)
	{
		const double Scale = BackSpan * 0.8 / RevealsAsked;
		P.BackRestGap *= Scale;
		P.BackRestReveal *= Scale;
	}

	P.BackRestThickness = FMath::Clamp(P.BackRestThickness, 0.0, FMath::Max(P.LegSection, 0.0));

	P.CushionRoll = FMath::Max(P.CushionRoll, 0.0);
	P.TimberRoll = FMath::Clamp(P.TimberRoll, 0.0, P.LegSection * 0.4);

	return P;
}

FHFChairBuild FHFFrameKit::BuildChair(const FHFChairParams& Params)
{
	FHFChairBuild Out;

	FHFMeshOps::InitialiseMesh(Out.Shell);
	FHFMeshOps::InitialiseMesh(Out.Frame);
	FHFMeshOps::InitialiseMesh(Out.Cushion);
	FHFMeshOps::InitialiseMesh(Out.BackRest);

	const FHFChairParams P = SanitiseChair(Params);
	Out.Used = P;

	if (!P.IsValid())
	{
		return Out;
	}

	const double HalfW = P.Width * 0.5;
	const double FrontY = -P.Depth * 0.5;
	const double SeatBackY = FrontY + P.SeatDepth();
	const double SeatUnderZ = P.SeatUnderZ();
	const double SeatTopZ = FMath::Max(P.SeatHeight - P.CushionThickness, SeatUnderZ + MinSolid);

	const FHFSoftBoxParams Timber = TimberArris(P.TimberRoll, P.LegSection);

	// ------------------------------------------------------------------------------------- legs
	//
	// Four uprights running INTO the seat board. Below the seat all four are vertical: a chair whose
	// rear legs splayed from the floor would sweep more than the footprint it was declared with, and
	// the whole reason that footprint is worth anything is that the clearance check in the living
	// room is done against it.
	{
		const double LegX[2] = { -HalfW + P.LegInset, HalfW - P.LegInset - P.LegSection };
		const double LegY[2] = { FrontY, SeatBackY - P.LegSection };

		for (const double X : LegX)
		{
			for (const double Y : LegY)
			{
				AppendSoft(Out.Frame,
					FVector3d(X, Y, 0.0),
					FVector3d(X + P.LegSection, Y + P.LegSection, SeatTopZ),
					Timber, EHFSurfaceRole::JoineryCarcass);
			}
		}
	}

	// ------------------------------------------------------------------------------ back stiles
	//
	// From inside the seat board up to the top of the chair, LEANING. Built as a shear rather than a
	// rotation - see FHFSoftBoxParams::RakeY - so a raked stile still occupies an answerable box and
	// the drawn depth is exactly the envelope the lean sweeps.
	{
		FHFSoftBoxParams Raked = Timber;
		Raked.RakeY = P.BackRake;

		const double StileX[2] = { -HalfW + P.LegInset, HalfW - P.LegInset - P.LegSection };
		const double StileY0 = SeatBackY - P.LegSection;

		for (const double X : StileX)
		{
			AppendSoft(Out.Frame,
				FVector3d(X, StileY0, SeatUnderZ),
				FVector3d(X + P.LegSection, StileY0 + P.LegSection + P.BackRake, P.Height),
				Raked, EHFSurfaceRole::JoineryCarcass);
		}
	}

	// ------------------------------------------------------------------------------- seat board
	{
		const FHFSoftBoxParams Soft = TimberArris(P.TimberRoll, P.SeatThickness);

		AppendSoft(Out.Frame,
			FVector3d(-HalfW, FrontY, SeatUnderZ),
			FVector3d(HalfW, SeatBackY, SeatTopZ),
			Soft, EHFSurfaceRole::JoineryCarcass);
	}

	FHFMeshOps::ApplyWorldScaleUVs(Out.Frame);

	// --------------------------------------------------------------------------------- seat pad
	//
	// The one Fabric surface on the chair, and the only radius on it big enough to see across a room.
	// It sits ON the board rather than in a rebate, so the board stays visible as a line under it.
	if (P.CushionThickness > MinSolid)
	{
		FHFSoftBoxParams Soft;
		Soft.CornerRadius = P.CushionRoll * 1.5;
		Soft.TopRadius = Soft.CornerRadius;
		Soft.BottomRadius = P.CushionRoll * 0.4;
		Soft.RollSteps = 3;

		AppendSoft(Out.Cushion,
			FVector3d(-HalfW + P.TimberRoll, FrontY + P.TimberRoll, SeatTopZ),
			FVector3d(HalfW - P.TimberRoll, SeatBackY - P.TimberRoll, P.SeatHeight),
			Soft, EHFSurfaceRole::Fabric);

		FHFMeshOps::ApplyWorldScaleUVs(Out.Cushion);
	}

	// -------------------------------------------------------------------------------- back rest
	//
	// A panel between the stiles, leaning at exactly their rate so it lies IN the frame rather than
	// across it. Its own rake is the stiles' rake scaled by the fraction of their height it occupies,
	// which keeps the whole chair free of an angle that would have to agree in two places.
	{
		const double StileSpan = FMath::Max(P.Height - SeatUnderZ, MinSolid);
		const double RakePerZ = P.BackRake / StileSpan;

		const double RestZ0 = P.SeatHeight + P.BackRestGap;
		const double RestZ1 = FMath::Max(P.Height - P.BackRestReveal, RestZ0 + MinSolid);
		const double RestRake = RakePerZ * (RestZ1 - RestZ0);

		// Where the stiles' front faces are at the bottom of the rest, plus the setback that keeps the
		// panel inside them rather than standing proud of them.
		const double Setback = FMath::Max((P.LegSection - P.BackRestThickness) * 0.5, 0.0);
		const double RestY0 = SeatBackY - P.LegSection + RakePerZ * (RestZ0 - SeatUnderZ) + Setback;

		FHFSoftBoxParams Soft = TimberArris(P.TimberRoll, P.BackRestThickness);
		Soft.RakeY = RestRake;

		AppendSoft(Out.BackRest,
			FVector3d(-HalfW + P.LegInset + P.LegSection - JointOverlap, RestY0, RestZ0),
			FVector3d(HalfW - P.LegInset - P.LegSection + JointOverlap,
				RestY0 + P.BackRestThickness + RestRake, RestZ1),
			Soft, EHFSurfaceRole::ShutterLaminate);

		FHFMeshOps::ApplyWorldScaleUVs(Out.BackRest);
	}

	FHFMeshOps::AppendPreservingRoles(Out.Shell, Out.Frame);
	FHFMeshOps::AppendPreservingRoles(Out.Shell, Out.Cushion);
	FHFMeshOps::AppendPreservingRoles(Out.Shell, Out.BackRest);

	Out.bValid = Out.Shell.TriangleCount() > 0;
	return Out;
}

// ------------------------------------------------------------------------------- balcony guard
//
// A railing is the same construction problem as a towel rail at twenty times the length: every
// dimension that matters is a centre line, every joint is a member dying into another member's
// surface, and the whole thing is thin enough that a seam at a joint reads from across the balcony.
// So JointOverlap applies here exactly as it does above - the balusters run INTO the rails and the
// posts INTO the handrail, rather than up to them.
//
// What is different is that the numbers are not a matter of taste. The post count comes from the
// span, the baluster count comes from the sphere rule, and neither is a figure anybody types.

FHFRailingParams FHFFrameKit::SanitiseRailing(const FHFRailingParams& Params)
{
	FHFRailingParams P = Params;

	P.Width = FMath::Max(P.Width, 0.0);
	P.Depth = FMath::Max(P.Depth, 0.0);
	P.Height = FMath::Max(P.Height, 0.0);
	P.MountBaseHeight = FMath::Max(P.MountBaseHeight, 0.0);
	P.MaxClearGap = FMath::Max(P.MaxClearGap, 1.0);
	P.MaxPostSpacing = FMath::Max(P.MaxPostSpacing, 1.0);

	// No member may be thicker than the box it was drawn in. A drawing that gave a 60 mm railing has
	// not asked for a 100 mm post standing 40 out of it.
	P.PostSection = FMath::Clamp(P.PostSection, 0.0, FMath::Max(P.Depth, 0.0));
	P.BottomRailDepth = FMath::Clamp(P.BottomRailDepth, 0.0, FMath::Max(P.Depth, 0.0));
	P.BalusterSection = FMath::Clamp(P.BalusterSection, 0.0, FMath::Max(P.Depth, 0.0));
	P.GlassThickness = FMath::Clamp(P.GlassThickness, 0.0, FMath::Max(P.Depth, 0.0));

	// THE SPHERE RULE IS ENFORCED, NOT DOCUMENTED. The gap under the bottom rail is the one nobody
	// counts, and a figure typed at 150 because it looked right is a guard a toddler goes through
	// head first. Clamped here so no caller can express it, exactly as FHFCeilingDefaults clamps a
	// band drop that would push a downlight through the slab.
	P.BottomRailClearance = FMath::Clamp(P.BottomRailClearance, 0.0, P.MaxClearGap);

	// The rails and the clearance together may not eat the whole height; a guard with no infill left
	// in it is two rails and some air.
	const double Stack = P.TopRailHeight + P.BottomRailHeight + P.BottomRailClearance;
	if (Stack > 0.0 && Stack >= P.Height)
	{
		const double Scale = P.Height * 0.6 / Stack;
		P.TopRailHeight *= Scale;
		P.BottomRailHeight *= Scale;
		P.BottomRailClearance *= Scale;
	}

	P.BasePlateThickness = FMath::Clamp(P.BasePlateThickness, 0.0,
		FMath::Max(P.BottomRailClearance, 0.0));

	P.SteelArris = FMath::Clamp(P.SteelArris, 0.0, FMath::Max(P.PostSection * 0.35, 0.0));

	return P;
}

FHFRailingBuild FHFFrameKit::BuildRailing(const FHFRailingParams& Params)
{
	FHFRailingBuild Out;
	FHFMeshOps::InitialiseMesh(Out.Shell);
	FHFMeshOps::InitialiseMesh(Out.Frame);
	FHFMeshOps::InitialiseMesh(Out.Infill);

	const FHFRailingParams P = SanitiseRailing(Params);
	Out.Used = P;

	if (!P.IsValid())
	{
		return Out;
	}

	const double HalfW = P.Width * 0.5;
	const double HalfD = P.Depth * 0.5;

	const FHFSoftBoxParams Steel = TimberArris(P.SteelArris, P.PostSection);

	// ------------------------------------------------------------------------------- the posts
	//
	// Set out so the END posts' outer faces land exactly on the ends of the run: the drawn width is
	// the object, and a railing whose posts are centred on the ends overhangs its own parapet by half
	// a section at each end.
	const int32 Posts = P.PostCount();
	const double PostPitch = (Posts > 1) ? (P.Width - P.PostSection) / (Posts - 1) : 0.0;

	TArray<double> PostCentreX;
	PostCentreX.Reserve(Posts);
	for (int32 Index = 0; Index < Posts; ++Index)
	{
		PostCentreX.Add(-HalfW + P.PostSection * 0.5 + PostPitch * Index);
	}

	for (const double CentreX : PostCentreX)
	{
		// The post runs the whole height, INTO the handrail rather than up to it.
		AppendSoft(Out.Frame,
			FVector3d(CentreX - P.PostSection * 0.5, -P.PostSection * 0.5, 0.0),
			FVector3d(CentreX + P.PostSection * 0.5, P.PostSection * 0.5, P.Height),
			Steel, EHFSurfaceRole::MetalHardware);

		// The base plate. As deep as the drawn box and no deeper - a 100 x 100 plate is what one
		// really is, and it would stand 20 mm proud of a railing drawn 60 thick on both faces.
		if (P.BasePlateThickness > 0.0)
		{
			// Clamped to the run as well as to the depth. The end posts stand hard against the ends,
			// so an unclamped plate would put steel a whole half-plate past the drawn width - which is
			// a railing overhanging its own parapet at both ends, and bounds that no longer answer for
			// the object.
			const double PlateX0 = FMath::Max(CentreX - P.PostSection, -HalfW);
			const double PlateX1 = FMath::Min(CentreX + P.PostSection, HalfW);

			AppendSoft(Out.Frame,
				FVector3d(PlateX0, -HalfD, 0.0),
				FVector3d(PlateX1, HalfD, P.BasePlateThickness),
				TimberArris(P.SteelArris * 0.5, P.BasePlateThickness), EHFSurfaceRole::MetalHardware);
		}
	}

	// -------------------------------------------------------------------------------- the rails
	//
	// The handrail is the full drawn depth, which is what makes the drawn box the object: it is the
	// widest member on the railing and the only one anybody puts a hand on.
	AppendSoft(Out.Frame,
		FVector3d(-HalfW, -HalfD, P.Height - P.TopRailHeight),
		FVector3d(HalfW, HalfD, P.Height),
		TimberArris(P.SteelArris, P.TopRailHeight), EHFSurfaceRole::MetalHardware);

	const double BottomRailZ0 = P.BottomRailClearance;
	const double BottomRailZ1 = BottomRailZ0 + P.BottomRailHeight;

	AppendSoft(Out.Frame,
		FVector3d(-HalfW, -P.BottomRailDepth * 0.5, BottomRailZ0),
		FVector3d(HalfW, P.BottomRailDepth * 0.5, BottomRailZ1),
		TimberArris(P.SteelArris, P.BottomRailHeight), EHFSurfaceRole::MetalHardware);

	// ------------------------------------------------------------------------------- the infill

	const double InfillZ0 = BottomRailZ1 - JointOverlap;
	const double InfillZ1 = P.Height - P.TopRailHeight + JointOverlap;

	if (InfillZ1 > InfillZ0 + MinSolid)
	{
		for (int32 Bay = 0; Bay + 1 < Posts; ++Bay)
		{
			const double BayX0 = PostCentreX[Bay] + P.PostSection * 0.5;
			const double BayX1 = PostCentreX[Bay + 1] - P.PostSection * 0.5;
			const double BayWidth = BayX1 - BayX0;

			if (BayWidth <= MinSolid)
			{
				continue;
			}

			if (P.Infill == EHFRailingInfill::Glass)
			{
				// Captured on all four sides, and running INTO the frame on every one of them: a panel
				// that stops on the rail's face is a pane resting in a groove it does not reach.
				AppendSoft(Out.Infill,
					FVector3d(BayX0 - JointOverlap, -P.GlassThickness * 0.5, InfillZ0),
					FVector3d(BayX1 + JointOverlap, P.GlassThickness * 0.5, InfillZ1),
					TimberArris(0.05, P.GlassThickness), EHFSurfaceRole::Glass);
				continue;
			}

			// BALUSTERS, AND THE COUNT IS THE SPHERE RULE. n bars leave n + 1 gaps, and the pitch is
			// the bay divided by those gaps - so the bars are evenly spaced across the bay and the two
			// end gaps are the same as the middle ones. A run laid out by repeated addition from one
			// post leaves the remainder as a wide gap against the other, which is exactly the opening
			// the rule exists to close.
			const int32 Bars = P.BalustersPerBay();
			const double Gap = P.BalusterClearGap();

			for (int32 Bar = 0; Bar < Bars; ++Bar)
			{
				const double X0 = BayX0 + Gap * (Bar + 1) + P.BalusterSection * Bar;

				AppendSoft(Out.Infill,
					FVector3d(X0, -P.BalusterSection * 0.5, InfillZ0),
					FVector3d(X0 + P.BalusterSection, P.BalusterSection * 0.5, InfillZ1),
					TimberArris(P.SteelArris * 0.5, P.BalusterSection), EHFSurfaceRole::MetalHardware);
			}
		}
	}

	FHFMeshOps::ApplyWorldScaleUVs(Out.Frame);
	FHFMeshOps::ApplyWorldScaleUVs(Out.Infill);

	FHFMeshOps::AppendPreservingRoles(Out.Shell, Out.Frame);
	FHFMeshOps::AppendPreservingRoles(Out.Shell, Out.Infill);

	Out.bValid = Out.Shell.TriangleCount() > 0;
	return Out;
}
