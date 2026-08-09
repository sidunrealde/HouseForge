// Copyright Siddartha G. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Model/HFArticulation.h"
#include "Model/HFTypes.h"
#include "HFCurtainKit.generated.h"

/**
 * Curtains: the fabric that hangs in a pelmet, and DRAWS.
 *
 * ## What a curtain actually is, in millimetres
 *
 * A curtain is not a panel. It is a length of fabric two to two and a half times as long as the
 * track it hangs on, gathered by its heading into folds, and it is that ratio - the FULLNESS - which
 * makes the folds. 1.0 is a bedsheet nailed over a window; 1.5 is a cheap ready-made that hangs in
 * shallow scallops; 2.0 to 2.5 is a made-to-measure curtain and is what every drawing in this
 * reference set implies. So Fullness is a first-class parameter here and the fold depth is DERIVED
 * from it, not the other way round: a curtain is as deep as its own excess fabric makes it.
 *
 * The heading is what turns that excess into a repeat:
 *
 *   Pinch pleat   Groups of two or three pinches sewn into the top, 100-140 mm apart along the
 *                 track, each consuming 150-180 mm of fabric. The fabric between them bows. The
 *                 dressiest of the four and the one a pelmet is designed around, because the pleat
 *                 heads and the hooks they carry are ugly and want hiding.
 *   Pencil pleat  A drawn gathering tape, 75-90 mm deep, pulled up to the track width. Denser and
 *                 shallower folds than a pinch pleat at the same fullness, on a 80-110 mm repeat.
 *   Eyelet        Rings punched through the fabric, running direct on a POLE at 150-200 mm centres.
 *                 It is the one heading a pelmet cannot conceal, because there is no track and the
 *                 pole is the point; noted here and deliberately not what this flat gets.
 *   Wave          Corded tape on 60 or 80 mm centres clipped to a purpose-made track. The folds are
 *                 an even sine wave top to bottom rather than pinched at the head, and it is the
 *                 easiest of the four to state as geometry - which is why the fold model below is a
 *                 sine wave regardless of the heading, and the heading only moves the numbers.
 *
 * Fold pitch and fold depth are not independent once fullness is fixed. For fabric hanging in a
 * sine wave of wavelength L and peak-to-peak depth H, the arc length per wavelength over L is the
 * fullness, and that ties H to L: at 2.0 fullness H is about 0.83 L, at 2.2 it is about 0.95 L. So a
 * 140 mm repeat is a 116 mm deep curtain and a 200 mm repeat is a 165 mm deep one. That is the
 * arithmetic that decides whether a curtain fits behind its pelmet, and FoldPitch exists so a
 * caller can trade one against the other rather than declaring a depth that its own fullness
 * contradicts. See DepthForFullness and FullnessFor, which are each other's inverse.
 *
 * DROP is track to hem. Floor length stops 10-15 mm above the finished floor so it does not sweep
 * the tiles; sill length stops 10-15 mm above the sill; apron length hangs 100-150 mm below it.
 * Nothing here decides which - the composing layer measures the room and hands over a drop.
 *
 * STACK-BACK is what the curtain occupies when it is drawn open, and it is the figure that decides
 * how wide a pelmet has to be. A drawn-back lined curtain is a rope of cloth about a FIFTH of its
 * hung width - mostly air, since a 1090 mm leaf of 1.2 mm cloth at 2.0 fullness is only 260 mm2 of
 * fabric in a bundle 200 mm wide by 120 mm deep. StackRatio carries that fifth. A designer sizing a
 * track adds it to each side of the window so the drawn curtain clears the glass, which is why a
 * 1500 window gets a 2200 pelmet and not an 1800 one.
 *
 * StackWidth and OpenApertureWidth are those figures, answerable without building anything - which
 * is what lets a pelmet be checked against the curtain it is supposed to hide before either exists.
 * They run a little WIDE of the real thing on purpose: the leading fold is rigid and cannot press,
 * so the built stack is one fold's width plus a fifth of the rest. Erring wide is the safe direction
 * for a pelmet check, because it can only ever ask for more allowance than the cloth needs.
 *
 * ## Sheers, and why there are none here
 *
 * The full domestic answer is two layers on two tracks - a sheer at the back for daytime privacy
 * and a lined main curtain in front of it - with the tracks 60-80 mm apart. That needs a pelmet
 * 200-250 mm deep, because each layer still wants its own 100 mm of fold depth plus clearance.
 *
 * The reference flat's pelmets are drawn 180 deep. FHFPelmetParams::ConcealedCurtainDepth says what
 * that leaves: about 135 mm of hanging depth, which is one curtain at 2.0 fullness and is not two of
 * anything. A sheer squeezed into 55 mm of depth is a flat panel with a ripple in it, and a flat
 * panel is exactly the lie this kit exists to avoid. So the double layer is stated, measured, and
 * not built. A deeper pelmet is what would buy it, and that is a drawing change rather than a
 * geometry one.
 *
 * ## What moves, and how honest it is
 *
 * A curtain DRAWS, so it moves, and it moves the way the real thing does: each fold hangs on its own
 * glider and the gliders converge along the track. Every fold is therefore its own part with its own
 * slide, its own axis and its own travel - the fold nearest the stack end barely moves and the
 * leading fold travels nearly the whole leaf - which is what makes the fabric GATHER as it goes
 * rather than translate. One part per leaf, sliding, would be a blind.
 *
 * The honest limit: a fold's SHAPE is rigid, so real fabric compression - the fold deepening and
 * crushing as it is pressed against its neighbour - is not modelled. What is modelled is the
 * carriers converging, which is the motion a person sees, and the folds nest into one another as
 * they close up. Inside a drawn-back bundle that nesting is invisible; it is a mass of the same
 * cloth. StackSpread fans the bundle slightly in depth so it reads as a bundle rather than as a
 * flattened stack, and it is clamped to whatever depth the pelmet has left.
 *
 * Modelling true compression needs a fold to change shape between poses, which no rigid part can
 * do, or the parts to hang off one another so a concertina's hinges can travel - which
 * AHFArticulatedActor deliberately does not allow ("Parts hang off the shell, never off each
 * other"). Neither is worth having for a bundle nobody can see into.
 *
 * ## Frame
 *
 * Centimetres. Origin at the CENTRE of the track run in plan, ON the track line, with Z = 0 at the
 * GLIDER LINE and the fabric hanging DOWN into negative Z to -Drop. +X along the run, +Y BACK
 * towards the wall.
 *
 * Hung from the top rather than stood on the bottom, and that is the one place this kit departs from
 * the mirror-and-pelmet convention of Z = 0 at the bottom edge. A curtain's fixed datum is the track:
 * lengthen the drop and the hem moves, never the heading. With the hem as the origin, every change of
 * drop would silently move the thing the actor is screwed to.
 *
 * The fabric snakes about y = 0, so the track's own centreline is where the composing layer puts
 * this origin - see FHFPelmetParams::TrackCentreY.
 *
 * ## Purity
 *
 * Parameters in, meshes out. No world, no actor, no editor, no asset loading, no settings object.
 * See .claude/rules/04-conventions.md.
 */

/** Which heading gathers the fabric. Changes the repeat and the fullness, not the construction. */
UENUM(BlueprintType)
enum class EHFCurtainHeading : uint8
{
	/** Sewn pinches on a 100-140 mm repeat, hooks hidden in a pelmet. The default here. */
	PinchPleat,

	/** Drawn gathering tape: denser, shallower folds on a 80-110 mm repeat. */
	PencilPleat,

	/** Rings on a pole at 150-200 mm centres. Deep soft waves, and nothing conceals it. */
	Eyelet,

	/** Corded tape on 60-80 mm centres. An even sine wave top to bottom. */
	Wave
};

/** How the run parts, and which end the fabric stacks at. */
UENUM(BlueprintType)
enum class EHFCurtainDraw : uint8
{
	/** Two leaves meeting in the middle, stacking to both ends. What a wide opening wants. */
	Pair,

	/** One leaf across the whole run, stacking at the -X end. */
	SingleStackLeft,

	/** One leaf across the whole run, stacking at the +X end. */
	SingleStackRight
};

USTRUCT(BlueprintType)
struct HOUSEFORGE_API FHFCurtainParams
{
	GENERATED_BODY()

	/** Clear length of track the gliders run on, end stop to end stop. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double TrackWidth = 190.0;

	/** Glider line to hem. Floor length is the floor-to-track height less 1.5 cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double Drop = 220.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	EHFCurtainDraw Draw = EHFCurtainDraw::Pair;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	EHFCurtainHeading Heading = EHFCurtainHeading::PinchPleat;

	/**
	 * Fabric width over track width. THE NUMBER THAT MAKES THE FOLDS.
	 *
	 * 2.0 is a made-to-measure lined curtain and 2.5 is a generous one; below 1.6 the fabric hangs
	 * nearly flat and there is no point drawing folds at all.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Fabric", meta = (ClampMin = "1.0"))
	double Fullness = 2.0;

	/**
	 * The repeat: one fold out and back, measured along the track with the curtain closed.
	 *
	 * Defaulted for the pinch pleat this flat uses. HeadingRepeat() is what a heading would choose
	 * on its own; this is what the caller actually gets, so a drawing that states a repeat is
	 * honoured.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Fabric", meta = (ClampMin = "1.0"))
	double FoldPitch = 14.0;

	/**
	 * What the repeat presses to when the curtain is drawn back, as a fraction of itself.
	 *
	 * A fifth: a drawn-back lined curtain is about a fifth of its hung width. Below about 0.12 the
	 * folds would occupy less track than the cloth in them is thick; above about 0.35 the curtain has
	 * not really been drawn.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Fabric",
		meta = (ClampMin = "0.05", ClampMax = "0.6"))
	double StackRatio = 0.18;

	/** Cloth thickness, lining included. 1.2 mm is a lined cotton; a sheer is a third of it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Fabric", meta = (ClampMin = "0.01"))
	double FabricThickness = 0.12;

	/** The stiffened band at the top the hooks are sewn into. 75-90 mm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Fabric", meta = (ClampMin = "0.0"))
	double HeadingHeight = 8.0;

	/** The turn-up at the bottom. Doubled cloth, which is why the hem hangs straight. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Fabric", meta = (ClampMin = "0.0"))
	double HemHeight = 8.0;

	/** Running clearance between a leaf's outer edge and the end stop, at each end. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double EndGap = 1.0;

	/**
	 * The deepest the folds may hang, front to back. Zero leaves the fullness to decide.
	 *
	 * A PELMET'S NUMBER, not a curtain's. Fabric hangs as deep as its own fullness makes it and stops
	 * only when something is in the way; here the something is a 180 mm pelmet with a track in it.
	 * Clamping DEPTH rather than refusing to build is the honest answer to a curtain that will not
	 * fit - the fabric flattens and the fullness it actually delivers drops, which is what happens
	 * on site when somebody hangs a 2.5 curtain in a shallow box. AchievedFullness reports it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double MaxFoldDepth = 0.0;

	/**
	 * Hairline between one fold part and the next, along the track.
	 *
	 * Every fold is its own solid, so consecutive folds present two end faces to each other. Butted,
	 * those faces are coincident planes - the flashing FHFCoplanarScan exists to catch - so they are
	 * parted by a millimetre and a half. Below anything a camera resolves at curtain distance, and it
	 * is a real feature of the object anyway: made-up curtains have a seam every width or two.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Fabric", meta = (ClampMin = "0.0"))
	double FoldGap = 0.15;

	/**
	 * How far each successive fold in a stack fans back in depth, so the bundle reads as a bundle.
	 *
	 * A drawn-back curtain is a rope of cloth deeper than the hanging fabric was, because the folds
	 * pile against one another rather than flattening. Rigid folds cannot deepen, so instead they are
	 * given a small component of travel across the track as well as along it. Clamped against
	 * MaxFoldDepth so a fanned bundle never comes through the pelmet the flat fabric fitted inside.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Fabric", meta = (ClampMin = "0.0"))
	double StackSpread = 0.35;

	/** Segments one fold's plan curve is drawn in. Twelve welds smooth and reads at any distance. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Fabric", meta = (ClampMin = "4"))
	int32 FoldSegments = 12;

	/**
	 * Thickest the cloth gets anywhere down the drop, as a multiple of FabricThickness.
	 *
	 * The heading: tape, buckram and the doubled cloth of the pleat. Stated here rather than only in
	 * the section table it comes from, because it is what every depth budget below has to allow for -
	 * a fold is as deep as its centreline plus half the cloth on each side of it, and a clamp that
	 * forgot the cloth would put a curtain that "fits" 2 mm through its own fascia. The table reads
	 * this constant, so the two cannot drift apart.
	 */
	static constexpr double MaxThicknessFactor = 2.6;

	/** How far the cloth's own thickness reaches past the fold's centreline, on one side. */
	double ClothReachAllowance() const { return FabricThickness * MaxThicknessFactor * 0.5; }

	/** True when two leaves meet in the middle rather than one running the whole way. */
	bool IsPair() const { return Draw == EHFCurtainDraw::Pair; }

	int32 LeafCount() const { return IsPair() ? 2 : 1; }

	/** Track between the two end stops' clearances: the run the fabric has to cover. */
	double ClearWidth() const { return FMath::Max(TrackWidth - 2.0 * EndGap, 0.0); }

	/** What one leaf covers when the curtain is shut. */
	double LeafWidth() const { return ClearWidth() / FMath::Max(LeafCount(), 1); }

	/** Folds in one leaf, at least one, chosen so the repeat divides the leaf exactly. */
	int32 FoldsPerLeaf() const
	{
		return FMath::Max(1, FMath::RoundToInt(LeafWidth() / FMath::Max(FoldPitch, 0.01)));
	}

	/** The repeat as built: FoldPitch rounded so a whole number of folds fills the leaf. */
	double BuiltFoldPitch() const { return LeafWidth() / FMath::Max(FoldsPerLeaf(), 1); }

	/** The repeat this heading would choose for itself, in centimetres. */
	double HeadingRepeat() const;

	/** Peak-to-peak fold depth the declared fullness produces at the built repeat. */
	double NaturalFoldDepth() const;

	/** Fold depth as built: the natural depth, flattened if MaxFoldDepth is in the way. */
	double FoldDepth() const;

	/** The fullness the built fold depth actually delivers, which is what a flattened curtain lost. */
	double AchievedFullness() const;

	/** The pressed repeat inside a drawn-back stack. */
	double StackPitch() const { return BuiltFoldPitch() * FMath::Max(StackRatio, 0.0); }

	/**
	 * How much of the run one drawn-back leaf occupies, measured along the track.
	 *
	 * The leading fold keeps its own width - it is rigid and cannot press - and every fold behind it
	 * is pressed to StackPitch. So this reads a little wide of a real stack, which is the safe
	 * direction: a pelmet sized against it has more allowance than the cloth needs, never less.
	 */
	double StackWidth() const
	{
		return BuiltFoldPitch() + FMath::Max(FoldsPerLeaf() - 1, 0) * StackPitch();
	}

	/** Clear run left once every leaf is drawn back. THE MEASUREMENT THAT SAYS THE CURTAIN OPENS. */
	double OpenApertureWidth() const
	{
		return FMath::Max(ClearWidth() - LeafCount() * StackWidth(), 0.0);
	}

	/** How far the bundle fans either side of the track line beyond the fabric's own reach. */
	double SpreadReach() const { return StackSpread * FMath::Max(FoldsPerLeaf() / 2, 0); }

	/** Half the fold depth: how far the hanging fabric's CENTRELINE reaches either side of the track. */
	double FoldReach() const { return FoldDepth() * 0.5; }

	/**
	 * How far the DRAWN-BACK bundle reaches either side of the track line. What has to fit a pelmet.
	 *
	 * Three terms and all three are real: the fold's own centreline, the fan the bundle opens to, and
	 * the cloth's thickness on the outside of the fold. The third is small and was missing once - the
	 * fold measured 107 mm deep and the mesh measured 109, which is a pelmet check 2 mm optimistic in
	 * exactly the place it cannot afford to be.
	 *
	 * Never more than half MaxFoldDepth: FoldDepth gives way rather than the other way round, because
	 * a bundle standing through the fascia is the failure and a slightly flatter fold is not.
	 */
	double StackedReach() const { return FoldReach() + SpreadReach() + ClothReachAllowance(); }

	bool IsValid() const
	{
		return TrackWidth > 4.0 && Drop > 5.0 && FabricThickness > 0.0 && ClearWidth() > 2.0
			&& FoldPitch > 0.5 && Fullness >= 1.0;
	}
};

/** A composed curtain. Plain data carrying meshes by value. */
struct HOUSEFORGE_API FHFCurtainBuild
{
	/** Everything at once, for a bounds or watertightness check. Not what the actor renders. */
	UE::Geometry::FDynamicMesh3 Shell;

	/**
	 * The fabric that does not move: the fold at each leaf's stack end.
	 *
	 * It is against the end stop already, so drawing the curtain does not move it - the rest of the
	 * leaf piles onto it. A part declaring a slide with zero travel would be a control that lies, so
	 * this is the actor's fixed mesh instead. See .claude/rules/04-conventions.md.
	 */
	UE::Geometry::FDynamicMesh3 Anchors;

	/** Every fold that travels, one per glider, in leaf then fold order. */
	TArray<FHFMeshPart> Parts;

	FHFCurtainParams Used;

	/** Where each leaf's fabric starts and ends along the run when shut, for assertions. */
	TArray<FVector2D> LeafSpans;

	bool bValid = false;
};

class HOUSEFORGE_API FHFCurtainKit
{
public:
	static FHFCurtainParams Sanitise(const FHFCurtainParams& Params);

	/**
	 * The fabric, as one fixed mesh and one part per travelling fold.
	 *
	 * @return A build with bValid false and empty meshes when the parameters describe no curtain.
	 */
	static FHFCurtainBuild Build(const FHFCurtainParams& Params);

	/** Part id of one fold. Leaf 0 is the -X leaf; fold 0 is the one nearest that leaf's stack end. */
	static FName FoldPartId(int32 LeafIndex, int32 FoldIndex);

	/** The fold a single control runs: the leading one, which every other fold is geared to. */
	static FName LeadFoldPartId(int32 LeafIndex, int32 FoldsInLeaf);

	/**
	 * Fullness delivered by a sine fold of this repeat and peak-to-peak depth.
	 *
	 * Arc length over chord, integrated rather than approximated: the small-angle answer is 20% out
	 * at the depths a real curtain hangs to, and this figure is what every pelmet check is sized
	 * against.
	 */
	static double FullnessFor(double Pitch, double Depth);

	/** The inverse: how deep a fold of this repeat has to be to deliver that fullness. */
	static double DepthForFullness(double Pitch, double Fullness);
};
