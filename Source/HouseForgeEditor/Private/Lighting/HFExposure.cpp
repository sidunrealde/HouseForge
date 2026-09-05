// Copyright Siddartha G. All Rights Reserved.

#include "Lighting/HFExposure.h"

#include "Engine/Scene.h"

float FHFExposure::IsoFor(float EV100)
{
	// ISO = N^2 * t^-1 * 100 / 2^EV100, rearranged from EV100 = log2(N^2 / t * 100 / ISO).
	return FMath::Max(1.0f,
		Fstop() * Fstop() * ShutterDenominator() * 100.0f / FMath::Pow(2.0f, EV100));
}

void FHFExposure::PinTo(FPostProcessSettings& Settings, float EV100)
{
	// EVERY bOverride_ BIT, or the field beside it is simply ignored and the volume silently
	// contributes nothing. There is no warning for a value written without its override.
	Settings.bOverride_AutoExposureMethod = 1;
	Settings.AutoExposureMethod = AEM_Manual;

	Settings.bOverride_AutoExposureApplyPhysicalCameraExposure = 1;
	Settings.AutoExposureApplyPhysicalCameraExposure = 1;

	Settings.bOverride_AutoExposureBias = 1;
	Settings.AutoExposureBias = 0.0f;

	Settings.bOverride_CameraISO = 1;
	Settings.CameraISO = IsoFor(EV100);

	Settings.bOverride_CameraShutterSpeed = 1;
	Settings.CameraShutterSpeed = ShutterDenominator();

	Settings.bOverride_DepthOfFieldFstop = 1;
	Settings.DepthOfFieldFstop = Fstop();
}
