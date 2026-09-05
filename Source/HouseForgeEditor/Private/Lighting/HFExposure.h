// Copyright Siddartha G. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FPostProcessSettings;

/**
 * Pinning exposure to a stated EV100, with no eye adaptation anywhere in the path.
 *
 * EXTRACTED SO THERE IS ONE COPY. This arithmetic was written once inside HFViewingLight.cpp, in an
 * anonymous namespace, with a long comment explaining two traps in it. The lighting milestone needed
 * exactly the same function, and copying it would have put the same two traps in two files, where
 * they would drift the first time somebody fixed one of them.
 *
 * ## Why the physical camera and not AutoExposureMinBrightness
 *
 * AutoExposureMinBrightness and MaxBrightness are the other way of pinning exposure, and they are
 * read as EV100 only when the project has bExtendDefaultLuminanceRange on - and as raw linear
 * brightness when it does not. The same numbers would therefore mean two entirely different
 * exposures depending on a project ini this plugin is not allowed to edit
 * (.claude/rules/01-scope.md). Shutter, ISO and aperture mean the same thing either way.
 *
 * ## Why the ISO carries the figure and not the aperture
 *
 * EV100 = log2(N^2 / t * 100 / ISO). The aperture is fixed and the ISO carries the figure, which is
 * the opposite of the obvious arrangement and is the point: the engine clamps
 * DepthOfFieldFstop to f/1 through f/32, and with the shutter at 1/100 that bounds an
 * aperture-carried EV100 to roughly 6.6 through 16.6. Asking for anything below 6.6 silently
 * clamps, so three different exposures come out identical - which is exactly what happened while
 * the placeholder rig was being measured, and it read as "exposure has no effect" rather than as a
 * clamp. ISO has no such ceiling, and the interior figure this plugin actually uses is below 6.6.
 */
class FHFExposure
{
public:
	/** Sets method, physical camera and every bOverride_ bit needed for the value to be read. */
	static void PinTo(FPostProcessSettings& Settings, float EV100);

	/** The ISO PinTo would use for this EV100. Published so a test can check the arithmetic. */
	static float IsoFor(float EV100);

	/** Fixed aperture. See the class comment for why this is not what carries the figure. */
	static float Fstop() { return 2.0f; }

	/** Fixed shutter, as a denominator: 100 means 1/100 s. */
	static float ShutterDenominator() { return 100.0f; }
};
