// Copyright Siddartha G. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

HOUSEFORGEEDITOR_API DECLARE_LOG_CATEGORY_EXTERN(LogHouseForgeEditor, Log, All);

/**
 * HouseForge editor module.
 *
 * Registers the HouseForge MCP toolset with the ToolsetRegistry, owns the editor subsystem that
 * builds levels from a house spec, and hosts the material and asset-replacement panels.
 *
 * Loads at PostEngineInit so the ToolsetRegistry subsystem exists by the time we register.
 */
class FHouseForgeEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	/**
	 * Whether the MCP toolset registration at startup actually succeeded.
	 *
	 * Asked by the CLAUDE panel, and asked of OURSELVES rather than of Claude. A missing toolset
	 * and an unreachable server look identical from the CLI's side - both are "Claude cannot build
	 * a house" - but they are completely different problems: one is a plugin fault the artist
	 * cannot fix, the other is a server they can start with a button. The plugin already knows
	 * which, so it should say so rather than make the artist find out by elimination.
	 */
	static bool IsToolsetRegistered();
};
