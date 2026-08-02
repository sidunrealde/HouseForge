// Copyright Siddartha G. All Rights Reserved.

#include "Geometry/HFUpholsteryKit.h"

#include "Geometry/HFMeshOps.h"

using namespace UE::Geometry;

namespace
{
	/** Below this a cushion is a sheet of fabric and a leg is a pencil line. */
	constexpr double MinSolid = 0.2;

	/**
	 * How far a leg runs up INTO the base, and a back panel into the arms it dies against.
	 *
	 * FHFFrameKit's rule, and it applies to soft goods for the same reason: two solids that TOUCH
	 * share a plane, and two coplanar faces at the join flicker against each other from any distance.
	 * So members run into what they land on. The overlap is inside the merged shell where nothing can
	 * see it, and the sub-assemblies are kept separately precisely so the clearances that DO matter -
	 * cushion to cushion, arm to base - stay measurable without it.
	 */
	constexpr double Lap = 2.0;

	/** A soft box, skipped rather than degenerate. Mirrors AppendSolid in the bed kit. */
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
}

FHFSofaParams FHFUpholsteryKit::SanitiseSofa(const FHFSofaParams& Params)
{
	FHFSofaParams P = Params;

	P.Width = FMath::Max(P.Width, 0.0);
	P.Depth = FMath::Max(P.Depth, 0.0);
	P.Height = FMath::Max(P.Height, 0.0);
	P.SeatCount = FMath::Clamp(P.SeatCount, 1, 6);

	// TWO ARMS CANNOT BE THE WHOLE SOFA. Clamped rather than refused, for the reason the bed kit
	// clamps a headboard: a drawing that gave a three-seater 400 mm of width has made a units
	// mistake, and the honest response is the widest arms that leave a seat rather than no sofa.
	P.ArmWidth = FMath::Clamp(P.ArmWidth, 0.0, P.Width * 0.25);
	P.BaseInset = FMath::Clamp(P.BaseInset, 0.0, FMath::Min(P.Width, P.Depth) * 0.05);

	// The back panel, the back cushion and its lean all come out of the same depth, and between them
	// they may not take the seat. Two thirds is the limit: at that point the seat is 300 mm deep.
	const double BackBudget = P.Depth * 0.66;
	const double BackAsked = P.BackThickness + P.BackCushionThickness + P.BackRake;
	if (BackAsked > BackBudget && BackAsked > 0.0)
	{
		const double Scale = BackBudget / BackAsked;
		P.BackThickness *= Scale;
		P.BackCushionThickness *= Scale;
		P.BackRake *= Scale;
	}

	P.BackThickness = FMath::Max(P.BackThickness, 0.0);
	P.BackCushionThickness = FMath::Max(P.BackCushionThickness, 0.0);
	P.BackRake = FMath::Max(P.BackRake, 0.0);

	// A seat above the arms is a bench, and arms above the back are a different object altogether.
	// Ordered outward from the seat because the seat is the figure a drawing actually states.
	P.SeatHeight = FMath::Clamp(P.SeatHeight, 0.0, P.Height * 0.75);
	P.ArmHeight = FMath::Clamp(P.ArmHeight, P.SeatHeight + MinSolid, FMath::Max(P.Height - MinSolid, 0.0));

	P.SeatCushionThickness = FMath::Clamp(P.SeatCushionThickness, 0.0, P.SeatHeight * 0.6);

	// The base has to stand on the legs and still be a base, so the legs cannot reach the deck.
	P.LegHeight = FMath::Clamp(P.LegHeight, 0.0, FMath::Max(P.DeckZ() - MinSolid, 0.0));
	P.LegDiameter = FMath::Max(P.LegDiameter, 0.0);
	P.LegInset = FMath::Clamp(P.LegInset, P.LegDiameter * 0.5,
		FMath::Max(FMath::Min(P.Width, P.Depth) * 0.5 - P.LegDiameter * 0.5, P.LegDiameter * 0.5));

	// EVERY GAP HAS TO COME OUT OF THE CLEAR WIDTH AND LEAVE CUSHIONS BEHIND. There is one more gap
	// than there are seats - one either side of the run as well as between each pair - so the whole
	// set is clamped to half the clear width and the cushions get the rest.
	P.CushionGap = FMath::Clamp(P.CushionGap, 0.0,
		P.InnerWidth() * 0.5 / FMath::Max(P.SeatCount + 1, 1));

	// What shows above the cushions is the panel; a cushion as tall as its own back leaves none.
	P.BackCushionHeight = FMath::Clamp(P.BackCushionHeight, 0.0,
		FMath::Max(P.Height - P.SeatHeight - MinSolid, 0.0));

	P.ArmRoll = FMath::Max(P.ArmRoll, 0.0);
	P.CushionRoll = FMath::Max(P.CushionRoll, 0.0);

	return P;
}

FHFSofaBuild FHFUpholsteryKit::BuildSofa(const FHFSofaParams& Params)
{
	FHFSofaBuild Out;

	FHFMeshOps::InitialiseMesh(Out.Shell);
	FHFMeshOps::InitialiseMesh(Out.Legs);
	FHFMeshOps::InitialiseMesh(Out.Base);
	FHFMeshOps::InitialiseMesh(Out.Back);

	const FHFSofaParams P = SanitiseSofa(Params);
	Out.Used = P;

	if (!P.IsValid())
	{
		return Out;
	}

	const double DeckZ = P.DeckZ();
	const double InnerX0 = P.InnerX0();
	const double InnerX1 = P.InnerX1();
	const double BackFaceY = P.BackFaceY();

	// ------------------------------------------------------------------------------------- legs
	//
	// THE ONLY HARD MATERIAL ON THE OBJECT, and the thing that stops a sofa reading as built-in
	// joinery. A 120 mm gap of daylight under a sofa is most of what separates loose furniture from a
	// bench: without it the base meets the floor in an unbroken line and the whole piece looks
	// scribed in, which is precisely what a sofa is not.
	//
	// Turned and tapered rather than square, because a revolve welds smooth under
	// ComputeShadingNormals and a 60 mm square leg is four more sharp arrises at exactly the height
	// the eye follows the floor line.
	{
		const double R = P.LegDiameter * 0.5;
		const double LegTopZ = P.LegHeight + Lap;

		const TArray<FVector2D> Profile = {
			FVector2D(0.0, R * 0.55),
			FVector2D(FMath::Min(0.8, P.LegHeight * 0.2), R * 0.7),
			FVector2D(LegTopZ, R)
		};

		const double LegX[2] = { P.LegInset, P.Width - P.LegInset };
		const double LegY[2] = { P.LegInset, P.Depth - P.LegInset };

		for (const double X : LegX)
		{
			for (const double Y : LegY)
			{
				FHFMeshOps::AppendRevolvedProfile(Out.Legs, Profile, FVector3d(X, Y, 0.0),
					FVector3d::UnitZ(), 12, EHFSurfaceRole::JoineryCarcass);
			}
		}

		FHFMeshOps::ApplyWorldScaleUVs(Out.Legs);
	}

	// ------------------------------------------------------------------------------------- base
	//
	// Set in from the drawn box on the front and the two sides so the arms and the cushions oversail
	// it. The bed's argument exactly: what is set back lies in shadow, and the silhouette of the sofa
	// becomes its arms rather than one slab running from the floor to the seat.
	{
		FHFSoftBoxParams Soft;
		Soft.BottomRadius = 2.0;
		Soft.TopRadius = 1.0;
		Soft.CornerRadius = Soft.BottomRadius;

		AppendSoft(Out.Base,
			FVector3d(P.BaseInset, P.BaseInset, P.LegHeight),
			FVector3d(P.Width - P.BaseInset, P.Depth, DeckZ),
			Soft, EHFSurfaceRole::Fabric);

		FHFMeshOps::ApplyWorldScaleUVs(Out.Base);
	}

	// ------------------------------------------------------------------------------------- arms
	//
	// The largest radius on the sofa and the one the light actually finds. Rolled in plan as well as
	// on top, so an arm reads as a bolster from above and in elevation rather than as a rectangle
	// with a rounded lid.
	{
		// The plan radius carries both rolls, so the corner of an arm is a sphere octant. See
		// FHFSoftBoxParams::CornerRadius - held below the roll it comes out as a flat lozenge, which on
		// the widest radius in the flat is the most conspicuous version of that defect there is.
		FHFSoftBoxParams Soft;
		Soft.CornerRadius = FMath::Min(P.ArmRoll, P.ArmWidth * 0.45);
		Soft.TopRadius = Soft.CornerRadius;
		Soft.BottomRadius = FMath::Min(3.0, Soft.CornerRadius);

		// FINER THAN THE KIT'S DEFAULT, and this is the one place in the flat where that is worth
		// paying for. An arm's corner is a 70 mm sphere octant at eye level a metre from the camera:
		// drawn in four steps by four it shades as a diamond-shaped highlight patch, which is a
		// low-polygon tell rather than a soft form. Six by five is where it stops reading as one.
		Soft.CornerSteps = 6;
		Soft.RollSteps = 5;

		const double ArmX[2][2] = { { 0.0, InnerX0 }, { InnerX1, P.Width } };

		for (const double(&Span)[2] : ArmX)
		{
			FDynamicMesh3 Arm;
			FHFMeshOps::InitialiseMesh(Arm);

			AppendSoft(Arm,
				FVector3d(Span[0], 0.0, P.LegHeight),
				FVector3d(Span[1], P.Depth, P.ArmHeight),
				Soft, EHFSurfaceRole::Fabric);

			FHFMeshOps::ApplyWorldScaleUVs(Arm);
			Out.Arms.Add(MoveTemp(Arm));
		}

		// After the array is final. A TArray of meshes relocates its elements with a raw Memmove, and
		// an attribute set left pointing at the freed buffer is undefined behaviour that looks right.
		// See FHFMeshOps::AdoptAttributes.
		for (FDynamicMesh3& Arm : Out.Arms)
		{
			FHFMeshOps::AdoptAttributes(Arm);
		}
	}

	// ------------------------------------------------------------------------------------- back
	//
	// Runs INTO both arms rather than up to them, and starts at the base rather than at the arm top:
	// a back panel that began where the arms end would leave a slot straight through the sofa at seat
	// height, which is exactly the failure the bed kit's headboard note describes.
	{
		FHFSoftBoxParams Soft;
		Soft.CornerRadius = FMath::Min(P.ArmRoll * 0.8, P.BackThickness * 0.45);
		Soft.TopRadius = Soft.CornerRadius;
		Soft.BottomRadius = FMath::Min(2.0, Soft.CornerRadius);

		AppendSoft(Out.Back,
			FVector3d(FMath::Max(InnerX0 - Lap, 0.0), BackFaceY, P.LegHeight),
			FVector3d(FMath::Min(InnerX1 + Lap, P.Width), P.Depth, P.Height),
			Soft, EHFSurfaceRole::Fabric);

		FHFMeshOps::ApplyWorldScaleUVs(Out.Back);
	}

	// --------------------------------------------------------------------------------- cushions
	//
	// One seat and one back per seat, with a gap either side of every one. The gaps are the whole
	// point: without them a three-seater's seat is a single 1740 mm slab, and no amount of radius on
	// its edges makes it read as three cushions.

	const double CushionWidth = P.SeatCushionWidth();
	const double SeatBackY = P.CushionFrontY() + P.SeatCushionDepth();
	const double BackCushionY0 = P.BackCushionY0();
	const double BackCushionY1 = BackCushionY0 + P.BackCushionThickness + P.BackRake;

	// Drawn finer than the kit's default, for the reason the arms are: a cushion corner is the
	// closest soft form to the camera in the whole flat.
	FHFSoftBoxParams SeatSoft;
	SeatSoft.CornerRadius = P.CushionRoll * 1.25;
	SeatSoft.TopRadius = SeatSoft.CornerRadius;
	SeatSoft.BottomRadius = P.CushionRoll * 0.5;
	SeatSoft.CornerSteps = 6;
	SeatSoft.RollSteps = 5;

	FHFSoftBoxParams BackSoft;
	BackSoft.CornerRadius = P.CushionRoll * 1.5;
	BackSoft.TopRadius = BackSoft.CornerRadius;
	BackSoft.BottomRadius = P.CushionRoll;
	BackSoft.RakeY = P.BackRake;
	BackSoft.CornerSteps = 6;
	BackSoft.RollSteps = 5;

	for (int32 Seat = 0; Seat < P.SeatCount; ++Seat)
	{
		const double X0 = InnerX0 + P.CushionGap + Seat * (CushionWidth + P.CushionGap);
		const double X1 = X0 + CushionWidth;

		FDynamicMesh3 SeatCushion;
		FHFMeshOps::InitialiseMesh(SeatCushion);
		AppendSoft(SeatCushion,
			FVector3d(X0, P.CushionFrontY(), DeckZ),
			FVector3d(X1, SeatBackY, P.SeatHeight),
			SeatSoft, EHFSurfaceRole::Fabric);
		FHFMeshOps::ApplyWorldScaleUVs(SeatCushion);
		Out.SeatCushions.Add(MoveTemp(SeatCushion));

		FDynamicMesh3 BackCushion;
		FHFMeshOps::InitialiseMesh(BackCushion);
		AppendSoft(BackCushion,
			FVector3d(X0, BackCushionY0, P.SeatHeight),
			FVector3d(X1, BackCushionY1, P.BackCushionTopZ()),
			BackSoft, EHFSurfaceRole::Fabric);
		FHFMeshOps::ApplyWorldScaleUVs(BackCushion);
		Out.BackCushions.Add(MoveTemp(BackCushion));
	}

	for (FDynamicMesh3& Cushion : Out.SeatCushions)
	{
		FHFMeshOps::AdoptAttributes(Cushion);
	}
	for (FDynamicMesh3& Cushion : Out.BackCushions)
	{
		FHFMeshOps::AdoptAttributes(Cushion);
	}

	// ------------------------------------------------------------------------------------ shell

	FHFMeshOps::AppendPreservingRoles(Out.Shell, Out.Legs);
	FHFMeshOps::AppendPreservingRoles(Out.Shell, Out.Base);
	for (const FDynamicMesh3& Arm : Out.Arms)
	{
		FHFMeshOps::AppendPreservingRoles(Out.Shell, Arm);
	}
	FHFMeshOps::AppendPreservingRoles(Out.Shell, Out.Back);
	for (const FDynamicMesh3& Cushion : Out.SeatCushions)
	{
		FHFMeshOps::AppendPreservingRoles(Out.Shell, Cushion);
	}
	for (const FDynamicMesh3& Cushion : Out.BackCushions)
	{
		FHFMeshOps::AppendPreservingRoles(Out.Shell, Cushion);
	}

	Out.bValid = Out.Shell.TriangleCount() > 0;
	return Out;
}
