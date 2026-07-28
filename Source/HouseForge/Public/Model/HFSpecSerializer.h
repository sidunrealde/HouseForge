// Copyright Siddartha G. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Model/HFTypes.h"

/**
 * JSON round-trip for FHFHouseSpec.
 *
 * This is the wire format Claude writes after reading a drawing, so the failure messages matter
 * as much as the parsing: a rejected spec must say what was wrong well enough for the next
 * attempt to fix it.
 */
class HOUSEFORGE_API FHFSpecSerializer
{
public:
	/** Serialises to pretty-printed JSON. Pretty because these files get read and hand-edited. */
	static bool ToJsonString(const FHFHouseSpec& Spec, FString& OutJson, FString& OutError);

	/** Parses JSON into a spec. OutError carries a human-readable reason on failure. */
	static bool FromJsonString(const FString& Json, FHFHouseSpec& OutSpec, FString& OutError);

	static bool SaveToFile(const FHFHouseSpec& Spec, const FString& FilePath, FString& OutError);
	static bool LoadFromFile(const FString& FilePath, FHFHouseSpec& OutSpec, FString& OutError);
};
