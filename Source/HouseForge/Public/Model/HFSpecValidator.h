// Copyright Siddartha G. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Model/HFTypes.h"
#include "HFSpecValidator.generated.h"

UENUM(BlueprintType)
enum class EHFValidationSeverity : uint8
{
	/** Buildable, but probably not what the drawing meant. Worth surfacing, not worth blocking. */
	Warning,
	/** Would produce broken or nonsensical geometry. Generation must not proceed. */
	Error
};

/**
 * One problem with a spec.
 *
 * Code is machine-readable and stable, so tests and tooling can assert on a specific rule.
 * Message is written for a human or an LLM to act on, and always quotes the offending numbers -
 * "opening 'D1' spans 0..95 but wall 'W3' is only 90 long" tells Claude what to change;
 * "invalid opening" does not.
 */
USTRUCT(BlueprintType)
struct HOUSEFORGE_API FHFValidationIssue
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "HouseForge")
	EHFValidationSeverity Severity = EHFValidationSeverity::Error;

	/** Stable rule identifier, e.g. "OpeningExceedsWall". */
	UPROPERTY(BlueprintReadOnly, Category = "HouseForge")
	FString Code;

	/** Id of the element at fault, where there is one. */
	UPROPERTY(BlueprintReadOnly, Category = "HouseForge")
	FName ElementId;

	UPROPERTY(BlueprintReadOnly, Category = "HouseForge")
	FString Message;
};

/** Everything wrong with a spec, in one pass. */
USTRUCT(BlueprintType)
struct HOUSEFORGE_API FHFValidationResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "HouseForge")
	TArray<FHFValidationIssue> Issues;

	bool HasErrors() const;
	bool HasWarnings() const;
	int32 CountOf(EHFValidationSeverity Severity) const;

	/** True if an issue with this exact code is present. Used by tests and by tooling. */
	bool Contains(const FString& Code) const;

	/** Multi-line report suitable for a log, a tool result, or feeding back to Claude. */
	FString ToString() const;

	void Add(EHFValidationSeverity Severity, const FString& Code, const FName& ElementId, const FString& Message);
};

/**
 * Checks a spec before anything is built from it.
 *
 * Runs every rule rather than stopping at the first failure: a misread drawing usually has
 * several problems, and reporting them one per round-trip would make correction needlessly slow.
 */
class HOUSEFORGE_API FHFSpecValidator
{
public:
	static FHFValidationResult Validate(const FHFHouseSpec& Spec);
};
