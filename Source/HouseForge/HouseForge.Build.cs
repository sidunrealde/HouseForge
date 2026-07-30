// Copyright Siddartha G. All Rights Reserved.

using UnrealBuildTool;

public class HouseForge : ModuleRules
{
	public HouseForge(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",

				// Procedural geometry. GeometryFramework brings UDynamicMeshComponent;
				// DynamicMesh/GeometryCore bring FDynamicMesh3 and the operators the
				// generators are built on.
				"GeometryCore",
				"GeometryFramework",
				"DynamicMesh",
				"GeometryScriptingCore",

				// Clipper2-backed polygon offset, used to inset room boundaries for false
				// ceilings. Naive per-edge offsetting collapses on the concave corners these
				// layouts are full of.
				"GeometryAlgorithms",

				// UHFSettings derives from UDeveloperSettings, which is what puts the plugin's
				// construction figures on a Project Settings page. Public rather than private
				// because UHFSettings is itself public API.
				"DeveloperSettings",
			}
			);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Projects",
				"Slate",
				"SlateCore",

				// House spec serialisation.
				"Json",
				"JsonUtilities",

				// Baking dynamic meshes down to static meshes.
				"MeshDescription",
				"StaticMeshDescription",
				"MeshConversion",
			}
			);
	}
}
