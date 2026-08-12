// Copyright Siddartha G. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * The names the panel is reached by, in one place.
 *
 * The tab spawner, the layout extender and the Tools menu entry all have to agree on the tab id or
 * the menu opens a second, empty tab beside the real one - a failure that looks like the panel
 * having lost its contents rather than like a typo.
 *
 * Functions rather than namespace-scope FName constants. An FName built during static
 * initialisation runs before the name table is guaranteed to exist, and the cost of building one
 * on demand is a hash lookup nobody will measure.
 */
namespace HFPanelTabIds
{
	/** The HouseForge panel's nomad tab. */
	inline FName HouseForgePanel() { return FName(TEXT("HouseForgePanel")); }
}

/**
 * Section ids. One per collapsible section in the panel's vertical stack.
 *
 * Ids rather than indices because sections come and go with what exists in Source/ - SURFACES is
 * here because the material library landed, and ASSETS and LIGHT are deliberately absent rather
 * than greyed out. Reserving a code seam is honest; reserving pixels for something unbuilt is not.
 */
namespace HFPanelSectionIds
{
	/** The drawings this house will be read from. Drop target, and what has been imported. */
	inline FName Drawings() { return FName(TEXT("Drawings")); }

	/** What every surface role is made of. Backed by UHFMaterialLibrary. */
	inline FName Surfaces() { return FName(TEXT("Surfaces")); }
}
