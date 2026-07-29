// Copyright Siddartha G. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Model/HFArticulation.h"
#include "Model/HFTypes.h"
#include "HFJoineryKit.generated.h"

/**
 * The recessed base a floor-standing carcass sits on.
 *
 * Everything is centimetres, in the plinth's own local space: the origin lies on the floor at the
 * front-left corner of the CARCASS footprint, +X runs along the run, +Y runs back into the unit,
 * +Z is up. Y = 0 is the CARCASS FRONT PLANE - the same datum every other frame in this kit uses,
 * and the reason a carcass can mix them without re-basing anything.
 *
 * The shutters do NOT hang in that plane. Every shutter and drawer front this kit generates is
 * full overlay and stands in front of it, at negative Y, while everything the carcass owns stays at
 * Y >= 0. A toe kick, though, is a thing you see: it is the recess measured from the shutter FACE,
 * not from the carcass behind it. So the two are stated separately - ShutterOverlay says how far in
 * front of the carcass the shutters hang, and FrontRecess is read from there.
 *
 * The back is assumed to run into a wall, which is true of every base run, wardrobe and TV unit in
 * a 2BHK or 3BHK. An island would want its back panel finished too, and that is a parameter to add
 * when there is an island to add it for.
 */
USTRUCT(BlueprintType)
struct HOUSEFORGE_API FHFPlinthParams
{
	GENERATED_BODY()

	/** Footprint width of the carcass this carries, along +X. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ClampMin = "0.0"))
	double Width = 0.0;

	/** Footprint depth of the carcass, its front plane to its back, along +Y. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ClampMin = "0.0"))
	double Depth = 0.0;

	/** Height off the floor. The carcass sits on top: 10 in a kitchen or wardrobe, 8 under a TV unit. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ClampMin = "0.0"))
	double Height = 10.0;

	/**
	 * How far the shutters hang in front of the carcass: their thickness plus the hinge clearance.
	 *
	 * Zero for a run with no doors over its base, and for a caller that genuinely wants the kick
	 * measured off the carcass. Anything else, and it is the figure the shutter was generated with -
	 * FHFShutterParams::Thickness plus BackClearance - because the toe kick is specified from the
	 * face somebody looks at, and a plinth that took its recess off the carcass instead came out
	 * that much deeper than asked for without saying so.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ClampMin = "0.0"))
	double ShutterOverlay = 0.0;

	/**
	 * The toe kick: how far the front panel sits behind the shutter face.
	 *
	 * This one number is what makes a run read as furniture rather than a box on the floor. It puts
	 * the base in its own shadow so the carcass appears to float, and it is the difference between a
	 * kitchen that looks built and one that looks like a wall of cubes.
	 *
	 * 5 to 8 is what a real Indian base unit or wardrobe has. Measured from the shutter face, so it
	 * is read off the finished elevation rather than off the carcass hidden behind it; see
	 * ShutterOverlay, and FrontFaceY for where that puts the panel.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ClampMin = "0.0"))
	double FrontRecess = 5.0;

	/** The same setback at an end on show. An end dying into a wall keeps the full width instead. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ClampMin = "0.0"))
	double EndRecess = 5.0;

	/** Board thickness: 1.8 for faced ply, 0.6 for ply clad in aluminium in a wet kitchen. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ClampMin = "0.0"))
	double PanelThickness = 1.8;

	/** True when the -X end is on show rather than dying into a wall or the next run. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	bool bLeftEndExposed = false;

	/** True when the +X end is on show. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	bool bRightEndExposed = false;

	/**
	 * Where the front panel's face lands, in the plinth's own local space.
	 *
	 * The kick is specified from the shutter face and the frame is measured from the carcass front
	 * plane, so this is the one conversion between them - stated once, here, rather than at every
	 * call site that wants to know where the panel went.
	 */
	double FrontFaceY() const { return FrontRecess - ShutterOverlay; }
};

/**
 * What a shelf is made of.
 *
 * Not a finish choice: it sets the board thickness and, more to the point, how far a shelf spans
 * before it sags. 18 ply takes 900 mm and 8 mm toughened glass takes 600, and a shelf run past
 * either reads as sagging on camera long before anyone measures it.
 */
UENUM(BlueprintType)
enum class EHFShelfMaterial : uint8
{
	/** 18 mm BWP ply, the default for a wardrobe, a shoe rack or a base unit. */
	Ply,
	/** 8 mm toughened glass, for a crockery unit or a display bay behind a glass shutter. */
	Glass
};

/**
 * A run of internal shelves in one bay, with a hanging rail under the top if it is a wardrobe.
 *
 * Describes the clear volume the shelves fill rather than the carcass around it. The carcass
 * generator owns its own board thicknesses, and a stack told to fill 56.3 cm of depth should not
 * have to know that figure came from a 600 body less a 19 shutter and an 18 back.
 */
USTRUCT(BlueprintType)
struct HOUSEFORGE_API FHFShelfStackParams
{
	GENERATED_BODY()

	/** Clear width between the carcass sides, along +X. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ClampMin = "0.0"))
	double Width = 0.0;

	/** Clear depth from the carcass front plane to the back panel, along +Y. 56.3 in a 600 wardrobe. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ClampMin = "0.0"))
	double Depth = 0.0;

	/** Clear height available to the stack, between whatever closes it top and bottom. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ClampMin = "0.0"))
	double Height = 0.0;

	/**
	 * Shelves in the stack, dividing the height into ShelfCount + 1 equal compartments.
	 *
	 * Taken literally. A shoe rack wants 180 mm compartments and a wardrobe wants 375, and choosing
	 * between them is not the generator's business - ShelfCountForClearHeight is where the domain
	 * ladder lives, for callers that want the count picked for them.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ClampMin = "0", ClampMax = "30"))
	int32 ShelfCount = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	EHFShelfMaterial ShelfMaterial = EHFShelfMaterial::Ply;

	/** Board thickness. Zero takes the material's own: 1.8 for ply, 0.8 for toughened glass. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ClampMin = "0.0"))
	double ShelfThickness = 0.0;

	/** How far the shelf front edge sits behind the carcass front plane, clear of a closing shutter. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ClampMin = "0.0"))
	double FrontSetback = 1.0;

	/** Gap left at the back, for a service run or a scribe against an out-of-plumb wall. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ClampMin = "0.0"))
	double BackClearance = 0.0;

	/** A hanging rail slung under the top of the stack, in the compartment above the last shelf. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	bool bHangingRail = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ClampMin = "0.0"))
	double RailDiameter = 2.5;

	/** Rail centre below the top of the stack. 65 mm is the clearance a hanger needs to lift off. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ClampMin = "0.0"))
	double RailDrop = 6.5;

	/**
	 * Break the stack with a mid partition once a shelf would span further than its material takes.
	 *
	 * On by default, because the alternative is a shelf that visibly sags: a 1200 wardrobe bay is
	 * well past what 18 ply carries. Turn it off when the carcass already supplies the partition and
	 * the caller is generating one stack per bay.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	bool bMidPartitionWhenOverspan = true;

	/** Span limit. Zero takes the material's own: 90 for 18 ply, 60 for 8 mm toughened glass. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ClampMin = "0.0"))
	double MaxSpan = 0.0;

	/** A mid partition is 18 ply whatever the shelves it carries are made of. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ClampMin = "0.0"))
	double PartitionThickness = 1.8;
};

/**
 * Which vertical edge of the module a shutter hangs on, seen from the front.
 *
 * Downstream this is the sign of the hinge's MaxAngleDegrees and which side of the hinge axis the
 * leaf is cut on - exactly the handedness mechanism FHFPartMotion describes. What it deliberately
 * does NOT change is which way the leaf faces in its own local space: both hands present their
 * outward face at local Y = 0 looking down -Y, so anything mounted on a leaf is described the same
 * way whichever way that leaf is hung. See FHFJoineryKit::GenerateShutter.
 */
UENUM(BlueprintType)
enum class EHFShutterHinge : uint8
{
	/** Hinged on the left edge of the module, swinging out to the left. */
	Left,
	/** Hinged on the right edge, swinging out to the right. */
	Right
};

/**
 * One hinged shutter leaf, filling one module of a carcass.
 *
 * The sizes here are the MODULE - the bay the shutter closes - and not the leaf. That is the way
 * joinery is actually set out: a carcass is divided into bays and each leaf is cut to its bay less
 * the gap. Driving it the other way round makes a run of shutters drift out of alignment the
 * moment one bay changes width, and the drift is only visible after everything is placed.
 *
 * Defaults are the Indian residential norms: a 19 mm finished leaf (18 mm ply plus 0.8 mm laminate
 * on each face) and a 3 mm reveal.
 */
USTRUCT(BlueprintType)
struct HOUSEFORGE_API FHFShutterParams
{
	GENERATED_BODY()

	/** Width of the bay this shutter closes. 45-60 for a wardrobe; 60 is where a hinged leaf sags. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ClampMin = "0.0"))
	double ModuleWidth = 45.0;

	/** Height of the bay this shutter closes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ClampMin = "0.0"))
	double ModuleHeight = 210.0;

	/** Finished leaf thickness: 1.9 = 1.8 ply plus 0.08 laminate on each face. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ClampMin = "0.0"))
	double Thickness = 1.9;

	/**
	 * Total gap between this leaf and the next, taken half from each side.
	 *
	 * The most load-bearing number in this struct. Without it a run of shutters renders as one
	 * unbroken slab, which is the clearest tell there is that joinery was generated rather than
	 * built: real shutters are separated by a shadow line legible from across a room. It is also
	 * what a hinged leaf needs in order to swing past its neighbour at all.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ClampMin = "0.0"))
	double RevealGap = 0.3;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	EHFShutterHinge Hinge = EHFShutterHinge::Left;

	/**
	 * Swing at open amount 1, unsigned - the direction comes from Hinge.
	 *
	 * 100 degrees rather than 90 because a leaf stopped square to the carcass still stands in front
	 * of the drawer bank behind it, and every concealed hinge sold for this work opens 100-110.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge",
		meta = (ClampMin = "0.0", ClampMax = "180.0"))
	double OpenAngleDegrees = 100.0;

	/** The gap a hinge leaves between the closed leaf's back and the carcass front edges. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ClampMin = "0.0"))
	double BackClearance = 0.1;

	/** Glazed leaf: a stile-and-rail frame with a pane set into a rebate. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	bool bGlassInsert = false;

	/** Width of the frame members around a pane. Ignored on a solid leaf. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge",
		meta = (EditCondition = "bGlassInsert", ClampMin = "0.0"))
	double StileWidth = 6.0;

	/** Pane thickness. Real thickness and never a plane, or refraction and reflection read wrong. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge",
		meta = (EditCondition = "bGlassInsert", ClampMin = "0.0"))
	double GlassThickness = 0.5;

	/** How far the pane runs under the frame all round, so no daylight shows along the rebate. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge",
		meta = (EditCondition = "bGlassInsert", ClampMin = "0.0"))
	double GlassRebate = 0.6;

	/** Cut width of the leaf: the module less half a reveal at each edge. */
	double LeafWidth() const { return ModuleWidth - RevealGap; }

	/** Cut height of the leaf. */
	double LeafHeight() const { return ModuleHeight - RevealGap; }

	/**
	 * Sign of the hinge rotation that opens this leaf.
	 *
	 * Negative for a left-hung leaf and positive for a right-hung one, because the module frame's
	 * +Y runs back into the unit and opening always carries the leading edge forward out of it.
	 */
	double SwingSign() const { return Hinge == EHFShutterHinge::Left ? -1.0 : 1.0; }

	/** False when the reveal has eaten the module, or a dimension is non-positive. */
	bool IsValid() const
	{
		return LeafWidth() > 0.0 && LeafHeight() > 0.0 && Thickness > 0.0 && RevealGap >= 0.0
			&& OpenAngleDegrees > 0.0;
	}

	/** True when the frame and its rebate actually leave a pane worth glazing. */
	bool HasGlazableFrame() const
	{
		return bGlassInsert && StileWidth > 0.0 && GlassThickness > 0.0
			&& GlassThickness <= Thickness && GlassRebate >= 0.0 && GlassRebate < StileWidth
			&& LeafWidth() > 2.0 * StileWidth && LeafHeight() > 2.0 * StileWidth;
	}
};

/**
 * Cross-section of a cornice, seen looking along the run.
 *
 * All four occupy the same envelope, so restyling a moulding never moves the cabinet it caps nor
 * changes what the run measures off a drawing. Only the material cut away at the front underside -
 * the one arris anybody actually sees, standing under a wall unit - differs.
 */
UENUM(BlueprintType)
enum class EHFCorniceProfile : uint8
{
	/** Plain rectangular fascia. The flat modern cornice most laminate kitchens get. */
	Square,

	/** Front underside cut back at 45 degrees, so the run reads as a splay from below. */
	Splay,

	/** Front underside scooped out on a quarter circle - the traditional cove. */
	Cove,

	/** Front underside set back as a square step, giving a two-band build-up. */
	Stepped
};

/**
 * The moulding capping a run of wall cabinets, standing proud of the shutter face.
 *
 * Centimetres, in the cornice's own local space, on the same axes as FHFPlinthParams so the top and
 * bottom of a run are described the same way round: the origin lies on the shutter face plane at
 * the -X end of the run, level with the cornice underside, +X runs along the run, +Y runs back into
 * the unit, +Z is up.
 *
 * The section therefore straddles Y = 0. It stands proud of the shutter face as far as -Projection
 * and reaches back to Depth - Projection, which is where it lands on the carcass; at the defaults
 * that back face sits 20 mm into the unit, just clear of a 19 mm shutter.
 *
 * One set of parameters is one straight run. A cornice returning around an exposed end is two runs
 * at two anchors - mitreing is a placement decision, and a generator that knew about corners would
 * have to know about the cabinet, which is exactly what these functions must not.
 *
 * Defaults are the standard Indian modular figures: 60 mm high, 25 mm proud.
 */
USTRUCT(BlueprintType)
struct HOUSEFORGE_API FHFCorniceParams
{
	GENERATED_BODY()

	/** Length of the run, along +X. A run of no length is not an error, just nothing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ClampMin = "0.0"))
	double Width = 0.0;

	/** Front-to-back depth of the moulding, front face to back face. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ClampMin = "0.0"))
	double Depth = 4.5;

	/** Height of the moulding. 6 caps a kitchen wall unit; 7.5 tops a wardrobe. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ClampMin = "0.0"))
	double Height = 6.0;

	/**
	 * How far the front face stands proud of the shutter face. 2.0 - 4.0 in practice.
	 *
	 * The whole point of the part. A cornice flush with the shutters is a strip of board; the
	 * projection is what throws the shadow line that reads as a capped run.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ClampMin = "0.0"))
	double Projection = 2.5;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	EHFCorniceProfile Profile = EHFCorniceProfile::Square;

	/** Size of the front-underside feature: the splay leg, the cove radius, the step. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ClampMin = "0.0"))
	double ProfileSize = 2.0;

	/**
	 * Chamfer put on the exposed arrises of the section.
	 *
	 * Small, and not really optional: a mathematically sharp edge reads as CG under any lighting,
	 * and a cornice sits at eye level in every walkthrough. See .claude/rules/04-conventions.md.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ClampMin = "0.0"))
	double EdgeBevel = 0.2;

	/** Segments the cove arc is drawn with. Ignored by the other profiles. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge",
		meta = (EditCondition = "Profile == EHFCorniceProfile::Cove", ClampMin = "2", ClampMax = "32"))
	int32 CoveSegments = 8;

	/** Front face plane - negative, because the front stands in front of the shutter face. */
	double FrontY() const { return -Projection; }

	/** Back face plane, where the moulding lands on the carcass. */
	double BackY() const { return Depth - Projection; }

	/** False when the parameters describe no moulding at all. */
	bool IsValid() const { return Width > 0.0 && Depth > 0.0 && Height > 0.0; }
};

/**
 * Which way a panel's front face looks, along the panel's own local Y.
 *
 * Every panel this kit generates - a shutter leaf of either hand, a drawer front - carries its
 * board on +Y of its own origin and therefore looks out along NegativeY. That is the value a
 * handle on the outside of a cabinet always wants, and it is a constant rather than something to
 * derive from how the panel is hung.
 *
 * The enum exists for the panel fitted the other way round: a handle on the INSIDE face of a leaf,
 * or a panel a caller has generated itself on the opposite convention. It has to be said out loud
 * rather than inferred, because getting it wrong fits the handle inside the cupboard, which reads
 * as correct in plan and absurd in a walkthrough.
 */
UENUM(BlueprintType)
enum class EHFPanelFacing : uint8
{
	PositiveY,
	NegativeY
};

/**
 * Which edge of a panel a handle serves.
 *
 * Named for the panel's own local axes rather than for the room. A shutter's local space hangs off
 * its hinge, so "left" stops meaning anything the moment the leaf swings.
 */
UENUM(BlueprintType)
enum class EHFHandleEdge : uint8
{
	/** The +Z edge. */
	Top,
	/** The -Z edge. */
	Bottom,
	/** The -X edge - the leading edge of a RIGHT-hung leaf. */
	MinX,
	/** The +X edge - the leading edge of a LEFT-hung leaf, and of a drawer front. */
	MaxX
};

/**
 * A handle on one joinery panel.
 *
 * The panel is part of the parameters rather than something the caller reconciles afterwards,
 * because a handle only exists relative to one: a bar sits a fixed distance in from an edge and
 * runs parallel to it, and a routed profile is a hole in a panel and nothing whatsoever on its own.
 *
 * Everything is in the panel's own local space and in centimetres. Defaults are the standard Indian
 * cabinet fittings: a 128 mm bar in 12 mm round stock, a 30 mm knob, a 38 mm profile with a 12 mm
 * finger recess.
 */
USTRUCT(BlueprintType)
struct HOUSEFORGE_API FHFHandleParams
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	EHFHandleStyle Style = EHFHandleStyle::Bar;

	/**
	 * The panel this handle serves, as a box in the panel's own local space.
	 *
	 * A box rather than a size because a panel's origin is its pivot, and a pivot is neither corner
	 * nor centre: a shutter's is on its hinge axis, offset by half a reveal. Stating where the board
	 * actually sits is what lets a handle land on it without the kit knowing what kind of panel it
	 * is, or where in the house it ended up.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	FBox PanelBox = FBox(FVector(0.0, 0.0, 0.0), FVector(45.0, 1.9, 210.0));

	/**
	 * NegativeY, because that is the face on the outside of the cabinet.
	 *
	 * Every panel this kit generates carries its board on +Y of its own origin and therefore looks
	 * out along -Y, which the enum above says in as many words. The rest of these defaults describe
	 * a left-hung wardrobe leaf exactly - PanelBox is ShutterPanelBox for one and Edge is its
	 * leading edge - so defaulting the other way fitted the bar inside the wardrobe and routed the
	 * J-profile into the back of the leaf. Both call sites in the kit override this, so the exposure
	 * was a designer editing it in a details panel, or the next fixture generator that forgets the
	 * line; and both failures look right in every still and are only visible once the leaf is opened.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	EHFPanelFacing Facing = EHFPanelFacing::NegativeY;

	/** The edge served. A bar runs parallel to it; a routed profile runs along it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	EHFHandleEdge Edge = EHFHandleEdge::MaxX;

	/** From that edge to the handle's centre line. Ignored by the routed styles, which sit on it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ClampMin = "0.0"))
	double EdgeInset = 5.0;

	/** Overall bar length. 12.8 is the 128 mm pull, the commonest size on the ladder. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ClampMin = "0.0"))
	double BarLength = 12.8;

	/** Bar stock diameter. 12 mm round is the standard Indian cabinet pull. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ClampMin = "0.0"))
	double BarDiameter = 1.2;

	/** How far each standoff sits in from the end of the bar. 1.6 gives 96 mm fixing centres. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ClampMin = "0.0"))
	double BarEndInset = 1.6;

	/** Knob head diameter. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ClampMin = "0.0"))
	double KnobDiameter = 3.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ClampMin = "0.0"))
	double KnobStemDiameter = 1.0;

	/** How far the handle stands proud of the face. Its outermost point sits exactly here. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ClampMin = "0.0"))
	double Projection = 3.2;

	/**
	 * How far the fixing pads sink into the face.
	 *
	 * Not decoration. A pad landing exactly on the face shares a plane with it, and two coplanar
	 * coincident faces z-fight through every frame of a walkthrough - the one artefact a still
	 * screenshot will not show you.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ClampMin = "0.0"))
	double Embed = 0.2;

	/** Height of a routed profile up the panel face. 3.8 is the gola profile. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ClampMin = "0.0"))
	double ProfileHeight = 3.8;

	/** How deep a routed profile cuts in. Clamped to leave MinWeb of board behind it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ClampMin = "0.0"))
	double RecessDepth = 1.2;

	/**
	 * Chamfer on the lip of a routed profile.
	 *
	 * The lip of a finger recess is at eye level on a wall unit and at hand level everywhere else,
	 * and a mathematically sharp one reads as CG under any lighting. A real routed edge carries a
	 * small break that catches light. See .claude/rules/04-conventions.md.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ClampMin = "0.0"))
	double LipChamfer = 0.2;

	/** HandlelessGroove only: board left between the groove and the panel edge. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ClampMin = "0.0"))
	double GrooveEdgeMargin = 1.0;

	/** Board that must survive behind any routed profile. The clamp on RecessDepth. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ClampMin = "0.0"))
	double MinWeb = 0.5;

	/** Sides on the round sections. Rounded up to a multiple of four; see AppendRevolvedProfile. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ClampMin = "4", ClampMax = "64"))
	int32 SideCount = 16;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	EHFSurfaceRole HandleRole = EHFSurfaceRole::MetalHardware;

	/**
	 * Role given to the faces a routed profile exposes.
	 *
	 * Its own role rather than the panel's, so the material panel can line the inside of a gola
	 * channel in aluminium while the shutter around it stays laminate. Set it to the panel's own
	 * role for a profile simply routed out of the board and finished with it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	EHFSurfaceRole RecessRole = EHFSurfaceRole::ShutterLaminate;

	/** True for the styles routed into the panel rather than screwed onto it. */
	bool IsRecessed() const
	{
		return Style == EHFHandleStyle::JProfile || Style == EHFHandleStyle::HandlelessGroove;
	}

	/** True for the styles that add a solid to the panel. */
	bool IsApplied() const
	{
		return Style == EHFHandleStyle::Bar || Style == EHFHandleStyle::Knob;
	}

	/** False when the parameters describe no handle, or no panel to put one on. */
	bool IsValid() const
	{
		return Style != EHFHandleStyle::None && PanelBox.IsValid != 0
			&& PanelBox.GetSize().GetMin() > UE_KINDA_SMALL_NUMBER;
	}
};

/**
 * How far a drawer comes out of its cabinet.
 *
 * Both are hardware you can buy, and the difference is obvious the moment a drawer is opened in a
 * walkthrough rather than looked at in a still: a three-quarter runner leaves the back of the box
 * inside the cabinet, which is why the back of that drawer is always the awkward one to reach.
 */
UENUM(BlueprintType)
enum class EHFDrawerExtension : uint8
{
	/** Travels three quarters of the box depth. */
	ThreeQuarter,

	/** Travels the whole box depth, which is the runner's nominal length. */
	Full
};

/**
 * One drawer: an applied front, the box behind it, and the runners it rides on.
 *
 * Sized like the shutter it sits beside - by the MODULE the front closes rather than by the front
 * itself - so a bank of drawers and a run of shutters are set out from the same numbers and stay
 * aligned when one of them changes width. Driving it the other way round makes a run drift the
 * moment a bay is resized, and the drift only shows once everything is placed.
 *
 * Nothing here is the box's depth, and that is deliberate: a box is built to the runner it hangs
 * on, not the other way round. SelectRunnerLength makes that choice and SanitiseDrawer is where it
 * can be read back.
 *
 * Centimetres. Defaults are standard Indian modular construction: an 18 mm ply carcass, a 19 mm
 * front, a 12 mm box in a 580 deep base unit, and a 3 mm reveal.
 */
USTRUCT(BlueprintType)
struct HOUSEFORGE_API FHFDrawerParams
{
	GENERATED_BODY()

	/** Width of the bay this drawer front closes, between the carcass sides. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ClampMin = "0.0"))
	double ModuleWidth = 45.0;

	/** Height of the bay it closes. In a bank, graduation sets this per drawer. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ClampMin = "0.0"))
	double ModuleHeight = 25.0;

	/** Carcass depth, front edge to the inside face of the back panel. 58 in a kitchen base unit. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ClampMin = "0.0"))
	double CarcassDepth = 58.0;

	/** Carcass side board. What it leaves between the sides is what the box has to fit inside. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ClampMin = "0.0"))
	double CarcassSideThickness = 1.8;

	/** Finished front: 1.8 ply with 0.08 laminate on the face and a balancing 0.08 on the back. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ClampMin = "0.0"))
	double FrontThickness = 1.9;

	/** Drawer box side: 12 mm ply, or the 13 mm side of a metal box. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ClampMin = "0.0"))
	double BoxSideThickness = 1.2;

	/** Drawer box bottom: 6 mm, grooved in above the bottom of the sides. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ClampMin = "0.0"))
	double BoxBottomThickness = 0.6;

	/**
	 * Total gap around the front, taken half from each edge.
	 *
	 * Carries the same weight here as it does on a shutter, and one more job besides: in a bank it is
	 * the shadow line between one front and the next, and without it a bank renders as a single slab
	 * with no indication of how many drawers it is.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ClampMin = "0.0"))
	double RevealGap = 0.3;

	/** Gap the fixing leaves between the back of the front and the carcass front edges. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ClampMin = "0.0"))
	double BackClearance = 0.1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	EHFDrawerExtension Extension = EHFDrawerExtension::Full;

	/**
	 * Runner nominal length, or 0 for the longest that fits.
	 *
	 * An explicit length is honoured whenever the carcass can physically take it - a wardrobe drawer
	 * specified at 450 is a decision, not an accident. One that cannot fit is dropped to the longest
	 * that can, rather than driving a box out through the back panel. SanitiseDrawer reports which.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ClampMin = "0.0"))
	double RunnerLength = 0.0;

	/** Cut width of the front: the module less half a reveal at each edge. */
	double FrontWidth() const { return ModuleWidth - RevealGap; }

	/** Cut height of the front. */
	double FrontHeight() const { return ModuleHeight - RevealGap; }

	/** False when the reveal has eaten the module, or there is no carcass to put a drawer in. */
	bool IsValid() const
	{
		return FrontWidth() > 0.0 && FrontHeight() > 0.0 && CarcassDepth > 0.0
			&& FrontThickness > 0.0 && BoxSideThickness > 0.0 && RevealGap >= 0.0;
	}
};

/**
 * A bank of drawers filling one module.
 *
 * Real banks are graduated - a shallow cutlery drawer at the top, a deep pan drawer at the bottom -
 * and never divided evenly. An evenly divided bank is one of the clearest tells that a kitchen was
 * generated rather than made, and it costs nothing to get right.
 */
USTRUCT(BlueprintType)
struct HOUSEFORGE_API FHFDrawerBankParams
{
	GENERATED_BODY()

	/** Construction shared by every drawer in the bank. ModuleHeight is set per drawer, not here. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ShowOnlyInnerProperties))
	FHFDrawerParams Drawer;

	/** Height the bank fills. 72 is a kitchen base carcass over its 10 cm plinth. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ClampMin = "0.0"))
	double BankHeight = 72.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ClampMin = "0", ClampMax = "12"))
	int32 DrawerCount = 3;

	/**
	 * How much deeper the bottom front is than the top one, before the heights are snapped to the
	 * sizes fronts are actually cut to.
	 *
	 * 2.0 is what a three-drawer kitchen bank is: 150/250/300 in a 720 carcass, the bottom front
	 * about twice the top. 1.0 asks for fronts as near equal as the ladder allows.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ClampMin = "1.0"))
	double GradationRatio = 2.0;

	/** Part ids are this with the index appended, top to bottom: Drawer0, Drawer1, ... */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	FName PartIdPrefix = TEXT("Drawer");
};

/**
 * Shared sub-generators for joinery: the pieces every cabinet is assembled from.
 *
 * Pure like every other generator - parameters in, FDynamicMesh3 out, no world, no actor, no asset
 * loading (see .claude/rules/04-conventions.md). Each piece comes back in its own local space with
 * its origin on the pivot, so the caller places it and, where it moves, articulates it. A piece that
 * does not move on its own can simply be appended into the element's fixed mesh; a piece mounted on
 * something that does move is generated in that moving part's local space so it travels with it.
 */
class HOUSEFORGE_API FHFJoineryKit
{
public:
	/**
	 * The parameters actually used, clamped so the thing they describe can exist.
	 *
	 * Public because the clamping is a real answer rather than an implementation detail: a caller
	 * stacking a carcass on top of a plinth needs the height that was built, not the height that was
	 * asked for, and a test needs to be able to say what the generator should have produced.
	 */
	static FHFPlinthParams SanitisePlinth(const FHFPlinthParams& Params);

	/**
	 * A recessed toe-kick base, in the local space described by FHFPlinthParams.
	 *
	 * Built as a ladder frame - front rail, back rail, ends closing between them - rather than a
	 * solid block, because that is what it is on site and because a solid block would put a
	 * misleading volume on every bill of quantities taken off the model. Returns an empty mesh when
	 * the parameters ask for no plinth at all, which a wall-hung unit legitimately does.
	 */
	static UE::Geometry::FDynamicMesh3 GeneratePlinth(const FHFPlinthParams& Params);

	// ----------------------------------------------------------------------------- shelf stacks
	//
	// Shelves and a hanging rail: what is on show the moment a shutter is opened or seen through a
	// glass insert, and therefore not optional detail. Nothing in a stack moves on its own, so it has
	// no parts - the caller appends it into the fixed mesh of the unit it belongs to. When the stack
	// rides on something that DOES move - shelves in a pull-out pantry, a rail in a sliding bay - the
	// caller appends it into that part's mesh instead, in the part's own local space, and it travels
	// with the part for free. That is the whole reason this returns a mesh in a local frame rather
	// than placing anything itself.

	/** The parameters actually used, with material defaults resolved and everything clamped to fit. */
	static FHFShelfStackParams SanitiseShelfStack(const FHFShelfStackParams& Params);

	/**
	 * Internal shelves, the mid partitions they need to not sag, and a hanging rail if asked for.
	 *
	 * Stack-local space, matching FHFPlinthParams: the origin is the bottom-left corner of the clear
	 * volume on the carcass front plane, +X across the bay, +Y back into the unit, +Z up. The result
	 * occupies exactly [0, Width] x [FrontSetback, Depth - BackClearance] x [0, Height] and never
	 * leaves it, so a caller can place a stack by translation alone and know it will not foul the
	 * shutter closing over it.
	 *
	 * Returns an empty mesh when the parameters describe no shelves and no rail, which an open bay
	 * legitimately is.
	 */
	static UE::Geometry::FDynamicMesh3 GenerateShelfStack(const FHFShelfStackParams& Params);

	/**
	 * How many shelves a clear height wants at a target compartment spacing.
	 *
	 * Where the domain ladder lives, kept out of the generator so the generator does exactly what it
	 * is told and a shoe rack can still have 180 mm compartments. Rounds to whole compartments and
	 * never returns a count that would leave one under MinUsefulCompartment.
	 *
	 * 200 cm at the wardrobe's 37.5 gives 4 shelves and 38.6 clear; 66.4 cm of wall unit at 32 gives
	 * 1 shelf and 32.3 clear, which is the 310-330 the trade actually builds.
	 *
	 * @param ShelfThickness Zero to assume 18 ply.
	 */
	static int32 ShelfCountForClearHeight(double ClearHeight, double TargetSpacing = 37.5,
		double ShelfThickness = 0.0);

	/** 1.8 for ply, 0.8 for toughened glass. */
	static double DefaultShelfThickness(EHFShelfMaterial Material);

	/** How far a shelf spans before it sags on camera: 90 for 18 ply, 60 for 8 mm toughened glass. */
	static double DefaultMaxSpan(EHFShelfMaterial Material);

	/** Below this a compartment holds nothing a wardrobe is for. Folded clothes need 300 mm. */
	static constexpr double MinUsefulCompartment = 30.0;

	// --------------------------------------------------------------------------------- shutters
	//
	// A shutter moves, so unlike the plinth it is never appended into a carcass mesh. It comes back
	// as an FHFMeshPart - mesh, pivot and motion - and the actor hangs it on its own component.
	//
	// Two frames are in play, and confusing them is the easy mistake:
	//
	//   Part-local  The space GenerateShutter returns the leaf in. Origin ON the hinge axis.
	//   Module      The bay the leaf closes. Origin at the bay's bottom-left corner on the carcass
	//               front plane; +X right across the bay, +Y back into the unit and +Z up - the
	//               same axes FHFPlinthParams uses, so a carcass can mix the two without
	//               re-basing anything. The leaf therefore hangs at negative Y, in front of the
	//               carcass, and ShutterPivotTransform is expressed in this frame.

	/**
	 * A shutter leaf in its own local space, hinge axis on the origin.
	 *
	 * Local frame: +Z up from the leaf's bottom edge, the board on +Y of the hinge axis, and the
	 * leaf cut out on the side of that axis its module lies on - +X for a left-hung leaf, -X for a
	 * right-hung one. The pivot is a pure translation for both hands, so those axes mean the same
	 * thing in the leaf's space as they do in the module's.
	 *
	 * TWO CONSEQUENCES, and both are load-bearing.
	 *
	 * The thickness always lies on Y *opposite the way the leaf swings*, because the leaf swings
	 * out of the unit - towards -Y - whichever way it is hung. That is not arbitrary: it puts the
	 * hinge axis on the face the leaf turns towards, which is where a butt hinge's knuckle sits and
	 * what makes the swing provably clean rather than merely clean-looking:
	 *
	 *   - the leaf never crosses behind its own closed back face, at any angle up to 180 degrees,
	 *     so it cannot reach the carcass however deep that carcass is or however thick its sides;
	 *   - below 90 degrees it never crosses its own hinge plane either, so it cannot reach the
	 *     neighbour it shares a reveal with, and beyond 90 it is already out in front of that
	 *     neighbour's face.
	 *
	 * Put the axis at the leaf's mid-thickness instead - the obvious choice, and what the door leaf
	 * does because a door has a frame around it rather than a carcass behind it - and the back
	 * corner scythes through the carcass side by Thickness*|cos(angle)| once past 90. That is about
	 * 3 mm on a 19 mm leaf at 100 degrees: too little to catch in a wireframe, and obvious the
	 * moment the thing is lit and opened.
	 *
	 * And because of that, BOTH HANDS PRESENT THEIR OUTWARD FACE AT LOCAL Y = 0, LOOKING DOWN -Y -
	 * the same convention as a drawer front. Anything mounted on a leaf, or routed into one, is
	 * therefore described identically for the two hands, and the only thing that changes hand to
	 * hand is which edge is the leading one. ShutterPanelBox and ShutterLeadingEdge answer both
	 * questions so a caller never derives them from Hinge itself.
	 *
	 * The mirror is deliberate, and it is the cheaper of the two prices available. Keeping +X on
	 * the leading edge for both hands would need a half-turn in the pivot, and a half-turn about Z
	 * necessarily takes local -Y to module +Y: the outward direction would then be -Y for one hand
	 * and +Y for the other. Every mounted part would need that flip applied by hand, and getting it
	 * wrong fits the handle inside the cupboard or routs the groove into the back of the leaf -
	 * failures that look right in every still and are only visible once the leaf is opened.
	 *
	 * @return An empty mesh when the parameters do not describe a leaf, never a degenerate one.
	 */
	static UE::Geometry::FDynamicMesh3 GenerateShutter(const FHFShutterParams& Params);

	/**
	 * The leaf's own box, in part-local space: what a handle or a mounted part is fitted to.
	 *
	 * Public because it is the answer to the one question handedness genuinely changes. A caller
	 * reading it off Hinge itself writes the flip out by hand at every call site, which is how a
	 * handle ends up on the hinge edge of the one right-hung leaf in a run.
	 *
	 * @return An empty box when the parameters describe no leaf.
	 */
	static FBox ShutterPanelBox(const FHFShutterParams& Params);

	/** The edge of that box the leaf opens from: MaxX left-hung, MinX right-hung. */
	static EHFHandleEdge ShutterLeadingEdge(const FHFShutterParams& Params);

	/** Where the leaf's hinge axis sits, in the module frame. A pure translation, both hands. */
	static FTransform ShutterPivotTransform(const FHFShutterParams& Params);

	/** The hinge the leaf turns on, expressed in the leaf's own local space. */
	static FHFPartMotion ShutterMotion(const FHFShutterParams& Params);

	/**
	 * The shutter as a moving part: mesh, module-relative pivot and motion.
	 *
	 * This is what a carcass generator hands back and an articulated actor hangs on a component of
	 * its own. A shutter is never merged into the carcass mesh - it moves, so it is its own part
	 * with its own pivot, per .claude/rules/04-conventions.md.
	 */
	static FHFMeshPart BuildShutterPart(const FHFShutterParams& Params, FName PartId);

	// --------------------------------------------------------------------------------- cornices
	//
	// A cornice does not move, so it is not a part: it belongs in the mesh of whatever it is fixed
	// to. On a fixed carcass that is the actor's shell mesh, which is what GenerateCornice plus a
	// caller-side placement gives. Fixed to something that DOES move - a moulding carried on a
	// pull-out unit, say - it must go into that part's mesh instead, in the part's own local space,
	// and then it travels with the part for free. AppendCornice is that case: it is the same
	// geometry, placed by an anchor expressed in whichever space the target mesh is in.

	/** The parameters actually used, clamped so the section they describe is a simple polygon. */
	static FHFCorniceParams SanitiseCornice(const FHFCorniceParams& Params);

	/**
	 * A cornice run, in the local space described by FHFCorniceParams, with roles and UVs applied.
	 *
	 * Swept from a cross-section rather than assembled from boxes, because the profile is the part:
	 * a moulding is what its section is, and the splay or cove has to survive being read at eye
	 * level under a kitchen light. The exposed arrises carry a chamfer for the same reason.
	 *
	 * Whatever the profile, the run occupies exactly [0, Width] x [-Projection, Depth - Projection]
	 * x [0, Height] - so it can be placed by its declared size and restyled afterwards without
	 * anything it touches having to move.
	 *
	 * @return An empty mesh when the parameters describe no moulding, never a degenerate one.
	 */
	static UE::Geometry::FDynamicMesh3 GenerateCornice(const FHFCorniceParams& Params);

	/**
	 * Appends a cornice into an existing mesh, placed by Anchor.
	 *
	 * Anchor is read in whatever space the target mesh is in: actor space to cap a fixed carcass, a
	 * moving part's local space to make the moulding travel with that part. It is also how a run
	 * turns a corner - a second call, rotated - since one call is one straight run.
	 *
	 * UVs are deliberately not applied here. ApplyWorldScaleUVs reprojects a whole mesh, so the
	 * caller applies it once, after the assembly is complete.
	 *
	 * @param Anchor Rotation and translation only. A mirrored anchor would invert the winding and
	 *        is not supported; the far side of a kitchen is a rotation, not a reflection.
	 * @return false if the parameters describe no moulding, in which case Mesh is left untouched.
	 */
	static bool AppendCornice(UE::Geometry::FDynamicMesh3& Mesh, const FHFCorniceParams& Params,
		const FTransform& Anchor);

	// ---------------------------------------------------------------------------------- handles
	//
	// A handle does not move on its own; it is screwed to something, and it goes wherever that
	// something goes. So the only question worth asking is which mesh it ends up in. On a fixed
	// panel it can share the element's shell mesh. On a shutter or a drawer front it must be built
	// in THAT PART's local space, or it stays behind on the carcass while the front it belongs to
	// swings away - the failure looks, from the front and closed, exactly like success.
	//
	// ApplyHandle answers that question by construction: it works in the local space of whatever
	// mesh it is handed, so passing it a part's mesh puts the handle on the part and passing it the
	// shell mesh puts it on the shell. There is no third option to get wrong.

	/** True for the styles routed into the panel rather than screwed onto it. */
	static bool IsRecessedHandle(EHFHandleStyle Style);

	/**
	 * The parameters actually used, clamped so the handle they describe can exist on that panel.
	 *
	 * Public for the same reason the other Sanitise functions are: the clamp is a real answer. A
	 * recess asked to go deeper than the board is thick has to leave a web behind it, and a caller
	 * setting out a run - or a test asserting what was built - needs the depth that was cut, not
	 * the depth that was requested.
	 */
	static FHFHandleParams SanitiseHandle(const FHFHandleParams& Params);

	/**
	 * An applied handle - a bar or a knob - as a closed solid in its own local space.
	 *
	 * Local frame: +X along the handle's run, +Y out of the panel face, +Z towards the edge it
	 * serves. The origin sits on the face at the handle's centre, so the solid spans exactly -Embed
	 * to Projection on Y, its declared length on X and its declared stock size on Z. Those bounds
	 * are the contract - a handle is the one thing on a cabinet the eye can measure against its own
	 * hand.
	 *
	 * Empty for None, and empty for the routed styles. A recess is a hole in a panel and not a
	 * part; handing back a plausible-looking solid for one would be a lie the caller cannot detect.
	 */
	static UE::Geometry::FDynamicMesh3 GenerateHandle(const FHFHandleParams& Params);

	/**
	 * Where GenerateHandle's local space sits, in the panel's own local space.
	 *
	 * For callers that want the handle on a component of its own - a separate material, or its own
	 * entry in a bake. ApplyHandle is the simpler path when it can just join the panel's mesh, and
	 * the two agree: applying a handle puts it exactly where this says it goes.
	 */
	static FTransform HandlePlacement(const FHFHandleParams& Params);

	/**
	 * The solid to rout out of the panel for a recessed style, in the panel's own local space.
	 *
	 * The two routed styles differ in one thing, and it is the thing you see. A J-profile takes the
	 * corner off the panel's edge and breaks out through it, so a run of fronts reads as one
	 * continuous shadow gap. A handleless groove is a channel in the face set back from the edge,
	 * so a rail of board survives between the two and each front keeps its own outline. That
	 * surviving rail is the whole difference; everything else about them is the same cut.
	 *
	 * The cutter overshoots every face it is meant to break out of, because a cutter flush with a
	 * surface leaves coplanar faces for the boolean to resolve, and it resolves them badly.
	 *
	 * Empty for the applied styles and for None.
	 */
	static UE::Geometry::FDynamicMesh3 GenerateHandleRecessCutter(const FHFHandleParams& Params);

	/**
	 * Puts the handle on the panel, in the panel's own local space.
	 *
	 * An applied handle is built straight into PanelMesh in its final position rather than
	 * generated elsewhere and appended, which is what keeps its surface-role polygroups intact: a
	 * mesh append that renumbered groups would leave the handle unreachable from the material
	 * panel, and untagged geometry cannot be re-materialled at all. A routed handle subtracts its
	 * cutter instead.
	 *
	 * World-scale UVs are reapplied to the whole panel afterwards, because a boolean leaves its new
	 * faces with none and an unwrapped recess takes whatever the material's first tile happens to
	 * be.
	 *
	 * @return false only when the handle could not be produced. A style of None is a success that
	 *         does nothing, not a failure.
	 */
	static bool ApplyHandle(UE::Geometry::FDynamicMesh3& PanelMesh, const FHFHandleParams& Params);

	// ---------------------------------------------------------------------------------- drawers
	//
	// A drawer moves, so like a shutter it is never merged into the carcass: it comes back as an
	// FHFMeshPart and the actor hangs it on a component of its own. What it is NOT is one solid that
	// slides - a drawer is a front, a box and the drawer half of its runners, and all three travel
	// together, while the cabinet half of the runners stays behind. GenerateDrawerRunnerMounts is
	// that half, and it is the only piece of a drawer that belongs in the carcass mesh.
	//
	// Drawer-local space IS the module frame the shutter's pivot is expressed in - origin at the
	// bay's bottom-left corner on the carcass front plane, +X right across the bay, +Y back into the
	// unit, +Z up - so a carcass places a drawer and a shutter the same way and a bank stacks by
	// translation alone. The drawer therefore pulls along -Y, its front stands proud of the carcass
	// at negative Y exactly as a shutter does, and everything the carcass owns stays at Y >= 0.
	//
	// A slide may pivot anywhere on its line of travel, so the module corner is a free choice, and
	// it is the useful one: it is the only point a carcass already knows without being told a box
	// depth or a runner length.
	//
	// TWO THINGS A COMPOSER HAS TO SUPPLY, because nothing here can:
	//
	// The front is always a full-overlay front the width of its module, which is right for a kitchen
	// base unit and wrong for a drawer INSIDE a wardrobe. An internal drawer's module must be inset
	// far enough that its front clears the arc its shutter's thickness sweeps - about half a reveal
	// plus the leaf thickness in from the hinge edge, not merely inside the carcass - and set back in
	// Y behind the closed leaf. Inset like that, GenerateDrawerRunnerMounts lands short of the
	// carcass sides, so the composer owes it packers to be screwed to.
	//
	// And the drawer still cannot come out until the shutter is open, which no open amount here
	// expresses: see AHFArticulatedActor for why that is an ordering rather than a parenting problem.
	// HouseForge.Joinery.InternalDrawerInterlock builds the composition and measures both.

	/**
	 * The parameters actually used, with the runner resolved and everything clamped to fit.
	 *
	 * The runner is the interesting field. It is what sets the box depth and therefore the travel,
	 * and a caller laying out a bank - or a test asserting how far a drawer comes out - needs the
	 * length that was fitted rather than the one that was asked for.
	 */
	static FHFDrawerParams SanitiseDrawer(const FHFDrawerParams& Params);

	/**
	 * Longest runner that fits a carcass of this internal depth, or 0 if none does.
	 *
	 * Runners come in 50 mm steps from 250 to 550 and are specified against carcass depth with an
	 * allowance for the front assembly in front of the box and the service gap behind it: a 580
	 * carcass takes a 500 runner, which is what a kitchen base unit is actually built with, and not
	 * the 550 that bare clearances alone would allow.
	 */
	static double SelectRunnerLength(double CarcassDepth);

	/** Depth of the box, which is the runner's nominal length. 0 when no drawer can be built. */
	static double DrawerBoxDepth(const FHFDrawerParams& Params);

	/**
	 * How far the drawer travels at open amount 1, in centimetres.
	 *
	 * Always less than the carcass is deep, whichever extension is fitted: the box is shorter than
	 * the cabinet and starts behind the front, so a drawer at its stop is clear of the carcass
	 * without ever having been inside the back panel.
	 */
	static double DrawerTravel(const FHFDrawerParams& Params);

	/** The applied front alone, in drawer-local space. */
	static UE::Geometry::FDynamicMesh3 GenerateDrawerFront(const FHFDrawerParams& Params);

	/**
	 * The box alone - sides, front, back, grooved bottom, and the runner members riding on it.
	 *
	 * Members are butted rather than lapped, as they are on the bench, so the mesh contains the
	 * board the box is really made of instead of counting every joint twice.
	 */
	static UE::Geometry::FDynamicMesh3 GenerateDrawerBox(const FHFDrawerParams& Params);

	/**
	 * Everything that slides: the front, the box and the drawer half of the runners.
	 *
	 * @return An empty mesh when the parameters describe no drawer - a module too narrow to take a
	 *         box, or a carcass too shallow for the shortest runner made - never a degenerate one.
	 */
	static UE::Geometry::FDynamicMesh3 GenerateDrawer(const FHFDrawerParams& Params);

	/**
	 * The cabinet half of the runners: fixed geometry, in the drawer's own local space.
	 *
	 * Separate because it does not move. A carcass appends this at the same transform it gives the
	 * drawer part, and it stays where it is while the drawer runs out of it. It is set 0.5 mm clear
	 * of the drawer's own runner member across the whole stroke, so the two never share a face.
	 */
	static UE::Geometry::FDynamicMesh3 GenerateDrawerRunnerMounts(const FHFDrawerParams& Params);

	/** The slide the drawer runs on, expressed in the drawer's own local space. */
	static FHFPartMotion DrawerMotion(const FHFDrawerParams& Params);

	/**
	 * One drawer as a moving part: mesh, pivot and slide.
	 *
	 * The pivot is the identity, because drawer-local space is already the module frame. A bank sets
	 * it to the module's own place in the bank; a single drawer in a carcass is placed by the caller
	 * the same way it would place a shutter.
	 */
	static FHFMeshPart BuildDrawerPart(const FHFDrawerParams& Params, FName PartId);

	/**
	 * Front heights for a bank, top to bottom, in centimetres.
	 *
	 * Never decreasing downward, snapped to the heights fronts are actually cut to, and summing with
	 * one reveal each to exactly the bank height. A 72 cm bank of three comes out 15/25/30 and one of
	 * four comes out 15/15/20/20, which is what those banks are in a real kitchen.
	 *
	 * What the ladder leaves over goes on the bottom front, where a few millimetres are least visible
	 * and where a deep pan drawer swallows them - rather than into the reveals, which have to stay
	 * the 3 mm shadow line they are on site whatever the carcass measures. Never more than the step
	 * between two rungs: past that, the front is simply one size up.
	 *
	 * @return false if the bank cannot hold that many fronts, and false the other way too - if every
	 *         front is already the deepest one made and the bank is still not full. A 2 m bank of
	 *         three is not a bank of three enormous drawers; it is a bank that needs more of them.
	 */
	static bool GraduateDrawerFronts(const FHFDrawerBankParams& Bank, TArray<double>& OutFrontHeights);

	/**
	 * A whole bank, appended to OutParts top to bottom.
	 *
	 * Each part's pivot places its module within the bank, whose origin is the bank's bottom-left
	 * corner on the carcass front plane - the same frame one drawer uses, so a bank drops into a
	 * carcass exactly where a single drawer would.
	 *
	 * @param OutFixedMounts Optional: the cabinet halves of every runner, already placed in bank
	 *        space, ready for the carcass to append into its fixed mesh. Appended to, not replaced.
	 * @return false if the bank cannot be graduated or its drawers cannot be built.
	 */
	static bool BuildDrawerBank(const FHFDrawerBankParams& Bank, TArray<FHFMeshPart>& OutParts,
		UE::Geometry::FDynamicMesh3* OutFixedMounts = nullptr);
};
