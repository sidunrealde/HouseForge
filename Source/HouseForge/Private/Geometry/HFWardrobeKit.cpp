// Copyright Siddartha G. All Rights Reserved.

#include "Geometry/HFWardrobeKit.h"

#include "DynamicMesh/MeshTransforms.h"
#include "Geometry/HFMeshOps.h"

using namespace UE::Geometry;

namespace
{
	/** Thinner than this is not a board, it is a veneer. */
	constexpr double MinBoardThickness = 0.3;

	/** Placing a mesh in wardrobe space. Translation only; nothing here is ever mirrored. */
	FDynamicMesh3 Placed(FDynamicMesh3&& Mesh, const FVector& Where)
	{
		FDynamicMesh3 Out = MoveTemp(Mesh);
		if (Out.TriangleCount() > 0 && !Where.IsNearlyZero())
		{
			MeshTransforms::Translate(Out, FVector3d(Where));
		}
		return Out;
	}

	/**
	 * The handle for one leaf, in that leaf's own local space.
	 *
	 * Not one line of this is conditional on how the leaf is hung, and that is the point. A leaf of
	 * either hand carries its board on +Y of its pivot, so the face that looks out of the wardrobe is
	 * the plane Y = 0 for both, and the facing is a constant - the same constant a drawer front uses.
	 * What handedness DOES change is which edge the leaf opens from, and the kit is asked rather than
	 * the answer being written out here: a run of shutters is exactly where a hand-derived flip gets
	 * applied to five leaves and forgotten on the sixth.
	 *
	 * Everything else stays at FHFHandleParams' own defaults, which are the standard Indian cabinet
	 * fittings. They are on the actor's parameter struct afterwards for anyone who wants a different
	 * pull on this particular wardrobe.
	 */
	FHFHandleParams MakeLeafHandle(const FHFShutterParams& Leaf, EHFHandleStyle Style)
	{
		FHFHandleParams Handle;
		Handle.Style = Style;
		Handle.PanelBox = FHFJoineryKit::ShutterPanelBox(Leaf);
		Handle.Facing = EHFPanelFacing::NegativeY;
		Handle.Edge = FHFJoineryKit::ShutterLeadingEdge(Leaf);
		return Handle;
	}

	/**
	 * How many leaves close a run of that many bays.
	 *
	 * A HINGED run has one leaf per bay: each is cut to its bay and swings clear of its neighbours,
	 * so the leaves and the boxes behind them are the same set-out.
	 *
	 * A SLIDING run does not, and this is the difference that catches people out. A sliding leaf
	 * passes its neighbour instead of swinging clear, so it must be able to expose the bay next to
	 * it - which means a two-track run is TWO leaves, each closing half the opening, whatever the
	 * carcass behind them is divided into. Four sliding leaves over four bays could never open: every
	 * one of them would have to move somewhere already occupied.
	 */
	int32 LeafCountFor(EHFShutterMotion Motion, int32 Bays)
	{
		return Motion == EHFShutterMotion::Sliding ? 2 : Bays;
	}
}

// ------------------------------------------------------------------------------------ parameters

double FHFWardrobeParams::BoardThickness() const
{
	return FMath::Max(Joinery.CarcassBoardThickness, MinBoardThickness);
}

bool FHFWardrobeParams::IsValid() const
{
	return Width > 0.0 && Depth > 0.0 && BodyHeight() > 2.0 * BoardThickness();
}

FHFWardrobeParams FHFWardrobeKit::Sanitise(const FHFWardrobeParams& Params)
{
	FHFWardrobeParams P = Params;

	P.Width = FMath::Max(P.Width, 0.0);
	P.Depth = FMath::Max(P.Depth, 0.0);
	P.Height = FMath::Max(P.Height, 0.0);
	P.BayCount = P.Bays();
	P.ShelfCount = FMath::Clamp(P.ShelfCount, 0, 30);
	P.CorniceHeight = FMath::Max(P.CorniceHeight, 0.0);
	P.PlinthHeight = FMath::Clamp(P.PlinthHeight, 0.0, P.Height);

	const double Board = P.BoardThickness();

	// A loft that cannot hold anything is not a loft. Below one useful compartment of CLEAR height
	// it is a 24 mm slot with two boards round it, which builds perfectly and is nothing anybody
	// would put a suitcase in - and a wardrobe told to have one is better off with the height in its
	// body. Refused rather than built for the same reason SanitiseShelfStack refuses a rail with no
	// room to hang under it: a caller reading the parameters back can then tell what happened.
	if (P.bHasLoft)
	{
		const double LoftClear = P.LoftHeight - 2.0 * Board;
		const double RemainingBody = P.Height - P.PlinthHeight - P.LoftHeight;

		if (LoftClear < FHFJoineryKit::MinUsefulCompartment
			|| RemainingBody < FHFJoineryKit::MinUsefulCompartment + 2.0 * Board)
		{
			P.bHasLoft = false;
		}
	}

	if (!P.bHasLoft)
	{
		P.LoftHeight = 0.0;
	}

	// One leaf has nothing to slide over. A single-leaf sliding run is a leaf that can never expose
	// the bay behind it, so it is hung instead - which is what a one-door wardrobe actually is.
	if (P.MotionKind == EHFShutterMotion::Sliding && P.Bays() < 2)
	{
		P.MotionKind = EHFShutterMotion::SideHung;
	}
	if (P.LoftMotionKind == EHFShutterMotion::Sliding && P.Bays() < 2)
	{
		P.LoftMotionKind = EHFShutterMotion::SideHung;
	}

	return P;
}

// ---------------------------------------------------------------------------------------- parts

FName FHFWardrobeKit::ShutterPartId(int32 Bay)
{
	return FName(*FString::Printf(TEXT("Shutter%d"), FMath::Max(Bay, 0)));
}

FName FHFWardrobeKit::LoftPartId(int32 Bay)
{
	return FName(*FString::Printf(TEXT("Loft%d"), FMath::Max(Bay, 0)));
}

int32 FHFWardrobeKit::ShelvesOverHangingRail(double ClearHeight, double ShelfThickness, double RailDrop,
	double RequiredClearance, int32 MaxCount)
{
	if (ClearHeight <= 0.0)
	{
		return 0;
	}

	const double Thickness = FMath::Max(ShelfThickness, MinBoardThickness);
	const double Needed = FMath::Max(RequiredClearance, 0.0) + FMath::Max(RailDrop, 0.0);

	// Downward, so the answer is the MOST shelves that still leave room to hang rather than the
	// fewest. A wardrobe wants both, and the clearance is the constraint rather than the goal.
	for (int32 Count = FMath::Max(MaxCount, 0); Count > 0; --Count)
	{
		const double Compartment = (ClearHeight - Count * Thickness) / static_cast<double>(Count + 1);
		if (Compartment >= Needed)
		{
			return Count;
		}
	}

	return 0;
}

// ---------------------------------------------------------------------------------------- build

FHFWardrobeBuild FHFWardrobeKit::Build(const FHFWardrobeParams& Params)
{
	FHFWardrobeBuild Out;

	FHFMeshOps::InitialiseMesh(Out.Shell);
	FHFMeshOps::InitialiseMesh(Out.Carcass);
	FHFMeshOps::InitialiseMesh(Out.Loft);
	FHFMeshOps::InitialiseMesh(Out.Plinth);
	FHFMeshOps::InitialiseMesh(Out.Cornice);

	const FHFWardrobeParams P = Sanitise(Params);
	if (!P.IsValid())
	{
		return Out;
	}

	const int32 Bays = P.Bays();
	const double Board = P.BoardThickness();
	const double Module = P.ModuleWidth();

	// ---------------------------------------------------------------------------------- leaves
	//
	// Set out first, before anything they hang on is generated: the shutter face plane is what the
	// toe kick and the cornice projection are both measured from, and both of those are behind it.

	FHFShutterParams BodyLeaf = P.Joinery.Make<FHFShutterParams>();
	BodyLeaf.MotionKind = P.MotionKind;
	BodyLeaf.ModuleHeight = P.BodyHeight();
	BodyLeaf.ModuleWidth = P.Width / static_cast<double>(LeafCountFor(P.MotionKind, Bays));
	BodyLeaf.bGlassInsert = P.bGlassInsert;

	FHFShutterParams LoftLeaf = P.Joinery.Make<FHFShutterParams>();
	LoftLeaf.MotionKind = P.LoftMotionKind;
	LoftLeaf.ModuleHeight = P.BuiltLoftHeight();
	LoftLeaf.ModuleWidth = P.Width / static_cast<double>(LeafCountFor(P.LoftMotionKind, Bays));
	LoftLeaf.bGlassInsert = P.bGlassInsert;

	Out.ShutterParams = BodyLeaf;

	// Leaves in one pass, body then loft, so their part ids and their order are fixed by
	// construction rather than by two loops that could drift apart.
	auto AppendLeaves = [&](const FHFShutterParams& Template, double BaseZ, bool bLoft)
	{
		const int32 Count = LeafCountFor(Template.MotionKind, Bays);
		const double LeafModule = P.Width / static_cast<double>(Count);

		for (int32 Index = 0; Index < Count; ++Index)
		{
			FHFShutterParams Leaf = Template;
			Leaf.ModuleWidth = LeafModule;

			// A pair opening from the middle out, which is how a run of wardrobe leaves is hung: the
			// leading edges meet, so one handle sits either side of the same shadow line. A slider
			// takes the same alternation, one leaf per track, so the pair laps in the middle and each
			// runs towards the other's bay.
			Leaf.Hinge = (Index % 2 == 0) ? EHFShutterHinge::Left : EHFShutterHinge::Right;
			if (Leaf.IsSliding())
			{
				Leaf.Track = Index % 2;
			}

			const FName PartId = bLoft ? LoftPartId(Index) : ShutterPartId(Index);
			FHFMeshPart Part = FHFJoineryKit::BuildShutterPart(Leaf, PartId);
			if (Part.Mesh.TriangleCount() == 0)
			{
				continue;
			}

			// Into the LEAF's own mesh, in the leaf's own space. Anywhere else and the handle stays
			// on the carcass when the leaf swings - which, closed and seen from the front, looks
			// exactly like success.
			FHFJoineryKit::ApplyHandle(Part.Mesh, MakeLeafHandle(Leaf, P.HandleStyle));

			Part.PivotTransform = FHFJoineryKit::ShutterPivotTransform(Leaf)
				* FTransform(FVector(Index * LeafModule, 0.0, BaseZ));

			Out.ShutterFaceY = FMath::Min(Out.ShutterFaceY, -Leaf.FaceOffset());
			Out.Parts.Add(MoveTemp(Part));
		}
	};

	AppendLeaves(BodyLeaf, P.BodyBottomZ(), /*bLoft*/ false);
	if (P.bHasLoft)
	{
		AppendLeaves(LoftLeaf, P.BodyTopZ(), /*bLoft*/ true);
	}

	// ---------------------------------------------------------------------------------- plinth

	Out.PlinthParams = P.Joinery.Make<FHFPlinthParams>();
	Out.PlinthParams.Width = P.Width;
	Out.PlinthParams.Depth = P.Depth;
	Out.PlinthParams.Height = P.PlinthHeight;
	Out.PlinthParams.bLeftEndExposed = P.bLeftEndExposed;
	Out.PlinthParams.bRightEndExposed = P.bRightEndExposed;

	// The kick is specified from the SHUTTER face, which is where anyone standing in front of the
	// wardrobe measures it from - so the plinth is told how far in front of the carcass the leaves
	// hang. Leaving this at zero puts the panel a kick behind the CARCASS instead, and the recess you
	// can actually see comes out a leaf's thickness deeper than the one that was asked for.
	Out.PlinthParams.ShutterOverlay = -Out.ShutterFaceY;

	Out.Plinth = FHFJoineryKit::GeneratePlinth(Out.PlinthParams);

	// ---------------------------------------------------------------------------------- carcass

	Out.CarcassParams = P.Joinery.Make<FHFCarcassParams>();
	Out.CarcassParams.Width = P.Width;
	Out.CarcassParams.Depth = P.Depth;
	Out.CarcassParams.Height = P.BodyHeight();
	Out.CarcassParams.BayCount = Bays;
	Out.CarcassParams.bHasBack = true;

	Out.Carcass = Placed(FHFJoineryKit::GenerateCarcass(Out.CarcassParams),
		FVector(0.0, 0.0, P.BodyBottomZ()));

	if (P.bHasLoft)
	{
		Out.LoftParams = Out.CarcassParams;
		Out.LoftParams.Height = P.BuiltLoftHeight();

		Out.Loft = Placed(FHFJoineryKit::GenerateCarcass(Out.LoftParams),
			FVector(0.0, 0.0, P.BodyTopZ()));
	}

	// ---------------------------------------------------------------------------------- shelves
	//
	// One stack per bay of the body, sitting in that bay's clear volume with a fitting gap all
	// round. The right-hand bays hang and the rest are shelved out, which is what half of every
	// Indian wardrobe is.

	Out.HangingBayCount = P.bHangingRail ? FMath::Max(1, Bays / 2) : 0;

	const FHFShelfMaterialFigures Figures = P.Joinery.ShelfFigures();

	for (int32 Bay = 0; Bay < Bays; ++Bay)
	{
		const FBox Clear = FHFJoineryKit::CarcassBayClearVolume(Out.CarcassParams, Bay);
		if (Clear.IsValid == 0)
		{
			continue;
		}

		FHFShelfStackParams Stack = P.Joinery.Make<FHFShelfStackParams>();
		Stack.Width = (Clear.Max.X - Clear.Min.X) - 2.0 * ShelfEndGap;
		Stack.Depth = Clear.Max.Y;
		Stack.Height = Clear.Max.Z - Clear.Min.Z;
		Stack.BackClearance = ShelfEndGap;
		Stack.ShelfMaterial = EHFShelfMaterial::Ply;
		Stack.bMidPartitionWhenOverspan = true;

		// The count this bay would get if it were shelved out. Asked of the PROJECT rather than of
		// the kit, so the settings page's spacing and minimum compartment actually decide it.
		const int32 ShelvedCount = P.ShelfCount > 0
			? P.ShelfCount
			: P.Joinery.ShelfCountFor(Stack.Height);

		if (Bay >= Bays - Out.HangingBayCount)
		{
			Stack.bHangingRail = true;
			Stack.ShelfCount = ShelvesOverHangingRail(Stack.Height, Figures.ThicknessFor(Stack.ShelfMaterial),
				Stack.RailDrop, Stack.MinHangingClearance, ShelvedCount);
		}
		else
		{
			Stack.ShelfCount = ShelvedCount;
		}

		Out.ShelfParams.Add(FHFJoineryKit::SanitiseShelfStack(Stack, Figures));
		Out.Shelves.Add(Placed(FHFJoineryKit::GenerateShelfStack(Stack, Figures),
			FVector(Clear.Min.X + ShelfEndGap, 0.0, P.BodyBottomZ() + Clear.Min.Z)));
	}

	// ---------------------------------------------------------------------------------- cornice
	//
	// Anchored on the shutter face plane at the top of the topmost carcass, which is what the
	// cornice's own local space is defined against - so the moulding projects from the leaves it caps
	// rather than from the carcass behind them.

	if (P.CorniceHeight > 0.0)
	{
		Out.CorniceParams = P.Joinery.Make<FHFCorniceParams>();
		Out.CorniceParams.Width = P.Width;
		Out.CorniceParams.Height = P.CorniceHeight;

		FHFJoineryKit::AppendCornice(Out.Cornice, Out.CorniceParams,
			FTransform(FVector(0.0, Out.ShutterFaceY, P.Height)));

		// AppendCornice deliberately leaves UVs alone so a caller can place several runs and unwrap
		// once. This is that once.
		FHFMeshOps::ApplyWorldScaleUVs(Out.Cornice);
	}

	// ------------------------------------------------------------------------------------ shell

	FHFMeshOps::AppendPreservingRoles(Out.Shell, Out.Carcass);
	FHFMeshOps::AppendPreservingRoles(Out.Shell, Out.Loft);
	FHFMeshOps::AppendPreservingRoles(Out.Shell, Out.Plinth);
	FHFMeshOps::AppendPreservingRoles(Out.Shell, Out.Cornice);
	for (const FDynamicMesh3& Stack : Out.Shelves)
	{
		FHFMeshOps::AppendPreservingRoles(Out.Shell, Stack);
	}

	Out.bValid = Out.Shell.TriangleCount() > 0;
	return Out;
}
