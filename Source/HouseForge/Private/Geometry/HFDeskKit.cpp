// Copyright Siddartha G. All Rights Reserved.

#include "Geometry/HFDeskKit.h"

#include "DynamicMesh/MeshTransforms.h"
#include "Geometry/HFMeshOps.h"

using namespace UE::Geometry;

namespace
{
	/** Below this a board is a veneer and a knee hole is a slot. */
	constexpr double MinSolid = 0.2;

	/** Placing a mesh in desk space. Translation only; nothing here is ever mirrored. */
	FDynamicMesh3 Placed(FDynamicMesh3&& Mesh, const FVector& Where)
	{
		FDynamicMesh3 Out = MoveTemp(Mesh);
		if (Out.TriangleCount() > 0 && !Where.IsNearlyZero())
		{
			MeshTransforms::Translate(Out, FVector3d(Where));
		}
		return Out;
	}

	void AppendSolid(FDynamicMesh3& Mesh, const FVector3d& Min, const FVector3d& Max, EHFSurfaceRole Role)
	{
		const FVector3d Size = Max - Min;
		if (Size.X <= MinSolid || Size.Y <= MinSolid || Size.Z <= MinSolid)
		{
			return;
		}

		FHFMeshOps::AppendBox(Mesh, (Min + Max) * 0.5, Size * 0.5, 0.0, Role);
	}

	/**
	 * The handle for one drawer front, read off the part's own mesh.
	 *
	 * The same function FHFCasedGoodsKit has, and for the same reason: a bank graduates its fronts, so
	 * each is a different height and the only place those heights exist is inside
	 * FHFJoineryKit::BuildDrawerBank. Deriving them a second time here would be the drawer ladder
	 * implemented twice, and the copies would drift the first time either changed.
	 */
	FHFHandleParams MakeDrawerHandle(const FDynamicMesh3& PartMesh, double FrontThickness,
		EHFHandleStyle Style)
	{
		const FAxisAlignedBox3d Bounds = PartMesh.GetBounds();

		FHFHandleParams Handle;
		Handle.Style = Style;
		Handle.PanelBox = FBox(
			FVector(Bounds.Min.X, Bounds.Min.Y, Bounds.Min.Z),
			FVector(Bounds.Max.X, Bounds.Min.Y + FMath::Max(FrontThickness, 0.3), Bounds.Max.Z));
		Handle.Facing = EHFPanelFacing::NegativeY;
		Handle.Edge = EHFHandleEdge::Top;
		return Handle;
	}

	/** True for the part BuildDrawerBank emits to carry a drawer rather than to be one. */
	bool IsGearedRunner(const FHFMeshPart& Part)
	{
		return !Part.Motion.DrivenByPartId.IsNone();
	}
}

FHFDeskParams FHFDeskKit::Sanitise(const FHFDeskParams& Params)
{
	FHFDeskParams P = Params;

	P.Width = FMath::Max(P.Width, 0.0);
	P.Depth = FMath::Max(P.Depth, 0.0);
	P.Height = FMath::Max(P.Height, 0.0);
	P.TopThickness = FMath::Clamp(P.TopThickness, 0.0, P.Height);
	P.GableThickness = FMath::Max(P.GableThickness, 0.0);
	P.DrawerCount = FMath::Clamp(P.DrawerCount, 0, 6);
	P.GradationRatio = FMath::Max(P.GradationRatio, 1.0);

	// THE KNEE HOLE IS WHAT THE DESK IS FOR, so the pedestal is what gives way when the arithmetic
	// runs out - never the hole. A pedestal wide enough to leave nothing between it and the gable is
	// a sideboard, and a drawing that asked for one has asked for the wrong object.
	const double MaxPedestal = FMath::Max(P.Width - P.GableThickness - 30.0, 0.0);
	P.PedestalWidth = FMath::Clamp(P.PedestalWidth, 0.0, MaxPedestal);

	// A support set back further than the desk is deep has nothing left to stand on.
	P.SupportSetback = FMath::Clamp(P.SupportSetback, 0.0, FMath::Max(P.Depth - MinSolid, 0.0));

	P.ModestyPanelHeight = FMath::Clamp(P.ModestyPanelHeight, 0.0, P.TopUnderZ());

	return P;
}

FHFDeskBuild FHFDeskKit::Build(const FHFDeskParams& Params)
{
	FHFDeskBuild Out;

	FHFMeshOps::InitialiseMesh(Out.Shell);
	FHFMeshOps::InitialiseMesh(Out.Top);
	FHFMeshOps::InitialiseMesh(Out.Pedestal);
	FHFMeshOps::InitialiseMesh(Out.Gable);
	FHFMeshOps::InitialiseMesh(Out.ModestyPanel);

	const FHFDeskParams P = Sanitise(Params);
	Out.Used = P;

	if (!P.IsValid())
	{
		return Out;
	}

	const double TopUnderZ = P.TopUnderZ();
	const double BackY = P.SupportDepth();

	// ---------------------------------------------------------------- where the drawer fronts land
	//
	// FLUSH WITH THE FRONT EDGE OF THE TOP, which is what fixes the pedestal's own front plane. A
	// drawer front hangs in front of the carcass it closes - that is the whole geometry of an applied
	// front - so a pedestal built with its carcass on the drawn front line would push its fronts 20 mm
	// out past the worktop above them. On a kitchen run that is right and is what the granite's
	// overhang is measured against; on a desk it is a lip at shin height on the one face somebody sits
	// against.
	FHFDrawerParams Drawer = P.Joinery.Make<FHFDrawerParams>();
	const double FrontFaceOffset = Drawer.BackClearance + Drawer.FrontThickness;
	const double CarcassFrontY = FMath::Min(FrontFaceOffset, FMath::Max(BackY - MinSolid, 0.0));
	const double SupportDepth = BackY - CarcassFrontY;

	// ---------------------------------------------------------------------- which end carries what

	Out.PedestalX0 = P.bPedestalAtRightEnd ? P.Width - P.PedestalWidth : 0.0;
	Out.GableX0 = P.bPedestalAtRightEnd ? 0.0 : P.Width - P.GableThickness;

	// ------------------------------------------------------------------------------------ the top
	//
	// The full drawn depth, so it runs OVER whatever skirting the supports had to stand clear of -
	// which is exactly what a worktop scribed to a wall does. See FHFDeskParams::SupportSetback.

	AppendSolid(Out.Top,
		FVector3d(0.0, 0.0, TopUnderZ),
		FVector3d(P.Width, P.Depth, P.Height),
		EHFSurfaceRole::ShutterLaminate);

	FHFMeshOps::ApplyWorldScaleUVs(Out.Top);

	// ------------------------------------------------------------------------------- the pedestal

	if (P.PedestalWidth > MinSolid && SupportDepth > MinSolid)
	{
		// AN APPLIED END PANEL ON THE KNEE-HOLE SIDE, and the carcass narrowed to take it.
		//
		// That side of a pedestal is on show - it is what somebody sitting at the desk looks straight
		// at, from 400 mm away - and a carcass side board is not a finished surface. Built without one
		// the desk rendered as a laminate top and laminate fronts with a bare ply panel between them,
		// down the middle of the one face the object is used from: a join where no join exists, and
		// exactly the sort of thing that measures perfectly and reads as unfinished.
		//
		// The carcass gives up the thickness rather than the knee hole, for the same reason Sanitise
		// takes it out of the pedestal: the hole is what the desk is for.
		const double EndPanel = FMath::Min(0.6, P.PedestalWidth * 0.25);
		const double CarcassWidth = P.PedestalWidth - EndPanel;

		FHFCarcassParams Carcass = P.Joinery.Make<FHFCarcassParams>();
		Carcass.Width = CarcassWidth;
		Carcass.Depth = SupportDepth;
		Carcass.Height = TopUnderZ;
		Carcass.BayCount = 1;

		// NO TOP BOARD. The worktop is this carcass's top, fixed down onto its sides - the same
		// construction a kitchen base unit has under its granite, and for the same reason: a board
		// under the top would steal 18 mm from the topmost drawer for nothing anybody can see.
		Carcass.bHasTop = false;
		Carcass = FHFJoineryKit::SanitiseCarcass(Carcass);
		Out.PedestalCarcass = Carcass;

		// The panel goes on whichever side faces the knee hole, and the carcass sits behind it.
		const double PanelX0 = P.bPedestalAtRightEnd
			? Out.PedestalX0
			: Out.PedestalX0 + CarcassWidth;
		const double CarcassX0 = P.bPedestalAtRightEnd
			? Out.PedestalX0 + EndPanel
			: Out.PedestalX0;

		const FVector ToPedestal(CarcassX0, CarcassFrontY, 0.0);

		FHFMeshOps::AppendPreservingRoles(Out.Pedestal,
			Placed(FHFJoineryKit::GenerateCarcass(Carcass), ToPedestal));

		AppendSolid(Out.Pedestal,
			FVector3d(PanelX0, CarcassFrontY, 0.0),
			FVector3d(PanelX0 + EndPanel, BackY, TopUnderZ),
			EHFSurfaceRole::ShutterLaminate);

		if (P.DrawerCount > 0)
		{
			FHFDrawerBankParams Bank;
			Bank.Drawer = Drawer;
			Bank.Drawer.ModuleWidth = Carcass.ModuleWidth();
			Bank.Drawer.CarcassDepth = Carcass.BackFaceY();
			Bank.Drawer.CarcassSideThickness = Carcass.BoardThickness;
			Bank.BankHeight = Carcass.Height;
			Bank.DrawerCount = P.DrawerCount;
			Bank.GradationRatio = P.GradationRatio;
			Bank.PartIdPrefix = DrawerPartIdPrefix();

			TArray<FHFMeshPart> BankParts;
			FDynamicMesh3 Channels;
			FHFMeshOps::InitialiseMesh(Channels);

			if (FHFJoineryKit::BuildDrawerBank(Bank, BankParts, &Channels))
			{
				for (FHFMeshPart& Part : BankParts)
				{
					// Into the FRONT's own mesh, in the front's own space. Anywhere else and the handle
					// stays on the carcass while the drawer runs out from under it, which from the
					// front and shut looks exactly like success.
					if (!IsGearedRunner(Part))
					{
						FHFJoineryKit::ApplyHandle(Part.Mesh,
							MakeDrawerHandle(Part.Mesh, Bank.Drawer.FrontThickness, P.HandleStyle));
					}

					Part.PivotTransform = Part.PivotTransform * FTransform(ToPedestal);
					Out.Parts.Add(MoveTemp(Part));
				}

				FHFMeshOps::AppendPreservingRoles(Out.Pedestal, Placed(MoveTemp(Channels), ToPedestal));
			}
		}
	}

	// Unwrapped once, after the carcass, its applied end panel and its runner channels are all in it.
	// The projection is per-polygroup and idempotent, so re-running it over geometry that already
	// carries UVs costs nothing and cannot leave the end panel as the one unwrapped board in the run.
	FHFMeshOps::ApplyWorldScaleUVs(Out.Pedestal);

	// ---------------------------------------------------------------------------------- the gable

	// FACED, NOT CARCASS. A gable is the one board of this desk that is on show from every side - it
	// is the end of the object, standing in the middle of a bedroom floor - so it is finished in the
	// same laminate as the top rather than in the carcass ply that lives behind a drawer front.
	// Tagged JoineryCarcass it rendered as a pair of pale legs under a coloured top, which is a desk
	// nobody makes: measurably correct, and two materials meeting where no join exists.
	AppendSolid(Out.Gable,
		FVector3d(Out.GableX0, CarcassFrontY, 0.0),
		FVector3d(Out.GableX0 + P.GableThickness, BackY, TopUnderZ),
		EHFSurfaceRole::ShutterLaminate);

	FHFMeshOps::ApplyWorldScaleUVs(Out.Gable);

	// -------------------------------------------------------------------------- the modesty panel
	//
	// Hung between the two supports at the BACK, under the top. It is what closes the knee hole in
	// elevation: without it the desk is a trestle and you see straight through it to the skirting.

	if (P.ModestyPanelHeight > MinSolid)
	{
		const double Board = FMath::Max(P.Joinery.CarcassBoardThickness, MinSolid);

		const double InnerX0 = P.bPedestalAtRightEnd
			? Out.GableX0 + P.GableThickness
			: Out.PedestalX0 + P.PedestalWidth;
		const double InnerX1 = P.bPedestalAtRightEnd ? Out.PedestalX0 : Out.GableX0;

		// Faced for the same reason the gable is: it is the surface anybody sitting at this desk looks
		// straight at, through the knee hole, and the only thing behind it is a wall.
		AppendSolid(Out.ModestyPanel,
			FVector3d(InnerX0, FMath::Max(BackY - Board, 0.0), TopUnderZ - P.ModestyPanelHeight),
			FVector3d(InnerX1, BackY, TopUnderZ),
			EHFSurfaceRole::ShutterLaminate);

		FHFMeshOps::ApplyWorldScaleUVs(Out.ModestyPanel);
	}

	// ------------------------------------------------------------------------------------- shell

	FHFMeshOps::AppendPreservingRoles(Out.Shell, Out.Top);
	FHFMeshOps::AppendPreservingRoles(Out.Shell, Out.Pedestal);
	FHFMeshOps::AppendPreservingRoles(Out.Shell, Out.Gable);
	FHFMeshOps::AppendPreservingRoles(Out.Shell, Out.ModestyPanel);

	Out.bValid = Out.Shell.TriangleCount() > 0;
	return Out;
}
