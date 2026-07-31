// Copyright Siddartha G. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Actors/HFArticulatedActor.h"
#include "Geometry/HFWardrobeKit.h"
#include "Model/HFTypes.h"
#include "HFWardrobeActor.generated.h"

/**
 * A wardrobe in the level: carcass, plinth, shelves, hanging rails, loft, cornice, and leaves that
 * actually open.
 *
 * THE FIRST PRODUCTION CONSUMER OF THE JOINERY KIT. Until this actor existed, FHFJoineryKit was
 * referenced only by itself and by test files - no UDynamicMeshComponent had ever been built from a
 * kit mesh, no collision cooked from one, no hand-edit flag raised on a shutter and no pose carried
 * across a rebuild. The framework was proven on doors and the kit was proven on meshes, and the seam
 * between them was the untested part. This is that seam.
 *
 * ## What it owns
 *
 * Its parameters, like every other element actor - see .claude/rules/04-conventions.md. The house
 * spec is the import and export format, not a live second source of truth, so the spec's FHFFixture
 * is read ONCE by ApplyFixture and the actor is the thing that gets edited afterwards. Change a bay
 * count in the details panel and only this wardrobe rebuilds.
 *
 * ## Where the project's figures come in
 *
 * ApplyProjectDefaults, called by the composing layer before the first generation - never inside a
 * generator. FHFWardrobeKit::Build is a pure function of FHFWardrobeParams and reaches for nothing,
 * which is what lets the whole composition be measured headlessly.
 *
 * ## What moves
 *
 * Every leaf is its own part on its own component, with its own pivot, motion, axis and travel
 * limit, posed by a normalised open amount. A hinged run has one leaf per bay; a sliding run has two
 * leaves whatever the carcass behind them is divided into, because a sliding leaf passes its
 * neighbour rather than swinging clear of it. Nothing about a wardrobe is welded into one mesh.
 *
 * ## What it does not do yet
 *
 * No internal drawers. FHFFixtureParams::DrawerCount is not read here, and that is a decision rather
 * than an omission: a drawer inside a wardrobe is an INTERLOCK - it cannot come out until the leaf in
 * front of it is open - and the threshold at which a particular leaf swings clear of a particular
 * drawer front is a measured figure, not a guessed one. HouseForge.Joinery.InternalDrawerInterlock
 * bisects it for one composition and gets 0.8916, which is just under square. Declaring a threshold
 * this actor had not measured would produce a wardrobe that looks right and drives a drawer through
 * its own shutter, so drawers land with the chest and the vanity in milestone 9 instead.
 */
UCLASS()
class HOUSEFORGE_API AHFWardrobeActor : public AHFArticulatedActor
{
	GENERATED_BODY()

public:
	/**
	 * Everything this wardrobe is, in centimetres, in its own local space.
	 *
	 * The actor's origin is the front-left corner of the carcass footprint on the floor - the corner
	 * a wardrobe is actually set out from on site - and not the middle of it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ShowOnlyInnerProperties))
	FHFWardrobeParams Wardrobe;

	/**
	 * Seeds the project's construction figures. Called by the composing layer, not by generation.
	 *
	 * The only line in this class that knows a settings object could exist. By the time the generator
	 * runs, everything it needs is already on the actor - see .claude/rules/04-conventions.md.
	 */
	void ApplyProjectDefaults();

	/**
	 * Reads a spec fixture's dimensions into the parameters. Call after ApplyProjectDefaults.
	 *
	 * The spec is in Unreal centimetres by the time it reaches an actor: AHFHouseActor::SetSpec
	 * converts exactly once, at ingest. Nothing here converts anything.
	 */
	void ApplyFixture(const FHFFixture& Fixture);

	/**
	 * Where a wardrobe standing at this fixture goes, and which way it faces.
	 *
	 * The rotation on a fixture says which way the RUN lies, and a run lies the same way whichever
	 * of its two faces is against the wall - so the drawing's angle alone cannot say which way a
	 * wardrobe faces, and a wardrobe facing the wrong way is a wall of shutters facing a wall. The
	 * wall it backs onto settles it: the back goes against AnchorWallId, and the rotation is turned
	 * through half a turn when it would have put the front there.
	 *
	 * With no anchor wall the rotation is taken as given, which is all there is to go on.
	 *
	 * @param FloorZ Finished floor level of the room the fixture stands in.
	 * @param AnchorWall The wall it backs onto, or null.
	 */
	static FTransform PlacementFor(const FHFFixture& Fixture, double FloorZ, const FHFWall* AnchorWall);

	/** Part id of the body leaf closing a bay, left to right. */
	static FName ShutterPartId(int32 Bay) { return FHFWardrobeKit::ShutterPartId(Bay); }

	/** Part id of the loft leaf over a bay, left to right. */
	static FName LoftPartId(int32 Bay) { return FHFWardrobeKit::LoftPartId(Bay); }

protected:
	virtual UE::Geometry::FDynamicMesh3 BuildMesh() const override;
	virtual void BuildParts(TArray<FHFMeshPart>& OutParts) const override;
};
