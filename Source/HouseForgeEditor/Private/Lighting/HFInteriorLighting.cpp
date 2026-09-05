// Copyright Siddartha G. All Rights Reserved.

#include "Lighting/HFInteriorLighting.h"

#include "Capture/HFViewingLight.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/Scene.h"
#include "Engine/SkyLight.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HouseForgeEditor.h"
#include "Lighting/HFExposure.h"

const FName& FHFInteriorLighting::Tag()
{
	static const FName Value(TEXT("HouseForge.InteriorLighting"));
	return Value;
}

namespace
{
	void MarkAsRig(AActor* Actor, const FString& Label)
	{
		if (Actor == nullptr)
		{
			return;
		}

		Actor->Tags.AddUnique(FHFInteriorLighting::Tag());

#if WITH_EDITOR
		Actor->SetActorLabel(Label);
		Actor->SetFolderPath(FHFInteriorLighting::OutlinerFolder());
#endif
	}
}

TArray<AActor*> FHFInteriorLighting::FindIn(UWorld* World)
{
	TArray<AActor*> Found;
	if (World == nullptr)
	{
		return Found;
	}

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (IsValid(*It) && It->Tags.Contains(Tag()))
		{
			Found.Add(*It);
		}
	}
	return Found;
}

TArray<AActor*> FHFInteriorLighting::EnsureIn(UWorld* World, bool* bOutSpawned)
{
	if (bOutSpawned != nullptr)
	{
		*bOutSpawned = false;
	}

	if (World == nullptr)
	{
		return {};
	}

	// Found before anything is spawned, and that ordering is the whole guarantee. Every build calls
	// this; without the search first, a level would gain a sun per build and get steadily brighter
	// until nothing in it could be read.
	TArray<AActor*> Existing = FindIn(World);
	if (!Existing.IsEmpty())
	{
		return Existing;
	}

	// THE PLACEHOLDER COMES OUT FIRST, and this is the call its own header was written around. Two
	// unbound post-process volumes and two suns in one level is not "brighter" in any way anybody
	// could reason about - the volumes fight over exposure by priority and the suns simply add - and
	// the failure looks like a lighting bug rather than like two rigs.
	if (const int32 Removed = FHFViewingLight::RemoveFrom(World); Removed > 0)
	{
		UE_LOG(LogHouseForgeEditor, Log,
			TEXT("HouseForge removed %d placeholder viewing-light actor(s) and replaced them with the ")
			TEXT("interior lighting rig."), Removed);
	}

	FActorSpawnParameters Params;
	Params.ObjectFlags = RF_Transactional;

	TArray<AActor*> Spawned;

	// ---------------------------------------------------------------------------------- the sun
	if (ADirectionalLight* Sun = World->SpawnActor<ADirectionalLight>(
		FVector::ZeroVector, FRotator(-48.0f, -140.0f, 0.0f), Params))
	{
		// Movable. The project runs with static lighting off, so a Stationary or Static light here
		// would be a light waiting for a build it can never get.
		Sun->SetMobility(EComponentMobility::Movable);

		if (UDirectionalLightComponent* Light = Sun->GetComponent())
		{
			// In LUX, which is the unit a directional light is already in - there is nothing to set.
			Light->SetIntensity(SunLux());
			Light->SetLightColor(FLinearColor(1.0f, 0.96f, 0.90f));

			// A real sun subtends about half a degree. Three, because what comes through a window
			// into one of these rooms is as much sky as sun, and a half-degree penumbra on a
			// window reveal is a harder edge than any of these flats actually show.
			Light->LightSourceAngle = 3.0f;
			Light->CastShadows = true;

			// Makes the sky atmosphere below take its direction and colour from this light.
			Light->bAtmosphereSunLight = true;
		}

		MarkAsRig(Sun, TEXT("HF_Sun"));
		Spawned.Add(Sun);
	}

	// ---------------------------------------------------------------------------- the sky light
	if (ASkyLight* Sky = World->SpawnActor<ASkyLight>(FVector(0.0, 0.0, 500.0), FRotator::ZeroRotator, Params))
	{
		if (USkyLightComponent* Light = Sky->GetLightComponent())
		{
			// ASkyLight has no SetMobility of its own; it has to be set on the component. Movable,
			// or it contributes nothing at all without a lighting build.
			Light->SetMobility(EComponentMobility::Movable);

			Light->SourceType = SLS_CapturedScene;
			Light->bRealTimeCapture = true;
			Light->SetIntensity(1.0f);

			// OCCLUDED, unlike the placeholder rig. See the header: the reason that one could not
			// afford occlusion was that an unlit back bedroom is indistinguishable from a bedroom
			// that failed to generate, and the back bedroom now has a light in it.
			Light->CastShadows = true;

			// The ground is not black. A sky light with a black lower hemisphere puts nothing at all
			// on downward-facing surfaces, and in a flat that is every soffit, every opening head and
			// the underside of every loft - the surfaces an interior is judged on.
			Light->bLowerHemisphereIsBlack = false;
		}

		MarkAsRig(Sky, TEXT("HF_SkyLight"));
		Spawned.Add(Sky);
	}

	// ----------------------------------------------------------------------- the sky to look at
	//
	// Worth having even though the subject is indoors: it is what a window frames. Without it a
	// window is a hole onto the clear colour, and every interior still in this repository has at
	// least one window in it.
	if (ASkyAtmosphere* Atmosphere = World->SpawnActor<ASkyAtmosphere>(
		FVector::ZeroVector, FRotator::ZeroRotator, Params))
	{
		MarkAsRig(Atmosphere, TEXT("HF_SkyAtmosphere"));
		Spawned.Add(Atmosphere);
	}

	// ------------------------------------------------------- exposure, local exposure and Lumen
	if (APostProcessVolume* Volume = World->SpawnActor<APostProcessVolume>(
		FVector::ZeroVector, FRotator::ZeroRotator, Params))
	{
		// Unbound, so it applies wherever a camera is put - including one outside the flat looking
		// in, which a bounded volume would miss, and including the PIE pawn.
		Volume->bUnbound = true;
		Volume->BlendWeight = 1.0f;
		ApplyInteriorSettingsTo(Volume->Settings);

		MarkAsRig(Volume, TEXT("HF_Exposure"));
		Spawned.Add(Volume);
	}

	if (bOutSpawned != nullptr)
	{
		*bOutSpawned = !Spawned.IsEmpty();
	}

	UE_LOG(LogHouseForgeEditor, Log,
		TEXT("HouseForge spawned %d interior lighting actor(s) at EV100 %.1f."),
		Spawned.Num(), InteriorEV100());

	return Spawned;
}

int32 FHFInteriorLighting::RemoveFrom(UWorld* World)
{
	int32 Removed = 0;
	for (AActor* Actor : FindIn(World))
	{
		if (IsValid(Actor) && World->DestroyActor(Actor))
		{
			++Removed;
		}
	}
	return Removed;
}

void FHFInteriorLighting::ApplyInteriorSettingsTo(FPostProcessSettings& Settings)
{
	FHFExposure::PinTo(Settings, InteriorEV100());

	// ------------------------------------------------------------------------- local exposure
	//
	// THE ONE TOOL FOR A BRIGHT WINDOW IN A DARK ROOM, and the reason this rig can pin its exposure
	// and still be readable. A window in one of these flats is a surface lit at 10000 lux next to a
	// wall lit at 250 - about five stops - and no single exposure holds both. Local exposure
	// compresses the base layer of the image while leaving detail contrast alone, so the window
	// keeps some tone and the room beside it keeps its own.
	//
	// Applied to highlights and shadows equally at 0.6, which is a little stronger than the
	// project's own default of 0.8 (r.DefaultFeature.LocalExposure.*ContrastScale) - lower means
	// more compression. Set here on the volume rather than relying on that default, because the
	// default is a project ini this plugin is not allowed to edit and a level carrying this rig
	// must look the same in a project that has not set it.
	Settings.bOverride_LocalExposureMethod = 1;
	Settings.LocalExposureMethod = ELocalExposureMethod::Bilateral;

	Settings.bOverride_LocalExposureHighlightContrastScale = 1;
	Settings.LocalExposureHighlightContrastScale = 0.6f;

	Settings.bOverride_LocalExposureShadowContrastScale = 1;
	Settings.LocalExposureShadowContrastScale = 0.6f;

	// Detail contrast left at full. The compression above is meant to move the BASE layer only;
	// pulling detail down with it is what makes a local-exposure image look flat and processed,
	// and the detail is where the tile grout and the joinery shadow gaps live.
	Settings.bOverride_LocalExposureDetailStrength = 1;
	Settings.LocalExposureDetailStrength = 1.0f;

	// ---------------------------------------------------------------------------------- Lumen
	//
	// Asked for explicitly rather than left to project scalability. An interior lit by fittings is
	// almost entirely lit by BOUNCE - the light that reaches the far side of a room has come off a
	// ceiling - and with global illumination off the same flat is four bright pools on the floor
	// and black everywhere else. That is not a worse-looking version of this rig; it is a different
	// picture entirely, and it must not depend on a machine's scalability setting.
	Settings.bOverride_DynamicGlobalIlluminationMethod = 1;
	Settings.DynamicGlobalIlluminationMethod = EDynamicGlobalIlluminationMethod::Lumen;

	Settings.bOverride_ReflectionMethod = 1;
	Settings.ReflectionMethod = EReflectionMethod::Lumen;

	// SKYLIGHT LEAKING STAYS AT ZERO, deliberately and explicitly. It is a cheat that pushes
	// skylight through geometry Lumen has not resolved, and its whole effect is to stop enclosed
	// interiors going black - which is to say it would hide exactly the failure this rig is
	// supposed to make visible. Written down rather than left at the default so that raising it is
	// a decision somebody makes on purpose.
	Settings.bOverride_LumenSkylightLeaking = 1;
	Settings.LumenSkylightLeaking = 0.0f;

	// A flat is small and every room is a box of bounces. The extra final-gather quality is cheap
	// at this scale and is what stops the corners of a room reading as noise in a still.
	Settings.bOverride_LumenFinalGatherQuality = 1;
	Settings.LumenFinalGatherQuality = 2.0f;
}
