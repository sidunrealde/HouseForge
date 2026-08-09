// Copyright Siddartha G. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class AHFElementActor;
class UPrimitiveComponent;
class UWorld;

/**
 * What Lumen will do with one drawn primitive, decided on the game thread from the same facts the
 * renderer decides it from.
 *
 * Ordered so that the two acceptable answers come first: everything from NoCards downwards is an
 * element that will not put light into the room.
 */
enum class EHFLumenVerdict : uint8
{
	/**
	 * In the Lumen scene, with mesh cards, capturable into the surface cache. What a bake produces.
	 */
	Radiant,

	/**
	 * In the Lumen scene and correctly card-less: its largest bounding-box face is under
	 * r.LumenScene.SurfaceCache.MeshCardsMinSize squared, so FLumenSceneData::AddMeshCardsFromBuildData
	 * rejects it (LumenMeshCards.cpp:1004-1007).
	 *
	 * NOT A FAULT, and the guard must never report it as one. A 10 x 10 cm face is the threshold, and
	 * a flat is full of legitimately smaller things - handles, hinge knuckles, a tap spout. Baking
	 * them harder does not help; the engine will not card them at any size of asset.
	 */
	TooSmallForCards,

	/**
	 * A UDynamicMeshComponent. Traced under hardware ray tracing, radiant under neither path.
	 *
	 * This is the verdict the whole milestone exists for. FBaseDynamicMeshSceneProxy hardcodes
	 * bSupportsDistanceFieldRepresentation and bAffectDistanceFieldLighting false
	 * (BaseDynamicMeshSceneProxy.cpp:65-68), so software tracing cannot see it at all; hardware
	 * tracing submits its triangles as a BLAS and the proxy even allocates six bounding-box cards,
	 * but card capture iterates PrimitiveSceneInfo->StaticMeshRelevances
	 * (LumenSceneCardCapture.cpp:775) and a dynamic mesh on the default DynamicDraw path emits no
	 * static batches, so those cards are never filled.
	 */
	LiveDynamicMesh,

	/**
	 * A static mesh built with DistanceFieldResolutionScale at zero, which kills the distance field -
	 * and the mesh card build is chained off the distance field build (DistanceFieldAtlas.cpp:296,
	 * :1050), so it kills the cards too. UE::AssetUtils writes that zero when
	 * FStaticMeshAssetOptions::bAllowDistanceField is false (CreateStaticMeshUtil.cpp:82).
	 */
	NoDistanceField,

	/** bAffectDynamicIndirectLighting or bAffectDistanceFieldLighting cleared on the component. */
	IndirectLightingOff,

	/**
	 * A transform Lumen refuses to card: sheared, or with a zero axis.
	 * FLumenSceneData::AddMeshCardsFromBuildData requires IsMatrixOrthogonal(LocalToWorld).
	 */
	NonOrthogonalTransform,

	/** A primitive class this inspector does not know how to answer for. Counted against the flat. */
	Unknown
};

/** One drawn primitive and what Lumen will do with it. */
struct FHFLumenPrimitive
{
	TWeakObjectPtr<const UPrimitiveComponent> Component;

	/** The element that owns it, and which of its components this is - enough to go and look. */
	FName ElementId;
	FString ComponentName;
	FString ElementClass;

	EHFLumenVerdict Verdict = EHFLumenVerdict::Unknown;

	/** World-space bounding-box surface area, the weight used for "how much of the flat". */
	double SurfaceAreaCm2 = 0.0;

	/** Largest bounding-box face, the figure the engine's card threshold is applied to. */
	double LargestFaceAreaCm2 = 0.0;

	FString Describe() const;
};

/**
 * Whether the flat is in the Lumen scene, and what is missing if it is not.
 *
 * ## Why this measures radiance rather than membership
 *
 * FPrimitiveSceneProxy::UpdateVisibleInLumenScene sets bVisibleInLumenScene from
 * AffectsDynamicIndirectLighting() && bCanBeTraced, and under hardware ray tracing a
 * UDynamicMeshComponent satisfies both - it has a ray tracing representation. So a guard that asked
 * only "is it in the Lumen scene" would pass configuration B of Saved/Review/lumen, which was
 * MEASURED to contribute almost no bounce: whole-frame luminance 0.030 against 0.083 baked, from
 * identical lighting. Membership gets a primitive a group; radiance comes from mesh cards captured
 * into the surface cache, and only a static mesh gets those.
 *
 * The verdict this reports is therefore "will this surface put light into the room", which is the
 * question a render actually depends on.
 */
struct FHFLumenCoverageReport
{
	/** Element actors examined. */
	int32 ElementsSeen = 0;

	/** Primitives that are drawn in game, and so are candidates at all. */
	int32 PrimitivesDrawn = 0;

	int32 Radiant = 0;
	int32 TooSmall = 0;

	/** Drawn primitives that will put no light into the room. The number that decides the guard. */
	int32 Absent = 0;

	double AreaRadiantCm2 = 0.0;
	double AreaAbsentCm2 = 0.0;

	/** Every absent primitive, so a report can name the wall rather than count it. */
	TArray<FHFLumenPrimitive> Absentees;

	/** Absent primitives tallied by verdict, so one line can say what KIND of absence this is. */
	TMap<EHFLumenVerdict, int32> AbsentByVerdict;

	// ------------------------------------------------------------------ project-level facts
	//
	// A perfectly baked flat is still absent from the Lumen scene if the project does not build
	// distance fields, and that failure looks exactly like the one this milestone is about. Checked
	// here so the guard cannot be satisfied by a bake that could never have worked.

	bool bLumenIsTheGiMethod = false;
	bool bProjectGeneratesDistanceFields = false;
	bool bProjectGeneratesMeshCards = false;
	bool bHardwareRayTracing = false;

	/** Project settings that would defeat the bake, phrased as what to change. */
	TArray<FString> ProjectProblems;

	/**
	 * The share of CARDABLE surface area that will contribute radiance. 1.0 on a fully baked flat.
	 *
	 * Primitives too small to card are in neither term, deliberately: they can never contribute
	 * radiance at any size of asset, so counting their area as missing would put a permanent ceiling
	 * on the figure and there would be no value that meant "correct". Measured on the reference flat,
	 * 96 of 410 drawn primitives are below the threshold - handles, hinge knuckles, tap spouts.
	 */
	double CoverageFraction() const;

	/**
	 * Whether this check has anything to say. False when the project is not using Lumen at all, in
	 * which case a dynamic mesh is not a correctness problem and the guard must stay quiet.
	 */
	bool IsApplicable() const { return bLumenIsTheGiMethod; }

	/** Nothing drawn will be invisible to Lumen, and nothing about the project prevents it. */
	bool IsCovered() const;

	/** One line: counts, area share and the tracing path. */
	FString Summary() const;

	/**
	 * The loud version, naming what is absent and what to do - or empty when covered.
	 *
	 * Written to be read by whoever is holding the wrong render, so it leads with the fact that the
	 * broken configuration is the BRIGHTER one. A human looking for a dark, obviously-broken image
	 * will not find one.
	 */
	FString WhyNot() const;
};

/**
 * Answers "is the flat in the Lumen scene" without a renderer, from the asset and component facts.
 *
 * ## Why not ask the renderer
 *
 * Because the answer is needed where the renderer cannot be asked: the validation gate runs the
 * whole suite under -nullrhi, where there is no FScene, no FLumenSceneData and no distance field
 * scene to count. A guard that only worked when a frame was being drawn would be untested in the
 * only run that gates a merge, and the failure it exists to catch is the one nobody sees.
 *
 * Everything consulted here is game-thread data the renderer derives its own answer from - the
 * component's lighting flags and transform, the asset's build settings, and the project cvars - so
 * the two agree by construction rather than by luck. Where they could drift, the test suite pins
 * the engine line numbers this was written from.
 */
class FHFLumenCoverage
{
public:
	/** Walks every HouseForge element in the level and decides each drawn primitive. */
	static void Inspect(UWorld* World, FHFLumenCoverageReport& OutReport);

	/** Just the element actors given, for a selection rather than a level. */
	static void InspectElements(TArrayView<AHFElementActor* const> Elements, FHFLumenCoverageReport& OutReport);

	/** The verdict for one drawn primitive. Public so a test can pin one component's answer. */
	static EHFLumenVerdict Judge(const UPrimitiveComponent* Primitive, double& OutLargestFaceAreaCm2);

	/**
	 * Whether this primitive is drawn in game, which is the term the Lumen predicate uses.
	 *
	 * IsDrawnInGame() || AffectsIndirectLightingWhileHidden(), exactly as
	 * FPrimitiveSceneProxy::UpdateVisibleInLumenScene reads it. Not editor visibility: the dynamic
	 * twin of a baked element is hidden in game as well as in the editor, and that is precisely what
	 * takes it out of the Lumen scene and stops it double-counting.
	 */
	static bool IsCandidate(const UPrimitiveComponent* Primitive);

	/** r.LumenScene.SurfaceCache.MeshCardsMinSize squared, the engine's card threshold in cm2. */
	static double CardMinFaceAreaCm2();

	static const TCHAR* VerdictName(EHFLumenVerdict Verdict);
};
