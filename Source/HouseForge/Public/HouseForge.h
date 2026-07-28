// Copyright Siddartha G. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

HOUSEFORGE_API DECLARE_LOG_CATEGORY_EXTERN(LogHouseForge, Log, All);

/**
 * HouseForge runtime module.
 *
 * Owns the house data model, spec validation, the pure geometry generators and the procedural
 * actors they drive. Deliberately free of editor dependencies so generated houses survive in
 * PIE and in a packaged build.
 */
class FHouseForgeModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
