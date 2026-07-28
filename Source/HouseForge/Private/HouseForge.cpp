// Copyright Siddartha G. All Rights Reserved.

#include "HouseForge.h"

#define LOCTEXT_NAMESPACE "FHouseForgeModule"

DEFINE_LOG_CATEGORY(LogHouseForge);

void FHouseForgeModule::StartupModule()
{
	UE_LOG(LogHouseForge, Log, TEXT("HouseForge runtime module started."));
}

void FHouseForgeModule::ShutdownModule()
{
	UE_LOG(LogHouseForge, Log, TEXT("HouseForge runtime module shut down."));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FHouseForgeModule, HouseForge)
