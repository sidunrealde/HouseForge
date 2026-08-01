// Copyright Siddartha G. All Rights Reserved.

#include "Actors/HFHouseActor.h"

#include "Actors/HFArticulatedActor.h"
#include "Actors/HFElementActors.h"
#include "Actors/HFOpeningActor.h"
#include "Actors/HFFanActor.h"
#include "Actors/HFWardrobeActor.h"
#include "Components/LineBatchComponent.h"
#include "Engine/World.h"
#include "Geometry/HFGenerators.h"
#include "HouseForge.h"
#include "Model/HFBuildDefaults.h"
#include "Model/HFCeilingTemplates.h"

namespace
{
	// Preview palette. Roles are colour-coded so a top-down screenshot can be compared against the
	// source drawing without needing labels.
	const FLinearColor ColourWall(0.10f, 0.10f, 0.12f);
	const FLinearColor ColourOpening(0.95f, 0.65f, 0.10f);
	const FLinearColor ColourSwing(0.95f, 0.35f, 0.05f);
	const FLinearColor ColourRoom(0.20f, 0.55f, 0.85f);
	const FLinearColor ColourBeam(0.85f, 0.25f, 0.25f);
	const FLinearColor ColourColumn(0.55f, 0.20f, 0.55f);
	const FLinearColor ColourFixture(0.25f, 0.70f, 0.35f);
	const FLinearColor ColourCeiling(0.70f, 0.35f, 0.75f);

	constexpr uint8 DepthPriority = SDPG_World;

	FVector2D RotateAbout(const FVector2D& Point, const FVector2D& Centre, double Degrees)
	{
		const double Radians = FMath::DegreesToRadians(Degrees);
		const double C = FMath::Cos(Radians);
		const double S = FMath::Sin(Radians);
		const FVector2D D = Point - Centre;
		return Centre + FVector2D(D.X * C - D.Y * S, D.X * S + D.Y * C);
	}

	/** Plan footprint of a wall: centreline expanded by half its thickness on each side. */
	TArray<FVector2D> WallFootprint(const FHFWall& Wall)
	{
		TArray<FVector2D> Out;
		const double Length = Wall.Length();
		if (Length <= UE_KINDA_SMALL_NUMBER)
		{
			return Out;
		}

		const FVector2D Direction = (Wall.End - Wall.Start) / Length;
		const FVector2D Normal(-Direction.Y, Direction.X);
		const double Half = Wall.Thickness * 0.5;

		Out.Add(Wall.Start + Normal * Half);
		Out.Add(Wall.End + Normal * Half);
		Out.Add(Wall.End - Normal * Half);
		Out.Add(Wall.Start - Normal * Half);
		return Out;
	}

	TArray<FVector2D> RectFootprint(const FVector2D& Centre, const FVector2D& Size, double RotationDegrees)
	{
		const FVector2D Half = Size * 0.5;
		TArray<FVector2D> Out;
		Out.Add(RotateAbout(Centre + FVector2D(-Half.X, -Half.Y), Centre, RotationDegrees));
		Out.Add(RotateAbout(Centre + FVector2D( Half.X, -Half.Y), Centre, RotationDegrees));
		Out.Add(RotateAbout(Centre + FVector2D( Half.X,  Half.Y), Centre, RotationDegrees));
		Out.Add(RotateAbout(Centre + FVector2D(-Half.X,  Half.Y), Centre, RotationDegrees));
		return Out;
	}

	// ------------------------------------------------------------------- the build order of a frame
	//
	// A house is not a bag of solids. Concrete is cast before masonry is laid, columns before the
	// beams that frame into them, and where two members want the same volume exactly one of them
	// gets it. Modelling every member as if it were alone gave two of them the same faces to draw,
	// and the whole flat flashed - see FHFStructuralCut.
	//
	// So the composing layer, which is the only thing that can see more than one element, works out
	// what displaces what and hands each member the list. The precedence is the build sequence:
	//
	//   1. Columns - cast first, full height, displaced by nothing.
	//   2. Beams   - frame into every column they land on. Where two cross, the LONGER one runs
	//                through and the shorter stops at its face, which is what primary and secondary
	//                beams are. Equal lengths break on id so a rebuild is deterministic.
	//   3. Walls   - blockwork infills around all of it, and around each other. Walls are set out on
	//                their CENTRELINES, so wherever two meet, both own the footprint of the junction.
	//                On site one run is built through and the other butts to its face: the THICKER
	//                wall runs, a 230 external through a 115 partition, and at equal thickness the
	//                longer one does. Without it a balcony parapet buried its last 115 in the main
	//                wall and the two undersides fought - which is the one defect the flat had left
	//                once the frame was right, and it is on show from outside the building.

	/** World-space bounds of an oriented structural volume. */
	FBox BoundsOf(const FHFStructuralCut& Cut)
	{
		if (!Cut.IsValid())
		{
			return FBox(ForceInit);
		}

		FBox Bounds(ForceInit);
		const FVector2D Centre(Cut.Centre.X, Cut.Centre.Y);

		for (const FVector2D& Corner : RectFootprint(Centre,
			FVector2D(Cut.Extents.X * 2.0, Cut.Extents.Y * 2.0), Cut.YawDegrees))
		{
			Bounds += FVector(Corner.X, Corner.Y, Cut.BottomZ());
			Bounds += FVector(Corner.X, Corner.Y, Cut.TopZ());
		}

		return Bounds;
	}

	FBox BoundsOf(const FHFWall& Wall)
	{
		FBox Bounds(ForceInit);
		for (const FVector2D& Corner : WallFootprint(Wall))
		{
			Bounds += FVector(Corner.X, Corner.Y, Wall.BaseZ);
			Bounds += FVector(Corner.X, Corner.Y, Wall.BaseZ + Wall.Height);
		}
		return Bounds;
	}

	/**
	 * True when two members want the same material, rather than merely meeting.
	 *
	 * Shrunk before the test on purpose. Two members that touch face to face are a butt joint - the
	 * everyday case, every wall against every other wall - and cutting one with the other there
	 * would run a mesh boolean per junction to remove exactly nothing.
	 */
	bool VolumesOverlap(const FBox& A, const FBox& B)
	{
		constexpr double Touching = 0.01;
		return A.IsValid && B.IsValid && A.ExpandBy(-Touching).Intersect(B.ExpandBy(-Touching));
	}

	/** True when the first beam is the one that runs through at a crossing. */
	bool BeamRunsThrough(const FHFBeam& Candidate, const FHFBeam& Other)
	{
		const double A = Candidate.Length();
		const double B = Other.Length();
		if (!FMath::IsNearlyEqual(A, B, 0.1))
		{
			return A > B;
		}
		return Candidate.Id.LexicalLess(Other.Id);
	}

	/**
	 * How far the finished wall face stands in from each edge of a room boundary.
	 *
	 * A ROOM BOUNDARY IS A CENTRELINE. Every room in this model is set out on the middle of the
	 * walls round it, so the plaster a skirting is fixed to is half a wall's thickness inside the
	 * polygon - 11.5 on a 230, 5.75 on a 115, and both on the same room. A skirting laid on the
	 * boundary itself is laid down the middle of the masonry, which is exactly where all seven of
	 * this flat's skirtings were: declared, generated, watertight, correctly tagged, and invisible.
	 *
	 * Worked out here because a generator may not go looking for the walls - see
	 * .claude/rules/04-conventions.md. Per edge rather than per room, because the thickness changes
	 * from one side of a room to the other.
	 */
	TArray<double> WallFaceInsetsFor(const FHFRoom& Room, const TArray<FHFWall>& Walls)
	{
		TArray<double> Insets;
		const int32 Count = Room.Boundary.Num();
		Insets.SetNumZeroed(Count);

		if (Count < 3)
		{
			return Insets;
		}

		// A wall counts for an edge when it is set out ON that edge: parallel to it, its centreline
		// in the same line, and overlapping it along its length. Half a centimetre of slack, which
		// is far below any thickness in this domain and far above the arithmetic.
		constexpr double OnTheLine = 0.5;

		for (int32 i = 0; i < Count; ++i)
		{
			const FVector2D& A = Room.Boundary[i];
			const FVector2D& B = Room.Boundary[(i + 1) % Count];

			const double EdgeLength = FVector2D::Distance(A, B);
			if (EdgeLength <= UE_KINDA_SMALL_NUMBER)
			{
				continue;
			}

			const FVector2D Direction = (B - A) / EdgeLength;
			const FVector2D Normal(-Direction.Y, Direction.X);

			for (const FHFWall& Wall : Walls)
			{
				const double WallLength = Wall.Length();
				if (WallLength <= UE_KINDA_SMALL_NUMBER)
				{
					continue;
				}

				const FVector2D WallDirection = (Wall.End - Wall.Start) / WallLength;
				if (FMath::Abs(FVector2D::DotProduct(WallDirection, Direction)) < 0.999)
				{
					continue;
				}

				if (FMath::Abs(FVector2D::DotProduct(Wall.Start - A, Normal)) > OnTheLine)
				{
					continue;
				}

				const double T0 = FVector2D::DotProduct(Wall.Start - A, Direction);
				const double T1 = FVector2D::DotProduct(Wall.End - A, Direction);

				if (FMath::Max(T0, T1) <= OnTheLine || FMath::Min(T0, T1) >= EdgeLength - OnTheLine)
				{
					continue;
				}

				// The thickest, because where a 230 and a 115 both run along one edge the skirting
				// has to clear the one that stands furthest into the room.
				Insets[i] = FMath::Max(Insets[i], Wall.Thickness * 0.5);
			}
		}

		return Insets;
	}

	/** True when the first wall is the one built through at a junction, and the other butts to it. */
	bool WallRunsThrough(const FHFWall& Candidate, const FHFWall& Other)
	{
		if (!FMath::IsNearlyEqual(Candidate.Thickness, Other.Thickness, 0.1))
		{
			return Candidate.Thickness > Other.Thickness;
		}

		const double A = Candidate.Length();
		const double B = Other.Length();
		if (!FMath::IsNearlyEqual(A, B, 0.1))
		{
			return A > B;
		}

		// Two identical walls meeting is not a real building, but a rebuild still has to make the
		// same choice twice or the flat changes shape between runs.
		return Candidate.Id.LexicalLess(Other.Id);
	}
}

AHFHouseActor::AHFHouseActor()
{
	PrimaryActorTick.bCanEverTick = false;

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	Lines = CreateDefaultSubobject<ULineBatchComponent>(TEXT("Preview"));
	Lines->SetupAttachment(Root);
	// Lines must persist rather than expire on a timer: this is a static preview of a saved level,
	// not transient debug output.
	Lines->DefaultLifeTime = 0.0f;
	Lines->bCalculateAccurateBounds = true;
}

void AHFHouseActor::SetSpec(const FHFHouseSpec& InSpec)
{
	Spec = InSpec;

	// One conversion, here. Everything downstream - preview, tools, geometry - works in
	// centimetres and never has to ask what units a spec arrived in.
	FHFUnits::ConvertToCentimeters(Spec);

	// AND ONE TEMPLATE RESOLUTION, here, for the same reason. A ceiling that names a design carries
	// no figures until this runs, and after it the spec holds plain numbers that the preview, the
	// tools and the geometry all read the same way. Re-applied rather than trusted from the incoming
	// spec, because the design belongs to the project: a drawing says "cove", and how deep a cove is
	// in this project is what the settings page is for.
	//
	// Idempotent, so a spec that arrived already resolved is unchanged, and Custom ceilings are left
	// exactly as authored.
	FHFCeilingTemplates::Apply(Spec, FHFBuildDefaults::FromProjectSettings().Ceiling);

	if (!Spec.SourceDrawing.IsEmpty())
	{
		SourceDrawing = Spec.SourceDrawing;
	}

	Rebuild();
	BuildGeometry();
}

void AHFHouseActor::Destroyed()
{
	ClearGeometry();
	Super::Destroyed();
}

void AHFHouseActor::ClearGeometry()
{
	for (AActor* Element : ElementActors)
	{
		if (IsValid(Element))
		{
			Element->Destroy();
		}
	}
	ElementActors.Reset();
}

void AHFHouseActor::BuildGeometry()
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	// Hand-edited elements survive a rebuild. Destroying and respawning everything would throw
	// away modelling work without warning, which is exactly the failure the per-element edit flag
	// exists to prevent - it would be pointless if the house-level rebuild ignored it.
	TMap<TPair<UClass*, FName>, AHFElementActor*> Preserved;
	TArray<TObjectPtr<AActor>> Survivors;

	// Open amounts are user state, exactly as a hand edit is. The elements themselves are respawned
	// here, so a pose held only on the actor would die with it and every door in the flat would slam
	// shut on a rebuild. Poses are carried across by element id and put back once the parts exist.
	TMap<TPair<UClass*, FName>, FHFPartPoses> PosedElements;

	for (AActor* Element : ElementActors)
	{
		const AHFArticulatedActor* Articulated = Cast<AHFArticulatedActor>(Element);
		if (IsValid(Articulated))
		{
			FHFPartPoses Poses = Articulated->CapturePartPoses();
			if (!Poses.IsEmpty())
			{
				PosedElements.Add({ Articulated->GetClass(), Articulated->ElementId }, MoveTemp(Poses));
			}
		}

		AHFElementActor* Typed = Cast<AHFElementActor>(Element);
		if (IsValid(Typed) && Typed->ShouldPreserveOnRebuild())
		{
			Preserved.Add({ Typed->GetClass(), Typed->ElementId }, Typed);
			Survivors.Add(Typed);
		}
		else if (IsValid(Element))
		{
			Element->Destroy();
		}
	}

	ElementActors = MoveTemp(Survivors);
	const int32 PreservedCount = ElementActors.Num();

	FActorSpawnParameters Params;
	Params.Owner = this;

	// Returns null when an element was preserved, which tells the caller to leave it alone.
	auto Spawn = [&](UClass* Class, const FName& Id, const FString& Label) -> AActor*
	{
		if (Preserved.Contains({ Class, Id }))
		{
			return nullptr;
		}

		AActor* Actor = World->SpawnActor<AActor>(Class, FTransform::Identity, Params);
		if (Actor != nullptr)
		{
#if WITH_EDITOR
			Actor->SetActorLabel(Label);
#endif
			if (AHFElementActor* Typed = Cast<AHFElementActor>(Actor))
			{
				Typed->ElementId = Id;
			}
			Actor->AttachToActor(this, FAttachmentTransformRules::KeepWorldTransform);
			ElementActors.Add(Actor);
		}
		return Actor;
	};

	// ------------------------------------------------------------------ resolve the frame, once
	//
	// Worked out here, before anything is generated, because only the composing layer can see more
	// than one element and a generator may not go looking for the rest of the house. See the note
	// on build order above, and .claude/rules/04-conventions.md.
	TArray<FHFStructuralCut> ColumnCuts;
	TArray<FBox> ColumnBounds;
	ColumnCuts.Reserve(Spec.Columns.Num());
	ColumnBounds.Reserve(Spec.Columns.Num());

	for (const FHFColumn& Column : Spec.Columns)
	{
		const FHFStructuralCut Cut = FHFGenerators::StructuralCutFor(Column);
		if (Cut.IsValid())
		{
			ColumnCuts.Add(Cut);
			ColumnBounds.Add(BoundsOf(Cut));
		}
	}

	TArray<FHFStructuralCut> BeamCuts;
	TArray<FBox> BeamBounds;
	BeamCuts.Reserve(Spec.Beams.Num());
	BeamBounds.Reserve(Spec.Beams.Num());

	for (const FHFBeam& Beam : Spec.Beams)
	{
		BeamCuts.Add(FHFGenerators::StructuralCutFor(Beam));
		BeamBounds.Add(BoundsOf(BeamCuts.Last()));
	}

	// What each beam is cut by: every column it lands on, and every beam that runs through it.
	TArray<TArray<FHFStructuralCut>> BeamStructure;
	BeamStructure.SetNum(Spec.Beams.Num());

	for (int32 Index = 0; Index < Spec.Beams.Num(); ++Index)
	{
		if (!BeamCuts[Index].IsValid())
		{
			continue;
		}

		for (int32 Column = 0; Column < ColumnCuts.Num(); ++Column)
		{
			if (VolumesOverlap(BeamBounds[Index], ColumnBounds[Column]))
			{
				BeamStructure[Index].Add(ColumnCuts[Column]);
			}
		}

		for (int32 Other = 0; Other < Spec.Beams.Num(); ++Other)
		{
			if (Other != Index && BeamCuts[Other].IsValid()
				&& VolumesOverlap(BeamBounds[Index], BeamBounds[Other])
				&& BeamRunsThrough(Spec.Beams[Other], Spec.Beams[Index]))
			{
				BeamStructure[Index].Add(BeamCuts[Other]);
			}
		}
	}

	TArray<FHFStructuralCut> WallCuts;
	TArray<FBox> WallBounds;
	WallCuts.Reserve(Spec.Walls.Num());
	WallBounds.Reserve(Spec.Walls.Num());

	for (const FHFWall& Wall : Spec.Walls)
	{
		WallCuts.Add(FHFGenerators::StructuralCutFor(Wall));
		WallBounds.Add(BoundsOf(WallCuts.Last()));
	}

	// Masonry is displaced by all of it, including by the masonry that outranks it.
	auto StructureInWall = [&Spec = Spec, &ColumnCuts, &ColumnBounds, &BeamCuts, &BeamBounds,
		&WallCuts, &WallBounds](const FHFWall& Wall)
	{
		const FBox Bounds = BoundsOf(Wall);
		TArray<FHFStructuralCut> Cuts;

		for (int32 Index = 0; Index < WallCuts.Num(); ++Index)
		{
			if (Spec.Walls[Index].Id != Wall.Id && WallCuts[Index].IsValid()
				&& VolumesOverlap(Bounds, WallBounds[Index])
				&& WallRunsThrough(Spec.Walls[Index], Wall))
			{
				Cuts.Add(WallCuts[Index]);
			}
		}

		for (int32 Index = 0; Index < ColumnCuts.Num(); ++Index)
		{
			if (VolumesOverlap(Bounds, ColumnBounds[Index]))
			{
				Cuts.Add(ColumnCuts[Index]);
			}
		}

		for (int32 Index = 0; Index < BeamCuts.Num(); ++Index)
		{
			if (BeamCuts[Index].IsValid() && VolumesOverlap(Bounds, BeamBounds[Index]))
			{
				Cuts.Add(BeamCuts[Index]);
			}
		}

		return Cuts;
	};

	// Walls carry their own openings, so each wall owns everything it needs to rebuild itself
	// when its thickness or height is edited.
	for (const FHFWall& Wall : Spec.Walls)
	{
		AHFWallActor* WallActor = Cast<AHFWallActor>(Spawn(AHFWallActor::StaticClass(), Wall.Id,
			FString::Printf(TEXT("Wall_%s"), *Wall.Id.ToString())));
		if (WallActor == nullptr)
		{
			continue;
		}

		WallActor->Wall = Wall;
		WallActor->Structure = StructureInWall(Wall);

		for (const FHFOpening& Opening : Spec.Openings)
		{
			if (Opening.WallId == Wall.Id)
			{
				WallActor->Openings.Add(Opening);
			}
		}

		// AN EXTRACT HAS TO BLOW THROUGH THE WALL IT IS SCREWED TO. The fan's case carries an
		// aperture and its blades turn inside it, and none of that is worth anything while the
		// masonry behind is solid - which it was for all three extracts in the flat. Invisible from
		// the room, because the case covers precisely the spot where the hole is not.
		//
		// Derived from the fan rather than asked of the drawing, and added to the WALL's openings
		// only - never to the spec's - so the hole is cut but no ventilator sash is built in it. See
		// AHFFanActor::DuctOpeningFor.
		for (const FHFFixture& Fixture : Spec.Fixtures)
		{
			if (Fixture.Type == EHFFixtureType::ExhaustFan && Fixture.AnchorWallId == Wall.Id)
			{
				// The ROOM as well as the wall: a fixture's BaseZ is measured above the room floor
				// and an opening's sill above the wall's base, and the hole has to land on the fan's
				// own centre rather than on whichever of the two datums happened to be handy.
				WallActor->Openings.Add(
					AHFFanActor::DuctOpeningFor(Fixture, Wall, Spec.FindRoom(Fixture.RoomId)));
			}
		}

		WallActor->Regenerate();
	}

	// Doorway positions per room, so skirting stops at each opening instead of running across it.
	for (const FHFRoom& Room : Spec.Rooms)
	{
		AHFRoomActor* RoomActor = Cast<AHFRoomActor>(Spawn(AHFRoomActor::StaticClass(), Room.Id,
			FString::Printf(TEXT("Room_%s"), *Room.Id.ToString())));
		if (RoomActor == nullptr)
		{
			continue;
		}

		RoomActor->Room = Room;
		RoomActor->SlabThickness = SlabThickness;
		RoomActor->WallFaceInsets = WallFaceInsetsFor(Room, Spec.Walls);

		double WidestDoor = 100.0;
		for (const FHFOpening& Opening : Spec.Openings)
		{
			const bool bIsDoorway =
				Opening.Kind == EHFOpeningKind::Door ||
				Opening.Kind == EHFOpeningKind::SlidingDoor ||
				Opening.Kind == EHFOpeningKind::Archway;

			if (!bIsDoorway || Opening.SillHeight > 1.0)
			{
				continue;
			}

			if (const FHFWall* Wall = Spec.FindWall(Opening.WallId))
			{
				RoomActor->DoorwayCentres.Add(FHFGenerators::OpeningCentre(Opening, *Wall));
				WidestDoor = FMath::Max(WidestDoor, Opening.Width);
			}
		}
		RoomActor->DoorwayWidth = WidestDoor;
		RoomActor->Regenerate();
	}

	// False ceilings, with the fan positions they have to be cut for.
	for (const FHFFalseCeiling& Ceiling : Spec.FalseCeilings)
	{
		const FHFRoom* Room = Spec.FindRoom(Ceiling.RoomId);
		if (Room == nullptr || Ceiling.Style == EHFCeilingStyle::None)
		{
			continue;
		}

		AHFCeilingActor* CeilingActor = Cast<AHFCeilingActor>(Spawn(AHFCeilingActor::StaticClass(), Ceiling.Id,
			FString::Printf(TEXT("Ceiling_%s"), *Ceiling.Id.ToString())));
		if (CeilingActor == nullptr)
		{
			continue;
		}

		CeilingActor->Ceiling = Ceiling;
		CeilingActor->Room = *Room;

		// The beams this ceiling has to bury, carried onto the actor so a later settings change can
		// re-derive the perimeter ring instead of quietly dropping it. Deliberately every beam that
		// SHOWS rather than every beam in the room: a 230 beam flush in the 230 wall under it is
		// invisible and needs no ring, and that distinction is the whole reason the uniform 500 drop
		// was wrong.
		if (const FHFBeam* Showing = Spec.DeepestBeamOverRoom(Ceiling.RoomId))
		{
			CeilingActor->BeamsShowingInRoom.Add(*Showing);
		}

		// THE HOLE IS A CONSEQUENCE OF THE FAN, exactly as an extract's duct is. It was a fixed 8 -
		// a 16 cm square opening for a 2.2 cm rod - whose corners showed past the 15 cm motor
		// housing as four bright wedges from below. Sized from the fan's own rod now, and the
		// canopy is sized to cover it, so the two cannot drift.
		double HoleHalfSide = 0.0;

		for (const FHFFixture& Fixture : Spec.Fixtures)
		{
			if (Fixture.Type == EHFFixtureType::CeilingFan && Fixture.RoomId == Ceiling.RoomId)
			{
				CeilingActor->FanDrops.Add(Fixture.Position);

				// The largest, because one radius cuts every hole in this ceiling and a hole too
				// small for a rod is a rod through plasterboard.
				HoleHalfSide = FMath::Max(HoleHalfSide,
					AHFFanActor::ParamsFor(Fixture).RodHoleHalfSide());
			}
		}

		if (HoleHalfSide > 0.0)
		{
			CeilingActor->FanDropRadius = HoleHalfSide;
		}

		CeilingActor->Regenerate();
	}

	for (int32 Index = 0; Index < Spec.Beams.Num(); ++Index)
	{
		const FHFBeam& Beam = Spec.Beams[Index];
		if (AHFBeamActor* BeamActor = Cast<AHFBeamActor>(Spawn(AHFBeamActor::StaticClass(), Beam.Id,
			FString::Printf(TEXT("Beam_%s"), *Beam.Id.ToString()))))
		{
			BeamActor->Beam = Beam;
			BeamActor->Structure = BeamStructure[Index];
			BeamActor->Regenerate();
		}
	}

	for (const FHFColumn& Column : Spec.Columns)
	{
		if (AHFColumnActor* ColumnActor = Cast<AHFColumnActor>(Spawn(AHFColumnActor::StaticClass(), Column.Id,
			FString::Printf(TEXT("Column_%s"), *Column.Id.ToString()))))
		{
			ColumnActor->Column = Column;
			ColumnActor->Regenerate();
		}
	}

	for (const FHFOpening& Opening : Spec.Openings)
	{
		const FHFWall* Wall = Spec.FindWall(Opening.WallId);
		if (Wall == nullptr || Opening.Kind == EHFOpeningKind::Archway)
		{
			continue;
		}

		if (AHFOpeningActor* OpeningActor = Cast<AHFOpeningActor>(Spawn(AHFOpeningActor::StaticClass(), Opening.Id,
			FString::Printf(TEXT("Opening_%s"), *Opening.Id.ToString()))))
		{
			OpeningActor->Opening = Opening;
			OpeningActor->HostWall = *Wall;

			// Settings resolve HERE, in the composing layer, and never inside a generator - see
			// .claude/rules/04-conventions.md. Before Regenerate, so the first mesh this actor ever
			// builds already has the project's figures on it.
			//
			// Only on a freshly spawned actor: Spawn returns null for a preserved one, so an opening
			// somebody has edited by hand keeps the figures it was built with rather than having a
			// project-wide setting reach in and change it.
			OpeningActor->ApplyProjectDefaults();
			OpeningActor->Regenerate();
		}
	}

	// Joinery. One fixture type so far - a wardrobe - and it is the first thing in the flat built out
	// of FHFJoineryKit rather than out of a bespoke generator. The rest of the catalogue composes from
	// the same kit and lands with milestone 9.
	for (const FHFFixture& Fixture : Spec.Fixtures)
	{
		if (Fixture.Type != EHFFixtureType::Wardrobe)
		{
			continue;
		}

		AHFWardrobeActor* WardrobeActor = Cast<AHFWardrobeActor>(
			Spawn(AHFWardrobeActor::StaticClass(), Fixture.Id,
				FString::Printf(TEXT("Wardrobe_%s"), *Fixture.Id.ToString())));

		if (WardrobeActor == nullptr)
		{
			continue;
		}

		// Settings first, then the drawing: ApplyFixture reads the project's module width and plinth
		// height to fill in what a drawing did not state, so the order is load-bearing.
		//
		// Only on a freshly spawned actor - Spawn returns null for a preserved one - so a wardrobe
		// somebody has modelled on keeps the figures it was built with rather than having a
		// project-wide setting reach in and change it.
		WardrobeActor->ApplyProjectDefaults();
		WardrobeActor->ApplyFixture(Fixture);

		const FHFRoom* Room = Spec.FindRoom(Fixture.RoomId);
		WardrobeActor->SetActorTransform(AHFWardrobeActor::PlacementFor(Fixture,
			Room != nullptr ? Room->FloorZ : 0.0,
			Spec.FindWall(Fixture.AnchorWallId)));

		WardrobeActor->Regenerate();
	}

	// Fans. The one thing in the flat that revolves rather than opens, and until this loop existed
	// the only production consumer of EHFMotionType::Spin was nothing at all: the mechanism was
	// complete and tested, CeilingFan was read here solely to punch a rod hole in the false ceiling
	// above a fan that did not exist, and ExhaustFan was not read anywhere.
	for (const FHFFixture& Fixture : Spec.Fixtures)
	{
		if (Fixture.Type != EHFFixtureType::CeilingFan && Fixture.Type != EHFFixtureType::ExhaustFan)
		{
			continue;
		}

		AHFFanActor* FanActor = Cast<AHFFanActor>(
			Spawn(AHFFanActor::StaticClass(), Fixture.Id,
				FString::Printf(TEXT("Fan_%s"), *Fixture.Id.ToString())));

		if (FanActor == nullptr)
		{
			continue;
		}

		// Settings first, then the drawing, exactly as a wardrobe: ApplyProjectDefaults picks the
		// catalogue for the kind and ApplyFixture puts the drawn dimensions over it, so the order is
		// load-bearing. Only on a freshly spawned actor - Spawn returns null for a preserved one.
		FanActor->ApplyProjectDefaults(
			Fixture.Type == EHFFixtureType::ExhaustFan ? EHFFanKind::Exhaust : EHFFanKind::Ceiling);
		FanActor->ApplyFixture(Fixture);

		const FHFRoom* FanRoom = Spec.FindRoom(Fixture.RoomId);
		const FHFWall* FanWall = Spec.FindWall(Fixture.AnchorWallId);

		// AN EXTRACT HAS A FAR SIDE. The duct is cored through the masonry and, with nothing on the
		// discharge face, left as a bare square opening in a finished wall - the only opening in the
		// flat with no lining, since it is deliberately kept out of Spec.Openings so no ventilator
		// sash is built in it. The sleeve and cowl belong to the fan, so the wall's thickness comes
		// to the fan as a dimension: a generator may not reach for the wall it stands in.
		if (Fixture.Type == EHFFixtureType::ExhaustFan && FanWall != nullptr)
		{
			FanActor->Fan.HostWallThickness = FanWall->Thickness;
		}

		// AND THEN WHAT IS BETWEEN THE FAN AND THE ROOM. A ceiling fan hangs from the structural
		// slab, so a false ceiling over it is something the rod has to get through - and a rod that
		// was a fixed project figure built the whole rotor inside the plasterboard of any room with
		// a full drop. Every ceiling fan in the reference flat sits in the open centre of a cove or
		// peripheral ceiling, where the drop is zero and nothing showed.
		//
		// The drop is asked of the room, per fan, because the answer depends on WHERE in the room
		// the fan is: the same ceiling covers its band and leaves its centre open.
		if (Fixture.Type == EHFFixtureType::CeilingFan && FanRoom != nullptr)
		{
			double SoffitDrop = 0.0;

			for (const FHFFalseCeiling& Ceiling : Spec.FalseCeilings)
			{
				if (Ceiling.RoomId == Fixture.RoomId)
				{
					SoffitDrop = FMath::Max(SoffitDrop,
						FHFGenerators::CeilingSoffitDropAt(Ceiling, *FanRoom, Fixture.Position));
				}
			}

			FanActor->ApplyCeilingAbove(SoffitDrop);
		}

		FanActor->SetActorTransform(AHFFanActor::PlacementFor(Fixture, FanRoom, FanWall));

		FanActor->Regenerate();
	}

	// Once every element has been regenerated its parts exist again, so the poses captured above can
	// go back on. Done in one pass at the end rather than per element type, so any future articulated
	// element gets it without having to remember to ask.
	for (AActor* Element : ElementActors)
	{
		AHFArticulatedActor* Articulated = Cast<AHFArticulatedActor>(Element);
		if (Articulated == nullptr)
		{
			continue;
		}

		if (const FHFPartPoses* Poses = PosedElements.Find({ Articulated->GetClass(), Articulated->ElementId }))
		{
			Articulated->RestorePartPoses(*Poses);
		}
	}

	UE_LOG(LogHouseForge, Log,
		TEXT("HouseForge built '%s': %d element actors, %d preserved as hand-edited."),
		*Spec.Name, ElementActors.Num(), PreservedCount);
}

void AHFHouseActor::PostLoad()
{
	Super::PostLoad();
	Rebuild();
}

void AHFHouseActor::PostRegisterAllComponents()
{
	Super::PostRegisterAllComponents();
	Rebuild();
}

#if WITH_EDITOR
void AHFHouseActor::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	Rebuild();
}
#endif

void AHFHouseActor::Rebuild()
{
	if (Lines == nullptr)
	{
		return;
	}

	Lines->Flush();

	if (!bShowPreview)
	{
		Lines->MarkRenderStateDirty();
		return;
	}

	DrawWalls();

	if (bShowOpenings)   { DrawOpenings(); }
	if (bShowRooms)      { DrawRooms(); }
	if (bShowStructure)  { DrawStructure(); }
	if (bShowFixtures)   { DrawFixtures(); }
	if (bShowCeilings)   { DrawCeilings(); }

	Lines->MarkRenderStateDirty();

	UE_LOG(LogHouseForge, Verbose, TEXT("HouseForge preview rebuilt: %d walls, %d rooms, %d fixtures."),
		Spec.Walls.Num(), Spec.Rooms.Num(), Spec.Fixtures.Num());
}

void AHFHouseActor::DrawPrism(const TArray<FVector2D>& Polygon, double BottomZ, double TopZ,
	const FLinearColor& Color, float Thickness, bool bVerticals)
{
	const int32 Count = Polygon.Num();
	if (Count < 2 || Lines == nullptr)
	{
		return;
	}

	for (int32 i = 0; i < Count; ++i)
	{
		const FVector2D& A = Polygon[i];
		const FVector2D& B = Polygon[(i + 1) % Count];

		const FVector A0(A.X, A.Y, BottomZ);
		const FVector B0(B.X, B.Y, BottomZ);
		const FVector A1(A.X, A.Y, TopZ);
		const FVector B1(B.X, B.Y, TopZ);

		Lines->DrawLine(A0, B0, Color, DepthPriority, Thickness, 0.0f);
		if (!FMath::IsNearlyEqual(BottomZ, TopZ))
		{
			Lines->DrawLine(A1, B1, Color, DepthPriority, Thickness, 0.0f);
			if (bVerticals)
			{
				Lines->DrawLine(A0, A1, Color, DepthPriority, Thickness, 0.0f);
			}
		}
	}
}

void AHFHouseActor::DrawWalls()
{
	for (const FHFWall& Wall : Spec.Walls)
	{
		const TArray<FVector2D> Footprint = WallFootprint(Wall);
		if (Footprint.Num() == 4)
		{
			DrawPrism(Footprint, Wall.BaseZ, Wall.BaseZ + Wall.Height, ColourWall, 2.0f);
		}
	}
}

void AHFHouseActor::DrawOpenings()
{
	for (const FHFOpening& Opening : Spec.Openings)
	{
		const FHFWall* Wall = Spec.FindWall(Opening.WallId);
		if (Wall == nullptr)
		{
			continue;
		}

		const double Length = Wall->Length();
		if (Length <= UE_KINDA_SMALL_NUMBER)
		{
			continue;
		}

		const FVector2D Direction = (Wall->End - Wall->Start) / Length;
		const FVector2D Normal(-Direction.Y, Direction.X);
		const double HalfWidth = Opening.Width * 0.5;
		const double HalfThickness = Wall->Thickness * 0.5;

		const FVector2D Near = Wall->Start + Direction * (Opening.OffsetAlongWall - HalfWidth);
		const FVector2D Far  = Wall->Start + Direction * (Opening.OffsetAlongWall + HalfWidth);

		TArray<FVector2D> Reveal;
		Reveal.Add(Near + Normal * HalfThickness);
		Reveal.Add(Far + Normal * HalfThickness);
		Reveal.Add(Far - Normal * HalfThickness);
		Reveal.Add(Near - Normal * HalfThickness);

		const double BottomZ = Wall->BaseZ + Opening.SillHeight;
		const double TopZ = Wall->BaseZ + Opening.HeadHeight();
		DrawPrism(Reveal, BottomZ, TopZ, ColourOpening, 2.5f);

		DrawSwing(Opening, *Wall, Direction, Normal, Near, Far, BottomZ);
	}
}

void AHFHouseActor::DrawSwing(const FHFOpening& Opening, const FHFWall& Wall,
	const FVector2D& Direction, const FVector2D& Normal,
	const FVector2D& Near, const FVector2D& Far, double BaseZ)
{
	if (Opening.Kind != EHFOpeningKind::Door || Opening.Swing == EHFSwing::None || Lines == nullptr)
	{
		return;
	}

	// Without this a door hung on the wrong side is completely invisible from above, which makes
	// the swing impossible to check against the drawing's swing arc.
	const bool bHingeAtNear = (Opening.Swing == EHFSwing::InwardLeft || Opening.Swing == EHFSwing::OutwardLeft);
	const FVector2D Hinge = bHingeAtNear ? Near : Far;
	const double Side = (Opening.Swing == EHFSwing::InwardLeft || Opening.Swing == EHFSwing::InwardRight) ? 1.0 : -1.0;

	const double Width = Opening.Width;
	const FVector2D LeafTip = Hinge + Normal * (Width * Side);

	// The leaf, drawn open at ninety degrees as it is on a plan.
	const FVector HingePoint(Hinge.X, Hinge.Y, BaseZ);
	Lines->DrawLine(HingePoint, FVector(LeafTip.X, LeafTip.Y, BaseZ), ColourSwing, DepthPriority, 2.0f, 0.0f);

	// The arc it sweeps, from the open leaf back to the closed position in the wall.
	const FVector2D Closed = bHingeAtNear ? Far : Near;
	constexpr int32 Segments = 12;
	FVector2D Previous = LeafTip;
	for (int32 i = 1; i <= Segments; ++i)
	{
		const double Alpha = static_cast<double>(i) / Segments;
		const FVector2D Swept =
			Normal * Side * FMath::Cos(Alpha * HALF_PI) +
			(Closed - Hinge).GetSafeNormal() * FMath::Sin(Alpha * HALF_PI);

		const FVector2D Point = Hinge + Swept * Width;
		Lines->DrawLine(FVector(Previous.X, Previous.Y, BaseZ), FVector(Point.X, Point.Y, BaseZ),
			ColourSwing, DepthPriority, 1.0f, 0.0f);
		Previous = Point;
	}
}

void AHFHouseActor::DrawRooms()
{
	for (const FHFRoom& Room : Spec.Rooms)
	{
		if (Room.Boundary.Num() < 3)
		{
			continue;
		}

		// Floor outline plus the slab line above it, so room height reads in a perspective view.
		DrawPrism(Room.Boundary, Room.FloorZ, Room.FloorZ + Room.CeilingHeight,
			ColourRoom, 1.0f, /*bVerticals*/ false);
	}
}

void AHFHouseActor::DrawStructure()
{
	for (const FHFBeam& Beam : Spec.Beams)
	{
		const double Length = Beam.Length();
		if (Length <= UE_KINDA_SMALL_NUMBER)
		{
			continue;
		}

		const FVector2D Direction = (Beam.End - Beam.Start) / Length;
		const FVector2D Normal(-Direction.Y, Direction.X);
		const double Half = Beam.Width * 0.5;

		TArray<FVector2D> Footprint;
		Footprint.Add(Beam.Start + Normal * Half);
		Footprint.Add(Beam.End + Normal * Half);
		Footprint.Add(Beam.End - Normal * Half);
		Footprint.Add(Beam.Start - Normal * Half);

		// Beams hang down from the slab soffit, so they occupy ClearHeight..SoffitZ.
		DrawPrism(Footprint, Beam.ClearHeight(), Beam.SoffitZ, ColourBeam, 2.0f);
	}

	for (const FHFColumn& Column : Spec.Columns)
	{
		DrawPrism(RectFootprint(Column.Position, Column.Size, Column.RotationDegrees),
			Column.BaseZ, Column.BaseZ + Column.Height, ColourColumn, 2.0f);
	}
}

void AHFHouseActor::DrawFixtures()
{
	for (const FHFFixture& Fixture : Spec.Fixtures)
	{
		if (Fixture.Footprint.X <= 0.0 || Fixture.Footprint.Y <= 0.0)
		{
			continue;
		}

		const FHFRoom* Room = Spec.FindRoom(Fixture.RoomId);
		const double FloorZ = Room ? Room->FloorZ : 0.0;
		const double BottomZ = FloorZ + Fixture.BaseZ;

		DrawPrism(RectFootprint(Fixture.Position, Fixture.Footprint, Fixture.RotationDegrees),
			BottomZ, BottomZ + Fixture.Height, ColourFixture, 1.5f);
	}
}

void AHFHouseActor::DrawCeilings()
{
	for (const FHFFalseCeiling& Ceiling : Spec.FalseCeilings)
	{
		if (Ceiling.Style == EHFCeilingStyle::None)
		{
			continue;
		}

		const FHFRoom* Room = Spec.FindRoom(Ceiling.RoomId);
		const TArray<FVector2D>& Polygon = Ceiling.ExplicitPolygon.Num() >= 3
			? Ceiling.ExplicitPolygon
			: (Room ? Room->Boundary : Ceiling.ExplicitPolygon);

		if (Polygon.Num() < 3 || Room == nullptr)
		{
			continue;
		}

		// The finished soffit sits Drop below the slab.
		const double SoffitZ = Room->FloorZ + Room->CeilingHeight - Ceiling.Drop;
		DrawPrism(Polygon, SoffitZ, SoffitZ, ColourCeiling, 2.0f, /*bVerticals*/ false);

		for (const FVector2D& Light : Ceiling.LightPositions)
		{
			const FVector Centre(Light.X, Light.Y, SoffitZ);
			Lines->DrawCircle(Centre, FVector::XAxisVector, FVector::YAxisVector,
				ColourCeiling, 8.0f, 12, DepthPriority);
		}
	}
}
