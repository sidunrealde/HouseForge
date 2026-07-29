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
}

void FHFGenerators::BuildOpeningParts(const FHFOpening& Opening, const FHFWall& Wall, TArray<FHFMeshPart>& OutParts)
{
	const FWallFrame Frame = MakeWallFrame(Wall.Start, Wall.End);
	if (!Frame.bValid || Opening.Width <= 0.0 || Opening.Height <= 0.0)
	{
		return;
	}

	const bool bIsDoor = Opening.Kind == EHFOpeningKind::Door || Opening.Kind == EHFOpeningKind::SlidingDoor;
	if (!bIsDoor)
	{
		// Window sashes are still fixed. They articulate in the retrofit that follows the joinery
		// kit; claiming they move before they do would be worse than saying they do not.
		//
		// Said out loud, though, because "does not move yet" and "has nothing to move" are
		// indistinguishable from the outside: AHFOpeningActor reports zero parts without complaint,
		// GenerateOpeningFixedInfill composes a perfectly correct closed pose, and a closed sash is
		// identical to a fixed pane in any still image. A SlidingWindow is the standard window of
		// the flats this plugin exists for and a Ventilator is a louvre or a pivot sash; both come
		// out here as fixed glazing, and nothing in the build says so.
		if (Opening.Kind == EHFOpeningKind::SlidingWindow || Opening.Kind == EHFOpeningKind::Ventilator)
		{
			UE_LOG(LogHouseForge, Warning,
				TEXT("Opening '%s' is a %s, which is generated as FIXED glazing: its sash does not move yet."),
				*Opening.Id.ToString(),
				Opening.Kind == EHFOpeningKind::SlidingWindow ? TEXT("sliding window") : TEXT("ventilator"));
		}
		return;
	}

	const FVector2D Centre = Wall.Start + Frame.Direction * Opening.OffsetAlongWall;
	const double SillZ = Wall.BaseZ + Opening.SillHeight;
	const double HalfWidth = Opening.Width * 0.5;

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
		// Window: a frame around the reveal, with glazing inside it.
		const double HalfWidth = Opening.Width * 0.5;
		const double HalfHeight = Opening.Height * 0.5;

		auto AppendFrameMember = [&](double OffsetAlong, double OffsetUp, double HalfAlong, double HalfUp)
		{
			const FVector2D MemberPlan = Plan + Frame.Direction * OffsetAlong;
			FHFMeshOps::AppendBox(Mesh,
				FVector3d(MemberPlan.X, MemberPlan.Y, CentreZ + OffsetUp),
				FVector3d(HalfAlong, WindowFrameDepth * 0.5, HalfUp),
				Frame.YawDegrees, EHFSurfaceRole::WindowFrame);
		};

		AppendFrameMember(0.0, HalfHeight - WindowFrameWidth * 0.5, HalfWidth, WindowFrameWidth * 0.5);   // head
		AppendFrameMember(0.0, -HalfHeight + WindowFrameWidth * 0.5, HalfWidth, WindowFrameWidth * 0.5);  // sill
		AppendFrameMember(-HalfWidth + WindowFrameWidth * 0.5, 0.0, WindowFrameWidth * 0.5, HalfHeight);  // jamb
		AppendFrameMember(HalfWidth - WindowFrameWidth * 0.5, 0.0, WindowFrameWidth * 0.5, HalfHeight);   // jamb

		// A central mullion, once the opening is wide enough to need one.
		if (Opening.Width > 120.0)
		{
			AppendFrameMember(0.0, 0.0, WindowFrameWidth * 0.5, HalfHeight - WindowFrameWidth);
		}

		FHFMeshOps::AppendBox(Mesh,
			FVector3d(Plan.X, Plan.Y, CentreZ),
			FVector3d(HalfWidth - WindowFrameWidth, GlassThickness * 0.5, HalfHeight - WindowFrameWidth),
			Frame.YawDegrees, EHFSurfaceRole::Glass);
	}

	FHFMeshOps::ApplyWorldScaleUVs(Mesh);
	return Mesh;
}
