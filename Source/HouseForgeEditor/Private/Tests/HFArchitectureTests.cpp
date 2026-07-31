// Copyright Siddartha G. All Rights Reserved.

#include "HouseForgeEditor.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "HAL/FileManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#define HF_TEST_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The reference 2BHK must never become a blueprint.
 *
 * Houses are built from a spec read out of a drawing. FHFSampleHouse exists only to produce the
 * reference drawings and to give the tests a known-good house - it is not, and must not become,
 * a shortcut past the drawing-reading pipeline. Convenience is exactly how that would happen:
 * someone adds a "build the sample" button, it becomes the path everyone uses, and the real
 * pipeline quietly stops being exercised.
 *
 * So: no production source file may reference it. Only tests and the export console command may.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFSampleIsNotOnBuildPathTest,
	"HouseForge.Architecture.SampleIsNotOnTheBuildPath", HF_TEST_FLAGS)

bool FHFSampleIsNotOnBuildPathTest::RunTest(const FString& Parameters)
{
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("HouseForge"));
	if (!TestTrue(TEXT("HouseForge plugin is discoverable"), Plugin.IsValid()))
	{
		return false;
	}

	const FString SourceRoot = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Source"));

	TArray<FString> Sources;
	IFileManager::Get().FindFilesRecursive(Sources, *SourceRoot, TEXT("*.cpp"), true, false, false);
	IFileManager::Get().FindFilesRecursive(Sources, *SourceRoot, TEXT("*.h"), true, false, false);

	if (!TestTrue(TEXT("Found source files to scan"), Sources.Num() > 0))
	{
		return false;
	}

	// The sample's own files, the tests, and the console command that regenerates the committed
	// spec are the only legitimate references.
	auto IsAllowed = [](const FString& Path)
	{
		const FString Normalised = Path.Replace(TEXT("\\"), TEXT("/"));
		return Normalised.Contains(TEXT("/Tests/"))
			|| Normalised.EndsWith(TEXT("HFSampleHouse.h"))
			|| Normalised.EndsWith(TEXT("HFSampleHouse.cpp"));
	};

	TArray<FString> Offenders;
	for (const FString& Path : Sources)
	{
		if (IsAllowed(Path))
		{
			continue;
		}

		FString Contents;
		if (!FFileHelper::LoadFileToString(Contents, *Path))
		{
			continue;
		}

		if (Contents.Contains(TEXT("HFSampleHouse")) || Contents.Contains(TEXT("Make2BHK")))
		{
			FString Relative = Path;
			FPaths::MakePathRelativeTo(Relative, *(SourceRoot / TEXT("")));
			Offenders.Add(Relative);
		}
	}

	if (!Offenders.IsEmpty())
	{
		AddError(FString::Printf(
			TEXT("These production files reference the sample house, which would put it on the build path: %s. ")
			TEXT("Houses must be built from a spec read from a drawing; the sample is reference data and a test fixture only."),
			*FString::Join(Offenders, TEXT(", "))));
	}

	TestTrue(TEXT("No production source references the sample house"), Offenders.IsEmpty());

	return true;
}

/**
 * No generator may read the settings object.
 *
 * .claude/rules/04-conventions.md: every generator is a pure function from parameters to a mesh,
 * with no world, actor, editor, asset or global state behind it. A settings singleton is global
 * state, and it is by far the easiest one to reach for by accident - GetDefault<UHFSettings>() is
 * one line, it compiles anywhere, and it looks like a convenience.
 *
 * What it would cost is not obvious at the call site, which is exactly why this is a test rather
 * than a comment. A generator that reads settings cannot be tested headlessly, because its output
 * would then depend on ini contents and on whether some earlier test had left a value changed. The
 * whole suite's ability to build geometry with no editor and no project rests on this holding.
 *
 * Settings resolve into parameter structs in the COMPOSING layer - the actors and the house - and
 * are handed to generators as arguments. So Private/Geometry may not so much as name the settings
 * class, and this scans for that.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFGeneratorsDoNotReadSettingsTest,
	"HouseForge.Architecture.GeneratorsDoNotReadSettings", HF_TEST_FLAGS)

bool FHFGeneratorsDoNotReadSettingsTest::RunTest(const FString& Parameters)
{
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("HouseForge"));
	if (!TestTrue(TEXT("HouseForge plugin is discoverable"), Plugin.IsValid()))
	{
		return false;
	}

	const FString SourceRoot = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Source"));

	TArray<FString> Sources;
	IFileManager::Get().FindFilesRecursive(Sources, *SourceRoot, TEXT("*.cpp"), true, false, false);
	IFileManager::Get().FindFilesRecursive(Sources, *SourceRoot, TEXT("*.h"), true, false, false);

	if (!TestTrue(TEXT("Found source files to scan"), Sources.Num() > 0))
	{
		return false;
	}

	// The geometry layer, both modules, headers and sources. Tests are excluded: a test is entitled
	// to set a value on the settings page and prove that nothing downstream noticed, and
	// HouseForge.Settings.HandBuiltParamsAreUnaffectedBySettings does exactly that.
	auto IsGeneratorCode = [](const FString& Path)
	{
		const FString Normalised = Path.Replace(TEXT("\\"), TEXT("/"));
		return Normalised.Contains(TEXT("/Geometry/")) && !Normalised.Contains(TEXT("/Tests/"));
	};

	TArray<FString> Offenders;
	int32 Scanned = 0;

	for (const FString& Path : Sources)
	{
		if (!IsGeneratorCode(Path))
		{
			continue;
		}

		FString Contents;
		if (!FFileHelper::LoadFileToString(Contents, *Path))
		{
			continue;
		}

		++Scanned;

		// The class by name, its header, and the generic accessors that would reach any settings
		// object at all. GetDefault/GetMutableDefault are the whole mechanism - catching only
		// "UHFSettings" would miss a generator that reached for somebody else's settings instead.
		const bool bOffends =
			Contents.Contains(TEXT("UHFSettings"))
			|| Contents.Contains(TEXT("HFSettings.h"))
			|| Contents.Contains(TEXT("GetDefault<"))
			|| Contents.Contains(TEXT("GetMutableDefault<"))
			|| Contents.Contains(TEXT("FHFBuildDefaults::FromProjectSettings"));

		if (bOffends)
		{
			FString Relative = Path;
			FPaths::MakePathRelativeTo(Relative, *(SourceRoot / TEXT("")));
			Offenders.Add(Relative);
		}
	}

	// A scan that matched nothing would pass silently and prove nothing at all.
	TestTrue(TEXT("The scan actually found geometry sources to check"), Scanned > 0);

	if (!Offenders.IsEmpty())
	{
		AddError(FString::Printf(
			TEXT("These geometry files reach for a settings object: %s. ")
			TEXT("Generators are pure functions of their arguments - see .claude/rules/04-conventions.md. ")
			TEXT("Resolve the settings in the composing layer (the actors, or AHFHouseActor::BuildGeometry) ")
			TEXT("into a parameter struct, and pass that in instead."),
			*FString::Join(Offenders, TEXT(", "))));
	}

	TestTrue(TEXT("No generator reads a settings object"), Offenders.IsEmpty());

	return true;
}

/**
 * Capturing may not touch an editor viewport.
 *
 * The whole point of the offscreen capture is that no human has to be sitting in front of the
 * editor with the level visible on screen. The implementation it replaced opened by hunting for a
 * visible FLevelEditorViewportClient, pointed it downward, forced a frame, read its back buffer and
 * put the camera back - so it failed outright with the window minimised, and read the wrong pixels
 * with the window covered.
 *
 * That is easy to reintroduce by accident, because reaching for the viewport is the obvious way to
 * do almost anything visual in the editor and it compiles fine. It would also pass every functional
 * test in the suite, since those all run in an editor that does happen to have a viewport when a
 * human runs them - the failure only shows up on the machine of whoever is not watching.
 *
 * So the capture layer may not so much as name the types involved.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFCaptureNeedsNoViewportSourceTest,
	"HouseForge.Architecture.CaptureDoesNotTouchAViewport", HF_TEST_FLAGS)

bool FHFCaptureNeedsNoViewportSourceTest::RunTest(const FString& Parameters)
{
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("HouseForge"));
	if (!TestTrue(TEXT("HouseForge plugin is discoverable"), Plugin.IsValid()))
	{
		return false;
	}

	const FString SourceRoot = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Source"));

	TArray<FString> Sources;
	IFileManager::Get().FindFilesRecursive(Sources, *SourceRoot, TEXT("*.cpp"), true, false, false);
	IFileManager::Get().FindFilesRecursive(Sources, *SourceRoot, TEXT("*.h"), true, false, false);

	// The capture layer and the subsystem that fronts it. Tests are excluded: this file itself has
	// to be able to write the word to say what is forbidden.
	auto IsCaptureCode = [](const FString& Path)
	{
		const FString Normalised = Path.Replace(TEXT("\\"), TEXT("/"));
		if (Normalised.Contains(TEXT("/Tests/")))
		{
			return false;
		}
		return Normalised.Contains(TEXT("/Capture/"))
			|| Normalised.EndsWith(TEXT("HFEditorSubsystem.cpp"))
			|| Normalised.EndsWith(TEXT("HFEditorSubsystem.h"));
	};

	// The viewport client types, the include that brings them, and the engine-wide accessor that
	// hands one over. Catching only the class name would miss code that took a viewport from
	// GEditor and called it something else.
	const TCHAR* Forbidden[] =
	{
		TEXT("FLevelEditorViewportClient"),
		TEXT("FEditorViewportClient"),
		TEXT("LevelEditorViewport.h"),
		TEXT("EditorViewportClient.h"),
		TEXT("GetLevelViewportClients"),
		TEXT("GetActiveViewport"),
	};

	TArray<FString> Offenders;
	int32 Scanned = 0;

	for (const FString& Path : Sources)
	{
		if (!IsCaptureCode(Path))
		{
			continue;
		}

		FString Contents;
		if (!FFileHelper::LoadFileToString(Contents, *Path))
		{
			continue;
		}

		++Scanned;

		for (const TCHAR* Term : Forbidden)
		{
			if (Contents.Contains(Term))
			{
				FString Relative = Path;
				FPaths::MakePathRelativeTo(Relative, *(SourceRoot / TEXT("")));
				Offenders.Add(FString::Printf(TEXT("%s (%s)"), *Relative, Term));
			}
		}
	}

	// A scan that matched no files would pass while proving nothing.
	TestTrue(TEXT("The scan actually found capture sources to check"), Scanned > 0);

	if (!Offenders.IsEmpty())
	{
		AddError(FString::Printf(
			TEXT("These files reach for an editor viewport: %s. Captures render offscreen through a ")
			TEXT("USceneCaptureComponent2D into a render target, precisely so they work with the editor ")
			TEXT("window minimised, covered or on another desktop - which is the only reason the tool is ")
			TEXT("useful to Claude at all."),
			*FString::Join(Offenders, TEXT(", "))));
	}

	TestTrue(TEXT("No capture code touches an editor viewport"), Offenders.IsEmpty());

	return true;
}

/**
 * A capture must consult material readiness before it draws.
 *
 * This is a source scan because the thing it protects cannot be executed by the gate. The gate runs
 * -nullrhi, FApp::CanEverRender() is false, and FHFSceneCapture::Render therefore refuses at
 * CanRender long before it reaches a material - so no headless test can ever observe the readiness
 * step running, and its removal would leave the entire suite green.
 *
 * That is exactly the shape of the defect this was written for. A material with no compiled shader
 * map does not delay a frame: the renderer substitutes DefaultMaterial and the capture writes a
 * confident PNG in grey checkerboard. There is no failure, no log line, and no wrong-looking API
 * result - the only evidence is the image, and the image is the thing nobody can check
 * automatically. Deleting the two lines that prevent it would look like tidying up a redundant
 * check.
 *
 * So the ORDER is asserted, not merely the presence: readiness has to be settled before
 * CaptureScene, since asking afterwards would be a check on an image already drawn wrong.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFCaptureWaitsForMaterialsTest,
	"HouseForge.Architecture.CaptureWaitsForItsMaterials", HF_TEST_FLAGS)

bool FHFCaptureWaitsForMaterialsTest::RunTest(const FString& Parameters)
{
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("HouseForge"));
	if (!TestTrue(TEXT("HouseForge plugin is discoverable"), Plugin.IsValid()))
	{
		return false;
	}

	const FString Path = FPaths::Combine(Plugin->GetBaseDir(),
		TEXT("Source"), TEXT("HouseForgeEditor"), TEXT("Private"), TEXT("Capture"), TEXT("HFSceneCapture.cpp"));

	FString Contents;
	if (!TestTrue(FString::Printf(TEXT("Read the capture implementation at '%s'"), *Path),
		FFileHelper::LoadFileToString(Contents, *Path)))
	{
		return false;
	}

	// The wait itself. EnsureIsComplete is the engine facility that resubmits a material's
	// outstanding compile jobs and blocks on them; a sleep or a frame-spin in its place would be
	// unsound, because nothing guarantees a job was ever submitted to wait on.
	TestTrue(TEXT("The capture waits on the engine's shader compilation rather than on a timer"),
		Contents.Contains(TEXT("EnsureIsComplete()")));

	// IsComplete() is the trap: for a parameter-only material instance, which is what every MI_HF_*
	// is, it never consults the parent that owns the shader map and so answers "ready" for precisely
	// the material about to render as the default. It is also the obvious-looking call, which is why
	// it is named here rather than left to a comment.
	TestFalse(TEXT("Readiness is not decided by IsComplete(), which is vacuous for our instances"),
		Contents.Contains(TEXT("->IsComplete()")));
	TestTrue(TEXT("Readiness is decided by the shader map the renderer's fallback actually reads"),
		Contents.Contains(TEXT("IsGameThreadShaderMapComplete()")));

	const int32 Guard = Contents.Find(TEXT("EnsureMaterialsReady(World, Request, OutError)"));
	const int32 Draw = Contents.Find(TEXT("CaptureScene()"));

	if (!TestTrue(TEXT("Render consults material readiness"), Guard != INDEX_NONE)
		|| !TestTrue(TEXT("Render draws a frame"), Draw != INDEX_NONE))
	{
		return false;
	}

	TestTrue(TEXT("Readiness is settled BEFORE the frame is drawn, not checked afterwards"), Guard < Draw);

	return true;
}

#undef HF_TEST_FLAGS

#endif // WITH_DEV_AUTOMATION_TESTS
