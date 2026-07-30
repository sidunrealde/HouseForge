// Copyright Siddartha G. All Rights Reserved.

#include "HouseForgeEditor.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Actors/HFElementActors.h"
#include "Actors/HFHouseActor.h"
#include "Capture/HFPlanSection.h"
#include "Capture/HFSceneCapture.h"
#include "Capture/HFViewingLight.h"
#include "Components/DynamicMeshComponent.h"
#include "Editor.h"
#include "Engine/DirectionalLight.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/SkyLight.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "HFEditorSubsystem.h"
#include "ImageUtils.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"

#define HF_TEST_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// ---------------------------------------------------------------------------------------------
//
// Seeing the flat with nobody at the editor window.
//
// The gate runs -nullrhi, which means no test in this file can ask the renderer anything. That is
// not a gap to work around; it is the same constraint the tool itself has to survive, and it
// decides what is worth asserting here.
//
// So: everything that determines whether a plan is CORRECT is measured on geometry - the section,
// what it drops, what it keeps, and that the house it was read from is untouched afterwards.
// Everything that determines whether a capture is POSSIBLE is measured on the API - that it never
// asks for a viewport, and that when it cannot render it says so instead of writing a black PNG.
// The pixels themselves are asserted only when there is a renderer to produce them, which is what
// the whole exercise is for and which the gate is not.
//
// ---------------------------------------------------------------------------------------------

namespace HouseForgeCapture
{
	UHFEditorSubsystem* Subsystem()
	{
		return GEditor ? GEditor->GetEditorSubsystem<UHFEditorSubsystem>() : nullptr;
	}

	UWorld* EditorWorld()
	{
		return GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	}

	/** A small flat with a room, a wall ring, a door and a ceiling over it. In millimetres. */
	FString SpecJson()
	{
		return TEXT(R"JSON(
{
  "schemaVersion": 1,
  "name": "Capture Test Flat",
  "sourceDrawing": "unit-test",
  "units": "Millimeters",
  "defaultWallThickness": 115,
  "defaultWallHeight": 3000,
  "walls": [
    { "id": "W_S", "start": {"x": 0, "y": 0}, "end": {"x": 4000, "y": 0},
      "thickness": 230, "height": 3000, "bIsExternal": true },
    { "id": "W_E", "start": {"x": 4000, "y": 0}, "end": {"x": 4000, "y": 3000},
      "thickness": 230, "height": 3000, "bIsExternal": true },
    { "id": "W_N", "start": {"x": 4000, "y": 3000}, "end": {"x": 0, "y": 3000},
      "thickness": 230, "height": 3000, "bIsExternal": true },
    { "id": "W_W", "start": {"x": 0, "y": 3000}, "end": {"x": 0, "y": 0},
      "thickness": 230, "height": 3000, "bIsExternal": true }
  ],
  "openings": [
    { "id": "D1", "wallId": "W_S", "offsetAlongWall": 2000, "width": 900,
      "height": 2100, "sillHeight": 0, "kind": "Door", "swing": "InwardLeft" }
  ],
  "rooms": [
    { "id": "R_Main", "name": "Main Room", "type": "Living",
      "boundary": [ {"x":0,"y":0}, {"x":4000,"y":0}, {"x":4000,"y":3000}, {"x":0,"y":3000} ],
      "ceilingHeight": 3000, "skirtingHeight": 100 }
  ]
}
)JSON");
	}

	/** Builds the test flat and returns its house actor, or nullptr with the failure reported. */
	AHFHouseActor* BuildFlat(FAutomationTestBase& Test)
	{
		UHFEditorSubsystem* Editor = Subsystem();
		if (Editor == nullptr)
		{
			Test.AddError(TEXT("No HouseForge editor subsystem."));
			return nullptr;
		}

		const FHFOperationResult Result = Editor->ApplySpecJson(SpecJson(), FString());
		if (!Result.bSuccess)
		{
			Test.AddError(FString::Printf(TEXT("Could not build the test flat: %s"), *Result.Message));
			return nullptr;
		}

		return Editor->FindHouseActor();
	}

	/** Highest point of anything the house has built, in world centimetres. */
	double HouseTop(const AHFHouseActor* House)
	{
		double Top = -TNumericLimits<double>::Max();
		for (AActor* Element : House->ElementActors)
		{
			if (!IsValid(Element))
			{
				continue;
			}
			TArray<UDynamicMeshComponent*> Components;
			Element->GetComponents<UDynamicMeshComponent>(Components);
			for (const UDynamicMeshComponent* Component : Components)
			{
				if (IsValid(Component))
				{
					Top = FMath::Max(Top, Component->Bounds.GetBox().Max.Z);
				}
			}
		}
		return Top;
	}

	/** Total triangles across every element of a house - a cheap fingerprint of "unchanged". */
	int32 TotalTriangles(const AHFHouseActor* House)
	{
		int32 Count = 0;
		for (AActor* Element : House->ElementActors)
		{
			if (!IsValid(Element))
			{
				continue;
			}
			TArray<UDynamicMeshComponent*> Components;
			Element->GetComponents<UDynamicMeshComponent>(Components);
			for (const UDynamicMeshComponent* Component : Components)
			{
				if (IsValid(Component))
				{
					Component->ProcessMesh([&Count](const UE::Geometry::FDynamicMesh3& Mesh)
					{
						Count += Mesh.TriangleCount();
					});
				}
			}
		}
		return Count;
	}

	int32 CountOfClass(UWorld* World, UClass* Class)
	{
		int32 Count = 0;
		for (AActor* Actor : FHFViewingLight::FindIn(World))
		{
			if (IsValid(Actor) && Actor->IsA(Class))
			{
				++Count;
			}
		}
		return Count;
	}
}

/**
 * A plan is a section, and it is drawn from a COPY.
 *
 * Two claims, and they belong together because the second is the price of the first.
 *
 * The section drops everything above the cut - which is what turns a picture of the roof into a
 * plan - and it keeps the floor, which is what rules out the shortcut of hiding ceiling actors:
 * AHFRoomActor holds its ceiling slab and its floor slab in the same mesh.
 *
 * And the house is exactly as it was afterwards. Cutting the real geometry and putting it back
 * would be simpler to write, and it would mean every screenshot rewrote every mesh in the flat -
 * including hand-edited ones, whose contents .claude/rules/04-conventions.md says must never be
 * overwritten by anything but an explicit revert. A tool that quietly destroyed modelling work
 * every time it drew a picture would be the worst defect in the plugin.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFCapturePlanIsASectionTest,
	"HouseForge.Capture.APlanIsASectionAndTheHouseIsNotTouched", HF_TEST_FLAGS)

bool FHFCapturePlanIsASectionTest::RunTest(const FString& Parameters)
{
	using namespace HouseForgeCapture;

	UWorld* World = EditorWorld();
	if (!TestNotNull(TEXT("There is an editor world"), World))
	{
		return false;
	}

	AHFHouseActor* House = BuildFlat(*this);
	if (!TestNotNull(TEXT("The test flat was built"), House))
	{
		return false;
	}

	const double TopBefore = HouseTop(House);
	const int32 TrianglesBefore = TotalTriangles(House);
	const int32 ElementsBefore = House->ElementActors.Num();

	if (!TestTrue(TEXT("The house reaches above the cut height, so there is something to cut off"),
		TopBefore > FHFPlanSection::DefaultCutHeight()))
	{
		return false;
	}

	const double CutZ = FHFPlanSection::DefaultCutHeight();

	FBox Bounds(ForceInit);
	TArray<AActor*> Section = FHFPlanSection::Build(World, House, CutZ, Bounds);

	if (!TestTrue(TEXT("The section produced geometry"), Section.Num() > 0 && Bounds.IsValid))
	{
		FHFPlanSection::DestroyAll(World, Section);
		return false;
	}

	// The point of the whole exercise. A top-down render of the uncut house is a picture of the
	// ceiling; a top-down render of this is a plan.
	TestTrue(TEXT("Nothing in the section is above the cut plane"), Bounds.Max.Z <= CutZ + 0.01);
	TestTrue(TEXT("The ceiling really was above it, so the cut did something"), TopBefore > Bounds.Max.Z + 1.0);

	// The floor is still there, which is what hiding the ceiling actors would have thrown away
	// along with it.
	TestTrue(TEXT("The section still reaches the floor"), Bounds.Min.Z <= 1.0);
	TestTrue(TEXT("The section covers the flat's plan footprint"),
		Bounds.GetSize().X > 300.0 && Bounds.GetSize().Y > 200.0);

	// And the house it was read from is untouched.
	TestEqual(TEXT("The house still has all its elements"), House->ElementActors.Num(), ElementsBefore);
	TestEqual(TEXT("Not one triangle of the house was changed"), TotalTriangles(House), TrianglesBefore);
	TestNearlyEqual(TEXT("The house still reaches its full height"), HouseTop(House), TopBefore, 0.01);

	for (AActor* Element : House->ElementActors)
	{
		if (const AHFElementActor* Typed = Cast<AHFElementActor>(Element))
		{
			if (Typed->bArtistEdited)
			{
				AddError(FString::Printf(
					TEXT("Element '%s' was marked as hand-edited by drawing a plan. Capturing must read the ")
					TEXT("house, never write to it."), *Typed->ElementId.ToString()));
			}
		}
	}

	FHFPlanSection::DestroyAll(World, Section);

	// The scaffolding goes away with it. A level that accumulated a sectioned copy of itself per
	// screenshot would double in size on the first capture and be coincident with itself, which is
	// invisible in exactly the view it would ruin.
	int32 Remaining = 0;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		TArray<UDynamicMeshComponent*> Components;
		It->GetComponents<UDynamicMeshComponent>(Components);
		if (!Components.IsEmpty() && !It->IsA<AHFElementActor>())
		{
			++Remaining;
		}
	}
	TestEqual(TEXT("The sectioned copy is gone again"), Remaining, 0);

	return true;
}

/**
 * The placeholder light is spawned once, and a rebuild does not spawn another.
 *
 * The failure this guards is cumulative and quiet: every capture ensures the rig exists, so a rig
 * that was found unreliably would add a sun per screenshot. Ten captures in and the flat is white,
 * with nothing in the level obviously wrong - just ten identical actors in a folder nobody opens.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFCaptureLightSpawnsOnceTest,
	"HouseForge.Capture.ThePlaceholderLightSpawnsOnce", HF_TEST_FLAGS)

bool FHFCaptureLightSpawnsOnceTest::RunTest(const FString& Parameters)
{
	using namespace HouseForgeCapture;

	UWorld* World = EditorWorld();
	UHFEditorSubsystem* Editor = Subsystem();
	if (!TestNotNull(TEXT("There is an editor world"), World) || !TestNotNull(TEXT("There is a subsystem"), Editor))
	{
		return false;
	}

	// Start from nothing, so this measures spawning rather than whatever an earlier test left.
	Editor->RemoveViewingLight();
	TestEqual(TEXT("The level starts with no placeholder light"), FHFViewingLight::FindIn(World).Num(), 0);

	AHFHouseActor* House = BuildFlat(*this);
	if (!TestNotNull(TEXT("The test flat was built"), House))
	{
		return false;
	}

	// Building lights the house, because a new level has nothing in it to see by.
	const int32 AfterBuild = FHFViewingLight::FindIn(World).Num();
	TestTrue(TEXT("Building a house lights it"), AfterBuild > 0);

	TestEqual(TEXT("Exactly one sun"), CountOfClass(World, ADirectionalLight::StaticClass()), 1);
	TestEqual(TEXT("Exactly one sky light"), CountOfClass(World, ASkyLight::StaticClass()), 1);
	TestEqual(TEXT("Exactly one exposure volume"), CountOfClass(World, APostProcessVolume::StaticClass()), 1);

	// Asking again is what every capture does.
	Editor->EnsureViewingLight();
	Editor->EnsureViewingLight();
	TestEqual(TEXT("Asking for the light again does not add a second one"),
		FHFViewingLight::FindIn(World).Num(), AfterBuild);

	// A geometry rebuild destroys and respawns element actors. The rig must not be caught up in it.
	House->BuildGeometry();
	TestEqual(TEXT("Rebuilding the geometry does not duplicate the light"),
		FHFViewingLight::FindIn(World).Num(), AfterBuild);

	// And re-applying the spec into the same level, which is the other way a house gets rebuilt.
	BuildFlat(*this);
	TestEqual(TEXT("Re-applying the spec does not duplicate the light"),
		FHFViewingLight::FindIn(World).Num(), AfterBuild);

	TestEqual(TEXT("Still exactly one sun"), CountOfClass(World, ADirectionalLight::StaticClass()), 1);

	// It comes out in one call, which is what the lighting milestone will do to it.
	const int32 Removed = Editor->RemoveViewingLight();
	TestEqual(TEXT("Removing the rig removes all of it"), Removed, AfterBuild);
	TestEqual(TEXT("And leaves none of it behind"), FHFViewingLight::FindIn(World).Num(), 0);

	return true;
}

/**
 * Capturing never asks for a viewport, and says what it cannot do rather than faking it.
 *
 * The old implementation opened with a search for a visible FLevelEditorViewportClient and failed
 * outright when there was none - so the tool that exists for Claude to check its own work only
 * worked while a human was sitting in front of the editor with the level open on screen. Running
 * headless, as this test does, there are no viewport clients at all.
 *
 * What is asserted is therefore the shape of the answer, not the picture: whatever happens, the
 * reason must never be a viewport. Under -nullrhi the honest answer is a refusal that names the
 * renderer - a black PNG would be far worse, because a black PNG of an unlit house and a black PNG
 * of a house that failed to render are the same file.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFCaptureNeedsNoViewportTest,
	"HouseForge.Capture.NoViewportIsNeededAndNoneIsAskedFor", HF_TEST_FLAGS)

bool FHFCaptureNeedsNoViewportTest::RunTest(const FString& Parameters)
{
	using namespace HouseForgeCapture;

	UHFEditorSubsystem* Editor = Subsystem();
	if (!TestNotNull(TEXT("There is a subsystem"), Editor))
	{
		return false;
	}

	if (!TestNotNull(TEXT("The test flat was built"), BuildFlat(*this)))
	{
		return false;
	}

	const FString Directory = FPaths::Combine(FPaths::ProjectPluginsDir(), TEXT("HouseForge"),
		TEXT("Saved"), TEXT("Screenshots"));

	const FString PlanName = TEXT("HFTest_Plan");
	const FString ViewName = TEXT("HFTest_View");
	const FString PlanPath = FPaths::Combine(Directory, PlanName + TEXT(".png"));
	const FString ViewPath = FPaths::Combine(Directory, ViewName + TEXT(".png"));

	IFileManager::Get().Delete(*PlanPath, false, true, true);
	IFileManager::Get().Delete(*ViewPath, false, true, true);

	constexpr int32 Resolution = 512;

	FString PlanOut;
	const FHFOperationResult Plan = Editor->CaptureTopDown(PlanName, Resolution, 0.0, PlanOut);

	FString ViewOut;
	const FHFOperationResult View = Editor->CaptureView(ViewName, Resolution,
		FVector(50.0, 150.0, 160.0), FVector(390.0, 150.0, 140.0), 70.0, ViewOut);

	// Neither outcome may blame a viewport, and neither may mention one at all.
	for (const TPair<FString, FString>& Answer :
		{ TPair<FString, FString>(TEXT("plan"), Plan.Message), TPair<FString, FString>(TEXT("view"), View.Message) })
	{
		if (Answer.Value.Contains(TEXT("viewport"), ESearchCase::IgnoreCase))
		{
			AddError(FString::Printf(
				TEXT("The %s capture answered '%s'. Capturing renders offscreen and must not depend on an ")
				TEXT("editor viewport being open, visible or focused."), *Answer.Key, *Answer.Value));
		}
	}

	FString WhyNot;
	const bool bCanRender = FHFSceneCapture::CanRender(WhyNot);

	if (!bCanRender)
	{
		// The gate's case. Assert the refusal is honest and specific rather than skipping.
		TestFalse(TEXT("Without a renderer the plan capture refuses"), Plan.bSuccess);
		TestFalse(TEXT("Without a renderer the view capture refuses"), View.bSuccess);
		TestTrue(TEXT("And says it is the renderer that is missing"),
			Plan.Message.Contains(TEXT("nullrhi")) || Plan.Message.Contains(TEXT("renderer")));
		TestFalse(TEXT("No file is written when nothing was rendered"),
			IFileManager::Get().FileExists(*PlanPath));

		AddInfo(FString::Printf(
			TEXT("Pixels not asserted: %s The geometry a plan is made of is asserted in ")
			TEXT("HouseForge.Section.* and HouseForge.Capture.APlanIsASectionAndTheHouseIsNotTouched."),
			*WhyNot));
		return true;
	}

	// With a renderer, the file and its size are the assertion.
	if (!TestTrue(FString::Printf(TEXT("The plan captured: %s"), *Plan.Message), Plan.bSuccess))
	{
		return false;
	}
	TestTrue(FString::Printf(TEXT("The view captured: %s"), *View.Message), View.bSuccess);

	for (const TPair<FString, FString>& Written :
		{ TPair<FString, FString>(TEXT("plan"), PlanOut), TPair<FString, FString>(TEXT("view"), ViewOut) })
	{
		if (!TestTrue(FString::Printf(TEXT("The %s image exists"), *Written.Key),
			IFileManager::Get().FileExists(*Written.Value)))
		{
			continue;
		}

		FImage Image;
		if (!TestTrue(FString::Printf(TEXT("The %s image is readable"), *Written.Key),
			FImageUtils::LoadImage(*Written.Value, Image)))
		{
			continue;
		}

		TestEqual(FString::Printf(TEXT("The %s image's longest edge is the resolution asked for"), *Written.Key),
			FMath::Max(Image.SizeX, Image.SizeY), Resolution);
		TestTrue(FString::Printf(TEXT("The %s image has a sensible short edge"), *Written.Key),
			FMath::Min(Image.SizeX, Image.SizeY) >= 64);
	}

	return true;
}

/**
 * A view of nowhere is refused rather than rendered.
 *
 * A camera and its target in the same place has no direction to look in, and FVector::Rotation of a
 * zero vector is a zero rotator - so the capture would silently point down the world X axis and
 * hand back a picture of whatever happened to be that way. A picture taken somewhere other than
 * where it was asked for is the one kind of wrong answer a visual check cannot catch.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFCaptureRefusesAViewOfNowhereTest,
	"HouseForge.Capture.AViewOfNowhereIsRefused", HF_TEST_FLAGS)

bool FHFCaptureRefusesAViewOfNowhereTest::RunTest(const FString& Parameters)
{
	using namespace HouseForgeCapture;

	UHFEditorSubsystem* Editor = Subsystem();
	if (!TestNotNull(TEXT("There is a subsystem"), Editor))
	{
		return false;
	}

	FString Out;
	const FHFOperationResult Result = Editor->CaptureView(TEXT("HFTest_Nowhere"), 256,
		FVector(100.0, 100.0, 100.0), FVector(100.0, 100.0, 100.0), 70.0, Out);

	TestFalse(TEXT("A camera pointed at itself is refused"), Result.bSuccess);
	TestTrue(TEXT("And no path is handed back"), Out.IsEmpty());

	return true;
}

#undef HF_TEST_FLAGS

#endif // WITH_DEV_AUTOMATION_TESTS
