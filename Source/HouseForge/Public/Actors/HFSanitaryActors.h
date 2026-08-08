// Copyright Siddartha G. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Actors/HFArticulatedActor.h"
#include "Actors/HFElementActors.h"
#include "Geometry/HFApplianceKit.h"
#include "Geometry/HFFrameKit.h"
#include "Geometry/HFSanitaryKit.h"
#include "Geometry/HFWallPlateKit.h"
#include "Model/HFTypes.h"
#include "HFSanitaryActors.generated.h"

/**
 * What is in a bathroom: the WC, the basin, the shower, the geyser, the mirror and the towel rail.
 *
 * Six actors in one pair of files, exactly as HFFittingActors.h holds the kitchen's three. Each is a
 * thin seam between a pure kit and the element framework - parameters, a settings hook, and the
 * generation overrides - and six files would be more ceremony than content in each one.
 *
 * They do NOT share a base beyond the framework's own, and that is deliberate: three of them
 * articulate and three of them do not, so three derive from AHFArticulatedActor and three from
 * AHFElementActor. Forcing a mirror through the articulated base would give it an empty Parts array
 * and a master open amount that moves nothing, which is a control that lies about the object it is
 * on. See .claude/rules/04-conventions.md.
 *
 * ## Where they all sit
 *
 * On the FINISHED FACE of the wall they are fixed to, not at the Y the drawing gave them - see
 * FHFFixturePlacement::OnWallFace, and the two defects it exists for. Every fitting here is bought
 * to a fixed size and screwed to plaster; none of them is scribed to a gap the way a run of joinery
 * is, so the drawn depth position is an approximation and the wall face is not.
 */

/**
 * A WC in the level: a lofted pan on a pedestal, a close-coupled cistern, a seat and a lid.
 *
 * ## What the drawing says and what is built
 *
 * The reference flat labels both of these "wall-hung" and then draws them 380 x 600 x 400 standing
 * on the floor. The DRAWN BOX wins, and it describes a floor-standing close-coupled pan whose seat
 * top is at 400 - the standard Indian figure. A wall-hung pan is a different fitting: its cistern is
 * concealed in a duct this plan does not have, and its box would start at 200 rather than at 0. See
 * FHFWCParams for the same note from the geometry's side.
 *
 * ## What moves
 *
 * The lid, the seat and the flush plate. The seat's angle TRACKS the lid's rather than being free -
 * a seat cannot rise through a closed lid - and both stop where they meet the cistern rather than
 * where they were asked to.
 */
UCLASS()
class HOUSEFORGE_API AHFWCActor : public AHFArticulatedActor
{
	GENERATED_BODY()

public:
	/** Everything this WC is, in centimetres. Origin at the footprint centre, on the floor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ShowOnlyInnerProperties))
	FHFWCParams WC;

	/** Seeds the project's construction figures. Called by the composing layer, not by generation. */
	void ApplyProjectDefaults();

	/** Reads a spec fixture into the parameters. Call after ApplyProjectDefaults. */
	void ApplyFixture(const FHFFixture& Fixture);

	/**
	 * What a WC of this size is before anything else touches it.
	 *
	 * Static and public because the composing layer has to be able to ask how tall one comes out
	 * without spawning it: a WC drawn 400 high stands 764 with its cistern on, and FHFCeilingFit takes
	 * the BUILT envelope rather than the drawn box. The same reason AHFFanActor::ParamsFor is.
	 */
	static FHFWCParams ParamsFor(const FHFFixture& Fixture);

	static bool Builds(EHFFixtureType Type) { return Type == EHFFixtureType::WC; }

	static FName LidPartId() { return FHFSanitaryKit::LidPartId(); }
	static FName SeatPartId() { return FHFSanitaryKit::SeatPartId(); }
	static FName FlushButtonPartId() { return FHFSanitaryKit::FlushButtonPartId(); }

protected:
	virtual UE::Geometry::FDynamicMesh3 BuildMesh() const override;
	virtual void BuildParts(TArray<FHFMeshPart>& OutParts) const override;
};

/**
 * A basin in the level: a lofted bowl on a tap ledge, on a counter or on the wall.
 *
 * ## Which mount, and who decides
 *
 * NOT THIS ACTOR. Whether a basin stands on a vanity or hangs on a wall depends on whether there is
 * a vanity under it, which is a question about another fixture - and neither a generator nor an
 * actor may go looking for one. AHFHouseActor resolves it, exactly as it resolves which counter a
 * sink is set into, and hands the answer over as a plain value.
 *
 * The difference is not cosmetic: a wall-hung basin carries a shroud over its trap that is entirely
 * BELOW the drawn box, and without it the fitting is a ceramic bowl floating at 800.
 */
UCLASS()
class HOUSEFORGE_API AHFBasinActor : public AHFArticulatedActor
{
	GENERATED_BODY()

public:
	/** Everything this basin is, in centimetres. Origin at the footprint centre, Z = 0 at the mount. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ShowOnlyInnerProperties))
	FHFBasinParams Basin;

	void ApplyProjectDefaults();
	void ApplyFixture(const FHFFixture& Fixture);

	/**
	 * Tells the basin what is holding it up.
	 *
	 * Called by the composing layer after ApplyFixture, because only that layer can see whether a
	 * vanity's footprint covers this basin's position.
	 */
	void ApplyMount(EHFBasinMount Mount);

	static bool Builds(EHFFixtureType Type) { return Type == EHFFixtureType::Basin; }

	static FName TapLeverPartId() { return FHFSanitaryKit::TapLeverPartId(); }
	static FName TapSpoutPartId() { return FHFSanitaryKit::TapSpoutPartId(); }

protected:
	virtual UE::Geometry::FDynamicMesh3 BuildMesh() const override;
	virtual void BuildParts(TArray<FHFMeshPart>& OutParts) const override;
};

/**
 * A shower in the level: a wet area, a mixer, a riser, an arm and a rose.
 *
 * NO ENCLOSURE, and that is what the spec says rather than an omission. The flat declares a 900 x
 * 900 Shower AREA, not a ShowerPartition, so the one big moving thing in a shower - a glass door -
 * is not in this drawing. What is built is the fitting and the floor it stands on: a threshold and a
 * gully, because a shower reduced to a rose on a pipe is a tap over an ordinary bathroom floor.
 */
UCLASS()
class HOUSEFORGE_API AHFShowerActor : public AHFArticulatedActor
{
	GENERATED_BODY()

public:
	/** Everything this shower is, in centimetres. Origin at the footprint centre, on the floor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ShowOnlyInnerProperties))
	FHFShowerParams Shower;

	void ApplyProjectDefaults();
	void ApplyFixture(const FHFFixture& Fixture);

	static bool Builds(EHFFixtureType Type) { return Type == EHFFixtureType::Shower; }

	static FName MixerLeverPartId() { return FHFSanitaryKit::MixerLeverPartId(); }
	static FName RosePartId() { return FHFSanitaryKit::RosePartId(); }

protected:
	virtual UE::Geometry::FDynamicMesh3 BuildMesh() const override;
	virtual void BuildParts(TArray<FHFMeshPart>& OutParts) const override;
};

/**
 * A storage water heater in the level: a horizontal cylinder on a wall bracket, high up.
 *
 * The one fitting in this group that FHFCeilingFit already had a rule for - Geyser Lowers, because
 * it is bought and screwed to a wall rather than made on site and cut. Nothing here needs to know
 * that; the fit happens on the spec before the fixture reaches this actor at all.
 */
UCLASS()
class HOUSEFORGE_API AHFGeyserActor : public AHFArticulatedActor
{
	GENERATED_BODY()

public:
	/** Everything this geyser is, in centimetres. Origin at the footprint centre, box bottom. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ShowOnlyInnerProperties))
	FHFGeyserParams Geyser;

	void ApplyProjectDefaults();
	void ApplyFixture(const FHFFixture& Fixture);

	static bool Builds(EHFFixtureType Type) { return Type == EHFFixtureType::Geyser; }

	static FName ThermostatPartId() { return FHFApplianceKit::ThermostatPartId(); }

protected:
	virtual UE::Geometry::FDynamicMesh3 BuildMesh() const override;
	virtual void BuildParts(TArray<FHFMeshPart>& OutParts) const override;
};

/**
 * A mirror in the level: a bevelled plate on a backing board.
 *
 * AN ELEMENT ACTOR RATHER THAN AN ARTICULATED ONE. A mirror CABINET opens; a 600 x 30 x 800 plate on
 * a wall does not, and saying so out loud is the point of it not deriving from the articulated base.
 * See .claude/rules/04-conventions.md.
 */
UCLASS()
class HOUSEFORGE_API AHFMirrorActor : public AHFElementActor
{
	GENERATED_BODY()

public:
	/** Everything this mirror is, in centimetres. Origin at the footprint centre, bottom edge. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ShowOnlyInnerProperties))
	FHFMirrorParams Mirror;

	void ApplyProjectDefaults();
	void ApplyFixture(const FHFFixture& Fixture);

	static bool Builds(EHFFixtureType Type) { return Type == EHFFixtureType::Mirror; }

protected:
	virtual UE::Geometry::FDynamicMesh3 BuildMesh() const override;
};

/**
 * A towel rail in the level: a tube on two wall brackets.
 *
 * Also an element actor, and for the same reason as the mirror: a swing-arm rail moves and this one
 * is not drawn as one.
 */
UCLASS()
class HOUSEFORGE_API AHFTowelRailActor : public AHFElementActor
{
	GENERATED_BODY()

public:
	/** Everything this rail is, in centimetres. Origin at the footprint centre, box bottom. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ShowOnlyInnerProperties))
	FHFTowelRailParams Rail;

	void ApplyProjectDefaults();
	void ApplyFixture(const FHFFixture& Fixture);

	static bool Builds(EHFFixtureType Type) { return Type == EHFFixtureType::TowelRail; }

protected:
	virtual UE::Geometry::FDynamicMesh3 BuildMesh() const override;
};
