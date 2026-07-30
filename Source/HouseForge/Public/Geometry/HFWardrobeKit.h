// Copyright Siddartha G. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Geometry/HFJoineryKit.h"
#include "Model/HFArticulation.h"
#include "Model/HFBuildDefaults.h"
#include "Model/HFTypes.h"
#include "HFWardrobeKit.generated.h"

/**
 * A whole wardrobe, described.
 *
 * THE FIRST FIXTURE. Everything in FHFJoineryKit has until now been reachable only from a test: the
 * six parts are each proven on their own, a composition test lays two of them up by hand, and no
 * component, no collision body and no articulated actor has ever been built from any of it. This
 * struct and FHFWardrobeKit::Build are the joint between the kit and AHFWardrobeActor, and they are
 * deliberately a pure function of their arguments so that everything they decide can be measured
 * without spawning anything.
 *
 * ## Frame
 *
 * Centimetres, in the wardrobe's own local space, which IS the carcass datum the whole kit is
 * specified against: the origin lies on the floor at the front-left corner of the carcass footprint,
 * +X runs along the run, +Y runs back into the unit, +Z is up, and Y = 0 IS THE CARCASS FRONT PLANE.
 * The shutters therefore hang at negative Y, in front of it, and everything the carcass owns stays
 * at Y >= 0.
 *
 * A caller places the whole thing by one transform. The actor's own origin is that corner rather
 * than the middle of the footprint, so a wardrobe against a wall is positioned by the corner it is
 * actually set out from on site.
 *
 * ## Construction figures live in Joinery
 *
 * Everything here divides into DIMENSIONS - what this particular wardrobe measures, read off a
 * drawing - and FIGURES - how joinery is built in this project, which is the same for every fixture
 * in the flat. The second kind all live in Joinery, which is a plain value with no UObject in it.
 *
 * That split is the settings architecture (see FHFBuildDefaults and .claude/rules/04-conventions.md):
 * the composing layer resolves the project's settings into an FHFJoineryDefaults exactly once and
 * hands it in here, and nothing in the geometry layer ever reaches for a settings object. A test
 * builds one of these by hand and gets the compiled-in figures.
 *
 * HouseForge.Architecture.GeneratorsDoNotReadSettings enforces that by scanning the geometry sources
 * for the settings class by NAME - which this comment was failing until it stopped naming it, and
 * which is exactly the right amount of paranoia for a rule whose breach compiles silently.
 */
USTRUCT(BlueprintType)
struct HOUSEFORGE_API FHFWardrobeParams
{
	GENERATED_BODY()

	// ------------------------------------------------------------------------------- dimensions

	/** Length of the run, along +X. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double Width = 240.0;

	/** Front plane to the back of the back panel, along +Y. 60 is the Indian wardrobe standard. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double Depth = 60.0;

	/**
	 * Floor to the top of the topmost carcass - the loft's top where there is one, the body's where
	 * there is not.
	 *
	 * A cornice is NOT inside this. It caps the run and stands above it, so restyling or removing one
	 * never changes the height the wardrobe was drawn at. See CorniceHeight.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double Height = 240.0;

	/**
	 * Bays the run divides into: one shutter each, and one mid partition on every internal boundary.
	 *
	 * Equal divisions of the OVERALL width, because that is what the carcass does - so the leaf and
	 * the box behind it are set out from the same number and cannot drift apart.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "1", ClampMax = "12"))
	int32 BayCount = 4;

	/**
	 * Height the carcass stands off the floor on its recessed base. Zero takes the project's figure.
	 *
	 * A sentinel rather than a plain dimension, for the same reason a shelf stack's board thickness is
	 * one: a drawing that did not mark a toe kick has not asked for a wardrobe standing flat on the
	 * floor, it has said nothing, and the project has a figure for exactly that case.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double PlinthHeight = 0.0;

	/**
	 * A loft: the storage box over a wardrobe, standard in an Indian bedroom.
	 *
	 * Built as a SEPARATE carcass standing on the body, which is how one is actually made - a loft
	 * unit is a box of its own, lifted on afterwards - so the body's top board and the loft's bottom
	 * board are two real boards in contact rather than one board counted twice. Butted, never lapped;
	 * see FHFCarcassParams.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions")
	bool bHasLoft = false;

	/** Overall height of the loft box, taken out of the top of Height. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions",
		meta = (EditCondition = "bHasLoft", ClampMin = "0.0"))
	double LoftHeight = 45.0;

	/**
	 * The moulding capping the run. Zero for no cornice, which is what a full-height wardrobe has.
	 *
	 * Measured above Height rather than out of it, because a cornice is fixed on after the carcass
	 * and a wardrobe drawn 2400 high is 2400 high whether or not one is fitted.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double CorniceHeight = 0.0;

	// ---------------------------------------------------------------------------------- interior

	/**
	 * Shelves in a bay that is shelved out. Zero asks the project for a count at its own spacing.
	 *
	 * A HANGING bay ignores this and takes as many shelves as will still leave a garment room to
	 * hang - see FHFJoineryDefaults::MinHangingClearance, which is the figure that decides it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Interior", meta = (ClampMin = "0", ClampMax = "30"))
	int32 ShelfCount = 0;

	/** Hanging rails. Half the bays get one, taken from the right-hand end. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Interior")
	bool bHangingRail = true;

	// ---------------------------------------------------------------------------------- shutters

	/**
	 * How the body's leaves move: side-hung, or running on tracks.
	 *
	 * Sliding is the commonest wardrobe in a modern Indian flat and hinged is the commonest
	 * everywhere else, which is why this is a parameter rather than an assumption.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Shutters")
	EHFShutterMotion MotionKind = EHFShutterMotion::SideHung;

	/**
	 * How the loft's leaves move. Side-hung even over a sliding body, and that is not an oversight.
	 *
	 * A sliding wardrobe's gear is a track at the head of the body. There is nothing above it for a
	 * loft leaf to run on, and hanging one off a second track standing further out into the room is
	 * neither what is built nor what would look right. Real sliding wardrobes have hinged loft
	 * shutters, and a flap is the other real answer - hence the parameter.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Shutters",
		meta = (EditCondition = "bHasLoft"))
	EHFShutterMotion LoftMotionKind = EHFShutterMotion::SideHung;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Shutters")
	EHFHandleStyle HandleStyle = EHFHandleStyle::Bar;

	/** Glazed leaves: a stile-and-rail frame with a real pane set into a rebate. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Shutters")
	bool bGlassInsert = false;

	// -------------------------------------------------------------------------------------- ends

	/** True when the -X end is on show rather than dying into a wall or the next run. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions")
	bool bLeftEndExposed = true;

	/** True when the +X end is on show. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions")
	bool bRightEndExposed = true;

	// ----------------------------------------------------------------------------------- figures

	/**
	 * How joinery is built in this project: board thicknesses, reveals, clearances, shelf figures.
	 *
	 * The composing layer fills this in from the project's settings before generation. Left at its
	 * defaults it carries exactly the figures compiled into FHFJoineryKit, which is what makes every
	 * hand-built test wardrobe independent of whatever is on the settings page.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Construction",
		meta = (ShowOnlyInnerProperties))
	FHFJoineryDefaults Joinery;

	// ------------------------------------------------------------------------------------ derived

	/** Board the carcasses are cut from, never thinner than a board can be. */
	double BoardThickness() const;

	/** Height of the loft box actually built - zero when there is no loft. */
	double BuiltLoftHeight() const { return bHasLoft ? FMath::Max(LoftHeight, 0.0) : 0.0; }

	/** Underside of the body carcass, which is the top of the plinth. */
	double BodyBottomZ() const { return FMath::Max(PlinthHeight, 0.0); }

	/** Top of the body carcass, which is the underside of the loft where there is one. */
	double BodyTopZ() const { return Height - BuiltLoftHeight(); }

	double BodyHeight() const { return BodyTopZ() - BodyBottomZ(); }

	/** Bays, never less than one. */
	int32 Bays() const { return FMath::Clamp(BayCount, 1, 12); }

	/** Width of one bay's module: what a shutter closes and what the carcass divides into. */
	double ModuleWidth() const { return Width / static_cast<double>(Bays()); }

	/** False when the parameters do not describe a wardrobe that can be built. */
	bool IsValid() const;
};

/**
 * A composed wardrobe: one fixed shell and one entry per part that moves.
 *
 * A plain struct rather than a USTRUCT because it carries meshes by value, exactly as FHFMeshPart
 * does. Still pure data - nothing here has touched a world, an actor or an asset.
 *
 * The sub-assemblies are kept alongside the merged shell on purpose. A clearance BETWEEN two of them
 * cannot be measured once they are one mesh, and the clearances are what a composition is: a shelf
 * sharing a plane with the side it sits in, a plinth measured off the wrong datum, a cornice
 * floating off the carcass. Every one of those looks correct in the merged result.
 */
struct HOUSEFORGE_API FHFWardrobeBuild
{
	/** Everything fixed, merged, in wardrobe-local space. What the actor's BuildMesh returns. */
	UE::Geometry::FDynamicMesh3 Shell;

	/** Every leaf, each in its own local space with its pivot on the origin. */
	TArray<FHFMeshPart> Parts;

	UE::Geometry::FDynamicMesh3 Carcass;
	UE::Geometry::FDynamicMesh3 Loft;
	UE::Geometry::FDynamicMesh3 Plinth;
	UE::Geometry::FDynamicMesh3 Cornice;

	/** One per bay of the body, in bay order, already placed in wardrobe space. */
	TArray<UE::Geometry::FDynamicMesh3> Shelves;

	/** The parameters each sub-assembly was actually built from, for a caller that must measure. */
	FHFCarcassParams CarcassParams;
	FHFCarcassParams LoftParams;
	FHFPlinthParams PlinthParams;
	FHFCorniceParams CorniceParams;

	/** The body's leaf as it was set out, before its hand and its bay were applied. */
	FHFShutterParams ShutterParams;

	/** The shelf stack in each bay of the body, in bay order, as sanitised. */
	TArray<FHFShelfStackParams> ShelfParams;

	/**
	 * Where the outermost closed shutter face sits, in wardrobe space. Negative.
	 *
	 * The plane the toe kick and the cornice projection are both measured from, because it is the
	 * plane somebody standing in front of the wardrobe actually sees.
	 */
	double ShutterFaceY = 0.0;

	/** Bays given a hanging rail, taken from the right-hand end. */
	int32 HangingBayCount = 0;

	/** False when the parameters described nothing that could be built. */
	bool bValid = false;
};

/**
 * Composing the joinery kit into a wardrobe.
 *
 * Pure, like every other generator here: parameters in, meshes and parts out, no world, no actor,
 * no editor, no asset loading and no settings object - see .claude/rules/04-conventions.md. The
 * actor above it owns the parameters, resolves the project's figures into them and hangs the parts
 * on components; none of that is visible from in here.
 */
class HOUSEFORGE_API FHFWardrobeKit
{
public:
	/**
	 * The parameters actually used, clamped so the wardrobe they describe can be built.
	 *
	 * Public because the clamping is a real answer rather than an implementation detail. A loft too
	 * shallow to hold anything is dropped rather than built as a 24 mm slot, and a caller - or a
	 * test - needs to be able to ask what was built rather than what was asked for.
	 */
	static FHFWardrobeParams Sanitise(const FHFWardrobeParams& Params);

	/**
	 * The whole wardrobe: carcass, plinth, shelves, rails, cornice, and a part per leaf.
	 *
	 * @return A build with bValid false and empty meshes when the parameters describe no wardrobe.
	 *         Never a degenerate one - an empty shell appends harmlessly, where a sliver carries
	 *         through every volume measurement taken afterwards.
	 */
	static FHFWardrobeBuild Build(const FHFWardrobeParams& Params);

	/** Part id of the body leaf closing a bay: Shutter0, Shutter1, ... left to right. */
	static FName ShutterPartId(int32 Bay);

	/** Part id of the loft leaf over a bay: Loft0, Loft1, ... left to right. */
	static FName LoftPartId(int32 Bay);

	/**
	 * Fitting gap on a shelf end, in centimetres.
	 *
	 * 1 mm, and it is not slack. It is what stops the shelf end sharing a plane with the carcass side
	 * it sits between, which is the commonest way a generated cabinet acquires z-fighting - invisible
	 * until the thing is lit and the camera moves. Every real shelf is cut a millimetre under for the
	 * same reason it is here: you have to be able to get it in.
	 */
	static constexpr double ShelfEndGap = 0.1;

	/**
	 * Shelves that will fit over a hanging rail and still leave a garment room to hang.
	 *
	 * The rule that makes MinHangingClearance mean something. GenerateShelfStack divides a bay into
	 * EQUAL compartments, so shelf count and hanging clearance are the same decision: one shelf in a
	 * 176 cm bay leaves 87 cm under the rail, and nothing hangs in that. Asking for a realistic shelf
	 * count and a rail together is how a wardrobe ends up geometrically perfect and useless.
	 *
	 * So the count is chosen against the clearance rather than the other way round: the most shelves
	 * whose top compartment still clears the rail drop plus the required clearance, and none at all
	 * when even an empty bay will not. A stack that cannot clear it at zero shelves has its rail
	 * refused by SanitiseShelfStack, which is the honest answer and is how a caller tells "no room to
	 * hang" from "not asked for".
	 *
	 * @param MaxCount The most shelves worth considering - what the bay would get if it were shelved.
	 */
	static int32 ShelvesOverHangingRail(double ClearHeight, double ShelfThickness, double RailDrop,
		double RequiredClearance, int32 MaxCount);
};
