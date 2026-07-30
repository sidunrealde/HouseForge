// Copyright Siddartha G. All Rights Reserved.

#include "Capture/HFViewingLight.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/SkyLight.h"
#include "Engine/TextureCube.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HouseForgeEditor.h"

const FName& FHFViewingLight::Tag()
{
	static const FName Value(TEXT("HouseForge.PlaceholderLighting"));
	return Value;
}

namespace
{
	/**
	 * Pins exposure to a stated EV100, with no eye adaptation anywhere in the path.
	 *
	 * Expressed through the physical camera rather than through AutoExposureMinBrightness and
	 * MaxBrightness, which is the other way of pinning exposure. Those two are read as EV100 only
	 * when the project has bExtendDefaultLuminanceRange on, and as raw linear brightness when it
	 * does not - so the same numbers would mean two entirely different exposures depending on a
	 * project ini this plugin is not allowed to edit (.claude/rules/01-scope.md). Shutter, ISO and
	 * aperture mean the same thing either way.
	 *
	 * EV100 = log2(N^2 / t * 100 / ISO). The aperture is fixed and the ISO carries the figure,
	 * which is the opposite of the obvious arrangement and is the point: the aperture is clamped to
	 * f/1 through f/32 by the engine, and with the shutter at 1/100 that bounds an aperture-carried
	 * EV100 to roughly 6.6 through 16.6. Asking for anything below 6.6 silently clamps, so three
	 * different exposures come out identical - which is exactly what happened while this was being
	 * measured, and it read as "exposure has no effect" rather than as a clamp. ISO has no such
	 * ceiling.
	 */
	void ApplyManualExposure(FPostProcessSettings& Settings, float EV100)
	{
		constexpr float Fstop = 2.0f;
		constexpr float ShutterDenominator = 100.0f;

		// ISO = N^2 * t^-1 * 100 / 2^EV100.
		const float Iso = FMath::Max(1.0f,
			Fstop * Fstop * ShutterDenominator * 100.0f / FMath::Pow(2.0f, EV100));

		Settings.bOverride_AutoExposureMethod = 1;
		Settings.AutoExposureMethod = AEM_Manual;

		Settings.bOverride_AutoExposureApplyPhysicalCameraExposure = 1;
		Settings.AutoExposureApplyPhysicalCameraExposure = 1;

		Settings.bOverride_AutoExposureBias = 1;
		Settings.AutoExposureBias = 0.0f;

		Settings.bOverride_CameraISO = 1;
		Settings.CameraISO = Iso;

		Settings.bOverride_CameraShutterSpeed = 1;
		Settings.CameraShutterSpeed = ShutterDenominator;

		Settings.bOverride_DepthOfFieldFstop = 1;
		Settings.DepthOfFieldFstop = Fstop;
	}

	/**
	 * The ambient fill that makes the inside of the flat visible.
	 *
	 * The engine's own daylight ambient cubemap, at the intensity the reference flat was measured
	 * at: the master bedroom renders around 44% mean brightness with it and around 3% without,
	 * while the plan barely moves - so this lights interiors and leaves the sun in charge of
	 * everything the sky can see.
	 *
	 * An ambient cubemap rather than the sky light because a scene capture has no global
	 * illumination, and a sky light's diffuse arrives through GI. See the class comment.
	 */
	void ApplyAmbientFill(FPostProcessSettings& Settings)
	{
		static const TCHAR* CubemapPath = TEXT("/Engine/MapTemplates/Sky/DaylightAmbientCubemap.DaylightAmbientCubemap");

		if (UTextureCube* Cubemap = LoadObject<UTextureCube>(nullptr, CubemapPath))
		{
			Settings.AmbientCubemap = Cubemap;
			Settings.bOverride_AmbientCubemapIntensity = 1;
			Settings.AmbientCubemapIntensity = 10.0f;
		}
	}

	void MarkAsPlaceholder(AActor* Actor, const FString& Label)
	{
		if (Actor == nullptr)
		{
			return;
		}

		Actor->Tags.AddUnique(FHFViewingLight::Tag());

#if WITH_EDITOR
		// Named and filed so nobody mistakes the rig for lighting design. It is scaffolding, and
		// milestone 11 deletes it.
		Actor->SetActorLabel(Label);
		Actor->SetFolderPath(FHFViewingLight::OutlinerFolder());
#endif
	}
}

TArray<AActor*> FHFViewingLight::FindIn(UWorld* World)
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

TArray<AActor*> FHFViewingLight::EnsureIn(UWorld* World, bool* bOutSpawned)
{
	if (bOutSpawned != nullptr)
	{
		*bOutSpawned = false;
	}

	if (World == nullptr)
	{
		return {};
	}

	// Found before anything is spawned, and that ordering is the whole guarantee. Every capture
	// calls this, and so does every apply; without the search first, a level would gain a sun per
	// screenshot and get steadily brighter until nothing in it could be read.
	TArray<AActor*> Existing = FindIn(World);
	if (!Existing.IsEmpty())
	{
		return Existing;
	}

	FActorSpawnParameters Params;
	Params.ObjectFlags = RF_Transactional;

	TArray<AActor*> Spawned;

	// ---------------------------------------------------------------------------------- the sun
	if (ADirectionalLight* Sun = World->SpawnActor<ADirectionalLight>(
		FVector::ZeroVector, FRotator(-48.0f, -140.0f, 0.0f), Params))
	{
		// Movable, because the rig is spawned into a level that is never lit-built and is expected
		// to be deleted again. A stationary light would want a build and complain in the viewport
		// until it got one.
		Sun->SetMobility(EComponentMobility::Movable);

		if (UDirectionalLightComponent* Light = Sun->GetComponent())
		{
			// A bright overcast day's worth of sun, not noon. The flat is being read, not
			// photographed: a hard sun through the window openings blows the rooms nearest them
			// and leaves the rest black. In lux, so it means something once real materials land.
			Light->SetIntensity(SunLux());
			Light->SetLightColor(FLinearColor(1.0f, 0.96f, 0.90f));

			// Wide, soft shadows for the same reason. A plan wants enough shadow to separate a
			// wall from the floor beside it and no more.
			Light->LightSourceAngle = 3.0f;
			Light->CastShadows = true;

			// Makes the sky atmosphere below take its sun direction and colour from this light.
			Light->bAtmosphereSunLight = true;
		}

		MarkAsPlaceholder(Sun, TEXT("HF_Placeholder_Sun"));
		Spawned.Add(Sun);
	}

	// ---------------------------------------------------------------------------- the sky light
	if (ASkyLight* Sky = World->SpawnActor<ASkyLight>(FVector(0.0, 0.0, 500.0), FRotator::ZeroRotator, Params))
	{
		if (USkyLightComponent* Light = Sky->GetLightComponent())
		{
			// Movable, or it contributes nothing at all without a lighting build - and this rig is
			// spawned into levels that are never built. ASkyLight has no SetMobility of its own;
			// it has to be set on the component.
			Light->SetMobility(EComponentMobility::Movable);

			Light->SourceType = SLS_CapturedScene;
			Light->bRealTimeCapture = true;
			Light->SetIntensity(1.0f);

			// Both of these are deliberately unphysical, and both are what makes an interior
			// readable at all without the lighting milestone behind them.
			//
			// A black lower hemisphere takes every downward-facing surface to nothing - soffits,
			// the underside of a loft, the head of every opening - and those are exactly the
			// surfaces an interior view is checked on.
			Light->bLowerHemisphereIsBlack = false;

			// Unoccluded ambient. With occlusion, and with no ray tracing guaranteed on whatever
			// machine this runs on, the rooms at the back of the flat go black - and a black room
			// is indistinguishable from a room that failed to generate, which is the one mistake
			// a diagnostic render must not make.
			Light->CastShadows = false;
		}

		MarkAsPlaceholder(Sky, TEXT("HF_Placeholder_SkyLight"));
		Spawned.Add(Sky);
	}

	// ----------------------------------------------------------------------- the sky to look at
	if (ASkyAtmosphere* Atmosphere = World->SpawnActor<ASkyAtmosphere>(
		FVector::ZeroVector, FRotator::ZeroRotator, Params))
	{
		MarkAsPlaceholder(Atmosphere, TEXT("HF_Placeholder_SkyAtmosphere"));
		Spawned.Add(Atmosphere);
	}

	// ------------------------------------------------------------------------------- exposure
	if (APostProcessVolume* Volume = World->SpawnActor<APostProcessVolume>(
		FVector::ZeroVector, FRotator::ZeroRotator, Params))
	{
		// Unbound, so it applies wherever a camera is put - including one placed outside the flat
		// looking in, which a bounded volume would miss.
		Volume->bUnbound = true;
		Volume->BlendWeight = 1.0f;
		ApplyViewingSettingsTo(Volume->Settings);

		MarkAsPlaceholder(Volume, TEXT("HF_Placeholder_Exposure"));
		Spawned.Add(Volume);
	}

	if (bOutSpawned != nullptr)
	{
		*bOutSpawned = !Spawned.IsEmpty();
	}

	UE_LOG(LogHouseForgeEditor, Log,
		TEXT("HouseForge spawned %d placeholder viewing-light actor(s). These are scaffolding for reading ")
		TEXT("generated geometry, not lighting design, and the lighting milestone replaces them."),
		Spawned.Num());

	return Spawned;
}

int32 FHFViewingLight::RemoveFrom(UWorld* World)
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

void FHFViewingLight::ApplyViewingSettingsTo(FPostProcessSettings& Settings)
{
	ApplyManualExposure(Settings, InteriorEV100());
	ApplyAmbientFill(Settings);
}
