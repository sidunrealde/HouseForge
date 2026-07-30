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

				// UDeveloperSettings::OnSettingChanged, which is how an edit on the HouseForge
				// settings page reaches the elements already built in the open level.
				"DeveloperSettings",

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

				// Offscreen capture: a scene capture into a render target, read back on the
				// game thread. RenderCore is what FTextureRenderTargetResource::ReadPixels
				// lives in - the editor viewport read this replaces needed none of it.
				"RenderCore",

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
