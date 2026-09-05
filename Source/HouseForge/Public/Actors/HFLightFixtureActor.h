// Copyright Siddartha G. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Actors/HFElementActors.h"
#include "Geometry/HFLuminaireKit.h"
#include "Model/HFTypes.h"
#include "HFLightFixtureActor.generated.h"

class UPointLightComponent;

/**
 * A light fitting in the level, which actually emits.
 *
 * THE ROW THAT WAS MISSING FROM THE RECIPE TABLE. EHFFixtureType::LightFixture was readable by the
 * validator, resolvable by FHFCeilingFit and reportable by the spec serializer, and
 * AHFHouseActor::FixtureRecipes had no entry for it - so a light fixture in a drawing produced no
 * actor, no mesh and no light, and the build report counted it among the types that "are not
 * modelled yet". This is that row, and the light on the end of it.
 *
 * ## What it owns
 *
 * Its FHFLuminaireParams, like every other element actor, plus the three figures that describe the
 * lamp rather than the fitting: how much light, what colour, and how far it is allowed to reach.
 * The spec's FHFFixture is read ONCE by ApplyFixture and the actor is what gets edited afterwards -
 * the house spec is the import and export format, not a live second source of truth
 * (.claude/rules/04-conventions.md).
 *
 * ## Why the light is a component of this actor rather than an actor of its own
 *
 * Because it has to move when the fitting moves. A separate ALight in the level is a second thing
 * that has to be kept at the same coordinates as the fitting, and the two drift the first time
 * somebody nudges one in the viewport - which is exactly the failure mode AHFCeilingActor avoided
 * by parenting its downlights to the ceiling that cuts their bores.
 *
 * ## Re-lighting does not regenerate geometry
 *
 * RebuildLights is callable on its own and touches nothing but the light components. That is what
 * lets a hand-edited fitting - one somebody has taken the modelling tools to, with bArtistEdited
 * set - have its lamp changed without its mesh being thrown away and rebuilt underneath it.
 * AHFElementActor::Regenerate refuses to run at all on such an actor, deliberately and silently, so
 * a lighting change routed through Regenerate would simply be lost on precisely the elements
 * somebody had invested work in.
 */
UCLASS()
class HOUSEFORGE_API AHFLightFixtureActor : public AHFElementActor
{
	GENERATED_BODY()

public:
	/** Everything this fitting is, in centimetres, about its own axis. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ShowOnlyInnerProperties))
	FHFLuminaireParams Luminaire;

	/**
	 * The parameters a fitting at this fixture will be built from.
	 *
	 * WHICH FITTING IT IS COMES OUT OF THE DIMENSIONS, not out of a second enum in the spec. A
	 * drawing gives a position, a footprint, a drop and sometimes an anchor wall, and those already
	 * say everything: a marked height barely deeper than the fitting is a panel screwed to the
	 * soffit, and one that hangs is a pendant. Adding EHFLuminaireKind to FHFFixture would be
	 * asking a drawing for something no drawing carries, and letting the two disagree.
	 *
	 * Static and public for the same reason AHFFanActor::ParamsFor is: the composing layer has to be
	 * able to size things that must AGREE with a fitting it has not spawned yet.
	 */
	static FHFLuminaireParams ParamsFor(const FHFFixture& Fixture);

	/**
	 * Reads a spec fixture into the parameters, and seeds the lamp from what the fitting turned out
	 * to be.
	 *
	 * The spec is in Unreal centimetres by the time it reaches an actor - AHFHouseActor::SetSpec
	 * converts exactly once, at ingest - so nothing here converts anything.
	 */
	void ApplyFixture(const FHFFixture& Fixture);

	/**
	 * Where a fitting at this fixture hangs, and which way it faces.
	 *
	 * HUNG FROM THE FINISHED SOFFIT, NOT FROM THE SLAB, which is the one place this differs from
	 * AHFFanActor::PlacementFor and the difference is not cosmetic. A fan hangs off the structural
	 * slab on a rod that passes through a hole cut in the plasterboard; a light fitting is screwed
	 * to the plasterboard. FHFCeilingFit::RuleFor has said so for LightFixture since it was written
	 * (EHFCeilingFitRule::HangsFromSoffit) and nothing had ever acted on it. So the soffit is handed
	 * in, resolved by the composing layer through the same FHFCeilingFit call the ceiling itself
	 * uses, rather than guessed from the room height here.
	 *
	 * BaseZ is a DROP BELOW THE CEILING, because FHFFixture::IsCeilingMounted says so for this type.
	 * That reading is applied to a wall-anchored fitting too rather than silently switching to a
	 * height above the floor for that case - one type, one meaning for BaseZ. A drawing that wants a
	 * bracket at 1.8 m in a 3 m room states a drop of 1.2 m.
	 *
	 * @param Room The room the fitting is in, for its floor level. May be null.
	 * @param AnchorWall The wall a bracket is screwed to. Null for a ceiling fitting.
	 * @param SoffitZ World Z of the finished ceiling over the fixture, resolved by the caller.
	 */
	static FTransform PlacementFor(const FHFFixture& Fixture, const FHFRoom* Room,
		const FHFWall* AnchorWall, double SoffitZ);

	/**
	 * The lamp a fitting of these proportions is fitted with, in lumens.
	 *
	 * TWO REAL PRODUCTS, NOT A SLIDER. A surface-mounted LED ceiling light in one of these flats is
	 * an 18 W panel putting out about 1600 lm - the fitting that lights a whole bedroom - and a
	 * pendant carries a single 9 W lamp at about 800 lm, which is the direct replacement for the
	 * 60 W incandescent everything in the country was lit by until recently. Both are the figure
	 * printed on the box.
	 *
	 * Published rather than written inline so the actor and any test asking whether the flat is
	 * plausibly lit are reading one number.
	 */
	static double DefaultLumensFor(const FHFLuminaireParams& Params);

	/**
	 * Puts a real light at the lens, and takes away the one that was there.
	 *
	 * Idempotent, and idempotent by destroying rather than by adjusting - the same argument
	 * AHFCeilingActor::RebuildLights makes. Safe to call on a hand-edited actor: it does not touch
	 * the mesh. See the class comment.
	 *
	 * @return How many lights the fitting now has: 1, or 0 when lighting is switched off.
	 */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "HouseForge")
	int32 RebuildLights();

	/** Off leaves the fitting modelled but dark, which is what a light fixture did before this existed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Lighting")
	bool bBuildLights = true;

	/** Output of the lamp, in lumens. Seeded by DefaultLumensFor and editable afterwards. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Lighting", meta = (ClampMin = "0.0"))
	double Lumens = 1600.0;

	/** Colour temperature in kelvin. 3000 is the warm white these flats are lit with. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Lighting", meta = (ClampMin = "1700.0", ClampMax = "12000.0"))
	double TemperatureKelvin = 3000.0;

	/**
	 * How far the light is allowed to reach, in centimetres. A hard cull, not a falloff.
	 *
	 * Nine metres, which is longer than the longest room in any of these flats and shorter than the
	 * whole dwelling. Too small and the light stops in mid-air part way across its own room; too
	 * large and every fitting in the flat is evaluated for every pixel of every other room.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Lighting", meta = (ClampMin = "0.0"))
	double AttenuationRadius = 900.0;

	/** The light this fitting owns, so a rebuild can take it away again. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HouseForge|Lighting")
	TObjectPtr<UPointLightComponent> Light;

protected:
	virtual UE::Geometry::FDynamicMesh3 BuildMesh() const override;
};
