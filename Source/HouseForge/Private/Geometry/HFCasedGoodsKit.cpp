// Copyright Siddartha G. All Rights Reserved.

#include "Geometry/HFCasedGoodsKit.h"

#include "DynamicMesh/MeshTransforms.h"
#include "Geometry/HFMeshOps.h"
#include "Geometry/HFWardrobeKit.h"

using namespace UE::Geometry;

namespace
{
	/** Thinner than this is not a board, it is a veneer. */
	constexpr double MinBoardThickness = 0.3;

	/** Below this a unit in a stack is not a box, it is a gap. */
	constexpr double MinUnitHeight = 1.0;

	/**
	 * How far a stone top stands proud of the shutter face it covers.
	 *
	 * A slab flush with the doors is a slab you cannot see the edge of, and the drip that runs off it
	 * runs straight down the door faces. 20-30 mm is what a fabricator leaves, and the figure is here
	 * rather than on the params because it is a property of the JOINT between stone and joinery,
	 * exactly as FHFCounterKit::ApertureRimLap is a property of the joint between stone and appliance.
	 */
	constexpr double TopOversail = 2.0;

	/** Placing a mesh in unit space. Translation only; nothing here is ever mirrored. */
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
	 * Not one line of it is conditional on how the leaf is hung, and that is the point - see the same
	 * function in HFWardrobeKit.cpp. What handedness DOES change is which edge the leaf opens from,
	 * and the kit is asked rather than the answer being written out here.
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
	 * The handle for one drawer front, read off the part's own mesh.
	 *
	 * MEASURED RATHER THAN RE-DERIVED. A bank graduates its fronts - 15/25/30 in a 720 - so each
	 * front is a different height, and the only place those heights exist is inside
	 * FHFJoineryKit::BuildDrawerBank. Working them out a second time here would be the drawer
	 * ladder implemented twice, and the two copies would drift the first time either changed.
	 *
	 * The front is the extreme of the part on all three axes that matter: it is wider and taller than
	 * the box behind it, and it is the frontmost thing in the drawer. So the front's board is exactly
	 * the first FrontThickness of the part's bounds at its minimum Y, which is the face that looks out
	 * of the cabinet.
	 */
	FHFHandleParams MakeDrawerHandle(const FDynamicMesh3& PartMesh, double FrontThickness,
		EHFHandleStyle Style)
	{
		const FAxisAlignedBox3d Bounds = PartMesh.GetBounds();

		FHFHandleParams Handle;
		Handle.Style = Style;
		Handle.PanelBox = FBox(
			FVector(Bounds.Min.X, Bounds.Min.Y, Bounds.Min.Z),
			FVector(Bounds.Max.X, Bounds.Min.Y + FMath::Max(FrontThickness, MinBoardThickness), Bounds.Max.Z));
		Handle.Facing = EHFPanelFacing::NegativeY;

		// The TOP edge, which is where a drawer pull goes: a bar runs parallel to the edge it serves,
		// so this is the horizontal pull a drawer has rather than the vertical one a door has. It is
		// also the edge a J-profile is routed into, which is what makes a bank read as one continuous
		// shadow gap down the run.
		Handle.Edge = EHFHandleEdge::Top;
		return Handle;
	}

	/** True for the part FHFJoineryKit::BuildDrawerBank emits to carry a drawer rather than to be one. */
	bool IsGearedRunner(const FHFMeshPart& Part)
	{
		return !Part.Motion.DrivenByPartId.IsNone();
	}

	/**
	 * Leaves closing a bay of this width, when the drawing did not count them.
	 *
	 * Rounded rather than divided up, so a bay within half a module of the project's figure gets one
	 * leaf: a 600 wall unit is one 600 door and not two 300 ones. Capped at two - see
	 * FHFCaseBay::LeafCount.
	 */
	int32 LeafCountFor(double BayWidth, double ModuleWidth)
	{
		const double Module = FMath::Max(ModuleWidth, 1.0);
		return FMath::Clamp(FMath::RoundToInt32(BayWidth / Module), 1, 2);
	}
}

// ------------------------------------------------------------------------------------ parameters

double FHFCasedGoodsParams::BoardThickness() const
{
	return FMath::Max(Joinery.CarcassBoardThickness, MinBoardThickness);
}

double FHFCasedGoodsParams::MountHeight() const
{
	return Mount == EHFCaseMount::WallHung ? 0.0 : FMath::Max(PlinthHeight, 0.0);
}

bool FHFCasedGoodsParams::IsValid() const
{
	return Width > 0.0 && Depth > 0.0 && StackHeight() > MinUnitHeight;
}

FHFCasedGoodsParams FHFCasedGoodsKit::Sanitise(const FHFCasedGoodsParams& Params)
{
	FHFCasedGoodsParams P = Params;

	P.Width = FMath::Max(P.Width, 0.0);
	P.Depth = FMath::Max(P.Depth, 0.0);
	P.Height = FMath::Max(P.Height, 0.0);
	P.CorniceHeight = FMath::Max(P.CorniceHeight, 0.0);

	// The top comes OUT of the run's height rather than being added to it - see
	// FHFCasedGoodsParams::TopThickness - so it may not take more of the height than there is. Clamped
	// to half, because a slab thicker than the carcass under it is not a run with a top on it.
	P.TopThickness = FMath::Clamp(P.TopThickness, 0.0, P.Height * 0.5);
	P.TopUpstandHeight = FMath::Max(P.TopUpstandHeight, 0.0);

	// A WALL-HUNG UNIT HAS NOTHING UNDER IT, and that is not the same as a plinth of zero height: the
	// skirting runs underneath one and dies into the other. Cleared here so a caller that set both
	// cannot end up with a toe kick hanging in the air at 1400 above the floor.
	if (P.Mount == EHFCaseMount::WallHung)
	{
		P.PlinthHeight = 0.0;
	}
	else
	{
		// Zero means "whatever this project builds a plinth at", resolved here rather than at the call
		// site - the same sentinel a wardrobe and a shelf stack use, and for the same reason.
		if (P.PlinthHeight <= 0.0)
		{
			P.PlinthHeight = P.Joinery.PlinthHeight;
		}
		P.PlinthHeight = FMath::Clamp(P.PlinthHeight, 0.0, P.Height);
	}

	if (P.Units.IsEmpty())
	{
		P.Units.Add(FHFCaseUnit());
	}

	// ------------------------------------------------------------------- how tall each box comes out
	//
	// Explicit heights are honoured in order and clamped to what is left, so a stack whose declared
	// boxes overrun the run loses the topmost rather than building a box hanging over the top. What
	// remains is shared equally between the boxes that asked for nothing, which is what "the body
	// fills whatever the loft above it left" means.

	const double Stack = FMath::Max(P.StackHeight(), 0.0);

	double Declared = 0.0;
	int32 Unstated = 0;
	for (const FHFCaseUnit& Unit : P.Units)
	{
		if (Unit.Height > 0.0)
		{
			Declared += Unit.Height;
		}
		else
		{
			++Unstated;
		}
	}

	const double Spare = FMath::Max(Stack - Declared, 0.0);
	const double PerUnstated = Unstated > 0 ? Spare / static_cast<double>(Unstated) : 0.0;

	double Remaining = Stack;
	for (FHFCaseUnit& Unit : P.Units)
	{
		const double Wanted = Unit.Height > 0.0 ? Unit.Height : PerUnstated;
		Unit.Height = FMath::Clamp(Wanted, 0.0, Remaining);
		Remaining -= Unit.Height;
	}

	P.Units.RemoveAll([](const FHFCaseUnit& Unit) { return Unit.Height <= MinUnitHeight; });
	if (P.Units.IsEmpty())
	{
		return P;
	}

	// ------------------------------------------------------------------------------ bays and fronts

	for (FHFCaseUnit& Unit : P.Units)
	{
		if (Unit.BayCount <= 0)
		{
			Unit.BayCount = FMath::RoundToInt32(P.Width / FMath::Max(P.Joinery.ShutterModuleWidth, 1.0));
		}
		Unit.BayCount = FMath::Clamp(Unit.BayCount, 1, 12);

		// Empty gives every bay the default; one entry is applied to all; a short list is padded with
		// its last rather than refused, so "the first bay is drawers and the rest are doors" is two.
		if (Unit.Bays.IsEmpty())
		{
			Unit.Bays.Add(FHFCaseBay());
		}
		// COPIED OUT OF THE ARRAY BEFORE ANYTHING IS ADDED TO IT. Add(Bays.Last()) hands TArray a
		// reference INTO its own storage: if the append reallocates, the element is read from a buffer
		// that has just been freed, and the padded bays come out as whatever is now at that address.
		// UE 5.8 checks for it and fails the assertion outright, which is how this was found - by a
		// bare parameter struct that stated fewer bays than the module width divides the run into.
		//
		// Every fixture in the flat happens to state exactly as many bays as it has, so nothing has
		// ever reached this loop; it would have fired the first time anybody hand-edited a bay list.
		if (Unit.Bays.Num() < Unit.BayCount)
		{
			const FHFCaseBay Pattern = Unit.Bays.Last();
			while (Unit.Bays.Num() < Unit.BayCount)
			{
				Unit.Bays.Add(Pattern);
			}
		}
		Unit.Bays.SetNum(Unit.BayCount);

		const double BayWidth = P.Width / static_cast<double>(Unit.BayCount);

		for (FHFCaseBay& Bay : Unit.Bays)
		{
			// A SLIDING RUN IS NOT A CASED GOOD. Two leaves on two tracks whatever the carcass behind
			// them is divided into, each set out from its own jamb and each running towards the
			// other's bay - and driven by one amount they simply exchange tracks and the run never
			// opens. The rule that keeps that out is the pairing in FHFWardrobeKit, and a cabinet
			// quietly building a sliding pair without it would report full travel on both leaves and
			// open by nothing. Side-hung is what every cased good in this catalogue actually is.
			if (Bay.Motion == EHFShutterMotion::Sliding)
			{
				Bay.Motion = EHFShutterMotion::SideHung;
			}

			if (Bay.LeafCount <= 0)
			{
				Bay.LeafCount = LeafCountFor(BayWidth, P.Joinery.ShutterModuleWidth);
			}

			// A flap is one panel on a pair of stays whichever edge it turns about, which is exactly
			// what makes a 900 lift-up - or a 600 tilt-out - ordinary where a 900 side-hung leaf is
			// not. Two half-width leaves on one horizontal hinge line is not something anybody builds.
			if (Bay.Motion == EHFShutterMotion::TopHung || Bay.Motion == EHFShutterMotion::BottomHung)
			{
				Bay.LeafCount = 1;
			}

			Bay.LeafCount = FMath::Clamp(Bay.LeafCount, 1, 2);
			Bay.DrawerCount = FMath::Clamp(Bay.DrawerCount, 0, 12);
			Bay.GradationRatio = FMath::Max(Bay.GradationRatio, 1.0);
			Bay.ShelfCount = FMath::Clamp(Bay.ShelfCount, 0, 30);

			if (Bay.Front == EHFCaseFront::ShutterOverDrawer && Bay.DrawerBandHeight <= 0.0)
			{
				Bay.DrawerBandHeight = Unit.Height / 3.0;
			}
			Bay.DrawerBandHeight = FMath::Clamp(Bay.DrawerBandHeight, 0.0, Unit.Height);
		}
	}

	// An applied handle cannot go on a lift-up flap's leading edge without standing proud of the run's
	// underside, which on a wall unit at 1400 is at eye level and in the way of the counter below. The
	// wardrobe refuses a bar on a slider for the same measurable reason; this one is left as a choice,
	// because a bar on the bottom rail of a lift-up IS what those are sold with.

	return P;
}

// ---------------------------------------------------------------------------------------- parts

FName FHFCasedGoodsKit::ShutterPartId(int32 Unit, int32 Bay, int32 Leaf)
{
	return FName(*FString::Printf(TEXT("Shutter_%d_%d_%d"),
		FMath::Max(Unit, 0), FMath::Max(Bay, 0), FMath::Max(Leaf, 0)));
}

FName FHFCasedGoodsKit::DrawerPartIdPrefix(int32 Unit, int32 Bay)
{
	return FName(*FString::Printf(TEXT("Drawer_%d_%d_"), FMath::Max(Unit, 0), FMath::Max(Bay, 0)));
}

// ---------------------------------------------------------------------------------------- build

FHFCasedGoodsBuild FHFCasedGoodsKit::Build(const FHFCasedGoodsParams& Params)
{
	FHFCasedGoodsBuild Out;

	FHFMeshOps::InitialiseMesh(Out.Shell);
	FHFMeshOps::InitialiseMesh(Out.Plinth);
	FHFMeshOps::InitialiseMesh(Out.Cornice);
	FHFMeshOps::InitialiseMesh(Out.Interior);
	FHFMeshOps::InitialiseMesh(Out.Top);

	const FHFCasedGoodsParams P = Sanitise(Params);
	Out.Used = P;

	if (!P.IsValid() || P.Units.IsEmpty())
	{
		return Out;
	}

	const double Board = P.BoardThickness();
	const FHFShelfMaterialFigures Figures = P.Joinery.ShelfFigures();

	double UnitZ = P.BodyBottomZ();

	for (int32 UnitIndex = 0; UnitIndex < P.Units.Num(); ++UnitIndex)
	{
		const FHFCaseUnit& Unit = P.Units[UnitIndex];

		FHFCarcassParams Carcass = P.Joinery.Make<FHFCarcassParams>();
		Carcass.Width = P.Width;
		Carcass.Depth = P.Depth;
		Carcass.Height = Unit.Height;
		Carcass.BayCount = Unit.BayCount;
		Carcass.bHasBack = Unit.bHasBack;
		Carcass.bHasTop = Unit.bHasTop;
		Carcass = FHFJoineryKit::SanitiseCarcass(Carcass);

		Out.UnitParams.Add(Carcass);
		Out.UnitBaseZ.Add(UnitZ);
		Out.Carcasses.Add(Placed(FHFJoineryKit::GenerateCarcass(Carcass), FVector(0.0, 0.0, UnitZ)));

		const double Module = Carcass.ModuleWidth();

		for (int32 BayIndex = 0; BayIndex < Unit.BayCount; ++BayIndex)
		{
			const FHFCaseBay& Bay = Unit.Bays[BayIndex];
			const double BayX = BayIndex * Module;

			// Where the drawers stop and the shutter over them starts. Zero for a bay with no band.
			double BandHeight = 0.0;
			if (Bay.Front == EHFCaseFront::DrawerBank)
			{
				BandHeight = Unit.Height;
			}
			else if (Bay.Front == EHFCaseFront::ShutterOverDrawer)
			{
				BandHeight = Bay.DrawerBandHeight;
			}

			// ------------------------------------------------------------------------ the drawers
			//
			// Built before the leaf over them, because a bank that cannot be graduated falls back to a
			// leaf filling the whole bay - an honest cupboard rather than a hole where three drawers
			// were asked for. GraduateDrawerFronts refuses both ways: too many fronts for the height,
			// and too few for it.

			bool bBankBuilt = false;

			if (BandHeight > 0.0 && Bay.DrawerCount > 0)
			{
				FHFDrawerBankParams Bank;
				Bank.Drawer = P.Joinery.Make<FHFDrawerParams>();
				Bank.Drawer.ModuleWidth = Module;
				Bank.Drawer.CarcassDepth = Carcass.BackFaceY();
				Bank.Drawer.CarcassSideThickness = Board;
				Bank.BankHeight = BandHeight;
				Bank.DrawerCount = Bay.DrawerCount;
				Bank.GradationRatio = Bay.GradationRatio;
				Bank.PartIdPrefix = DrawerPartIdPrefix(UnitIndex, BayIndex);

				TArray<FHFMeshPart> BankParts;
				FDynamicMesh3 Channels;
				FHFMeshOps::InitialiseMesh(Channels);

				if (FHFJoineryKit::BuildDrawerBank(Bank, BankParts, &Channels))
				{
					const FVector ToBay(BayX, 0.0, UnitZ);

					for (FHFMeshPart& Part : BankParts)
					{
						// Into the FRONT's own mesh, in the front's own space. Anywhere else and the
						// handle stays on the carcass while the drawer runs out from under it, which
						// from the front and shut looks exactly like success.
						if (!IsGearedRunner(Part))
						{
							FHFJoineryKit::ApplyHandle(Part.Mesh,
								MakeDrawerHandle(Part.Mesh, Bank.Drawer.FrontThickness, P.HandleStyle));

							Out.ShutterFaceY = FMath::Min(Out.ShutterFaceY,
								-(Bank.Drawer.BackClearance + Bank.Drawer.FrontThickness));
						}

						Part.PivotTransform = Part.PivotTransform * FTransform(ToBay);
						Out.Parts.Add(MoveTemp(Part));
					}

					FHFMeshOps::AppendPreservingRoles(Out.Interior, Placed(MoveTemp(Channels), ToBay));
					bBankBuilt = true;
				}
			}

			// -------------------------------------------------------------------------- the leaves

			const double LeafBaseZ = UnitZ + (bBankBuilt ? BandHeight : 0.0);
			const double LeafHeight = Unit.Height - (bBankBuilt ? BandHeight : 0.0);

			const bool bWantsLeaf = Bay.Front == EHFCaseFront::Shutter
				|| Bay.Front == EHFCaseFront::ShutterOverDrawer
				|| (Bay.Front == EHFCaseFront::DrawerBank && !bBankBuilt);

			if (bWantsLeaf && LeafHeight > 0.0)
			{
				const int32 Leaves = FMath::Clamp(Bay.LeafCount, 1, 2);
				const double LeafModule = Module / static_cast<double>(Leaves);

				for (int32 LeafIndex = 0; LeafIndex < Leaves; ++LeafIndex)
				{
					FHFShutterParams Leaf = P.Joinery.Make<FHFShutterParams>();
					Leaf.MotionKind = Bay.Motion;
					Leaf.ModuleWidth = LeafModule;
					Leaf.ModuleHeight = LeafHeight;
					Leaf.bGlassInsert = Bay.bGlassInsert;

					// A FLAP UNDER A CORNICE CANNOT GO PAST SQUARE, and the geometry says so exactly.
					// A top-hung leaf turns about the axis at the head of its own outer face, so at
					// any angle past 90 the part of it still near the hinge has risen ABOVE that axis
					// while still lying within the cornice's projection - 4 mm of leaf inside the
					// moulding at the kit's 100 degrees and its 25 mm projection. It is a few
					// millimetres, it is at eye level on a wall unit, and it only appears once
					// somebody opens the flap. Square minus a running clearance clears it at every
					// projection, because at 90 the whole leaf lies at or below the hinge plane.
					if (Leaf.IsTopHung() && P.CorniceHeight > 0.0)
					{
						Leaf.OpenAngleDegrees = FMath::Min(Leaf.OpenAngleDegrees, 88.0);
					}

					// A TILT-OUT STOPS SOMEWHERE ELSE ENTIRELY. Every other leaf in this kit opens to
					// the project's 100 degrees because a concealed hinge does; a tilt-out hangs on a
					// stay that stops it at about 68, because past that the compartment tips its
					// contents onto the floor. Resolved here rather than in ApplyTo because ApplyTo
					// stamps a shutter before anything knows how the bay moves - and a shoe rack whose
					// flaps swung to 100 would lie flat out into the foyer, which measures as four
					// perfectly good hinges and reads as a wrecked cabinet.
					if (Leaf.IsBottomHung())
					{
						Leaf.OpenAngleDegrees = P.Joinery.TiltOutFlapAngleDegrees;
					}

					// A PAIR OPENS FROM THE MIDDLE OUT, which is how a double base unit is hung: the
					// leading edges meet, so one handle sits either side of the same shadow line.
					// A run of single-leaf bays alternates the same way, bay by bay, so the doors of
					// adjacent bays meet rather than all swinging the same way.
					const int32 Alternation = Leaves == 2 ? LeafIndex : BayIndex;
					Leaf.Hinge = (Alternation % 2 == 0) ? EHFShutterHinge::Left : EHFShutterHinge::Right;

					const FName PartId = ShutterPartId(UnitIndex, BayIndex, LeafIndex);
					FHFMeshPart Part = FHFJoineryKit::BuildShutterPart(Leaf, PartId);
					if (Part.Mesh.TriangleCount() == 0)
					{
						continue;
					}

					FHFJoineryKit::ApplyHandle(Part.Mesh, MakeLeafHandle(Leaf, P.HandleStyle));

					Part.PivotTransform = FHFJoineryKit::ShutterPivotTransform(Leaf)
						* FTransform(FVector(BayX + LeafIndex * LeafModule, 0.0, LeafBaseZ));

					Out.ShutterFaceY = FMath::Min(Out.ShutterFaceY, -Leaf.FaceOffset());
					Out.Parts.Add(MoveTemp(Part));
				}
			}

			// ------------------------------------------------------------------------- the interior
			//
			// Only what is left above a drawer band: a bay full of drawers has its clear volume taken
			// by the boxes, and a shelf in there would be a shelf through a drawer.

			if (Bay.Interior == EHFCaseInterior::None || (bBankBuilt && BandHeight >= Unit.Height))
			{
				continue;
			}

			const FBox Clear = FHFJoineryKit::CarcassBayClearVolume(Carcass, BayIndex);
			if (Clear.IsValid == 0)
			{
				continue;
			}

			const double StackBottomZ = FMath::Max(Clear.Min.Z, bBankBuilt ? BandHeight : 0.0);
			const double StackHeight = Clear.Max.Z - StackBottomZ;
			if (StackHeight <= UE_KINDA_SMALL_NUMBER)
			{
				continue;
			}

			FHFShelfStackParams Stack = P.Joinery.Make<FHFShelfStackParams>();
			Stack.Width = (Clear.Max.X - Clear.Min.X) - 2.0 * ShelfEndGap;
			Stack.Depth = Clear.Max.Y;
			Stack.Height = StackHeight;
			Stack.BackClearance = ShelfEndGap;
			Stack.ShelfMaterial = Bay.ShelfMaterial;
			Stack.bMidPartitionWhenOverspan = true;

			// The count this bay would get if it were shelved out. Asked of the PROJECT rather than of
			// the kit, so the settings page's spacing and minimum compartment actually decide it.
			const int32 ShelvedCount = Bay.ShelfCount > 0
				? Bay.ShelfCount
				: P.Joinery.ShelfCountFor(Stack.Height, Figures.ThicknessFor(Stack.ShelfMaterial));

			if (Bay.Interior == EHFCaseInterior::ShelvesAndRail)
			{
				Stack.bHangingRail = true;

				// THE SHARED RULE, ASKED FOR RATHER THAN COPIED. "The most shelves whose top
				// compartment still clears the rail" is a property of hanging, not of wardrobes, and
				// it is the difference between a bay that is geometrically perfect and one somebody
				// can hang a shirt in. It lives on FHFWardrobeKit because that is where it was first
				// needed; when the wardrobe is re-expressed in terms of this kit the rule comes down
				// with it. Two copies of it would drift the first time either changed.
				Stack.ShelfCount = FHFWardrobeKit::ShelvesOverHangingRail(Stack.Height,
					Figures.ThicknessFor(Stack.ShelfMaterial), Stack.RailDrop,
					Stack.MinHangingClearance, ShelvedCount);
			}
			else
			{
				Stack.ShelfCount = ShelvedCount;
			}

			FHFMeshOps::AppendPreservingRoles(Out.Interior,
				Placed(FHFJoineryKit::GenerateShelfStack(Stack, Figures),
					FVector(Clear.Min.X + ShelfEndGap, 0.0, UnitZ + StackBottomZ)));
		}

		UnitZ += Unit.Height;
	}

	Out.CarcassTopZ = UnitZ;

	// ------------------------------------------------------------------------------------- plinth
	//
	// After the fronts, because the toe kick is specified from the SHUTTER face - the plane somebody
	// standing in front of the run actually measures from - and that face is not known until the
	// leaves have been set out. Left at zero the recess comes out a leaf's thickness deeper than the
	// one that was asked for, and nothing says so.

	if (P.Mount == EHFCaseMount::Plinth && P.PlinthHeight > 0.0)
	{
		Out.PlinthParams = P.Joinery.Make<FHFPlinthParams>();
		Out.PlinthParams.Width = P.Width;
		Out.PlinthParams.Depth = P.Depth;
		Out.PlinthParams.Height = P.PlinthHeight;
		Out.PlinthParams.ShutterOverlay = -Out.ShutterFaceY;
		Out.PlinthParams.bLeftEndExposed = P.bLeftEndExposed;
		Out.PlinthParams.bRightEndExposed = P.bRightEndExposed;

		Out.Plinth = FHFJoineryKit::GeneratePlinth(Out.PlinthParams);
	}

	// ------------------------------------------------------------------------------------ cornice

	if (P.CorniceHeight > 0.0)
	{
		Out.CorniceParams = P.Joinery.Make<FHFCorniceParams>();
		Out.CorniceParams.Width = P.Width;
		Out.CorniceParams.Height = P.CorniceHeight;

		FHFJoineryKit::AppendCornice(Out.Cornice, Out.CorniceParams,
			FTransform(FVector(0.0, Out.ShutterFaceY, Out.CarcassTopZ)));

		// AppendCornice deliberately leaves UVs alone so a caller can place several runs and unwrap
		// once. This is that once.
		FHFMeshOps::ApplyWorldScaleUVs(Out.Cornice);
	}

	// ---------------------------------------------------------------------------------------- top
	//
	// After the fronts, for exactly the reason the plinth is: the OVERHANG is measured past the
	// shutter face and not past the carcass, and that face is not known until the leaves have been set
	// out. A slab flush with the carcass finishes BEHIND the doors it covers, which is the one
	// arrangement that exists nowhere and reads as wrong immediately - see FHFCounterParams::Overhang,
	// where the same figure is resolved for a kitchen worktop by the composing layer.
	//
	// Here it is resolved inside the kit rather than handed in, and that is not an inconsistency: a
	// worktop is its OWN fixture over somebody else's carcasses, so only the composing layer can see
	// both; a vanity's top is part of the same object as the carcass under it, and this function is
	// already holding the shutter face it needs.

	if (P.TopThickness > 0.0)
	{
		Out.TopParams.Width = P.Width;
		Out.TopParams.Depth = P.Depth;
		Out.TopParams.Thickness = P.TopThickness;
		Out.TopParams.Overhang = -Out.ShutterFaceY + TopOversail;
		Out.TopParams.UpstandHeight = P.TopUpstandHeight;
		Out.TopParams.Edge = P.TopEdge;

		FHFCounterBuild TopBuild = FHFCounterKit::Build(Out.TopParams);
		Out.TopParams = TopBuild.Used;

		if (TopBuild.bValid)
		{
			MeshTransforms::Translate(TopBuild.Shell, FVector3d(0.0, 0.0, P.TopBottomZ()));
			Out.Top = MoveTemp(TopBuild.Shell);
		}
	}

	// -------------------------------------------------------------------------------------- shell

	for (const FDynamicMesh3& Carcass : Out.Carcasses)
	{
		FHFMeshOps::AppendPreservingRoles(Out.Shell, Carcass);
	}
	FHFMeshOps::AppendPreservingRoles(Out.Shell, Out.Interior);
	FHFMeshOps::AppendPreservingRoles(Out.Shell, Out.Plinth);
	FHFMeshOps::AppendPreservingRoles(Out.Shell, Out.Cornice);
	FHFMeshOps::AppendPreservingRoles(Out.Shell, Out.Top);

	Out.bValid = Out.Shell.TriangleCount() > 0;
	return Out;
}
