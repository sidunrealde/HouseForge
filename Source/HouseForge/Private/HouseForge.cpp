// Copyright Siddartha G. All Rights Reserved.

#include "HouseForge.h"

#include "Materials/HFMaterialLibrary.h"

#define LOCTEXT_NAMESPACE "FHouseForgeModule"

DEFINE_LOG_CATEGORY(LogHouseForge);

void FHouseForgeModule::StartupModule()
{
	UE_LOG(LogHouseForge, Log, TEXT("HouseForge runtime module started."));
}

void FHouseForgeModule::ShutdownModule()
{
	// The placeholder cache holds strong references to material assets. Dropped here rather than
	// left to a static destructor, which runs after the UObject system has already been torn down.
	FHFMaterialLibrary::InvalidateCache();

	UE_LOG(LogHouseForge, Log, TEXT("HouseForge runtime module shut down."));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FHouseForgeModule, HouseForge)
