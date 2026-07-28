// Copyright Siddartha G. All Rights Reserved.

#include "Actors/HFHouseActor.h"

#include "Components/LineBatchComponent.h"
#include "HouseForge.h"

namespace
{
	// Preview palette. Roles are colour-coded so a top-down screenshot can be compared against the
	// source drawing without needing labels.
	const FLinearColor ColourWall(0.10f, 0.10f, 0.12f);
	const FLinearColor ColourOpening(0.95f, 0.65f, 0.10f);
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

	if (!Spec.SourceDrawing.IsEmpty())
	{
		SourceDrawing = Spec.SourceDrawing;
	}

	Rebuild();
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
