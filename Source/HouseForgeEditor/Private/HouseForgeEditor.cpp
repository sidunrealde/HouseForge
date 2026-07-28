// Copyright Siddartha G. All Rights Reserved.

#include "HouseForgeEditor.h"

#define LOCTEXT_NAMESPACE "FHouseForgeEditorModule"

DEFINE_LOG_CATEGORY(LogHouseForgeEditor);

void FHouseForgeEditorModule::StartupModule()
{
	// UHFToolset registration lands here in feature/mcp-bridge.
	UE_LOG(LogHouseForgeEditor, Log, TEXT("HouseForge editor module started."));
}

void FHouseForgeEditorModule::ShutdownModule()
{
	UE_LOG(LogHouseForgeEditor, Log, TEXT("HouseForge editor module shut down."));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FHouseForgeEditorModule, HouseForgeEditor)
