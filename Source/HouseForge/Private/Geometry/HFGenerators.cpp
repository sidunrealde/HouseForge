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

FDynamicMesh3 FHFGenerators::GenerateWall(const FHFWall& Wall, const TArray<FHFOpening>& OpeningsInWall)
{
	FDynamicMesh3 Mesh;
	FHFMeshOps::InitialiseMesh(Mesh);

	const FWallFrame Frame = MakeWallFrame(Wall.Start, Wall.End);
	if (!Frame.bValid || Wall.Thickness <= 0.0 || Wall.Height <= 0.0)
	{
		return Mesh;
	}

	const FVector2D Midpoint = (Wall.Start + Wall.End) * 0.5;
	const FVector3d Centre(Midpoint.X, Midpoint.Y, Wall.BaseZ + Wall.Height * 0.5);
	const FVector3d Extents(Frame.Length * 0.5, Wall.Thickness * 0.5, Wall.Height * 0.5);

	FHFMeshOps::AppendBox(Mesh, Centre, Extents, Frame.YawDegrees, Wall.SurfaceRole);

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

	switch (Ceiling.Style)
	{
	case EHFCeilingStyle::FullDrop:
	case EHFCeilingStyle::Bulkhead:
	{
		// A flat panel across the whole outline, plus a fascia dropping from the structure to it
		// so the edge reads as a boxed soffit rather than a floating sheet.
		Checked(FHFMeshOps::AppendPrismWithHoles(Mesh, Outline, FanHoles, SoffitZ, SoffitZ + PanelThickness,
			EHFSurfaceRole::CeilingSoffit));

		if (Ceiling.Style == EHFCeilingStyle::Bulkhead)
		{
			// Hollow, so only the perimeter face remains - a bulkhead is a box, not a plug.
			Checked(AppendBand(Outline, PanelThickness, SoffitZ, StructuralZ, EHFSurfaceRole::CeilingSoffit));
		}
		break;
	}

	case EHFCeilingStyle::Peripheral:
	{
		Checked(AppendBand(Outline, Ceiling.BandWidth, SoffitZ, StructuralZ, EHFSurfaceRole::CeilingSoffit));
		break;
	}

	case EHFCeilingStyle::Tray:
	{
		// Outer band at the full drop, inner region stepped back up to half of it.
		Checked(AppendBand(Outline, Ceiling.BandWidth, SoffitZ, StructuralZ, EHFSurfaceRole::CeilingSoffit));

		const double InnerSoffitZ = StructuralZ - Ceiling.Drop * 0.5;
		for (const TArray<FVector2D>& Loop : FHFMeshOps::InsetPolygon(Outline, Ceiling.BandWidth))
		{
			Checked(FHFMeshOps::AppendPrismWithHoles(Mesh, Loop, FanHoles, InnerSoffitZ,
				InnerSoffitZ + PanelThickness, EHFSurfaceRole::CeilingSoffit));
		}
		break;
	}

	case EHFCeilingStyle::Cove:
	{
		// The band, then a channel recessed behind a lip. The lip is what hides the LED strip from
		// direct view, which is the entire point of a cove.
		const double LipHeight = FMath::Max(Ceiling.Cove.LipHeight, 1.0);
		const double ChannelWidth = FMath::Max(Ceiling.Cove.ChannelWidth, 1.0);
		const double Setback = FMath::Max(Ceiling.Cove.Setback, 0.0);

		const double BandInner = FMath::Max(Ceiling.BandWidth - ChannelWidth - Setback, 1.0);

		Checked(AppendBand(Outline, BandInner, SoffitZ, StructuralZ, EHFSurfaceRole::CeilingSoffit));

		// The channel floor sits above the band soffit, behind the lip.
		for (const TArray<FVector2D>& Lip : FHFMeshOps::InsetPolygon(Outline, BandInner))
		{
			const TArray<TArray<FVector2D>> Inner = FHFMeshOps::InsetPolygon(Lip, ChannelWidth);
			if (Inner.IsEmpty())
			{
				Checked(FHFMeshOps::AppendPrism(Mesh, Lip, SoffitZ + LipHeight, StructuralZ, EHFSurfaceRole::CoveInterior));
			}
			else
			{
				Checked(FHFMeshOps::AppendPrismWithHoles(Mesh, Lip, Inner, SoffitZ + LipHeight, StructuralZ,
					EHFSurfaceRole::CoveInterior));
			}
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
	{
		// Band only; the centre is open to the slab, which is why the three fans in the reference
		// flat hang in clear air and why nothing caught this.
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

	case EHFCeilingStyle::Cove:
	{
		const double LipHeight = FMath::Max(Ceiling.Cove.LipHeight, 1.0);
		const double ChannelWidth = FMath::Max(Ceiling.Cove.ChannelWidth, 1.0);
		const double Setback = FMath::Max(Ceiling.Cove.Setback, 0.0);
		const double BandInner = FMath::Max(Ceiling.BandWidth - ChannelWidth - Setback, 1.0);

		const TArray<TArray<FVector2D>> Lips = FHFMeshOps::InsetPolygon(Outline, BandInner);
		if (Lips.IsEmpty())
		{
			return Ceiling.Drop;
		}

		if (!InsideAnyLoop(Lips, Point))
		{
			// Out in the band itself.
			return Ceiling.Drop;
		}

		// Inside the lip: either the channel, whose floor sits a lip's height above the band soffit,
		// or the open centre beyond it.
		for (const TArray<FVector2D>& Lip : Lips)
		{
			if (!PointInPolygon2D(Lip, Point))
			{
				continue;
			}

			const TArray<TArray<FVector2D>> Centre = FHFMeshOps::InsetPolygon(Lip, ChannelWidth);
			if (Centre.IsEmpty())
			{
				// Channel wider than what is left, so the whole lip is channel floor.
				return FMath::Max(Ceiling.Drop - LipHeight, 0.0);
			}

			return InsideAnyLoop(Centre, Point)
				? 0.0
				: FMath::Max(Ceiling.Drop - LipHeight, 0.0);
		}

		return 0.0;
	}

	default:
		return 0.0;
	}
}

FDynamicMesh3 FHFGenerators::GenerateBeam(const FHFBeam& Beam)
{
	FDynamicMesh3 Mesh;
	FHFMeshOps::InitialiseMesh(Mesh);

	const FWallFrame Frame = MakeWallFrame(Beam.Start, Beam.End);
	if (!Frame.bValid || Beam.Width <= 0.0 || Beam.Depth <= 0.0)
	{
		return Mesh;
	}

	const FVector2D Midpoint = (Beam.Start + Beam.End) * 0.5;

	// Beams hang down from the slab soffit, so they occupy ClearHeight..SoffitZ.
	FHFMeshOps::AppendBox(Mesh,
		FVector3d(Midpoint.X, Midpoint.Y, Beam.SoffitZ - Beam.Depth * 0.5),
		FVector3d(Frame.Length * 0.5, Beam.Width * 0.5, Beam.Depth * 0.5),
		Frame.YawDegrees, Beam.SurfaceRole);

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

	// Leaf-local space: the pivot is the origin, the leaf runs along +X to the far jamb, its
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

	// The inset all round is the gap a real leaf leaves in its frame; without it the leaf shares
	// faces with the reveal and the two z-fight.
	const double Gap = Params.LeafFrameGap;

	FHFMeshOps::AppendBox(Mesh,
		FVector3d(Opening.Width * 0.5, LeafY, Opening.Height * 0.5),
		FVector3d(Opening.Width * 0.5 - Gap, Params.LeafThickness * 0.5, Opening.Height * 0.5 - Gap),
		0.0, EHFSurfaceRole::DoorLeaf);

	FHFMeshOps::ApplyWorldScaleUVs(Mesh);
	return Mesh;
}

namespace
{
	/**
	 * One panel of a sliding unit, in the unit's local space.
	 *
	 * X is measured from the near jamb, Y is the track the panel runs in. Panels are generated
	 * where they sit rather than each about its own origin, so both share the unit's pivot and the
	 * only difference between them is that one of them moves.
	 */
	FDynamicMesh3 MakeSlidingPanel(double XMin, double XMax, double TrackY, double Height,
		const FHFDoorParams& Params)
	{
		FDynamicMesh3 Mesh;
		FHFMeshOps::InitialiseMesh(Mesh);

		if (XMax - XMin <= MinMemberSize || Height <= MinMemberSize)
		{
			return Mesh;
		}

		FHFMeshOps::AppendBox(Mesh,
			FVector3d((XMin + XMax) * 0.5, TrackY, Height * 0.5),
			FVector3d((XMax - XMin) * 0.5, Params.LeafThickness * 0.5, Height * 0.5 - Params.LeafFrameGap),
			0.0, EHFSurfaceRole::DoorLeaf);

		FHFMeshOps::ApplyWorldScaleUVs(Mesh);
		return Mesh;
	}

	/**
	 * One sash of a sliding window, in the unit's local space.
	 *
	 * X is measured from the near jamb, Y is the track the sash rides in and Z runs up from the
	 * sill. Like the sliding door's panels, both sashes are generated where they sit rather than
	 * each about its own origin, so they share the unit's pivot and the only difference between
	 * them is that one of them moves.
	 *
	 * Built as a picture frame - two stiles full height with the rails let in between them - so no
	 * two members share a volume and the sash measures exactly its section times its perimeter. The
	 * pane then engages into the glazing groove of all four, which is where a real one sits and why
	 * it is a solid rather than a plane.
	 */
	FDynamicMesh3 MakeSlidingSash(double XMin, double XMax, double TrackY, double ZMin, double ZMax,
		bool bWithHandle, const FHFSlidingWindowParams& Params)
	{
		FDynamicMesh3 Mesh;
		FHFMeshOps::InitialiseMesh(Mesh);

		const double Face = Params.SashFaceWidth;
		const double Width = XMax - XMin;
		const double Height = ZMax - ZMin;
		if (Width <= Face * 2.0 || Height <= Face * 2.0)
		{
			return Mesh;
		}

		auto AppendMember = [&Mesh, TrackY, &Params](double MemberXMin, double MemberXMax,
			double MemberZMin, double MemberZMax)
		{
			FHFMeshOps::AppendBox(Mesh,
				FVector3d((MemberXMin + MemberXMax) * 0.5, TrackY, (MemberZMin + MemberZMax) * 0.5),
				FVector3d((MemberXMax - MemberXMin) * 0.5, Params.SashDepth * 0.5,
					(MemberZMax - MemberZMin) * 0.5),
				0.0, EHFSurfaceRole::WindowFrame);
		};

		AppendMember(XMin, XMin + Face, ZMin, ZMax);            // stile
		AppendMember(XMax - Face, XMax, ZMin, ZMax);            // stile

		// The bottom rail is deeper than the track upstand it comes to rest over, and the two
		// interpenetrate. A real bottom rail is hollow and the upstand runs up inside it, so that is
		// the section rather than a clash - and it is hidden inside the rail either way.
		AppendMember(XMin + Face, XMax - Face, ZMin, ZMin + Face);
		AppendMember(XMin + Face, XMax - Face, ZMax - Face, ZMax);

		// The pane, engaged into the glazing groove of all four members.
		const double GlassInset = Face - Params.GlassRebate;
		FHFMeshOps::AppendBox(Mesh,
			FVector3d((XMin + XMax) * 0.5, TrackY, (ZMin + ZMax) * 0.5),
			FVector3d(Width * 0.5 - GlassInset, Params.GlassThickness * 0.5, Height * 0.5 - GlassInset),
			0.0, EHFSurfaceRole::Glass);

		if (bWithHandle)
		{
			// On the meeting stile, projecting away from the other sash's track - which is the face
			// a hand can reach and the side the catch is fitted on.
			const double Side = TrackY >= 0.0 ? 1.0 : -1.0;

			FHFMeshOps::AppendBox(Mesh,
				FVector3d(XMax - Face * 0.5,
					TrackY + Side * (Params.SashDepth + Params.HandleProjection) * 0.5,
					(ZMin + ZMax) * 0.5),
				FVector3d(Params.HandleWidth * 0.5, Params.HandleProjection * 0.5, Params.HandleHeight * 0.5),
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
	 * Two sashes on two tracks: one fixed, one running.
	 *
	 * The same shape of answer the sliding DOOR arrived at the hard way, and for the same reason. A
	 * single sash the width of the opening has nowhere to go - sliding it its own width buries it in
	 * the masonry beside the jamb - so each sash takes half the clear opening, and the running one
	 * travels until its far edge meets the fixed one's. That keeps every sash wholly inside the
	 * reveal at every open amount, and it is also what a sliding window is.
	 */
	void BuildSlidingWindowSashes(const FHFOpening& Opening, const FTransform& UnitPivot,
		TArray<FHFMeshPart>& OutParts, const FHFSlidingWindowParams& Params)
	{
		// The clear opening between the outer frame's members - what the sashes actually fill.
		const double ClearWidth = Params.ClearWidth(Opening.Width);
		const double Near = Params.FrameFace;
		const double ZMin = Params.FrameFace;
		const double ZMax = Opening.Height - Params.FrameFace;

		const double Half = ClearWidth * 0.5;
		const double HalfOverlap = Params.InterlockOverlap * 0.5;
		const double TrackY = Params.TrackPitch * 0.5;

		FHFMeshPart Fixed;
		Fixed.PartId = TEXT("SashFixed");
		Fixed.Mesh = MakeSlidingSash(Near + Half - HalfOverlap, Near + ClearWidth, -TrackY, ZMin, ZMax,
			/*bWithHandle*/ false, Params);
		Fixed.PivotTransform = UnitPivot;
		OutParts.Add(MoveTemp(Fixed));

		FHFMeshPart Sash;
		Sash.PartId = TEXT("Sash");
		Sash.Mesh = MakeSlidingSash(Near, Near + Half + HalfOverlap, TrackY, ZMin, ZMax,
			/*bWithHandle*/ true, Params);
		Sash.PivotTransform = UnitPivot;
		Sash.Motion.Type = EHFMotionType::Slide;
		Sash.Motion.Axis = FVector::XAxisVector;

		// Far edge to far edge: the running sash comes to rest exactly over its fixed partner, so it
		// is still wholly inside the reveal at full travel.
		Sash.Motion.MaxTravelCm = FMath::Max(0.0, Half - HalfOverlap);
		OutParts.Add(MoveTemp(Sash));
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

	// Which jamb the leaf hangs on, matching the swing arc the plan preview draws. A door hung on
	// the wrong jamb is invisible in elevation and wrong in every walkthrough.
	const bool bHingeAtNear =
		Opening.Swing == EHFSwing::InwardLeft ||
		Opening.Swing == EHFSwing::OutwardLeft ||
		Opening.Swing == EHFSwing::None;

	const FVector2D HingePlan = bHingeAtNear
		? Centre - Frame.Direction * HalfWidth
		: Centre + Frame.Direction * HalfWidth;

	// Local +X points from the pivot towards the other jamb, so the leaf mesh is the same whichever
	// jamb it hangs on and only the pivot rotation differs.
	const FVector2D LeafDirection = bHingeAtNear ? Frame.Direction : -Frame.Direction;
	const double LeafYaw = FMath::RadiansToDegrees(FMath::Atan2(LeafDirection.Y, LeafDirection.X));

	const FTransform Pivot(FRotator(0.0, LeafYaw, 0.0), FVector(HingePlan.X, HingePlan.Y, SillZ));

	if (Opening.Kind == EHFOpeningKind::SlidingDoor)
	{
		// A sliding unit is two panels on two tracks, not one leaf that slides into the wall.
		//
		// One leaf the full width of the opening has nowhere to go: sliding it its own width buries
		// it in the masonry beside the jamb, or drives it through the next window along, which is
		// what the 1800 balcony units in the reference flat did. Half the opening each, and the
		// moving panel travelling until its far edge meets the fixed panel's, keeps every panel
		// inside the reveal at every open amount - and is what a sliding unit actually is.
		const FHFDoorParams& Door = Params.Door;

		const double TrackY = (Door.LeafThickness + Door.SlidingTrackGap) * 0.5;

		// The set-out rule itself lives in FHFSlidingSetOut, because a sliding wardrobe shutter is
		// the same problem and must not grow a second copy of the answer.
		const FHFSlidingSetOut Running =
			FHFSlidingSetOut::Leaf(Opening.Width * 0.5, Door.SlidingPanelOverlap, Door.LeafFrameGap);
		const FHFSlidingSetOut Standing = Running.MirroredIn(Opening.Width);

		FHFMeshPart Fixed;
		Fixed.PartId = TEXT("PanelFixed");
		Fixed.Mesh = MakeSlidingPanel(Standing.NearEdge, Standing.FarEdge, -TrackY, Opening.Height, Door);
		Fixed.PivotTransform = Pivot;
		OutParts.Add(MoveTemp(Fixed));

		FHFMeshPart Slider;
		Slider.PartId = TEXT("Leaf");
		Slider.Mesh = MakeSlidingPanel(Running.NearEdge, Running.FarEdge, TrackY, Opening.Height, Door);
		Slider.PivotTransform = Pivot;
		Slider.Motion.Type = EHFMotionType::Slide;
		Slider.Motion.Axis = FVector::XAxisVector;

		// Far edge to far edge: the panel comes to rest exactly over its fixed partner, so it is
		// still wholly inside the opening at full travel.
		Slider.Motion.MaxTravelCm = Running.Travel;
		OutParts.Add(MoveTemp(Slider));
		return;
	}

	// Local +Y is up-cross-leaf-direction, which is the wall normal when the leaf hangs on the
	// near jamb and its opposite when it hangs on the far one. Inward swings follow the wall
	// normal, outward swings oppose it, so the sign flips with both choices.
	const double InwardSign =
		(Opening.Swing == EHFSwing::OutwardLeft || Opening.Swing == EHFSwing::OutwardRight) ? -1.0 : 1.0;
	const double HingeSign = bHingeAtNear ? InwardSign : -InwardSign;

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

	// An archway is a hole and nothing else, and a door's only infill is its leaf, which moves.
	if (Opening.Kind == EHFOpeningKind::Archway ||
		Opening.Kind == EHFOpeningKind::Door ||
		Opening.Kind == EHFOpeningKind::SlidingDoor)
	{
		return Mesh;
	}

	const FVector2D Plan = Wall.Start + Frame.Direction * Opening.OffsetAlongWall;
	const double SillZ = Wall.BaseZ + Opening.SillHeight;
	const double CentreZ = SillZ + Opening.Height * 0.5;

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

		const double HalfWidth = Opening.Width * 0.5;
		const double HalfHeight = Opening.Height * 0.5;

		auto AppendMember = [&](double OffsetAlong, double OffsetAcross, double OffsetUp,
			double HalfAlong, double HalfAcross, double HalfUp, EHFSurfaceRole Role)
		{
			const FVector2D MemberPlan = Plan + Frame.Direction * OffsetAlong + Frame.Normal * OffsetAcross;
			FHFMeshOps::AppendBox(Mesh,
				FVector3d(MemberPlan.X, MemberPlan.Y, CentreZ + OffsetUp),
				FVector3d(HalfAlong, HalfAcross, HalfUp),
				Frame.YawDegrees, Role);
		};

		auto AppendFrameMember = [&](double OffsetAlong, double OffsetUp, double HalfAlong, double HalfUp)
		{
			AppendMember(OffsetAlong, 0.0, OffsetUp, HalfAlong, FrameDepth * 0.5, HalfUp,
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
				AppendMember(0.0, Side * Sliding.TrackPitch * 0.5,
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
			AppendMember(0.0, 0.0, 0.0, HalfWidth - FaceWidth, Params.FixedWindow.GlassThickness * 0.5,
				HalfHeight - FaceWidth, EHFSurfaceRole::Glass);
		}
	}

	FHFMeshOps::ApplyWorldScaleUVs(Mesh);
	return Mesh;
}
