// Copyright Siddartha G. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Model/HFTypes.h"
#include "HFUpholsteryKit.generated.h"

/**
 * Soft goods: a sofa, built as separate forms with real radii rather than as a box.
 *
 * ## FOUR NAMED DESIGNS, AND WHY THEY ARE DESIGNS RATHER THAN NUMBERS
 *
 * EHFCeilingTemplate's argument, made about the object the eye lands on from the front door. A
 * drawing of a living room does not say "a sofa 2100 wide"; it says which sofa is in it, and the
 * difference between the four this kit builds is a difference of SILHOUETTE before it is a
 * difference of dimension:
 *
 *   SquareArm        Straight run, square track arms the full depth, low turned legs. The commodity
 *                    contemporary sofa, and figure for figure what this kit built before there was a
 *                    choice - so a project that names no design gets exactly the sofa it had.
 *   ChaiseSectional  The same run with a chaise returning at one end. THE ONLY ONE WHOSE PLAN IS A
 *                    DIFFERENT SHAPE: an L, with one arm running the full depth of the return, no
 *                    arm at all on the inside of the corner, and a single 1100 mm cushion where a
 *                    person's legs go. What most 2BHK living rooms actually get.
 *   LowProfile       400 seat, 700 back, 120 arms and 180 mm of splayed tapered leg. Reads as a
 *                    different ROOM rather than a different sofa: half a metre more floor and
 *                    skirting shows behind the seating, which is most of what makes a small living
 *                    room look larger.
 *   RolledArm        English roll arm - the arm's top is a genuine half-round in section - on a
 *                    fabric skirt to the floor instead of legs, with a higher 450 seat and an 850
 *                    back. Nothing on it touches the floor except cloth.
 *
 * The DRAWING owns the footprint and the overall height. The DESIGN owns everything else, and the
 * two ergonomic figures it owns hardest are the seat height and the arm height: those are set by the
 * human rather than by the drawn envelope, so a design's 430 seat stays 430 whether its box was
 * drawn 700 tall or 900. What the drawn height moves is the BACK CUSHION, which grows or shrinks to
 * leave FHFSofaParams::BackPanelShow of bare panel above it - see SanitiseSofa.
 *
 * ### What was rejected, and why
 *
 * A CHESTERFIELD. It is what most people mean by "a classic sofa", and its deep buttoning is a grid
 * of re-entrant dimples pulled into the cloth. AppendSoftBox lofts convex rings out of an inner box;
 * a dimple is precisely the shape it cannot make, and WorstConcavityCm < 0.02 - the assertion that
 * caught the folded-corner defect this kit was built around - exists to refuse exactly that
 * geometry. Buttoning suggested by a shallow crease rather than built is the caricature the brief
 * warned about, so the family is represented by its honest half: a roll arm, where the arm really is
 * a half-round and the skirt really is a flat panel.
 *
 * A RECLINER, and a sofa-cum-bed with it. Both MOVE - a footrest swings out, a back folds flat - so
 * under .claude/rules/04-conventions.md each would need its own part, pivot, axis, travel limits and
 * normalised open amount. That is a fixture with a mechanism, not a design swapped in from a spec
 * field, and it is a different request.
 *
 * A TWO-SEATER, A LOVE SEAT, A CORNER UNIT. Not designs, sizes: SeatCount is already derived from
 * the clear width between the arms, so a 1500 box comes out as a two-seater of whichever design it
 * names without anything being added here.
 *
 * ## Why this is its own kit
 *
 * Everything else in this catalogue is made of boards, tubes or pressed ceramic, and every one of
 * them is a flat-faced solid whose whole quality comes from the 1 mm chamfer FHFMeshOps puts on its
 * arrises. Upholstery is the one construction in the flat where that is useless, and the reason is
 * written into FHFBevelParams itself: Fabric's chamfer width is ZERO, deliberately, because "a
 * cushion has no arris to catch light". Correct - and it means a sofa built the way a wardrobe is
 * built comes out with mathematically perfect edges and reads as a cardboard box.
 *
 * A sofa arm is a 60-80 mm radius and a seat cushion is 40 mm. Those are not chamfers, they are the
 * shape of the object, so they are built: see FHFMeshOps::AppendSoftBox, which lofts a box out of
 * rounded-rectangle rings at a true radius. The facets land below FHFBevelParams::MinAngleDegrees,
 * so ComputeShadingNormals welds them smooth and the bevel pass correctly does nothing to them.
 *
 * ## Six forms, not one
 *
 * The same argument FHFBedKit makes about a bed, and harder: a sofa is the largest soft object in
 * the flat and the one the eye lands on from the front door. What makes it read is that its parts
 * are separately legible - the arms stand proud of the base, the cushions sit in a well between
 * them with shadow gaps between each pair, and the back cushions LEAN. A sofa drawn as one 2100 x
 * 900 x 800 solid is a shipping crate however exactly it matches the drawing.
 *
 *   Legs        JoineryCarcass   turned timber, and the only hard material on the object
 *   Base        Fabric           the upholstered plinth the cushions sit in
 *   Arms        Fabric           bolsters standing proud of the base on both sides
 *   Back        Fabric           the panel closing the back, above the arms
 *   Seat        Fabric           one cushion per seat, with a shadow gap between each pair
 *   Back cushions Fabric         one per seat, LEANING - see FHFSofaParams::BackRake
 *
 * ## What moves: nothing, and it is a decision
 *
 * .claude/rules/04-conventions.md asks that anything which moves in the real object moves here. On a
 * plain three-seater the honest answer is that nothing does. Cushions are REMOVABLE, which is not
 * the same as articulated - there is no hinge, no travel limit and no open amount that means
 * anything, and a "cushion lift" invented to satisfy the rule would be a control that lies about the
 * object. A RECLINER's footrest swings and a sofa-bed's back folds flat; the reference flat draws
 * neither. Stated here so the absence reads as an answer rather than an oversight.
 *
 * ## Frame
 *
 * Centimetres, in the sofa's own local space, and the same datum every wall-backed piece uses so
 * that FHFFixturePlacement::AgainstWall can place it: the origin is the front-left corner of the
 * drawn footprint on the floor, +X across the sofa, +Y BACK towards the wall, +Z up.
 *
 * The drawn box is the object. Depth is front to back OVERALL and Height is the top of the back, so
 * a sofa cannot grow out of the wall it stands against or over the picture above it.
 *
 * ## Purity
 *
 * Parameters in, meshes out. No world, no actor, no editor, no asset loading and no settings object
 * - see .claude/rules/04-conventions.md.
 */
USTRUCT(BlueprintType)
struct HOUSEFORGE_API FHFSofaParams
{
	GENERATED_BODY()

	/**
	 * Which of the four this is.
	 *
	 * NEVER EHFSofaDesign::Default by the time the kit sees it. Default means "ask the project", and
	 * the project is a settings object no generator is allowed to read - see
	 * .claude/rules/04-conventions.md. AHFSofaActor::ParamsFor resolves it against FHFSofaDefaults
	 * before any of these figures are chosen, exactly as FHFCeilingTemplates::Apply resolves a named
	 * ceiling into plain numbers before anything validates or builds one. SanitiseSofa treats a
	 * Default that reaches it as SquareArm rather than as nothing.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Design")
	EHFSofaDesign Design = EHFSofaDesign::SquareArm;

	/** Overall width, arm to arm outside. 2100 is a three-seater; 1500 a two. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double Width = 210.0;

	/** Front of the arms to the back of the back panel, overall. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double Depth = 90.0;

	/** Top of the back panel above the floor. THE HEIGHT A DRAWING GIVES A SOFA. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double Height = 80.0;

	/** Seats, and therefore cushions: one seat cushion and one back cushion each. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions",
		meta = (ClampMin = "1", ClampMax = "6"))
	int32 SeatCount = 3;

	/**
	 * Top of the seat cushion above the floor, uncompressed.
	 *
	 * 420-450 is a sofa; 450-460 is a dining chair. The two figures are close and they are not the
	 * same thing, because a sofa is sat back in and a chair is sat up at.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double SeatHeight = 43.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double SeatCushionThickness = 14.0;

	/** Top of the arms above the floor. Between the seat and the back, or it is not an arm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double ArmHeight = 62.0;

	/** Width of each arm in plan. 180-220 is upholstered over a frame; 60 is a metal-framed arm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double ArmWidth = 18.0;

	/** The upholstered panel closing the back. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double BackThickness = 14.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double BackCushionThickness = 14.0;

	/**
	 * How far a back cushion rises above the seat it stands on. ZERO DERIVES IT FROM BackPanelShow.
	 *
	 * Its own dimension rather than "up to the back panel", because what shows ABOVE the cushions is
	 * the panel, and a sofa whose cushions reach the top of their own back has no panel visible at
	 * all - which is the one thing that would make the back read as a slab again.
	 *
	 * The zero is what lets a DESIGN state its proportions without knowing the drawn height. Seat
	 * height and arm height are ergonomic and belong to the design; the top of the back is whatever
	 * the drawing says the sofa is, so the cushion is what takes up the difference. Left absolute,
	 * naming a low-profile design on a box drawn 900 tall would have produced a 260 cushion under
	 * 210 mm of bare upholstered panel - a sofa with a headboard.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double BackCushionHeight = 0.0;

	/**
	 * Bare back panel showing above the back cushions, when BackCushionHeight is derived.
	 *
	 * 60 mm on every design here, and the range is narrow for a reason: under about 30 the panel
	 * disappears and the back is a slab again, over about 120 the sofa grows a headboard. It is the
	 * shadow line along the top of the back, not a proportion.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double BackPanelShow = 6.0;

	/**
	 * How far the top of a back cushion leans back over its own height.
	 *
	 * THE SINGLE FIGURE THAT DECIDES WHETHER THIS READS AS SEATING. Upright, a back cushion is a slab
	 * standing on a seat and the sofa is three boxes in a row; leaned back it is somewhere to sit, and
	 * the lean is what puts a shadow under the cushion's own bottom edge. Built as a shear rather than
	 * a rotation - see FHFSoftBoxParams::RakeY - so the cushion still occupies an answerable box.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double BackRake = 4.0;

	/**
	 * Gap between one cushion and the next, and between the outer cushions and the arms.
	 *
	 * Not a tolerance. It is the shadow line that makes three cushions read as three, and without it a
	 * three-seater's seat is one continuous slab 1740 mm long.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double CushionGap = 1.5;

	/** Clear height under the base. Light under a sofa is most of what makes it read as furniture. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double LegHeight = 12.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double LegDiameter = 6.0;

	/** How far each leg stands in from the two faces of the corner it is under. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double LegInset = 9.0;

	/**
	 * How far each leg leans OUT from vertical, in degrees, measured away from the plan centre.
	 *
	 * The splay that separates a mid-century leg from a table leg, and it is the whole reason
	 * LowProfile reads as a lighter object from across the room: four vertical posts under a box are
	 * plinth, four raked ones are legs. Twelve degrees is where the reference photographs sit.
	 *
	 * The FOOT moves outward, not the top: the leg's head stays under the corner of the base it
	 * carries, and its foot lands LegHeight * tan(splay) further out. SanitiseSofa keeps that foot
	 * inside the drawn box, because a sofa whose legs stand outside their own footprint is a sofa the
	 * drawing cannot be checked against.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions",
		meta = (ClampMin = "0.0", ClampMax = "30.0"))
	double LegSplayDegrees = 0.0;

	/** Radius at the foot as a fraction of the radius at the head. 0.55 is a turned leg; 0.32 a taper. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions",
		meta = (ClampMin = "0.05", ClampMax = "1.0"))
	double LegFootTaper = 0.55;

	/**
	 * The small swell just above the foot of a turned leg. Off for a clean taper.
	 *
	 * A turned leg has an ankle; a machined tapered one is a straight cone from head to floor, and
	 * putting the swell on it is the difference between mid-century and reproduction.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions")
	bool bLegFootFlare = true;

	// --------------------------------------------------------------------------------- the skirt

	/**
	 * A fabric valance hung from the base to just off the floor, in place of visible legs.
	 *
	 * RolledArm's, and it is a silhouette decision rather than a detail: the 120 mm band of daylight
	 * under a sofa is most of what makes it read as loose furniture, so closing it deliberately is
	 * the strongest single change any of these four designs makes. A skirted sofa sits on the floor.
	 *
	 * The legs behind it are not built. They would be four solids nothing can ever see - the skirt
	 * closes the gap on all four sides - and geometry nobody can see is triangles in every render for
	 * the life of the project.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Skirt")
	bool bHasSkirt = false;

	/** How far the hem of the skirt stops above the floor. 15 mm: a skirt clears, it does not sweep. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Skirt", meta = (ClampMin = "0.0"))
	double SkirtGroundClearance = 1.5;

	/** How far the skirt stands inside the drawn faces, so the arms above oversail it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Skirt", meta = (ClampMin = "0.0"))
	double SkirtSetback = 1.0;

	/** Thickness of the valance panel. Lined cloth over a light frame. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Skirt", meta = (ClampMin = "0.1"))
	double SkirtThickness = 1.2;

	// -------------------------------------------------------------------------------- the chaise

	/**
	 * How far the chaise runs FORWARD of the main run's front, in centimetres. Zero derives it.
	 *
	 * The figure that makes an L an L. Derived rather than declared by default, because what a
	 * drawing states about a sectional is its overall DEPTH - the box it occupies - and the main run
	 * behind it is a 900 sofa like any other. So the derivation is simply "the main run keeps its 900
	 * and the chaise takes what is left", which turns a 1400 drawn depth into a 500 return and a 1600
	 * one into a 700.
	 *
	 * Clamped so the main run never falls below MinMainRunDepth, and abandoned altogether below
	 * MinChaiseProjection - see BuiltChaiseProjection.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Chaise", meta = (ClampMin = "0.0"))
	double ChaiseProjection = 0.0;

	/** How wide the chaise is across the sofa. A seat turned through ninety degrees, so 900. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Chaise", meta = (ClampMin = "0.0"))
	double ChaiseWidth = 90.0;

	/** Which end the chaise returns at, looking at the sofa from the front. An L has a handedness. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Chaise")
	bool bChaiseOnLeft = false;

	/**
	 * Shortest return still worth calling a chaise. Below it the design falls back to SquareArm.
	 *
	 * A silent fallback would be worse than a refusal, so it is not silent: FHFSofaBuild::Used
	 * carries the design that was actually built, and a spec that asked for a sectional in a 900 box
	 * comes back saying SquareArm. The alternative - a 200 mm stub off one end - is a shape nobody
	 * ordered and the drawing would still say it was a sectional.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Chaise", meta = (ClampMin = "0.0"))
	double MinChaiseProjection = 40.0;

	/** Shallowest the straight run may be squeezed to by a chaise. 700 is a sofa; less is a bench. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Chaise", meta = (ClampMin = "0.0"))
	double MinMainRunDepth = 70.0;

	/**
	 * How far the base is set in from the drawn box on the front and the two sides.
	 *
	 * The bed's argument, applied to a sofa: the arms and the cushions oversail the base, so the base
	 * lies in their shadow and the sofa's silhouette is its arms rather than one slab from the floor
	 * up. Small, because it is a shadow and not a ledge.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double BaseInset = 2.0;

	/** Radius rolled onto an arm's top edge and its ends in plan. The biggest radius on the object. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Softness", meta = (ClampMin = "0.0"))
	double ArmRoll = 7.0;

	/** Radius rolled onto a cushion's edges. 40 mm is a filled cushion; 10 is a seat pad. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Softness", meta = (ClampMin = "0.0"))
	double CushionRoll = 4.0;

	/** Underside of the seat cushions, which is the top of the base. */
	double DeckZ() const { return FMath::Max(SeatHeight - SeatCushionThickness, 0.0); }

	// ----------------------------------------------------------------- the L, answerable in advance
	//
	// Every one of these collapses to the straight sofa's answer when there is no chaise, which is
	// what lets one build path serve all four designs instead of a second one that would drift.

	/**
	 * The return as built: derived when ChaiseProjection is zero, clamped, and zero when there is none.
	 *
	 * Never mind what Design says on its own - a sectional whose drawn box has no depth to return
	 * into is a straight sofa, and this is the single place that decides so.
	 */
	double BuiltChaiseProjection() const
	{
		if (Design != EHFSofaDesign::ChaiseSectional)
		{
			return 0.0;
		}

		// The main run keeps a sofa's own depth and the chaise takes what is left over.
		const double Asked = ChaiseProjection > 0.0 ? ChaiseProjection : FMath::Max(Depth - 90.0, 0.0);
		const double Room = FMath::Max(Depth - FMath::Max(MinMainRunDepth, 0.0), 0.0);
		const double Built = FMath::Clamp(Asked, 0.0, Room);

		return Built >= FMath::Max(MinChaiseProjection, 0.0) ? Built : 0.0;
	}

	/** True when the L is real. The design that was actually built, whatever was asked for. */
	bool IsSectional() const { return BuiltChaiseProjection() > 0.0; }

	/** The design as built, which is SquareArm for a sectional the drawn box could not hold. */
	EHFSofaDesign BuiltDesign() const
	{
		return (Design == EHFSofaDesign::ChaiseSectional && !IsSectional())
			? EHFSofaDesign::SquareArm : Design;
	}

	/** Front face of the straight run. The chaise is what stands in front of it. */
	double MainRunFrontY() const { return BuiltChaiseProjection(); }

	/** Front to back of the straight run alone. The whole drawn depth when there is no chaise. */
	double MainRunDepth() const { return FMath::Max(Depth - MainRunFrontY(), 0.0); }

	/**
	 * How wide the chaise is as built, ARM INCLUDED: never so wide that the run loses its last seat.
	 *
	 * Overall rather than seat-only, because that is the figure a plan of a sectional dimensions and
	 * the figure a room has to find space for. The seat inside it is this less the arm on its
	 * outboard side - 720 of the 900, which is what a chaise's cushion actually measures.
	 */
	double BuiltChaiseWidth() const
	{
		return IsSectional()
			? FMath::Clamp(ChaiseWidth, 0.0, FMath::Max(Width - 2.0 * ArmWidth - 50.0, 0.0))
			: 0.0;
	}

	/** Inside face of the arm at the -X end. Where the back panel dies into it. */
	double ArmInnerX0() const { return FMath::Min(ArmWidth, Width * 0.5); }

	/** Inside face of the arm at the +X end. */
	double ArmInnerX1() const { return FMath::Max(Width - ArmWidth, Width * 0.5); }

	/** -X edge of the chaise's seat: its own arm's inner face, or where the straight run's end. */
	double ChaiseSeatX0() const
	{
		return bChaiseOnLeft ? ArmInnerX0()
			: FMath::Clamp(Width - BuiltChaiseWidth(), ArmInnerX0(), ArmInnerX1());
	}

	/** +X edge of the chaise's seat. */
	double ChaiseSeatX1() const
	{
		return bChaiseOnLeft ? FMath::Clamp(BuiltChaiseWidth(), ArmInnerX0(), ArmInnerX1())
			: ArmInnerX1();
	}

	/** -X end of the straight run's clear seat span: an arm's face, or the chaise beside it. */
	double InnerX0() const
	{
		return (IsSectional() && bChaiseOnLeft) ? ChaiseSeatX1() : ArmInnerX0();
	}

	/** +X end of the straight run's clear seat span. */
	double InnerX1() const
	{
		return (IsSectional() && !bChaiseOnLeft) ? ChaiseSeatX0() : ArmInnerX1();
	}

	/** Clear width the straight run's cushions and their gaps have to divide up. */
	double InnerWidth() const { return FMath::Max(InnerX1() - InnerX0(), 0.0); }

	/** Front face of the back panel. */
	double BackFaceY() const { return FMath::Max(Depth - BackThickness, 0.0); }

	/** Front edge of the straight run's cushions, in front of its base so they oversail it. */
	double CushionFrontY() const { return MainRunFrontY() + BaseInset * 0.5; }

	/** Front edge of the chaise's cushion: the front of the drawn box, not of the straight run. */
	double ChaiseCushionFrontY() const { return BaseInset * 0.5; }

	/** Top of the skirt, which is the underside of the base it hangs from. */
	double SkirtTopZ() const { return LegHeight; }

	/** Hem of the skirt. */
	double SkirtBottomZ() const { return FMath::Clamp(SkirtGroundClearance, 0.0, FMath::Max(LegHeight - 0.5, 0.0)); }

	/** How far a splayed leg's foot lands outboard of its own head. */
	double LegSplayOffset() const
	{
		return LegHeight * FMath::Tan(FMath::DegreesToRadians(FMath::Clamp(LegSplayDegrees, 0.0, 60.0)));
	}

	/**
	 * Front edge of a back cushion at its BOTTOM, where it is furthest forward.
	 *
	 * Set out backwards from the panel: the cushion's TOP runs into the panel by one gap's worth, so
	 * that leaning it back opens a shadow under its bottom edge rather than a slot behind its top one.
	 */
	double BackCushionY0() const
	{
		return FMath::Max(BackFaceY() + CushionGap - BackRake - BackCushionThickness, CushionFrontY());
	}

	/** Top of the back cushions. What shows between here and Height is the panel. */
	double BackCushionTopZ() const { return SeatHeight + BackCushionHeight; }

	/** Width of one seat cushion, after the gap either side of every one has been taken out. */
	double SeatCushionWidth() const
	{
		const int32 Seats = FMath::Max(SeatCount, 1);
		return FMath::Max((InnerWidth() - CushionGap * (Seats + 1)) / Seats, 0.0);
	}

	/**
	 * Front to back of a seat cushion: what is left once the back cushion and its lean are taken out.
	 *
	 * The measurement that decides whether this is a sofa or a bench. 500 mm is the shallowest seat
	 * anybody sits back in; below about 450 it is a hall bench with a cushion on it.
	 */
	double SeatCushionDepth() const
	{
		return FMath::Max(BackCushionY0() - CushionGap - CushionFrontY(), 0.0);
	}

	/** Back edge of every seat cushion, chaise included: they line up whatever their depths are. */
	double SeatCushionBackY() const { return CushionFrontY() + SeatCushionDepth(); }

	/**
	 * Front to back of the chaise's own cushion. THE MEASUREMENT THAT SAYS IT IS A CHAISE.
	 *
	 * A seat plus the return in front of it, so it comes out around 1100 where a seat cushion is 570 -
	 * which is the whole point of the piece and the one figure that separates a chaise from a corner
	 * seat with a cushion on it.
	 */
	double ChaiseCushionDepth() const
	{
		return IsSectional() ? FMath::Max(SeatCushionBackY() - ChaiseCushionFrontY(), 0.0) : 0.0;
	}

	/** The drawn box IS the object: nothing on a sofa stands above its own back. */
	double BuiltHeight() const { return Height; }

	bool IsValid() const
	{
		return Width > 0.0 && Depth > 0.0 && InnerWidth() > 0.0 && SeatCushionWidth() > 0.0
			&& SeatCushionDepth() > 0.0 && DeckZ() > LegHeight
			&& ArmHeight > SeatHeight && Height > ArmHeight;
	}
};

/**
 * A composed sofa: one merged shell, and every form kept alongside it.
 *
 * The sub-assemblies are here for the reason FHFBedBuild's are: a clearance BETWEEN two of them stops
 * being measurable the moment they are one mesh, and on a sofa nearly everything worth asserting is
 * such a clearance - the gap between one cushion and the next, the arm standing proud of the base,
 * the lean of a back cushion over its own footprint. Merged, none of those has an answer at all.
 *
 * A plain struct rather than a USTRUCT because it carries meshes by value.
 */
struct HOUSEFORGE_API FHFSofaBuild
{
	/** Everything, merged, in sofa-local space. What the actor's BuildMesh returns. */
	UE::Geometry::FDynamicMesh3 Shell;

	/** The turned legs: four under a straight sofa, six under an L, none under a skirt. */
	UE::Geometry::FDynamicMesh3 Legs;

	/** The fabric valance, where the design has one instead of legs. Empty otherwise. */
	UE::Geometry::FDynamicMesh3 Skirt;

	/** The upholstered plinth the cushions sit in. Two boxes on an L, one on everything else. */
	UE::Geometry::FDynamicMesh3 Base;

	/** Left arm then right arm. On an L the chaise's arm is the longer of the two. */
	TArray<UE::Geometry::FDynamicMesh3> Arms;

	/** The panel closing the back, above the arms. */
	UE::Geometry::FDynamicMesh3 Back;

	/** One per seat, left to right. */
	TArray<UE::Geometry::FDynamicMesh3> SeatCushions;

	/** One per seat, left to right, each leaning back by BackRake. */
	TArray<UE::Geometry::FDynamicMesh3> BackCushions;

	/**
	 * The chaise's single long cushion. Empty unless the sofa is a sectional.
	 *
	 * ONE CUSHION, NOT A ROW OF THEM, and that is what a chaise is: an uninterrupted 1100 mm run to
	 * put your legs along. Divided into seat-sized pieces with shadow gaps between them it would be
	 * two more seats facing the wrong way, which is a corner sofa and a different object.
	 */
	UE::Geometry::FDynamicMesh3 ChaiseCushion;

	/**
	 * The parameters actually used, after clamping - INCLUDING the design that was actually built.
	 *
	 * Used.BuiltDesign() is the one to read, not Used.Design: a sectional asked for in a box with no
	 * depth to return into comes back as a SquareArm, and the caller has to be able to find that out.
	 */
	FHFSofaParams Used;

	bool bValid = false;
};

/**
 * Building a sofa out of its six forms.
 *
 * Pure, like every other generator here: parameters in, meshes out, no world, no actor, no editor,
 * no asset loading and no settings object - see .claude/rules/04-conventions.md.
 */
class HOUSEFORGE_API FHFUpholsteryKit
{
public:
	/**
	 * The recipe behind a named design: every figure that is not the drawn box.
	 *
	 * WHERE THE FOUR DESIGNS ACTUALLY DIFFER, in one place, so they can be read against each other -
	 * the same argument AHFTableActor::ParamsFor makes for keeping a coffee table and a dining table
	 * in one switch. Width, Depth and Height come back at the design's NOMINAL size; a caller
	 * overwrites them from the drawing and leaves everything else alone.
	 *
	 * EHFSofaDesign::Default is not a design and comes back as SquareArm. Resolving Default against
	 * the project is the composing layer's job - a generator reads no settings object.
	 */
	static FHFSofaParams FiguresFor(EHFSofaDesign Design);

	/** The parameters actually used, clamped so the sofa they describe can be built. */
	static FHFSofaParams SanitiseSofa(const FHFSofaParams& Params);

	/**
	 * Legs, base, arms, back and two cushions per seat, merged and kept separately.
	 *
	 * @return A build with bValid false and empty meshes when the parameters describe no sofa. Never
	 *         a degenerate one - an empty mesh appends harmlessly, where a sliver carries through
	 *         every volume measurement taken afterwards.
	 */
	static FHFSofaBuild BuildSofa(const FHFSofaParams& Params);
};
