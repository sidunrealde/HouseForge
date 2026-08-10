// Copyright Siddartha G. All Rights Reserved.

#include "Assets/HFAssetMappingTable.h"

#include "HouseForge.h"
#include "Model/HFSettings.h"

UHFAssetMappingTable* UHFAssetMappingTable::GetProjectTable()
{
	const UHFSettings* Settings = GetDefault<UHFSettings>();
	if (Settings == nullptr || Settings->AssetMappingTable.IsNull())
	{
		return nullptr;
	}

	UHFAssetMappingTable* Table = Settings->AssetMappingTable.LoadSynchronous();
	if (Table == nullptr)
	{
		// Named and missing is worth saying out loud, exactly as the material library says it: falling
		// through silently would look identical to never having configured one, and the visible
		// symptom - a flat that comes out procedural - is the same in both cases.
		static bool bWarned = false;
		if (!bWarned)
		{
			bWarned = true;
			UE_LOG(LogHouseForge, Warning,
				TEXT("HouseForge asset mapping table '%s' could not be loaded; fixtures are staying as generated."),
				*Settings->AssetMappingTable.ToString());
		}
	}

	return Table;
}

const FHFAssetMapping* UHFAssetMappingTable::FindUsable(EHFFixtureType Type) const
{
	const FHFAssetMapping* Mapping = Entries.Find(Type);
	return (Mapping != nullptr && Mapping->IsUsable()) ? Mapping : nullptr;
}

int32 UHFAssetMappingTable::UsableEntryCount() const
{
	int32 Count = 0;
	for (const TPair<EHFFixtureType, FHFAssetMapping>& Pair : Entries)
	{
		if (Pair.Value.IsUsable())
		{
			++Count;
		}
	}
	return Count;
}

FHFAssetOverride UHFAssetMappingTable::MakeOverride(const FHFAssetMapping& Mapping) const
{
	FHFAssetOverride Override;
	Override.OverrideMesh = Mapping.Mesh;
	Override.FitMode = Mapping.FitMode;
	Override.AssetRotationOffset = Mapping.AssetRotationOffset;
	Override.AdditionalOffset = Mapping.AdditionalOffset;

	// STAMPED WITH THE TABLE'S NAME, and this is the whole of how a rebuild tells a batch-applied
	// override from a hand-picked one. GetFName rather than the package path: it is what a report
	// reads back as, and a table that has been moved in the Content Browser is still the same table.
	Override.SourceTable = GetFName();

	return Override;
}
