// Copyright Siddartha G. All Rights Reserved.

#include "Model/HFArticulation.h"

FVector FHFPartMotion::UnitAxis() const
{
	// A zero axis would give a garbage quaternion that looks like a random rotation rather than
	// like an error, so fall back to something harmless and obvious.
	const FVector Normalised = Axis.GetSafeNormal();
	return Normalised.IsNearlyZero() ? FVector::ZAxisVector : Normalised;
}

FTransform FHFPartMotion::OffsetAt(double OpenAmount) const
{
	const double Alpha = FMath::Clamp(OpenAmount, 0.0, 1.0);

	switch (Type)
	{
	case EHFMotionType::Hinge:
		return FTransform(FQuat(UnitAxis(), FMath::DegreesToRadians(MaxAngleDegrees * Alpha)));

	case EHFMotionType::Slide:
		return FTransform(UnitAxis() * (MaxTravelCm * Alpha));

	default:
		return FTransform::Identity;
	}
}

FVector FHFPartMotion::SweptLocalPoint(const FVector& LocalPoint, double OpenAmount) const
{
	return OffsetAt(OpenAmount).TransformPosition(LocalPoint);
}

FTransform FHFPartState::PoseAt(double InOpenAmount) const
{
	// Offset first, in the part's own space, then the pivot placing that space in the actor.
	// UE composes left-to-right: (A * B) applies A and then B.
	return Motion.OffsetAt(InOpenAmount) * PivotTransform;
}
