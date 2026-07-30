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

#undef HF_TEST_FLAGS

#endif // WITH_DEV_AUTOMATION_TESTS
