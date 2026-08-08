// Copyright Siddartha G. All Rights Reserved.

#include "HouseForge.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Geometry/HFMeshOps.h"
#include "Geometry/HFRenderFinish.h"
#include "Materials/HFMaterialLibrary.h"
#include "Materials/HFSurfaceFinish.h"
#include "Materials/MaterialInterface.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Model/HFTypes.h"

#define HF_TEST_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
	FString LibraryRoleName(EHFSurfaceRole Role)
	{
		return StaticEnum<EHFSurfaceRole>()->GetNameStringByValue(static_cast<int64>(Role));
	}

	TArray<EHFSurfaceRole> EveryRole()
	{
		TArray<EHFSurfaceRole> Roles;
		for (int32 Index = 0; Index < FHFMeshOps::NumSurfaceRoles(); ++Index)
		{
			Roles.Add(static_cast<EHFSurfaceRole>(Index));
		}
		return Roles;
	}
}

/**
 * EVERY ROLE IS A FINISH SOMEBODY SPECIFIED, ON THE LIBRARY SIDE.
 *
 * The failure this catches is a role added to EHFSurfaceRole and left to inherit whatever a blank
 * FHFSurfaceFinish happens to be - mid-grey plastic at a one-metre tile, which looks enough like a
 * decision to survive a review. Every surface in an Indian flat is something specific, and the
 * Description field is where that is written down; an empty one means nobody chose.
 *
 * Also asserts the fallback works, because that is what makes a project with no library asset -
 * every project today - still generate a flat that looks like a flat.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFEveryRoleHasALibraryFinishTest,
	"HouseForge.Materials.EveryRoleHasALibraryFinish", HF_TEST_FLAGS)

bool FHFEveryRoleHasALibraryFinishTest::RunTest(const FString& Parameters)
{
	UHFMaterialLibrary* Library = UHFMaterialLibrary::Get();
	if (!TestNotNull(TEXT("There is always a library, configured or compiled in"), Library))
	{
		return false;
	}

	TestEqual(TEXT("The library carries exactly one finish per surface role"),
		Library->Finishes.Num(), FHFMeshOps::NumSurfaceRoles());

	const FHFSurfaceFinish Blank;
	int32 Glazed = 0;
	int32 Emitting = 0;
	int32 Coated = 0;

	for (const EHFSurfaceRole Role : EveryRole())
	{
		const FString Name = LibraryRoleName(Role);

		TestTrue(*FString::Printf(TEXT("'%s' has an entry of its own rather than falling back"), *Name),
			Library->Finishes.Contains(Role));

		const FHFSurfaceFinish& Finish = Library->FinishForRole(Role);

		TestFalse(*FString::Printf(TEXT("'%s' says what it is"), *Name), Finish.Description.IsEmpty());

		// A colour that is still the struct's own mid-grey is the tell for a row nobody filled in.
		TestFalse(*FString::Printf(TEXT("'%s' has a colour of its own"), *Name),
			Finish.BaseColor.Equals(Blank.BaseColor, 0.0001f));

		// Tiling is load-bearing now that a material divides millimetres into a world-scale UV, so a
		// role silently inheriting a one-metre module is a real defect rather than a tidiness one.
		TestTrue(*FString::Printf(TEXT("'%s' tiles at a positive module"), *Name), Finish.TilingMM > 0.0f);

		TestTrue(*FString::Printf(TEXT("'%s' is 0 or 1 metallic, not something in between"), *Name),
			Finish.Metallic == 0.0f || Finish.Metallic == 1.0f);

		Glazed += (Finish.Shading == EHFFinishShading::Glazed) ? 1 : 0;
		Emitting += (Finish.EmissiveStrength > 0.0f) ? 1 : 0;
		Coated += (Finish.CoatWeight > 0.0f) ? 1 : 0;
	}

	// Named rather than counted loosely: these three are the whole reason the parameter set is
	// shaped the way it is, and each has exactly one right answer today.
	TestEqual(TEXT("Exactly one role transmits"), Glazed, 1);
	TestEqual(TEXT("Glass is the one that transmits"),
		Library->FinishForRole(EHFSurfaceRole::Glass).Shading, EHFFinishShading::Glazed);

	TestEqual(TEXT("Exactly one role emits"), Emitting, 1);
	TestTrue(TEXT("LightSource is the one that emits"),
		Library->FinishForRole(EHFSurfaceRole::LightSource).EmissiveStrength > 0.0f);

	// A glaze over a body is the biggest photoreal win available with no assets, so a version of
	// this table with no coats at all would be a regression that nothing else here would see.
	TestTrue(TEXT("Several roles are a glaze over a body rather than one lobe"), Coated >= 5);

	// The fallback, on a role deliberately not present in a library.
	UHFMaterialLibrary* Sparse = NewObject<UHFMaterialLibrary>();
	Sparse->Finishes.Remove(EHFSurfaceRole::CounterStone);
	TestEqual(TEXT("A role missing from a library falls back to the shipped default, not to blank"),
		Sparse->FinishForRole(EHFSurfaceRole::CounterStone).Description,
		UHFMaterialLibrary::DefaultFinishForRole(EHFSurfaceRole::CounterStone).Description);

	return true;
}

/**
 * CHANGING THE TILE SIZE CHANGES THE UV SCALE, PROPORTIONALLY AND IN THE RIGHT DIRECTION.
 *
 * Tiling can only be stated in millimetres because UV0 is world-scale: FHFMeshOps::ApplyWorldScaleUVs
 * unwraps so one UV unit is exactly FHFRenderFinish::TexelSizeCm of surface, and the finish divides
 * a millimetre figure into that. Two things can go wrong and neither is visible in a single value:
 *
 *   - THE DIRECTION. A repeat count that goes UP as the tile gets BIGGER is a reciprocal the wrong
 *     way round. It looks entirely plausible on one number.
 *   - THE UNIT. Millimetres and centimetres differ by ten, and this project converts between them
 *     exactly once, at spec ingest, and has been bitten at that boundary. A missing factor of ten
 *     makes every tile in the flat 60 mm or 6 m and both are merely "wrong-looking".
 *
 * So the ratio is asserted as well as the absolute value: a constant error cancels in a ratio and a
 * proportionality error survives one absolute check, and neither test alone sees both.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFTileSizeScalesTheUVRepeatTest,
	"HouseForge.Materials.TileSizeScalesTheUVRepeat", HF_TEST_FLAGS)

bool FHFTileSizeScalesTheUVRepeatTest::RunTest(const FString& Parameters)
{
	const double TexelSizeCm = FHFRenderFinish().TexelSizeCm;

	FHFSurfaceFinish Finish;

	// ---- the absolute value, on the module the reference flat is actually laid in ---------------
	Finish.TilingMM = 600.0f;
	TestEqual(TEXT("A 600 mm tile is 60 cm of floor"), Finish.TileSizeCm(), 60.0, 1e-9);
	TestEqual(*FString::Printf(TEXT("A 600 mm tile repeats %.4f times per %.0f cm UV unit"),
		TexelSizeCm * 10.0 / 600.0, TexelSizeCm),
		Finish.UVRepeatsPerUnit(TexelSizeCm), TexelSizeCm * 10.0 / 600.0, 1e-9);

	// The contract restated from the other end: repeat count times tile size is the UV unit itself.
	// This is the form a missing factor of ten cannot survive.
	TestEqual(TEXT("One repeat covers one tile, in centimetres of real floor"),
		Finish.UVRepeatsPerUnit(TexelSizeCm) * Finish.TileSizeCm(), TexelSizeCm, 1e-9);

	// ---- proportionality, across a range rather than at one point --------------------------------
	const double Reference = Finish.UVRepeatsPerUnit(TexelSizeCm);

	for (const double Factor : { 0.25, 0.5, 2.0, 4.0 })
	{
		FHFSurfaceFinish Scaled = Finish;
		Scaled.TilingMM = static_cast<float>(600.0 * Factor);

		const double Repeats = Scaled.UVRepeatsPerUnit(TexelSizeCm);

		// INVERSE, and that is the direction that matters: a bigger tile repeats FEWER times.
		TestEqual(*FString::Printf(TEXT("A tile %.2fx as big repeats %.2fx as often"), Factor, 1.0 / Factor),
			Repeats * Factor, Reference, 1e-9);

		TestEqual(*FString::Printf(TEXT("At %.0f mm one repeat is still one tile"), Scaled.TilingMM),
			Repeats * Scaled.TileSizeCm(), TexelSizeCm, 1e-9);
	}

	// ---- and the real tables move with it --------------------------------------------------------
	//
	// Measured through the library rather than on a struct built by the test, so a role whose tiling
	// stopped reaching the computed scale would fail here rather than only in a render.
	const FHFSurfaceFinish& Floor = UHFMaterialLibrary::Get()->FinishForRole(EHFSurfaceRole::FloorFinish);
	const FHFSurfaceFinish& Fabric = UHFMaterialLibrary::Get()->FinishForRole(EHFSurfaceRole::Fabric);

	TestTrue(TEXT("A floor tile is a bigger module than a fabric weave"), Floor.TilingMM > Fabric.TilingMM);
	TestTrue(TEXT("...so the weave repeats more often across the same surface"),
		Fabric.UVRepeatsPerUnit(TexelSizeCm) > Floor.UVRepeatsPerUnit(TexelSizeCm));

	// A degenerate module returns nothing rather than dividing by zero. Clamped in the UI, so this
	// is the belt to that brace.
	FHFSurfaceFinish Degenerate;
	Degenerate.TilingMM = 0.0f;
	TestEqual(TEXT("A zero module yields no repeat rather than an infinity"),
		Degenerate.UVRepeatsPerUnit(TexelSizeCm), 0.0, 1e-9);

	return true;
}

/**
 * THE LIBRARY AND THE INSTANCES IT COMPILES TO SAY THE SAME THING.
 *
 * The shipped MI_HF_* assets are authored by Scripts/gen_materials.py from its own copy of these
 * numbers. Two tables of eighteen finishes is two places to edit, and a value changed in one of them
 * would be invisible: the flat would render the instance's number while every panel, test and export
 * reported the library's. That is the worst kind of drift, so it is measured rather than trusted.
 *
 * Compared through the RESOLVED value - GetScalarParameterValue walks the instance and then its
 * parent - so a parameter the instance does not override still has to agree with the library, which
 * is what catches a default silently diverging from an intent.
 *
 * The colour tolerance is loose on purpose: the sRGB transfer function is evaluated in double in
 * Python and in float here, so the two agree to about a part in a million and asserting exact
 * equality would be asserting something neither side promises.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFLibraryAndInstancesAgreeTest,
	"HouseForge.Materials.LibraryAndItsInstancesAgree", HF_TEST_FLAGS)

bool FHFLibraryAndInstancesAgreeTest::RunTest(const FString& Parameters)
{
	UHFMaterialLibrary* Library = UHFMaterialLibrary::Get();

	int32 Compared = 0;

	for (const EHFSurfaceRole Role : EveryRole())
	{
		UMaterialInterface* Material = Library->ResolveMaterial(Role);
		if (Material == nullptr)
		{
			// Reported by HouseForge.Materials.EveryRoleResolvesToAMaterial. Not restated here.
			continue;
		}

		const FString Name = LibraryRoleName(Role);
		const FHFSurfaceFinish& Finish = Library->FinishForRole(Role);

		auto CheckScalar = [&](const TCHAR* Param, float Expected)
		{
			float Actual = 0.0f;
			if (TestTrue(*FString::Printf(TEXT("MI_HF_%s exposes '%s'"), *Name, Param),
				Material->GetScalarParameterValue(FMaterialParameterInfo(Param), Actual)))
			{
				TestEqual(*FString::Printf(TEXT("MI_HF_%s agrees with the library about '%s'"), *Name, Param),
					Actual, Expected, 1e-4f);
				++Compared;
			}
		};

		auto CheckVector = [&](const TCHAR* Param, const FLinearColor& Expected)
		{
			FLinearColor Actual = FLinearColor::Black;
			if (TestTrue(*FString::Printf(TEXT("MI_HF_%s exposes '%s'"), *Name, Param),
				Material->GetVectorParameterValue(FMaterialParameterInfo(Param), Actual)))
			{
				TestTrue(*FString::Printf(
					TEXT("MI_HF_%s agrees with the library about '%s' (instance %s, library %s)"),
					*Name, Param, *Actual.ToString(), *Expected.ToString()),
					Actual.Equals(Expected, 1e-4f));
				++Compared;
			}
		};

		auto CheckSwitch = [&](const TCHAR* Param, bool bExpected)
		{
			bool bActual = false;
			FGuid Unused;
			if (TestTrue(*FString::Printf(TEXT("MI_HF_%s exposes '%s'"), *Name, Param),
				Material->GetStaticSwitchParameterValue(
					FHashedMaterialParameterInfo(FMaterialParameterInfo(Param)), bActual, Unused)))
			{
				TestEqual(*FString::Printf(TEXT("MI_HF_%s agrees with the library about '%s'"), *Name, Param),
					bActual, bExpected);
				++Compared;
			}
		};

		CheckVector(TEXT("BaseColor"), Finish.BaseColor);
		CheckScalar(TEXT("Roughness"), Finish.Roughness);
		CheckScalar(TEXT("Metallic"), Finish.Metallic);
		CheckScalar(TEXT("Specular"), Finish.Specular);

		CheckScalar(TEXT("TilingMM"), Finish.TilingMM);
		CheckScalar(TEXT("TilingRotationDegrees"), Finish.TilingRotationDegrees);

		CheckScalar(TEXT("MacroRoughnessAmount"), Finish.MacroRoughnessAmount);
		CheckScalar(TEXT("MacroAlbedoAmount"), Finish.MacroAlbedoAmount);
		CheckScalar(TEXT("MacroVariationMM"), Finish.MacroVariationMM);
		CheckScalar(TEXT("DetailBumpStrength"), Finish.DetailBumpStrength);
		CheckScalar(TEXT("DetailBumpMM"), Finish.DetailBumpMM);

		CheckScalar(TEXT("GroutWidthMM"), Finish.GroutWidthMM);
		CheckVector(TEXT("GroutColor"), Finish.GroutColor);
		CheckScalar(TEXT("GroutRoughness"), Finish.GroutRoughness);
		CheckScalar(TEXT("TileShadeVariation"), Finish.TileShadeVariation);

		CheckVector(TEXT("EmissiveColor"), Finish.EmissiveColor);
		CheckScalar(TEXT("EmissiveStrength"), Finish.EmissiveStrength);

		CheckScalar(TEXT("NormalMapStrength"), Finish.NormalMapStrength);

		if (Finish.Shading == EHFFinishShading::Glazed)
		{
			CheckScalar(TEXT("Opacity"), Finish.Opacity);
			CheckScalar(TEXT("IndexOfRefraction"), Finish.IndexOfRefraction);
			CheckScalar(TEXT("GlassThicknessMM"), Finish.GlassThicknessMM);
		}
		else
		{
			CheckScalar(TEXT("CoatWeight"), Finish.CoatWeight);
			CheckScalar(TEXT("CoatRoughness"), Finish.CoatRoughness);
		}

		CheckSwitch(TEXT("UseProceduralBump"), Finish.bUseProceduralBump);
		CheckSwitch(TEXT("WorldAlignedProjection"), Finish.bWorldAlignedProjection);

		// The map switches follow their slot, and every slot is empty until a texture library exists.
		// Asserting it here is what stops a switch being flipped on with nothing behind it.
		CheckSwitch(TEXT("UseAlbedoMap"), !Finish.AlbedoMap.IsNull());
		CheckSwitch(TEXT("UseRoughnessMap"), !Finish.RoughnessMap.IsNull());
		CheckSwitch(TEXT("UseMetallicMap"), !Finish.MetallicMap.IsNull());
		CheckSwitch(TEXT("UseAOMap"), !Finish.AOMap.IsNull());
		CheckSwitch(TEXT("UseNormalMap"), !Finish.NormalMap.IsNull());
	}

	TestTrue(*FString::Printf(TEXT("There were parameters to compare (%d)"), Compared), Compared > 300);

	return true;
}

#if WITH_EDITOR

/**
 * CHANGING A LIBRARY VALUE REACHES EVERY ACTOR USING THAT ROLE - WITHOUT VISITING ONE.
 *
 * This is the whole live-update claim, and it rests on one structural fact rather than on a loop: a
 * role's material instance is a SHARED ASSET that every component's slot already points at, so
 * writing the instance once updates all 155 elements of the reference flat at once. Nothing here
 * iterates actors, which is exactly why nothing here can regenerate one.
 *
 * BOTH TIERS ARE ASSERTED TO LAND THE SAME NUMBER. The interactive path exists because
 * PostEditChange on a material instance runs a render-state recreate and a rendering-thread sync
 * across every primitive using it, which is a hitch per frame of a slider drag. The hazard of having
 * two paths is that they disagree about what a finish is - a value written on release but not during
 * the drag makes a surface jump the instant the mouse comes up, which reads as a broken control. One
 * parameter list serves both; this measures that it does.
 *
 * Nothing is saved: the push dirties the instance's package in this process and the process exits
 * without writing it. The finish is restored at the end regardless, so no other test can observe it.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFFinishReachesTheInstanceTest,
	"HouseForge.Materials.ChangingAFinishReachesTheInstance", HF_TEST_FLAGS)

bool FHFFinishReachesTheInstanceTest::RunTest(const FString& Parameters)
{
	// A library of its own rather than the one in use, so nothing outside this test sees the edit.
	// A fresh one carries the shipped defaults, which is what makes it a fair starting point.
	UHFMaterialLibrary* Edited = NewObject<UHFMaterialLibrary>();
	UMaterialInterface* Floor = Edited->ResolveMaterial(EHFSurfaceRole::FloorFinish);
	if (!TestNotNull(TEXT("The floor finish resolves to a material"), Floor))
	{
		return false;
	}

	auto ReadScalar = [&](const TCHAR* Param)
	{
		float Value = 0.0f;
		Floor->GetScalarParameterValue(FMaterialParameterInfo(Param), Value);
		return Value;
	};
	auto ReadColour = [&](const TCHAR* Param)
	{
		FLinearColor Value = FLinearColor::Black;
		Floor->GetVectorParameterValue(FMaterialParameterInfo(Param), Value);
		return Value;
	};

	const float OriginalRoughness = ReadScalar(TEXT("Roughness"));
	const float OriginalTiling = ReadScalar(TEXT("TilingMM"));
	const FLinearColor OriginalColour = ReadColour(TEXT("BaseColor"));

	ON_SCOPE_EXIT
	{
		// Put the shipped finish back whatever happened above, so test order cannot matter and
		// HouseForge.Materials.LibraryAndItsInstancesAgree measures what was committed.
		UHFMaterialLibrary* Pristine = NewObject<UHFMaterialLibrary>();
		Pristine->PushFinish(EHFSurfaceRole::FloorFinish, EHFMaterialPush::Commit);
	};

	// ---- the commit path -------------------------------------------------------------------------
	FHFSurfaceFinish& Finish = Edited->Finishes.FindOrAdd(EHFSurfaceRole::FloorFinish);
	Finish.Roughness = 0.11f;
	Finish.TilingMM = 800.0f;
	Finish.BaseColor = FLinearColor(0.21f, 0.34f, 0.55f, 1.0f);

	TestTrue(TEXT("The floor's finish pushes"),
		Edited->PushFinish(EHFSurfaceRole::FloorFinish, EHFMaterialPush::Commit));

	TestEqual(TEXT("A committed roughness reaches the shared instance"),
		ReadScalar(TEXT("Roughness")), 0.11f, 1e-4f);
	TestEqual(TEXT("A committed tile module reaches the shared instance"),
		ReadScalar(TEXT("TilingMM")), 800.0f, 1e-3f);
	TestTrue(TEXT("A committed colour reaches the shared instance"),
		ReadColour(TEXT("BaseColor")).Equals(FLinearColor(0.21f, 0.34f, 0.55f, 1.0f), 1e-4f));

	TestTrue(TEXT("...and that is a change, not the value it already had"),
		!FMath::IsNearlyEqual(ReadScalar(TEXT("Roughness")), OriginalRoughness, 1e-4f));

	// ---- the interactive path lands the same numbers ---------------------------------------------
	Finish.Roughness = 0.77f;
	Finish.TilingMM = 250.0f;
	Finish.BaseColor = FLinearColor(0.60f, 0.15f, 0.05f, 1.0f);

	TestTrue(TEXT("The floor's finish pushes interactively"),
		Edited->PushFinish(EHFSurfaceRole::FloorFinish, EHFMaterialPush::Interactive));

	TestEqual(TEXT("A dragged roughness reaches the shared instance too"),
		ReadScalar(TEXT("Roughness")), 0.77f, 1e-4f);
	TestEqual(TEXT("A dragged tile module reaches the shared instance too"),
		ReadScalar(TEXT("TilingMM")), 250.0f, 1e-3f);
	TestTrue(TEXT("A dragged colour reaches the shared instance too"),
		ReadColour(TEXT("BaseColor")).Equals(FLinearColor(0.60f, 0.15f, 0.05f, 1.0f), 1e-4f));

	// ---- every role pushes, not only the one this test chose -------------------------------------
	//
	// Pushed from a pristine library, so the values written are the ones already there and the
	// instances end this block exactly as they started it.
	UHFMaterialLibrary* Pristine = NewObject<UHFMaterialLibrary>();
	TestEqual(TEXT("Every role has an instance to push to"),
		Pristine->PushAllFinishes(EHFMaterialPush::Commit), FHFMeshOps::NumSurfaceRoles());

	TestEqual(TEXT("A pristine push restores the shipped roughness"),
		ReadScalar(TEXT("Roughness")), OriginalRoughness, 1e-4f);
	TestEqual(TEXT("A pristine push restores the shipped tile module"),
		ReadScalar(TEXT("TilingMM")), OriginalTiling, 1e-3f);
	TestTrue(TEXT("A pristine push restores the shipped colour"),
		ReadColour(TEXT("BaseColor")).Equals(OriginalColour, 1e-4f));

	return true;
}

#endif // WITH_EDITOR

#endif // WITH_DEV_AUTOMATION_TESTS
