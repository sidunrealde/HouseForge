// Copyright Siddartha G. All Rights Reserved.

#include "HouseForgeEditor.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Modules/ModuleManager.h"

#include "ToolsetRegistry/UToolsetRegistry.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FHFEditorModuleLoadedTest,
	"HouseForge.Foundation.EditorModuleLoaded",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHFEditorModuleLoadedTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("HouseForgeEditor module is loaded"),
		FModuleManager::Get().IsModuleLoaded(TEXT("HouseForgeEditor")));

	return true;
}

/**
 * The ToolsetRegistry is how HouseForge reaches Claude: UHFToolset registers with it in
 * feature/mcp-bridge, and the engine's MCP server surfaces it from there. If the registry is not
 * available the whole MCP path is dead, so it is worth failing loudly here rather than later.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FHFToolsetRegistryAvailableTest,
	"HouseForge.Foundation.ToolsetRegistryAvailable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHFToolsetRegistryAvailableTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("ToolsetRegistry is available for HouseForge to register its MCP toolset"),
		UToolsetRegistry::IsAvailable());

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
