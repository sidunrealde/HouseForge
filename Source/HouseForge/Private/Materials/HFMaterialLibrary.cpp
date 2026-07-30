// Copyright Siddartha G. All Rights Reserved.

#include "Materials/HFMaterialLibrary.h"

#include "Components/DynamicMeshComponent.h"
#include "Geometry/HFMeshOps.h"
#include "HouseForge.h"
#include "Materials/MaterialInterface.h"
#include "UObject/UObjectGlobals.h"

namespace
{
	/**
	 * Loaded placeholders, indexed by role. Strong pointers, because nothing else holds a reference
	 * to these until a component takes one and a garbage collect between two generations would
	 * otherwise make every element re-load the set.
	 */
	TArray<TStrongObjectPtr<UMaterialInterface>> GPlaceholders;

	/** Roles already warned about, so a missing asset logs once rather than once per element. */
	TSet<EHFSurfaceRole> GWarnedRoles;
}

FString FHFMaterialLibrary::AssetPathForRole(EHFSurfaceRole Role)
{
	// The enumerator's own name, so the asset set and the enum cannot drift apart silently. Adding
	// a role without authoring its material makes HouseForge.Materials.EveryRoleResolves fail by
	// name, which is a far better signal than a room that renders one surface in checkerboard.
	const FString RoleName = StaticEnum<EHFSurfaceRole>()->GetNameStringByValue(static_cast<int64>(Role));
	return FString::Printf(TEXT("%s/MI_HF_%s.MI_HF_%s"), MaterialFolder(), *RoleName, *RoleName);
}

UMaterialInterface* FHFMaterialLibrary::GetPlaceholder(EHFSurfaceRole Role)
{
	const int32 Index = FHFMeshOps::MaterialIdForRole(Role);
	const int32 Count = FHFMeshOps::NumSurfaceRoles();
	if (Index < 0 || Index >= Count)
	{
		return nullptr;
	}

	if (GPlaceholders.Num() != Count)
	{
		GPlaceholders.SetNum(Count);
	}

	if (GPlaceholders[Index].IsValid())
	{
		return GPlaceholders[Index].Get();
	}

	const FString Path = AssetPathForRole(Role);
	UMaterialInterface* Loaded = LoadObject<UMaterialInterface>(nullptr, *Path);

	if (Loaded == nullptr)
	{
		bool bAlreadyWarned = false;
		GWarnedRoles.Add(Role, &bAlreadyWarned);
		if (!bAlreadyWarned)
		{
			UE_LOG(LogHouseForge, Warning,
				TEXT("No placeholder material for surface role '%s' at '%s'; that role will render as the default material."),
				*StaticEnum<EHFSurfaceRole>()->GetNameStringByValue(static_cast<int64>(Role)), *Path);
		}
		return nullptr;
	}

	GPlaceholders[Index] = TStrongObjectPtr<UMaterialInterface>(Loaded);
	return Loaded;
}

TArray<UMaterialInterface*> FHFMaterialLibrary::GetPlaceholderSet()
{
	const int32 Count = FHFMeshOps::NumSurfaceRoles();

	TArray<UMaterialInterface*> Set;
	Set.Reserve(Count);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		Set.Add(GetPlaceholder(static_cast<EHFSurfaceRole>(Index)));
	}
	return Set;
}

void FHFMaterialLibrary::ApplyPlaceholders(UDynamicMeshComponent* Component)
{
	if (Component == nullptr)
	{
		return;
	}

	const TArray<UMaterialInterface*> Set = GetPlaceholderSet();

	// ConfigureMaterialSet rather than a SetMaterial loop with bDeleteExtraSlots: the slot count is
	// fixed at one per role, so an element regenerated after a role was removed from the enum must
	// shed the slot rather than keep a dangling one that the scene proxy would still allocate a
	// render section for.
	Component->ConfigureMaterialSet(Set, /*bDeleteExtraSlots*/ true);
}

void FHFMaterialLibrary::InvalidateCache()
{
	GPlaceholders.Reset();
	GWarnedRoles.Reset();
}
