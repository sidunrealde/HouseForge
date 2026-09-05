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
#include "Lighting/HFExposure.h"

const FName& FHFViewingLight::Tag()
{
	static const FName Value(TEXT("HouseForge.PlaceholderLighting"));
	return Value;
}

namespace
{
	/**
	 * The rig's exposure. The arithmetic and the two traps in it live in FHFExposure.
	 *
	 * Moved out when the lighting milestone needed the same function: a formula with a documented
	 * clamp trap in it should not exist twice.
	 */
	void ApplyManualExposure(FPostProcessSettings& Settings, float EV100)
	{
		FHFExposure::PinTo(Settings, EV100);
	}

	/**
	 * The ambient fill that makes the inside of the flat visible.
	 *
	 * An ambient cubemap rather than the sky light because a scene capture has no global
	 * illumination, and a sky light's diffuse arrives through GI. See the class comment.
	 *
	 * THE INTENSITY IS BALANCED AGAINST THE ALBEDOS, AND IT HAS BEEN WRONG ONCE FOR EXACTLY THAT
	 * REASON. It was 10.0, chosen when every surface was a dark untextured placeholder. Milestone 10
	 * raised the albedo set to what real finishes actually are - wall paint 0.79, ceiling 0.87, cove
	 * 0.90 linear - and left this where it was, which drove every interior render into the shoulder
	 * of the tone curve. Measured over the delivered images: 72.9% of the living room above sRGB 225
	 * with an interquartile range of 14 levels, and the corridor 90.3% above it with an IQR of 6 -
	 * a whole room living inside six levels. Every contrast the milestone bought landed where the
	 * curve is flat, so the tile grout measured four to eight levels out of 232 and the flat read as
	 * a white box. The finishes had been tuned against renders that could not show what was being
	 * tuned.
	 *
	 * At 3.0 the same two shots sit at a median of 198 and 202 with IQRs of 26 and 16: walls in the
	 * 190-200 band a real emulsion occupies, and roughly double the contrast to carry the detail.
	 *
	 * So this number is not free to drift from the material library. If the albedo set moves again,
	 * re-measure - a histogram of one interior shot is the whole test, and it takes a minute.
	 */
	void ApplyAmbientFill(FPostProcessSettings& Settings)
	{
		static const TCHAR* CubemapPath = TEXT("/Engine/MapTemplates/Sky/DaylightAmbientCubemap.DaylightAmbientCubemap");

		if (UTextureCube* Cubemap = LoadObject<UTextureCube>(nullptr, CubemapPath))
		{
			Settings.AmbientCubemap = Cubemap;
			Settings.bOverride_AmbientCubemapIntensity = 1;
			Settings.AmbientCubemapIntensity = 3.0f;
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
