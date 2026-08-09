// Copyright Siddartha G. All Rights Reserved.

//
// Is the flat in the Lumen scene?
//
// This file exists because of a measurement, not a theory. Saved/Review/lumen/ holds five renders of
// the same flat under the same lighting, and the one where Lumen cannot see the geometry is the
// BRIGHTEST of them - whole-frame luminance 0.260 against 0.083 for the baked, correct one - because
// unoccluded sky floods straight through walls that are absent from the scene. The broken render is
// bright, warm-looking and completely wrong, so no human reviewing images will catch it.
//
// Everything here therefore checks the mechanism rather than the picture:
//
//   * an unbaked flat is absent, and the guard says so       AnUnbakedFlatIsNotInTheLumenScene
//   * baking it puts every element in                        BakingPutsTheFlatInTheLumenScene
//   * the hidden live mesh does not count twice              TheHiddenLiveMeshIsNotCountedTwice
//   * a lit capture refuses; a plan does not                 TheCaptureRefusesAnUnbakedLitView
//   * a handle is not a defect                               SmallPartsAreNotHeldAgainstTheBake
//   * a bake that could never work is caught, not counted    AnAssetWithNoDistanceFieldIsCaught
//   * a project that cannot build DFs fails even when baked  ProjectSettingsThatDefeatTheBakeFail
//   * a non-Lumen project is not nagged                      AProjectNotUsingLumenIsNotGuarded
//
// AnAssetWithNoDistanceFieldIsCaught and ProjectSettingsThatDefeatTheBakeFail are the two to keep.
// Both describe a flat that has been baked perfectly and is STILL invisible to Lumen - the state
// that would otherwise be reported as a successful bake, rendered, and believed.
//

#include "HouseForgeEditor.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Actors/HFBakeTypes.h"
#include "Actors/HFElementActors.h"
#include "Actors/HFHouseActor.h"
#include "Bake/HFBakeService.h"
#include "Capture/HFLumenCoverage.h"
#include "Capture/HFSceneCapture.h"
#include "Components/DynamicMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Editor.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Model/HFSampleHouse.h"

#define HF_TEST_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace HFLumenTest
{
	/**
	 * Holds a console variable at a value for a scope and puts it back.
	 *
	 * A bare Set would leak into every test that ran afterwards, and the leak would be invisible:
	 * r.GenerateMeshDistanceFields left at 0 by a failed test makes every LATER Lumen assertion fail
	 * for a reason that has nothing to do with what it was testing. Same shape, and the same reason,
	 * as FHFBakeSaveScope.
	 */
	struct FCVarScope
	{
		FCVarScope(const TCHAR* InName, int32 Value)
			: Variable(IConsoleManager::Get().FindConsoleVariable(InName))
		{
			if (Variable != nullptr)
			{
				Previous = Variable->GetInt();
				Variable->Set(Value, ECVF_SetByCode);
			}
		}

		~FCVarScope()
		{
			if (Variable != nullptr)
			{
				Variable->Set(Previous, ECVF_SetByCode);
			}
		}

		bool Exists() const { return Variable != nullptr; }

		FCVarScope(const FCVarScope&) = delete;
		FCVarScope& operator=(const FCVarScope&) = delete;

	private:
		IConsoleVariable* Variable = nullptr;
		int32 Previous = 0;
	};

	/** Every HouseForge actor already standing, gone, so one flat is measured rather than two. */
	void ClearHouseForgeActors(UWorld* World)
	{
		TArray<AActor*> Doomed;

		for (TActorIterator<AHFHouseActor> It(World); It; ++It)
		{
			It->ClearGeometry();
			Doomed.Add(*It);
		}
		for (TActorIterator<AHFElementActor> It(World); It; ++It)
		{
			Doomed.Add(*It);
		}

		for (AActor* Actor : Doomed)
		{
			if (IsValid(Actor))
			{
				Actor->Destroy();
			}
		}
	}

	AHFHouseActor* BuildReferenceFlat(UWorld* World)
	{
		ClearHouseForgeActors(World);

		AHFHouseActor* House = World->SpawnActor<AHFHouseActor>();
		if (House == nullptr)
		{
			return nullptr;
		}

		House->SetSpec(FHFSampleHouse::Make2BHK());
		House->BuildGeometry();
		return House;
	}

	TArray<AHFElementActor*> ElementsOf(UWorld* World)
	{
		TArray<AHFElementActor*> Out;
		for (TActorIterator<AHFElementActor> It(World); It; ++It)
		{
			Out.Add(*It);
		}
		return Out;
	}

	/** Leaves no dirty packages behind, so the automation run does not try to save test assets. */
	void ForgetAssets(UWorld* World)
	{
		for (TActorIterator<AHFElementActor> It(World); It; ++It)
		{
			for (const FHFBakedPart& Part : It->BakedParts)
			{
				if (Part.BakedMesh != nullptr && Part.BakedMesh->GetOutermost() != nullptr)
				{
					Part.BakedMesh->GetOutermost()->SetDirtyFlag(false);
				}
			}
		}
	}

	/** A lit view of a room: the request shape the guard is meant to apply to. */
	FHFCaptureRequest LitView()
	{
		FHFCaptureRequest Request;
		Request.bOrthographic = false;
		Request.bShowSky = true;
		Request.LumenGuard = EHFLumenGuard::Refuse;
		return Request;
	}
}

// ============================================================================================
// The unbaked flat

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFLumenUnbakedTest,
	"HouseForge.Lumen.AnUnbakedFlatIsNotInTheLumenScene", HF_TEST_FLAGS)

bool FHFLumenUnbakedTest::RunTest(const FString& Parameters)
{
	using namespace HFLumenTest;

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("An editor world is open"), World))
	{
		return false;
	}

	// Lumen has to be the GI method for any of this to mean anything, and the project ships that way.
	// Forced rather than assumed so the test measures the guard and not the machine it runs on.
	const FCVarScope Lumen(TEXT("r.DynamicGlobalIlluminationMethod"), 1);

	AHFHouseActor* House = BuildReferenceFlat(World);
	if (!TestNotNull(TEXT("The reference flat builds"), House))
	{
		return false;
	}
	ON_SCOPE_EXIT{ if (IsValid(House)) { House->ClearGeometry(); House->Destroy(); } };

	FHFLumenCoverageReport Report;
	FHFLumenCoverage::Inspect(World, Report);

	AddInfo(Report.Summary());

	TestTrue(TEXT("The flat has elements to measure"), Report.ElementsSeen > 0);
	TestTrue(TEXT("Something is drawn"), Report.PrimitivesDrawn > 0);

	// THE CLAIM. Every drawn primitive of an unbaked flat is a live dynamic mesh, and a live dynamic
	// mesh gets no mesh cards on either tracing path.
	TestTrue(TEXT("An unbaked flat has primitives absent from the Lumen scene"), Report.Absent > 0);
	TestFalse(TEXT("An unbaked flat is not covered"), Report.IsCovered());

	const int32* LiveMeshes = Report.AbsentByVerdict.Find(EHFLumenVerdict::LiveDynamicMesh);
	if (TestNotNull(TEXT("The absences are live dynamic meshes"), LiveMeshes))
	{
		TestEqual(TEXT("Every absence is a live dynamic mesh"), *LiveMeshes, Report.Absent);
	}

	TestEqual(TEXT("Nothing is radiant"), Report.Radiant, 0);
	TestTrue(TEXT("Coverage is nil"), Report.CoverageFraction() < 0.001);

	// The message is part of the feature. A guard that fires and says "coverage failed" teaches its
	// reader nothing, and this one has to survive being read by somebody holding a bright, plausible
	// render they are about to sign off.
	const FString WhyNot = Report.WhyNot();
	TestTrue(TEXT("The refusal warns that the broken render is the brighter one"),
		WhyNot.Contains(TEXT("BRIGHTER")));
	TestTrue(TEXT("The refusal names the remedy"), WhyNot.Contains(TEXT("bake")));
	AddInfo(WhyNot.Left(600));

	return true;
}

// ============================================================================================
// The baked flat - the claim this milestone was promoted for

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFLumenBakedTest,
	"HouseForge.Lumen.BakingPutsTheFlatInTheLumenScene", HF_TEST_FLAGS)

bool FHFLumenBakedTest::RunTest(const FString& Parameters)
{
	using namespace HFLumenTest;

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("An editor world is open"), World))
	{
		return false;
	}

	const FCVarScope Lumen(TEXT("r.DynamicGlobalIlluminationMethod"), 1);
	const FCVarScope DistanceFields(TEXT("r.GenerateMeshDistanceFields"), 1);

	AHFHouseActor* House = BuildReferenceFlat(World);
	if (!TestNotNull(TEXT("The reference flat builds"), House))
	{
		return false;
	}
	ON_SCOPE_EXIT
	{
		ForgetAssets(World);
		if (IsValid(House)) { House->ClearGeometry(); House->Destroy(); }
	};

	// The assets are real; writing them to the user's Content folder from a gate run is not. See
	// FHFBakeSaveScope: the default is already off under automation, and this states it.
	const FHFBakeSaveScope NoSaving(false);

	FHFBakeReport Bake;
	FHFBakeService::SetHouseRenderMode(World, /*bBaked*/ true, Bake);
	AddInfo(Bake.Summary());

	if (!TestTrue(TEXT("The flat baked"), Bake.ElementsBaked > 0 && Bake.ElementsFailed == 0))
	{
		return false;
	}

	FHFLumenCoverageReport Report;
	FHFLumenCoverage::Inspect(World, Report);
	AddInfo(Report.Summary());

	// THE CLAIM, in three parts: nothing is absent, something is radiant, and the surface area that
	// will contribute light is all of it. The area figure is the one that would catch a bake which
	// covered 149 elements out of 150 - a count alone reads as a rounding error, an area share of
	// 92% reads as a missing wall.
	if (!TestEqual(TEXT("Nothing is absent from the Lumen scene after a bake"), Report.Absent, 0))
	{
		for (int32 Index = 0; Index < FMath::Min(Report.Absentees.Num(), 10); ++Index)
		{
			AddError(FString::Printf(TEXT("Absent: %s"), *Report.Absentees[Index].Describe()));
		}
	}

	TestTrue(TEXT("The baked flat has radiant primitives"), Report.Radiant > 0);
	TestTrue(TEXT("All of the drawn surface area is radiant"), Report.CoverageFraction() > 0.999);
	TestTrue(TEXT("The baked flat is covered"), Report.IsCovered());
	TestTrue(TEXT("A covered flat has nothing to complain about"), Report.WhyNot().IsEmpty());

	return true;
}

// ============================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFLumenDoubleCountTest,
	"HouseForge.Lumen.TheHiddenLiveMeshIsNotCountedTwice", HF_TEST_FLAGS)

bool FHFLumenDoubleCountTest::RunTest(const FString& Parameters)
{
	using namespace HFLumenTest;

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("An editor world is open"), World))
	{
		return false;
	}

	const FCVarScope Lumen(TEXT("r.DynamicGlobalIlluminationMethod"), 1);

	AHFHouseActor* House = BuildReferenceFlat(World);
	if (!TestNotNull(TEXT("The reference flat builds"), House))
	{
		return false;
	}
	ON_SCOPE_EXIT
	{
		ForgetAssets(World);
		if (IsValid(House)) { House->ClearGeometry(); House->Destroy(); }
	};

	const FHFBakeSaveScope NoSaving(false);

	FHFLumenCoverageReport Before;
	FHFLumenCoverage::Inspect(World, Before);

	FHFBakeReport Bake;
	FHFBakeService::SetHouseRenderMode(World, /*bBaked*/ true, Bake);

	FHFLumenCoverageReport After;
	FHFLumenCoverage::Inspect(World, After);

	AddInfo(FString::Printf(TEXT("Drawn primitives: %d live, %d baked."),
		Before.PrimitivesDrawn, After.PrimitivesDrawn));

	// A baked element holds BOTH components. If the dynamic twin were still counted as drawn, this
	// would roughly double - and, far worse, the flat would report absences that no amount of baking
	// could clear, because the thing reported absent is the thing the bake correctly hid.
	//
	// The mechanism is USceneComponent::IsVisible(), which is !bHiddenInGame && GetVisibleFlag() -
	// exactly what the scene proxy's DrawInGame flag is built from. ApplyRenderMode sets both.
	TestTrue(TEXT("Baking does not multiply the drawn primitive count"),
		After.PrimitivesDrawn <= Before.PrimitivesDrawn + 2);

	TestEqual(TEXT("No live mesh is still counted as drawn after a bake"),
		After.AbsentByVerdict.FindRef(EHFLumenVerdict::LiveDynamicMesh), 0);

	return true;
}

// ============================================================================================
// The guard, in the capture path

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFLumenCaptureGuardTest,
	"HouseForge.Lumen.TheCaptureRefusesAnUnbakedLitView", HF_TEST_FLAGS)

bool FHFLumenCaptureGuardTest::RunTest(const FString& Parameters)
{
	using namespace HFLumenTest;

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("An editor world is open"), World))
	{
		return false;
	}

	const FCVarScope Lumen(TEXT("r.DynamicGlobalIlluminationMethod"), 1);

	AHFHouseActor* House = BuildReferenceFlat(World);
	if (!TestNotNull(TEXT("The reference flat builds"), House))
	{
		return false;
	}
	ON_SCOPE_EXIT
	{
		ForgetAssets(World);
		if (IsValid(House)) { House->ClearGeometry(); House->Destroy(); }
	};

	// Tested through EnsureLumenCoverage rather than through Render, and that is not a shortcut: the
	// validation gate runs under -nullrhi, where CanRender refuses first and correctly, so a test
	// written against Render would pass on the -nullrhi refusal and assert nothing about this guard
	// at all. Same reason GatherRenderedMaterials is a separate function from EnsureMaterialsReady.
	FString WhyNot;
	TestFalse(TEXT("A lit view of an unbaked flat is refused"),
		FHFSceneCapture::EnsureLumenCoverage(World, LitView(), WhyNot));
	TestFalse(TEXT("The refusal says why"), WhyNot.IsEmpty());

	// A plan is an orthographic section with the sky off, judged on where the walls are. Blocking it
	// over a bake would make the guard something users switch off rather than something they believe.
	FHFCaptureRequest Plan = LitView();
	Plan.LumenGuard = EHFLumenGuard::Off;
	WhyNot.Reset();
	TestTrue(TEXT("A plan is not blocked by the Lumen guard"),
		FHFSceneCapture::EnsureLumenCoverage(World, Plan, WhyNot));
	TestTrue(TEXT("An unguarded request has nothing to say"), WhyNot.IsEmpty());

	// Warn exists so the broken configuration can be MEASURED - the comparison in Saved/Review/lumen
	// requires rendering exactly what this guard is for. It must let the render through, and it must
	// not quietly become the way to make the message go away.
	FHFCaptureRequest Warned = LitView();
	Warned.LumenGuard = EHFLumenGuard::Warn;
	WhyNot.Reset();
	AddExpectedError(TEXT("NOT IN THE LUMEN SCENE"), EAutomationExpectedErrorFlags::Contains, 0);
	TestTrue(TEXT("Warn renders anyway"),
		FHFSceneCapture::EnsureLumenCoverage(World, Warned, WhyNot));
	TestTrue(TEXT("Warn clears the refusal so the caller proceeds"), WhyNot.IsEmpty());

	// And once baked, the same lit view is allowed.
	const FHFBakeSaveScope NoSaving(false);
	FHFBakeReport Bake;
	FHFBakeService::SetHouseRenderMode(World, /*bBaked*/ true, Bake);

	WhyNot.Reset();
	TestTrue(TEXT("A lit view of a baked flat is allowed"),
		FHFSceneCapture::EnsureLumenCoverage(World, LitView(), WhyNot));
	TestTrue(TEXT("Nothing to say about a baked flat"), WhyNot.IsEmpty());

	return true;
}

// ============================================================================================
// The two false-alarm cases, and the two silent-failure cases

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFLumenSmallPartsTest,
	"HouseForge.Lumen.SmallPartsAreNotHeldAgainstTheBake", HF_TEST_FLAGS)

bool FHFLumenSmallPartsTest::RunTest(const FString& Parameters)
{
	using namespace HFLumenTest;

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("An editor world is open"), World))
	{
		return false;
	}

	const double Threshold = FHFLumenCoverage::CardMinFaceAreaCm2();
	AddInfo(FString::Printf(TEXT("r.LumenScene.SurfaceCache.MeshCardsMinSize squared is %.1f cm2."), Threshold));

	// The engine's own figure, and the reason this test exists: a door handle's largest face is
	// smaller than a 10 x 10 cm square, so FLumenSceneData::AddMeshCardsFromBuildData rejects it at
	// any size of asset. A guard that failed on those would fail on every handle, hinge and tap in
	// the building and would be switched off within a day.
	TestTrue(TEXT("The threshold is the engine's 10 cm square"), FMath::IsNearlyEqual(Threshold, 100.0, 1.0));

	AHFElementActor* Element = World->SpawnActor<AHFWallActor>();
	if (!TestNotNull(TEXT("A host actor spawns"), Element))
	{
		return false;
	}
	ON_SCOPE_EXIT{ if (IsValid(Element)) { Element->Destroy(); } };

	UStaticMeshComponent* Tiny = NewObject<UStaticMeshComponent>(Element);
	Tiny->SetupAttachment(Element->GetRootComponent());
	Tiny->RegisterComponent();

	// A component holding no asset draws nothing and is not judged at all - which is also the resting
	// state of every baked component while its element shows the live mesh.
	double Face = 0.0;
	TestEqual(TEXT("An empty static mesh component is not an absence"),
		FHFLumenCoverage::Judge(Tiny, Face), EHFLumenVerdict::Radiant);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFLumenNoDistanceFieldTest,
	"HouseForge.Lumen.AnAssetWithNoDistanceFieldIsCaught", HF_TEST_FLAGS)

bool FHFLumenNoDistanceFieldTest::RunTest(const FString& Parameters)
{
	using namespace HFLumenTest;

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("An editor world is open"), World))
	{
		return false;
	}

	const FCVarScope Lumen(TEXT("r.DynamicGlobalIlluminationMethod"), 1);

	AHFElementActor* Wall = World->SpawnActor<AHFWallActor>();
	if (!TestNotNull(TEXT("A wall spawns"), Wall))
	{
		return false;
	}
	ON_SCOPE_EXIT
	{
		for (const FHFBakedPart& Part : Wall->BakedParts)
		{
			if (Part.BakedMesh != nullptr && Part.BakedMesh->GetOutermost() != nullptr)
			{
				Part.BakedMesh->GetOutermost()->SetDirtyFlag(false);
			}
		}
		if (IsValid(Wall)) { Wall->Destroy(); }
	};

	// Built in CENTIMETRES, by hand, rather than lifted out of FHFSampleHouse::Make2BHK(). That spec
	// declares Millimeters and is converted at ingest by the house actor, so assigning one of its
	// walls straight onto an actor produces a wall ten times too big - the first version of this test
	// measured a 32,400,000 cm2 face and read as perfectly healthy. Rule 04: the conversion happens
	// once, at ingest, and nothing downstream may see millimetres.
	AHFWallActor* AsWall = static_cast<AHFWallActor*>(Wall);
	AsWall->Wall.Id = TEXT("W_LumenProbe");
	AsWall->Wall.Start = FVector2D(0.0, 0.0);
	AsWall->Wall.End = FVector2D(400.0, 0.0);
	AsWall->Wall.Thickness = 20.0;
	AsWall->Wall.Height = 300.0;
	Wall->Regenerate();

	const FHFBakeSaveScope NoSaving(false);
	FHFBakeReport Bake;
	if (!TestTrue(TEXT("The wall bakes"), FHFBakeService::BakeElement(Wall, Bake)))
	{
		return false;
	}

	UStaticMeshComponent* Baked = Wall->BakedParts.IsValidIndex(0) ? Wall->BakedParts[0].Component.Get() : nullptr;
	UStaticMesh* Asset = Wall->BakedParts.IsValidIndex(0) ? Wall->BakedParts[0].BakedMesh.Get() : nullptr;
	if (!TestNotNull(TEXT("The bake produced a component"), Baked) || !TestNotNull(TEXT("and an asset"), Asset))
	{
		return false;
	}

	double Face = 0.0;
	TestEqual(TEXT("A freshly baked wall is radiant"),
		FHFLumenCoverage::Judge(Baked, Face), EHFLumenVerdict::Radiant);
	AddInfo(FString::Printf(TEXT("Largest face of the baked wall: %.0f cm2."), Face));

	// A 4 m x 3 m wall is 120,000 cm2 on its face. Asserted rather than merely printed, because the
	// figure is the one thing in this test that would have caught the unit error above.
	TestTrue(TEXT("The measured face is the wall's own 4 m x 3 m face"),
		Face > 100000.0 && Face < 140000.0);

	// ============================================================ THE SILENT FAILURE
	//
	// DistanceFieldResolutionScale at zero is what UE::AssetUtils writes when
	// FStaticMeshAssetOptions::bAllowDistanceField is false (CreateStaticMeshUtil.cpp:82), and it
	// kills the distance field - which kills the mesh cards, because the card build is chained off
	// the distance field build (DistanceFieldAtlas.cpp:296, :1050). The asset still exists, still
	// renders, still collides, and contributes no light whatsoever. Nothing about the bake report
	// would differ. This is the state the guard has to be able to see.
#if WITH_EDITORONLY_DATA
	if (Asset->GetNumSourceModels() > 0)
	{
		const float Was = Asset->GetSourceModel(0).BuildSettings.DistanceFieldResolutionScale;
		Asset->GetSourceModel(0).BuildSettings.DistanceFieldResolutionScale = 0.0f;

		TestEqual(TEXT("An asset built without a distance field is caught"),
			FHFLumenCoverage::Judge(Baked, Face), EHFLumenVerdict::NoDistanceField);

		FHFLumenCoverageReport Report;
		AHFElementActor* One = Wall;
		FHFLumenCoverage::InspectElements(MakeArrayView(&One, 1), Report);

		TestTrue(TEXT("It is counted as absent, not as too small"), Report.Absent > 0);
		TestFalse(TEXT("A flat baked without distance fields is not covered"), Report.IsCovered());

		Asset->GetSourceModel(0).BuildSettings.DistanceFieldResolutionScale = Was;
	}
#endif

	// And the component-side switch, which is the other way to bake perfectly and light nothing.
	Baked->bAffectDynamicIndirectLighting = false;
	TestEqual(TEXT("A component excluded from indirect lighting is caught"),
		FHFLumenCoverage::Judge(Baked, Face), EHFLumenVerdict::IndirectLightingOff);
	Baked->bAffectDynamicIndirectLighting = true;

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFLumenProjectSettingsTest,
	"HouseForge.Lumen.ProjectSettingsThatDefeatTheBakeFail", HF_TEST_FLAGS)

bool FHFLumenProjectSettingsTest::RunTest(const FString& Parameters)
{
	using namespace HFLumenTest;

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("An editor world is open"), World))
	{
		return false;
	}

	const FCVarScope Lumen(TEXT("r.DynamicGlobalIlluminationMethod"), 1);

	// A perfectly baked flat is still invisible to Lumen if the project does not build distance
	// fields, because the mesh card build is chained off the distance field build. That failure is
	// indistinguishable, in the rendered image, from not having baked at all - so the guard has to
	// check the project as well as the geometry, or a user who turned this off in Project Settings
	// gets a green bake and a wrong render.
	{
		const FCVarScope NoDistanceFields(TEXT("r.GenerateMeshDistanceFields"), 0);
		if (!NoDistanceFields.Exists())
		{
			AddInfo(TEXT("r.GenerateMeshDistanceFields does not exist in this build; skipped."));
			return true;
		}

		FHFLumenCoverageReport Report;
		FHFLumenCoverage::Inspect(World, Report);

		TestFalse(TEXT("A project that builds no distance fields has a problem to report"),
			Report.ProjectProblems.IsEmpty());
		TestFalse(TEXT("and is not covered, however well anything is baked"), Report.IsCovered());
		TestTrue(TEXT("The problem names the setting to change"),
			Report.WhyNot().Contains(TEXT("r.GenerateMeshDistanceFields")));
	}

	// Restored by the scope, and asserted rather than assumed - a leaked cvar would make every later
	// Lumen test fail for a reason that has nothing to do with what it measures.
	FHFLumenCoverageReport Restored;
	FHFLumenCoverage::Inspect(World, Restored);
	TestTrue(TEXT("The cvar is restored on the way out"), Restored.bProjectGeneratesDistanceFields);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFLumenNotLumenTest,
	"HouseForge.Lumen.AProjectNotUsingLumenIsNotGuarded", HF_TEST_FLAGS)

bool FHFLumenNotLumenTest::RunTest(const FString& Parameters)
{
	using namespace HFLumenTest;

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("An editor world is open"), World))
	{
		return false;
	}

	AHFHouseActor* House = BuildReferenceFlat(World);
	if (!TestNotNull(TEXT("The reference flat builds"), House))
	{
		return false;
	}
	ON_SCOPE_EXIT{ if (IsValid(House)) { House->ClearGeometry(); House->Destroy(); } };

	// With Lumen off, a dynamic mesh is not a correctness problem - there is no surface cache for it
	// to be missing from. A guard that fired anyway would be noise, and a noisy guard is one that
	// gets switched off before the day it matters.
	const FCVarScope NotLumen(TEXT("r.DynamicGlobalIlluminationMethod"), 0);
	if (!NotLumen.Exists())
	{
		AddInfo(TEXT("r.DynamicGlobalIlluminationMethod does not exist in this build; skipped."));
		return true;
	}

	FHFLumenCoverageReport Report;
	FHFLumenCoverage::Inspect(World, Report);

	TestFalse(TEXT("The check knows it does not apply"), Report.IsApplicable());
	TestTrue(TEXT("An unbaked flat is not failed on a project that does not use Lumen"), Report.IsCovered());

	FString WhyNot;
	TestTrue(TEXT("A capture is not refused either"),
		FHFSceneCapture::EnsureLumenCoverage(World, LitView(), WhyNot));

	// But it must still be able to SAY the absences are there, or "the check does not apply" and
	// "the check passed" become the same answer and the day Lumen is switched on nobody is told.
	TestTrue(TEXT("The absences are still counted and reportable"), Report.Absent > 0);

	return true;
}

#undef HF_TEST_FLAGS

#endif // WITH_DEV_AUTOMATION_TESTS
