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

#undef HF_TEST_FLAGS

#endif // WITH_DEV_AUTOMATION_TESTS
