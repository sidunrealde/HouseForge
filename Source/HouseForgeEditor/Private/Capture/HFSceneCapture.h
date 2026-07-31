// Copyright Siddartha G. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class AActor;
class UMaterialInterface;
class UWorld;

/** One offscreen render: where the camera is, what it sees, and how big the image is. */
struct FHFCaptureRequest
{
	FVector Location = FVector::ZeroVector;
	FRotator Rotation = FRotator::ZeroRotator;

	/** Orthographic when true - a plan. Perspective otherwise - a view of a room. */
	bool bOrthographic = false;

	/** World width the image spans, in centimetres. Orthographic only. */
	double OrthoWidth = 1000.0;

	/** Horizontal field of view in degrees. Perspective only. */
	double FieldOfViewDegrees = 60.0;

	int32 Width = 2048;
	int32 Height = 2048;

	/**
	 * When non-empty, only these actors are rendered.
	 *
	 * How a plan is drawn without touching the house: the sectioned copy goes in here, and the
	 * uncut original is simply never in the picture.
	 */
	TArray<AActor*> ShowOnly;

	/**
	 * Draw the sky and atmospheric fog.
	 *
	 * Off for a plan, where a sky behind the cut would be a bright rectangle around the flat and
	 * the drawing it is compared against has a blank background. On for an interior view, where
	 * what comes through a window is part of what is being judged.
	 */
	bool bShowSky = true;

	/** Absolute path of the PNG to write. */
	FString OutputPath;
};

/**
 * Renders a frame with no editor viewport involved, and writes it to a PNG.
 *
 * The tool this replaces read the level editor's back buffer with FViewport::ReadPixels, which
 * means it borrowed a viewport that a human had to be looking at: minimise the editor, occlude its
 * window, or run it with no window at all and the read returns the wrong pixels or none. That is
 * the wrong shape for a tool whose entire purpose is letting Claude check its own work.
 *
 * A USceneCaptureComponent2D into a UTextureRenderTarget2D has none of that coupling. It renders
 * to memory this code owns, at exactly the resolution asked for rather than at whatever size the
 * user left the viewport, and it neither reads nor disturbs anything on screen - no borrowing the
 * camera and handing it back, which the old implementation had to do so as not to leave the user
 * staring at a top-down view they never asked for.
 *
 * What it cannot do is render without a renderer. Under -nullrhi - which is how the validation gate
 * runs the whole suite - there is no rendering at all, and CanRender says so with the reason rather
 * than writing a black PNG that looks like a captured image of an unlit house.
 *
 * Nor will it render before the materials are ready. See EnsureMaterialsReady: a capture that draws
 * with a half-compiled material produces a confident image of the wrong thing, which is a worse
 * outcome than either a refusal or a wait.
 */
class FHFSceneCapture
{
public:
	/**
	 * Whether this process can render anything at all.
	 *
	 * @param OutWhyNot  Filled in with what to change when the answer is no.
	 */
	static bool CanRender(FString& OutWhyNot);

	/**
	 * Every distinct material that would be drawn by this request, in the order first encountered.
	 *
	 * Honours ShowOnly, because that is what decides what is in the picture: a plan draws the
	 * sectioned copy and nothing else, so the uncut house's materials are irrelevant to it.
	 *
	 * Separated out from EnsureMaterialsReady so that WHICH materials get checked is testable
	 * without a renderer - the checking itself needs one, this does not.
	 */
	static TArray<UMaterialInterface*> GatherRenderedMaterials(UWorld* World, const FHFCaptureRequest& Request);

	/**
	 * Blocks until every material in the picture has a compiled shader map, and refuses if one
	 * cannot be given one.
	 *
	 * A material whose shader map is not ready does not delay the frame - the renderer silently
	 * substitutes DefaultMaterial for it and carries on, so the capture succeeds and writes a
	 * plausible PNG in grey checkerboard. That is the failure this exists to stop: a whole review
	 * package came back showing the default material while the MI_HF_* instances were correctly
	 * assigned to all sixteen slots of every component, and nothing anywhere said so.
	 *
	 * @param OutWhyNot  Filled in with the materials that could not be made ready.
	 */
	static bool EnsureMaterialsReady(UWorld* World, const FHFCaptureRequest& Request, FString& OutWhyNot);

	/**
	 * Renders and writes the PNG.
	 *
	 * @param OutSize   Pixel size actually written.
	 * @param OutError  Filled in on failure, phrased as what to do about it.
	 */
	static bool Render(UWorld* World, const FHFCaptureRequest& Request, FIntPoint& OutSize, FString& OutError);
};
