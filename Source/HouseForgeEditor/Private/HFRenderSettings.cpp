// Copyright Siddartha G. All Rights Reserved.

#include "HFRenderSettings.h"

namespace
{
	/** Set only inside an FHFLumenGuardScope. See the comment on that struct. */
	bool GHasGuardOverride = false;
	EHFLumenGuard GGuardOverride = EHFLumenGuard::Refuse;
}

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

	// Applied after the settings are read, so a scope wins over the project page rather than being
	// silently overwritten by it.
	if (GHasGuardOverride)
	{
		Out.LumenGuard = GGuardOverride;
	}

	return Out;
}

FHFLumenGuardScope::FHFLumenGuardScope(EHFLumenGuard InGuard)
	: bPreviousHasOverride(GHasGuardOverride)
	, PreviousGuard(GGuardOverride)
{
	GHasGuardOverride = true;
	GGuardOverride = InGuard;
}

FHFLumenGuardScope::~FHFLumenGuardScope()
{
	GHasGuardOverride = bPreviousHasOverride;
	GGuardOverride = PreviousGuard;
}
