// Copyright Siddartha G. All Rights Reserved.

//
// The fit solver: where a Content Browser asset lands when it stands in for a generated fixture.
//
// Pure, so these run with no world, no actor, no editor and no asset - which is the same reason
// .claude/rules/04-conventions.md holds generators to `(params) -> FDynamicMesh3`. Every number the
// panel shows on a preview and every transform a component gets comes through FHFAssetFit::Solve, so
// what is asserted here is what the user sees and what lands.
//
// Written against the failure modes rather than the happy path:
//
//   * a rotated asset is measured ROTATED, not by its unrotated extents    RotationIsMeasuredNotAssumed
//   * the order rotate-then-scale, because the reverse shears              NonUniformScaleDoesNotShear
//   * an object standing on the floor still stands on it                   ThingsStandOnTheirBase
//   * a degenerate axis cannot drive a uniform fit to nothing              AThinAxisDoesNotCollapseTheFit
//   * the distortion figure the user is shown is symmetric                 DistortionIsReportedBothWays
//   * nothing produces a mirrored or zero scale                            ScaleIsNeverZeroOrNegative
//

#include "HouseForge.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Actors/HFAssetOverrideTypes.h"
#include "Misc/AutomationTest.h"

#define HF_TEST_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace HFAssetFitTest
{
	/** A wardrobe-shaped generated box: 200 wide, 60 deep, 240 tall, standing on z = 0. */
	FBox Generated()
	{
		return FBox(FVector(0.0, 0.0, 0.0), FVector(200.0, 60.0, 240.0));
	}

	/** The box an asset occupies once the solver has placed it. */
	FBox Placed(const FBox& AssetLocal, const FHFAssetFitResult& Fit)
	{
		FVector Corners[8];
		AssetLocal.GetVertices(Corners);

		FBox Out(ForceInit);
		for (const FVector& Corner : Corners)
		{
			Out += Fit.RelativeTransform.TransformPosition(Corner);
		}
		return Out;
	}
}

/**
 * A stretch-to-footprint fit fills the generated box exactly, whatever pivot the author used.
 *
 * THE PIVOT IS THE POINT. An asset modelled around its centre and one modelled from its base corner
 * are the same object to a person and completely different boxes to this code, and getting it wrong
 * puts a wardrobe half underground. Both are asserted here against the same target.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFAssetFitFillsTheBoxTest,
	"HouseForge.Assets.Fit.StretchFillsTheGeneratedBox", HF_TEST_FLAGS)

bool FHFAssetFitFillsTheBoxTest::RunTest(const FString& Parameters)
{
	FHFAssetOverride Override;
	Override.FitMode = EHFAssetFitMode::StretchToFootprint;

	// Pivot at the base corner, like a piece of joinery.
	{
		const FBox Asset(FVector(0.0, 0.0, 0.0), FVector(180.0, 55.0, 210.0));
		const FHFAssetFitResult Fit = FHFAssetFit::Solve(HFAssetFitTest::Generated(), Asset, Override);

		TestTrue(TEXT("The fit is valid"), Fit.bValid);

		const FBox Landed = HFAssetFitTest::Placed(Asset, Fit);
		TestTrue(TEXT("A base-pivoted asset fills the generated box exactly"),
			Landed.Min.Equals(HFAssetFitTest::Generated().Min, 1e-6)
			&& Landed.Max.Equals(HFAssetFitTest::Generated().Max, 1e-6));
	}

	// Pivot at the centre, like a downloaded prop - and deliberately off-centre in Z as well, which
	// is the case that a naive "translate by the difference of the centres" gets wrong.
	{
		const FBox Asset(FVector(-90.0, -27.5, -140.0), FVector(90.0, 27.5, 70.0));
		const FHFAssetFitResult Fit = FHFAssetFit::Solve(HFAssetFitTest::Generated(), Asset, Override);

		const FBox Landed = HFAssetFitTest::Placed(Asset, Fit);
		TestTrue(TEXT("A centre-pivoted asset fills the same box"),
			Landed.Min.Equals(HFAssetFitTest::Generated().Min, 1e-6)
			&& Landed.Max.Equals(HFAssetFitTest::Generated().Max, 1e-6));
	}

	return true;
}

/**
 * A rotation offset is measured through the rotation, not around it.
 *
 * A 180 x 55 asset turned 90 degrees is 55 x 180, and a solver that scaled by the UNROTATED extents
 * would divide 200 by 180 and 60 by 55 and produce something a quarter of the width of the recess
 * it is supposed to fill - while reporting a plausible-looking scale. Nothing about that is visible
 * in a wireframe.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFAssetFitRotationTest,
	"HouseForge.Assets.Fit.RotationIsMeasuredNotAssumed", HF_TEST_FLAGS)

bool FHFAssetFitRotationTest::RunTest(const FString& Parameters)
{
	// Authored along Y where HouseForge wants it along X: the commonest vendor-pack correction.
	const FBox Asset(FVector(-27.5, -90.0, 0.0), FVector(27.5, 90.0, 210.0));

	FHFAssetOverride Override;
	Override.FitMode = EHFAssetFitMode::StretchToFootprint;
	Override.AssetRotationOffset = FRotator(0.0, 90.0, 0.0);

	const FHFAssetFitResult Fit = FHFAssetFit::Solve(HFAssetFitTest::Generated(), Asset, Override);

	TestTrue(TEXT("The fit is valid"), Fit.bValid);
	TestNearlyEqual(TEXT("The asset is measured 180 wide AFTER the yaw, not 55"), Fit.AssetSize.X, 180.0, 1e-6);
	TestNearlyEqual(TEXT("...and 55 deep"), Fit.AssetSize.Y, 55.0, 1e-6);

	const FBox Landed = HFAssetFitTest::Placed(Asset, Fit);
	TestTrue(TEXT("A rotated asset still fills the generated box exactly"),
		Landed.Min.Equals(HFAssetFitTest::Generated().Min, 1e-5)
		&& Landed.Max.Equals(HFAssetFitTest::Generated().Max, 1e-5));

	return true;
}

/**
 * Rotation is applied BEFORE the non-uniform scale.
 *
 * The other order shears: scaling x by 1.1 and y by 1.5 and then yawing 45 degrees turns a
 * rectangular carcass into a parallelogram. It is a real, silent, geometry-corrupting bug and the
 * only way to see it is to measure a diagonal.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFAssetFitNoShearTest,
	"HouseForge.Assets.Fit.NonUniformScaleDoesNotShear", HF_TEST_FLAGS)

bool FHFAssetFitNoShearTest::RunTest(const FString& Parameters)
{
	const FBox Asset(FVector(0.0, 0.0, 0.0), FVector(100.0, 40.0, 200.0));

	FHFAssetOverride Override;
	Override.FitMode = EHFAssetFitMode::StretchToFootprint;
	Override.AssetRotationOffset = FRotator(0.0, 45.0, 0.0);

	const FHFAssetFitResult Fit = FHFAssetFit::Solve(HFAssetFitTest::Generated(), Asset, Override);

	// Two edges of the asset that are perpendicular in its own space. If the scale were applied in
	// world space after the rotation they would stop being perpendicular; applied before it, the
	// scale is in the asset's own axes and a right angle stays a right angle.
	const FVector Origin = Fit.RelativeTransform.TransformPosition(FVector(0.0, 0.0, 0.0));
	const FVector AlongX = Fit.RelativeTransform.TransformPosition(FVector(100.0, 0.0, 0.0)) - Origin;
	const FVector AlongY = Fit.RelativeTransform.TransformPosition(FVector(0.0, 40.0, 0.0)) - Origin;

	TestTrue(TEXT("Two edges that were perpendicular in the asset are still perpendicular"),
		FMath::Abs(FVector::DotProduct(AlongX.GetSafeNormal(), AlongY.GetSafeNormal())) < 1e-6);

	return true;
}

/**
 * Whatever the fit mode, the asset stands where the generated element stood.
 *
 * The alignment rule is centre-in-plan, base-in-Z, and it is stated as one rule for every fixture
 * rather than inferred per type - see FHFAssetFit::Solve. A sofa two thirds the size of the drawn
 * box still has its feet on the floor rather than floating at the centre of it.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFAssetFitStandsOnItsBaseTest,
	"HouseForge.Assets.Fit.ThingsStandOnTheirBase", HF_TEST_FLAGS)

bool FHFAssetFitStandsOnItsBaseTest::RunTest(const FString& Parameters)
{
	// Well under the generated box on every axis, so every mode has slack to place wrongly.
	const FBox Asset(FVector(-50.0, -15.0, 0.0), FVector(50.0, 15.0, 150.0));

	// A base at 40 rather than 0, so "stands on its base" is distinguishable from "sits at z = 0".
	const FBox Generated(FVector(0.0, 0.0, 40.0), FVector(200.0, 60.0, 240.0));

	const EHFAssetFitMode Modes[] = {
		EHFAssetFitMode::KeepAssetSize,
		EHFAssetFitMode::UniformFit,
		EHFAssetFitMode::FitPlanKeepHeight,
		EHFAssetFitMode::StretchToFootprint
	};

	for (const EHFAssetFitMode Mode : Modes)
	{
		FHFAssetOverride Override;
		Override.FitMode = Mode;

		const FHFAssetFitResult Fit = FHFAssetFit::Solve(Generated, Asset, Override);
		const FBox Landed = HFAssetFitTest::Placed(Asset, Fit);

		TestNearlyEqual(TEXT("The asset's underside is on the generated element's base"),
			Landed.Min.Z, Generated.Min.Z, 1e-6);
		TestNearlyEqual(TEXT("...and it is centred in plan on X"),
			Landed.GetCenter().X, Generated.GetCenter().X, 1e-6);
		TestNearlyEqual(TEXT("...and on Y"),
			Landed.GetCenter().Y, Generated.GetCenter().Y, 1e-6);
	}

	return true;
}

/**
 * FitPlanKeepHeight leaves the authored height alone. That is the whole reason it exists.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFAssetFitKeepsHeightTest,
	"HouseForge.Assets.Fit.PlanFitKeepsTheAuthoredHeight", HF_TEST_FLAGS)

bool FHFAssetFitKeepsHeightTest::RunTest(const FString& Parameters)
{
	// A base unit at the ergonomic 85, standing in a box the drawing says is 90 tall.
	const FBox Asset(FVector(0.0, 0.0, 0.0), FVector(180.0, 55.0, 85.0));
	const FBox Generated(FVector(0.0, 0.0, 0.0), FVector(200.0, 60.0, 90.0));

	FHFAssetOverride Override;
	Override.FitMode = EHFAssetFitMode::FitPlanKeepHeight;

	const FHFAssetFitResult Fit = FHFAssetFit::Solve(Generated, Asset, Override);
	const FBox Landed = HFAssetFitTest::Placed(Asset, Fit);

	TestNearlyEqual(TEXT("The plan is filled in X"), Landed.GetSize().X, 200.0, 1e-6);
	TestNearlyEqual(TEXT("...and in Y"), Landed.GetSize().Y, 60.0, 1e-6);
	TestNearlyEqual(TEXT("...and the worktop is still at the height the manufacturer put it"),
		Landed.GetSize().Z, 85.0, 1e-6);

	// And the slack is reported rather than swallowed, so a panel can say so.
	TestNearlyEqual(TEXT("The 5 cm of unfilled height is reported as slack"), Fit.Slack.Z, 5.0, 1e-6);

	return true;
}

/**
 * A generated element with a near-zero axis does not drive a uniform fit to nothing.
 *
 * A mirror is 2 cm deep and an asset for it may be 4. Taking the minimum over all three ratios
 * blindly would scale the whole mirror to half size to make its depth fit, which is absurd and is
 * exactly the kind of thing that looks like a units bug when it appears in a render.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFAssetFitThinAxisTest,
	"HouseForge.Assets.Fit.AThinAxisDoesNotCollapseTheFit", HF_TEST_FLAGS)

bool FHFAssetFitThinAxisTest::RunTest(const FString& Parameters)
{
	const FBox Generated(FVector(0.0, 0.0, 100.0), FVector(60.0, 0.5, 190.0));	// a mirror, 5 mm deep
	const FBox Asset(FVector(0.0, 0.0, 0.0), FVector(60.0, 4.0, 90.0));			// a framed one, 4 cm deep

	FHFAssetOverride Override;
	Override.FitMode = EHFAssetFitMode::UniformFit;

	const FHFAssetFitResult Fit = FHFAssetFit::Solve(Generated, Asset, Override);

	TestTrue(TEXT("The fit is valid"), Fit.bValid);
	TestTrue(TEXT("The 5 mm depth did not scale the mirror down by 8x"),
		Fit.RelativeTransform.GetScale3D().X > 0.9);

	const FBox Landed = HFAssetFitTest::Placed(Asset, Fit);
	TestTrue(TEXT("It is still a mirror-sized object"), Landed.GetSize().X > 50.0 && Landed.GetSize().Z > 80.0);

	return true;
}

/**
 * The distortion figure is symmetric, because the user cares how far from the authored shape it is.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFAssetFitDistortionTest,
	"HouseForge.Assets.Fit.DistortionIsReportedBothWays", HF_TEST_FLAGS)

bool FHFAssetFitDistortionTest::RunTest(const FString& Parameters)
{
	FHFAssetOverride Override;
	Override.FitMode = EHFAssetFitMode::StretchToFootprint;

	// Squeezed: a 250-wide asset into a 200 box.
	{
		const FBox Asset(FVector(0.0, 0.0, 0.0), FVector(250.0, 60.0, 240.0));
		const FHFAssetFitResult Fit = FHFAssetFit::Solve(HFAssetFitTest::Generated(), Asset, Override);
		TestNearlyEqual(TEXT("A 0.8x squeeze reports as 1.25"), Fit.WorstAxisRatio, 1.25, 1e-6);
	}

	// Stretched: a 160-wide asset into the same box.
	{
		const FBox Asset(FVector(0.0, 0.0, 0.0), FVector(160.0, 60.0, 240.0));
		const FHFAssetFitResult Fit = FHFAssetFit::Solve(HFAssetFitTest::Generated(), Asset, Override);
		TestNearlyEqual(TEXT("A 1.25x stretch reports as 1.25 too"), Fit.WorstAxisRatio, 1.25, 1e-6);
	}

	// An undistorted placement says so, which is what lets a panel stay quiet about the common case.
	{
		const FBox Asset = HFAssetFitTest::Generated();
		const FHFAssetFitResult Fit = FHFAssetFit::Solve(HFAssetFitTest::Generated(), Asset, Override);
		TestNearlyEqual(TEXT("An exact match is not reported as distorted"), Fit.WorstAxisRatio, 1.0, 1e-6);
	}

	return true;
}

/**
 * Nothing the solver can be handed produces a zero, negative or mirrored scale.
 *
 * A negative determinant inverts the normals and the object renders as though lit from inside; a
 * zero scale makes it disappear. Both look like an asset problem rather than a fit one.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFAssetFitScaleSanityTest,
	"HouseForge.Assets.Fit.ScaleIsNeverZeroOrNegative", HF_TEST_FLAGS)

bool FHFAssetFitScaleSanityTest::RunTest(const FString& Parameters)
{
	const FBox Awkward[] = {
		FBox(FVector::ZeroVector, FVector(1e-4, 1e-4, 1e-4)),				// a speck
		FBox(FVector::ZeroVector, FVector(100000.0, 100000.0, 100000.0)),	// something enormous
		FBox(FVector::ZeroVector, FVector(200.0, 0.0, 240.0)),				// a flat plane
	};

	const EHFAssetFitMode Modes[] = {
		EHFAssetFitMode::KeepAssetSize,
		EHFAssetFitMode::UniformFit,
		EHFAssetFitMode::FitPlanKeepHeight,
		EHFAssetFitMode::StretchToFootprint
	};

	for (const FBox& Asset : Awkward)
	{
		for (const EHFAssetFitMode Mode : Modes)
		{
			FHFAssetOverride Override;
			Override.FitMode = Mode;

			const FHFAssetFitResult Fit = FHFAssetFit::Solve(HFAssetFitTest::Generated(), Asset, Override);
			const FVector Scale = Fit.RelativeTransform.GetScale3D();

			TestTrue(TEXT("Every axis of the scale is strictly positive"),
				Scale.X > 0.0 && Scale.Y > 0.0 && Scale.Z > 0.0);
			TestTrue(TEXT("The transform is not mirrored"),
				Fit.RelativeTransform.GetDeterminant() > 0.0);
		}
	}

	// And an invalid box on either side is refused rather than fitted to nothing.
	{
		FHFAssetOverride Override;
		const FHFAssetFitResult Fit = FHFAssetFit::Solve(FBox(ForceInit), HFAssetFitTest::Generated(), Override);
		TestFalse(TEXT("An empty generated box is not a fit"), Fit.bValid);
		TestTrue(TEXT("...and it says why"), !Fit.Note.IsEmpty());
	}

	return true;
}

#undef HF_TEST_FLAGS

#endif	// WITH_DEV_AUTOMATION_TESTS
