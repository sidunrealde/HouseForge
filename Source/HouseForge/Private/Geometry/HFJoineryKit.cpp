// Copyright Siddartha G. All Rights Reserved.

#include "Geometry/HFJoineryKit.h"

#include "CompGeom/PolygonTriangulation.h"
#include "DynamicMesh/MeshTransforms.h"
#include "Geometry/HFMeshOps.h"
#include "HouseForge.h"
#include "Model/HFTypes.h"

using namespace UE::Geometry;

namespace
{
	/** Thinner than this is not a board. A frame built from it folds in on itself. */
	constexpr double MinBoardThickness = 0.2;

	/** Appends an axis-aligned box given by its corners rather than a centre and half-extents. */
	void AppendRail(FDynamicMesh3& Mesh, const FVector3d& Min, const FVector3d& Max, EHFSurfaceRole Role)
	{
		FHFMeshOps::AppendBox(Mesh, (Min + Max) * 0.5, (Max - Min) * 0.5, 0.0, Role);
	}
}

FHFPlinthParams FHFJoineryKit::SanitisePlinth(const FHFPlinthParams& Params)
{
	FHFPlinthParams Out = Params;

	Out.Width = FMath::Max(Out.Width, 0.0);
	Out.Depth = FMath::Max(Out.Depth, 0.0);
	Out.Height = FMath::Max(Out.Height, 0.0);

	// A board can be at most half the smallest footprint dimension, or opposite rails of the frame
	// pass through each other and the mesh reports more material than it contains.
	const double Smallest = FMath::Min(Out.Width, Out.Depth);
	Out.PanelThickness = FMath::Clamp(Out.PanelThickness, MinBoardThickness,
		FMath::Max(MinBoardThickness, Smallest * 0.5));

	// The kick is measured from the shutter face and the frame is built off the carcass front plane,
	// so what has to be clamped is where the panel actually lands - FrontRecess less the overlay -
	// and not FrontRecess on its own.
	//
	// A recess deeper than the unit would put the front panel out behind its own back, and one
	// shallower than the shutters stand proud would put it out in front of them. Clamping both ends
	// leaves the caller with a plinth to look at and a bound to notice it by, rather than nothing.
	Out.ShutterOverlay = FMath::Clamp(Out.ShutterOverlay, 0.0,
		FMath::Max(0.0, Out.Depth - Out.PanelThickness));
	Out.FrontRecess = FMath::Clamp(Out.FrontRecess, Out.ShutterOverlay,
		FMath::Max(Out.ShutterOverlay, Out.Depth - Out.PanelThickness));

	return Out;
}

FDynamicMesh3 FHFJoineryKit::GeneratePlinth(const FHFPlinthParams& Params)
{
	FDynamicMesh3 Mesh;
	FHFMeshOps::InitialiseMesh(Mesh);

	const FHFPlinthParams P = SanitisePlinth(Params);

	// FLUSH AT BOTH ENDS, always. The plinth is what the run stands on, so its ends are directly
	// under the gables and directly under the shutters at each extremity: set either of them back
	// and the carcass above it overhangs daylight. That is what an end setback of 5 did to every
	// wardrobe in the flat - a 5 x 10 notch at each end of a 2400 run, floor visible under both
	// corners, and nothing measuring it because the plinth was still a valid closed solid.
	//
	// The toe kick is at the FRONT, and FrontFaceY below is where that happens.
	const double X0 = 0.0;
	const double X1 = P.Width;

	// The kick is asked for from the shutter face and built off the carcass front plane. Taking
	// FrontRecess as the plane directly - which is what this used to do while the struct documented
	// the opposite - makes the visible kick FrontRecess plus the shutter overlay: on the wardrobe
	// composition, a 5 cm kick came out 7 cm and nothing measured it.
	const double Y0 = P.FrontFaceY();
	const double Y1 = P.Depth;

	const double Span = X1 - X0;
	const double Reach = Y1 - Y0;

	// No plinth is a legitimate answer, not a failure: a wall-hung unit has none, and a run that
	// asked for zero height asked for none.
	if (Span <= UE_KINDA_SMALL_NUMBER || Reach <= UE_KINDA_SMALL_NUMBER || P.Height <= UE_KINDA_SMALL_NUMBER)
	{
		return Mesh;
	}

	const double T = P.PanelThickness;

	// Roles by what the face is finished as, which is what the material panel targets. The toe-kick
	// face is a finished laminate face like a shutter, so it is tagged as one and retextures with
	// them; the rails buried under the carcass are carcass board. A contrast plinth - dark under
	// light shutters, which is a common enough choice - is the case that would justify a dedicated
	// role in EHFSurfaceRole, and nothing else here would have to change.
	constexpr EHFSurfaceRole Finished = EHFSurfaceRole::ShutterLaminate;
	constexpr EHFSurfaceRole Concealed = EHFSurfaceRole::JoineryCarcass;

	if (Reach <= 2.0 * T || Span <= 2.0 * T)
	{
		// Too small to frame. A filler this size is a solid packer on site as well, so building it
		// as one is honest rather than a fallback - and it keeps the rails from overlapping, which
		// would otherwise count the same board twice.
		AppendRail(Mesh, FVector3d(X0, Y0, 0.0), FVector3d(X1, Y1, P.Height), Finished);
	}
	else
	{
		// A ladder frame: front and back rails running the length, ends closing between them. The
		// ends are butted between the rails rather than lapped over them, so the volume is the board
		// the plinth is really made of instead of double-counting all four corners.
		AppendRail(Mesh, FVector3d(X0, Y0, 0.0), FVector3d(X1, Y0 + T, P.Height), Finished);
		AppendRail(Mesh, FVector3d(X0, Y1 - T, 0.0), FVector3d(X1, Y1, P.Height), Concealed);

		AppendRail(Mesh, FVector3d(X0, Y0 + T, 0.0), FVector3d(X0 + T, Y1 - T, P.Height),
			P.bLeftEndExposed ? Finished : Concealed);
		AppendRail(Mesh, FVector3d(X1 - T, Y0 + T, 0.0), FVector3d(X1, Y1 - T, P.Height),
			P.bRightEndExposed ? Finished : Concealed);
	}

	FHFMeshOps::ApplyWorldScaleUVs(Mesh);
	return Mesh;
}

// --------------------------------------------------------------------------------- shelf stacks

namespace
{
	/** Faces of a hanging rail's tube. Twelve reads round under a shutter's shadow and costs nothing. */
	constexpr int32 RailFacets = 12;

	/** How far a rail's end fixing stands proud of the tube it carries. */
	constexpr double RailFixingProud = 0.7;

	/** Thickness of that fixing, against the carcass side. */
	constexpr double RailFixingThickness = 1.6;

	/** A tube thinner than 12 mm is a curtain wire, not a wardrobe rail. */
	constexpr double MinRailRadius = 0.6;

	/** Upper bound on partitions, so a nonsense span limit cannot spin the bay search. */
	constexpr int32 MaxShelfBays = 16;

	struct FShelfBays
	{
		int32 Count = 1;
		double Width = 0.0;
	};

	/**
	 * How the width divides into bays once the span limit is honoured.
	 *
	 * Iterative rather than a single division because every partition eats width of its own, which
	 * can push the remaining bays straight back over the limit. Dividing once and hoping is how a
	 * 1830 wardrobe ends up with two 906 bays that still sag.
	 */
	FShelfBays LayOutShelfBays(const FHFShelfStackParams& P)
	{
		FShelfBays Bays;
		Bays.Count = 1;
		Bays.Width = P.Width;

		if (!P.bMidPartitionWhenOverspan || P.MaxSpan <= 0.0 || P.Width <= P.MaxSpan)
		{
			return Bays;
		}

		// Clamped INTO the search range, not merely floored at two.
		//
		// Taking Max(2, ceil(Width / MaxSpan)) alone turns MaxShelfBays into a trapdoor: the moment
		// the span limit asks for more bays than the ceiling allows, the loop body never runs at all
		// and Bays goes back untouched as one full-width unsupported shelf - the exact opposite of
		// what the guard is for, and a cliff rather than a degradation. At 300 wide, a 19 limit gave
		// 16 supported bays and an 18 limit gave one unsupported 3 m shelf.
		//
		// The Min() before the cast also keeps CeilToInt32 in range: a very small positive MaxSpan
		// overflows int32 and the conversion is undefined.
		const int32 First = FMath::Clamp(
			FMath::CeilToInt32(FMath::Min(P.Width / P.MaxSpan, static_cast<double>(MaxShelfBays))),
			2, MaxShelfBays);

		for (int32 Count = First; Count <= MaxShelfBays; ++Count)
		{
			const double BayWidth = (P.Width - (Count - 1) * P.PartitionThickness) / Count;
			if (BayWidth <= UE_KINDA_SMALL_NUMBER)
			{
				break;
			}

			Bays.Count = Count;
			Bays.Width = BayWidth;

			if (BayWidth <= P.MaxSpan)
			{
				break;
			}
		}

		return Bays;
	}

	/** Clear height of every compartment, the height divided evenly by the shelves in it. */
	double ShelfCompartmentHeight(const FHFShelfStackParams& P)
	{
		return (P.Height - P.ShelfCount * P.ShelfThickness) / (P.ShelfCount + 1);
	}

	/**
	 * A round tube swept along +X, capped at both ends.
	 *
	 * Wound to match FHFMeshOps::AppendBox exactly - its side quads and its two caps, with the ring
	 * standing in for the box's bottom face - so the tube's normals face outward under the same
	 * convention. An inverted solid still looks right in the viewport but reports negative volume,
	 * which is how a mesh that is quietly inside out gets shipped.
	 */
	void AppendTubeAlongX(FDynamicMesh3& Mesh, double X0, double X1, double CentreY, double CentreZ,
		double Radius, EHFSurfaceRole Role)
	{
		if (X1 - X0 <= UE_KINDA_SMALL_NUMBER || Radius <= 0.0)
		{
			return;
		}

		const int32 Group = FHFMeshOps::GroupForRole(Role);

		TArray<int32, TInlineAllocator<RailFacets>> Near;
		TArray<int32, TInlineAllocator<RailFacets>> Far;
		for (int32 Facet = 0; Facet < RailFacets; ++Facet)
		{
			const double Theta = 2.0 * UE_DOUBLE_PI * Facet / RailFacets;
			const double Y = CentreY + Radius * FMath::Cos(Theta);
			const double Z = CentreZ + Radius * FMath::Sin(Theta);
			Near.Add(Mesh.AppendVertex(FVector3d(X0, Y, Z)));
			Far.Add(Mesh.AppendVertex(FVector3d(X1, Y, Z)));
		}

		const int32 NearCap = Mesh.AppendVertex(FVector3d(X0, CentreY, CentreZ));
		const int32 FarCap = Mesh.AppendVertex(FVector3d(X1, CentreY, CentreZ));

		for (int32 Facet = 0; Facet < RailFacets; ++Facet)
		{
			const int32 Next = (Facet + 1) % RailFacets;

			Mesh.AppendTriangle(Near[Facet], Far[Next], Near[Next], Group);
			Mesh.AppendTriangle(Near[Facet], Far[Facet], Far[Next], Group);

			Mesh.AppendTriangle(NearCap, Near[Facet], Near[Next], Group);
			Mesh.AppendTriangle(FarCap, Far[Next], Far[Facet], Group);
		}
	}

	/** A hanging rail spanning one bay, on an end fixing at each side. */
	void AppendHangingRail(FDynamicMesh3& Mesh, double BayX0, double BayX1, double CentreY,
		double CentreZ, double Radius)
	{
		const double FixingHalf = Radius + RailFixingProud;
		if (BayX1 - BayX0 <= 2.0 * RailFixingThickness || Radius <= 0.0)
		{
			return;
		}

		// The tube runs between the fixings rather than through them. A real flange sockets the rail
		// end, so nothing is lost visually, and keeping the solids disjoint means the stack's volume
		// is the material it contains instead of counting the overlap twice.
		AppendTubeAlongX(Mesh, BayX0 + RailFixingThickness, BayX1 - RailFixingThickness,
			CentreY, CentreZ, Radius, EHFSurfaceRole::MetalHardware);

		AppendRail(Mesh,
			FVector3d(BayX0, CentreY - FixingHalf, CentreZ - FixingHalf),
			FVector3d(BayX0 + RailFixingThickness, CentreY + FixingHalf, CentreZ + FixingHalf),
			EHFSurfaceRole::MetalHardware);

		AppendRail(Mesh,
			FVector3d(BayX1 - RailFixingThickness, CentreY - FixingHalf, CentreZ - FixingHalf),
			FVector3d(BayX1, CentreY + FixingHalf, CentreZ + FixingHalf),
			EHFSurfaceRole::MetalHardware);
	}
}

// A default-constructed FHFShelfMaterialFigures has to BE the compiled-in constants, or every
// caller that does not pass one silently changes behaviour. Held here at compile time rather than
// in a test, because the two declarations sit in different places and are easy to edit apart.
static_assert(FHFShelfMaterialFigures().PlyThickness == FHFJoineryKit::PlyShelfThickness,
	"The default ply shelf thickness has drifted from FHFJoineryKit::PlyShelfThickness.");
static_assert(FHFShelfMaterialFigures().GlassThickness == FHFJoineryKit::GlassShelfThickness,
	"The default glass shelf thickness has drifted from FHFJoineryKit::GlassShelfThickness.");
static_assert(FHFShelfMaterialFigures().PlyMaxSpan == FHFJoineryKit::PlyMaxSpan,
	"The default ply span has drifted from FHFJoineryKit::PlyMaxSpan.");
static_assert(FHFShelfMaterialFigures().GlassMaxSpan == FHFJoineryKit::GlassMaxSpan,
	"The default glass span has drifted from FHFJoineryKit::GlassMaxSpan.");

double FHFJoineryKit::DefaultShelfThickness(EHFShelfMaterial Material,
	const FHFShelfMaterialFigures& Figures)
{
	return Figures.ThicknessFor(Material);
}

double FHFJoineryKit::DefaultMaxSpan(EHFShelfMaterial Material, const FHFShelfMaterialFigures& Figures)
{
	return Figures.MaxSpanFor(Material);
}

int32 FHFJoineryKit::ShelfCountForClearHeight(double ClearHeight, double TargetSpacing,
	double ShelfThickness, double MinCompartment)
{
	if (ClearHeight <= 0.0)
	{
		return 0;
	}

	const double Thickness = ShelfThickness > 0.0 ? ShelfThickness : DefaultShelfThickness(EHFShelfMaterial::Ply);
	const double Target = TargetSpacing > 0.0 ? TargetSpacing : DefaultTargetShelfSpacing;
	const double SmallestUseful = MinCompartment > 0.0 ? MinCompartment : MinUsefulCompartment;

	// Whole compartments of about the target spacing. Each one past the first costs a shelf's
	// thickness as well as its own clear height, which is why the thickness appears on both sides.
	const int32 Compartments = FMath::Max(1, FMath::RoundToInt32((ClearHeight + Thickness) / (Target + Thickness)));

	int32 Count = Compartments - 1;
	while (Count > 0 && (ClearHeight - Count * Thickness) / (Count + 1) < SmallestUseful)
	{
		--Count;
	}

	return Count;
}

FHFShelfStackParams FHFJoineryKit::SanitiseShelfStack(const FHFShelfStackParams& Params,
	const FHFShelfMaterialFigures& Figures)
{
	FHFShelfStackParams Out = Params;

	Out.Width = FMath::Max(Out.Width, 0.0);
	Out.Depth = FMath::Max(Out.Depth, 0.0);
	Out.Height = FMath::Max(Out.Height, 0.0);

	// Zero means "whatever this material is", which is the only sensible default: a glass shelf
	// generated 18 mm thick would be a glass shelf nobody has ever seen. Resolving it HERE, against
	// figures handed in, is what lets a project set its own board and span without the material
	// having to be known before the stack is described - see FHFShelfMaterialFigures.
	if (Out.ShelfThickness <= 0.0)
	{
		Out.ShelfThickness = DefaultShelfThickness(Out.ShelfMaterial, Figures);
	}
	Out.ShelfThickness = FMath::Max(Out.ShelfThickness, MinBoardThickness);

	if (Out.MaxSpan <= 0.0)
	{
		Out.MaxSpan = DefaultMaxSpan(Out.ShelfMaterial, Figures);
	}
	Out.PartitionThickness = FMath::Max(Out.PartitionThickness, MinBoardThickness);

	Out.FrontSetback = FMath::Clamp(Out.FrontSetback, 0.0, Out.Depth);
	Out.BackClearance = FMath::Clamp(Out.BackClearance, 0.0, FMath::Max(0.0, Out.Depth - Out.FrontSetback));

	// Shelves that will not fit are not built. Dropping the count is the honest answer; the
	// alternative is compartments of negative height, which come out as boards through one another
	// and a volume that reads plausible.
	Out.ShelfCount = FMath::Max(Out.ShelfCount, 0);
	while (Out.ShelfCount > 0 && Out.Height - Out.ShelfCount * Out.ShelfThickness <= UE_KINDA_SMALL_NUMBER)
	{
		--Out.ShelfCount;
	}

	Out.RailDiameter = FMath::Max(Out.RailDiameter, 0.0);
	Out.RailDrop = FMath::Max(Out.RailDrop, 0.0);

	if (Out.bHangingRail)
	{
		// The rail and its fixings have to fit inside the top compartment and inside the shelf depth,
		// or they leave the volume the stack was given and foul the shutter closing over it.
		const double ClearDepth = Out.Depth - Out.FrontSetback - Out.BackClearance;
		const double Compartment = ShelfCompartmentHeight(Out);
		const FShelfBays Bays = LayOutShelfBays(Out);

		const double Radius = FMath::Min3(Out.RailDiameter * 0.5,
			ClearDepth * 0.5 - RailFixingProud,
			Compartment * 0.5 - RailFixingProud);

		// Nothing hangs under a rail with a compartment's worth of air below it, and compartments
		// are forced equal - so a caller who asks for a realistic shelf count AND a rail gets one
		// with 32 cm under it in a 2 m bay. The tube is the right diameter, in the right place,
		// inside the volume it was given, and the wardrobe holds no clothes.
		//
		// Refused for the same reason, and in the same way, as a bay too shallow to take the tube:
		// a caller reading the parameters back can tell "no room to hang" from "not asked for",
		// where a rail that came out anyway says neither.
		const double ClearBelowRail = Compartment - Out.RailDrop;

		// The floor a rail has to clear. Taken from the params so a project can raise it - 90 is a
		// short-hang bay, and a wardrobe meant for full-length garments wants 150. Zero means the
		// caller did not express a view, so the domain floor stands.
		const double RequiredClearance = Out.MinHangingClearance > 0.0
			? Out.MinHangingClearance
			: MinHangingClearance;

		if (Radius < MinRailRadius || Bays.Width <= 2.0 * RailFixingThickness
			|| ClearBelowRail < RequiredClearance)
		{
			// No room for a rail is a real answer. Saying so here rather than silently emitting
			// nothing is what lets a caller - or a test - tell "too shallow" from "not asked for".
			Out.bHangingRail = false;
		}
		else
		{
			Out.RailDiameter = Radius * 2.0;

			const double FixingHalf = Radius + RailFixingProud;
			const double MaxDrop = Compartment - FixingHalf;
			Out.RailDrop = MaxDrop > FixingHalf
				? FMath::Clamp(Out.RailDrop, FixingHalf, MaxDrop)
				: Compartment * 0.5;
		}
	}

	return Out;
}

FDynamicMesh3 FHFJoineryKit::GenerateShelfStack(const FHFShelfStackParams& Params,
	const FHFShelfMaterialFigures& Figures)
{
	FDynamicMesh3 Mesh;
	FHFMeshOps::InitialiseMesh(Mesh);

	const FHFShelfStackParams P = SanitiseShelfStack(Params, Figures);

	const double Y0 = P.FrontSetback;
	const double Y1 = P.Depth - P.BackClearance;

	// An empty bay is a legitimate answer, not a failure: a hanging-only wardrobe section has no
	// shelves, and a bay whose shelves were all clamped away has nothing left to build.
	if (P.Width <= UE_KINDA_SMALL_NUMBER || P.Height <= UE_KINDA_SMALL_NUMBER
		|| Y1 - Y0 <= UE_KINDA_SMALL_NUMBER
		|| (P.ShelfCount == 0 && !P.bHangingRail))
	{
		return Mesh;
	}

	const FShelfBays Bays = LayOutShelfBays(P);
	const double Compartment = ShelfCompartmentHeight(P);
	const double RailZ = P.Height - P.RailDrop;

	// Glass shelves are tagged as glass so they get glass's thickness in the material panel and,
	// more to the point, so they refract rather than reading as thin white boards.
	const EHFSurfaceRole ShelfRole = P.ShelfMaterial == EHFShelfMaterial::Glass
		? EHFSurfaceRole::Glass
		: EHFSurfaceRole::JoineryCarcass;

	for (int32 Bay = 0; Bay < Bays.Count; ++Bay)
	{
		const double BayX0 = Bay * (Bays.Width + P.PartitionThickness);
		const double BayX1 = BayX0 + Bays.Width;

		for (int32 Shelf = 0; Shelf < P.ShelfCount; ++Shelf)
		{
			// Compartments are equal, so the shelf above N compartments sits N clear heights and N
			// boards up. Even spacing is what a wardrobe is actually set out to; graduating shelves
			// is a drawer-bank idea and does not belong here.
			const double Z0 = (Shelf + 1) * Compartment + Shelf * P.ShelfThickness;
			AppendRail(Mesh, FVector3d(BayX0, Y0, Z0), FVector3d(BayX1, Y1, Z0 + P.ShelfThickness), ShelfRole);
		}

		if (P.bHangingRail)
		{
			// One rail per bay. A rail cannot pass through the partition holding its shelves up.
			AppendHangingRail(Mesh, BayX0, BayX1, (Y0 + Y1) * 0.5, RailZ, P.RailDiameter * 0.5);
		}
	}

	// The partitions the run is broken over. Always carcass board, whatever it carries - and it
	// carries rails as well as shelves, because a 1500 hanging rail sags exactly as a 1500 shelf
	// does. A bay with neither returned empty above, so nothing here supports nothing.
	for (int32 Partition = 1; Partition < Bays.Count; ++Partition)
	{
		const double X0 = Partition * (Bays.Width + P.PartitionThickness) - P.PartitionThickness;
		AppendRail(Mesh, FVector3d(X0, Y0, 0.0), FVector3d(X0 + P.PartitionThickness, Y1, P.Height),
			EHFSurfaceRole::JoineryCarcass);
	}

	FHFMeshOps::ApplyWorldScaleUVs(Mesh);
	return Mesh;
}

// ------------------------------------------------------------------------------------- shutters

FDynamicMesh3 FHFJoineryKit::GenerateShutter(const FHFShutterParams& Params)
{
	FDynamicMesh3 Mesh;
	FHFMeshOps::InitialiseMesh(Mesh);

	if (!Params.IsValid())
	{
		return Mesh;
	}

	const double T = Params.Thickness;

	// The leaf is cut out on the side of its own origin that its module lies on, and its board
	// always lies on +Y of that origin. Every kind therefore presents its outward face at local
	// Y = 0 and carries its thickness away from the room, which is what keeps a swung leaf out of
	// its own carcass. See the header for the whole argument, including why the mirror is the
	// cheaper price.
	const FBox Leaf = FHFJoineryKit::ShutterPanelBox(Params);
	const double X0 = Leaf.Min.X;
	const double X1 = Leaf.Max.X;
	const double Y0 = 0.0;
	const double Y1 = T;

	// A top-hung flap hangs BELOW its hinge, so its own space runs from -LeafHeight up to 0 rather
	// than from 0 up. Everything below is written against these two rather than against 0 and H.
	const double Z0 = Leaf.Min.Z;
	const double Z1 = Leaf.Max.Z;

	if (!Params.HasGlazableFrame())
	{
		AppendRail(Mesh, FVector3d(X0, Y0, Z0), FVector3d(X1, Y1, Z1), EHFSurfaceRole::ShutterLaminate);
	}
	else
	{
		const double S = Params.StileWidth;

		// The pane runs under the frame by the rebate all round, so the join reads as a shadow
		// rather than a gap you can see through, and it is a solid of real thickness centred in
		// the leaf - glass modelled as a plane refracts and reflects wrong under any real lighting.
		const double PaneInset = S - Params.GlassRebate;
		const double GlassCentreY = (Y0 + Y1) * 0.5;
		const double GlassHalf = Params.GlassThickness * 0.5;

		// The rebate is CUT, not implied. Emitting the frame at full thickness right across the
		// pane's footprint and then dropping the pane through it puts a rebate-wide strip of ply
		// and a rebate-wide strip of glass in exactly the same space: IsClosed still passes,
		// because each box is closed on its own, and GetVolumeArea sums both, so the leaf reports
		// board it does not contain. It is the same double count the butted stiles and rails below
		// exist to avoid, and it leaves a self-intersecting solid - which matters, because
		// SubtractInPlace refuses any boolean whose result is not closed, so a glazed leaf handed
		// to ApplyHandle for a J-profile would silently come back with no handle recess at all.
		//
		// So the frame comes in two parts. Outside the pane's footprint it is full thickness; over
		// the rebate strip only the two shoulders either side of the groove are emitted, and the
		// pane sits in what is left. Nothing overlaps, and the volume is the board really cut.
		const double PaneX0 = X0 + PaneInset;
		const double PaneX1 = X1 - PaneInset;
		const double PaneZ0 = Z0 + PaneInset;
		const double PaneZ1 = Z1 - PaneInset;

		// Full-thickness outer band. Stiles run the full height and the rails butt between them,
		// rather than both running full length and lapping at the corners; lapped members would put
		// four corners' worth of board into the mesh twice.
		AppendRail(Mesh, FVector3d(X0, Y0, Z0), FVector3d(PaneX0, Y1, Z1), EHFSurfaceRole::ShutterLaminate);
		AppendRail(Mesh, FVector3d(PaneX1, Y0, Z0), FVector3d(X1, Y1, Z1), EHFSurfaceRole::ShutterLaminate);
		AppendRail(Mesh, FVector3d(PaneX0, Y0, Z0), FVector3d(PaneX1, Y1, PaneZ0), EHFSurfaceRole::ShutterLaminate);
		AppendRail(Mesh, FVector3d(PaneX0, Y0, PaneZ1), FVector3d(PaneX1, Y1, Z1), EHFSurfaceRole::ShutterLaminate);

		// The shoulders: the same picture-frame ring the pane overlaps, split either side of the
		// groove the glass sits in. A zero rebate, or a pane as thick as the leaf, collapses these
		// to nothing - which AppendBox declines rather than emitting a degenerate board.
		const double GrooveY0 = GlassCentreY - GlassHalf;
		const double GrooveY1 = GlassCentreY + GlassHalf;

		auto AppendShoulders = [&](double SX0, double SZ0, double SX1, double SZ1)
		{
			AppendRail(Mesh, FVector3d(SX0, Y0, SZ0), FVector3d(SX1, GrooveY0, SZ1),
				EHFSurfaceRole::ShutterLaminate);
			AppendRail(Mesh, FVector3d(SX0, GrooveY1, SZ0), FVector3d(SX1, Y1, SZ1),
				EHFSurfaceRole::ShutterLaminate);
		};

		AppendShoulders(PaneX0, PaneZ0, X0 + S, PaneZ1);
		AppendShoulders(X1 - S, PaneZ0, PaneX1, PaneZ1);
		AppendShoulders(X0 + S, PaneZ0, X1 - S, Z0 + S);
		AppendShoulders(X0 + S, Z1 - S, X1 - S, PaneZ1);

		AppendRail(Mesh,
			FVector3d(PaneX0, GrooveY0, PaneZ0),
			FVector3d(PaneX1, GrooveY1, PaneZ1),
			EHFSurfaceRole::Glass);
	}

	FHFMeshOps::ApplyWorldScaleUVs(Mesh);
	return Mesh;
}

FBox FHFJoineryKit::ShutterPanelBox(const FHFShutterParams& Params)
{
	if (!Params.IsValid())
	{
		return FBox(ForceInit);
	}

	const double W = Params.LeafWidth();
	const double H = Params.LeafHeight();
	const bool bLeft = Params.Hinge == EHFShutterHinge::Left;

	if (Params.IsTopHung())
	{
		// A flap hangs below its hinge. Its X runs the way the module's does, because a flap has no
		// handedness to mirror - only one thing can happen to it, and that is lifting.
		return FBox(FVector(0.0, 0.0, -H), FVector(W, Params.Thickness, 0.0));
	}

	if (Params.IsBottomHung())
	{
		// And a tilt-out flap stands ABOVE its hinge: the exact mirror, and the same absence of a
		// hand. Its board still lies on +Y of its own origin, so the face it presents to the room is
		// at local Y = 0 exactly as every other leaf's is.
		return FBox(FVector(0.0, 0.0, 0.0), FVector(W, Params.Thickness, H));
	}

	if (Params.IsSliding())
	{
		// Set out over the module rather than inside it: the near edge stands off the jamb by the
		// running clearance, and the far edge reaches past the meeting line by the lap. Mirrored for
		// a leaf set out from the right, exactly as the second panel of a sliding door is.
		const FHFSlidingSetOut SetOut = Params.SlideSetOut();
		const double Near = bLeft ? SetOut.NearEdge : -SetOut.FarEdge;
		const double Far = bLeft ? SetOut.FarEdge : -SetOut.NearEdge;

		return FBox(FVector(Near, 0.0, 0.0), FVector(Far, Params.Thickness, H));
	}

	return FBox(
		FVector(bLeft ? 0.0 : -W, 0.0, 0.0),
		FVector(bLeft ? W : 0.0, Params.Thickness, H));
}

EHFHandleEdge FHFJoineryKit::ShutterLeadingEdge(const FHFShutterParams& Params)
{
	// A flap's leading edge is its bottom one: that is where the handle goes, and where a gas stay
	// pushes. Reading it off the hand instead would put the pull on a vertical edge of a panel that
	// does not turn about a vertical axis at all.
	if (Params.IsTopHung())
	{
		return EHFHandleEdge::Bottom;
	}

	// A tilt-out flap's leading edge is its TOP one, for the same reason and the other way up: that
	// is the edge that comes towards you, and the edge the hand goes behind to tip the compartment
	// open. A pull at the bottom of a tilt-out is a pull that fights the hinge it is next to.
	if (Params.IsBottomHung())
	{
		return EHFHandleEdge::Top;
	}

	// A sliding leaf leads with the edge it runs towards, which is the one away from the jamb it is
	// set out from - the same edge a hinged leaf of that hand opens from.
	return Params.Hinge == EHFShutterHinge::Left ? EHFHandleEdge::MaxX : EHFHandleEdge::MinX;
}

FTransform FHFJoineryKit::ShutterPivotTransform(const FHFShutterParams& Params)
{
	const bool bLeft = Params.Hinge == EHFShutterHinge::Left;
	const double HalfReveal = Params.RevealGap * 0.5;

	// The origin sits on the leaf's FRONT FACE in every case, one leaf thickness and its clearance
	// in front of the carcass - and for a slider, a further track out for each track it runs in
	// front of. Putting the axis on the face the leaf turns towards is what makes a swing provably
	// clean rather than merely clean-looking; see the header.
	const double AxisY = -Params.FaceOffset();

	if (Params.IsTopHung())
	{
		// The head of the module, half a reveal down from its top. The leaf hangs below.
		return FTransform(FVector(HalfReveal, AxisY, Params.ModuleHeight - HalfReveal));
	}

	if (Params.IsBottomHung())
	{
		// The FOOT of the module, half a reveal up from its bottom. The leaf stands above, and the
		// axis lies on its front face like every other leaf's - which is what keeps the flap swinging
		// out of the compartment rather than back through the shelf behind it.
		return FTransform(FVector(HalfReveal, AxisY, HalfReveal));
	}

	if (Params.IsSliding())
	{
		// A slide may pivot anywhere on its line of travel, so the module edge this leaf is set out
		// from is the free and useful choice: it is the one point the carcass already knows. The
		// running clearance is in the leaf's own box rather than here, so a leaf and its mirror are
		// described by the same set-out.
		return FTransform(FVector(bLeft ? 0.0 : Params.ModuleWidth, AxisY, HalfReveal));
	}

	// Half a reveal in from the module edge on the hinge side. The leaf itself is cut on whichever
	// side of that axis its module lies, so there is no rotation to apply: a pure translation keeps
	// the leaf's local axes pointing the same way as the module's, and therefore keeps "out of the
	// cupboard" at -Y for both hands. A half-turn here would flip that for one of them.
	const double HingeX = bLeft ? HalfReveal : Params.ModuleWidth - HalfReveal;

	return FTransform(FVector(HingeX, AxisY, HalfReveal));
}

FHFPartMotion FHFJoineryKit::ShutterMotion(const FHFShutterParams& Params)
{
	FHFPartMotion Motion;

	if (Params.IsTopHung())
	{
		// About the horizontal axis at its head, and NEGATIVE, which is what lifts the leaf out of
		// the unit rather than driving it back through the carcass. The sign is the whole
		// correctness of a flap: hung the other way it swings inward and disappears into the
		// cabinet, which is invisible in elevation and obvious the moment it is opened.
		Motion.Type = EHFMotionType::Hinge;
		Motion.Axis = FVector::XAxisVector;
		Motion.MaxAngleDegrees = -Params.OpenAngleDegrees;
		return Motion;
	}

	if (Params.IsBottomHung())
	{
		// The same axis and the OPPOSITE sign, which is the whole of the difference between a lift-up
		// and a tilt-out. Rotating a point at +Z about +X by a positive angle carries it to negative
		// Y - forward, out of the compartment - and the leaf's top edge comes down into the room as
		// it goes. The other sign would tip the flap backwards through the shelf behind it, which
		// looks identical in elevation and is obvious the instant it is opened.
		Motion.Type = EHFMotionType::Hinge;
		Motion.Axis = FVector::XAxisVector;
		Motion.MaxAngleDegrees = Params.OpenAngleDegrees;
		return Motion;
	}

	if (Params.IsSliding())
	{
		// EVERY LEAF OF A SLIDING RUN RUNS. It goes along the module, towards its neighbour's bay,
		// and comes to rest exactly over it - the same stopping rule a sliding door's panel obeys,
		// from the same set-out.
		Motion.Type = EHFMotionType::Slide;
		Motion.Axis = FVector::XAxisVector;
		Motion.MaxTravelCm = Params.SlideSign() * Params.SlideTravel();

		// What separates the pair is not whether they move but which of them ONE control moves. Two
		// leaves driven outward from the meeting line by a single amount exchange tracks and uncover
		// nothing, which is what the master bedroom wardrobe was doing while both its leaves
		// reported 118 cm of travel. See FHFShutterParams::bLeadsTheRun.
		//
		// The pairing itself - AlternateToPartId - is the composer's to set, because a leaf on its
		// own does not know its partner's id.
		Motion.bMasterOpens = Params.bLeadsTheRun;
		return Motion;
	}

	Motion.Type = EHFMotionType::Hinge;
	Motion.Axis = FVector::ZAxisVector;
	Motion.MaxAngleDegrees = Params.SwingSign() * Params.OpenAngleDegrees;
	return Motion;
}

FHFMeshPart FHFJoineryKit::BuildShutterPart(const FHFShutterParams& Params, FName PartId)
{
	FHFMeshPart Part;
	Part.PartId = PartId;
	Part.Mesh = GenerateShutter(Params);
	Part.PivotTransform = ShutterPivotTransform(Params);
	Part.Motion = ShutterMotion(Params);
	Part.DefaultOpenAmount = 0.0;
	return Part;
}

// -------------------------------------------------------------------------------------- handles

namespace
{
	/**
	 * How far a recess cutter overshoots the faces it is meant to break out of.
	 *
	 * A cutter flush with a surface leaves coplanar faces for the boolean to resolve, and it
	 * resolves them badly - the same reason GenerateWall overshoots its opening cutters.
	 */
	constexpr double RecessOvershoot = 2.0;

	/** Thinner than this is not stock, it is wire. */
	constexpr double MinHandleStock = 0.1;

	/**
	 * The panel a handle is fitted to, resolved once into the axes the handle is built on.
	 *
	 * A panel's board thickness is always its local Y, so the edge a handle serves is on X or Z and
	 * the run is on whichever of the two is left. Deriving that here rather than at each use is what
	 * keeps a handle on a wardrobe's leading edge and one along a drawer front's top edge from being
	 * two different pieces of code that drift apart.
	 */
	struct FHandleFrame
	{
		bool bValid = false;

		/** 0 for X, 2 for Z. The other of the two is the run; Y is always the board thickness. */
		int32 EdgeAxis = 2;
		int32 RunAxis = 0;

		/** Out of the panel's front face. */
		FVector3d FaceNormal = FVector3d::UnitY();

		/** Out of the served edge. */
		FVector3d EdgeDir = FVector3d::UnitZ();

		/**
		 * EdgeDir x FaceNormal.
		 *
		 * That order, not the other one. AppendExtrudedSection derives its second in-plane axis as
		 * SweepDir x SectionU, so sweeping along this and passing EdgeDir as u puts v on FaceNormal
		 * exactly - which is how a section can be authored in (out along the edge, out of the face)
		 * and mean the same thing on all four edges of all two facings.
		 */
		FVector3d RunDir = -FVector3d::UnitX();

		/** Panel corner on both the face plane and the edge plane, at the -RunDir end of the run. */
		FVector3d RunStartCorner = FVector3d::Zero();

		double RunSpan = 0.0;
		double RunMidpoint = 0.0;
		double EdgeSpan = 0.0;
		double Thickness = 0.0;

		double FaceCoord = 0.0;
		double EdgeCoord = 0.0;
	};

	FHandleFrame MakeHandleFrame(const FHFHandleParams& Params)
	{
		FHandleFrame Frame;

		const FBox& Box = Params.PanelBox;
		if (Box.IsValid == 0)
		{
			return Frame;
		}

		const FVector Size = Box.GetSize();
		if (Size.GetMin() <= UE_KINDA_SMALL_NUMBER)
		{
			return Frame;
		}

		const bool bEdgeOnZ = (Params.Edge == EHFHandleEdge::Top || Params.Edge == EHFHandleEdge::Bottom);
		Frame.EdgeAxis = bEdgeOnZ ? 2 : 0;
		Frame.RunAxis = bEdgeOnZ ? 0 : 2;

		const double EdgeSign =
			(Params.Edge == EHFHandleEdge::Top || Params.Edge == EHFHandleEdge::MaxX) ? 1.0 : -1.0;
		const double FaceSign = (Params.Facing == EHFPanelFacing::PositiveY) ? 1.0 : -1.0;

		Frame.EdgeCoord = EdgeSign > 0.0 ? Box.Max[Frame.EdgeAxis] : Box.Min[Frame.EdgeAxis];
		Frame.FaceCoord = FaceSign > 0.0 ? Box.Max.Y : Box.Min.Y;

		Frame.FaceNormal = FVector3d(0.0, FaceSign, 0.0);
		Frame.EdgeDir = FVector3d::Zero();
		Frame.EdgeDir[Frame.EdgeAxis] = EdgeSign;
		Frame.RunDir = Frame.EdgeDir.Cross(Frame.FaceNormal);

		const double RunStart = (Frame.RunDir[Frame.RunAxis] > 0.0)
			? Box.Min[Frame.RunAxis] : Box.Max[Frame.RunAxis];

		Frame.RunStartCorner = FVector3d::Zero();
		Frame.RunStartCorner[Frame.EdgeAxis] = Frame.EdgeCoord;
		Frame.RunStartCorner[Frame.RunAxis] = RunStart;
		Frame.RunStartCorner.Y = Frame.FaceCoord;

		Frame.RunSpan = Size[Frame.RunAxis];
		Frame.RunMidpoint = (Box.Min[Frame.RunAxis] + Box.Max[Frame.RunAxis]) * 0.5;
		Frame.EdgeSpan = Size[Frame.EdgeAxis];
		Frame.Thickness = Size.Y;
		Frame.bValid = true;
		return Frame;
	}

	/** The frame an applied handle's solid is built on: an origin on the face, and three unit axes. */
	struct FHandleBasis
	{
		FVector3d Origin = FVector3d::Zero();
		/** Along the handle's run. */
		FVector3d Run = FVector3d::UnitX();
		/** Out of the panel face; the direction the handle projects. */
		FVector3d Out = FVector3d::UnitY();
		/** Towards the edge served. Unused by the geometry, and kept so the frame reads complete. */
		FVector3d Toward = FVector3d::UnitZ();
	};

	bool AppendBarHandle(FDynamicMesh3& Mesh, const FHFHandleParams& P, const FHandleBasis& Basis)
	{
		const double R = P.BarDiameter * 0.5;
		const double HalfLength = P.BarLength * 0.5;

		// A small break on the bar ends. Not a flourish: a flat-sawn end on a metal pull catches no
		// light at all along its rim, and it is the first thing on a cabinet that reads as untouched
		// geometry once the scene is lit.
		const double Chamfer = FMath::Min(R * 0.4, HalfLength * 0.25);

		// The bar's axis sits one radius in from the outermost point, so Projection is exactly what
		// the handle stands proud - the figure a caller can set a run out against.
		const FVector3d BarCentre = Basis.Origin + Basis.Out * (P.Projection - R);

		const TArray<FVector2D> BarProfile = {
			FVector2D(-HalfLength, R - Chamfer),
			FVector2D(-HalfLength + Chamfer, R),
			FVector2D(HalfLength - Chamfer, R),
			FVector2D(HalfLength, R - Chamfer)
		};

		bool bOk = FHFMeshOps::AppendRevolvedProfile(Mesh, BarProfile, BarCentre, Basis.Run,
			P.SideCount, P.HandleRole);

		// Two standoffs, ending on the bar's own axis. Stopping exactly there keeps each post inside
		// the bar's silhouette; running one past would push a rim of its end cap out through the
		// side of the bar, which is a hairline artefact that only ever shows up lit.
		const double StandoffOffset = HalfLength - P.BarEndInset;
		const TArray<FVector2D> PostProfile = {
			FVector2D(-P.Embed, R),
			FVector2D(P.Projection - R, R)
		};

		static constexpr double Sides[] = { -1.0, 1.0 };
		for (const double Side : Sides)
		{
			bOk &= FHFMeshOps::AppendRevolvedProfile(Mesh, PostProfile,
				Basis.Origin + Basis.Run * (StandoffOffset * Side), Basis.Out, P.SideCount, P.HandleRole);
		}

		return bOk;
	}

	bool AppendKnobHandle(FDynamicMesh3& Mesh, const FHFHandleParams& P, const FHandleBasis& Basis)
	{
		const double HeadRadius = P.KnobDiameter * 0.5;
		const double StemRadius = FMath::Min(P.KnobStemDiameter * 0.5, HeadRadius);
		const double L = P.Projection;

		// Stem, a cone out to the head, a short barrel, then a dome closing on the axis. The dome is
		// the point of doing this as a revolve at all: a knob capped with a flat disc reads as a
		// bottle top under any light, and closing on an apex costs one triangle fan.
		const TArray<FVector2D> Profile = {
			FVector2D(-P.Embed, StemRadius),
			FVector2D(L * 0.45, StemRadius),
			FVector2D(L * 0.62, HeadRadius),
			FVector2D(L * 0.80, HeadRadius),
			FVector2D(L * 0.94, HeadRadius * 0.72),
			FVector2D(L, 0.0)
		};

		return FHFMeshOps::AppendRevolvedProfile(Mesh, Profile, Basis.Origin, Basis.Out,
			P.SideCount, P.HandleRole);
	}

	/**
	 * Cross-section of a routed profile, in (distance out along the edge, distance out of the face).
	 *
	 * Both coordinates are negative inside the board, so the same section means the same cut on all
	 * four edges and both facings - the frame carries the signs, not the section.
	 */
	TArray<FVector2D> MakeRecessSection(const FHFHandleParams& P)
	{
		const double D = P.RecessDepth;
		const double H = P.ProfileHeight;
		const double C = P.LipChamfer;
		const double O = RecessOvershoot;

		TArray<FVector2D> Section;

		// A zero chamfer collapses two corners onto their neighbours, and a duplicated vertex is not
		// a simple polygon. Dropping them here keeps the section honest rather than making the
		// chamfer secretly mandatory.
		auto Push = [&Section](double U, double V)
		{
			const FVector2D Point(U, V);
			if (Section.IsEmpty() || !Section.Last().Equals(Point, UE_KINDA_SMALL_NUMBER))
			{
				Section.Add(Point);
			}
		};

		if (P.Style == EHFHandleStyle::JProfile)
		{
			// The corner comes off: out through the edge and out through the face, with a chamfer
			// on EACH of the two arrises the cut creates. That break-out is the whole character of a
			// J-profile - it is what makes a run of fronts read as one continuous shadow gap.
			//
			// Both lips, and the outer one is the one that matters most. The cut leaves two convex
			// arrises: the inner one where the recess wall meets the panel face, and the outer one
			// where the recess floor runs out through the edge - and that outer arris IS the grip
			// lip, at hand height on every front in the flat and the most prominent edge the profile
			// produces. Breaking only the inner one leaves the other mathematically sharp, which
			// reads as CG under any lighting; the handleless groove below breaks both of its lips
			// for exactly this reason. See .claude/rules/04-conventions.md.
			//
			// The outer break is taken by leaving material rather than by cutting deeper: the floor
			// runs out and up to meet the edge, so the recess is never deeper than RecessDepth and
			// the web behind it is untouched. Halved against the profile height so a very shallow
			// profile cannot fold the section through itself.
			const double Lip = FMath::Min(C, H * 0.5);

			Push(-H, -D);
			Push(-Lip, -D);
			Push(0.0, -D + Lip);
			Push(O, -D + Lip);
			Push(O, O);
			Push(-H - C, O);
			Push(-H - C, 0.0);
			Push(-H, -C);
		}
		else
		{
			// A channel in the face and nothing else. Both lips are real board edges, so both are
			// chamfered, and the near one stays inside the margin: a chamfer wider than the margin
			// would break the groove out through the edge and quietly turn it into a J-profile.
			const double M = P.GrooveEdgeMargin;
			Push(-(M + H), -D);
			Push(-M, -D);
			Push(-M, -C);
			Push(-M + C, 0.0);
			Push(-M + C, O);
			Push(-(M + H) - C, O);
			Push(-(M + H) - C, 0.0);
			Push(-(M + H), -C);
		}

		if (Section.Num() > 1 && Section.Last().Equals(Section[0], UE_KINDA_SMALL_NUMBER))
		{
			Section.Pop();
		}

		return Section;
	}
}

bool FHFJoineryKit::IsRecessedHandle(EHFHandleStyle Style)
{
	return Style == EHFHandleStyle::JProfile || Style == EHFHandleStyle::HandlelessGroove;
}

FHFHandleParams FHFJoineryKit::SanitiseHandle(const FHFHandleParams& Params)
{
	FHFHandleParams Out = Params;

	const FHandleFrame Frame = MakeHandleFrame(Out);
	if (!Frame.bValid)
	{
		return Out;
	}

	Out.SideCount = FMath::Clamp(Out.SideCount, 4, 64);
	Out.Embed = FMath::Max(Out.Embed, 0.0);

	// ----------------------------------------------------------------------------- applied styles

	Out.BarDiameter = FMath::Max(Out.BarDiameter, MinHandleStock);
	Out.KnobDiameter = FMath::Max(Out.KnobDiameter, MinHandleStock);
	Out.KnobStemDiameter = FMath::Clamp(Out.KnobStemDiameter, MinHandleStock, Out.KnobDiameter);

	// A bar has to clear the face by more than its own stock, or there is nowhere for fingers to go
	// and the standoffs come out with a negative length.
	Out.Projection = FMath::Max(Out.Projection,
		Out.Style == EHFHandleStyle::Knob ? MinHandleStock * 2.0 : Out.BarDiameter * 1.5);

	const double LongestBar = FMath::Max(MinHandleStock, Frame.RunSpan - 2.0 * Out.BarDiameter);
	Out.BarLength = FMath::Clamp(Out.BarLength, Out.BarDiameter, LongestBar);

	// At least a radius in, so a standoff cannot poke out past the end of the bar it carries and
	// make the handle measure longer than it was asked to be.
	Out.BarEndInset = FMath::Clamp(Out.BarEndInset, Out.BarDiameter * 0.5, Out.BarLength * 0.5);

	const double HalfAcross =
		(Out.Style == EHFHandleStyle::Knob ? Out.KnobDiameter : Out.BarDiameter) * 0.5;
	Out.EdgeInset = FMath::Clamp(Out.EdgeInset, HalfAcross,
		FMath::Max(HalfAcross, Frame.EdgeSpan - HalfAcross));

	// ----------------------------------------------------------------------------- routed styles

	Out.LipChamfer = FMath::Max(Out.LipChamfer, 0.0);
	Out.MinWeb = FMath::Clamp(Out.MinWeb, 0.0, Frame.Thickness * 0.5);

	// A recess deeper than the board would rout the panel in two. Leaving the web is the honest
	// clamp: the request was a mistake, and a shallower groove gives the caller something to look at
	// and a dimension to notice it by, rather than a shutter with a slot straight through it.
	Out.RecessDepth = FMath::Clamp(Out.RecessDepth, 0.0, FMath::Max(0.0, Frame.Thickness - Out.MinWeb));

	if (Out.Style == EHFHandleStyle::HandlelessGroove)
	{
		Out.GrooveEdgeMargin = FMath::Clamp(Out.GrooveEdgeMargin, 0.0, Frame.EdgeSpan * 0.5);
		Out.LipChamfer = FMath::Min(Out.LipChamfer, Out.GrooveEdgeMargin * 0.5);
	}
	else
	{
		Out.GrooveEdgeMargin = FMath::Max(Out.GrooveEdgeMargin, 0.0);
	}
	Out.LipChamfer = FMath::Min(Out.LipChamfer, Out.RecessDepth * 0.5);

	const double MarginInUse =
		(Out.Style == EHFHandleStyle::HandlelessGroove) ? Out.GrooveEdgeMargin : 0.0;
	Out.ProfileHeight = FMath::Clamp(Out.ProfileHeight, 0.0,
		FMath::Max(0.0, Frame.EdgeSpan - MarginInUse - Out.LipChamfer));

	return Out;
}

FTransform FHFJoineryKit::HandlePlacement(const FHFHandleParams& Params)
{
	const FHFHandleParams P = SanitiseHandle(Params);
	const FHandleFrame Frame = MakeHandleFrame(P);
	if (!Frame.bValid)
	{
		return FTransform::Identity;
	}

	// Out of the face and towards the edge; the run follows from those two, which is what makes the
	// basis right-handed on every edge without a table of special cases.
	const FVector3d Out = Frame.FaceNormal;
	const FVector3d Toward = Frame.EdgeDir;
	const FVector3d Run = Out.Cross(Toward);

	FVector3d Mount = FVector3d::Zero();
	Mount[Frame.EdgeAxis] = Frame.EdgeCoord - Frame.EdgeDir[Frame.EdgeAxis] * P.EdgeInset;
	Mount[Frame.RunAxis] = Frame.RunMidpoint;
	Mount.Y = Frame.FaceCoord;

	return FTransform(FMatrix(FVector(Run), FVector(Out), FVector(Toward), FVector(Mount)));
}

FDynamicMesh3 FHFJoineryKit::GenerateHandle(const FHFHandleParams& Params)
{
	FDynamicMesh3 Mesh;
	FHFMeshOps::InitialiseMesh(Mesh);

	const FHFHandleParams P = SanitiseHandle(Params);
	if (!P.IsValid() || !P.IsApplied())
	{
		return Mesh;
	}

	// An identity basis: the handle in its own space, which is what the local-space contract means.
	const FHandleBasis Basis;
	const bool bBuilt = (P.Style == EHFHandleStyle::Bar)
		? AppendBarHandle(Mesh, P, Basis)
		: AppendKnobHandle(Mesh, P, Basis);

	if (!bBuilt)
	{
		// Half a handle is worse than none: it would pass a watertightness check per component and
		// still be missing the bar.
		FHFMeshOps::InitialiseMesh(Mesh);
		return Mesh;
	}

	FHFMeshOps::ApplyWorldScaleUVs(Mesh);
	return Mesh;
}

FDynamicMesh3 FHFJoineryKit::GenerateHandleRecessCutter(const FHFHandleParams& Params)
{
	FDynamicMesh3 Mesh;
	FHFMeshOps::InitialiseMesh(Mesh);

	const FHFHandleParams P = SanitiseHandle(Params);
	if (!P.IsValid() || !P.IsRecessed()
		|| P.RecessDepth <= UE_KINDA_SMALL_NUMBER || P.ProfileHeight <= UE_KINDA_SMALL_NUMBER)
	{
		return Mesh;
	}

	const FHandleFrame Frame = MakeHandleFrame(P);
	if (!Frame.bValid)
	{
		return Mesh;
	}

	const TArray<FVector2D> Section = MakeRecessSection(P);
	if (Section.Num() < 3)
	{
		return Mesh;
	}

	// Swept the whole run and out past both ends, so the channel breaks out cleanly instead of dying
	// into the board against a face coplanar with the panel's own end.
	const FVector3d Start = Frame.RunStartCorner - Frame.RunDir * RecessOvershoot;
	const double Length = Frame.RunSpan + 2.0 * RecessOvershoot;

	FHFMeshOps::AppendExtrudedSection(Mesh, Section, Start, Frame.EdgeDir, Frame.RunDir, Length,
		P.RecessRole);

	// No UVs: this is a tool, not geometry. The panel it cuts is unwrapped afterwards, by which
	// point the new faces exist.
	return Mesh;
}

bool FHFJoineryKit::ApplyHandle(FDynamicMesh3& PanelMesh, const FHFHandleParams& Params)
{
	if (Params.Style == EHFHandleStyle::None)
	{
		// Nothing to do is an answer, not a failure. Plenty of joinery is handleless by design.
		return true;
	}

	const FHFHandleParams P = SanitiseHandle(Params);
	if (!P.IsValid())
	{
		return false;
	}

	// A mesh handed in from anywhere has to be able to carry groups and UVs, or the handle lands
	// untagged and the material panel can never reach it.
	if (!PanelMesh.HasTriangleGroups())
	{
		PanelMesh.EnableTriangleGroups();
	}
	if (!PanelMesh.HasAttributes())
	{
		PanelMesh.EnableAttributes();
	}

	bool bApplied = false;

	if (P.IsRecessed())
	{
		const FDynamicMesh3 Cutter = GenerateHandleRecessCutter(P);
		bApplied = Cutter.TriangleCount() > 0 && FHFMeshOps::SubtractInPlace(PanelMesh, Cutter);
	}
	else
	{
		// Built straight into the panel's own space rather than generated elsewhere and appended. An
		// append that renumbered polygroups would strip the handle of its surface role on the way
		// in, and untagged geometry cannot be re-materialled at all.
		const FTransform Placement = HandlePlacement(P);

		FHandleBasis Basis;
		Basis.Origin = FVector3d(Placement.GetTranslation());
		Basis.Run = FVector3d(Placement.GetUnitAxis(EAxis::X));
		Basis.Out = FVector3d(Placement.GetUnitAxis(EAxis::Y));
		Basis.Toward = FVector3d(Placement.GetUnitAxis(EAxis::Z));

		bApplied = (P.Style == EHFHandleStyle::Bar)
			? AppendBarHandle(PanelMesh, P, Basis)
			: AppendKnobHandle(PanelMesh, P, Basis);
	}

	if (bApplied)
	{
		FHFMeshOps::ApplyWorldScaleUVs(PanelMesh);
	}
	return bApplied;
}

// ------------------------------------------------------------------------------------- cornices

namespace
{
	/**
	 * How much of the section a front-underside profile is allowed to eat.
	 *
	 * Short of the whole thing, so a moulding asked for an absurd profile still comes back as a
	 * solid with a face on every declared plane rather than as a cross-section folded through
	 * itself, which sweeps into a self-intersecting shell that reports a plausible volume.
	 */
	constexpr double MaxProfileFraction = 0.9;

	/** No chamfer may take more of an edge than this, or two of them meet and collapse it. */
	constexpr double MaxBevelFraction = 0.45;

	/**
	 * One authored point of a swept cross-section.
	 *
	 * FaceRole belongs to the face swept from THIS point to the next, which is what lets a single
	 * section carry both the finished faces on show and the one buried against the carcass - the
	 * distinction the material panel targets, and one a single role per mesh would lose.
	 */
	struct FSectionPoint
	{
		/** (Y, Z) in the part's own local space. */
		FVector2D Point = FVector2D::ZeroVector;

		/** Role of the face swept from this point to the next. */
		EHFSurfaceRole FaceRole = EHFSurfaceRole::ShutterLaminate;

		/** Whether the arris at this point wants a chamfer. */
		bool bBevel = false;
	};

	/**
	 * Chamfers the tagged convex corners of a closed, counter-clockwise section.
	 *
	 * Done on the section rather than on the swept mesh because that is where it is exact: cutting a
	 * corner of the profile puts a facet along the whole run, which is precisely the light-catching
	 * edge a real moulding has, and it costs two vertices instead of a bevel operator over a solid.
	 */
	TArray<FSectionPoint> ChamferSection(const TArray<FSectionPoint>& Section, double Bevel)
	{
		const int32 Count = Section.Num();
		if (Count < 3 || Bevel <= 0.0)
		{
			return Section;
		}

		TArray<FSectionPoint> Out;
		Out.Reserve(Count * 2);

		for (int32 i = 0; i < Count; ++i)
		{
			const FSectionPoint& Here = Section[i];
			const FVector2D Prev = Section[(i + Count - 1) % Count].Point;
			const FVector2D Next = Section[(i + 1) % Count].Point;

			const FVector2D ToPrev = Prev - Here.Point;
			const FVector2D ToNext = Next - Here.Point;
			const double PrevLength = ToPrev.Size();
			const double NextLength = ToNext.Size();

			// Convex corners only. An inside corner catches no light, and cutting one would add
			// material into the void instead of taking an arris off.
			const bool bConvex = FVector2D::CrossProduct(Here.Point - Prev, Next - Here.Point) > 0.0;

			// Measured against the untouched section, so two chamfers sharing an edge each get the
			// same allowance and together still leave it.
			const double Amount = FMath::Min3(Bevel, PrevLength * MaxBevelFraction, NextLength * MaxBevelFraction);

			if (!Here.bBevel || !bConvex || Amount <= UE_KINDA_SMALL_NUMBER)
			{
				Out.Add(Here);
				continue;
			}

			// Both cut points inherit this corner's role, which gives the new facet the role of the
			// face it leads into - right for a chamfer, whose whole job is to soften that face's edge.
			FSectionPoint Entering = Here;
			Entering.Point = Here.Point + ToPrev * (Amount / PrevLength);
			Entering.bBevel = false;
			Out.Add(Entering);

			FSectionPoint Leaving = Here;
			Leaving.Point = Here.Point + ToNext * (Amount / NextLength);
			Leaving.bBevel = false;
			Out.Add(Leaving);
		}

		return Out;
	}

	/**
	 * Sweeps a closed cross-section along local +X, placing the result with Anchor.
	 *
	 * Section points are (Y, Z) and must be wound counter-clockwise in that plane. (Y, Z, X) is a
	 * cyclic permutation of (X, Y, Z) and so exactly the right-handed frame FHFMeshOps::AppendPrism
	 * works in - which is why the identical winding gives outward-facing normals here. That matters
	 * more than it looks: an inverted solid still renders as a solid while reporting negative volume
	 * and silently defeating every mesh boolean downstream.
	 */
	bool AppendSweptSection(FDynamicMesh3& Mesh, const TArray<FSectionPoint>& Section,
		double StartX, double EndX, EHFSurfaceRole CapRole, const FTransform& Anchor)
	{
		const int32 Count = Section.Num();
		if (Count < 3 || EndX - StartX <= UE_KINDA_SMALL_NUMBER)
		{
			return false;
		}

		TArray<FVector2D> Flat;
		Flat.Reserve(Count);
		for (const FSectionPoint& Point : Section)
		{
			Flat.Add(Point.Point);
		}

		if (FHFMeshOps::SignedArea(Flat) <= 0.0)
		{
			// Reversing would have to permute the per-edge roles with the points, and every section
			// built here is authored counter-clockwise. Refusing beats quietly emitting a solid
			// turned inside out, which looks identical in the viewport.
			UE_LOG(LogHouseForge, Warning,
				TEXT("Swept section is not wound counter-clockwise; no geometry emitted."));
			return false;
		}

		TArray<FVector2d> Points;
		Points.Reserve(Count);
		for (const FVector2D& Point : Flat)
		{
			Points.Add(FVector2d(Point.X, Point.Y));
		}

		// Handles the concave sections - a stepped profile has a genuine inside corner.
		//
		// bOrientAsHoleFill=false. The default is TRUE and winds the output OPPOSITE to the input
		// polygon, which is what a hole patch wants and the exact opposite of what a cap wants. The
		// section frame below is right-handed and the side walls are wound correctly, so taking the
		// default produced a solid whose two end caps alone were inside out - and since a sweep along
		// local +X puts the entire volume integral on those caps, the whole moulding came back with
		// its volume negated.
		TArray<FIndex3i> Triangles;
		PolygonTriangulation::TriangulateSimplePolygon(Points, Triangles, /*bOrientAsHoleFill*/ false);
		if (Triangles.IsEmpty())
		{
			return false;
		}

		TArray<int32> StartVerts;
		TArray<int32> EndVerts;
		StartVerts.Reserve(Count);
		EndVerts.Reserve(Count);
		for (const FVector2D& Point : Flat)
		{
			StartVerts.Add(Mesh.AppendVertex(Anchor.TransformPosition(FVector(StartX, Point.X, Point.Y))));
			EndVerts.Add(Mesh.AppendVertex(Anchor.TransformPosition(FVector(EndX, Point.X, Point.Y))));
		}

		const int32 CapGroup = FHFMeshOps::GroupForRole(CapRole);
		for (const FIndex3i& Tri : Triangles)
		{
			Mesh.AppendTriangle(StartVerts[Tri.A], StartVerts[Tri.B], StartVerts[Tri.C], CapGroup);
			Mesh.AppendTriangle(EndVerts[Tri.C], EndVerts[Tri.B], EndVerts[Tri.A], CapGroup);
		}

		for (int32 i = 0; i < Count; ++i)
		{
			const int32 Next = (i + 1) % Count;
			const int32 Group = FHFMeshOps::GroupForRole(Section[i].FaceRole);
			Mesh.AppendTriangle(StartVerts[i], EndVerts[Next], StartVerts[Next], Group);
			Mesh.AppendTriangle(StartVerts[i], EndVerts[i], EndVerts[Next], Group);
		}

		return true;
	}

	/** The cornice cross-section in (Y, Z), counter-clockwise, arrises tagged for chamfering. */
	TArray<FSectionPoint> BuildCorniceSection(const FHFCorniceParams& P)
	{
		// The back is glued to the carcass and never seen; every other face is finished moulding and
		// retextures with the shutters it caps.
		constexpr EHFSurfaceRole Finished = EHFSurfaceRole::ShutterLaminate;
		constexpr EHFSurfaceRole Concealed = EHFSurfaceRole::JoineryCarcass;

		const double Front = P.FrontY();
		const double Back = P.BackY();
		const double Top = P.Height;
		const double Size = P.ProfileSize;

		TArray<FSectionPoint> Section;

		// Counter-clockwise from the back underside: up the back, forward along the top, down the
		// front, then the profile feature, then back along the underside.
		Section.Add(FSectionPoint{ FVector2D(Back, 0.0), Concealed, false });
		Section.Add(FSectionPoint{ FVector2D(Back, Top), Finished, false });
		Section.Add(FSectionPoint{ FVector2D(Front, Top), Finished, true });

		const bool bHasFeature = Size > UE_KINDA_SMALL_NUMBER && P.Profile != EHFCorniceProfile::Square;
		if (!bHasFeature)
		{
			Section.Add(FSectionPoint{ FVector2D(Front, 0.0), Finished, true });
			return Section;
		}

		switch (P.Profile)
		{
		case EHFCorniceProfile::Splay:
			Section.Add(FSectionPoint{ FVector2D(Front, Size), Finished, true });
			Section.Add(FSectionPoint{ FVector2D(Front + Size, 0.0), Finished, true });
			break;

		case EHFCorniceProfile::Cove:
		{
			// Centred on the arris a square profile would have had, so the section is scooped rather
			// than rounded off. A cove is concave, and under a downlight the difference between a
			// scoop and a bullnose is the difference between a moulding and a bar of soap.
			const int32 Segments = FMath::Clamp(P.CoveSegments, 2, 32);
			Section.Add(FSectionPoint{ FVector2D(Front, Size), Finished, true });

			for (int32 i = 1; i < Segments; ++i)
			{
				const double Angle = FMath::DegreesToRadians(90.0 * (1.0 - static_cast<double>(i) / Segments));
				Section.Add(FSectionPoint{
					FVector2D(Front + Size * FMath::Cos(Angle), Size * FMath::Sin(Angle)), Finished, false });
			}

			Section.Add(FSectionPoint{ FVector2D(Front + Size, 0.0), Finished, true });
			break;
		}

		case EHFCorniceProfile::Stepped:
			Section.Add(FSectionPoint{ FVector2D(Front, Size), Finished, true });
			// The inside corner of the step takes no chamfer - nothing catches light in a valley.
			Section.Add(FSectionPoint{ FVector2D(Front + Size, Size), Finished, false });
			Section.Add(FSectionPoint{ FVector2D(Front + Size, 0.0), Finished, true });
			break;

		default:
			Section.Add(FSectionPoint{ FVector2D(Front, 0.0), Finished, true });
			break;
		}

		return Section;
	}
}

FHFCorniceParams FHFJoineryKit::SanitiseCornice(const FHFCorniceParams& Params)
{
	FHFCorniceParams Out = Params;

	Out.Width = FMath::Max(Out.Width, 0.0);
	Out.Depth = FMath::Max(Out.Depth, 0.0);
	Out.Height = FMath::Max(Out.Height, 0.0);

	// A moulding projecting further than it is deep would have its back face out in front of the
	// shutters, fixed to thin air. Clamped so the back always lands on something.
	Out.Projection = FMath::Clamp(Out.Projection, 0.0, Out.Depth);

	// The feature is cut out of the section, so it cannot be bigger than the section it comes out of.
	Out.ProfileSize = FMath::Clamp(Out.ProfileSize, 0.0,
		FMath::Min(Out.Depth, Out.Height) * MaxProfileFraction);

	Out.EdgeBevel = FMath::Max(Out.EdgeBevel, 0.0);
	Out.CoveSegments = FMath::Clamp(Out.CoveSegments, 2, 32);

	return Out;
}

FDynamicMesh3 FHFJoineryKit::GenerateCornice(const FHFCorniceParams& Params)
{
	FDynamicMesh3 Mesh;
	FHFMeshOps::InitialiseMesh(Mesh);

	if (AppendCornice(Mesh, Params, FTransform::Identity))
	{
		FHFMeshOps::ApplyWorldScaleUVs(Mesh);
	}

	return Mesh;
}

bool FHFJoineryKit::AppendCornice(FDynamicMesh3& Mesh, const FHFCorniceParams& Params, const FTransform& Anchor)
{
	const FHFCorniceParams P = SanitiseCornice(Params);
	if (!P.IsValid())
	{
		// No moulding is a real answer - an open-topped run has none - rather than a failure.
		return false;
	}

	// Appending into a mesh somebody else owns, which may not have been set up for roles yet. Without
	// groups the section's roles would be dropped without a word, leaving geometry the material panel
	// cannot target; enabling them costs nothing and the alternative fails invisibly.
	if (!Mesh.HasTriangleGroups())
	{
		Mesh.EnableTriangleGroups();
	}
	if (!Mesh.HasAttributes())
	{
		Mesh.EnableAttributes();
	}

	const TArray<FSectionPoint> Section = ChamferSection(BuildCorniceSection(P), P.EdgeBevel);

	// The cut ends are as much on show as the front: a run either stops in the open or is mitred into
	// its return, and both want the finished face.
	return AppendSweptSection(Mesh, Section, 0.0, P.Width, EHFSurfaceRole::ShutterLaminate, Anchor);
}

// -------------------------------------------------------------------------------------- drawers

namespace
{
	/**
	 * Drawer front heights as fronts are actually cut: 150 to 500 mm in 50 mm steps.
	 *
	 * A ladder rather than a free height because a bank divided by arithmetic looks divided by
	 * arithmetic. Three drawers in a 720 carcass are 150/250/300 in every kitchen in the country,
	 * and the reason is that those are the sizes a cutlery tray, a crockery drawer and a pan drawer
	 * need - not that 720 happens to divide that way.
	 */
	constexpr double DrawerFrontLadder[] = { 15.0, 20.0, 25.0, 30.0, 35.0, 40.0, 45.0, 50.0 };

	/** Telescopic runner nominal lengths: 250 to 550 mm in 50 mm steps. */
	constexpr double DrawerRunnerLadder[] = { 25.0, 30.0, 35.0, 40.0, 45.0, 50.0, 55.0 };

	/**
	 * What a runner gives up to the carcass depth it is specified against.
	 *
	 * Bigger than the bare clearances below, and deliberately: a 580 carcass takes a 500 runner on
	 * site, not the 550 the front setback and the rear gap alone would leave room for. The box has
	 * to clear the whole front assembly and the services that run down the back of a kitchen, and
	 * runners are sold against carcass depth on exactly that assumption.
	 */
	constexpr double DrawerRunnerFitAllowance = 5.0;

	/** Runner front end, back from the carcass front edge. */
	constexpr double DrawerRunnerFrontSetback = 0.3;

	/** Runner back end to the back panel. Below 10 mm there is nowhere for the fixings to land. */
	constexpr double DrawerRunnerRearClearance = 1.2;

	/** Box side to carcass side: 12.5 mm a side, which is what a side-mounted runner needs. */
	constexpr double DrawerRunnerSideClearance = 1.25;

	/**
	 * The three members of the runner, across the 12.5 mm the carcass leaves it.
	 *
	 * THREE, not two, and that is what makes full extension possible at all. A two-member slide is a
	 * three-quarter-extension slide by construction: the drawer member and the cabinet member are
	 * both the runner's length, so pulling out by the whole of it leaves them overlapping by exactly
	 * nothing, and the drawer, its front and its rails are cantilevered on air with a zero-thickness
	 * sliver of rail still in the cabinet. That is unmissable the moment a drawer is opened on
	 * camera and invisible in every closed still.
	 *
	 * A real full-extension slide carries the drawer member on an intermediate that itself stays
	 * half-engaged with the cabinet member. Geared to travel half as far, it keeps both joints
	 * overlapped by half the runner at any extension. It moves, so it is its own part with its own
	 * pivot and travel - see FHFPartMotion::DrivenByPartId, and .claude/rules/04-conventions.md.
	 *
	 * The five figures sum to DrawerRunnerSideClearance exactly: 0.45 + 0.05 + 0.25 + 0.05 + 0.45.
	 */
	constexpr double DrawerRailThickness = 0.45;
	constexpr double DrawerIntermediateThickness = 0.25;
	constexpr double DrawerChannelThickness = 0.45;

	/**
	 * Running clearance between one runner member and the next.
	 *
	 * Half a millimetre, and not decoration: two members sharing a face would z-fight down the whole
	 * length of the runner every time a drawer is opened on camera.
	 */
	constexpr double DrawerRunnerAirGap = 0.05;

	/** Section heights, nested: the channel carries the intermediate, which carries the rail. */
	constexpr double DrawerRailHeight = 2.0;
	constexpr double DrawerIntermediateHeight = 2.5;
	constexpr double DrawerChannelHeight = 3.0;

	/** How much of a full-extension drawer's travel the intermediate member takes. */
	constexpr double DrawerIntermediateTravelShare = 0.5;

	/** Part id of the intermediate carrying a drawer, formed from the drawer's own. */
	FName DrawerRunnerPartId(FName DrawerPartId)
	{
		return FName(*FString::Printf(TEXT("%sRunner"), *DrawerPartId.ToString()));
	}

	/** Box front face behind the carcass front plane. With a 19 mm front that is the trade's 20 mm. */
	constexpr double DrawerBoxFrontClearance = 0.1;

	/** How much shorter the box is than the front it is screwed to. */
	constexpr double DrawerBoxHeightRelief = 5.0;

	/** How far the bottom sits up from the bottom of the sides, sitting in its groove. */
	constexpr double DrawerBottomGroove = 1.0;

	/** A front below 130 mm holds nothing: a cutlery tray alone needs 84-100 mm clear inside. */
	constexpr double DrawerMinFrontHeight = 13.0;

	/**
	 * Everything a drawer's geometry is set out from, in drawer-local space.
	 *
	 * Computed once and shared by the front, the box, the runner mounts and the motion, so those
	 * four cannot drift apart. A rail generated from one set of numbers and a channel from another
	 * is the kind of defect that shows up as a drawer binding halfway out, on camera, months later.
	 */
	struct FDrawerLayout
	{
		bool bValid = false;
		FHFDrawerParams P;

		/** The applied front. */
		double FrontX0 = 0.0, FrontX1 = 0.0;
		double FrontY0 = 0.0, FrontY1 = 0.0;
		double FrontZ0 = 0.0, FrontZ1 = 0.0;

		/** The box, outside faces. */
		double BoxX0 = 0.0, BoxX1 = 0.0;
		double BoxY0 = 0.0, BoxY1 = 0.0;
		double BoxZ0 = 0.0, BoxZ1 = 0.0;

		/** The runner members, shared by the drawer's rail, the intermediate and the channel. */
		double RailY0 = 0.0, RailY1 = 0.0;
		double RailZ0 = 0.0, RailZ1 = 0.0;

		/** Inside faces of the carcass sides, which is what the box has to fit between. */
		double InternalX0 = 0.0, InternalX1 = 0.0;

		double BoxDepth = 0.0;
		double Travel = 0.0;
	};

	FDrawerLayout MakeDrawerLayout(const FHFDrawerParams& Params)
	{
		FDrawerLayout L;
		L.P = FHFJoineryKit::SanitiseDrawer(Params);
		const FHFDrawerParams& P = L.P;

		// No runner fits means no drawer, and saying so is the honest answer: a box built anyway
		// would be a box longer than the cabinet holding it.
		if (!P.IsValid() || P.RunnerLength <= 0.0)
		{
			return L;
		}

		const double HalfReveal = P.RevealGap * 0.5;

		L.FrontX0 = HalfReveal;
		L.FrontX1 = P.ModuleWidth - HalfReveal;
		L.FrontY1 = -P.BackClearance;
		L.FrontY0 = L.FrontY1 - P.FrontThickness;
		L.FrontZ0 = HalfReveal;
		L.FrontZ1 = P.ModuleHeight - HalfReveal;

		L.InternalX0 = P.CarcassSideThickness;
		L.InternalX1 = P.ModuleWidth - P.CarcassSideThickness;

		L.BoxX0 = L.InternalX0 + DrawerRunnerSideClearance;
		L.BoxX1 = L.InternalX1 - DrawerRunnerSideClearance;

		// Two sides, a bottom butting between them, and something left over to put a spoon in.
		if (L.BoxX1 - L.BoxX0 <= 4.0 * P.BoxSideThickness)
		{
			return L;
		}

		// The same guard the width gets, on the other two axes. A box shallower than its own front
		// and back boards has them passing through each other, and one shallower in height than its
		// bottom board silently loses the bottom - both of which come back reporting bValid.
		if (P.RunnerLength <= 2.0 * P.BoxSideThickness)
		{
			return L;
		}

		L.BoxDepth = P.RunnerLength;
		L.BoxY0 = DrawerBoxFrontClearance;
		L.BoxY1 = L.BoxY0 + L.BoxDepth;

		// The box is shorter than its front. That is not a detail: the gap above is what a hand goes
		// over to reach into the drawer, and the gap below is where the runner's fixings land.
		const double FrontHeight = P.FrontHeight();
		const double BoxHeight = FMath::Max(FrontHeight - DrawerBoxHeightRelief, FrontHeight * 0.5);

		if (BoxHeight <= P.BoxBottomThickness)
		{
			return L;
		}

		// Four tenths of the relief below and six above, so the front stands a little proud of the
		// box top rather than level with it, which is what stops a drawer reading as an open crate.
		L.BoxZ0 = L.FrontZ0 + (FrontHeight - BoxHeight) * 0.4;
		L.BoxZ1 = L.BoxZ0 + BoxHeight;

		L.RailY0 = DrawerRunnerFrontSetback;
		L.RailY1 = L.RailY0 + P.RunnerLength;

		const double RailHeight = FMath::Min(DrawerRailHeight, BoxHeight * 0.5);
		const double RailCentreZ = (L.BoxZ0 + L.BoxZ1) * 0.5;
		L.RailZ0 = RailCentreZ - RailHeight * 0.5;
		L.RailZ1 = RailCentreZ + RailHeight * 0.5;

		// A full-extension runner brings the whole box out; a three-quarter one leaves a quarter of
		// it behind, which is exactly why the back of that drawer is the awkward one.
		L.Travel = (P.Extension == EHFDrawerExtension::Full) ? L.BoxDepth : L.BoxDepth * 0.75;

		L.bValid = true;
		return L;
	}

	/**
	 * X span of one runner member on one side of the box, counted out from the box side.
	 *
	 * Derived here rather than at each of the four places that need it, for the same reason the
	 * layout exists at all: a rail generated from one set of numbers and a channel from another is
	 * the defect that shows up as a drawer binding halfway out, on camera, months later.
	 *
	 * @param Member 0 the drawer's own rail, 1 the intermediate, 2 the cabinet's channel.
	 */
	void DrawerRunnerMemberX(const FDrawerLayout& L, int32 Member, bool bLeftSide,
		double& OutX0, double& OutX1)
	{
		constexpr double Thicknesses[] = {
			DrawerRailThickness, DrawerIntermediateThickness, DrawerChannelThickness };

		// Distance from the box side out to this member's near face: everything nearer, plus a
		// running clearance for each joint crossed.
		double Offset = 0.0;
		for (int32 Nearer = 0; Nearer < Member; ++Nearer)
		{
			Offset += Thicknesses[Nearer] + DrawerRunnerAirGap;
		}

		const double BoxFace = bLeftSide ? L.BoxX0 : L.BoxX1;
		const double Outward = bLeftSide ? -1.0 : 1.0;

		const double Near = BoxFace + Outward * Offset;
		const double Far = Near + Outward * Thicknesses[Member];

		OutX0 = FMath::Min(Near, Far);
		OutX1 = FMath::Max(Near, Far);
	}

	/** Nearest height on the ladder. */
	double NearestDrawerFrontHeight(double Target)
	{
		double Best = DrawerFrontLadder[0];
		double BestDelta = FMath::Abs(Target - Best);

		for (const double Rung : DrawerFrontLadder)
		{
			const double Delta = FMath::Abs(Target - Rung);
			if (Delta < BestDelta)
			{
				Best = Rung;
				BestDelta = Delta;
			}
		}
		return Best;
	}

	/** The rung below this height, or 0 when it is already the shortest front made. */
	double NextDrawerFrontHeightBelow(double Height)
	{
		double Best = 0.0;
		for (const double Rung : DrawerFrontLadder)
		{
			if (Rung < Height - UE_KINDA_SMALL_NUMBER)
			{
				Best = Rung;
			}
		}
		return Best;
	}

	/** The rung above this height, or 0 when it is already the deepest front made. */
	double NextDrawerFrontHeightAbove(double Height)
	{
		for (const double Rung : DrawerFrontLadder)
		{
			if (Rung > Height + UE_KINDA_SMALL_NUMBER)
			{
				return Rung;
			}
		}
		return 0.0;
	}

	/** The step between rungs. What "a front one size up" is worth. */
	constexpr double DrawerFrontRung = 5.0;

	/** The box, its bottom and the drawer half of its runners - everything but the applied front. */
	void AppendDrawerBoxBoards(FDynamicMesh3& Mesh, const FDrawerLayout& L)
	{
		const double T = L.P.BoxSideThickness;

		// Sides run the full depth and height; the front and back butt between them. Butted rather
		// than lapped for the same reason the plinth is a ladder frame - the mesh then holds the
		// board the box is really made of instead of counting all four corners twice.
		AppendRail(Mesh, FVector3d(L.BoxX0, L.BoxY0, L.BoxZ0), FVector3d(L.BoxX0 + T, L.BoxY1, L.BoxZ1),
			EHFSurfaceRole::JoineryCarcass);
		AppendRail(Mesh, FVector3d(L.BoxX1 - T, L.BoxY0, L.BoxZ0), FVector3d(L.BoxX1, L.BoxY1, L.BoxZ1),
			EHFSurfaceRole::JoineryCarcass);
		AppendRail(Mesh, FVector3d(L.BoxX0 + T, L.BoxY0, L.BoxZ0), FVector3d(L.BoxX1 - T, L.BoxY0 + T, L.BoxZ1),
			EHFSurfaceRole::JoineryCarcass);
		AppendRail(Mesh, FVector3d(L.BoxX0 + T, L.BoxY1 - T, L.BoxZ0), FVector3d(L.BoxX1 - T, L.BoxY1, L.BoxZ1),
			EHFSurfaceRole::JoineryCarcass);

		// The bottom sits in a groove above the bottom of the sides, which is where a 6 mm bottom
		// goes and why a real drawer shows a lip of side below its floor.
		const double BoxHeight = L.BoxZ1 - L.BoxZ0;
		const double Groove = FMath::Min(DrawerBottomGroove,
			FMath::Max(0.0, (BoxHeight - L.P.BoxBottomThickness) * 0.5));
		const double BottomZ0 = L.BoxZ0 + Groove;
		const double BottomZ1 = BottomZ0 + L.P.BoxBottomThickness;

		if (BottomZ1 < L.BoxZ1)
		{
			AppendRail(Mesh, FVector3d(L.BoxX0 + T, L.BoxY0 + T, BottomZ0),
				FVector3d(L.BoxX1 - T, L.BoxY1 - T, BottomZ1), EHFSurfaceRole::JoineryCarcass);
		}

		// The drawer's own member of each runner, screwed to the outside of the box side and
		// travelling with it. The intermediate travels half as far and the cabinet's channel stays
		// put - see GenerateDrawerRunnerIntermediate and GenerateDrawerRunnerMounts - which is the
		// whole reason a runner cannot be one piece of geometry.
		static constexpr bool Sides[] = { true, false };
		for (const bool bLeftSide : Sides)
		{
			double X0 = 0.0, X1 = 0.0;
			DrawerRunnerMemberX(L, /*Member*/ 0, bLeftSide, X0, X1);
			AppendRail(Mesh, FVector3d(X0, L.RailY0, L.RailZ0), FVector3d(X1, L.RailY1, L.RailZ1),
				EHFSurfaceRole::MetalHardware);
		}
	}
}

double FHFJoineryKit::SelectRunnerLength(double CarcassDepth)
{
	const double Fits = CarcassDepth - DrawerRunnerFitAllowance;

	double Best = 0.0;
	for (const double Rung : DrawerRunnerLadder)
	{
		if (Rung <= Fits)
		{
			Best = Rung;
		}
	}
	return Best;
}

FHFDrawerParams FHFJoineryKit::SanitiseDrawer(const FHFDrawerParams& Params)
{
	FHFDrawerParams Out = Params;

	Out.ModuleWidth = FMath::Max(Out.ModuleWidth, 0.0);
	Out.ModuleHeight = FMath::Max(Out.ModuleHeight, 0.0);
	Out.CarcassDepth = FMath::Max(Out.CarcassDepth, 0.0);
	Out.BackClearance = FMath::Max(Out.BackClearance, 0.0);

	// A reveal wider than the module leaves no front at all.
	Out.RevealGap = FMath::Clamp(Out.RevealGap, 0.0, FMath::Min(Out.ModuleWidth, Out.ModuleHeight));

	Out.FrontThickness = FMath::Max(Out.FrontThickness, MinBoardThickness);
	Out.BoxSideThickness = FMath::Max(Out.BoxSideThickness, MinBoardThickness);
	Out.BoxBottomThickness = FMath::Max(Out.BoxBottomThickness, MinBoardThickness);

	// Sides that meet in the middle leave no cavity to put a box in.
	Out.CarcassSideThickness = FMath::Clamp(Out.CarcassSideThickness, 0.0,
		FMath::Max(0.0, Out.ModuleWidth * 0.5 - MinBoardThickness));

	// An explicit runner length is a decision and is honoured whenever the carcass can physically
	// take it - a wardrobe drawer specified at 450 in a body that would auto-select 500 is somebody
	// matching hardware they already have. One that cannot fit is dropped to the longest that can,
	// because the alternative is a box driven out through the back panel.
	//
	// It also has to be a runner. Runners are made from 250 up, and honouring anything shorter
	// builds a box whose front and back boards pass through each other and whose back board breaks
	// out through the carcass front plane into the applied front - reported as a valid drawer, with
	// the overlap counted twice in its volume. Falling back to the longest that fits is the same
	// documented answer a length that cannot be used already gets.
	const double Fits = Out.CarcassDepth - DrawerRunnerFrontSetback - DrawerRunnerRearClearance;
	const double ShortestRunner = DrawerRunnerLadder[0];

	Out.RunnerLength = (Out.RunnerLength >= ShortestRunner && Out.RunnerLength <= Fits)
		? Out.RunnerLength
		: SelectRunnerLength(Out.CarcassDepth);

	return Out;
}

double FHFJoineryKit::DrawerBoxDepth(const FHFDrawerParams& Params)
{
	const FDrawerLayout L = MakeDrawerLayout(Params);
	return L.bValid ? L.BoxDepth : 0.0;
}

double FHFJoineryKit::DrawerTravel(const FHFDrawerParams& Params)
{
	const FDrawerLayout L = MakeDrawerLayout(Params);
	return L.bValid ? L.Travel : 0.0;
}

FDynamicMesh3 FHFJoineryKit::GenerateDrawerFront(const FHFDrawerParams& Params)
{
	FDynamicMesh3 Mesh;
	FHFMeshOps::InitialiseMesh(Mesh);

	const FDrawerLayout L = MakeDrawerLayout(Params);
	if (!L.bValid)
	{
		return Mesh;
	}

	// Finished like a shutter, because that is what it is: 18 ply with laminate on the face and a
	// balancing sheet behind. It retextures with the shutters beside it for the same reason.
	AppendRail(Mesh, FVector3d(L.FrontX0, L.FrontY0, L.FrontZ0), FVector3d(L.FrontX1, L.FrontY1, L.FrontZ1),
		EHFSurfaceRole::ShutterLaminate);

	FHFMeshOps::ApplyWorldScaleUVs(Mesh);
	return Mesh;
}

FDynamicMesh3 FHFJoineryKit::GenerateDrawerBox(const FHFDrawerParams& Params)
{
	FDynamicMesh3 Mesh;
	FHFMeshOps::InitialiseMesh(Mesh);

	const FDrawerLayout L = MakeDrawerLayout(Params);
	if (!L.bValid)
	{
		return Mesh;
	}

	AppendDrawerBoxBoards(Mesh, L);

	FHFMeshOps::ApplyWorldScaleUVs(Mesh);
	return Mesh;
}

FDynamicMesh3 FHFJoineryKit::GenerateDrawer(const FHFDrawerParams& Params)
{
	FDynamicMesh3 Mesh;
	FHFMeshOps::InitialiseMesh(Mesh);

	const FDrawerLayout L = MakeDrawerLayout(Params);
	if (!L.bValid)
	{
		return Mesh;
	}

	AppendRail(Mesh, FVector3d(L.FrontX0, L.FrontY0, L.FrontZ0), FVector3d(L.FrontX1, L.FrontY1, L.FrontZ1),
		EHFSurfaceRole::ShutterLaminate);
	AppendDrawerBoxBoards(Mesh, L);

	// Applied once over the assembled part rather than per piece: ApplyWorldScaleUVs reprojects the
	// whole mesh, so doing it twice would only throw the first pass away.
	FHFMeshOps::ApplyWorldScaleUVs(Mesh);
	return Mesh;
}

FDynamicMesh3 FHFJoineryKit::GenerateDrawerRunnerMounts(const FHFDrawerParams& Params)
{
	FDynamicMesh3 Mesh;
	FHFMeshOps::InitialiseMesh(Mesh);

	const FDrawerLayout L = MakeDrawerLayout(Params);
	if (!L.bValid)
	{
		return Mesh;
	}

	// Taller than the members it carries, and on the same centre line, which is what a channel
	// section looks like from the front.
	const double ChannelHeight = FMath::Min(DrawerChannelHeight, L.BoxZ1 - L.BoxZ0);
	const double CentreZ = (L.RailZ0 + L.RailZ1) * 0.5;
	const double Z0 = CentreZ - ChannelHeight * 0.5;
	const double Z1 = CentreZ + ChannelHeight * 0.5;

	// Outermost of the three members, against the carcass side, with a running clearance to the
	// intermediate inside it rather than to the panel outside: the panel is somebody else's
	// geometry, and a channel that shared a plane with it would z-fight whether the drawer moved
	// or not.
	static constexpr bool Sides[] = { true, false };
	for (const bool bLeftSide : Sides)
	{
		double X0 = 0.0, X1 = 0.0;
		DrawerRunnerMemberX(L, /*Member*/ 2, bLeftSide, X0, X1);

		// Only where the carcass actually leaves room for it. An inset module - an internal drawer
		// in a wardrobe bay - lands short of the carcass sides and the composer owes it packers.
		if (X0 >= L.InternalX0 - UE_KINDA_SMALL_NUMBER && X1 <= L.InternalX1 + UE_KINDA_SMALL_NUMBER)
		{
			AppendRail(Mesh, FVector3d(X0, L.RailY0, Z0), FVector3d(X1, L.RailY1, Z1),
				EHFSurfaceRole::MetalHardware);
		}
	}

	FHFMeshOps::ApplyWorldScaleUVs(Mesh);
	return Mesh;
}

FDynamicMesh3 FHFJoineryKit::GenerateDrawerRunnerIntermediate(const FHFDrawerParams& Params)
{
	FDynamicMesh3 Mesh;
	FHFMeshOps::InitialiseMesh(Mesh);

	const FDrawerLayout L = MakeDrawerLayout(Params);
	if (!L.bValid)
	{
		return Mesh;
	}

	// Between the drawer's rail and the cabinet's channel, and sectioned between them too.
	const double IntermediateHeight = FMath::Min(DrawerIntermediateHeight, L.BoxZ1 - L.BoxZ0);
	const double CentreZ = (L.RailZ0 + L.RailZ1) * 0.5;
	const double Z0 = CentreZ - IntermediateHeight * 0.5;
	const double Z1 = CentreZ + IntermediateHeight * 0.5;

	static constexpr bool Sides[] = { true, false };
	for (const bool bLeftSide : Sides)
	{
		double X0 = 0.0, X1 = 0.0;
		DrawerRunnerMemberX(L, /*Member*/ 1, bLeftSide, X0, X1);
		AppendRail(Mesh, FVector3d(X0, L.RailY0, Z0), FVector3d(X1, L.RailY1, Z1),
			EHFSurfaceRole::MetalHardware);
	}

	FHFMeshOps::ApplyWorldScaleUVs(Mesh);
	return Mesh;
}

FHFPartMotion FHFJoineryKit::DrawerRunnerIntermediateMotion(const FHFDrawerParams& Params, FName DrawerPartId)
{
	FHFPartMotion Motion;

	const FDrawerLayout L = MakeDrawerLayout(Params);
	if (!L.bValid)
	{
		return Motion;
	}

	Motion.Type = EHFMotionType::Slide;
	Motion.Axis = -FVector::YAxisVector;
	Motion.MaxTravelCm = L.Travel * DrawerIntermediateTravelShare;

	// Geared to the drawer rather than posed on its own. Half the travel is the whole mechanism:
	// it keeps the drawer's rail overlapping this member, and this member overlapping the cabinet's
	// channel, by half the runner's length at any extension.
	Motion.DrivenByPartId = DrawerPartId;
	return Motion;
}

FHFMeshPart FHFJoineryKit::BuildDrawerRunnerPart(const FHFDrawerParams& Params, FName DrawerPartId)
{
	FHFMeshPart Part;
	Part.PartId = DrawerRunnerPartId(DrawerPartId);
	Part.Mesh = GenerateDrawerRunnerIntermediate(Params);
	Part.PivotTransform = FTransform::Identity;
	Part.Motion = DrawerRunnerIntermediateMotion(Params, DrawerPartId);
	Part.DefaultOpenAmount = 0.0;
	return Part;
}

bool FHFJoineryKit::BuildDrawerParts(const FHFDrawerParams& Params, FName PartId,
	TArray<FHFMeshPart>& OutParts)
{
	FHFMeshPart Drawer = BuildDrawerPart(Params, PartId);
	if (Drawer.Mesh.TriangleCount() == 0)
	{
		return false;
	}

	FHFMeshPart Runner = BuildDrawerRunnerPart(Params, PartId);
	if (Runner.Mesh.TriangleCount() == 0)
	{
		return false;
	}

	OutParts.Add(MoveTemp(Drawer));
	OutParts.Add(MoveTemp(Runner));
	return true;
}

FHFPartMotion FHFJoineryKit::DrawerMotion(const FHFDrawerParams& Params)
{
	FHFPartMotion Motion;

	const FDrawerLayout L = MakeDrawerLayout(Params);
	if (!L.bValid)
	{
		return Motion;
	}

	Motion.Type = EHFMotionType::Slide;

	// Out of the unit is -Y in the module frame. The direction is carried by the axis rather than by
	// the sign of the travel, so MaxTravelCm reads as the thing it is: how far the drawer comes out.
	Motion.Axis = -FVector::YAxisVector;
	Motion.MaxTravelCm = L.Travel;
	return Motion;
}

FHFMeshPart FHFJoineryKit::BuildDrawerPart(const FHFDrawerParams& Params, FName PartId)
{
	FHFMeshPart Part;
	Part.PartId = PartId;
	Part.Mesh = GenerateDrawer(Params);

	// Identity, because drawer-local space is already the module frame. A slide can pivot anywhere
	// on its line of travel, so this costs nothing and saves every caller a transform.
	Part.PivotTransform = FTransform::Identity;
	Part.Motion = DrawerMotion(Params);
	Part.DefaultOpenAmount = 0.0;
	return Part;
}

bool FHFJoineryKit::GraduateDrawerFronts(const FHFDrawerBankParams& Bank, TArray<double>& OutFrontHeights)
{
	OutFrontHeights.Reset();

	const int32 Count = Bank.DrawerCount;
	const double Reveal = FMath::Max(Bank.Drawer.RevealGap, 0.0);

	if (Count <= 0 || Bank.BankHeight <= 0.0)
	{
		return false;
	}

	// Every front carries one reveal with it, so the gap between two fronts is a full reveal and the
	// bank's own top and bottom carry half of one each - the module convention a shutter uses, which
	// is what lets a bank and a run of shutters sit side by side without a step in the shadow lines.
	const double Available = Bank.BankHeight - Count * Reveal;

	// Below 130 mm a front has nothing behind it worth opening: a cutlery tray alone needs 84-100
	// clear inside. Refusing is better than emitting a bank of slots.
	if (Available < Count * DrawerMinFrontHeight)
	{
		return false;
	}

	const double Ratio = FMath::Max(Bank.GradationRatio, 1.0);

	// A linear ramp from the top front to a bottom front Ratio times deeper. At 2.0 and three
	// drawers that is 0.22/0.33/0.44 of the bank, which is within a hair of the 0.21/0.35/0.43 a
	// three-drawer kitchen bank is actually built to.
	TArray<double> Targets;
	Targets.SetNum(Count);

	double WeightSum = 0.0;
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const double Alpha = (Count > 1) ? static_cast<double>(Index) / static_cast<double>(Count - 1) : 0.0;
		Targets[Index] = 1.0 + (Ratio - 1.0) * Alpha;
		WeightSum += Targets[Index];
	}
	for (double& Target : Targets)
	{
		Target = Available * Target / WeightSum;
	}

	// Snap to the ladder, then make it non-decreasing downward. A bank that gets shallower towards
	// the floor reads as a mistake even to somebody who could not say why.
	TArray<double> Heights;
	Heights.SetNum(Count);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		Heights[Index] = NearestDrawerFrontHeight(Targets[Index]);
	}
	for (int32 Index = 1; Index < Count; ++Index)
	{
		Heights[Index] = FMath::Max(Heights[Index], Heights[Index - 1]);
	}

	auto SumOf = [](const TArray<double>& Values)
	{
		double Total = 0.0;
		for (const double Value : Values)
		{
			Total += Value;
		}
		return Total;
	};

	// Come back down to what the bank can hold, a rung at a time, always off whichever front is
	// furthest above its target. Taking it off the deepest instead would flatten the graduation into
	// the even division the ladder exists to avoid.
	double Sum = SumOf(Heights);
	while (Sum > Available + UE_KINDA_SMALL_NUMBER)
	{
		int32 Chosen = INDEX_NONE;
		double ChosenExcess = -UE_DOUBLE_BIG_NUMBER;

		for (int32 Index = 0; Index < Count; ++Index)
		{
			const double Lower = NextDrawerFrontHeightBelow(Heights[Index]);
			if (Lower <= 0.0)
			{
				continue;
			}
			// Only where it stays at least as deep as the front above it.
			if (Index > 0 && Lower < Heights[Index - 1] - UE_KINDA_SMALL_NUMBER)
			{
				continue;
			}

			const double Excess = Heights[Index] - Targets[Index];
			if (Excess > ChosenExcess)
			{
				ChosenExcess = Excess;
				Chosen = Index;
			}
		}

		if (Chosen == INDEX_NONE)
		{
			break;
		}

		const double Lower = NextDrawerFrontHeightBelow(Heights[Chosen]);
		Sum -= Heights[Chosen] - Lower;
		Heights[Chosen] = Lower;
	}

	if (Sum > Available + UE_KINDA_SMALL_NUMBER)
	{
		// Nothing on the ladder fits: a short bank cut into a lot of drawers. Even fronts are what a
		// maker would cut in that case, and saying so is better than refusing a bank that can be
		// built - the height floor above has already ruled out the ones that cannot.
		const double Even = Available / Count;
		for (double& Height : Heights)
		{
			Height = Even;
		}
		Sum = Available;
	}

	// And back UP a rung at a time, the mirror of the pass above, always onto whichever front is
	// furthest BELOW its target.
	//
	// Without this the whole of the slack landed on the bottom front, unbounded: the ladder tops out
	// at 50, so three drawers in a 200 cm bank came out 45 / 50 / 104.1 and were reported as a
	// successful graduation. A metre-tall drawer front is not a thing any kitchen or wardrobe in a
	// 2BHK contains, and it is not "a few millimetres where they are least visible" either. With the
	// raise pass, only the sub-rung remainder ever reaches the bottom front, which is what the
	// comment below has always claimed.
	while (Sum + DrawerFrontRung <= Available + UE_KINDA_SMALL_NUMBER)
	{
		int32 Chosen = INDEX_NONE;
		double ChosenDeficit = -UE_DOUBLE_BIG_NUMBER;

		for (int32 Index = 0; Index < Count; ++Index)
		{
			const double Higher = NextDrawerFrontHeightAbove(Heights[Index]);
			if (Higher <= 0.0)
			{
				continue;
			}
			// Only where it stays no deeper than the front below it, so the bank keeps reading as a
			// graduation rather than a mistake.
			if (Index + 1 < Count && Higher > Heights[Index + 1] + UE_KINDA_SMALL_NUMBER)
			{
				continue;
			}

			const double Deficit = Targets[Index] - Heights[Index];
			if (Deficit > ChosenDeficit)
			{
				ChosenDeficit = Deficit;
				Chosen = Index;
			}
		}

		if (Chosen == INDEX_NONE)
		{
			break;
		}

		const double Higher = NextDrawerFrontHeightAbove(Heights[Chosen]);
		Sum += Higher - Heights[Chosen];
		Heights[Chosen] = Higher;
	}

	if (Sum + DrawerFrontRung <= Available + UE_KINDA_SMALL_NUMBER)
	{
		// Every front is already the deepest one made and there is still a whole rung of bank left
		// over. That bank cannot be built out of fronts anyone cuts, and it needs more drawers -
		// which is the same answer, in the other direction, as the too-many-fronts refusal above.
		return false;
	}

	// What the ladder leaves over goes on the bottom front. It is the deepest, so a few millimetres
	// are least visible there, and it keeps every reveal at exactly the 3 mm shadow line it is on
	// site - which absorbing the slack into the gaps instead would not.
	Heights[Count - 1] += Available - Sum;

	OutFrontHeights = MoveTemp(Heights);
	return true;
}

bool FHFJoineryKit::BuildDrawerBank(const FHFDrawerBankParams& Bank, TArray<FHFMeshPart>& OutParts,
	FDynamicMesh3* OutFixedMounts)
{
	TArray<double> Heights;
	if (!GraduateDrawerFronts(Bank, Heights))
	{
		return false;
	}

	const double Reveal = FMath::Max(Bank.Drawer.RevealGap, 0.0);
	const FString Prefix = Bank.PartIdPrefix.IsNone() ? TEXT("Drawer") : Bank.PartIdPrefix.ToString();

	// Assembled into a scratch list first. A bank that half-builds would leave a carcass carrying
	// three drawers where four were asked for, and a gap where the fourth should be is worse than an
	// honest refusal.
	TArray<FHFMeshPart> Built;
	Built.Reserve(Heights.Num());

	FDynamicMesh3 Mounts;
	FHFMeshOps::InitialiseMesh(Mounts);

	double ModuleTop = Bank.BankHeight;

	for (int32 Index = 0; Index < Heights.Num(); ++Index)
	{
		FHFDrawerParams DrawerParams = Bank.Drawer;
		DrawerParams.ModuleHeight = Heights[Index] + Reveal;

		const double ModuleZ = ModuleTop - DrawerParams.ModuleHeight;
		ModuleTop = ModuleZ;

		const FName PartId(*FString::Printf(TEXT("%s%d"), *Prefix, Index));

		// The drawer AND the intermediate member of its runner. Emitting the drawer alone would hang
		// it on nothing at full extension, and would do it silently.
		const int32 FirstOfThisDrawer = Built.Num();
		if (!BuildDrawerParts(DrawerParams, PartId, Built))
		{
			return false;
		}

		// Stacked by translation alone, which is the point of drawer-local space being the module
		// frame: nothing about the drawer changes because of where in the bank it ended up.
		for (int32 Moved = FirstOfThisDrawer; Moved < Built.Num(); ++Moved)
		{
			Built[Moved].PivotTransform = FTransform(FVector(0.0, 0.0, ModuleZ));
		}

		if (OutFixedMounts != nullptr)
		{
			FDynamicMesh3 One = GenerateDrawerRunnerMounts(DrawerParams);
			if (One.TriangleCount() > 0)
			{
				MeshTransforms::Translate(One, FVector3d(0.0, 0.0, ModuleZ));
				FHFMeshOps::AppendPreservingRoles(Mounts, One);
			}
		}
	}

	OutParts.Append(MoveTemp(Built));

	if (OutFixedMounts != nullptr && Mounts.TriangleCount() > 0)
	{
		// Appending into a mesh somebody else owns, which may already carry roles of its own. The
		// role-preserving append is what keeps both sets reachable from the material panel - a plain
		// append renumbers the incoming groups and the mounts stop being metal.
		FHFMeshOps::AppendPreservingRoles(*OutFixedMounts, Mounts);
	}

	return true;
}

// ------------------------------------------------------------------------------------ carcasses

namespace
{
	/**
	 * X range a bay's clear volume occupies, in carcass-local space.
	 *
	 * Bounded by a side board at the ends of the run and by half a partition inboard, because a
	 * partition straddles the boundary between two bays and each of them gets half of it. Working it
	 * out in one place is what stops the shelf placement and the partition placement disagreeing by
	 * half a board - an error small enough to look like a fitting gap and large enough to z-fight.
	 */
	void CarcassBayClearX(const FHFCarcassParams& P, int32 Bay, double& OutX0, double& OutX1)
	{
		const int32 Count = P.Bays();
		const int32 Index = FMath::Clamp(Bay, 0, Count - 1);
		const double Module = P.ModuleWidth();
		const double HalfPartition = P.BoardThickness * 0.5;

		OutX0 = (Index == 0) ? P.BoardThickness : Index * Module + HalfPartition;
		OutX1 = (Index == Count - 1) ? P.Width - P.BoardThickness : (Index + 1) * Module - HalfPartition;
	}
}

FHFCarcassParams FHFJoineryKit::SanitiseCarcass(const FHFCarcassParams& Params)
{
	FHFCarcassParams Out = Params;

	Out.Width = FMath::Max(Out.Width, 0.0);
	Out.Depth = FMath::Max(Out.Depth, 0.0);
	Out.Height = FMath::Max(Out.Height, 0.0);

	// A board can be at most half the smallest dimension of the box, or the two sides pass through
	// each other and the mesh reports more material than the carcass contains.
	const double Smallest = FMath::Min3(Out.Width, Out.Depth, Out.Height);
	Out.BoardThickness = FMath::Clamp(Out.BoardThickness, MinBoardThickness,
		FMath::Max(MinBoardThickness, Smallest * 0.5));

	// The back cannot be deeper than the box, and cannot be so deep that nothing is left in front of
	// it: a carcass whose back has eaten its own depth has no inside.
	Out.BackThickness = FMath::Clamp(Out.BackThickness, MinBoardThickness,
		FMath::Max(MinBoardThickness, Out.Depth * 0.5));

	// Bays are only worth having while each one still has clear width to be a bay. Asking for more is
	// asking for partitions standing in each other, so the count comes down to what fits and the
	// caller can read back how many it actually got.
	Out.BayCount = FMath::Max(1, Out.BayCount);

	const double ClearRun = Out.Width - 2.0 * Out.BoardThickness;
	if (ClearRun <= 0.0)
	{
		Out.BayCount = 1;
	}
	else
	{
		// Each bay past the first costs a partition and needs somewhere to put a shutter behind it.
		const double MinBay = FMath::Max(Out.BoardThickness, MinBoardThickness);
		const int32 Fits = FMath::FloorToInt32((ClearRun + Out.BoardThickness) / (MinBay + Out.BoardThickness));
		Out.BayCount = FMath::Clamp(Out.BayCount, 1, FMath::Max(1, Fits));
	}

	return Out;
}

FDynamicMesh3 FHFJoineryKit::GenerateCarcass(const FHFCarcassParams& Params)
{
	FDynamicMesh3 Mesh;
	FHFMeshOps::InitialiseMesh(Mesh);

	const FHFCarcassParams P = SanitiseCarcass(Params);

	// No carcass is a real answer for a run of zero length, and an honest one: an empty mesh appends
	// into a shell harmlessly, where a degenerate box would be a sliver nothing could see and every
	// volume measurement would carry.
	if (!P.IsValid())
	{
		return Mesh;
	}

	constexpr EHFSurfaceRole Carc = EHFSurfaceRole::JoineryCarcass;

	const double T = P.BoardThickness;
	const double W = P.Width;
	const double D = P.Depth;
	const double H = P.Height;
	const double BackY = P.BackFaceY();

	// Too small to frame: a filler this size is a solid packer on site as well, so building it as one
	// is honest rather than a fallback, and it keeps the boards from overlapping.
	if (W <= 2.0 * T || H <= 2.0 * T)
	{
		AppendRail(Mesh, FVector3d(0.0, 0.0, 0.0), FVector3d(W, D, H), Carc);
		FHFMeshOps::ApplyWorldScaleUVs(Mesh);
		return Mesh;
	}

	// Sides full height and full depth; top and bottom running between them; the back behind both.
	// Exactly the lay-up the composition tests build by hand, so their clearances keep measuring the
	// same boards in the same places.
	const double TopFace = P.TopFaceZ();

	// The two gables, in the role each one actually is. A gable dying into a wall or into the next run
	// is bare board; one standing in the room is a finished end, laminated to match the fronts. See
	// FHFCarcassParams::bLeftEndFinished - left as carcass, an exposed end renders as a bare cream
	// slab beside a wall of shutters, which is how the TV column, both nightstands, the wall cabinet
	// and the vanity all came out.
	const EHFSurfaceRole LeftRole = P.bLeftEndFinished ? EHFSurfaceRole::ShutterLaminate : Carc;
	const EHFSurfaceRole RightRole = P.bRightEndFinished ? EHFSurfaceRole::ShutterLaminate : Carc;

	AppendRail(Mesh, FVector3d(0.0, 0.0, 0.0), FVector3d(T, D, H), LeftRole);
	AppendRail(Mesh, FVector3d(W - T, 0.0, 0.0), FVector3d(W, D, H), RightRole);
	AppendRail(Mesh, FVector3d(T, 0.0, 0.0), FVector3d(W - T, D, T), Carc);

	// A BASE UNIT UNDER A COUNTER HAS NO TOP BOARD - the granite is its top, and a bowl 200 deep has
	// to pass through the plane where a board would be. See FHFCarcassParams::bHasTop.
	if (P.bHasTop)
	{
		AppendRail(Mesh, FVector3d(T, 0.0, H - T), FVector3d(W - T, D, H), Carc);
	}

	if (P.bHasBack && D - BackY > UE_KINDA_SMALL_NUMBER)
	{
		AppendRail(Mesh, FVector3d(T, BackY, T), FVector3d(W - T, D, TopFace), Carc);
	}

	// Mid partitions on the internal bay boundaries, butted between the bottom and whatever closes
	// the box above, and stopping at the back panel rather than running through it.
	const double Module = P.ModuleWidth();
	for (int32 Boundary = 1; Boundary < P.Bays(); ++Boundary)
	{
		const double Centre = Boundary * Module;
		AppendRail(Mesh, FVector3d(Centre - T * 0.5, 0.0, T),
			FVector3d(Centre + T * 0.5, BackY, TopFace), Carc);
	}

	FHFMeshOps::ApplyWorldScaleUVs(Mesh);
	return Mesh;
}

FBox FHFJoineryKit::CarcassBayClearVolume(const FHFCarcassParams& Params, int32 Bay)
{
	const FHFCarcassParams P = SanitiseCarcass(Params);

	if (!P.IsValid())
	{
		return FBox(ForceInit);
	}

	double X0 = 0.0;
	double X1 = 0.0;
	CarcassBayClearX(P, Bay, X0, X1);

	const double Z0 = P.BoardThickness;
	const double Z1 = P.TopFaceZ();

	// A bay whose partitions have eaten it has no clear volume, and saying so is what stops a caller
	// placing a shelf stack into a negative width and getting geometry inside out.
	if (X1 - X0 <= UE_KINDA_SMALL_NUMBER || Z1 - Z0 <= UE_KINDA_SMALL_NUMBER
		|| P.BackFaceY() <= UE_KINDA_SMALL_NUMBER)
	{
		return FBox(ForceInit);
	}

	return FBox(FVector(X0, 0.0, Z0), FVector(X1, P.BackFaceY(), Z1));
}
