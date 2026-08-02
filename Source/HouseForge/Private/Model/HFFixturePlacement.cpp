// Copyright Siddartha G. All Rights Reserved.

#include "Model/HFFixturePlacement.h"

double FHFFixturePlacement::FacingYaw(const FHFFixture& Fixture, const FHFWall* AnchorWall)
{
	double Yaw = Fixture.RotationDegrees;

	if (AnchorWall == nullptr)
	{
		return Yaw;
	}

	// Which way the back of the run looks at this yaw. Local +Y runs back into the unit, and a yaw
	// rotation takes it to (-sin, cos).
	const double Radians = FMath::DegreesToRadians(Yaw);
	const FVector2D Back(-FMath::Sin(Radians), FMath::Cos(Radians));

	// From the wall to the fixture. If the back is already pointing that way it is pointing AWAY from
	// the wall, so the run is facing into it - and the whole thing is turned round.
	const FVector2D OnWall = FMath::ClosestPointOnSegment2D(Fixture.Position, AnchorWall->Start, AnchorWall->End);
	const FVector2D ToFixture = Fixture.Position - OnWall;

	if (FVector2D::DotProduct(Back, ToFixture) > 0.0)
	{
		Yaw += 180.0;
	}

	return Yaw;
}

FTransform FHFFixturePlacement::AgainstWall(const FHFFixture& Fixture, double FloorZ, const FHFWall* AnchorWall)
{
	const FRotator Rotation(0.0, FacingYaw(Fixture, AnchorWall), 0.0);

	// The fixture is positioned by the CENTRE of its footprint and a run is built from its front-left
	// corner, so the corner is where the actor goes. Rotated with the run, or a turned fixture lands
	// half its own length away from where the drawing put it.
	const FVector ToCorner = Rotation.RotateVector(
		FVector(-Fixture.Footprint.X * 0.5, -Fixture.Footprint.Y * 0.5, 0.0));

	return FTransform(Rotation,
		FVector(Fixture.Position.X, Fixture.Position.Y, FloorZ + Fixture.BaseZ) + ToCorner);
}

FTransform FHFFixturePlacement::FreeStanding(const FHFFixture& Fixture, double FloorZ)
{
	return FTransform(FRotator(0.0, Fixture.RotationDegrees, 0.0),
		FVector(Fixture.Position.X, Fixture.Position.Y, FloorZ + Fixture.BaseZ));
}

FTransform FHFFixturePlacement::OnSurface(const FHFFixture& Fixture, double SurfaceZ, double YawDegrees)
{
	return FTransform(FRotator(0.0, YawDegrees, 0.0),
		FVector(Fixture.Position.X, Fixture.Position.Y, SurfaceZ));
}

double FHFFixturePlacement::WallFaceCorrection(const FHFFixture& Fixture, const FHFWall* AnchorWall)
{
	if (AnchorWall == nullptr)
	{
		return 0.0;
	}

	// Which way the back of the fitting looks once it has been turned to face the room. The same
	// (-sin, cos) as FacingYaw, read at the yaw FacingYaw settled on rather than at the drawn one -
	// a fitting the half turn caught has its back where the half turn left it.
	const double Radians = FMath::DegreesToRadians(FacingYaw(Fixture, AnchorWall));
	const FVector2D Back(-FMath::Sin(Radians), FMath::Cos(Radians));

	// The finished face on THIS fitting's side of the wall, not the centreline. Which side is decided
	// by where the fitting is: a partition has a face in each room and both are real.
	const FVector2D OnCentreline =
		FMath::ClosestPointOnSegment2D(Fixture.Position, AnchorWall->Start, AnchorWall->End);

	FVector2D ToFixture = Fixture.Position - OnCentreline;
	if (ToFixture.IsNearlyZero())
	{
		// Dead on the centreline, which is a fixture drawn inside its own wall. The back direction is
		// the only side information left, and it points at the face this fitting hangs on.
		ToFixture = -Back;
	}

	const FVector2D Face = OnCentreline + ToFixture.GetSafeNormal() * (AnchorWall->Thickness * 0.5);

	// The fitting's own back plane, at the drawn position, and how far past the plaster it is.
	const FVector2D BackPoint = Fixture.Position + Back * (Fixture.Footprint.Y * 0.5);

	return FVector2D::DotProduct(BackPoint - Face, Back);
}

FTransform FHFFixturePlacement::OnWallFace(const FHFFixture& Fixture, double FloorZ,
	const FHFWall* AnchorWall)
{
	if (AnchorWall == nullptr)
	{
		return FreeStanding(Fixture, FloorZ);
	}

	const double Yaw = FacingYaw(Fixture, AnchorWall);
	const double Radians = FMath::DegreesToRadians(Yaw);
	const FVector2D Back(-FMath::Sin(Radians), FMath::Cos(Radians));

	// ALONG ITS OWN BACK DIRECTION AND NOWHERE ELSE. The drawing's position along the wall is a real
	// decision - a mirror is over the basin, a geyser over the shower - and sliding sideways to
	// satisfy a face would move the fitting off the thing it belongs to. Only the depth is corrected.
	const FVector2D Corrected = Fixture.Position - Back * WallFaceCorrection(Fixture, AnchorWall);

	return FTransform(FRotator(0.0, Yaw, 0.0),
		FVector(Corrected.X, Corrected.Y, FloorZ + Fixture.BaseZ));
}

double FHFFixturePlacement::WallCentrelineCorrection(const FHFFixture& Fixture, const FHFWall* AnchorWall)
{
	if (AnchorWall == nullptr)
	{
		return 0.0;
	}

	const double Radians = FMath::DegreesToRadians(FacingYaw(Fixture, AnchorWall));
	const FVector2D Back(-FMath::Sin(Radians), FMath::Cos(Radians));

	// The centreline itself, not a face: base plates go on the middle of a coping. Signed along the
	// fixture's own back direction, so the sign says which way it has to move.
	const FVector2D OnCentreline =
		FMath::ClosestPointOnSegment2D(Fixture.Position, AnchorWall->Start, AnchorWall->End);

	return FVector2D::DotProduct(Fixture.Position - OnCentreline, Back);
}

FTransform FHFFixturePlacement::OnWallTop(const FHFFixture& Fixture, double FloorZ,
	const FHFWall* AnchorWall)
{
	if (AnchorWall == nullptr)
	{
		return FreeStanding(Fixture, FloorZ);
	}

	const double Yaw = FacingYaw(Fixture, AnchorWall);
	const double Radians = FMath::DegreesToRadians(Yaw);
	const FVector2D Back(-FMath::Sin(Radians), FMath::Cos(Radians));

	// ACROSS THE WALL AND NOWHERE ELSE, as OnWallFace does. Where a railing starts and stops along
	// its parapet is a real decision on the drawing; only the offset across it is a fixing detail.
	const FVector2D Corrected = Fixture.Position - Back * WallCentrelineCorrection(Fixture, AnchorWall);

	return FTransform(FRotator(0.0, Yaw, 0.0),
		FVector(Corrected.X, Corrected.Y, FloorZ + Fixture.BaseZ));
}

FTransform FHFFixturePlacement::UnderSoffit(const FHFFixture& Fixture, double SoffitZ,
	const FHFWall* AnchorWall, double Embedment)
{
	// The top of the drawn box lands on the soffit, driven up into it by the embedment. The BaseZ the
	// drawing carries is deliberately not read: see the header.
	const double BottomZ = SoffitZ + FMath::Max(Embedment, 0.0) - Fixture.Height;

	if (AnchorWall == nullptr)
	{
		return FTransform(FRotator(0.0, Fixture.RotationDegrees, 0.0),
			FVector(Fixture.Position.X, Fixture.Position.Y, BottomZ));
	}

	const double Yaw = FacingYaw(Fixture, AnchorWall);
	const double Radians = FMath::DegreesToRadians(Yaw);
	const FVector2D Back(-FMath::Sin(Radians), FMath::Cos(Radians));

	const FVector2D Corrected = Fixture.Position - Back * WallFaceCorrection(Fixture, AnchorWall);

	return FTransform(FRotator(0.0, Yaw, 0.0), FVector(Corrected.X, Corrected.Y, BottomZ));
}

bool FHFFixturePlacement::FootprintContains(const FHFFixture& Fixture, const FVector2D& Point, double Margin)
{
	const double Radians = FMath::DegreesToRadians(Fixture.RotationDegrees);
	const double C = FMath::Cos(Radians);
	const double S = FMath::Sin(Radians);

	// Into the fixture's own frame: the inverse of a yaw about the footprint centre.
	const FVector2D D = Point - Fixture.Position;
	const FVector2D Local(D.X * C + D.Y * S, -D.X * S + D.Y * C);

	return FMath::Abs(Local.X) <= Fixture.Footprint.X * 0.5 + Margin
		&& FMath::Abs(Local.Y) <= Fixture.Footprint.Y * 0.5 + Margin;
}

void FHFFixturePlacement::WorldZRange(const FHFFixture& Fixture, const FHFRoom* Room,
	double& OutBottomZ, double& OutTopZ)
{
	const double FloorZ = Room != nullptr ? Room->FloorZ : 0.0;

	if (Fixture.IsCeilingMounted())
	{
		// BaseZ on a ceiling-mounted fixture is a DROP measured down from the ceiling, so its head is
		// that far below the slab and its foot a height further down again. Read from the floor it
		// would come out at knee level, which is not where any fan in this flat is.
		const double CeilingZ = FloorZ + (Room != nullptr ? Room->CeilingHeight : 300.0);
		OutTopZ = CeilingZ - Fixture.BaseZ;
		OutBottomZ = OutTopZ - Fixture.Height;
		return;
	}

	OutBottomZ = FloorZ + Fixture.BaseZ;
	OutTopZ = OutBottomZ + Fixture.Height;
}
