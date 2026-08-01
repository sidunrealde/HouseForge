// Copyright Siddartha G. All Rights Reserved.

#include "Geometry/HFBedKit.h"

#include "Geometry/HFMeshOps.h"

using namespace UE::Geometry;

namespace
{
	/** Below this a mattress is a pad and a plinth is a pencil line. */
	constexpr double MinSolid = 0.2;

	/**
	 * A box given by its two opposite corners.
	 *
	 * Every solid in a bed is one of these, so it is worth having in the one form rather than
	 * converting to centre-and-extent at each of the five call sites. Silently does nothing for a
	 * degenerate box: a zero-thickness solid is not a thin one, it is a pair of coincident faces that
	 * carries through every volume and closedness measurement taken afterwards.
	 */
	void AppendSolid(FDynamicMesh3& Mesh, const FVector3d& Min, const FVector3d& Max, EHFSurfaceRole Role)
	{
		const FVector3d Size = Max - Min;
		if (Size.X <= MinSolid || Size.Y <= MinSolid || Size.Z <= MinSolid)
		{
			return;
		}

		FHFMeshOps::AppendBox(Mesh, (Min + Max) * 0.5, Size * 0.5, 0.0, Role);
	}
}

FHFBedParams FHFBedKit::Sanitise(const FHFBedParams& Params)
{
	FHFBedParams P = Params;

	P.Width = FMath::Max(P.Width, 0.0);
	P.Depth = FMath::Max(P.Depth, 0.0);
	P.MattressThickness = FMath::Max(P.MattressThickness, 0.0);
	P.MattressTopZ = FMath::Max(P.MattressTopZ, 0.0);

	// A HEADBOARD CANNOT BE THE WHOLE BED. Clamped rather than refused, because a drawing that gave a
	// bed 60 mm of depth has made a units mistake, and the honest response to that is a bed with a
	// thin headboard rather than no bed at all - the validator is what says the drawing is wrong.
	P.HeadboardThickness = FMath::Clamp(P.HeadboardThickness, 0.0, P.Depth * 0.5);

	// The panel has to clear the mattress it stands behind, or it is a rail rather than a headboard.
	P.HeadboardHeight = FMath::Max(P.HeadboardHeight, P.MattressTopZ);

	// THE FRAME MUST STILL BE A FRAME once it is set in on both sides. An inset wider than half the
	// bed would turn the box inside out, and the shadow line it exists to cast has nothing left to
	// fall on.
	P.FrameInset = FMath::Clamp(P.FrameInset, 0.0, P.Width * 0.25);
	P.PlinthRecess = FMath::Clamp(P.PlinthRecess, 0.0,
		FMath::Max(P.Width * 0.25 - P.FrameInset, 0.0));

	// The plinth stands under the deck, so it cannot be taller than the deck's underside.
	P.PlinthHeight = FMath::Clamp(P.PlinthHeight, 0.0, FMath::Max(P.DeckTopZ() - MinSolid, 0.0));

	P.UpholsteryProud = FMath::Clamp(P.UpholsteryProud, 0.0, P.HeadboardThickness);
	P.UpholsteryMargin = FMath::Max(P.UpholsteryMargin, 0.0);

	return P;
}

FHFBedBuild FHFBedKit::Build(const FHFBedParams& Params)
{
	FHFBedBuild Out;

	FHFMeshOps::InitialiseMesh(Out.Shell);
	FHFMeshOps::InitialiseMesh(Out.Frame);
	FHFMeshOps::InitialiseMesh(Out.Headboard);
	FHFMeshOps::InitialiseMesh(Out.Mattress);

	const FHFBedParams P = Sanitise(Params);
	Out.Used = P;

	if (!P.IsValid())
	{
		return Out;
	}

	const double W = P.Width;
	const double HeadFaceY = P.HeadboardFaceY();
	const double MattressLength = P.MattressLength();
	const double DeckTopZ = P.DeckTopZ();

	// ------------------------------------------------------------------------------------- frame
	//
	// The box the mattress lies on, set in from it on the foot and the two sides so the mattress
	// oversails it. NOT set in at the head: that end dies into the headboard, which is wider than the
	// frame and covers it, so an inset there would open a slot between the two that light gets into
	// from the one direction nobody can reach to see what is in it.

	AppendSolid(Out.Frame,
		FVector3d(P.FrameInset, P.FrameInset, P.PlinthHeight),
		FVector3d(W - P.FrameInset, HeadFaceY, DeckTopZ),
		EHFSurfaceRole::JoineryCarcass);

	// And the recessed base under it - the toe shadow that keeps a box bed from reading as a block
	// resting on the floor. The same figure and the same reason as a run of joinery's plinth.
	AppendSolid(Out.Frame,
		FVector3d(P.FrameInset + P.PlinthRecess, P.FrameInset + P.PlinthRecess, 0.0),
		FVector3d(W - P.FrameInset - P.PlinthRecess, HeadFaceY, P.PlinthHeight),
		EHFSurfaceRole::JoineryCarcass);

	FHFMeshOps::ApplyWorldScaleUVs(Out.Frame);

	// --------------------------------------------------------------------------------- headboard
	//
	// Full width and down to the floor, which is what a fitted headboard is: it frames the mattress
	// on three sides in elevation, and the frame's own inset is measured against it.

	AppendSolid(Out.Headboard,
		FVector3d(0.0, HeadFaceY, 0.0),
		FVector3d(W, P.Depth, P.HeadboardHeight),
		EHFSurfaceRole::ShutterLaminate);

	// The upholstered pad, PROUD of the panel rather than flush in it. A pad let into a rebate is
	// what a picture frame does; a stuffed one stands off its backing, and standing off is what gives
	// it the soft rolled edge that separates fabric from board under any light. It also starts above
	// the mattress top: below that line the headboard is behind bedding and nobody upholsters it.
	if (P.bUpholsteredHeadboard)
	{
		const double PadZ0 = P.MattressTopZ + P.UpholsteryMargin;
		const double PadZ1 = P.HeadboardHeight - P.UpholsteryMargin;

		AppendSolid(Out.Headboard,
			FVector3d(P.UpholsteryMargin, HeadFaceY - P.UpholsteryProud, PadZ0),
			FVector3d(W - P.UpholsteryMargin, HeadFaceY, PadZ1),
			EHFSurfaceRole::Fabric);
	}

	FHFMeshOps::ApplyWorldScaleUVs(Out.Headboard);

	// ---------------------------------------------------------------------------------- mattress
	//
	// The full drawn width and the full length up to the headboard's face. This is the one solid in
	// the bed that is NOT set in from anything, and that is the whole arrangement: everything else
	// retreats from it, so it is the mattress that catches the light along its edge.

	AppendSolid(Out.Mattress,
		FVector3d(0.0, 0.0, DeckTopZ),
		FVector3d(W, MattressLength, P.MattressTopZ),
		EHFSurfaceRole::Fabric);

	FHFMeshOps::ApplyWorldScaleUVs(Out.Mattress);

	// ------------------------------------------------------------------------------------- shell

	FHFMeshOps::AppendPreservingRoles(Out.Shell, Out.Frame);
	FHFMeshOps::AppendPreservingRoles(Out.Shell, Out.Headboard);
	FHFMeshOps::AppendPreservingRoles(Out.Shell, Out.Mattress);

	Out.bValid = Out.Shell.TriangleCount() > 0;
	return Out;
}
