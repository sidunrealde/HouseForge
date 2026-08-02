// Copyright Siddartha G. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Actors/HFArticulatedActor.h"
#include "Geometry/HFApplianceKit.h"
#include "Geometry/HFWallPlateKit.h"
#include "Model/HFTypes.h"
#include "HFServiceActors.generated.h"

/**
 * The services: what makes a flat work rather than what makes it furnished.
 *
 * Six actors in one pair of files, exactly as HFSanitaryActors.h holds a bathroom and
 * HFFittingActors.h a kitchen. Twenty-one instances between them and not one is individually
 * important - which is the whole difficulty of the group. Sockets and switch plates are SMALL AND
 * NUMEROUS: there are thirteen of them at eye level in every room of the flat, so a wrong height or a
 * wrong stand-off is not one mistake, it is the same mistake thirteen times in the same still.
 *
 * ## Where they sit, and the two rules
 *
 * Everything BOUGHT AND SCREWED TO PLASTER - the sockets, the plates, the consumer unit, the split AC
 * head - is placed by FHFFixturePlacement::OnWallFace: position along the wall and height exactly as
 * drawn, and only the depth corrected until the back lands on the finished face. A plan marks a
 * socket at a precision of "on that wall", and the fitting itself is a fixed size that has to end up
 * against the plaster.
 *
 * Everything BOUGHT AND STANDING ON THE FLOOR - the refrigerator, the washing machine, the two
 * condensing units - is placed by FHFFixturePlacement::AgainstWall, and NOT pulled onto the face.
 * That difference is deliberate and it is not tidiness: an appliance is not screwed to the wall, it
 * is pushed up near it, and every one of them needs air behind it. Pulling a condensing unit flush
 * to a parapet would bury its coil in the masonry it is supposed to be rejecting heat through.
 *
 * ## And the skirting under the two that stand on the floor
 *
 * A refrigerator is not scribed joinery - FHFSkirting::IsScribedJoinery says so, because the board
 * runs on behind it - so the appliance has to stand IN FRONT of that board. Left flat against the
 * drawn back plane it stands 18 mm inside it, permanently, invisibly in plan and in every test of the
 * appliance on its own. Resolved by the composing layer from the project's skirting depth and handed
 * in as a dimension, exactly as FHFDeskParams::SupportSetback already is.
 */

/**
 * A socket or a switch plate in the level: grid, cover, blanked window, and a rocker per gang.
 *
 * ## One actor, two fixture types, one recipe switch
 *
 * The cased goods kit's argument at the smallest scale. A two gang socket and a six gang switch plate
 * are the same construction with a different filling, and what separates them lives in ParamsFor, in
 * one switch, where the two can be read against each other. Splitting them would be two actors that
 * had to agree about a stand-off.
 */
UCLASS()
class HOUSEFORGE_API AHFAccessoryPlateActor : public AHFArticulatedActor
{
	GENERATED_BODY()

public:
	/** Everything this plate is, in centimetres. Origin at the footprint centre, bottom edge. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ShowOnlyInnerProperties))
	FHFAccessoryPlateParams Plate;

	/** Seeds the project's construction figures. Called by the composing layer, not by generation. */
	void ApplyProjectDefaults();

	/** Reads a spec fixture into the parameters. Call after ApplyProjectDefaults. */
	void ApplyFixture(const FHFFixture& Fixture);

	/** What a plate of this type and size is. The recipe switch lives here. */
	static FHFAccessoryPlateParams ParamsFor(const FHFFixture& Fixture);

	static bool Builds(EHFFixtureType Type)
	{
		return Type == EHFFixtureType::PowerSocket || Type == EHFFixtureType::SwitchPlate;
	}

	static FName RockerPartId(int32 Index) { return FHFWallPlateKit::RockerPartId(Index); }

protected:
	virtual UE::Geometry::FDynamicMesh3 BuildMesh() const override;
	virtual void BuildParts(TArray<FHFMeshPart>& OutParts) const override;
};

/**
 * A consumer unit in the level: an enclosure of breakers behind a glazed door.
 *
 * The breakers are SEQUENCED after the door rather than free, because a breaker cannot be thrown
 * through a shut one. See FHFDistributionBoardParams.
 */
UCLASS()
class HOUSEFORGE_API AHFDistributionBoardActor : public AHFArticulatedActor
{
	GENERATED_BODY()

public:
	/** Everything this board is, in centimetres. Origin at the footprint centre, bottom edge. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ShowOnlyInnerProperties))
	FHFDistributionBoardParams Board;

	void ApplyProjectDefaults();
	void ApplyFixture(const FHFFixture& Fixture);

	static bool Builds(EHFFixtureType Type) { return Type == EHFFixtureType::DistributionBoard; }

	static FName DoorPartId() { return FHFWallPlateKit::DoorPartId(); }
	static FName BreakerPartId(int32 Index) { return FHFWallPlateKit::BreakerPartId(Index); }

protected:
	virtual UE::Geometry::FDynamicMesh3 BuildMesh() const override;
	virtual void BuildParts(TArray<FHFMeshPart>& OutParts) const override;
};

/**
 * A split AC indoor unit in the level: a moulded casing high on a wall with a vane in its discharge.
 *
 * ## The ceiling, and why this one is called out
 *
 * A split head hangs at 2200 with its top at 2500, and the rooms it hangs in have false ceilings whose
 * depth is a PROJECT SETTING rather than a fact of the drawing. That is exactly the defect the user
 * reported for the ceiling fans - a fitting sized and placed against the structural slab in a room
 * whose finished soffit somebody moved - and it is not repeated here: FHFCeilingFit::RuleFor already
 * answers Lowers for this type, AHFHouseActor supplies its BUILT height to FitAll, and the fit runs on
 * the spec before the fixture ever reaches this actor.
 *
 * Nothing in the reference flat is squeezed enough to make it move, and that is the reason the
 * mechanism needs a deliberate test at a deep ceiling rather than a reason to trust it - see
 * HouseForge.Services.SplitACLowersUnderADeepCeiling.
 *
 * ## What moves
 *
 * The discharge vane, and the vertical deflectors behind it - one part per blade, each on its own pin.
 * Both are real controls on a real unit; the vane is the one anybody sees, because it is the only
 * thing that tells you whether the machine is running. See FHFApplianceKit::DeflectorPartId for why a
 * ganged set is still one part each.
 */
UCLASS()
class HOUSEFORGE_API AHFSplitACActor : public AHFArticulatedActor
{
	GENERATED_BODY()

public:
	/** Everything this unit is, in centimetres. Origin at the footprint centre, bottom of the box. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ShowOnlyInnerProperties))
	FHFSplitACParams Unit;

	void ApplyProjectDefaults();
	void ApplyFixture(const FHFFixture& Fixture);

	/**
	 * What a split head of this size is before anything else touches it.
	 *
	 * Static and public because the composing layer has to be able to ask how tall one comes out
	 * without spawning it - FHFCeilingFit takes the BUILT envelope, not the drawn box. The same reason
	 * AHFFanActor::ParamsFor and AHFWCActor::ParamsFor are.
	 */
	static FHFSplitACParams ParamsFor(const FHFFixture& Fixture);

	static bool Builds(EHFFixtureType Type) { return Type == EHFFixtureType::ACIndoorUnit; }

	static FName LouvrePartId() { return FHFApplianceKit::LouvrePartId(); }
	static FName DeflectorPartId(int32 Index) { return FHFApplianceKit::DeflectorPartId(Index); }

protected:
	virtual UE::Geometry::FDynamicMesh3 BuildMesh() const override;
	virtual void BuildParts(TArray<FHFMeshPart>& OutParts) const override;
};

/**
 * A condensing unit in the level: a louvred case on feet with a guarded fan that turns.
 *
 * ## Where it sits, and why it is not pulled to the plaster
 *
 * FHFFixturePlacement::AgainstWall, at the drawn position - the one bought fitting in this file that
 * deliberately keeps its drawn gap. A condenser rejects heat through the coil on its BACK, and every
 * installation instruction ever printed asks for a hand's width of air behind it. The two in this flat
 * are drawn 267 and 367 mm off their parapets, which is exactly right and would be thrown away by a
 * rule that put every bought fitting on the face.
 *
 * ## What moves
 *
 * The fan, and it SPINS rather than opens - EHFMotionType::Spin, the same motion and the same
 * TraceOnly collision a ceiling fan's rotor carries, and for the same reasons. See EHFPartCollision.
 */
UCLASS()
class HOUSEFORGE_API AHFCondenserActor : public AHFArticulatedActor
{
	GENERATED_BODY()

public:
	/** Everything this unit is, in centimetres. Origin at the front-left corner of the footprint. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ShowOnlyInnerProperties))
	FHFCondenserParams Unit;

	void ApplyProjectDefaults();
	void ApplyFixture(const FHFFixture& Fixture);

	static bool Builds(EHFFixtureType Type) { return Type == EHFFixtureType::ACOutdoorUnit; }

	static FName FanPartId() { return FHFApplianceKit::CondenserFanPartId(); }

protected:
	virtual UE::Geometry::FDynamicMesh3 BuildMesh() const override;
	virtual void BuildParts(TArray<FHFMeshPart>& OutParts) const override;
};

/**
 * A refrigerator in the level: a cabinet on a plinth grille with two doors that open.
 *
 * ## It IGNORES the ceiling, deliberately
 *
 * FHFCeilingFit::RuleFor says so in as many words: a refrigerator neither shortens nor sinks into the
 * floor, so a ceiling that comes down onto one is a design fault to be reported rather than a fit to
 * be resolved. Nothing here has to know that; it happens on the spec before the fixture arrives.
 */
UCLASS()
class HOUSEFORGE_API AHFRefrigeratorActor : public AHFArticulatedActor
{
	GENERATED_BODY()

public:
	/** Everything this appliance is, in centimetres. Origin at the front-left footprint corner. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ShowOnlyInnerProperties))
	FHFRefrigeratorParams Fridge;

	void ApplyProjectDefaults();
	void ApplyFixture(const FHFFixture& Fixture);

	static bool Builds(EHFFixtureType Type) { return Type == EHFFixtureType::Refrigerator; }

	static FName DoorPartId(int32 Index) { return FHFApplianceKit::FridgeDoorPartId(Index); }

protected:
	virtual UE::Geometry::FDynamicMesh3 BuildMesh() const override;
	virtual void BuildParts(TArray<FHFMeshPart>& OutParts) const override;
};

/**
 * A washing machine in the level: a front loader with a porthole, a detergent drawer and a dial.
 *
 * The dial is not decoration and it is not scope creep. A programme selector is a control a person
 * turns, so it turns - the same rule that gave a geyser its thermostat and a hob its knobs. See
 * .claude/rules/04-conventions.md.
 */
UCLASS()
class HOUSEFORGE_API AHFWashingMachineActor : public AHFArticulatedActor
{
	GENERATED_BODY()

public:
	/** Everything this appliance is, in centimetres. Origin at the front-left footprint corner. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ShowOnlyInnerProperties))
	FHFWashingMachineParams Washer;

	void ApplyProjectDefaults();
	void ApplyFixture(const FHFFixture& Fixture);

	static bool Builds(EHFFixtureType Type) { return Type == EHFFixtureType::WashingMachine; }

	static FName PortholePartId() { return FHFApplianceKit::PortholePartId(); }
	static FName DetergentDrawerPartId() { return FHFApplianceKit::DetergentDrawerPartId(); }
	static FName ProgrammeDialPartId() { return FHFApplianceKit::ProgrammeDialPartId(); }

protected:
	virtual UE::Geometry::FDynamicMesh3 BuildMesh() const override;
	virtual void BuildParts(TArray<FHFMeshPart>& OutParts) const override;
};
