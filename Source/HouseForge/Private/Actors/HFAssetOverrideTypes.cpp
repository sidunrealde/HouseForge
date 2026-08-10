// Copyright Siddartha G. All Rights Reserved.

#include "Actors/HFAssetOverrideTypes.h"

namespace
{
	/**
	 * The box an asset occupies once its author's orientation has been corrected.
	 *
	 * Rotating a box is not rotating its extents: a 200 x 60 wardrobe turned 90 degrees is 60 x 200,
	 * and turned 45 it is 184 x 184. Taking the eight corners through the rotation and re-bounding
	 * them is the only thing that is right for every angle, and the fit divides by these numbers, so
	 * an approximation here is a wrong scale rather than a slightly wrong one.
	 */
	FBox RotateBox(const FBox& Box, const FRotator& Rotation)
	{
		if (!Box.IsValid || Rotation.IsNearlyZero())
		{
			return Box;
		}

		const FQuat Quat = Rotation.Quaternion();
		FVector Corners[8];
		Box.GetVertices(Corners);

		FBox Out(ForceInit);
		for (const FVector& Corner : Corners)
		{
			Out += Quat.RotateVector(Corner);
		}
		return Out;
	}

	/** Guards every division in the solver. A zero-thickness axis is real - a mirror is one. */
	double SafeRatio(double Target, double Source)
	{
		return (FMath::Abs(Source) < UE_KINDA_SMALL_NUMBER) ? 1.0 : (Target / Source);
	}
}

FHFAssetFitResult FHFAssetFit::Solve(const FBox& GeneratedLocal, const FBox& AssetLocal,
	const FHFAssetOverride& Override)
{
	FHFAssetFitResult Result;

	if (!GeneratedLocal.IsValid || !AssetLocal.IsValid)
	{
		Result.Note = TEXT("Nothing to fit: the generated element or the asset has no bounds. A freshly "
			"loaded static mesh has empty bounds until its build finishes - see "
			"FStaticMeshCompilingManager::FinishCompilation.");
		return Result;
	}

	// THE SCALE IS WORKED OUT IN THE ASSET'S OWN AXES, AND THE TARGET IS BROUGHT TO IT.
	//
	// FTransform(Quat, Translation, Scale) evaluates as Quat * (Scale * v) + Translation, so the
	// scale multiplies the asset's LOCAL axes and the rotation happens afterwards. That ordering is
	// the one that cannot shear - a per-axis scale applied in world axes after a rotation is a shear
	// for any angle that is not a multiple of 90 degrees - but it means the scale factors have to be
	// expressed in the asset's frame, not the world's.
	//
	// Getting that backwards is invisible at 0 degrees and catastrophic at 90: a wardrobe authored
	// along Y and yawed into place would take the width correction on its depth axis and come out a
	// quarter of the width of the recess, while every number in the preview looked plausible. Found
	// by HouseForge.Assets.Fit.RotationIsMeasuredNotAssumed, not by reading.
	//
	// So the GENERATED box is rotated by the INVERSE offset to express the target in the asset's
	// frame, and the ratios are taken there.
	const FBox TargetInAssetFrame = RotateBox(GeneratedLocal, Override.AssetRotationOffset.GetInverse());

	const FVector GeneratedSize = GeneratedLocal.GetSize();
	const FVector TargetSize = TargetInAssetFrame.GetSize();
	const FVector AssetLocalSize = AssetLocal.GetSize();

	Result.GeneratedSize = GeneratedSize;

	// Reported in WORLD axes, because that is the shape the user is looking at: a 55 x 180 asset
	// yawed into place reads as 180 wide in the viewport and should read as 180 wide in the panel.
	Result.AssetSize = RotateBox(AssetLocal, Override.AssetRotationOffset).GetSize();

	if (AssetLocalSize.GetMax() < UE_KINDA_SMALL_NUMBER)
	{
		Result.Note = TEXT("Nothing to fit: the asset's bounding box is empty.");
		return Result;
	}

	FVector Scale = FVector::OneVector;

	switch (Override.FitMode)
	{
	case EHFAssetFitMode::KeepAssetSize:
		break;

	case EHFAssetFitMode::UniformFit:
	{
		// The LARGEST factor that still fits, which is the smallest of the three per-axis ratios. An
		// axis the generated box has no thickness on - a mirror is 2 cm deep and an asset for it may
		// be 4 - would otherwise drive the whole object down to nothing, so a degenerate axis is
		// excluded from the minimum rather than allowed to decide it.
		double Smallest = TNumericLimits<double>::Max();
		bool bAnyAxis = false;
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			if (TargetSize[Axis] < 1.0 || AssetLocalSize[Axis] < UE_KINDA_SMALL_NUMBER)
			{
				continue;
			}
			Smallest = FMath::Min(Smallest, TargetSize[Axis] / AssetLocalSize[Axis]);
			bAnyAxis = true;
		}

		if (bAnyAxis)
		{
			Scale = FVector(Smallest);
		}
		else
		{
			Result.Note = TEXT("The generated element has no axis longer than a centimetre, so there is "
				"nothing to fit into. Placed at the asset's own size.");
		}
		break;
	}

	case EHFAssetFitMode::StretchToFootprint:
		Scale = FVector(
			SafeRatio(TargetSize.X, AssetLocalSize.X),
			SafeRatio(TargetSize.Y, AssetLocalSize.Y),
			SafeRatio(TargetSize.Z, AssetLocalSize.Z));
		break;

	case EHFAssetFitMode::FitPlanKeepHeight:
		// Z LEFT AT ONE, not derived from X and Y. The height of a base unit is an ergonomic figure
		// the manufacturer got right; the whole reason this mode is separate from StretchToFootprint
		// is to leave it alone.
		// Z is the ASSET's own up axis here, which is what "keep the authored height" means. For the
		// yaw-only offsets this field is actually used with, the asset's Z is the world's Z too.
		Scale = FVector(
			SafeRatio(TargetSize.X, AssetLocalSize.X),
			SafeRatio(TargetSize.Y, AssetLocalSize.Y),
			1.0);
		break;
	}

	// A negative or zero scale flips the winding and inverts the normals, which reads as an object
	// lit from inside. Nothing above can produce one from valid boxes, but the clamp is here because
	// the boxes come from assets and an asset can be anything.
	Scale.X = FMath::Max(Scale.X, UE_KINDA_SMALL_NUMBER);
	Scale.Y = FMath::Max(Scale.Y, UE_KINDA_SMALL_NUMBER);
	Scale.Z = FMath::Max(Scale.Z, UE_KINDA_SMALL_NUMBER);

	// WHERE THE ASSET ACTUALLY ENDS UP, measured rather than derived: the scaled box taken through
	// the rotation and re-bounded. Deriving it from the extents would repeat the very mistake this
	// function exists to avoid, and the translation below divides by nothing, so it has to be right.
	const FBox ScaledAsset(AssetLocal.Min * Scale, AssetLocal.Max * Scale);
	const FBox PlacedAsset = RotateBox(ScaledAsset, Override.AssetRotationOffset);
	const FVector FittedSize = PlacedAsset.GetSize();

	// WHERE IT LANDS: centred on the generated box in plan, standing on its base.
	//
	// Stated as one rule for everything rather than inferred per fixture type, because an inferred
	// anchor is wrong SILENTLY and this one is wrong visibly - a wall-hung asset narrower than the
	// box it replaces floats off the wall by half the slack, the preview reports that slack as a
	// number, and FHFAssetOverride::AdditionalOffset closes it. Min-Z rather than centre-Z because
	// almost everything in a flat stands on something: a sofa on the floor, a wall cabinet on its
	// drawn underside, a WC on the tiles.
	const FVector GeneratedCentre = GeneratedLocal.GetCenter();

	FVector Translation;
	Translation.X = GeneratedCentre.X - PlacedAsset.GetCenter().X;
	Translation.Y = GeneratedCentre.Y - PlacedAsset.GetCenter().Y;
	Translation.Z = GeneratedLocal.Min.Z - PlacedAsset.Min.Z;
	Translation += Override.AdditionalOffset;

	Result.RelativeTransform = FTransform(Override.AssetRotationOffset.Quaternion(), Translation, Scale);
	Result.FittedSize = FittedSize;
	Result.Slack = GeneratedSize - FittedSize;
	Result.bValid = true;

	// One number for "how much is this being distorted". A ratio never below 1, so 0.75 and 1.33 are
	// both reported as 1.33 - the user cares how far from the authored shape it is, not which way.
	double Worst = 1.0;
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		const double Ratio = Scale[Axis];
		Worst = FMath::Max(Worst, Ratio > UE_KINDA_SMALL_NUMBER ? FMath::Max(Ratio, 1.0 / Ratio) : 1.0);
	}
	Result.WorstAxisRatio = Worst;

	return Result;
}
