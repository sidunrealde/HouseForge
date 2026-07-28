// Copyright Siddartha G. All Rights Reserved.

#pragma once

#if WITH_DEV_AUTOMATION_TESTS

#include "CoreMinimal.h"
#include "Model/HFTypes.h"

namespace HouseForgeTest
{
	/**
	 * A minimal spec that passes every validator rule: one 400x300 rectangular room enclosed by
	 * four walls, with a door in the south wall.
	 *
	 * Tests break exactly one thing about this and assert the matching rule fires, which keeps
	 * each test honest - if a rule stops firing, only that test fails.
	 */
	inline FHFHouseSpec MakeValidSpec()
	{
		FHFHouseSpec Spec;
		Spec.SchemaVersion = 1;
		Spec.Name = TEXT("Test Unit");
		Spec.Units = EHFUnits::Centimeters;
		Spec.DefaultWallThickness = 11.5;
		Spec.DefaultWallHeight = 300.0;

		const FVector2D SW(0.0, 0.0);
		const FVector2D SE(400.0, 0.0);
		const FVector2D NE(400.0, 300.0);
		const FVector2D NW(0.0, 300.0);

		auto MakeWall = [&Spec](const FName& Id, const FVector2D& Start, const FVector2D& End)
		{
			FHFWall Wall;
			Wall.Id = Id;
			Wall.Start = Start;
			Wall.End = End;
			Wall.Thickness = Spec.DefaultWallThickness;
			Wall.Height = Spec.DefaultWallHeight;
			Wall.bIsExternal = true;
			return Wall;
		};

		Spec.Walls.Add(MakeWall(TEXT("W_South"), SW, SE));
		Spec.Walls.Add(MakeWall(TEXT("W_East"),  SE, NE));
		Spec.Walls.Add(MakeWall(TEXT("W_North"), NE, NW));
		Spec.Walls.Add(MakeWall(TEXT("W_West"),  NW, SW));

		FHFOpening Door;
		Door.Id = TEXT("D1");
		Door.WallId = TEXT("W_South");
		Door.OffsetAlongWall = 200.0;
		Door.Width = 90.0;
		Door.Height = 210.0;
		Door.SillHeight = 0.0;
		Door.Kind = EHFOpeningKind::Door;
		Door.Swing = EHFSwing::InwardLeft;
		Spec.Openings.Add(Door);

		FHFRoom Room;
		Room.Id = TEXT("R_Bedroom");
		Room.Name = TEXT("Bedroom");
		Room.Type = EHFRoomType::Bedroom;
		Room.Boundary = { SW, SE, NE, NW };	// counter-clockwise, closing edge implicit
		Room.CeilingHeight = 300.0;
		Room.SkirtingHeight = 10.0;
		Spec.Rooms.Add(Room);

		return Spec;
	}

	/** A fixture sized and placed so it sits comfortably inside MakeValidSpec's room. */
	inline FHFFixture MakeFixture(const FName& Id, EHFFixtureType Type, const FVector2D& Position)
	{
		FHFFixture Fixture;
		Fixture.Id = Id;
		Fixture.RoomId = TEXT("R_Bedroom");
		Fixture.Type = Type;
		Fixture.Position = Position;
		Fixture.Footprint = FVector2D(60.0, 60.0);
		Fixture.Height = 75.0;
		return Fixture;
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS
