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

				// SDropTarget, for dropping drawings onto the panel. It draws the valid/invalid
				// hover state and refuses a bad drag BEFORE the drop, which is the difference
				// between a "no" cursor and a dialog listing what was skipped.
				"EditorWidgets",

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

				// GetFeatureLevelShaderPlatform, so the capture can ask whether a material's
				// shader map is compiled for the platform it is about to render at rather than
				// letting the renderer quietly swap in DefaultMaterial.
				"RHI",

				// Geometry, for baking and level construction.
				"GeometryCore",
				"GeometryFramework",
				"DynamicMesh",

				// Baking a dynamic mesh down to a UStaticMesh asset.
				// MeshConversion is FDynamicMeshToMeshDescription; ModelingComponentsEditorOnly is
				// UE::AssetUtils::CreateStaticMeshAsset, the low-level asset creator that does NOT
				// touch GEditor or GUndo, which is what makes it safe in a headless run.
				"MeshDescription",
				"StaticMeshDescription",
				"MeshConversion",
				"ModelingComponentsEditorOnly",

				// UToolTargetManager and the tool target interfaces. HouseForge has to reason about
				// what a Modeling Tool will pick when a baked element carries two mesh components,
				// because picking the wrong one silently edits a baked asset instead of the live
				// mesh. See HouseForge.Bake.Probe.* in Private/Tests/HFBakeProbeTests.cpp.
				"InteractiveToolsFramework",
				"ModelingComponents",

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
