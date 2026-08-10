// Copyright Siddartha G. All Rights Reserved.

#pragma once

#include "Actors/HFAssetOverrideTypes.h"
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Model/HFTypes.h"
#include "HFAssetMappingTable.generated.h"

/**
 * What one fixture type is replaced by, and how.
 *
 * The same three fields FHFAssetOverride carries, because a table entry IS an override with the
 * instance taken out of it. Kept as its own struct rather than reusing FHFAssetOverride so the
 * fields a table cannot sensibly hold - SourceTable, which the table itself supplies - are absent
 * instead of present and meaningless.
 */
USTRUCT(BlueprintType)
struct HOUSEFORGE_API FHFAssetMapping
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Asset",
		meta = (AllowedClasses = "/Script/Engine.StaticMesh"))
	TSoftObjectPtr<UStaticMesh> Mesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Asset")
	EHFAssetFitMode FitMode = EHFAssetFitMode::UniformFit;

	/**
	 * Corrected once per TYPE, which is the return on having a table at all.
	 *
	 * A vendor pack whose whole catalogue is authored facing -X is one number here, not one number
	 * per wardrobe in every flat anybody ever builds.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Asset")
	FRotator AssetRotationOffset = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Asset")
	FVector AdditionalOffset = FVector::ZeroVector;

	/**
	 * Off leaves the row in the table without applying it.
	 *
	 * Wanted more than it sounds: a table is built up over months, and the way to try a flat without
	 * the new sofa is to untick it for ten seconds, not to delete the row and lose the rotation
	 * offset somebody worked out.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Asset")
	bool bEnabled = true;

	bool IsUsable() const { return bEnabled && !Mesh.IsNull(); }
};

/**
 * WHAT MAKES THE PROCEDURAL FLAT A STEPPING STONE RATHER THAN A DEAD END.
 *
 * A mapping from fixture type to a real asset, built once and applied to every house generated
 * afterwards. Without it, swapping in a library is a per-instance chore that has to be repeated for
 * every flat and thrown away every time a drawing is re-read; with it, a studio's furniture library
 * is configured once and every future generation comes out of the drawing already furnished with
 * real models.
 *
 * A TMap KEYED ON THE ENUM, following UHFMaterialLibrary and for the reason its comment gives:
 * adding a value to EHFFixtureType leaves a saved table loadable and merely incomplete, where an
 * array indexed by the enum would silently shift every entry up by one and put the sofa where the
 * chairs were. A type with no row falls back to generated geometry, which is the correct and quiet
 * default - a table is allowed to cover four types out of forty.
 *
 * HOW IT INTERACTS WITH A HAND-PICKED OVERRIDE. It loses, always. A table pass never touches an
 * element whose FHFAssetOverride::SourceTable is NAME_None, because that is somebody's decision
 * about that instance and a batch pass silently reverting it would be the worst kind of data loss:
 * invisible until a render, and impossible to attribute. Applying the table is what a rebuild does
 * automatically; changing one wardrobe out of two is what the panel is for.
 */
UCLASS(BlueprintType)
class HOUSEFORGE_API UHFAssetMappingTable : public UDataAsset
{
	GENERATED_BODY()

public:
	/** Free text, so a table can say what library it is for. Shown in the panel. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Asset")
	FString Description;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Asset")
	TMap<EHFFixtureType, FHFAssetMapping> Entries;

	/**
	 * The table this project builds with, or null.
	 *
	 * Follows UHFMaterialLibrary::Get so the settings lookup lives with the asset rather than in the
	 * composing layer - .claude/rules/04-conventions.md keeps generators free of settings, and the
	 * house actor reaches everything else it needs through FHFBuildDefaults::FromProjectSettings,
	 * which is a value snapshot and cannot carry a UObject.
	 *
	 * NULL IS A REAL AND COMMON ANSWER, unlike the material library's, which falls back to a shipped
	 * default. There is no shipped furniture library to fall back to, and there should not be: an
	 * unconfigured project generates the procedural flat, which is exactly right.
	 */
	static UHFAssetMappingTable* GetProjectTable();

	/** The usable row for a type, or null. Null covers "no row" and "row switched off" alike. */
	const FHFAssetMapping* FindUsable(EHFFixtureType Type) const;

	/** How many rows would actually replace something. What the panel counts. */
	UFUNCTION(BlueprintPure, Category = "HouseForge|Asset")
	int32 UsableEntryCount() const;

	/** A table entry as an override, stamped with this table's name so a rebuild can tell them apart. */
	FHFAssetOverride MakeOverride(const FHFAssetMapping& Mapping) const;
};
