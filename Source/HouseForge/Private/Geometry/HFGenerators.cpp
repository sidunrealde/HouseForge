// Copyright Siddartha G. All Rights Reserved.

#include "Geometry/HFGenerators.h"

#include "DynamicMesh/MeshTransforms.h"
#include "Geometry/HFMeshOps.h"
#include "Geometry/HFSlidingSetOut.h"
#include "HouseForge.h"

using namespace UE::Geometry;

namespace
{
	// Everything an artist can override now arrives in an FHFOpeningBuildParams. What is left here is
	// the generator's own business: numerical margins that exist to keep the boolean and the mesh
	// well-formed, and which mean nothing outside this file.

	/** How far an opening cutter overshoots the wall faces, in centimetres. */
	constexpr double CutterOvershoot = 5.0;

	/** Below this a member has no useful volume and is skipped rather than emitted degenerate. */
	constexpr double MinMemberSize = 1.0;

	struct FWallFrame
	{
		FVector2D Direction = FVector2D(1.0, 0.0);
		FVector2D Normal = FVector2D(0.0, 1.0);
		double Length = 0.0;
		double YawDegrees = 0.0;
		bool bValid = false;
	};

	FWallFrame MakeWallFrame(const FVector2D& Start, const FVector2D& End)
	{
		FWallFrame Frame;
		Frame.Length = FVector2D::Distance(Start, End);
		if (Frame.Length <= UE_KINDA_SMALL_NUMBER)
		{
			return Frame;
		}

		Frame.Direction = (End - Start) / Frame.Length;
		Frame.Normal = FVector2D(-Frame.Direction.Y, Frame.Direction.X);
		Frame.YawDegrees = FMath::RadiansToDegrees(FMath::Atan2(Frame.Direction.Y, Frame.Direction.X));
		Frame.bValid = true;
		return Frame;
	}

	/**
	 * One surviving stretch of a linear member: where it runs, and how tall it is there.
	 *
	 * A wall interrupted by two columns is three panels of masonry; a wall under a beam over part of
	 * its length is a tall stretch and a short one. Both are the same list.
	 */
	struct FMemberRun
	{
		double Start = 0.0;
		double End = 0.0;
		double TopZ = 0.0;
	};

	/**
	 * Builds a linear member around the structure that displaces it.
	 *
	 * ONE FUNCTION FOR MASONRY AND FOR CONCRETE, because the question is the same either way: a wall
	 * under a beam and a beam landing on a column are both "this member stops where that one starts".
	 * Told the member as a centreline, a width and a height range, so a wall and a beam are the same
	 * shape of problem and neither generator has to know about the other.
	 *
	 * ## Why this is arithmetic and not a mesh boolean
	 *
	 * The obvious implementation is to build the member whole and subtract each structural volume
	 * from it. That was the first implementation, and on the reference flat EIGHT of those
	 * subtractions came back with cracked seams - FMeshBoolean cuts correctly and then fails to weld
	 * the new edges, so IsClosed rejects the result and the cut is abandoned. The member keeps
	 * material the structure also occupies, both draw the shared faces, and that patch of the flat
	 * flashes. Moving the cuts ahead of the openings, onto a pristine box, fixed two of the eight;
	 * nothing fixed the rest, because a boolean's output is a far poorer boolean input than a box and
	 * each cut ran on the wreckage of the last.
	 *
	 * None of that computation was ever needed. Structure meets a linear member in exactly two
	 * shapes, and both of them are ONE-DIMENSIONAL:
	 *
	 *   A COLUMN INTERRUPTS IT.  Full height, so the masonry stops at one face of it and starts
	 *                            again at the other. The wall becomes two panels.
	 *   A BEAM CAPS IT.          Hung from the slab, so the masonry below is built up to the beam
	 *                            soffit. The wall keeps its length and loses its top.
	 *
	 * So the member is worked out as a set of runs along its own length, each with its own top, and
	 * emitted as one box per run. Exact, no tolerance in the geometry at all, cannot fail, and it
	 * takes some seventy mesh booleans out of every house build. Where two runs meet, the faces are
	 * opposed and buried in solid material, which is what a masonry joint is.
	 *
	 * A cut that is neither of those two shapes - one floating in the middle of the member's height,
	 * or a column too short to reach its top - leaves material above it that a run cannot express.
	 * Those fall back to the boolean, which is the right tool for a shape this is not: rare, honest,
	 * and reported when it fails.
	 *
	 * Skewed structure falls back too. Reducing a cut to an interval needs its axes to line up with
	 * this member's, and a projected bounding interval would take out more than the thing meant to
	 * fill it. Nothing in this domain produces one - beams follow wall lines and columns sit on them.
	 */
	void AppendMemberAroundStructure(FDynamicMesh3& Mesh, const TArray<FHFStructuralCut>& Structure,
		const FWallFrame& Frame, const FVector2D& Start, double Width, double BaseZ, double TopZ,
		EHFSurfaceRole Role, const FName& MemberId)
	{
		if (!Frame.bValid || Width <= 0.0 || TopZ <= BaseZ)
		{
			return;
		}

		const FVector2D Midpoint = Start + Frame.Direction * (Frame.Length * 0.5);

		/** At the surface, or through it. A beam the same width as its wall is flush to the last decimal. */
		constexpr double Reached = 0.01;

		TArray<FMemberRun> Runs;
		Runs.Add({ 0.0, Frame.Length, TopZ });

		// Structure this member cannot be built around by arithmetic alone.
		TArray<FHFStructuralCut> Awkward;

		/** Caps every run over [From, To] at NewTopZ; at or below the base that removes the stretch. */
		auto CapRuns = [&Runs, BaseZ](double From, double To, double NewTopZ)
		{
			TArray<FMemberRun> Out;
			Out.Reserve(Runs.Num() + 2);

			for (const FMemberRun& Run : Runs)
			{
				if (To <= Run.Start || From >= Run.End)
				{
					Out.Add(Run);
					continue;
				}

				if (From > Run.Start)
				{
					Out.Add({ Run.Start, From, Run.TopZ });
				}

				const double Capped = FMath::Min(Run.TopZ, NewTopZ);
				if (Capped > BaseZ + UE_KINDA_SMALL_NUMBER)
				{
					Out.Add({ FMath::Max(From, Run.Start), FMath::Min(To, Run.End), Capped });
				}

				if (To < Run.End)
				{
					Out.Add({ To, Run.End, Run.TopZ });
				}
			}

			Runs = MoveTemp(Out);
		};

		for (const FHFStructuralCut& Cut : Structure)
		{
			if (!Cut.IsValid())
			{
				continue;
			}

			// Where the cutter's own axes sit relative to this member's. A quarter turn swaps its
			// two plan half-extents and nothing else, which is why both cases are exact.
			const double Relative = FMath::UnwindDegrees(Cut.YawDegrees - Frame.YawDegrees);
			const bool bAligned = FMath::IsNearlyZero(FMath::Abs(Relative), 0.01)
				|| FMath::IsNearlyEqual(FMath::Abs(Relative), 180.0, 0.01);
			const bool bCrossed = FMath::IsNearlyEqual(FMath::Abs(Relative), 90.0, 0.01);

			if (!bAligned && !bCrossed)
			{
				Awkward.Add(Cut);
				continue;
			}

			const FVector2D Offset = FVector2D(Cut.Centre.X, Cut.Centre.Y) - Midpoint;
			const double HalfAlong = bAligned ? Cut.Extents.X : Cut.Extents.Y;
			const double HalfAcross = bAligned ? Cut.Extents.Y : Cut.Extents.X;
			const double Across = FVector2D::DotProduct(Offset, Frame.Normal);

			// Measured from the member's start, which is how a run is measured.
			const double AlongCentre = FVector2D::DotProduct(Offset, Frame.Direction) + Frame.Length * 0.5;
			const double From = FMath::Max(AlongCentre - HalfAlong, 0.0);
			const double To = FMath::Min(AlongCentre + HalfAlong, Frame.Length);

			// Misses this member entirely, along it or across it.
			if (To <= From
				|| Across - HalfAcross >= Width * 0.5 - Reached
				|| Across + HalfAcross <= -Width * 0.5 + Reached)
			{
				continue;
			}

			const bool bReachesBottom = Cut.BottomZ() <= BaseZ + Reached;
			const bool bReachesTop = Cut.TopZ() >= TopZ - Reached;

			// A RUN IS THE MEMBER'S FULL WIDTH, so only a cut that crosses the whole of that width
			// may shorten one.
			//
			// The test above rejects a cut that misses across entirely, and everything that merely
			// OVERLAPPED then went to CapRuns - which removes the stretch over the member's full
			// thickness and refills only the part the structure actually occupies. A 150 beam in a
			// 230 wall took the whole 230 out and put 150 back, leaving a 40 slot open through the
			// masonry for the length of the run. Each emitted box was still watertight, correctly
			// wound, correctly sized and correctly tagged, so nothing measurable was wrong with any
			// of it and the wall had a hole you could see the sky through.
			//
			// The reference flat never showed it - every beam there is 230 in a 230 wall and every
			// column 450x230 - but a 150 or 200 beam in a 230 external wall is routine on the very
			// drawings this plugin exists to build from.
			//
			// A cut that overlaps without spanning is exactly the "shape this is not" the fallback
			// below was written for: hand it to the boolean, which carves the structure out and
			// leaves the flanking masonry standing, which is what packing a wall around a narrower
			// beam looks like on site.
			const bool bSpansWidth =
				Across - HalfAcross <= -Width * 0.5 + Reached &&
				Across + HalfAcross >= Width * 0.5 - Reached;

			if (!bSpansWidth)
			{
				Awkward.Add(Cut);
				continue;
			}

			if (bReachesBottom && bReachesTop)
			{
				CapRuns(From, To, BaseZ);
			}
			else if (bReachesTop)
			{
				CapRuns(From, To, FMath::Max(Cut.BottomZ(), BaseZ));
			}
			else
			{
				// Material would survive above it. Not a run; hand it to the boolean.
				Awkward.Add(Cut);
			}
		}

		for (const FMemberRun& Run : Runs)
		{
			const double RunLength = Run.End - Run.Start;
			if (RunLength <= UE_KINDA_SMALL_NUMBER || Run.TopZ <= BaseZ + UE_KINDA_SMALL_NUMBER)
			{
				continue;
			}

			const FVector2D Plan = Start + Frame.Direction * (Run.Start + RunLength * 0.5);
			FHFMeshOps::AppendBox(Mesh,
				FVector3d(Plan.X, Plan.Y, (BaseZ + Run.TopZ) * 0.5),
				FVector3d(RunLength * 0.5, Width * 0.5, (Run.TopZ - BaseZ) * 0.5),
				Frame.YawDegrees, Role);
		}

		for (const FHFStructuralCut& Cut : Awkward)
		{
			// Overshot on every face that already lies at or beyond this member's surface, and left
			// exact on every face that cuts into it. Pushing a face further out through a surface it
			// has already reached removes nothing extra, so it is free; leaving the rest exact is what
			// stops the tool eating material the structure does not fill.
			const FVector2D Offset = FVector2D(Cut.Centre.X, Cut.Centre.Y) - Midpoint;
			const double AlongCentre = FVector2D::DotProduct(Offset, Frame.Direction);
			const double AcrossCentre = FVector2D::DotProduct(Offset, Frame.Normal);
			const double HalfLength = Frame.Length * 0.5;
			const double HalfWidth = Width * 0.5;

			double AlongMin = AlongCentre - Cut.Extents.X;
			double AlongMax = AlongCentre + Cut.Extents.X;
			double AcrossMin = AcrossCentre - Cut.Extents.Y;
			double AcrossMax = AcrossCentre + Cut.Extents.Y;
			double Bottom = Cut.BottomZ();
			double Top = Cut.TopZ();

			AlongMin -= (AlongMin <= -HalfLength + Reached) ? CutterOvershoot : 0.0;
			AlongMax += (AlongMax >= HalfLength - Reached) ? CutterOvershoot : 0.0;
			AcrossMin -= (AcrossMin <= -HalfWidth + Reached) ? CutterOvershoot : 0.0;
			AcrossMax += (AcrossMax >= HalfWidth - Reached) ? CutterOvershoot : 0.0;
			Bottom -= (Bottom <= BaseZ + Reached) ? CutterOvershoot : 0.0;
			Top += (Top >= TopZ - Reached) ? CutterOvershoot : 0.0;

			if (Top <= Bottom || AlongMax <= AlongMin || AcrossMax <= AcrossMin)
			{
				continue;
			}

			FDynamicMesh3 Cutter;
			FHFMeshOps::InitialiseMesh(Cutter);

			// Tagged with the member's own role, not the structure's. The faces a subtraction exposes
			// come from the TOOL - see FHFMeshOps::SubtractInPlace - and the face this one exposes is
			// the end of the masonry, which is masonry.
			FHFMeshOps::AppendBox(Cutter,
				FVector3d(Cut.Centre.X, Cut.Centre.Y, (Bottom + Top) * 0.5),
				FVector3d((AlongMax - AlongMin) * 0.5, (AcrossMax - AcrossMin) * 0.5, (Top - Bottom) * 0.5),
				Cut.YawDegrees, Role);

			if (!FHFMeshOps::SubtractInPlace(Mesh, Cutter))
			{
				// Named, because the consequence is invisible in every other measurement: the member
				// keeps material the structure also occupies, both draw the shared faces, and that
				// patch of the flat flashes. Nothing else about either mesh is wrong.
				UE_LOG(LogHouseForge, Warning,
					TEXT("'%s' could not be built around '%s'; they overlap and will z-fight."),
					*MemberId.ToString(), *Cut.SourceId.ToString());
			}
		}
	}
}

FHFStructuralCut FHFGenerators::StructuralCutFor(const FHFBeam& Beam)
{
	FHFStructuralCut Cut;
	Cut.SourceId = Beam.Id;

	const FWallFrame Frame = MakeWallFrame(Beam.Start, Beam.End);
	if (!Frame.bValid || Beam.Width <= 0.0 || Beam.Depth <= 0.0)
	{
		return Cut;
	}

	const FVector2D Midpoint = (Beam.Start + Beam.End) * 0.5;
	Cut.Centre = FVector(Midpoint.X, Midpoint.Y, Beam.SoffitZ - Beam.Depth * 0.5);
	Cut.Extents = FVector(Frame.Length * 0.5, Beam.Width * 0.5, Beam.Depth * 0.5);
	Cut.YawDegrees = Frame.YawDegrees;
	return Cut;
}

FHFStructuralCut FHFGenerators::StructuralCutFor(const FHFWall& Wall)
{
	FHFStructuralCut Cut;
	Cut.SourceId = Wall.Id;

	const FWallFrame Frame = MakeWallFrame(Wall.Start, Wall.End);
	if (!Frame.bValid || Wall.Thickness <= 0.0 || Wall.Height <= 0.0)
	{
		return Cut;
	}

	const FVector2D Midpoint = (Wall.Start + Wall.End) * 0.5;
	Cut.Centre = FVector(Midpoint.X, Midpoint.Y, Wall.BaseZ + Wall.Height * 0.5);
	Cut.Extents = FVector(Frame.Length * 0.5, Wall.Thickness * 0.5, Wall.Height * 0.5);
	Cut.YawDegrees = Frame.YawDegrees;
	return Cut;
}

FHFStructuralCut FHFGenerators::StructuralCutFor(const FHFColumn& Column)
{
	FHFStructuralCut Cut;
	Cut.SourceId = Column.Id;

	if (Column.Size.X <= 0.0 || Column.Size.Y <= 0.0 || Column.Height <= 0.0)
	{
		return Cut;
	}

	Cut.Centre = FVector(Column.Position.X, Column.Position.Y, Column.BaseZ + Column.Height * 0.5);
	Cut.Extents = FVector(Column.Size.X * 0.5, Column.Size.Y * 0.5, Column.Height * 0.5);
	Cut.YawDegrees = Column.RotationDegrees;
	return Cut;
}

FVector2D FHFGenerators::OpeningCentre(const FHFOpening& Opening, const FHFWall& Wall)
{
	const FWallFrame Frame = MakeWallFrame(Wall.Start, Wall.End);
	if (!Frame.bValid)
	{
		return Wall.Start;
	}
	return Wall.Start + Frame.Direction * Opening.OffsetAlongWall;
}

FDynamicMesh3 FHFGenerators::GenerateWall(const FHFWall& Wall, const TArray<FHFOpening>& OpeningsInWall,
	const TArray<FHFStructuralCut>& Structure)
{
	FDynamicMesh3 Mesh;
	FHFMeshOps::InitialiseMesh(Mesh);

	const FWallFrame Frame = MakeWallFrame(Wall.Start, Wall.End);
	if (!Frame.bValid || Wall.Thickness <= 0.0 || Wall.Height <= 0.0)
	{
		return Mesh;
	}

	// THE FRAME FIRST, THEN THE OPENINGS IN WHAT IS LEFT - the order a wall is actually built in.
	// With no structure this emits exactly the one box it always did.
	AppendMemberAroundStructure(Mesh, Structure, Frame, Wall.Start, Wall.Thickness,
		Wall.BaseZ, Wall.BaseZ + Wall.Height, Wall.SurfaceRole, Wall.Id);

	for (const FHFOpening& Opening : OpeningsInWall)
	{
		if (Opening.Width <= 0.0 || Opening.Height <= 0.0)
		{
			continue;
		}

		const FVector2D OpeningPlan = Wall.Start + Frame.Direction * Opening.OffsetAlongWall;
		const double CentreZ = Wall.BaseZ + Opening.SillHeight + Opening.Height * 0.5;

		FDynamicMesh3 Cutter;
		FHFMeshOps::InitialiseMesh(Cutter);

		// Overshoot the wall faces. A cutter flush with the surface leaves coplanar faces that the
		// boolean has to resolve, and it frequently resolves them badly - stray slivers, or no cut
		// at all.
		FHFMeshOps::AppendBox(Cutter,
			FVector3d(OpeningPlan.X, OpeningPlan.Y, CentreZ),
			FVector3d(Opening.Width * 0.5, Wall.Thickness * 0.5 + CutterOvershoot, Opening.Height * 0.5),
			Frame.YawDegrees, Wall.SurfaceRole);

		FHFMeshOps::SubtractInPlace(Mesh, Cutter);
	}

	FHFMeshOps::ApplyWorldScaleUVs(Mesh);
	return Mesh;
}

FDynamicMesh3 FHFGenerators::GenerateFloor(const FHFRoom& Room, double SlabThickness,
	const TArray<FVector2D>& SkirtingGaps, double GapWidth)
{
	FDynamicMesh3 Mesh;
	FHFMeshOps::InitialiseMesh(Mesh);

	if (Room.Boundary.Num() < 3 || SlabThickness <= 0.0)
	{
		return Mesh;
	}

	// The slab sits below the finished floor level, so FloorZ stays the walkable surface.
	//
	// Checked, not dropped. A boundary the triangulator refuses - a bow-tie, which is an everyday
	// mis-read of a plan and which the validator does now reject - would otherwise leave a room
	// whose floor has no triangles at all while its skirting, emitted per edge below, generates
	// perfectly. The room outline still reads correctly from above and the missing floor looks like
	// an unfinished one.
	if (!FHFMeshOps::AppendPrism(Mesh, Room.Boundary, Room.FloorZ - SlabThickness, Room.FloorZ, Room.FloorRole))
	{
		UE_LOG(LogHouseForge, Warning,
			TEXT("Room '%s' has no floor slab: its boundary could not be triangulated."), *Room.Id.ToString());
		return Mesh;
	}

	if (Room.SkirtingHeight > 0.0)
	{
		constexpr double SkirtingDepth = 1.8;
		const int32 Count = Room.Boundary.Num();

		for (int32 i = 0; i < Count; ++i)
		{
			const FVector2D& A = Room.Boundary[i];
			const FVector2D& B = Room.Boundary[(i + 1) % Count];

			const FWallFrame Edge = MakeWallFrame(A, B);
			if (!Edge.bValid)
			{
				continue;
			}

			// Walk the edge, skipping the stretch in front of each doorway. Running skirting
			// straight across an opening is one of the most obvious tells that geometry was
			// generated rather than modelled.
			TArray<TPair<double, double>> Gaps;
			for (const FVector2D& Gap : SkirtingGaps)
			{
				const FVector2D ToGap = Gap - A;
				const double Along = FVector2D::DotProduct(ToGap, Edge.Direction);
				const double Across = FMath::Abs(FVector2D::DotProduct(ToGap, Edge.Normal));

				if (Across < 30.0 && Along > -GapWidth && Along < Edge.Length + GapWidth)
				{
					Gaps.Add({ Along - GapWidth * 0.5, Along + GapWidth * 0.5 });
				}
			}
			Gaps.Sort([](const TPair<double, double>& L, const TPair<double, double>& R) { return L.Key < R.Key; });

			double Cursor = 0.0;
			auto EmitRun = [&](double From, double To)
			{
				const double RunLength = To - From;
				if (RunLength <= 1.0)
				{
					return;
				}

				const FVector2D RunCentre = A + Edge.Direction * ((From + To) * 0.5);
				// Inset so the skirting sits against the wall face rather than through it.
				const FVector2D Offset = Edge.Normal * (SkirtingDepth * 0.5);

				FHFMeshOps::AppendBox(Mesh,
					FVector3d(RunCentre.X + Offset.X, RunCentre.Y + Offset.Y, Room.FloorZ + Room.SkirtingHeight * 0.5),
					FVector3d(RunLength * 0.5, SkirtingDepth * 0.5, Room.SkirtingHeight * 0.5),
					Edge.YawDegrees, EHFSurfaceRole::Skirting);
			};

			for (const TPair<double, double>& Gap : Gaps)
			{
				EmitRun(Cursor, FMath::Min(Gap.Key, Edge.Length));
				Cursor = FMath::Max(Cursor, Gap.Value);
			}
			EmitRun(Cursor, Edge.Length);
		}
	}

	FHFMeshOps::ApplyWorldScaleUVs(Mesh);
	return Mesh;
}

FDynamicMesh3 FHFGenerators::GenerateCeilingSlab(const FHFRoom& Room, double SlabThickness)
{
	FDynamicMesh3 Mesh;
	FHFMeshOps::InitialiseMesh(Mesh);

	if (Room.Boundary.Num() < 3 || SlabThickness <= 0.0)
	{
		return Mesh;
	}

	// The visible underside sits at the room's ceiling height; the slab thickens upward from there.
	const double SoffitZ = Room.FloorZ + Room.CeilingHeight;
	if (!FHFMeshOps::AppendPrism(Mesh, Room.Boundary, SoffitZ, SoffitZ + SlabThickness, EHFSurfaceRole::CeilingSoffit))
	{
		UE_LOG(LogHouseForge, Warning,
			TEXT("Room '%s' has no ceiling slab: its boundary could not be triangulated."), *Room.Id.ToString());
	}

	FHFMeshOps::ApplyWorldScaleUVs(Mesh);
	return Mesh;
}

FDynamicMesh3 FHFGenerators::GenerateCeiling(const FHFFalseCeiling& Ceiling, const FHFRoom& Room,
	const TArray<FVector2D>& FanDrops, double FanDropRadius)
{
	FDynamicMesh3 Mesh;
	FHFMeshOps::InitialiseMesh(Mesh);

	if (Ceiling.Style == EHFCeilingStyle::None || Ceiling.Drop <= 0.0)
	{
		return Mesh;
	}

	// Bulkheads follow their own polygon; everything else follows the room.
	const TArray<FVector2D>& Outline = (Ceiling.ExplicitPolygon.Num() >= 3)
		? Ceiling.ExplicitPolygon
		: Room.Boundary;

	if (Outline.Num() < 3)
	{
		return Mesh;
	}

	const double StructuralZ = Room.FloorZ + Room.CeilingHeight;
	const double SoffitZ = StructuralZ - Ceiling.Drop;

	if (SoffitZ <= Room.FloorZ)
	{
		// Validated against elsewhere, but refusing here too keeps the generator honest when it is
		// called directly.
		return Mesh;
	}

	// Fan rods pass through the ceiling as holes in the panel rather than as boolean cuts. The
	// boolean returned geometry that was not closed for a cut this simple, and a hole in a
	// triangulation cannot half-succeed.
	TArray<TArray<FVector2D>> FanHoles;
	if (FanDropRadius > 0.0)
	{
		for (const FVector2D& Drop : FanDrops)
		{
			FanHoles.Add({
				FVector2D(Drop.X - FanDropRadius, Drop.Y - FanDropRadius),
				FVector2D(Drop.X + FanDropRadius, Drop.Y - FanDropRadius),
				FVector2D(Drop.X + FanDropRadius, Drop.Y + FanDropRadius),
				FVector2D(Drop.X - FanDropRadius, Drop.Y + FanDropRadius)
			});
		}
	}

	// Every piece of a false ceiling is a triangulation that can decline, and a ceiling missing one
	// of its pieces is still an actor with a plausible element count and a correct outline from
	// above. Counted rather than dropped, and reported once at the end.
	int32 Refused = 0;
	auto Checked = [&Refused](bool bBuilt)
	{
		Refused += bBuilt ? 0 : 1;
		return bBuilt;
	};

	/**
	 * Builds a band between an outer loop and its inset.
	 *
	 * Triangulated as a polygon with a hole rather than subtracted as one solid from another. A
	 * mesh boolean resolves that case imperfectly - it returned geometry that was not closed and
	 * reported failure, which silently left every ceiling band solid. The annulus is exact.
	 * The hole also gives the inner fascia for free: the vertical face you actually see standing
	 * under a peripheral ceiling.
	 */
	auto AppendBand = [&Mesh](const TArray<FVector2D>& OuterLoop, double BandWidth,
		double BottomZ, double TopZ, EHFSurfaceRole Role) -> bool
	{
		const TArray<TArray<FVector2D>> Inner = FHFMeshOps::InsetPolygon(OuterLoop, BandWidth);
		if (Inner.IsEmpty())
		{
			// The band is wider than the room, so it becomes a full drop. That is the honest
			// result rather than an error - the geometry is still correct.
			return FHFMeshOps::AppendPrism(Mesh, OuterLoop, BottomZ, TopZ, Role);
		}

		return FHFMeshOps::AppendPrismWithHoles(Mesh, OuterLoop, Inner, BottomZ, TopZ, Role);
	};

	constexpr double PanelThickness = 2.0;

	/**
	 * How far a piece laps into the one beside it, rather than stopping in its face.
	 *
	 * TWO FACES IN THE SAME PLANE FACING THE SAME WAY IS A COIN TOSS the depth test re-tosses every
	 * frame, which is the flashing this flat was reported for. Two faces in the same plane facing
	 * OPPOSITE ways is not: one of them is always the back of a solid the other one closes, so the
	 * pair is sealed inside the assembly and neither is ever drawn. That distinction decides where a
	 * lap is needed and where it would cause the very thing it is meant to prevent:
	 *
	 *   - Sideways, into a piece that stands beside this one - lap it. The tray's inner panel would
	 *     otherwise stop in the band's inner face, two vertical faces in a plane the room can see.
	 *   - Upward, onto a piece that stands ON this one - do NOT lap it. A fascia lapped down into
	 *     its panel would leave 5 mm of both their outer faces in the same plane facing the same
	 *     way, all the way round the edge. Sitting it exactly on the panel's top face instead
	 *     leaves the two outer faces edge to edge, reading as one continuous face, and seals the
	 *     horizontal pair between them.
	 *
	 * 5 mm is under any board tolerance a builder would measure and far above what the depth buffer
	 * confuses at room distances.
	 */
	constexpr double Lap = 0.5;

	/**
	 * THE FASCIA RULE. It is general, so a style added later inherits it instead of re-deriving it:
	 *
	 *   ANY HORIZONTAL SOFFIT EDGE A PERSON IN THE ROOM CAN SEE IS CLOSED TO THE SURFACE ABOVE IT.
	 *
	 * A soffit panel is a 20 mm board. Where its edge stops in mid-air the plenum behind it is on
	 * show, and 20 mm of board with 280 mm of black gap above it does not read as a ceiling - it
	 * reads as a sheet hanging in the room, which is exactly what was reported. The vertical fascia
	 * from the top of the panel up to the structure is what turns the drop into a boxed soffit.
	 *
	 * Applied unconditionally rather than per style, because a generator cannot see the room. The
	 * outline of a full drop usually dies into the walls, and there the fascia is buried in the
	 * masonry and costs a few triangles. Judging per style where the edge happens to be visible is
	 * how this file came to describe a fascia in its own comment and build one only for Bulkhead.
	 *
	 * Hollow, never a plug: the plenum has to stay a plenum for the services that run in it, and a
	 * solid fill would also put a second downward face in the soffit plane, which flashes.
	 */
	auto AppendFascia = [&AppendBand](const TArray<FVector2D>& Loop, double BottomZ, double TopZ) -> bool
	{
		// The panel already meets the structure; there is no edge to close.
		if (TopZ - BottomZ <= Lap)
		{
			return true;
		}

		return AppendBand(Loop, PanelThickness, BottomZ, TopZ, EHFSurfaceRole::CeilingSoffit);
	};

	switch (Ceiling.Style)
	{
	case EHFCeilingStyle::FullDrop:
	case EHFCeilingStyle::Bulkhead:
	{
		// A flat panel across the whole outline, plus the fascia that closes its edge to the slab.
		//
		// Both styles now. The fascia used to be added for Bulkhead alone, so every full drop in
		// the flat was a 20 mm sheet with the plenum open behind it wherever its outline did not
		// happen to die into a wall.
		Checked(FHFMeshOps::AppendPrismWithHoles(Mesh, Outline, FanHoles, SoffitZ, SoffitZ + PanelThickness,
			EHFSurfaceRole::CeilingSoffit));

		// Standing on the panel's top face - see the note on Lap - and never at the soffit, which
		// would put a second horizontal face in the one plane the room certainly can see.
		Checked(AppendFascia(Outline, SoffitZ + PanelThickness, StructuralZ));
		break;
	}

	case EHFCeilingStyle::Peripheral:
	{
		// The band is its own fascia. It is a solid annulus from the soffit to the structure: the
		// inner face is the vertical edge you see standing under it, and the outer edge dies into
		// the wall. Nothing to add here - said out loud so the next style copies the right one.
		Checked(AppendBand(Outline, Ceiling.BandWidth, SoffitZ, StructuralZ, EHFSurfaceRole::CeilingSoffit));
		break;
	}

	case EHFCeilingStyle::Tray:
	{
		// Outer band at the full drop, inner region stepped back up to half of it.
		Checked(AppendBand(Outline, Ceiling.BandWidth, SoffitZ, StructuralZ, EHFSurfaceRole::CeilingSoffit));

		const double InnerSoffitZ = StructuralZ - Ceiling.Drop * 0.5;

		// The inner panel laps into the band instead of stopping in its face, so the two never share
		// a vertical plane. It needs no fascia of its own: the band IS the fascia for this step -
		// it runs from the lower soffit to the slab and the panel's edge ends inside it - and a
		// fascia here would stand a fin of its own proud of the band's inner face.
		const double InnerInset = (Ceiling.BandWidth > Lap) ? Ceiling.BandWidth - Lap : Ceiling.BandWidth;
		for (const TArray<FVector2D>& Loop : FHFMeshOps::InsetPolygon(Outline, InnerInset))
		{
			Checked(FHFMeshOps::AppendPrismWithHoles(Mesh, Loop, FanHoles, InnerSoffitZ,
				InnerSoffitZ + PanelThickness, EHFSurfaceRole::CeilingSoffit));
		}
		break;
	}

	case EHFCeilingStyle::Cove:
	{
		// A cove is a peripheral band with a trough at its inner edge. The strip lies in the trough,
		// the lip in front of it keeps the strip out of sight, and the light leaves UPWARD and
		// washes the slab.
		//
		// So the trough is OPEN TO THE SLAB AND CLOSED TO THE ROOM, and that is the whole
		// difference between a cove and a groove. The old profile had it the other way round: a
		// recess facing down into the room, with the strip in plain view of anyone who looked up
		// and no path for the light to reach the slab at all. FHFCoveProfile::LipHeight already
		// documented the lip as rising above the band soffit to shield the strip; nothing built it.
		//
		// Measured inward from the wall:
		//   0          .. SolidBand    band, soffit to slab, exactly as Peripheral
		//   SolidBand  .. +Channel     the trough: soffit board only, open to the slab above it
		//   ..         .. +LipWidth    the lip: the same board, plus an upstand standing LipHeight
		//                              above the soffit
		const double ChannelWidth = FMath::Max(Ceiling.Cove.ChannelWidth, 1.0);
		const double LipWidth = FMath::Max(Ceiling.Cove.Setback, 1.0);

		// The upstand has to clear the board it stands on or it shields nothing.
		const double LipRise = FMath::Max(Ceiling.Cove.LipHeight, PanelThickness + 1.0);
		const double SolidBand = FMath::Max(Ceiling.BandWidth - ChannelWidth - LipWidth, 1.0);
		const double BoardTopZ = SoffitZ + PanelThickness;

		// ONE board across the whole band. Band, trough floor and lip all show the room the same
		// plane, and one piece is what keeps it one face: a board per zone would butt them together
		// in the soffit, which is the plane a person in the room is looking straight at.
		Checked(AppendBand(Outline, Ceiling.BandWidth, SoffitZ, BoardTopZ, EHFSurfaceRole::CeilingSoffit));

		// The band above the board, solid to the slab, standing on the board rather than lapped
		// into it - see the note on Lap.
		Checked(AppendBand(Outline, SolidBand, BoardTopZ, StructuralZ, EHFSurfaceRole::CeilingSoffit));

		// The upstand. CoveInterior rather than CeilingSoffit: the faces that matter here are the
		// trough side the strip washes and the sliver the room sees above the soffit line, and both
		// belong to the cove detail rather than to the flat ceiling around it.
		for (const TArray<FVector2D>& LipLoop : FHFMeshOps::InsetPolygon(Outline, SolidBand + ChannelWidth))
		{
			Checked(AppendBand(LipLoop, LipWidth, BoardTopZ, SoffitZ + LipRise, EHFSurfaceRole::CoveInterior));
		}
		break;
	}

	default:
		break;
	}

	if (Refused > 0)
	{
		UE_LOG(LogHouseForge, Warning,
			TEXT("False ceiling '%s' is missing %d of its pieces: their outlines could not be triangulated."),
			*Ceiling.Id.ToString(), Refused);
	}

	FHFMeshOps::ApplyWorldScaleUVs(Mesh);
	return Mesh;
}

namespace
{
	/**
	 * Crossing test, on the polygon a ceiling is triangulated from.
	 *
	 * A ray cast along +X counting edge crossings. Handedness-agnostic, which matters because a room
	 * boundary and an explicit bulkhead polygon are not guaranteed to be wound the same way.
	 */
	bool PointInPolygon2D(const TArray<FVector2D>& Polygon, const FVector2D& Point)
	{
		bool bInside = false;
		int32 Previous = Polygon.Num() - 1;

		for (int32 Index = 0; Index < Polygon.Num(); ++Index)
		{
			const FVector2D& A = Polygon[Index];
			const FVector2D& B = Polygon[Previous];
			Previous = Index;

			if ((A.Y > Point.Y) != (B.Y > Point.Y) &&
				Point.X < (B.X - A.X) * (Point.Y - A.Y) / (B.Y - A.Y) + A.X)
			{
				bInside = !bInside;
			}
		}

		return bInside;
	}

	/** True when the point falls inside any loop of an inset result. */
	bool InsideAnyLoop(const TArray<TArray<FVector2D>>& Loops, const FVector2D& Point)
	{
		for (const TArray<FVector2D>& Loop : Loops)
		{
			if (PointInPolygon2D(Loop, Point))
			{
				return true;
			}
		}
		return false;
	}
}

double FHFGenerators::CeilingSoffitDropAt(const FHFFalseCeiling& Ceiling, const FHFRoom& Room,
	const FVector2D& Point)
{
	if (Ceiling.Style == EHFCeilingStyle::None || Ceiling.Drop <= 0.0)
	{
		return 0.0;
	}

	// The same outline GenerateCeiling triangulates: a bulkhead follows its own polygon and
	// everything else follows the room.
	const TArray<FVector2D>& Outline = (Ceiling.ExplicitPolygon.Num() >= 3)
		? Ceiling.ExplicitPolygon
		: Room.Boundary;

	if (Outline.Num() < 3 || !PointInPolygon2D(Outline, Point))
	{
		return 0.0;
	}

	// Mirrors GenerateCeiling's switch case for case. The two answer the same question about the
	// same geometry - what covers this spot - and a fan resolved against a different answer from the
	// one the panel was built with is a fan in the plasterboard.
	switch (Ceiling.Style)
	{
	case EHFCeilingStyle::FullDrop:
	case EHFCeilingStyle::Bulkhead:
		// A panel across the whole outline.
		return Ceiling.Drop;

	case EHFCeilingStyle::Peripheral:
	case EHFCeilingStyle::Cove:
	{
		// Band only; the centre is open to the slab, which is why the three fans in the reference
		// flat hang in clear air and why nothing caught this.
		//
		// A cove answers the same way. Its trough and its lip are cut out of the top of the band,
		// so the soffit under the whole band width is one plane at the full drop - there is no
		// longer a step in the underside for a fan to be resolved against.
		const TArray<TArray<FVector2D>> Inner = FHFMeshOps::InsetPolygon(Outline, Ceiling.BandWidth);
		if (Inner.IsEmpty())
		{
			// The band swallowed the room, so it built as a full drop. Same answer here.
			return Ceiling.Drop;
		}
		return InsideAnyLoop(Inner, Point) ? 0.0 : Ceiling.Drop;
	}

	case EHFCeilingStyle::Tray:
	{
		const TArray<TArray<FVector2D>> Inner = FHFMeshOps::InsetPolygon(Outline, Ceiling.BandWidth);
		if (Inner.IsEmpty())
		{
			return Ceiling.Drop;
		}

		// The inner region steps back up to half the drop, and it is still a panel.
		return InsideAnyLoop(Inner, Point) ? Ceiling.Drop * 0.5 : Ceiling.Drop;
	}

	default:
		return 0.0;
	}
}

FDynamicMesh3 FHFGenerators::GenerateBeam(const FHFBeam& Beam, const TArray<FHFStructuralCut>& Structure)
{
	FDynamicMesh3 Mesh;
	FHFMeshOps::InitialiseMesh(Mesh);

	const FWallFrame Frame = MakeWallFrame(Beam.Start, Beam.End);
	if (!Frame.bValid || Beam.Width <= 0.0 || Beam.Depth <= 0.0)
	{
		return Mesh;
	}

	// Beams hang down from the slab soffit, so they occupy ClearHeight..SoffitZ - and they frame into
	// the columns they land on and into any beam that runs through them. Left overlapping, two beam
	// soffits share a patch of plane in the very surface a room's ceiling is made of.
	AppendMemberAroundStructure(Mesh, Structure, Frame, Beam.Start, Beam.Width,
		Beam.ClearHeight(), Beam.SoffitZ, Beam.SurfaceRole, Beam.Id);

	FHFMeshOps::ApplyWorldScaleUVs(Mesh);
	return Mesh;
}

FDynamicMesh3 FHFGenerators::GenerateColumn(const FHFColumn& Column)
{
	FDynamicMesh3 Mesh;
	FHFMeshOps::InitialiseMesh(Mesh);

	if (Column.Size.X <= 0.0 || Column.Size.Y <= 0.0 || Column.Height <= 0.0)
	{
		return Mesh;
	}

	FHFMeshOps::AppendBox(Mesh,
		FVector3d(Column.Position.X, Column.Position.Y, Column.BaseZ + Column.Height * 0.5),
		FVector3d(Column.Size.X * 0.5, Column.Size.Y * 0.5, Column.Height * 0.5),
		Column.RotationDegrees, Column.SurfaceRole);

	FHFMeshOps::ApplyWorldScaleUVs(Mesh);
	return Mesh;
}

FDynamicMesh3 FHFGenerators::GenerateDoorLeaf(const FHFOpening& Opening, double SwingSign,
	const FHFDoorParams& InParams)
{
	FDynamicMesh3 Mesh;
	FHFMeshOps::InitialiseMesh(Mesh);

	if (Opening.Width <= 0.0 || Opening.Height <= 0.0)
	{
		return Mesh;
	}

	// Reconciled against this opening before anything is measured from them, because a gap wider
	// than the opening does not thin the leaf, it deletes it.
	const FHFDoorParams Params = InParams.Sanitised(Opening.Width, Opening.Height);

	// Leaf-local space: the pivot is the origin, the leaf runs along +X towards the far jamb, its
	// thickness sits on Y and its height on Z from the sill. Generating it here rather than in
	// place is what lets the actor swing it without the generator knowing anything about the
	// house - see .claude/rules/04-conventions.md.
	//
	// The leaf hangs on the face it swings towards rather than on the wall centreline. A leaf
	// centred in the reveal pivots its own back edge into the masonry beside the jamb - half the
	// leaf thickness at the limit, which a walkthrough camera on the hinge side sees as the door
	// vanishing into the wall. Hanging it on the swing face sweeps that edge out into the room,
	// which is where a real butt hinge puts it.
	const double LeafY = -FMath::Sign(SwingSign) * Params.LeafThickness * 0.5;

	// The leaf is hung IN A FRAME, not in the hole. The pivot sits at the frame's check, so the
	// leaf starts a running clearance out from it and finishes the same distance short of the check
	// on the far jamb; its top stops under the head's check and its bottom is undercut clear of the
	// floor, because a door frame has no bottom member for it to close onto.
	//
	// This is why a 900 door leaf is not 900 wide. It laps into the check on both jambs, so it is
	// wider than the daylight opening and narrower than the masonry one.
	const double Gap = Params.LeafFrameGap;
	const double Width = FMath::Max(Params.LeafWidth(Opening.Width), 0.0);
	const double Height = FMath::Max(Params.LeafHeight(Opening.Height), 0.0);

	FHFMeshOps::AppendBox(Mesh,
		FVector3d(Gap + Width * 0.5, LeafY, Params.LeafUndercut + Height * 0.5),
		FVector3d(Width * 0.5, Params.LeafThickness * 0.5, Height * 0.5),
		0.0, EHFSurfaceRole::DoorLeaf);

	FHFMeshOps::ApplyWorldScaleUVs(Mesh);
	return Mesh;
}

namespace
{
	/**
	 * One sash of a sliding unit, in the unit's local space.
	 *
	 * X is measured from the near jamb, Y is the track the sash rides in and Z runs up from the
	 * sill. Both sashes of a pair are generated where they sit rather than each about its own
	 * origin, so they share the unit's pivot and the only difference between them is that one of
	 * them moves.
	 *
	 * Built as a picture frame - two stiles full height with the rails let in between them - so no
	 * two members share a volume and the sash measures exactly its section times its perimeter. The
	 * pane then engages into the glazing groove of all four, which is where a real one sits and why
	 * it is a solid rather than a plane.
	 *
	 * ONE builder for a sliding window's sash and a sliding door's panel. They are the same object:
	 * the door's section is heavier and its bottom rail deeper, and that is the whole of the
	 * difference. A door path that built its own opaque slab instead is exactly the defect this
	 * replaces - both balcony doors in the reference flat were solid boards in a bare hole.
	 */
	FDynamicMesh3 MakeSlidingSash(double XMin, double XMax, double TrackY, double ZMin, double ZMax,
		bool bWithHandle, const FHFSlidingSashSection& Section)
	{
		FDynamicMesh3 Mesh;
		FHFMeshOps::InitialiseMesh(Mesh);

		const double Face = Section.FaceWidth;
		const double Rail = Section.BottomRailWidth;
		const double Width = XMax - XMin;
		const double Height = ZMax - ZMin;
		if (Width <= Face * 2.0 || Height <= Face + Rail)
		{
			return Mesh;
		}

		auto AppendMember = [&Mesh, TrackY, &Section](double MemberXMin, double MemberXMax,
			double MemberZMin, double MemberZMax)
		{
			FHFMeshOps::AppendBox(Mesh,
				FVector3d((MemberXMin + MemberXMax) * 0.5, TrackY, (MemberZMin + MemberZMax) * 0.5),
				FVector3d((MemberXMax - MemberXMin) * 0.5, Section.SashDepth * 0.5,
					(MemberZMax - MemberZMin) * 0.5),
				0.0, EHFSurfaceRole::WindowFrame);
		};

		AppendMember(XMin, XMin + Face, ZMin, ZMax);            // stile
		AppendMember(XMax - Face, XMax, ZMin, ZMax);            // stile

		// The bottom rail is deeper than the track upstand it comes to rest over, and the two
		// interpenetrate. A real bottom rail is hollow and the upstand runs up inside it, so that is
		// the section rather than a clash - and it is hidden inside the rail either way.
		AppendMember(XMin + Face, XMax - Face, ZMin, ZMin + Rail);
		AppendMember(XMin + Face, XMax - Face, ZMax - Face, ZMax);

		// The pane, engaged into the glazing groove of all four members. Stated corner by corner
		// rather than as a centred box, because a door's bottom rail is deeper than its top one and
		// a pane centred between them would ride up out of the top rail's groove.
		const double GlassMinX = XMin + Face - Section.GlassRebate;
		const double GlassMaxX = XMax - Face + Section.GlassRebate;
		const double GlassMinZ = ZMin + Rail - Section.GlassRebate;
		const double GlassMaxZ = ZMax - Face + Section.GlassRebate;

		if (GlassMaxX > GlassMinX && GlassMaxZ > GlassMinZ)
		{
			FHFMeshOps::AppendBox(Mesh,
				FVector3d((GlassMinX + GlassMaxX) * 0.5, TrackY, (GlassMinZ + GlassMaxZ) * 0.5),
				FVector3d((GlassMaxX - GlassMinX) * 0.5, Section.GlassThickness * 0.5,
					(GlassMaxZ - GlassMinZ) * 0.5),
				0.0, EHFSurfaceRole::Glass);
		}

		if (bWithHandle)
		{
			// On the meeting stile, projecting away from the other sash's track - which is the face
			// a hand can reach and the side the catch is fitted on. A window's catch sits at mid
			// height because a window is small; a door's pull is at hand height whatever the door
			// is, so where it goes is the section's to say. Kept inside the sash either way.
			const double Side = TrackY >= 0.0 ? 1.0 : -1.0;
			const double Half = Section.HandleHeight * 0.5;
			const double Lowest = ZMin + Rail + Half;
			const double Highest = FMath::Max(ZMax - Face - Half, Lowest);
			const double HandleZ = Section.HandleAboveSill > 0.0
				? FMath::Clamp(ZMin + Section.HandleAboveSill, Lowest, Highest)
				: (ZMin + ZMax) * 0.5;

			FHFMeshOps::AppendBox(Mesh,
				FVector3d(XMax - Face * 0.5,
					TrackY + Side * (Section.SashDepth + Section.HandleProjection) * 0.5,
					HandleZ),
				FVector3d(Section.HandleWidth * 0.5, Section.HandleProjection * 0.5, Half),
				0.0, EHFSurfaceRole::MetalHardware);
		}

		FHFMeshOps::ApplyWorldScaleUVs(Mesh);
		return Mesh;
	}

	/**
	 * A top-hung ventilator sash, in the part's own local space.
	 *
	 * The origin is on the hinge line at the near jamb: +X runs along the hinge, the sash hangs down
	 * in -Z and its thickness lies on Y.
	 *
	 * The body hangs on the side AWAY from the direction it opens, for the same reason a door leaf
	 * hangs on its swing face rather than on the wall centreline. Rotating about the hinge, the
	 * corner nearest the axis sweeps a quarter circle of the sash's own thickness; put the body on
	 * the opening side and that corner sweeps UP into the head masonry, and the sash disappears into
	 * the lintel as it opens. Hung this way it sweeps down into the opening instead, and no point of
	 * the sash ever rises above the hinge line.
	 *
	 * @param OpenSign  +1 to open along the wall normal, -1 to open against it.
	 */
	FDynamicMesh3 MakeVentilatorSash(double Width, double Height, double OpenSign,
		const FHFVentilatorParams& Params)
	{
		FDynamicMesh3 Mesh;
		FHFMeshOps::InitialiseMesh(Mesh);

		const double F = Params.SashFaceWidth;
		if (Width <= F * 2.0 || Height <= F * 2.0)
		{
			return Mesh;
		}

		const double Side = OpenSign >= 0.0 ? 1.0 : -1.0;
		const double SashY = -Side * Params.SashThickness * 0.5;

		auto AppendMember = [&Mesh, SashY, &Params](double MemberXMin, double MemberXMax,
			double MemberZMin, double MemberZMax)
		{
			FHFMeshOps::AppendBox(Mesh,
				FVector3d((MemberXMin + MemberXMax) * 0.5, SashY, (MemberZMin + MemberZMax) * 0.5),
				FVector3d((MemberXMax - MemberXMin) * 0.5, Params.SashThickness * 0.5,
					(MemberZMax - MemberZMin) * 0.5),
				0.0, EHFSurfaceRole::WindowFrame);
		};

		AppendMember(0.0, F, -Height, 0.0);              // stile
		AppendMember(Width - F, Width, -Height, 0.0);    // stile
		AppendMember(F, Width - F, -Height, -Height + F);// bottom rail
		AppendMember(F, Width - F, -F, 0.0);             // top rail

		const double GlassInset = F - Params.GlassRebate;
		FHFMeshOps::AppendBox(Mesh,
			FVector3d(Width * 0.5, SashY, -Height * 0.5),
			FVector3d(Width * 0.5 - GlassInset, Params.GlassThickness * 0.5, Height * 0.5 - GlassInset),
			0.0, EHFSurfaceRole::Glass);

		// The pull, on the face somebody stands in front of - which is the face it opens towards.
		FHFMeshOps::AppendBox(Mesh,
			FVector3d(Width * 0.5, Side * Params.PullProjection * 0.5, -Height + F * 0.5),
			FVector3d(Params.PullWidth * 0.5, Params.PullProjection * 0.5, Params.PullHeight * 0.5),
			0.0, EHFSurfaceRole::MetalHardware);

		FHFMeshOps::ApplyWorldScaleUVs(Mesh);
		return Mesh;
	}

	/**
	 * Two leaves on two tracks: one fixed, one running.
	 *
	 * A single leaf the width of the opening has nowhere to go - sliding it its own width buries it
	 * in the masonry beside the jamb - so each leaf takes half the clear opening, laps past the
	 * meeting line, and the running one travels until its far edge meets its partner's. That keeps
	 * every leaf wholly inside the reveal at every open amount, and it is also what a sliding unit
	 * IS. Both balcony doors in the reference flat were rebuilt around this after one of them slid
	 * bodily through the window next to it.
	 *
	 * One function for the sliding window and the sliding door, with the set-out itself in
	 * FHFSlidingSetOut where a sliding wardrobe shutter reaches it too. Two implementations of this
	 * rule is how the two drifted apart in the first place.
	 *
	 * @param NearX  Where the clear opening starts, measured from the unit's pivot.
	 */
	void BuildSlidingPair(const FTransform& UnitPivot, TArray<FHFMeshPart>& OutParts,
		double NearX, double ClearWidth, double ZMin, double ZMax,
		double TrackPitch, double InterlockOverlap, const FHFSlidingSashSection& Section,
		const FName& FixedPartId, const FName& RunningPartId)
	{
		const FHFSlidingSetOut Running =
			FHFSlidingSetOut::Leaf(ClearWidth * 0.5, InterlockOverlap * 0.5, /*EndGap*/ 0.0);
		const FHFSlidingSetOut Standing = Running.MirroredIn(ClearWidth);

		const double TrackY = TrackPitch * 0.5;

		FHFMeshPart Fixed;
		Fixed.PartId = FixedPartId;
		Fixed.Mesh = MakeSlidingSash(NearX + Standing.NearEdge, NearX + Standing.FarEdge, -TrackY,
			ZMin, ZMax, /*bWithHandle*/ false, Section);
		Fixed.PivotTransform = UnitPivot;
		OutParts.Add(MoveTemp(Fixed));

		FHFMeshPart Sash;
		Sash.PartId = RunningPartId;
		Sash.Mesh = MakeSlidingSash(NearX + Running.NearEdge, NearX + Running.FarEdge, TrackY,
			ZMin, ZMax, /*bWithHandle*/ true, Section);
		Sash.PivotTransform = UnitPivot;
		Sash.Motion.Type = EHFMotionType::Slide;
		Sash.Motion.Axis = FVector::XAxisVector;

		// Far edge to far edge: the running leaf comes to rest exactly over its fixed partner, so it
		// is still wholly inside the reveal at full travel.
		Sash.Motion.MaxTravelCm = FMath::Max(0.0, Running.Travel);
		OutParts.Add(MoveTemp(Sash));
	}

	/** The two sashes of a sliding window, set out between its outer frame's members. */
	void BuildSlidingWindowSashes(const FHFOpening& Opening, const FTransform& UnitPivot,
		TArray<FHFMeshPart>& OutParts, const FHFSlidingWindowParams& Params)
	{
		BuildSlidingPair(UnitPivot, OutParts,
			/*NearX*/ Params.FrameFace, Params.ClearWidth(Opening.Width),
			/*ZMin*/ Params.FrameFace, /*ZMax*/ Opening.Height - Params.FrameFace,
			Params.TrackPitch, Params.InterlockOverlap, Params.SashSection(),
			TEXT("SashFixed"), TEXT("Sash"));
	}

	/**
	 * The two panels of a sliding door.
	 *
	 * Identical to the window's pair but standing on a threshold rather than on a sill member: a
	 * balcony door runs to the floor, and what it runs on is the one member a hinged door frame
	 * does not have.
	 */
	void BuildSlidingDoorPanels(const FHFOpening& Opening, const FTransform& UnitPivot,
		TArray<FHFMeshPart>& OutParts, const FHFSlidingDoorParams& Params)
	{
		BuildSlidingPair(UnitPivot, OutParts,
			/*NearX*/ Params.FrameFace, Params.ClearWidth(Opening.Width),
			/*ZMin*/ Params.ThresholdHeight, /*ZMax*/ Opening.Height - Params.FrameFace,
			Params.TrackPitch, Params.InterlockOverlap, Params.SashSection(),
			TEXT("PanelFixed"), TEXT("Leaf"));
	}

	/** One top-hung sash, hinged along the head of the clear opening. */
	void BuildVentilatorSash(const FHFOpening& Opening, const FTransform& UnitPivot,
		TArray<FHFMeshPart>& OutParts, const FHFVentilatorParams& Params)
	{
		const double ClearWidth = Opening.Width - Params.FrameFace * 2.0;
		const double ClearHeight = Opening.Height - Params.FrameFace * 2.0;

		// Which way it opens. A ventilator carries no swing arc on a plan, so the wall normal is the
		// default and an explicitly outward swing turns it around.
		const double OpenSign =
			(Opening.Swing == EHFSwing::OutwardLeft || Opening.Swing == EHFSwing::OutwardRight) ? -1.0 : 1.0;

		FHFMeshPart Sash;
		Sash.PartId = TEXT("Sash");
		Sash.Mesh = MakeVentilatorSash(ClearWidth, ClearHeight, OpenSign, Params);

		// The hinge line: along the head of the clear opening, starting at the near jamb.
		Sash.PivotTransform =
			FTransform(FVector(Params.FrameFace, 0.0, Opening.Height - Params.FrameFace)) * UnitPivot;

		Sash.Motion.Type = EHFMotionType::Hinge;
		Sash.Motion.Axis = FVector::XAxisVector;
		Sash.Motion.MaxAngleDegrees = Params.OpenAngleDegrees * OpenSign;
		OutParts.Add(MoveTemp(Sash));
	}
}

void FHFGenerators::BuildOpeningParts(const FHFOpening& Opening, const FHFWall& Wall,
	TArray<FHFMeshPart>& OutParts, const FHFOpeningBuildParams& InParams)
{
	const FWallFrame Frame = MakeWallFrame(Wall.Start, Wall.End);
	if (!Frame.bValid || Opening.Width <= 0.0 || Opening.Height <= 0.0)
	{
		return;
	}

	// Sanitised against the same opening as GenerateOpeningFixedInfill, so the two agree about where
	// the frame ends and whether this unit has sashes at all. They ask HasSashes separately, and a
	// disagreement would leave a framed hole with no glass in it.
	const FHFOpeningBuildParams Params = InParams.Sanitised(Opening.Width, Opening.Height);

	const bool bIsDoor = Opening.Kind == EHFOpeningKind::Door || Opening.Kind == EHFOpeningKind::SlidingDoor;
	const bool bHasSash =
		Opening.Kind == EHFOpeningKind::SlidingWindow || Opening.Kind == EHFOpeningKind::Ventilator;

	// A fixed window and an archway have nothing that moves, and saying so is the whole answer -
	// the rule is that anything which moves must be able to move, not that everything must move.
	if (!bIsDoor && !bHasSash)
	{
		return;
	}

	const FVector2D Centre = Wall.Start + Frame.Direction * Opening.OffsetAlongWall;
	const double SillZ = Wall.BaseZ + Opening.SillHeight;
	const double HalfWidth = Opening.Width * 0.5;

	if (bHasSash)
	{
		// Sashes all measure from the near jamb along the wall, so they share one frame: local +X
		// runs to the far jamb, +Y is the wall normal and +Z runs up from the sill.
		const FVector2D NearJamb = Centre - Frame.Direction * HalfWidth;
		const FTransform UnitPivot(FRotator(0.0, Frame.YawDegrees, 0.0),
			FVector(NearJamb.X, NearJamb.Y, SillZ));

		if (Opening.Kind == EHFOpeningKind::SlidingWindow)
		{
			if (Params.SlidingWindow.HasSashes(Opening.Width, Opening.Height))
			{
				BuildSlidingWindowSashes(Opening, UnitPivot, OutParts, Params.SlidingWindow);
			}
			else
			{
				// Too small to divide, so it is honestly fixed glazing rather than a unit with sashes
				// too narrow to be anything. GenerateOpeningFixedInfill asks the same question and
				// glazes it, so the pair cannot disagree and leave a framed hole.
				UE_LOG(LogHouseForge, Warning,
					TEXT("Sliding window '%s' is %.0f x %.0f cm, too small to divide into two sashes; it is built as fixed glazing."),
					*Opening.Id.ToString(), Opening.Width, Opening.Height);
			}
			return;
		}

		if (Params.Ventilator.HasSash(Opening.Width, Opening.Height))
		{
			BuildVentilatorSash(Opening, UnitPivot, OutParts, Params.Ventilator);
		}
		else
		{
			UE_LOG(LogHouseForge, Warning,
				TEXT("Ventilator '%s' is %.0f x %.0f cm, too small to carry an opening sash; it is built as fixed glazing."),
				*Opening.Id.ToString(), Opening.Width, Opening.Height);
		}
		return;
	}

	if (Opening.Kind == EHFOpeningKind::SlidingDoor)
	{
		// A sliding unit is two glazed panels on two tracks, not one slab that slides into the wall.
		//
		// One leaf the full width of the opening has nowhere to go: sliding it its own width buries
		// it in the masonry beside the jamb, or drives it through the next window along, which is
		// what the 1800 balcony units in the reference flat did. Half the opening each, and the
		// moving panel travelling until its far edge meets the fixed panel's, keeps every panel
		// inside the reveal at every open amount - and is what a sliding unit actually is.
		//
		// The panels themselves are the sliding WINDOW's sash, built by the same function from a
		// heavier section: a balcony door in one of these flats is a full-height sliding window
		// with a threshold under it, and it is glazed.
		const FHFSlidingDoorParams& Door = Params.SlidingDoor;

		const FVector2D NearJamb = Centre - Frame.Direction * HalfWidth;
		const FTransform UnitPivot(FRotator(0.0, Frame.YawDegrees, 0.0),
			FVector(NearJamb.X, NearJamb.Y, SillZ));

		if (Door.HasSashes(Opening.Width, Opening.Height))
		{
			BuildSlidingDoorPanels(Opening, UnitPivot, OutParts, Door);
		}
		else
		{
			// Too small to divide into two panels, so it is honestly fixed glazing rather than a
			// unit whose panels are too narrow to be anything. GenerateOpeningFixedInfill asks the
			// same question and glazes it, so the pair cannot disagree and leave a framed hole.
			UE_LOG(LogHouseForge, Warning,
				TEXT("Sliding door '%s' is %.0f x %.0f cm, too small to divide into two panels; it is built as fixed glazing."),
				*Opening.Id.ToString(), Opening.Width, Opening.Height);
		}
		return;
	}

	// Which jamb the leaf hangs on, matching the swing arc the plan preview draws. A door hung on
	// the wrong jamb is invisible in elevation and wrong in every walkthrough.
	const bool bHingeAtNear =
		Opening.Swing == EHFSwing::InwardLeft ||
		Opening.Swing == EHFSwing::OutwardLeft ||
		Opening.Swing == EHFSwing::None;

	// Local +Y is up-cross-leaf-direction, which is the wall normal when the leaf hangs on the
	// near jamb and its opposite when it hangs on the far one. Inward swings follow the wall
	// normal, outward swings oppose it, so the sign flips with both choices.
	const double InwardSign =
		(Opening.Swing == EHFSwing::OutwardLeft || Opening.Swing == EHFSwing::OutwardRight) ? -1.0 : 1.0;
	const double HingeSign = bHingeAtNear ? InwardSign : -InwardSign;

	// The hinge line is on the FRAME, not on the masonry. Two moves, and both of them matter:
	//
	//  - along the wall, in from the jamb by the frame's sight line less the lap into its check, so
	//    the leaf pivots about the edge it actually hangs from;
	//  - across the wall, out to the frame's room-side face, so a leaf opened to ninety degrees lies
	//    against the reveal instead of driving its back edge through the jamb beside it.
	//
	// The arc a plan preview draws, and the arc HFSpecValidator sweeps for obstructions, are both
	// struck from the MASONRY jamb with the full opening width as their radius. Moving the hinge in
	// and shortening the leaf keeps the whole swept quadrant strictly inside that arc, so the two
	// still agree - the drawn arc is now conservative rather than exact, which is the safe direction.
	const double FrameInset = Params.Door.LeafInset();
	const double FrameFaceAcross = InwardSign * (Wall.Thickness * 0.5 + Params.Door.FrameProud);

	const FVector2D HingePlan = (bHingeAtNear
		? Centre - Frame.Direction * (HalfWidth - FrameInset)
		: Centre + Frame.Direction * (HalfWidth - FrameInset))
		+ Frame.Normal * FrameFaceAcross;

	// Local +X points from the pivot towards the other jamb, so the leaf mesh is the same whichever
	// jamb it hangs on and only the pivot rotation differs.
	const FVector2D LeafDirection = bHingeAtNear ? Frame.Direction : -Frame.Direction;
	const double LeafYaw = FMath::RadiansToDegrees(FMath::Atan2(LeafDirection.Y, LeafDirection.X));

	const FTransform Pivot(FRotator(0.0, LeafYaw, 0.0), FVector(HingePlan.X, HingePlan.Y, SillZ));

	FHFMeshPart Part;
	Part.PartId = TEXT("Leaf");
	Part.Mesh = GenerateDoorLeaf(Opening, HingeSign, Params.Door);
	Part.PivotTransform = Pivot;
	Part.Motion.Type = EHFMotionType::Hinge;
	Part.Motion.Axis = FVector::ZAxisVector;
	Part.Motion.MaxAngleDegrees = 90.0 * HingeSign;

	OutParts.Add(MoveTemp(Part));
}

FDynamicMesh3 FHFGenerators::GenerateOpeningInfill(const FHFOpening& Opening, const FHFWall& Wall,
	const FHFOpeningBuildParams& Params)
{
	FDynamicMesh3 Mesh = GenerateOpeningFixedInfill(Opening, Wall, Params);

	// Every moving part in its closed pose. Composing the snapshot from the same parts the actor
	// hangs is what guarantees a closed door looks identical to the one-piece infill it replaced.
	TArray<FHFMeshPart> Parts;
	BuildOpeningParts(Opening, Wall, Parts, Params);

	for (const FHFMeshPart& Part : Parts)
	{
		FDynamicMesh3 Posed = Part.Mesh;
		MeshTransforms::ApplyTransform(Posed, FTransformSRT3d(Part.PivotTransform), /*bReverseOrientationIfNeeded*/ true);
		FHFMeshOps::AppendPreservingRoles(Mesh, Posed);
	}

	return Mesh;
}

FDynamicMesh3 FHFGenerators::GenerateOpeningFixedInfill(const FHFOpening& Opening, const FHFWall& Wall,
	const FHFOpeningBuildParams& InParams)
{
	FDynamicMesh3 Mesh;
	FHFMeshOps::InitialiseMesh(Mesh);

	const FWallFrame Frame = MakeWallFrame(Wall.Start, Wall.End);
	if (!Frame.bValid || Opening.Width <= 0.0 || Opening.Height <= 0.0)
	{
		return Mesh;
	}

	// The same reconciliation BuildOpeningParts does, on the same opening, so the pair cannot
	// disagree about the frame face or about whether the sashes carry the glass.
	const FHFOpeningBuildParams Params = InParams.Sanitised(Opening.Width, Opening.Height);

	// An archway is a hole and nothing else, and that is a decision rather than an omission.
	//
	// A cased opening between a living room and a dining space in one of these flats carries NO
	// frame: the reveal is plastered, its arrises are rounded, and it is painted with the walls.
	// A chowkhat is there to hang a leaf on, and an archway has no leaf. Framing one would put a
	// timber section around a hole that in the reference drawing is a continuation of the wall.
	if (Opening.Kind == EHFOpeningKind::Archway)
	{
		return Mesh;
	}

	const FVector2D Plan = Wall.Start + Frame.Direction * Opening.OffsetAlongWall;
	const double SillZ = Wall.BaseZ + Opening.SillHeight;
	const double CentreZ = SillZ + Opening.Height * 0.5;
	const double HalfOpeningWidth = Opening.Width * 0.5;

	// One member appender for every kind of opening: offsets are along the wall, across it, and up
	// from the opening's centre, with the wall's own yaw applied. Everything below is a box placed
	// with this, so a frame member, a track and a pane are all positioned by the same arithmetic.
	auto AppendOpeningMember = [&](double OffsetAlong, double OffsetAcross, double OffsetUp,
		double HalfAlong, double HalfAcross, double HalfUp, EHFSurfaceRole Role)
	{
		if (HalfAlong <= 0.0 || HalfAcross <= 0.0 || HalfUp <= 0.0)
		{
			return;
		}

		const FVector2D MemberPlan = Plan + Frame.Direction * OffsetAlong + Frame.Normal * OffsetAcross;
		FHFMeshOps::AppendBox(Mesh,
			FVector3d(MemberPlan.X, MemberPlan.Y, CentreZ + OffsetUp),
			FVector3d(HalfAlong, HalfAcross, HalfUp),
			Frame.YawDegrees, Role);
	};

	if (Opening.Kind == EHFOpeningKind::Door)
	{
		// The chowkhat: head and two jambs, no bottom member, with a check cut in it for the leaf.
		//
		// There was nothing here at all until the reference flat was walked - every door in it was a
		// bare leaf hanging in a bare hole, with the masonry reveal for a lining. It is the kind of
		// absence a plan view and a wireframe both agree looks fine.
		const FHFDoorParams& Door = Params.Door;

		// Which face the frame is set at: the one the leaf swings towards, so the leaf shuts flush
		// with the frame and opens away from its stop.
		const double InwardSign =
			(Opening.Swing == EHFSwing::OutwardLeft || Opening.Swing == EHFSwing::OutwardRight) ? -1.0 : 1.0;

		// A frame section is NOT stretched to the wall thickness. It is set at the room-side face,
		// standing a few millimetres proud of the plaster, and whatever reveal is left behind it in a
		// 230 wall stays plastered masonry. That is why one section serves both walls in this flat -
		// and it is clamped so it can never come out through the far face of a thin one.
		const double Depth = FMath::Min(Door.FrameDepth, Wall.Thickness + Door.FrameProud);
		const double Rebate = FMath::Min(Door.RebateDepth(), Depth - MinMemberSize);

		if (Depth > MinMemberSize && Rebate > 0.0)
		{
			const double Face = Door.FrameFace;
			const double Stop = Door.RebateStop;
			const double Embed = Door.FrameEmbed;
			const double Height = Opening.Height;

			// A band across the wall, measured in from the frame's room-side face, placed as an
			// offset from the wall centreline. Z is measured up from the sill.
			auto AppendFramePiece = [&](double AlongA, double AlongB, double NearN, double FarN,
				double ZFromSill, double ZToSill)
			{
				AppendOpeningMember(
					(AlongA + AlongB) * 0.5,
					InwardSign * (Wall.Thickness * 0.5 + Door.FrameProud - (NearN + FarN) * 0.5),
					(ZFromSill + ZToSill) * 0.5 - Opening.Height * 0.5,
					FMath::Abs(AlongB - AlongA) * 0.5,
					(FarN - NearN) * 0.5,
					(ZToSill - ZFromSill) * 0.5,
					// The frame is the door's timber, not a window's aluminium: a chowkhat is
					// painted or polished with the leaf it carries, so the material panel reaches
					// the two together.
					EHFSurfaceRole::DoorLeaf);
			};

			for (const double Side : { -1.0, 1.0 })
			{
				// The jamb runs floor to lintel, buried at both ends in the construction rather than
				// stopping in the plane of it. Two surfaces in one plane are the flicker this whole
				// pass exists to remove, and the foot of a jamb sitting exactly on the floor finish
				// is the easiest one in the flat to produce.
				AppendFramePiece(Side * (HalfOpeningWidth + Embed), Side * (HalfOpeningWidth - Face),
					Rebate, Depth, -Embed, Height + Embed);

				// In front of the check, stopping short of the daylight edge by the stop width - the
				// void left between the two is the rebate the leaf shuts into.
				AppendFramePiece(Side * (HalfOpeningWidth + Embed), Side * (HalfOpeningWidth - Face + Stop),
					0.0, Rebate, -Embed, Height - Face + Stop);
			}

			// The head, let in between the jambs behind the check and running over them in front of
			// it, so the front face of the frame is continuous around all three sides.
			AppendFramePiece(-(HalfOpeningWidth - Face), HalfOpeningWidth - Face,
				Rebate, Depth, Height - Face, Height + Embed);
			AppendFramePiece(-(HalfOpeningWidth + Embed), HalfOpeningWidth + Embed,
				0.0, Rebate, Height - Face + Stop, Height + Embed);
		}

		FHFMeshOps::ApplyWorldScaleUVs(Mesh);
		return Mesh;
	}

	if (Opening.Kind == EHFOpeningKind::SlidingDoor)
	{
		// The outer frame of a glazed sliding door: head, two jambs, and the one member a hinged
		// door frame does not have - a threshold with the tracks standing on it, which is what you
		// step over walking onto the balcony.
		const FHFSlidingDoorParams& Slider = Params.SlidingDoor;

		const double Face = Slider.FrameFace;
		const double Threshold = Slider.ThresholdHeight;
		const double Height = Opening.Height;
		const double HalfDepth = Slider.FrameDepth * 0.5;

		// Z offsets are from the opening's centre, which is where AppendOpeningMember measures from.
		const double ToCentre = -Opening.Height * 0.5;

		// Threshold and head across the full width; the jambs between them, so no two members of the
		// frame share a volume.
		AppendOpeningMember(0.0, 0.0, ToCentre + Threshold * 0.5,
			HalfOpeningWidth, HalfDepth, Threshold * 0.5, EHFSurfaceRole::WindowFrame);
		AppendOpeningMember(0.0, 0.0, ToCentre + Height - Face * 0.5,
			HalfOpeningWidth, HalfDepth, Face * 0.5, EHFSurfaceRole::WindowFrame);

		for (const double Side : { -1.0, 1.0 })
		{
			AppendOpeningMember(Side * (HalfOpeningWidth - Face * 0.5), 0.0,
				ToCentre + (Threshold + Height - Face) * 0.5,
				Face * 0.5, HalfDepth, (Height - Face - Threshold) * 0.5, EHFSurfaceRole::WindowFrame);
		}

		const bool bPanelsCarryTheGlass = Slider.HasSashes(Opening.Width, Opening.Height);

		if (bPanelsCarryTheGlass)
		{
			// The two tracks, standing on the threshold. They are fixed - only the panel on them
			// moves - and they run up inside the hollow bottom rail of the panel above them.
			for (const double Side : { -1.0, 1.0 })
			{
				AppendOpeningMember(0.0, Side * Slider.TrackPitch * 0.5,
					ToCentre + Threshold + Slider.TrackUpstand * 0.5,
					HalfOpeningWidth - Face, Slider.TrackWidth * 0.5, Slider.TrackUpstand * 0.5,
					EHFSurfaceRole::MetalHardware);
			}
		}
		else
		{
			// Too small to divide into two panels, so it is glazed fixed - and glazed HERE, because
			// BuildOpeningParts declined to build panels and something has to fill the frame.
			AppendOpeningMember(0.0, 0.0, ToCentre + (Threshold + Height - Face) * 0.5,
				HalfOpeningWidth - Face, Slider.GlassThickness * 0.5,
				(Height - Face - Threshold) * 0.5, EHFSurfaceRole::Glass);
		}

		FHFMeshOps::ApplyWorldScaleUVs(Mesh);
		return Mesh;
	}

	{
		// Window: a frame around the reveal. What goes inside it depends on whether the opening has
		// sashes of its own - a sliding window's glazing rides in them, so putting glazing here too
		// would double it, and a closed sash in front of a fixed pane is invisible.
		const bool bSliding = Opening.Kind == EHFOpeningKind::SlidingWindow;
		const bool bVentilator = Opening.Kind == EHFOpeningKind::Ventilator;

		const bool bSashesCarryTheGlass =
			(bSliding && Params.SlidingWindow.HasSashes(Opening.Width, Opening.Height)) ||
			(bVentilator && Params.Ventilator.HasSash(Opening.Width, Opening.Height));

		const double FaceWidth = bSliding
			? Params.SlidingWindow.FrameFace
			: (bVentilator ? Params.Ventilator.FrameFace : Params.FixedWindow.FrameFace);
		const double FrameDepth = bSliding
			? Params.SlidingWindow.FrameDepth
			: (bVentilator ? Params.Ventilator.FrameDepth : Params.FixedWindow.FrameDepth);

		const double HalfWidth = HalfOpeningWidth;
		const double HalfHeight = Opening.Height * 0.5;

		auto AppendFrameMember = [&](double OffsetAlong, double OffsetUp, double HalfAlong, double HalfUp)
		{
			AppendOpeningMember(OffsetAlong, 0.0, OffsetUp, HalfAlong, FrameDepth * 0.5, HalfUp,
				EHFSurfaceRole::WindowFrame);
		};

		AppendFrameMember(0.0, HalfHeight - FaceWidth * 0.5, HalfWidth, FaceWidth * 0.5);   // head
		AppendFrameMember(0.0, -HalfHeight + FaceWidth * 0.5, HalfWidth, FaceWidth * 0.5);  // sill
		AppendFrameMember(-HalfWidth + FaceWidth * 0.5, 0.0, FaceWidth * 0.5, HalfHeight);  // jamb
		AppendFrameMember(HalfWidth - FaceWidth * 0.5, 0.0, FaceWidth * 0.5, HalfHeight);   // jamb

		if (bSliding && bSashesCarryTheGlass)
		{
			// The two tracks, standing proud of the sill member. They are what makes a sliding
			// window read as one at rest, and they are fixed - only the sash on them moves.
			const FHFSlidingWindowParams& Sliding = Params.SlidingWindow;

			for (const double Side : { -1.0, 1.0 })
			{
				AppendOpeningMember(0.0, Side * Sliding.TrackPitch * 0.5,
					-HalfHeight + FaceWidth + Sliding.TrackUpstand * 0.5,
					HalfWidth - FaceWidth, Sliding.TrackWidth * 0.5, Sliding.TrackUpstand * 0.5,
					EHFSurfaceRole::MetalHardware);
			}
		}

		if (!bSashesCarryTheGlass)
		{
			// A central mullion, once the opening is wide enough to need one. A sliding unit never
			// gets one: its meeting stiles ARE the mullion, and a second one down the middle would
			// sit in the running sash's way.
			if (Opening.Width > Params.FixedWindow.MullionAboveWidth && !bSliding)
			{
				AppendFrameMember(0.0, 0.0, FaceWidth * 0.5, HalfHeight - FaceWidth);
			}

			// The pane. Deliberately the fixed window's thickness even when a sliding or ventilator
			// opening reaches here, which it only does when it was too small for sashes: what is being
			// built at that point IS a fixed window, and glazing it as one is what the code did before
			// any of this was overridable. Changing it to each section's own pane would be a real
			// change of output, and this refactor is meant to be a no-op.
			AppendOpeningMember(0.0, 0.0, 0.0, HalfWidth - FaceWidth,
				Params.FixedWindow.GlassThickness * 0.5, HalfHeight - FaceWidth, EHFSurfaceRole::Glass);
		}
	}

	FHFMeshOps::ApplyWorldScaleUVs(Mesh);
	return Mesh;
}
