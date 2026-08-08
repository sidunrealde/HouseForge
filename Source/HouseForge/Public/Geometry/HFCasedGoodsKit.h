// Copyright Siddartha G. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Geometry/HFCounterKit.h"
#include "Geometry/HFJoineryKit.h"
#include "Model/HFArticulation.h"
#include "Model/HFBuildDefaults.h"
#include "Model/HFTypes.h"
#include "HFCasedGoodsKit.generated.h"

/**
 * What closes one bay of a carcass.
 *
 * The whole of the difference between a kitchen base unit, a wall unit, a TV console, a nightstand
 * and a shoe rack is which of these each of their bays carries. Seven of milestone 9's thirty types
 * are the same object at different proportions with different fronts, and this enum is that "with
 * different fronts" written down.
 */
UENUM(BlueprintType)
enum class EHFCaseFront : uint8
{
	/** Nothing closes it. An open shelf bay, a plate rack, the void a fridge stands in. */
	None,

	/** One or two leaves filling the bay. See FHFCaseBay::LeafCount for why it may be two. */
	Shutter,

	/** A graduated bank of drawers filling the whole bay. */
	DrawerBank,

	/**
	 * A leaf over a bank of drawers in the bottom of the bay.
	 *
	 * The standard vanity and the standard tall unit. The drawers are at the BOTTOM and the leaf
	 * above them, which is the way round the name reads and the way round both of those are built.
	 */
	ShutterOverDrawer
};

/** What fills a bay behind its front. */
UENUM(BlueprintType)
enum class EHFCaseInterior : uint8
{
	/** Empty. What a bay full of drawers has, and what a sink bay has under its bowl. */
	None,

	/** A stack of shelves at the project's spacing, or at the count the drawing stated. */
	Shelves,

	/** A rail to hang on, with as many shelves over it as still leave a garment room. */
	ShelvesAndRail
};

/** What the bottom of the stack stands on. */
UENUM(BlueprintType)
enum class EHFCaseMount : uint8
{
	/**
	 * A recessed toe-kick base. Every floor-standing run in the flat.
	 *
	 * The recess is what makes a run read as furniture rather than as a box on the floor, and it is
	 * measured from the SHUTTER face rather than from the carcass - see FHFPlinthParams.
	 */
	Plinth,

	/**
	 * Screwed to the wall with nothing under it: a kitchen wall unit, a wall-hung vanity.
	 *
	 * Not merely "a plinth of zero height". A wall-hung unit's underside is a finished surface
	 * somebody looks straight up at from across the room, and its skirting runs UNDERNEATH it - see
	 * FHFSkirting::IsScribedJoinery, where that is the difference between a wall-hung vanity and a
	 * plinth-mounted one.
	 */
	WallHung
};

/**
 * One bay of one carcass: what closes it, and what is behind that.
 *
 * Centimetres and counts only. Nothing here is a position - a bay does not know which bay it is, and
 * the carcass it belongs to divides its own width equally, so the two cannot drift apart.
 */
USTRUCT(BlueprintType)
struct HOUSEFORGE_API FHFCaseBay
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Front")
	EHFCaseFront Front = EHFCaseFront::Shutter;

	/**
	 * Leaves closing one bay. Zero divides the bay at the project's module width, 1 or 2.
	 *
	 * A 800 BASE UNIT HAS TWO DOORS, and that is not a stylistic preference. A hinged leaf past about
	 * 600 sags on its hinges and needs a swing clear of half its own width - which is why
	 * FHFJoineryDefaults::ShutterModuleWidth exists and why the wardrobe divides its run by it. A
	 * single 800 leaf on a kitchen base unit is one of the loudest tells there is that a kitchen was
	 * generated: nobody builds one, and it reads wrong from across the room long before anybody
	 * measures it.
	 *
	 * A bay is divided rather than the carcass, because the CARCASS bay is a real box with a real
	 * partition and a pair of doors on one box is what a real 800 unit is. Two is the limit: three
	 * leaves on one bay is a run that should have been divided instead.
	 *
	 * A lift-up flap is always one whatever its width - it is a single panel on a pair of stays, and
	 * that is what makes a 900 lift-up ordinary where a 900 side-hung leaf is not.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Front",
		meta = (ClampMin = "0", ClampMax = "2"))
	int32 LeafCount = 0;

	/**
	 * How the leaves move. Side-hung unless said otherwise, which is what a cabinet door is.
	 *
	 * A wall cabinet's flap LIFTS, and that is a real difference rather than a detail: a side-hung
	 * leaf at head height stands out into the room where somebody's face is, which is exactly why
	 * lift-up gear is what a modern kitchen wall unit is sold with.
	 *
	 * Sliding is refused here and converted to side-hung by Sanitise. A sliding run is two leaves on
	 * two tracks whatever the carcass behind them is divided into, and the pairing rule that keeps
	 * such a pair from cancelling lives in FHFWardrobeKit - see FHFPartMotion::bMasterOpens. A cased
	 * good that quietly built a sliding pair without it would be a cabinet that reports full travel
	 * on both leaves and never opens.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Front")
	EHFShutterMotion Motion = EHFShutterMotion::SideHung;

	/** Glazed leaves: a stile-and-rail frame with a real pane set into a rebate. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Front")
	bool bGlassInsert = false;

	/** Drawers in a bank. Ignored by a bay with no drawers in it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Front",
		meta = (ClampMin = "0", ClampMax = "12"))
	int32 DrawerCount = 3;

	/** How much deeper the bottom front is than the top one. 2.0 is a real kitchen bank. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Front", meta = (ClampMin = "1.0"))
	double GradationRatio = 2.0;

	/**
	 * ShutterOverDrawer only: height of the drawer band at the bottom. Zero takes a third of the bay.
	 *
	 * Measured rather than shared equally, because a vanity's drawers are a shallow band under a
	 * cupboard and not half the unit.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Front", meta = (ClampMin = "0.0"))
	double DrawerBandHeight = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Interior")
	EHFCaseInterior Interior = EHFCaseInterior::Shelves;

	/** Shelves in the stack. Zero asks the project for a count at its own spacing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Interior",
		meta = (ClampMin = "0", ClampMax = "30"))
	int32 ShelfCount = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Interior")
	EHFShelfMaterial ShelfMaterial = EHFShelfMaterial::Ply;
};

/**
 * One carcass in a stack.
 *
 * A LOFT, A KITCHEN TALL UNIT'S OVERHEAD AND A WALL RUN OVER A BASE RUN ARE THE SAME SHAPE, which is
 * why the stack is here from the first line of this kit rather than added when the second client
 * turns up. A unit is a box of its own, lifted onto the one below, exactly as a wardrobe's loft is -
 * so the lower box's top board and the upper box's bottom board are two real boards in contact
 * rather than one board counted twice.
 */
USTRUCT(BlueprintType)
struct HOUSEFORGE_API FHFCaseUnit
{
	GENERATED_BODY()

	/** Overall height of this box. Zero takes whatever the stack has left over. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ClampMin = "0.0"))
	double Height = 0.0;

	/** Bays this box is divided into. Zero divides it at the project's module width. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ClampMin = "0", ClampMax = "12"))
	int32 BayCount = 0;

	/**
	 * One entry per bay, left to right.
	 *
	 * Empty gives every bay the default; a single entry is applied to all of them, which is what a
	 * run of identical units is; a short list is padded with its last entry rather than refused, so
	 * "the first bay is drawers and the rest are doors" is two entries.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	TArray<FHFCaseBay> Bays;

	/** A back panel. Off for a unit built against a finished wall that is itself the back. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	bool bHasBack = true;

	/** A top board. Off for a base unit under a counter - see FHFCarcassParams::bHasTop. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	bool bHasTop = true;
};

/**
 * A stack of carcasses, divided into bays, each bay carrying a front and an interior.
 *
 * THE SHARED SHAPE OF SEVEN FIXTURE TYPES. A kitchen base unit, a kitchen wall unit, a TV unit, a
 * nightstand, a shoe rack, a vanity and a study table's pedestal are one object at seven sets of
 * proportions, and everything this kit builds already exists in FHFJoineryKit - it is composition,
 * not new primitives.
 *
 * ## Frame
 *
 * Centimetres, in the unit's own local space, which IS the carcass datum the whole joinery kit is
 * specified against: the origin lies at the front-left corner of the carcass footprint at the BOTTOM
 * OF WHAT THE STACK STANDS ON, +X runs along the run, +Y runs back into the unit, +Z is up, and
 * Y = 0 IS THE CARCASS FRONT PLANE. Shutters and drawer fronts therefore hang at negative Y, in
 * front of it, and everything a carcass owns stays at Y >= 0.
 *
 * Z = 0 is the underside of the PLINTH on a floor-standing run and the underside of the CARCASS on a
 * wall-hung one, which is what makes both of them placeable by FHFFixturePlacement::AgainstWall from
 * the fixture's own BaseZ without the composing layer knowing which kind it is.
 *
 * ## Construction figures live in Joinery
 *
 * Exactly as FHFWardrobeParams describes: what this particular unit MEASURES is here, and HOW
 * joinery is built in this project is in Joinery, which the composing layer resolves from the
 * settings once and hands in. Nothing in this file reaches for a settings object - see
 * .claude/rules/04-conventions.md, and HouseForge.Architecture.GeneratorsDoNotReadSettings.
 */
USTRUCT(BlueprintType)
struct HOUSEFORGE_API FHFCasedGoodsParams
{
	GENERATED_BODY()

	/** Length of the run, along +X. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double Width = 120.0;

	/** Front plane to the back of the back panel, along +Y. 58 in a base unit, 30 in a wall unit. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double Depth = 58.0;

	/**
	 * Overall height, floor to the top of the topmost carcass, INCLUDING whatever it stands on.
	 *
	 * A cornice is not inside this, for the same reason it is not inside a wardrobe's: it caps the
	 * run and stands above it, so restyling or removing one never changes the height the run was
	 * drawn at. See CorniceHeight and BuiltHeight.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double Height = 85.0;

	/** Bottom-up: the body, then whatever is stacked on it. Empty builds one unit filling Height. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions")
	TArray<FHFCaseUnit> Units;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions")
	EHFCaseMount Mount = EHFCaseMount::Plinth;

	/**
	 * Height the carcass stands off the floor on its recessed base. Zero takes the project's figure.
	 *
	 * A sentinel for the same reason a wardrobe's is, and resolved in the same place: a drawing that
	 * did not mark a toe kick has not asked for a carcass standing flat on the floor, it has said
	 * nothing, and the project has a figure for exactly that case. Ignored by a wall-hung unit.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double PlinthHeight = 0.0;

	/** The moulding capping the run, standing above Height. Zero for no cornice. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double CorniceHeight = 0.0;

	/**
	 * A stone top on the run. Zero for none, which is every cased good in the kitchen and the bedroom.
	 *
	 * ## Why this is INSIDE Height and a cornice is not
	 *
	 * They are opposite kinds of thing, and the difference is what a drawing means by the figure it
	 * gives. A cornice is a MOULDING STUCK ON TOP of a finished run - restyle it or take it off and
	 * the run is still the height it was drawn - so it stands above Height and BuiltHeight adds it. A
	 * top is a WORKING SURFACE, and the height a vanity is drawn at IS the height of that surface,
	 * because that is the figure a basin, a tap and a mirror are all set out from. Built above Height
	 * instead, a vanity drawn 800 would present its stone at 830 and the basin resolved onto it would
	 * float 30 mm over the china below it.
	 *
	 * So the carcass stack loses the top's thickness rather than the run gaining it - see StackHeight.
	 *
	 * ## Why the counter kit rather than a board
	 *
	 * Because it is the same object. A vanity top and a kitchen worktop are both a slab with a worked
	 * front edge and an upstand at the wall, and the kitchen's already exists with its own tests; a
	 * second one here would be a second answer to a solved question. See FHFCounterParams.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double TopThickness = 0.0;

	/** How the top's front edge is worked. Ignored with no top. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions")
	EHFCounterEdge TopEdge = EHFCounterEdge::Bullnose;

	/** Splashback standing on the top at the wall. Zero for none. Ignored with no top. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double TopUpstandHeight = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Front")
	EHFHandleStyle HandleStyle = EHFHandleStyle::Bar;

	/** True when the -X end is on show rather than dying into a wall or the next run. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions")
	bool bLeftEndExposed = true;

	/** True when the +X end is on show. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions")
	bool bRightEndExposed = true;

	/** How joinery is built in this project. Filled in by the composing layer, never read here. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Construction",
		meta = (ShowOnlyInnerProperties))
	FHFJoineryDefaults Joinery;

	/** Board the carcasses are cut from, never thinner than a board can be. */
	double BoardThickness() const;

	/** Height of the base the stack stands on: the plinth, or nothing at all when wall-hung. */
	double MountHeight() const;

	/** Underside of the bottom carcass. */
	double BodyBottomZ() const { return MountHeight(); }

	/** Height available to the whole stack of carcasses: what the mount and the top have left. */
	double StackHeight() const { return Height - MountHeight() - FMath::Max(TopThickness, 0.0); }

	/** Underside of the top slab, which is the top of the carcass stack. */
	double TopBottomZ() const { return Height - FMath::Max(TopThickness, 0.0); }

	/**
	 * Overall height of what is actually BUILT, cornice included.
	 *
	 * THE DRAWN BOX IS NOT ALWAYS THE OBJECT - the same rule FHFCeilingFit::Fit states for an
	 * extract's bezel. A run of wall cabinets drawn 700 high with a 60 cornice on it stands 760, and
	 * a ceiling fitted to the drawn box would leave 60 mm of moulding inside the plasterboard.
	 */
	double BuiltHeight() const { return Height + FMath::Max(CorniceHeight, 0.0); }

	/** False when the parameters do not describe a unit that can be built. */
	bool IsValid() const;
};

/**
 * A composed cased good: one fixed shell and one entry per part that moves.
 *
 * A plain struct rather than a USTRUCT because it carries meshes by value, exactly as
 * FHFWardrobeBuild does. The sub-assemblies are kept alongside the merged shell for the same reason
 * they are there: a clearance BETWEEN two of them cannot be measured once they are one mesh, and the
 * clearances are what a composition IS.
 */
struct HOUSEFORGE_API FHFCasedGoodsBuild
{
	/** Everything fixed, merged, in unit-local space. What the actor's BuildMesh returns. */
	UE::Geometry::FDynamicMesh3 Shell;

	/** Every leaf and every drawer, each in its own local space with its pivot on the origin. */
	TArray<FHFMeshPart> Parts;

	UE::Geometry::FDynamicMesh3 Plinth;
	UE::Geometry::FDynamicMesh3 Cornice;

	/** The stone top, where there is one, already placed in unit space. */
	UE::Geometry::FDynamicMesh3 Top;

	/** One per unit of the stack, bottom-up, already placed in unit space. */
	TArray<UE::Geometry::FDynamicMesh3> Carcasses;

	/** Shelf stacks and drawer runner channels, already placed. */
	UE::Geometry::FDynamicMesh3 Interior;

	/** The parameters each carcass was actually built from, bottom-up. */
	TArray<FHFCarcassParams> UnitParams;

	/** Underside of each unit in the stack, bottom-up. */
	TArray<double> UnitBaseZ;

	FHFPlinthParams PlinthParams;
	FHFCorniceParams CorniceParams;

	/** What the top was actually cut from, empty-width where there is no top. */
	FHFCounterParams TopParams;

	/** Where the outermost closed front's face sits, in unit space. Negative. */
	double ShutterFaceY = 0.0;

	/** Top of the topmost carcass, which is where a cornice is anchored. */
	double CarcassTopZ = 0.0;

	/** The parameters actually used, after clamping. */
	FHFCasedGoodsParams Used;

	bool bValid = false;
};

/**
 * Composing the joinery kit into a stack of cased goods.
 *
 * Pure, like every other generator here: parameters in, meshes and parts out, no world, no actor, no
 * editor, no asset loading and no settings object - see .claude/rules/04-conventions.md.
 */
class HOUSEFORGE_API FHFCasedGoodsKit
{
public:
	/**
	 * The parameters actually used, clamped so the run they describe can be built.
	 *
	 * Public because the clamping is a real answer rather than an implementation detail: a caller
	 * stacking something on top of this, or a test asserting what should have been produced, needs
	 * the unit heights and bay counts that were resolved rather than the sentinels asked for.
	 */
	static FHFCasedGoodsParams Sanitise(const FHFCasedGoodsParams& Params);

	/**
	 * The whole run: plinth, carcasses, shelves, runner channels, cornice, and a part per front.
	 *
	 * @return A build with bValid false and empty meshes when the parameters describe no unit. Never
	 *         a degenerate one - an empty shell appends harmlessly, where a sliver carries through
	 *         every volume measurement taken afterwards.
	 */
	static FHFCasedGoodsBuild Build(const FHFCasedGoodsParams& Params);

	/** Part id of a leaf: Shutter_<unit>_<bay>_<leaf>, left to right, bottom-up. */
	static FName ShutterPartId(int32 Unit, int32 Bay, int32 Leaf);

	/**
	 * Part id prefix of a bay's drawer bank. FHFJoineryKit::BuildDrawerBank appends the index, so a
	 * bank in unit 0 bay 1 comes out Drawer_0_1_0, Drawer_0_1_1 and so on, top to bottom, each with
	 * its geared runner member as <id>Runner.
	 */
	static FName DrawerPartIdPrefix(int32 Unit, int32 Bay);

	/** Fitting gap on a shelf end. See FHFWardrobeKit::ShelfEndGap - the same 1 mm, and not slack. */
	static constexpr double ShelfEndGap = 0.1;
};
