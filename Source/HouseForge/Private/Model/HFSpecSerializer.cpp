// Copyright Siddartha G. All Rights Reserved.

#include "Model/HFSpecSerializer.h"

#include "HouseForge.h"

#include "Dom/JsonObject.h"
#include "JsonObjectConverter.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

bool FHFSpecSerializer::ToJsonString(const FHFHouseSpec& Spec, FString& OutJson, FString& OutError)
{
	OutJson.Reset();
	OutError.Reset();

	// CPF_Transient / CPF_Deprecated skipped, everything else written. Field names come out
	// camelCase, which is what the schema doc and the sample specs use.
	if (!FJsonObjectConverter::UStructToJsonObjectString(Spec, OutJson, 0, 0, 0, nullptr, /*bPrettyPrint*/ true))
	{
		OutError = TEXT("Failed to convert FHFHouseSpec to JSON.");
		return false;
	}

	return true;
}

bool FHFSpecSerializer::FromJsonString(const FString& Json, FHFHouseSpec& OutSpec, FString& OutError)
{
	OutSpec = FHFHouseSpec();
	OutError.Reset();

	if (Json.IsEmpty())
	{
		OutError = TEXT("Spec JSON is empty.");
		return false;
	}

	// Parse to a DOM first so a syntax error is reported distinctly from a shape mismatch.
	// "Unexpected token at line 41" and "field 'walls' is not an array" need different fixes.
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		OutError = FString::Printf(TEXT("Spec JSON is not valid JSON: %s"), *Reader->GetErrorMessage());
		return false;
	}

	if (!FJsonObjectConverter::JsonObjectToUStruct(Root.ToSharedRef(), FHFHouseSpec::StaticStruct(), &OutSpec, 0, 0))
	{
		OutError = TEXT("Spec JSON did not match the FHFHouseSpec schema. Check field names and types against Docs/HouseSpecSchema.md.");
		return false;
	}

	return true;
}

bool FHFSpecSerializer::SaveToFile(const FHFHouseSpec& Spec, const FString& FilePath, FString& OutError)
{
	FString Json;
	if (!ToJsonString(Spec, Json, OutError))
	{
		return false;
	}

	if (!FFileHelper::SaveStringToFile(Json, *FilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		OutError = FString::Printf(TEXT("Could not write spec to '%s'."), *FilePath);
		return false;
	}

	UE_LOG(LogHouseForge, Log, TEXT("Wrote house spec to %s"), *FilePath);
	return true;
}

bool FHFSpecSerializer::LoadFromFile(const FString& FilePath, FHFHouseSpec& OutSpec, FString& OutError)
{
	FString Json;
	if (!FFileHelper::LoadFileToString(Json, *FilePath))
	{
		OutError = FString::Printf(TEXT("Could not read spec from '%s'."), *FilePath);
		return false;
	}

	if (!FromJsonString(Json, OutSpec, OutError))
	{
		OutError = FString::Printf(TEXT("%s (file: %s)"), *OutError, *FilePath);
		return false;
	}

	return true;
}
