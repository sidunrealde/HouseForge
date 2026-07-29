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
 * The thresholds the validator judges a spec against.
 *
 * Separated out because these are project conventions rather than laws of geometry: a developer
 * building to a 2.7 m slab-to-slab wants a different headroom floor from one building to 3.0, and
 * neither is wrong. Everything the validator checks that is genuinely structural - an opening wider
 * than its wall, a boundary that crosses itself - stays hardcoded, because those are not opinions.
 *
 * Plain and copyable, and passed in as a DEFAULTED argument whose defaults are exactly the figures
 * that were compiled in. A test that wants a specific threshold builds one of these by hand and no
 * settings object need exist.
 */
USTRUCT(BlueprintType)
struct HOUSEFORGE_API FHFValidationLimits
{
	GENERATED_BODY()

	/**
	 * Minimum clear height under a beam or a false ceiling, in centimetres.
	 *
	 * Below this a room is oppressive to stand in rather than unbuildable, which is why breaching it
	 * is a warning and not an error. 210 matches a door head, so a ceiling that clears it is a
	 * ceiling you do not duck under.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge",
		meta = (ClampMin = "120.0", UIMin = "180.0", UIMax = "260.0"))
	double MinHeadroomCm = 210.0;

	/**
	 * How much two fixtures may overlap in plan before it is called a clash, as a fraction of the
	 * smaller footprint.
	 *
	 * Not zero, because fixtures in a real drawing touch: a hob sits in a counter and a wardrobe
	 * stands against a bed. This is the band where a shared edge read off a plan is treated as
	 * contact rather than as two things in the same place.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge",
		meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "0.25"))
	double FixtureOverlapToleranceRatio = 0.05;
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
	/**
	 * @param Limits The project's thresholds. Defaulted, so every existing caller and test is
	 *               unaffected and gets the figures that were compiled in.
	 */
	static FHFValidationResult Validate(const FHFHouseSpec& Spec,
		const FHFValidationLimits& Limits = FHFValidationLimits());
};
