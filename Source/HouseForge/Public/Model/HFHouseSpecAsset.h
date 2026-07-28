// Copyright Siddartha G. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Model/HFTypes.h"
#include "HFHouseSpecAsset.generated.h"

/**
 * Editor-persistable wrapper around FHFHouseSpec.
 *
 * The spec itself is a plain USTRUCT so it can be serialised, validated and unit-tested with no
 * asset, no world and no editor. This asset exists purely so a spec can live in the Content
 * Browser and be picked in a details panel.
 */
UCLASS(BlueprintType)
class HOUSEFORGE_API UHFHouseSpecAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ShowOnlyInnerProperties))
	FHFHouseSpec Spec;
};
