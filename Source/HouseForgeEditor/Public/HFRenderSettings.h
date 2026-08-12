// Copyright Siddartha G. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "HFRenderSettings.generated.h"

/**
 * What a capture does when the flat is not in the Lumen scene.
 *
 * Not a taste. The default is Refuse because the failure it guards is the one a human cannot catch:
 * the broken configuration renders BRIGHTER than the correct one - measured whole-frame luminance
 * 0.260 unbaked against 0.083 baked, from identical lighting - because unoccluded sky floods
 * straight through walls Lumen cannot see. A wrong render looks bright and cheerful, gets acted on,
 * and nothing anywhere says otherwise. See Saved/Review/lumen/ and Docs/LumenAndTheBake.md.
 */
UENUM(BlueprintType)
enum class EHFLumenGuard : uint8
{
	/** Refuse to write the image, and say what to bake. The default for anything lit. */
	Refuse,

	/**
	 * Log the whole refusal at Warning and render anyway.
	 *
	 * The setting that makes measuring the broken state possible - the comparison in
	 * Saved/Review/lumen needs to be allowed to render exactly the configuration this exists to
	 * stop. Not a way to make the message go away: it says everything Refuse would have said.
	 */
	Warn,

	/** Do not check. Correct for a plan: an orthographic line drawing has no indirect light in it. */
	Off
};

/**
 * Whether generating a house also bakes it.
 *
 * THE DECISION AND THE REASONING ARE IN Docs/LumenAndTheBake.md; this is the control it describes.
 */
UENUM(BlueprintType)
enum class EHFBakeOnBuild : uint8
{
	/**
	 * A freshly built house draws its live dynamic meshes and is not baked. THE DEFAULT.
	 *
	 * Chosen deliberately, against the pull of "baking is now required for a correct render", for
	 * four reasons - the fourth being the one that makes the other three safe:
	 *
	 *  1. Generation is iterative. The reference flat is ~150 elements and ~350 parts, and each part
	 *     is one static mesh asset built, compiled and written to the project's Content folder. The
	 *     usual next action after a build is another build.
	 *  2. Baking changes what the Modeling Tools edit. Measured in
	 *     HouseForge.Bake.Probe.ToolTargetSelection: in Baked mode the tool target is the baked
	 *     ASSET. bUnbakeOnHandEdit catches it, but a plugin whose premise is "generated geometry
	 *     stays artist-editable" should not put every element into that state unasked.
	 *  3. Assets are user output (.claude/rules/01-scope.md). Creating 350 of them as a silent side
	 *     effect of "apply this spec" is a surprising write to somebody else's project.
	 *  4. The reason to auto-bake anyway - that an unbaked render is silently, attractively wrong -
	 *     no longer holds, because the capture path REFUSES rather than rendering it. Once the
	 *     failure is loud, the default can be the one that is right for editing.
	 */
	Never,

	/**
	 * Bake every element as soon as the house is built.
	 *
	 * For an unattended pipeline whose output is renders rather than edits, where paying the bake
	 * once at the end of a build is cheaper than discovering a refusal at capture time.
	 */
	Always
};

/**
 * The two policy values, resolved, as something that cannot be null.
 *
 * The same shape as FHFBuildDefaults and for the same reason: a caller reading a policy is on a path
 * that must not crash because a CDO does not exist yet, and a default-constructed value here is
 * exactly what the settings class declares anyway.
 */
struct FHFRenderPolicy
{
	EHFLumenGuard LumenGuard = EHFLumenGuard::Refuse;
	EHFBakeOnBuild BakeOnBuild = EHFBakeOnBuild::Never;
};

/**
 * Rendering and bake policy: the two choices that decide whether a HouseForge render can be trusted.
 *
 * Separate from UHFSettings, and deliberately so. That page holds figures that describe the
 * BUILDING - board thicknesses, cornice profiles, shelf spans - and is read by the runtime module
 * while it generates geometry. Nothing here describes a building. These are choices about the
 * editor's behaviour: whether generating also bakes, and what a capture does when it cannot be
 * correct. Neither is reachable from, or meaningful to, a cooked runtime.
 *
 * Filed alongside UHFSettings under Project Settings > Plugins rather than in Editor Preferences,
 * for the reason that page gives: a render policy that one artist has and another does not produces
 * two different-looking renders of the same flat and no way to tell which is the honest one.
 */
UCLASS(config = HouseForge, defaultconfig, meta = (DisplayName = "HouseForge Rendering"))
class HOUSEFORGEEDITOR_API UHFRenderSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UHFRenderSettings();

	/**
	 * What a lit capture does when the geometry in the picture cannot contribute indirect light.
	 *
	 * Leave this at Refuse. It is the only mechanical defence against a render that is wrong in the
	 * flattering direction, and the message it produces names the elements and the remedy.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Lumen")
	EHFLumenGuard LumenGuard = EHFLumenGuard::Refuse;

	/**
	 * Whether building a house from a spec also bakes it.
	 *
	 * Never by default. See EHFBakeOnBuild::Never for the four reasons, and
	 * Docs/LumenAndTheBake.md for the decision written out with the counter-argument.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Bake")
	EHFBakeOnBuild BakeOnBuild = EHFBakeOnBuild::Never;

	/** The settings, or the compiled-in defaults if the CDO does not exist yet. */
	static FHFRenderPolicy Policy();
};

/**
 * Forces the Lumen guard to one value for the duration of a scope.
 *
 * ## What it is for, and the narrower thing to reach for first
 *
 * Not every render is a render of the lighting, and a caller that knows its picture is not being
 * judged on its light says so on the REQUEST: `Request.LumenGuard = EHFLumenGuard::Off`. That is what
 * the plan capture does, and what HouseForge.Materials.TilingIsInMillimetres does - an orthographic
 * top-down of one floor with the sky off, in which what is counted is the period of a joint pattern.
 * The capture reads the request and nothing else, so that is the direct and preferable route.
 *
 * THIS SCOPE IS FOR THE CASE WHERE THE REQUEST IS NOT YOURS TO SET.
 * UHFEditorSubsystem::CaptureView builds the request on the caller's behalf and fills LumenGuard from
 * the project policy, so a caller reaching CaptureView has no field to write - and
 * HouseForge.Capture.NoViewportIsNeededAndNoneIsAskedFor is exactly that: it photographs a
 * seven-element stand-in to prove a capture needs no viewport, was correctly refused the moment the
 * guard went in, and never looks at a pixel's brightness.
 *
 * Baking the stand-in instead would put a several-hundred-asset write into a test about whether a
 * capture needs a window.
 *
 * A scope rather than a bare flag, on the pattern of FHFBakeSaveScope and for the same reason: a
 * caller that returned early having switched the guard off would license every capture after it, in
 * precisely the runs nobody watches.
 *
 * WHAT THIS MUST NEVER BE USED FOR is a capture whose subject is the lighting. The test that proves
 * the guard fires - HouseForge.Lumen.TheCaptureRefusesAnUnbakedLitView - does not use it, and would
 * be meaningless if it did.
 *
 * A scope guard rather than a bare flag because a test that failed early and left the guard switched
 * off would silently change what every capture after it was allowed to draw.
 */
struct HOUSEFORGEEDITOR_API FHFLumenGuardScope
{
	explicit FHFLumenGuardScope(EHFLumenGuard InGuard);
	~FHFLumenGuardScope();

	FHFLumenGuardScope(const FHFLumenGuardScope&) = delete;
	FHFLumenGuardScope& operator=(const FHFLumenGuardScope&) = delete;

private:
	bool bPreviousHasOverride = false;
	EHFLumenGuard PreviousGuard = EHFLumenGuard::Refuse;
};
