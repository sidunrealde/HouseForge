// Copyright Siddartha G. All Rights Reserved.

using UnrealBuildTool;

public class HouseForgeEditor : ModuleRules
{
	public HouseForgeEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"HouseForge",
			}
			);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"UnrealEd",
				"EditorSubsystem",
				"EditorScriptingUtilities",
				"LevelEditor",
				"ToolMenus",
				"Projects",
				"InputCore",

				// Panel UI.
				"Slate",
				"SlateCore",
				"ToolWidgets",
				"PropertyEditor",
				"WorkspaceMenuStructure",

				// Content Browser integration for the asset replacement pass.
				"AssetTools",
				"ContentBrowser",
				"ContentBrowserData",

				// Drawing intake: native file dialog, and viewport capture for the compare loop.
				"DesktopPlatform",
				"ImageCore",
				"ImageWrapper",

				// Geometry, for baking and level construction.
				"GeometryCore",
				"GeometryFramework",
				"DynamicMesh",

				"Json",
				"JsonUtilities",

				// MCP tool surface. HouseForge registers a UToolsetDefinition here; the engine's
				// ModelContextProtocol plugin picks it up automatically, so we never depend on
				// the MCP modules directly.
				"ToolsetRegistry",
			}
			);
	}
}
