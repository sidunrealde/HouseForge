// Copyright Siddartha G. All Rights Reserved.

#include "Geometry/HFGenerators.h"

#include "DynamicMesh/MeshTransforms.h"
#include "Geometry/HFMeshOps.h"
#include "HouseForge.h"

using namespace UE::Geometry;

namespace
{
	/** How far an opening cutter overshoots the wall faces, in centimetres. */
	constexpr double CutterOvershoot = 5.0;

	/** Door leaf and window frame thicknesses. */
	constexpr double DoorLeafThickness = 4.0;

	/** Interlock between the meeting stiles of a sliding unit, so its two panels never show a gap. */
	constexpr double SlidingPanelOverlap = 2.5;

	/** Clearance between the two tracks of a sliding unit, so its panels pass rather than collide. */
	constexpr double SlidingTrackGap = 1.0;
	constexpr double WindowFrameDepth = 6.0;
	constexpr double WindowFrameWidth = 5.0;
	constexpr double GlassThickness = 0.8;

	// ------------------------------------------------------- aluminium sliding window, two-track
	//
	// Modelled on the 27 mm Domal series, which is THE commodity window of the flats this plugin
	// exists for: a two-track outer frame 65 mm deep carrying two 27 mm sashes. TWO-track rather
	// than three - the third track is the flyscreen option, it deepens the frame to 92.5 mm, and it
	// is an upgrade rather than what a flat of this class ships with. The arithmetic is the check
	// that these are a real section rather than plausible numbers: two 27 mm sashes plus running
	// clearance is exactly the published 65 mm frame.

	/** Outer frame depth, front to back. Sits inside the wall reveal. */
	constexpr double SlidingFrameDepth = 6.5;

	/** How far the outer frame eats into the clear opening on each side. Fabricators quote 40-50. */
	constexpr double SlidingFrameFace = 4.5;

	/** Sash section depth, measured along the wall normal. The series is named for it. */
	constexpr double SashDepth = 2.7;

	/** Track pitch, centre to centre. 27.5-30 is the band; 30 leaves 3 mm between the sashes. */
	constexpr double SashTrackPitch = 3.0;

	/** Sight line of the sash stiles and rails around the glass. */
	constexpr double SashFaceWidth = 4.0;

	/**
	 * Total overlap of the two meeting stiles when closed.
	 *
	 * No manufacturer publishes this figure; 15-25 mm is what the section geometry allows and 25 is
	 * the top of that band. It is what stops daylight showing between the two sashes, so it is the
	 * one number here that is visible in a render rather than only in a section.
	 */
	constexpr double SashInterlockOverlap = 2.5;

	/** 5 mm clear toughened - the near-universal included spec. A solid, never a plane. */
	constexpr double SashGlassThickness = 0.5;

	/** How far the pane sits into the sash's glazing groove. The groove itself is 18 mm. */
	constexpr double SashGlassRebate = 0.9;

	/** The upstand a sash's rollers ride on, standing proud of the frame's sill member. */
	constexpr double SashTrackUpstand = 1.5;
	constexpr double SashTrackWidth = 0.6;

	/** The catch on the meeting stile: the only part of a sliding window anybody touches. */
	constexpr double SashHandleProjection = 1.2;
	constexpr double SashHandleWidth = 1.6;
	constexpr double SashHandleHeight = 8.0;

	// ------------------------------------------------------------------ top-hung ventilator sash
	//
	// A ventilator can be a fixed louvre, in which case nothing about it moves and the rule that
	// anything which moves must be able to move is already satisfied. A top-hung pivot sash is the
	// other half of the category and it DOES move: it hangs on hinges at its head and its bottom
	// edge swings out. That is what is built here, because a ventilator that cannot be opened is a
	// hole with glass in it.

	constexpr double VentilatorFrameDepth = 6.0;
	constexpr double VentilatorFrameFace = 3.5;

	/** Shutter thickness. IS practice is 20/25/30 by opening size; these are all the small ones. */
	constexpr double VentilatorSashThickness = 2.5;
	constexpr double VentilatorSashFaceWidth = 3.0;

	/** 4 mm, as a ventilator pane or a louvre blade is. */
	constexpr double VentilatorGlassThickness = 0.4;
	constexpr double VentilatorGlassRebate = 0.6;

	/** How far a top-hung sash comes open. Past this the stay fouls the reveal. */
	constexpr double VentilatorOpenAngleDegrees = 30.0;

	/** The pull on the bottom rail, which is how a ventilator this high up is reached at all. */
	constexpr double VentilatorPullProjection = 1.5;
	constexpr double VentilatorPullWidth = 6.0;
	constexpr double VentilatorPullHeight = 1.4;

	/** Below these an opening is too small to divide into sashes and is left as fixed glazing. */
	constexpr double MinSashWidth = 20.0;
	constexpr double MinSashHeight = 25.0;

	/**
	 * True when a sliding window is big enough to be built as a real two-sash unit.
	 *
	 * Shared with the fixed infill rather than checked twice. The two answers have to agree: if the
	 * sashes decline and the frame still leaves out its glazing, the result is a framed hole - which
	 * looks exactly like a correctly generated open window in any still image.
	 */
	bool SlidingWindowHasSashes(const FHFOpening& Opening)
	{
		return Opening.Width - SlidingFrameFace * 2.0 >= MinSashWidth * 2.0
			&& Opening.Height - SlidingFrameFace * 2.0 >= MinSashHeight;
	}

	/** The same question for a ventilator, whose one sash fills the whole clear opening. */
	bool VentilatorHasSash(const FHFOpening& Opening)
	{
		return Opening.Width - VentilatorFrameFace * 2.0 >= MinSashWidth
			&& Opening.Height - VentilatorFrameFace * 2.0 >= MinSashHeight;
	}

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

FDynamicMesh3 FHFGenerators::GenerateDoorLeaf(const FHFOpening& Opening, double SwingSign)
{
	FDynamicMesh3 Mesh;
	FHFMeshOps::InitialiseMesh(Mesh);

	if (Opening.Width <= 0.0 || Opening.Height <= 0.0)
	{
		return Mesh;
	}

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
	const double LeafY = -FMath::Sign(SwingSign) * DoorLeafThickness * 0.5;

	// The half-centimetre inset all round is the gap a real leaf leaves in its frame; without it
	// the leaf shares faces with the reveal and the two z-fight.
	FHFMeshOps::AppendBox(Mesh,
		FVector3d(Opening.Width * 0.5, LeafY, Opening.Height * 0.5),
		FVector3d(Opening.Width * 0.5 - 0.5, DoorLeafThickness * 0.5, Opening.Height * 0.5 - 0.5),
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
	FDynamicMesh3 MakeSlidingPanel(double XMin, double XMax, double TrackY, double Height)
	{
		FDynamicMesh3 Mesh;
		FHFMeshOps::InitialiseMesh(Mesh);

		if (XMax - XMin <= 1.0 || Height <= 1.0)
		{
			return Mesh;
		}

		FHFMeshOps::AppendBox(Mesh,
			FVector3d((XMin + XMax) * 0.5, TrackY, Height * 0.5),
			FVector3d((XMax - XMin) * 0.5, DoorLeafThickness * 0.5, Height * 0.5 - 0.5),
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
		bool bWithHandle)
	{
		FDynamicMesh3 Mesh;
		FHFMeshOps::InitialiseMesh(Mesh);

		const double Width = XMax - XMin;
		const double Height = ZMax - ZMin;
		if (Width <= SashFaceWidth * 2.0 || Height <= SashFaceWidth * 2.0)
		{
			return Mesh;
		}

		auto AppendMember = [&Mesh, TrackY](double MemberXMin, double MemberXMax,
			double MemberZMin, double MemberZMax)
		{
			FHFMeshOps::AppendBox(Mesh,
				FVector3d((MemberXMin + MemberXMax) * 0.5, TrackY, (MemberZMin + MemberZMax) * 0.5),
				FVector3d((MemberXMax - MemberXMin) * 0.5, SashDepth * 0.5, (MemberZMax - MemberZMin) * 0.5),
				0.0, EHFSurfaceRole::WindowFrame);
		};

		AppendMember(XMin, XMin + SashFaceWidth, ZMin, ZMax);            // stile
		AppendMember(XMax - SashFaceWidth, XMax, ZMin, ZMax);            // stile

		// The bottom rail is deeper than the track upstand it comes to rest over, and the two
		// interpenetrate. A real bottom rail is hollow and the upstand runs up inside it, so that is
		// the section rather than a clash - and it is hidden inside the rail either way.
		AppendMember(XMin + SashFaceWidth, XMax - SashFaceWidth, ZMin, ZMin + SashFaceWidth);
		AppendMember(XMin + SashFaceWidth, XMax - SashFaceWidth, ZMax - SashFaceWidth, ZMax);

		// The pane, engaged into the glazing groove of all four members.
		const double GlassInset = SashFaceWidth - SashGlassRebate;
		FHFMeshOps::AppendBox(Mesh,
			FVector3d((XMin + XMax) * 0.5, TrackY, (ZMin + ZMax) * 0.5),
			FVector3d(Width * 0.5 - GlassInset, SashGlassThickness * 0.5, Height * 0.5 - GlassInset),
			0.0, EHFSurfaceRole::Glass);

		if (bWithHandle)
		{
			// On the meeting stile, projecting away from the other sash's track - which is the face
			// a hand can reach and the side the catch is fitted on.
			const double Side = TrackY >= 0.0 ? 1.0 : -1.0;

			FHFMeshOps::AppendBox(Mesh,
				FVector3d(XMax - SashFaceWidth * 0.5,
					TrackY + Side * (SashDepth + SashHandleProjection) * 0.5,
					(ZMin + ZMax) * 0.5),
				FVector3d(SashHandleWidth * 0.5, SashHandleProjection * 0.5, SashHandleHeight * 0.5),
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
	FDynamicMesh3 MakeVentilatorSash(double Width, double Height, double OpenSign)
	{
		FDynamicMesh3 Mesh;
		FHFMeshOps::InitialiseMesh(Mesh);

		if (Width <= VentilatorSashFaceWidth * 2.0 || Height <= VentilatorSashFaceWidth * 2.0)
		{
			return Mesh;
		}

		const double Side = OpenSign >= 0.0 ? 1.0 : -1.0;
		const double SashY = -Side * VentilatorSashThickness * 0.5;

		auto AppendMember = [&Mesh, SashY](double MemberXMin, double MemberXMax,
			double MemberZMin, double MemberZMax)
		{
			FHFMeshOps::AppendBox(Mesh,
				FVector3d((MemberXMin + MemberXMax) * 0.5, SashY, (MemberZMin + MemberZMax) * 0.5),
				FVector3d((MemberXMax - MemberXMin) * 0.5, VentilatorSashThickness * 0.5,
					(MemberZMax - MemberZMin) * 0.5),
				0.0, EHFSurfaceRole::WindowFrame);
		};

		const double F = VentilatorSashFaceWidth;

		AppendMember(0.0, F, -Height, 0.0);              // stile
		AppendMember(Width - F, Width, -Height, 0.0);    // stile
		AppendMember(F, Width - F, -Height, -Height + F);// bottom rail
		AppendMember(F, Width - F, -F, 0.0);             // top rail

		const double GlassInset = F - VentilatorGlassRebate;
		FHFMeshOps::AppendBox(Mesh,
			FVector3d(Width * 0.5, SashY, -Height * 0.5),
			FVector3d(Width * 0.5 - GlassInset, VentilatorGlassThickness * 0.5, Height * 0.5 - GlassInset),
			0.0, EHFSurfaceRole::Glass);

		// The pull, on the face somebody stands in front of - which is the face it opens towards.
		FHFMeshOps::AppendBox(Mesh,
			FVector3d(Width * 0.5, Side * VentilatorPullProjection * 0.5, -Height + F * 0.5),
			FVector3d(VentilatorPullWidth * 0.5, VentilatorPullProjection * 0.5, VentilatorPullHeight * 0.5),
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
		TArray<FHFMeshPart>& OutParts)
	{
		// The clear opening between the outer frame's members - what the sashes actually fill.
		const double ClearWidth = Opening.Width - SlidingFrameFace * 2.0;
		const double Near = SlidingFrameFace;
		const double ZMin = SlidingFrameFace;
		const double ZMax = Opening.Height - SlidingFrameFace;

		const double Half = ClearWidth * 0.5;
		const double HalfOverlap = SashInterlockOverlap * 0.5;
		const double TrackY = SashTrackPitch * 0.5;

		FHFMeshPart Fixed;
		Fixed.PartId = TEXT("SashFixed");
		Fixed.Mesh = MakeSlidingSash(Near + Half - HalfOverlap, Near + ClearWidth, -TrackY, ZMin, ZMax,
			/*bWithHandle*/ false);
		Fixed.PivotTransform = UnitPivot;
		OutParts.Add(MoveTemp(Fixed));

		FHFMeshPart Sash;
		Sash.PartId = TEXT("Sash");
		Sash.Mesh = MakeSlidingSash(Near, Near + Half + HalfOverlap, TrackY, ZMin, ZMax,
			/*bWithHandle*/ true);
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
		TArray<FHFMeshPart>& OutParts)
	{
		const double ClearWidth = Opening.Width - VentilatorFrameFace * 2.0;
		const double ClearHeight = Opening.Height - VentilatorFrameFace * 2.0;

		// Which way it opens. A ventilator carries no swing arc on a plan, so the wall normal is the
		// default and an explicitly outward swing turns it around.
		const double OpenSign =
			(Opening.Swing == EHFSwing::OutwardLeft || Opening.Swing == EHFSwing::OutwardRight) ? -1.0 : 1.0;

		FHFMeshPart Sash;
		Sash.PartId = TEXT("Sash");
		Sash.Mesh = MakeVentilatorSash(ClearWidth, ClearHeight, OpenSign);

		// The hinge line: along the head of the clear opening, starting at the near jamb.
		Sash.PivotTransform =
			FTransform(FVector(VentilatorFrameFace, 0.0, Opening.Height - VentilatorFrameFace)) * UnitPivot;

		Sash.Motion.Type = EHFMotionType::Hinge;
		Sash.Motion.Axis = FVector::XAxisVector;
		Sash.Motion.MaxAngleDegrees = VentilatorOpenAngleDegrees * OpenSign;
		OutParts.Add(MoveTemp(Sash));
	}
}

void FHFGenerators::BuildOpeningParts(const FHFOpening& Opening, const FHFWall& Wall, TArray<FHFMeshPart>& OutParts)
{
	const FWallFrame Frame = MakeWallFrame(Wall.Start, Wall.End);
	if (!Frame.bValid || Opening.Width <= 0.0 || Opening.Height <= 0.0)
	{
		return;
	}

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
			if (SlidingWindowHasSashes(Opening))
			{
				BuildSlidingWindowSashes(Opening, UnitPivot, OutParts);
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

		if (VentilatorHasSash(Opening))
		{
			BuildVentilatorSash(Opening, UnitPivot, OutParts);
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
		const double Half = Opening.Width * 0.5;
		const double TrackY = (DoorLeafThickness + SlidingTrackGap) * 0.5;

		FHFMeshPart Fixed;
		Fixed.PartId = TEXT("PanelFixed");
		Fixed.Mesh = MakeSlidingPanel(Half - SlidingPanelOverlap, Opening.Width - 0.5, -TrackY, Opening.Height);
		Fixed.PivotTransform = Pivot;
		OutParts.Add(MoveTemp(Fixed));

		FHFMeshPart Slider;
		Slider.PartId = TEXT("Leaf");
		Slider.Mesh = MakeSlidingPanel(0.5, Half + SlidingPanelOverlap, TrackY, Opening.Height);
		Slider.PivotTransform = Pivot;
		Slider.Motion.Type = EHFMotionType::Slide;
		Slider.Motion.Axis = FVector::XAxisVector;

		// Far edge to far edge: the panel comes to rest exactly over its fixed partner, so it is
		// still wholly inside the opening at full travel.
		Slider.Motion.MaxTravelCm = FMath::Max(0.0, Half - SlidingPanelOverlap - 0.5);
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
	Part.Mesh = GenerateDoorLeaf(Opening, HingeSign);
	Part.PivotTransform = Pivot;
	Part.Motion.Type = EHFMotionType::Hinge;
	Part.Motion.Axis = FVector::ZAxisVector;
	Part.Motion.MaxAngleDegrees = 90.0 * HingeSign;

	OutParts.Add(MoveTemp(Part));
}

FDynamicMesh3 FHFGenerators::GenerateOpeningInfill(const FHFOpening& Opening, const FHFWall& Wall)
{
	FDynamicMesh3 Mesh = GenerateOpeningFixedInfill(Opening, Wall);

	// Every moving part in its closed pose. Composing the snapshot from the same parts the actor
	// hangs is what guarantees a closed door looks identical to the one-piece infill it replaced.
	TArray<FHFMeshPart> Parts;
	BuildOpeningParts(Opening, Wall, Parts);

	for (const FHFMeshPart& Part : Parts)
	{
		FDynamicMesh3 Posed = Part.Mesh;
		MeshTransforms::ApplyTransform(Posed, FTransformSRT3d(Part.PivotTransform), /*bReverseOrientationIfNeeded*/ true);
		FHFMeshOps::AppendPreservingRoles(Mesh, Posed);
	}

	return Mesh;
}

FDynamicMesh3 FHFGenerators::GenerateOpeningFixedInfill(const FHFOpening& Opening, const FHFWall& Wall)
{
	FDynamicMesh3 Mesh;
	FHFMeshOps::InitialiseMesh(Mesh);

	const FWallFrame Frame = MakeWallFrame(Wall.Start, Wall.End);
	if (!Frame.bValid || Opening.Width <= 0.0 || Opening.Height <= 0.0)
	{
		return Mesh;
	}

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
			(bSliding && SlidingWindowHasSashes(Opening)) || (bVentilator && VentilatorHasSash(Opening));

		const double FaceWidth = bSliding ? SlidingFrameFace : (bVentilator ? VentilatorFrameFace : WindowFrameWidth);
		const double FrameDepth = bSliding ? SlidingFrameDepth : (bVentilator ? VentilatorFrameDepth : WindowFrameDepth);

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
			for (const double Side : { -1.0, 1.0 })
			{
				AppendMember(0.0, Side * SashTrackPitch * 0.5,
					-HalfHeight + FaceWidth + SashTrackUpstand * 0.5,
					HalfWidth - FaceWidth, SashTrackWidth * 0.5, SashTrackUpstand * 0.5,
					EHFSurfaceRole::MetalHardware);
			}
		}

		if (!bSashesCarryTheGlass)
		{
			// A central mullion, once the opening is wide enough to need one. A sliding unit never
			// gets one: its meeting stiles ARE the mullion, and a second one down the middle would
			// sit in the running sash's way.
			if (Opening.Width > 120.0 && !bSliding)
			{
				AppendFrameMember(0.0, 0.0, FaceWidth * 0.5, HalfHeight - FaceWidth);
			}

			AppendMember(0.0, 0.0, 0.0, HalfWidth - FaceWidth, GlassThickness * 0.5,
				HalfHeight - FaceWidth, EHFSurfaceRole::Glass);
		}
	}

	FHFMeshOps::ApplyWorldScaleUVs(Mesh);
	return Mesh;
}
