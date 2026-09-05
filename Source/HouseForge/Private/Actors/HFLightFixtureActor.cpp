// Copyright Siddartha G. All Rights Reserved.

#include "Actors/HFLightFixtureActor.h"

#include "Components/PointLightComponent.h"
#include "Model/HFFixturePlacement.h"

using namespace UE::Geometry;

// ------------------------------------------------------------------------------------ parameters

FHFLuminaireParams AHFLightFixtureActor::ParamsFor(const FHFFixture& Fixture)
{
	FHFLuminaireParams P;

	// The narrower plan dimension, because a fitting drawn in a box is inscribed in it. A circular
	// fitting given a 30 x 20 box is 20 across, not 30 - the other 10 is the space around it.
	const double Drawn = FMath::Min(FMath::Abs(Fixture.Footprint.X), FMath::Abs(Fixture.Footprint.Y));

	// FHFFixtureParams::Diameter wins where a drawing states one, exactly as it does for every other
	// round thing in this plugin. It is the field for saying "this is 300 across" about something
	// whose footprint box is only a bounding box.
	P.Diameter = Fixture.Params.Diameter > 0.0 ? Fixture.Params.Diameter
		: (Drawn > 0.0 ? Drawn : 22.0);

	// A drum is roughly a third as deep as it is wide, bounded so that neither a 12 cm downlight-
	// sized fitting nor a 60 cm feature light comes out as a disc or as a barrel.
	P.BodyHeight = FMath::Clamp(P.Diameter * 0.32, 3.5, 18.0);

	// ------------------------------------------------------------------ panel or pendant, decided here
	//
	// The whole of the fitting-type question, in one comparison, and it is a comparison a drawing can
	// actually answer. A light marked with a vertical extent no greater than the fitting itself is a
	// fitting screwed to the soffit; one marked deeper than that is hanging, and the excess IS the
	// drop. Nothing else in the spec is consulted, because nothing else in the spec knows.
	//
	// The 2 cm of slack matters: a drawing that states 40 for a 40-deep fitting must not come out as
	// a pendant on a 0 mm flex because of a rounding difference in the body depth derived above.
	const double Marked = FMath::Max(Fixture.Height, 0.0);

	if (Marked > P.BodyHeight + 2.0)
	{
		P.DropLength = Marked - P.BodyHeight;
	}
	else if (Marked > 0.0)
	{
		P.BodyHeight = Marked;
		P.DropLength = 0.0;
	}

	P.CanopyDiameter = FMath::Min(9.0, P.Diameter);

	return FHFLuminaireKit::Sanitise(P);
}

double AHFLightFixtureActor::DefaultLumensFor(const FHFLuminaireParams& Params)
{
	// See the header: two real products rather than a formula. A pendant is a lamp in a shade and a
	// panel is a panel, and there is no continuum between them worth interpolating along.
	return Params.HasDrop() ? 800.0 : 1600.0;
}

void AHFLightFixtureActor::ApplyFixture(const FHFFixture& Fixture)
{
	Luminaire = ParamsFor(Fixture);
	Lumens = DefaultLumensFor(Luminaire);
}

// ------------------------------------------------------------------------------------- placement

FTransform AHFLightFixtureActor::PlacementFor(const FHFFixture& Fixture, const FHFRoom* Room,
	const FHFWall* AnchorWall, double SoffitZ)
{
	// BaseZ is a drop below the finished ceiling for this fixture type - FHFFixture::IsCeilingMounted
	// - so it LOWERS the mounting point. See the header for why a wall-anchored fitting is read the
	// same way rather than switching to a height above the floor.
	const double FloorZ = (Room != nullptr) ? Room->FloorZ : 0.0;
	const double MountZ = SoffitZ - FMath::Max(Fixture.BaseZ, 0.0);

	if (AnchorWall == nullptr)
	{
		// Local +Z is the axis pointing away from the surface the fitting is screwed to, so on a
		// ceiling it points straight down. Half a turn about X, which takes +Z to -Z and leaves the
		// handedness intact - the same construction AHFFanActor uses for a ceiling fan, for the same
		// reason, and written the same way so the two can be read against each other.
		return FTransform(FQuat(FVector::XAxisVector, UE_DOUBLE_PI),
			FVector(Fixture.Position.X, Fixture.Position.Y, MountZ));
	}

	// A bracket is screwed to the FINISHED FACE of its wall, and its axis is that wall's normal
	// pointing into the room it serves.
	const double Yaw = FHFFixturePlacement::FacingYaw(Fixture, AnchorWall);
	const double Radians = FMath::DegreesToRadians(Yaw);

	// Local +Y at this yaw is the direction the BACK of a wall-mounted fitting looks, which is into
	// the wall. The lamp therefore points the other way. This is the same (-sin, cos) every other
	// wall placement in FHFFixturePlacement is built on, read at the yaw the half turn settled on.
	const FVector2D Back(-FMath::Sin(Radians), FMath::Cos(Radians));
	const FVector2D Corrected = Fixture.Position
		- Back * FHFFixturePlacement::WallFaceCorrection(Fixture, AnchorWall);

	const FVector Axis(-Back.X, -Back.Y, 0.0);

	// Roll pinned to world up rather than left to MakeFromZ, for the reason spelled out in
	// AHFFanActor::PlacementFor: the same fitting on two walls would otherwise come out rolled
	// differently, and a fitting that is not symmetric about its axis would show it.
	return FTransform(FRotationMatrix::MakeFromZY(Axis, FVector::ZAxisVector).ToQuat(),
		FVector(Corrected.X, Corrected.Y, FMath::Max(MountZ, FloorZ)));
}

// ------------------------------------------------------------------------------------ generation

FDynamicMesh3 AHFLightFixtureActor::BuildMesh() const
{
	// The lamp is rebuilt with the fitting, because where the light goes is a consequence of the
	// fitting's own dimensions and a fitting whose diameter changed would otherwise keep a lamp at
	// the old lens plane. Same argument, same shape, as AHFCeilingActor::BuildMesh.
	const_cast<AHFLightFixtureActor*>(this)->RebuildLights();

	return FHFLuminaireKit::Build(Luminaire);
}

int32 AHFLightFixtureActor::RebuildLights()
{
	// Destroyed and rebuilt rather than adjusted, so that calling this twice cannot leave two lamps
	// in one fitting. The same reasoning AHFCeilingActor::RebuildLights sets out at greater length.
	if (Light != nullptr)
	{
		Light->DestroyComponent();
		Light = nullptr;
	}

	if (!bBuildLights)
	{
		return 0;
	}

	UPointLightComponent* Point = NewObject<UPointLightComponent>(this);
	if (Point == nullptr)
	{
		return 0;
	}

	// A POINT LIGHT AND NOT A SPOT. A pendant or a surface panel throws into the whole room, and its
	// own body is what stops the half of that going up into the ceiling - which it does properly,
	// because the body is a closed solid that casts shadows. A spot cone would be a second, invented
	// shade in front of the modelled one, and the two would disagree the moment somebody edited the
	// fitting. The recessed downlights on AHFCeilingActor are spots because a downlight really does
	// have a reflector; this does not.
	const FVector3d LensLocal = FHFLuminaireKit::LensCentre(Luminaire);

	Point->SetWorldLocation(GetActorTransform().TransformPosition(FVector(LensLocal)));

	// Movable, because this actor regenerates on a property change and a static light would need a
	// lighting build to notice. The project runs with static lighting off in any case.
	Point->SetMobility(EComponentMobility::Movable);

	Point->SetUseTemperature(true);
	Point->SetTemperature(static_cast<float>(TemperatureKelvin));

	Point->SetIntensityUnits(ELightUnits::Lumens);
	Point->SetIntensity(static_cast<float>(FMath::Max(Lumens, 0.0)));
	Point->SetAttenuationRadius(static_cast<float>(FMath::Max(AttenuationRadius, 1.0)));

	// The lamp is as big as the diffuser it is behind, which is most of what makes the shadow under
	// a panel soft rather than the hard-edged one a mathematical point casts.
	Point->SetSourceRadius(static_cast<float>(FHFLuminaireKit::LensRadius(Luminaire)));

	Point->SetCastShadows(true);

	Point->RegisterComponent();
	Point->AttachToComponent(GetRootComponent(), FAttachmentTransformRules::KeepWorldTransform);

	Light = Point;

	return 1;
}
