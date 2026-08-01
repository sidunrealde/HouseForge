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
	const FHFSkirtingPlan& Skirting)
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

	// THE HEIGHT COMES FROM THE ROOM, the runs from the plan. A drawing states a skirting height and
	// the details panel edits it; where the skirting runs is composed from the walls round it. Copying
	// the height onto the plan would make typing a new one do nothing until the house was rebuilt.
	if (Room.SkirtingHeight > 0.0 && Skirting.Depth > 0.0)
	{
		/**
		 * How far the skirting is buried in the masonry behind it.
		 *
		 * A skirting stops dead in the wall face, and two faces in one plane is what the whole
		 * flashing report was about. Opposed faces are the safe case - the back of this one is
		 * sealed against the front of that one - but there is no reason to rely on it here when the
		 * fix is free. This is the same 3-5 mm interpenetration the door frames use to sit in a
		 * reveal rather than on it.
		 */
		constexpr double Embed = 0.5;

		for (const FHFSkirtingEdge& Edge : Skirting.Edges)
		{
			const FWallFrame Frame = MakeWallFrame(Edge.Start, Edge.End);
			if (!Frame.bValid)
			{
				continue;
			}

			for (const FHFSkirtingRun& Run : Edge.Runs)
			{
				const double RunLength = Run.Length();
				if (RunLength <= MinMemberSize)
				{
					continue;
				}

				const FVector2D RunCentre = Edge.Start + Frame.Direction * ((Run.Start + Run.End) * 0.5);

				// AGAINST THE PLASTER, WHICH IS NOT WHERE THE BOUNDARY IS.
				//
				// A room boundary is the wall CENTRELINE, so the finished face of the wall stands
				// half a wall's thickness inside it - 11.5 cm on a 230, 5.75 on a 115. This offset
				// was once Depth * 0.5 and nothing else, which put a 100 mm skirting between 0 and
				// 18 mm of the centreline: entirely inside the masonry, in every room, on every edge.
				// Seven rooms in the reference flat declare a skirting and not one of them had a
				// skirting you could see.
				//
				// Per EDGE because the walls round a room are not all the same thickness, and
				// resolved by FHFSkirting rather than here because a generator cannot see the walls.
				const FVector2D Offset = Frame.Normal * (Edge.FaceInset - Embed + Skirting.Depth * 0.5);

				// The run reaches the CENTRELINE corner at each end of its edge, so at a corner the
				// two runs overlap through the masonry and their union fills it. That overlap is the
				// mitre: a butt joint would leave the end grain of one in the plane of the back of
				// the other, and a run trimmed to the face would leave a notch you can see from
				// across the room.
				FHFMeshOps::AppendBox(Mesh,
					FVector3d(RunCentre.X + Offset.X, RunCentre.Y + Offset.Y,
						Room.FloorZ + Room.SkirtingHeight * 0.5),
					FVector3d(RunLength * 0.5, Skirting.Depth * 0.5, Room.SkirtingHeight * 0.5),
					Frame.YawDegrees, EHFSurfaceRole::Skirting);
			}
		}

		// ------------------------------------------------------------------ round the columns
		//
		// A return is a length of skirting that has left the boundary, so it carries its own two
		// points rather than a distance along an edge. The room is on the LEFT of Start -> End - the
		// same winding the boundary uses - which is what lets one offset serve both.
		for (const FHFSkirtingReturn& Run : Skirting.Returns)
		{
			const FWallFrame Frame = MakeWallFrame(Run.Start, Run.End);
			if (!Frame.bValid || Frame.Length <= MinMemberSize)
			{
				continue;
			}

			const FVector2D Centre = (Run.Start + Run.End) * 0.5
				+ Frame.Normal * (Skirting.Depth * 0.5 - Embed);

			FHFMeshOps::AppendBox(Mesh,
				FVector3d(Centre.X, Centre.Y, Room.FloorZ + Room.SkirtingHeight * 0.5),
				FVector3d(Frame.Length * 0.5, Skirting.Depth * 0.5, Room.SkirtingHeight * 0.5),
				Frame.YawDegrees, EHFSurfaceRole::Skirting);
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

	/**
	 * How far inside a loop a point sits, measured to the nearest edge. Negative when outside.
	 *
	 * The figure every zone question here reduces to. "Is this downlight in the band" and "does the
	 * perimeter ring cover this beam" are both "how far from the wall is it", and asking it this way
	 * needs no second polygon offset and cannot disagree with one.
	 */
	double InsetDepth(const TArray<FVector2D>& Loop, const FVector2D& Point)
	{
		if (Loop.Num() < 3)
		{
			return -1.0;
		}

		double Nearest = TNumericLimits<double>::Max();
		for (int32 Index = 0; Index < Loop.Num(); ++Index)
		{
			const FVector2D& A = Loop[Index];
			const FVector2D& B = Loop[(Index + 1) % Loop.Num()];

			const FVector2D Edge = B - A;
			const double LengthSquared = Edge.SizeSquared();
			const double T = (LengthSquared > UE_KINDA_SMALL_NUMBER)
				? FMath::Clamp(FVector2D::DotProduct(Point - A, Edge) / LengthSquared, 0.0, 1.0)
				: 0.0;

			Nearest = FMath::Min(Nearest, FVector2D::Distance(Point, A + Edge * T));
		}

		return PointInPolygon2D(Loop, Point) ? Nearest : -Nearest;
	}

	/** A circle as a closed polygon, for a hole cut through a soffit. */
	TArray<FVector2D> CirclePolygon(const FVector2D& Centre, double Radius, int32 Sides = 16)
	{
		TArray<FVector2D> Out;
		if (Radius <= 0.0)
		{
			return Out;
		}

		Out.Reserve(Sides);
		for (int32 Index = 0; Index < Sides; ++Index)
		{
			const double Angle = 2.0 * PI * static_cast<double>(Index) / Sides;
			Out.Add(Centre + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Radius);
		}
		return Out;
	}

	/** Soffit board thickness. A 12.5 plasterboard with its skim, or a POP soffit. */
	constexpr double HFCeilingPanel = 2.0;

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
	constexpr double HFCeilingLap = 0.5;

	/**
	 * Where the pieces of one false ceiling go, worked out once.
	 *
	 * THE GEOMETRY AND EVERY QUESTION ASKED ABOUT IT COME FROM THE SAME PLACE. GenerateCeiling
	 * builds the ceiling, CeilingSoffitDropAt tells a fan's rod how far it has to reach, and
	 * CeilingDownlights tells the lighting milestone where the fittings are - three answers about
	 * one object, and the first two used to be two hand-copied switch statements that had to be
	 * kept in step by reading them side by side. Adding a perimeter ring and a centre panel would
	 * have made that three copies of a five-branch switch.
	 */
	struct FHFCeilingLayout
	{
		bool bValid = false;

		/** The outline the ceiling is set out from: the bulkhead's own polygon, or the room. */
		TArray<FVector2D> Outline;

		/**
		 * The loops the STYLED ceiling is built on - the outline, or what is left of it inside the
		 * perimeter ring. More than one when a deep ring pinches an L-shaped room into two.
		 */
		TArray<TArray<FVector2D>> StyledLoops;

		/**
		 * The ring's own footprint, clipped to the outline. Empty when there is no ring.
		 *
		 * A REGION RATHER THAN A WIDTH, because the ring runs along the edges a beam actually shows
		 * on and not round the whole room - see FHFFalseCeiling::PerimeterBulkheadEdges.
		 */
		TArray<TArray<FVector2D>> RingLoops;

		double StructuralZ = 0.0;

		/** Soffit of the styled ceiling: the shallow one. */
		double SoffitZ = 0.0;

		/** Soffit of the perimeter ring, when there is one. */
		double RingSoffitZ = 0.0;
		bool bRing = false;

		/**
		 * The zone a downlight may sit in, as insets from a styled loop.
		 *
		 * A fitting has to bore through solid ceiling for its whole diameter. In a band style that
		 * means the solid part of the band - not the cove trough, and not past the inner edge into
		 * open air - and in a panelled style it means anywhere clear of the fascia.
		 */
		double LightZoneInner = 0.0;
		double LightZoneOuter = 0.0;
	};

	FHFCeilingLayout ResolveCeilingLayout(const FHFFalseCeiling& Ceiling, const FHFRoom& Room)
	{
		FHFCeilingLayout Layout;

		if (Ceiling.Style == EHFCeilingStyle::None || Ceiling.Drop <= 0.0)
		{
			return Layout;
		}

		// Bulkheads follow their own polygon; everything else follows the room.
		Layout.Outline = (Ceiling.ExplicitPolygon.Num() >= 3) ? Ceiling.ExplicitPolygon : Room.Boundary;
		if (Layout.Outline.Num() < 3)
		{
			return Layout;
		}

		Layout.StructuralZ = Room.FloorZ + Room.CeilingHeight;
		Layout.SoffitZ = Layout.StructuralZ - Ceiling.Drop;

		if (Layout.SoffitZ <= Room.FloorZ)
		{
			// Validated against elsewhere, but refusing here too keeps the generator honest when it
			// is called directly.
			return Layout;
		}

		Layout.bValid = true;
		Layout.StyledLoops = { Layout.Outline };

		// THE PERIMETER RING GOES ON FIRST AND THE STYLE IS BUILT INSIDE IT. That order is what
		// makes the level change local: the ring buries the beams round the edge of the room, and
		// everything further in than the ring is free to be as shallow as the design wants.
		if (Ceiling.HasPerimeterBulkhead())
		{
			Layout.RingSoffitZ = Layout.StructuralZ - Ceiling.PerimeterBulkheadDrop;

			Layout.RingLoops = FHFMeshOps::IntersectPolygons(Layout.Outline,
				Ceiling.BulkheadStrips(Layout.Outline, Ceiling.PerimeterBulkheadWidth));

			Layout.bRing = !Layout.RingLoops.IsEmpty();

			if (Layout.bRing)
			{
				// The styled part laps INTO the ring rather than stopping in its face, so the two
				// never share a vertical plane - the same lap rule every other piece follows.
				const double Inward = FMath::Max(Ceiling.PerimeterBulkheadWidth - HFCeilingLap, 0.0);

				// The ring swallowed the room. That is an honest answer for a small bathroom rather
				// than an error - what is left is a full drop at the ring's depth - and it is what
				// AppendBand does with an over-wide band for the same reason.
				Layout.StyledLoops = FHFMeshOps::SubtractPolygons(Layout.Outline,
					Ceiling.BulkheadStrips(Layout.Outline, Inward));
			}
		}

		switch (Ceiling.Style)
		{
		case EHFCeilingStyle::Cove:
		{
			// Only the solid part of the band takes a fitting: the trough and the lip are hollow,
			// and a bore through them would open the cove channel into the room below.
			const double ChannelWidth = FMath::Max(Ceiling.Cove.ChannelWidth, 1.0);
			const double LipWidth = FMath::Max(Ceiling.Cove.Setback, 1.0);
			Layout.LightZoneInner = HFCeilingPanel + HFCeilingLap;
			Layout.LightZoneOuter = FMath::Max(Ceiling.BandWidth - ChannelWidth - LipWidth, 1.0);
			break;
		}

		case EHFCeilingStyle::Peripheral:
		case EHFCeilingStyle::Tray:
			Layout.LightZoneInner = HFCeilingPanel + HFCeilingLap;
			Layout.LightZoneOuter = FMath::Max(Ceiling.BandWidth, 1.0);
			break;

		case EHFCeilingStyle::FullDrop:
		case EHFCeilingStyle::Bulkhead:
		default:
			// Clear of the fascia ring standing on the panel's edge, and otherwise anywhere.
			Layout.LightZoneInner = HFCeilingPanel + HFCeilingLap;
			Layout.LightZoneOuter = TNumericLimits<double>::Max();
			break;
		}

		return Layout;
	}

	/**
	 * The downlights that will actually be built into a given loop, with the loop's zone applied.
	 *
	 * A position that does not fit is dropped rather than forced. A fitting straddling the inner
	 * edge of a band would bore a hole half in plasterboard and half in nothing, which is watertight,
	 * plausible in plan, and a slot in the ceiling from underneath.
	 */
	void FittingDownlights(const FHFFalseCeiling& Ceiling, const FHFCeilingLayout& Layout,
		const TArray<FVector2D>& Loop, TArray<FVector2D>& OutPositions)
	{
		if (!Ceiling.Downlight.bRecessed)
		{
			return;
		}

		const double Radius = Ceiling.Downlight.FlangeRadius();
		if (Radius <= 0.0)
		{
			return;
		}

		for (const FVector2D& Position : Ceiling.LightPositions)
		{
			const double Depth = InsetDepth(Loop, Position);
			if (Depth - Radius >= Layout.LightZoneInner && Depth + Radius <= Layout.LightZoneOuter)
			{
				OutPositions.Add(Position);
			}
		}
	}
}

FDynamicMesh3 FHFGenerators::GenerateCeiling(const FHFFalseCeiling& Ceiling, const FHFRoom& Room,
	const TArray<FVector2D>& FanDrops, double FanDropRadius)
{
	FDynamicMesh3 Mesh;
	FHFMeshOps::InitialiseMesh(Mesh);

	const FHFCeilingLayout Layout = ResolveCeilingLayout(Ceiling, Room);
	if (!Layout.bValid)
	{
		return Mesh;
	}

	const double StructuralZ = Layout.StructuralZ;
	const double SoffitZ = Layout.SoffitZ;

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
	 * Builds a band between an outer loop and its inset, with anything else that has to pass
	 * through it punched out at the same time.
	 *
	 * Triangulated as a polygon with holes rather than subtracted as one solid from another. A
	 * mesh boolean resolves that case imperfectly - it returned geometry that was not closed and
	 * reported failure, which silently left every ceiling band solid. The annulus is exact.
	 * The hole also gives the inner fascia for free: the vertical face you actually see standing
	 * under a peripheral ceiling.
	 */
	auto AppendBand = [&Mesh](const TArray<FVector2D>& OuterLoop, double BandWidth,
		double BottomZ, double TopZ, EHFSurfaceRole Role,
		const TArray<TArray<FVector2D>>& ExtraHoles = TArray<TArray<FVector2D>>()) -> bool
	{
		TArray<TArray<FVector2D>> Holes = FHFMeshOps::InsetPolygon(OuterLoop, BandWidth);
		const bool bSwallowed = Holes.IsEmpty();

		Holes.Append(ExtraHoles);

		if (bSwallowed && Holes.IsEmpty())
		{
			// The band is wider than the room, so it becomes a full drop. That is the honest
			// result rather than an error - the geometry is still correct.
			return FHFMeshOps::AppendPrism(Mesh, OuterLoop, BottomZ, TopZ, Role);
		}

		return FHFMeshOps::AppendPrismWithHoles(Mesh, OuterLoop, Holes, BottomZ, TopZ, Role);
	};

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
		if (TopZ - BottomZ <= HFCeilingLap)
		{
			return true;
		}

		return AppendBand(Loop, HFCeilingPanel, BottomZ, TopZ, EHFSurfaceRole::CeilingSoffit);
	};

	/**
	 * A recessed downlight: the trim you see, and the aperture you look up into.
	 *
	 * The bore itself is punched through the soffit by whichever piece carries it, because a hole
	 * in a triangulation is exact where a boolean through a 20 mm board is a coin toss. What is
	 * added here is the two things a bore alone is not: the trim ring standing proud of the plaster,
	 * which is the part that catches light and reads as a fitting rather than as a hole, and the
	 * aperture disc up inside the recess that the lighting milestone will make emissive.
	 */
	auto AppendDownlight = [&Mesh, &Ceiling](const FVector2D& Position, double SoffitPlaneZ) -> bool
	{
		const FHFDownlightProfile& Fitting = Ceiling.Downlight;
		const double CutRadius = Fitting.CutoutRadius();
		const double FlangeRadius = Fitting.FlangeRadius();

		bool bBuilt = true;

		if (Fitting.FlangeProjection > 0.0 && FlangeRadius > CutRadius)
		{
			bBuilt &= FHFMeshOps::AppendPrismWithHoles(Mesh,
				CirclePolygon(Position, FlangeRadius, 20),
				{ CirclePolygon(Position, CutRadius, 20) },
				SoffitPlaneZ - Fitting.FlangeProjection, SoffitPlaneZ,
				EHFSurfaceRole::MetalHardware);
		}

		// The lens, set back up the can. LightSource rather than Glass: it is the emitting face, and
		// as glass it was a pale disc up a hole - which is why a run of downlights read as a row of
		// faint pencil circles in every render of the flat.
		if (Fitting.BodyDepth > 0.0)
		{
			const double LensZ = SoffitPlaneZ + Fitting.BodyDepth;
			bBuilt &= FHFMeshOps::AppendPrism(Mesh,
				CirclePolygon(Position, FMath::Max(CutRadius - 0.2, 0.1), 20),
				LensZ, LensZ + 0.4, EHFSurfaceRole::LightSource);
		}

		return bBuilt;
	};

	// ---------------------------------------------------------------- the perimeter beam bulkhead
	//
	// A solid ring from its own soffit up to the slab, exactly as Peripheral is - so its inner face
	// IS the fascia of the level change, and there is no edge left open by construction.
	if (Layout.bRing)
	{
		for (const TArray<FVector2D>& Loop : Layout.RingLoops)
		{
			// Solid from its own soffit to the slab, so its inner face IS the fascia of the level
			// change and no edge is left open by construction. Only the fan holes that fall in this
			// piece: a hole outside its outline is not a hole, it is a triangulation that refuses.
			TArray<TArray<FVector2D>> Holes;
			for (const TArray<FVector2D>& Hole : FanHoles)
			{
				if (!Hole.IsEmpty() && PointInPolygon2D(Loop, Hole[0]))
				{
					Holes.Add(Hole);
				}
			}

			Checked(FHFMeshOps::AppendPrismWithHoles(Mesh, Loop, Holes,
				Layout.RingSoffitZ, StructuralZ, EHFSurfaceRole::CeilingSoffit));
		}
	}

	// ------------------------------------------------------------------------ the styled ceiling
	for (const TArray<FVector2D>& Outline : Layout.StyledLoops)
	{
		TArray<FVector2D> Downlights;
		FittingDownlights(Ceiling, Layout, Outline, Downlights);

		// The bores, as holes to be punched through whatever carries the soffit here, and the fan
		// rods with them: both are things that pass through the ceiling rather than sit on it.
		TArray<TArray<FVector2D>> SoffitHoles = FanHoles;
		for (const FVector2D& Position : Downlights)
		{
			SoffitHoles.Add(CirclePolygon(Position, Ceiling.Downlight.CutoutRadius(), 16));
		}

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
			Checked(FHFMeshOps::AppendPrismWithHoles(Mesh, Outline, SoffitHoles, SoffitZ,
				SoffitZ + HFCeilingPanel, EHFSurfaceRole::CeilingSoffit));

			// Standing on the panel's top face - see the note on the lap - and never at the soffit,
			// which would put a second horizontal face in the one plane the room certainly can see.
			Checked(AppendFascia(Outline, SoffitZ + HFCeilingPanel, StructuralZ));
			break;
		}

		case EHFCeilingStyle::Peripheral:
		{
			// The band is its own fascia. It is a solid annulus from the soffit to the structure: the
			// inner face is the vertical edge you see standing under it, and the outer edge dies into
			// the wall. Nothing to add here - said out loud so the next style copies the right one.
			Checked(AppendBand(Outline, Ceiling.BandWidth, SoffitZ, StructuralZ,
				EHFSurfaceRole::CeilingSoffit, SoffitHoles));
			break;
		}

		case EHFCeilingStyle::Tray:
		{
			// Outer band at the full drop, inner region stepped back up.
			Checked(AppendBand(Outline, Ceiling.BandWidth, SoffitZ, StructuralZ,
				EHFSurfaceRole::CeilingSoffit, SoffitHoles));

			// A REAL SECOND LEVEL, not a halving. On site a two-level tray is 200 outside and 100
			// inside; halving the outer drop makes the step follow the band instead of being a
			// figure in its own right, and made a shallow tray step by 75 mm - under the board it
			// is cut from. Zero keeps the old behaviour for a ceiling written before the field.
			const double InnerDrop = (Ceiling.InnerDrop > 0.0) ? Ceiling.InnerDrop : Ceiling.Drop * 0.5;
			const double InnerSoffitZ = StructuralZ - FMath::Min(InnerDrop, Ceiling.Drop);

			// The inner panel laps into the band instead of stopping in its face, so the two never share
			// a vertical plane. It needs no fascia of its own: the band IS the fascia for this step -
			// it runs from the lower soffit to the slab and the panel's edge ends inside it - and a
			// fascia here would stand a fin of its own proud of the band's inner face.
			const double InnerInset = (Ceiling.BandWidth > HFCeilingLap)
				? Ceiling.BandWidth - HFCeilingLap
				: Ceiling.BandWidth;

			for (const TArray<FVector2D>& Loop : FHFMeshOps::InsetPolygon(Outline, InnerInset))
			{
				Checked(FHFMeshOps::AppendPrismWithHoles(Mesh, Loop, FanHoles, InnerSoffitZ,
					InnerSoffitZ + HFCeilingPanel, EHFSurfaceRole::CeilingSoffit));
			}
			break;
		}

		case EHFCeilingStyle::Cove:
		{
			// A cove is a peripheral band with a trough at its inner edge. The strip lies in the trough,
			// the lip in front of it keeps the strip out of sight, and the light leaves UPWARD and
			// washes whatever is above - the slab, or the centre panel when there is one.
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
			const double LipRise = FMath::Max(Ceiling.Cove.LipHeight, HFCeilingPanel + 1.0);
			const double SolidBand = FMath::Max(Ceiling.BandWidth - ChannelWidth - LipWidth, 1.0);
			const double BoardTopZ = SoffitZ + HFCeilingPanel;

			// ONE board across the whole band. Band, trough floor and lip all show the room the same
			// plane, and one piece is what keeps it one face: a board per zone would butt them together
			// in the soffit, which is the plane a person in the room is looking straight at.
			Checked(AppendBand(Outline, Ceiling.BandWidth, SoffitZ, BoardTopZ,
				EHFSurfaceRole::CeilingSoffit, SoffitHoles));

			// The band above the board, solid to the slab, standing on the board rather than lapped
			// into it - see the note on the lap.
			//
			// COVEINTERIOR, AND THE ONLY PIECE THAT IS. Every face of this one is buried except its
			// inner face, and that inner face IS the outer wall of the trough - the surface the strip
			// washes and the only thing in the cove that a warm finish belongs on.
			Checked(AppendBand(Outline, SolidBand, BoardTopZ, StructuralZ,
				EHFSurfaceRole::CoveInterior, SoffitHoles));

			// The upstand. CEILINGSOFFIT, not CoveInterior: this is a plastered POP upstand painted
			// with the rest of the ceiling, and the face of it the room can actually see is the one
			// pointing INWARD, at the middle of the room. Tagged as cove interior it came out as a
			// 30 mm tan pinstripe running round every room - which, with nothing emitting anywhere,
			// was the entire visible result of a cove.
			for (const TArray<FVector2D>& LipLoop : FHFMeshOps::InsetPolygon(Outline, SolidBand + ChannelWidth))
			{
				Checked(AppendBand(LipLoop, LipWidth, BoardTopZ, SoffitZ + LipRise, EHFSurfaceRole::CeilingSoffit));
			}

			// THE STRIP ITSELF, lying in the trough. Its top has to stay below the lip top or the
			// cove conceals nothing - that single inequality is the whole of the sight line, because
			// a strip throwing upward sends every ray that clears the lip AWAY from the eye, so the
			// lowest thing any ray from below can reach over the trough is the lip top.
			//
			// Set back from the lip rather than shoved against the far wall of the trough: the
			// setback is what decides how far inboard the wash starts, and a strip in the corner of
			// a trough leaves a dark gap along the surface it is meant to be lighting.
			const double StripWidth = FMath::Min(FMath::Max(Ceiling.Cove.StripWidth, 0.0), ChannelWidth);
			const double StripOuter = SolidBand + ChannelWidth
				- FMath::Clamp(Ceiling.Cove.StripSetback, 0.0, ChannelWidth - StripWidth) - StripWidth;

			if (Ceiling.Cove.bHasLedStrip && StripWidth > 0.0 && Ceiling.Cove.StripHeight > 0.0
				&& StripOuter > 0.0)
			{
				for (const TArray<FVector2D>& StripLoop : FHFMeshOps::InsetPolygon(Outline, StripOuter))
				{
					// LIGHTSOURCE, so something can be made of it. Tagged MetalHardware the strip was
					// a grey bar lying in a trough nobody could see into, which is a faithful model
					// of an LED that is switched off.
					Checked(AppendBand(StripLoop, StripWidth, BoardTopZ,
						BoardTopZ + Ceiling.Cove.StripHeight, EHFSurfaceRole::LightSource));
				}
			}
			break;
		}

		default:
			break;
		}

		// ------------------------------------------------------------------- the centre panel
		//
		// What turns a band into a FRAME: a panel filling the middle, hanging higher than the band
		// it sits inside, so the cove throws its light onto something rather than at bare concrete.
		// Its own fascia closes its edge to the slab, by the same rule every other soffit edge is.
		if (Ceiling.CentrePanelDrop > 0.0 && Ceiling.CentrePanelDrop < Ceiling.Drop)
		{
			const double PanelSoffitZ = StructuralZ - Ceiling.CentrePanelDrop;
			const double PanelInset = FMath::Max(Ceiling.BandWidth - HFCeilingLap, HFCeilingLap);

			for (const TArray<FVector2D>& Loop : FHFMeshOps::InsetPolygon(Outline, PanelInset))
			{
				Checked(FHFMeshOps::AppendPrismWithHoles(Mesh, Loop, FanHoles, PanelSoffitZ,
					PanelSoffitZ + HFCeilingPanel, EHFSurfaceRole::CeilingSoffit));
				Checked(AppendFascia(Loop, PanelSoffitZ + HFCeilingPanel, StructuralZ));
			}
		}

		// -------------------------------------------------------------------- the fittings
		for (const FVector2D& Position : Downlights)
		{
			Checked(AppendDownlight(Position, SoffitZ));
		}
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

TArray<FVector> FHFGenerators::CeilingDownlights(const FHFFalseCeiling& Ceiling, const FHFRoom& Room)
{
	TArray<FVector> Out;

	const FHFCeilingLayout Layout = ResolveCeilingLayout(Ceiling, Room);
	if (!Layout.bValid)
	{
		return Out;
	}

	for (const TArray<FVector2D>& Loop : Layout.StyledLoops)
	{
		TArray<FVector2D> Positions;
		FittingDownlights(Ceiling, Layout, Loop, Positions);

		for (const FVector2D& Position : Positions)
		{
			// The APERTURE, not the plan dot: a real light belongs up inside the can at the lens,
			// so that the trim shades it exactly as the fitting does and the scallop on the wall
			// starts where the reflector says it does rather than at the plasterboard.
			Out.Add(FVector(Position.X, Position.Y, Layout.SoffitZ + Ceiling.Downlight.BodyDepth));
		}
	}

	return Out;
}

TArray<FHFCoveLightRun> FHFGenerators::CeilingCoveLights(const FHFFalseCeiling& Ceiling,
	const FHFRoom& Room)
{
	TArray<FHFCoveLightRun> Runs;

	if (Ceiling.Style != EHFCeilingStyle::Cove || !Ceiling.Cove.bHasLedStrip)
	{
		return Runs;
	}

	const FHFCeilingLayout Layout = ResolveCeilingLayout(Ceiling, Room);
	if (!Layout.bValid)
	{
		return Runs;
	}

	// Set out from exactly the figures GenerateCeiling lays the strip with. Two derivations of one
	// position is how a light comes to sit in the plasterboard beside its own strip.
	const double ChannelWidth = FMath::Max(Ceiling.Cove.ChannelWidth, 1.0);
	const double LipWidth = FMath::Max(Ceiling.Cove.Setback, 1.0);
	const double SolidBand = FMath::Max(Ceiling.BandWidth - ChannelWidth - LipWidth, 1.0);
	const double BoardTopZ = Layout.SoffitZ + HFCeilingPanel;

	const double StripWidth = FMath::Min(FMath::Max(Ceiling.Cove.StripWidth, 0.0), ChannelWidth);
	const double StripOuter = SolidBand + ChannelWidth
		- FMath::Clamp(Ceiling.Cove.StripSetback, 0.0, ChannelWidth - StripWidth) - StripWidth;

	if (StripWidth <= 0.0 || Ceiling.Cove.StripHeight <= 0.0 || StripOuter <= 0.0)
	{
		return Runs;
	}

	// What the wash lands on: the centre panel where there is one, otherwise the slab.
	const double WashedZ = (Ceiling.CentrePanelDrop > 0.0 && Ceiling.CentrePanelDrop < Ceiling.Drop)
		? Layout.StructuralZ - Ceiling.CentrePanelDrop
		: Layout.StructuralZ;

	const double StripTopZ = BoardTopZ + Ceiling.Cove.StripHeight;

	for (const TArray<FVector2D>& Outline : Layout.StyledLoops)
	{
		// The centreline of the strip, which is the loop the geometry is built on offset by half
		// the strip's own width.
		for (const TArray<FVector2D>& Loop : FHFMeshOps::InsetPolygon(Outline, StripOuter + StripWidth * 0.5))
		{
			for (int32 Index = 0; Index < Loop.Num(); ++Index)
			{
				const FVector2D& A = Loop[Index];
				const FVector2D& B = Loop[(Index + 1) % Loop.Num()];

				const double Length = FVector2D::Distance(A, B);

				// A mitre at a corner is a few centimetres of edge, not a run of lighting.
				if (Length <= ChannelWidth)
				{
					continue;
				}

				const FVector2D Middle = (A + B) * 0.5;
				const FVector2D Direction = (B - A) / Length;

				FHFCoveLightRun& Run = Runs.AddDefaulted_GetRef();
				Run.Centre = FVector(Middle.X, Middle.Y, StripTopZ);
				Run.YawDegrees = FMath::RadiansToDegrees(FMath::Atan2(Direction.Y, Direction.X));
				Run.Length = Length;
				Run.Width = ChannelWidth;
				Run.ThrowHeight = FMath::Max(WashedZ - StripTopZ, 1.0);
			}
		}
	}

	return Runs;
}

double FHFGenerators::CeilingSoffitDropAt(const FHFFalseCeiling& Ceiling, const FHFRoom& Room,
	const FVector2D& Point)
{
	const FHFCeilingLayout Layout = ResolveCeilingLayout(Ceiling, Room);
	if (!Layout.bValid || !PointInPolygon2D(Layout.Outline, Point))
	{
		return 0.0;
	}

	// Inside the perimeter ring, which covers everything under it whatever the style does further in.
	if (Layout.bRing && !InsideAnyLoop(Layout.StyledLoops, Point))
	{
		return Ceiling.PerimeterBulkheadDrop;
	}

	const TArray<FVector2D>* Outline = nullptr;
	for (const TArray<FVector2D>& Loop : Layout.StyledLoops)
	{
		if (PointInPolygon2D(Loop, Point))
		{
			Outline = &Loop;
			break;
		}
	}

	if (Outline == nullptr)
	{
		return 0.0;
	}

	// Inside the band, the centre panel is what a rod meets first.
	auto CentreDrop = [&Ceiling]() -> double
	{
		return (Ceiling.CentrePanelDrop > 0.0 && Ceiling.CentrePanelDrop < Ceiling.Drop)
			? Ceiling.CentrePanelDrop
			: 0.0;
	};

	// Mirrors the switch above case for case. The two answer the same question about the same
	// geometry - what covers this spot - and a fan resolved against a different answer from the one
	// the panel was built with is a fan in the plasterboard.
	switch (Ceiling.Style)
	{
	case EHFCeilingStyle::FullDrop:
	case EHFCeilingStyle::Bulkhead:
		// A panel across the whole outline.
		return Ceiling.Drop;

	case EHFCeilingStyle::Peripheral:
	case EHFCeilingStyle::Cove:
	{
		// Band only; the centre is open to the slab unless a centre panel fills it, which is why
		// the three fans in the reference flat hang in clear air and why nothing caught this.
		//
		// A cove answers the same way. Its trough and its lip are cut out of the top of the band,
		// so the soffit under the whole band width is one plane at the full drop - there is no
		// longer a step in the underside for a fan to be resolved against.
		const TArray<TArray<FVector2D>> Inner = FHFMeshOps::InsetPolygon(*Outline, Ceiling.BandWidth);
		if (Inner.IsEmpty())
		{
			// The band swallowed the room, so it built as a full drop. Same answer here.
			return Ceiling.Drop;
		}
		return InsideAnyLoop(Inner, Point) ? CentreDrop() : Ceiling.Drop;
	}

	case EHFCeilingStyle::Tray:
	{
		const TArray<TArray<FVector2D>> Inner = FHFMeshOps::InsetPolygon(*Outline, Ceiling.BandWidth);
		if (Inner.IsEmpty())
		{
			return Ceiling.Drop;
		}

		// The inner region steps back up, and it is still a panel.
		const double InnerDrop = (Ceiling.InnerDrop > 0.0) ? Ceiling.InnerDrop : Ceiling.Drop * 0.5;
		return InsideAnyLoop(Inner, Point) ? FMath::Min(InnerDrop, Ceiling.Drop) : Ceiling.Drop;
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
		bool bMeetingStileAtMaxX, const FHFSlidingSashSection& Section)
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

		// EVERY SASH OF A TWO-TRACK UNIT CARRIES A PULL, because either of them is the one you push.
		// A real Domal or UPVC slider has a D-pull or a flush catch on BOTH meeting stiles - it has
		// to, or half the unit could not be operated - and the plugin used to fit one only to the
		// leaf it had designated the runner. That reads as correct in a still of a shut door and is
		// a missing handle the moment anybody opens the unit from the other end.
		{
			// On the meeting stile, projecting AWAY from the other sash's track.
			//
			// Which END the meeting stile is on is the caller's to say: the leaf set out from the
			// near jamb meets its partner at its far edge, and the leaf set out from the far jamb
			// meets it at its near edge. Reading it off the geometry here would put both pulls on
			// the same side of the unit.
			//
			// Which FACE is not a style choice, it is a clearance. Two tracks are one leaf thickness
			// and a running clearance apart, and a pull stands 15-30 mm proud of the leaf it is
			// screwed to; put the outer leaf's pull on the room side and the inner leaf drives
			// through it every time either one moves. That reads perfectly well in a still of a shut
			// unit, which is the same trap the wardrobe's applied handles fell into.
			//
			// So each pull faces out of its own track. On a balcony door that is exactly right - a
			// pull inside and a pull on the balcony is what one has. On a window it means the outer
			// sash's catch faces outward, where a real domal unit would use a slim flush pull inside
			// the 10 mm gap; that is a simplification, and it is the one that cannot clash.
			const double Side = TrackY >= 0.0 ? 1.0 : -1.0;
			const double Half = Section.HandleHeight * 0.5;
			const double Lowest = ZMin + Rail + Half;
			const double Highest = FMath::Max(ZMax - Face - Half, Lowest);
			const double HandleZ = Section.HandleAboveSill > 0.0
				? FMath::Clamp(ZMin + Section.HandleAboveSill, Lowest, Highest)
				: (ZMin + ZMax) * 0.5;
			const double HandleX = bMeetingStileAtMaxX ? XMax - Face * 0.5 : XMin + Face * 0.5;

			FHFMeshOps::AppendBox(Mesh,
				FVector3d(HandleX,
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
	 * Which leaf of a sliding unit a single "open it" runs.
	 *
	 * A TWO-PANEL SLIDER OPENS FROM EITHER END, and which end is a property of the drawing rather
	 * than of the construction. It is exactly the handedness the plan already carries: a swing arc
	 * drawn to the left says the left half of the unit is the part that gets used, and for a slider
	 * that means the leaf set out from the near jamb is the one that runs and the daylight appears
	 * at that jamb. So EHFSwing is read here rather than a second handedness field being invented
	 * for sliders, which would be one more thing a spec could contradict itself about.
	 *
	 * The leaf that does NOT lead still slides - see FHFPartMotion::bMasterOpens. This decides which
	 * one a master control reaches for, not which one is capable of moving.
	 *
	 * None means the near jamb, matching the hinged case where an undrawn swing hangs on the near
	 * jamb too.
	 */
	bool NearLeafLeads(EHFSwing Swing)
	{
		return Swing != EHFSwing::InwardRight && Swing != EHFSwing::OutwardRight;
	}

	/**
	 * Two leaves on two tracks, EITHER of which runs.
	 *
	 * A single leaf the width of the opening has nowhere to go - sliding it its own width buries it
	 * in the masonry beside the jamb - so each leaf takes half the clear opening, laps past the
	 * meeting line, and a running leaf travels until its far edge meets its partner's. That keeps
	 * every leaf wholly inside the reveal at every open amount, and it is also what a sliding unit
	 * IS. Both balcony doors in the reference flat were rebuilt around this after one of them slid
	 * bodily through the window next to it.
	 *
	 * BOTH LEAVES RUN, AND THAT IS THE CHANGE. The unit used to be one fixed panel and one runner,
	 * which opened it and opened it one way only: the aperture appeared at the same jamb whatever
	 * anybody wanted. A real two-track unit has gear on both leaves and you slide whichever one you
	 * like. So both carry a Slide motion, each set out from its own jamb and travelling towards the
	 * other's bay, and each names the other as its alternate.
	 *
	 * What stops that collapsing back into the defect it came from is bMasterOpens. Two leaves
	 * driven out TOGETHER by one amount exchange tracks and uncover nothing - the run is as covered
	 * at full open as it was shut - so exactly one of the pair is the one a master control drives,
	 * and the other is shut whenever the master speaks. Either can still be posed by hand or through
	 * AHFArticulatedActor::OpenRunFrom, which is what "open it both ways" actually needs.
	 *
	 * One function for the sliding window and the sliding door, with the set-out itself in
	 * FHFSlidingSetOut where a sliding wardrobe shutter reaches it too. Two implementations of this
	 * rule is how the two drifted apart in the first place.
	 *
	 * @param NearX          Where the clear opening starts, measured from the unit's pivot.
	 * @param bNearLeafLeads Which of the pair a master control runs. The other still slides.
	 */
	void BuildSlidingPair(const FTransform& UnitPivot, TArray<FHFMeshPart>& OutParts,
		double NearX, double ClearWidth, double ZMin, double ZMax,
		double TrackPitch, double InterlockOverlap, const FHFSlidingSashSection& Section,
		const FName& NearPartId, const FName& FarPartId, bool bNearLeafLeads)
	{
		// Set out once, from the near jamb, and mirrored for the far leaf. The mirror carries the
		// signed travel with it, so the far leaf's run is the near leaf's rule rather than a second
		// piece of arithmetic that can drift from it.
		const FHFSlidingSetOut Near =
			FHFSlidingSetOut::Leaf(ClearWidth * 0.5, InterlockOverlap * 0.5, /*EndGap*/ 0.0);
		const FHFSlidingSetOut Far = Near.MirroredIn(ClearWidth);

		const double TrackY = TrackPitch * 0.5;

		// The near leaf runs on the room-side track and the far one behind it, unchanged from when
		// the far one was fixed: the tracks are what let the pair lap without sharing a volume.
		FHFMeshPart NearLeaf;
		NearLeaf.PartId = NearPartId;
		NearLeaf.Mesh = MakeSlidingSash(NearX + Near.NearEdge, NearX + Near.FarEdge, TrackY,
			ZMin, ZMax, /*bMeetingStileAtMaxX*/ true, Section);
		NearLeaf.PivotTransform = UnitPivot;
		NearLeaf.Motion.Type = EHFMotionType::Slide;
		NearLeaf.Motion.Axis = FVector::XAxisVector;

		// Far edge to far edge: a running leaf comes to rest exactly over its partner, so it is
		// still wholly inside the reveal at full travel.
		NearLeaf.Motion.MaxTravelCm = Near.Travel;
		NearLeaf.Motion.AlternateToPartId = FarPartId;
		NearLeaf.Motion.bMasterOpens = bNearLeafLeads;
		OutParts.Add(MoveTemp(NearLeaf));

		FHFMeshPart FarLeaf;
		FarLeaf.PartId = FarPartId;
		FarLeaf.Mesh = MakeSlidingSash(NearX + Far.NearEdge, NearX + Far.FarEdge, -TrackY,
			ZMin, ZMax, /*bMeetingStileAtMaxX*/ false, Section);
		FarLeaf.PivotTransform = UnitPivot;
		FarLeaf.Motion.Type = EHFMotionType::Slide;
		FarLeaf.Motion.Axis = FVector::XAxisVector;
		FarLeaf.Motion.MaxTravelCm = Far.Travel;
		FarLeaf.Motion.AlternateToPartId = NearPartId;
		FarLeaf.Motion.bMasterOpens = !bNearLeafLeads;
		OutParts.Add(MoveTemp(FarLeaf));
	}

	/** The two sashes of a sliding window, set out between its outer frame's members. */
	void BuildSlidingWindowSashes(const FHFOpening& Opening, const FTransform& UnitPivot,
		TArray<FHFMeshPart>& OutParts, const FHFSlidingWindowParams& Params)
	{
		BuildSlidingPair(UnitPivot, OutParts,
			/*NearX*/ Params.FrameFace, Params.ClearWidth(Opening.Width),
			/*ZMin*/ Params.FrameFace, /*ZMax*/ Opening.Height - Params.FrameFace,
			Params.TrackPitch, Params.InterlockOverlap, Params.SashSection(),
			TEXT("SashNear"), TEXT("SashFar"), NearLeafLeads(Opening.Swing));
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
			TEXT("LeafNear"), TEXT("LeafFar"), NearLeafLeads(Opening.Swing));
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
