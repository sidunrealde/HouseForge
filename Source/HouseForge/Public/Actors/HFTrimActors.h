// Copyright Siddartha G. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Actors/HFElementActors.h"
#include "Geometry/HFFrameKit.h"
#include "Geometry/HFWallPlateKit.h"
#include "Model/HFTypes.h"
#include "HFTrimActors.generated.h"

/**
 * The trim: the balcony guards and the curtain pelmets.
 *
 * Two actors in one pair of files, as HFFittingActors.h holds the sink, the hob and the chimney. Each
 * is a thin seam between a pure kit and the element framework.
 *
 * ## Both derive from AHFElementActor, and both say why
 *
 * Nothing in this group articulates, and in both cases that is the rule in
 * .claude/rules/04-conventions.md applied rather than dodged.
 *
 * A GATE in a balustrade swings; the reference flat draws 10.2 m of continuous MS railing across
 * three balconies with no opening anywhere in it, so there is no leaf, no hinge and no travel limit.
 *
 * A CURTAIN slides, and it is the one thing in this milestone that a rigid part genuinely cannot
 * represent: fabric drawn back gathers to a fifth of its width, and no translation of a solid does
 * that. See FHFPelmetParams for the two ways of faking it and why both are visible lies. The pelmet
 * builds the track and the slot; EHFFixtureType::Curtain is a type of its own and this flat declares
 * none of them.
 *
 * Giving either an empty Parts array and a master open amount that does nothing would put a control
 * on the details panel that lies about the object it is on.
 */

/**
 * A balcony guard in the level: posts on the parapet, a handrail, and infill that passes no sphere.
 *
 * ## Where it sits
 *
 * ON the parapet's coping, centred across it - FHFFixturePlacement::OnWallTop. Not against its face
 * and not where the drawing put it: all three are drawn 60 mm off their parapet's centreline, which
 * leaves three quarters of the railing's footprint overhanging the balcony with nothing under its
 * base plates.
 *
 * ## The parapet arrives as a dimension
 *
 * A code height is measured from the BALCONY FLOOR, so a railing standing on a 450 dwarf wall and one
 * standing on the floor are different guards at the same height. A generator may not go looking for
 * the wall under it, so the composing layer measures the parapet and seeds FHFRailingParams::MountBaseHeight
 * with it - exactly as an extract takes its host wall's thickness. See ApplyMount.
 */
UCLASS()
class HOUSEFORGE_API AHFRailingActor : public AHFElementActor
{
	GENERATED_BODY()

public:
	/** Everything this railing is, in centimetres. Origin at the footprint centre, on the coping. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ShowOnlyInnerProperties))
	FHFRailingParams Railing;

	/** Seeds the project's construction figures. Called by the composing layer, not by generation. */
	void ApplyProjectDefaults();

	/** Reads a spec fixture into the parameters. Call after ApplyProjectDefaults. */
	void ApplyFixture(const FHFFixture& Fixture);

	/**
	 * How high the parapet under this railing is, above the balcony floor.
	 *
	 * The composing layer's answer, because only it can see the wall. Zero for a railing with no
	 * parapet under it at all, which is a guard that has to make the whole code height by itself.
	 */
	void ApplyMount(double ParapetHeight);

	/**
	 * What a railing of this size comes out as before anything else touches it.
	 *
	 * Static and public for the reason AHFBedActor::ParamsFor is: the composing layer and the tests
	 * have to be able to ask what one is without spawning it.
	 */
	static FHFRailingParams ParamsFor(const FHFFixture& Fixture);

	static bool Builds(EHFFixtureType Type) { return Type == EHFFixtureType::Railing; }

protected:
	virtual UE::Geometry::FDynamicMesh3 BuildMesh() const override;
};

/**
 * A curtain pelmet in the level: a fascia and a board forming a slot, with a track in it.
 *
 * ## Where it sits, and why its drawn height is not read
 *
 * Its TOP goes on the finished soffit over it, driven a little way into it, by
 * FHFFixturePlacement::UnderSoffit. The BaseZ the drawing carries is not used at all: 2350 was
 * arrived at by subtracting a pelmet from a ceiling that has since become 300 mm shallower, and a
 * fitting placed at that figure hangs with bare wall above it. That is the same staleness a sink's
 * drawn 690 has against the counter it is set into, and it is answered the same way - ask the thing
 * it is fixed to, not the drawing.
 */
UCLASS()
class HOUSEFORGE_API AHFPelmetActor : public AHFElementActor
{
	GENERATED_BODY()

public:
	/** Everything this pelmet is, in centimetres. Origin at the footprint centre, fascia bottom. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ShowOnlyInnerProperties))
	FHFPelmetParams Pelmet;

	void ApplyProjectDefaults();
	void ApplyFixture(const FHFFixture& Fixture);

	/** What a pelmet of this size comes out as before anything else touches it. */
	static FHFPelmetParams ParamsFor(const FHFFixture& Fixture);

	static bool Builds(EHFFixtureType Type) { return Type == EHFFixtureType::Pelmet; }

protected:
	virtual UE::Geometry::FDynamicMesh3 BuildMesh() const override;
};
