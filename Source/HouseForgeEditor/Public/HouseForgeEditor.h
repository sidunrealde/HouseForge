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
};
