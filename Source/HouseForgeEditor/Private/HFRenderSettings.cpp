// Copyright Siddartha G. All Rights Reserved.

#include "HFRenderSettings.h"

UHFRenderSettings::UHFRenderSettings()
{
	// Project Settings > Plugins > HouseForge Rendering, beside the HouseForge page rather than
	// inside it: the settings viewer takes one section per UDeveloperSettings class, and the two
	// pages answer to different owners - one describes the building, this one describes what the
	// editor does with it.
	CategoryName = TEXT("Plugins");
	SectionName = TEXT("HouseForge Rendering");
}

FHFRenderPolicy UHFRenderSettings::Policy()
{
	FHFRenderPolicy Out;

	// GetDefault<> can return null before the CDO exists - a real state during early module startup
	// and in a bare commandlet - and every caller here is on a path that must not crash for the sake
	// of reading a policy. Falling back to the struct's own defaults is honest: they are the same
	// values this class declares, so the worst case is the behaviour the plugin ships with.
	if (const UHFRenderSettings* Settings = GetDefault<UHFRenderSettings>())
	{
		Out.LumenGuard = Settings->LumenGuard;
		Out.BakeOnBuild = Settings->BakeOnBuild;
	}

	return Out;
}
