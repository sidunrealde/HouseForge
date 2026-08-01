// Copyright Siddartha G. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Model/HFTypes.h"
#include "HFCeilingTemplates.generated.h"

/**
 * The figures behind the named ceiling designs.
 *
 * ALL IN CENTIMETRES, always, whatever units the spec that uses them is in. A project's ceiling
 * design is a statement about the building - a 150 band with a 100 cove channel - and it does not
 * change because a drawing happened to be dimensioned in millimetres. FHFCeilingTemplates scales
 * them into the spec's units at the one point they are stamped, exactly as the validator scales its
 * limits before comparing them to anything.
 *
 * Every number here is defended in the research behind the templates, and the two that decide
 * whether a cove reads at all are worth repeating:
 *
 *   - The trough must not be deeper than about one and a half times its width. The flat's committed
 *     coves were 480 deep by 80 wide - six to one - which absorbs most of what the strip emits and
 *     lands the near edge of the wash the better part of a metre inboard. A 150 drop with a 100
 *     channel is 1.3 to 1 and the glow starts at the cove.
 *   - The strip's top must stay below the lip's top, or the fitting is in plain sight. That is the
 *     whole sight line, and it has no distance term in it.
 */
USTRUCT(BlueprintType)
struct HOUSEFORGE_API FHFCeilingDefaults
{
	GENERATED_BODY()

	// ------------------------------------------------------------------------------ plain band

	/** Width of a plain perimeter band, in centimetres. 1.5 ft is the common figure on site. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	double BandWidth = 45.0;

	/**
	 * Drop of a plain band below the slab, in centimetres.
	 *
	 * Not a free choice: a 20 board plus a 60 downlight can plus its wiring is what has to fit in
	 * the plenum, which is the real reason 150 is the usual minimum rather than any rule of taste.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	double BandDrop = 15.0;

	// ------------------------------------------------------------------------------------ cove

	/** Width of a band carrying a cove, in centimetres. Wider than a plain one: the solid zone still has to take a fitting. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	double CoveBandWidth = 60.0;

	/** Drop of a band carrying a cove, in centimetres. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	double CoveDrop = 15.0;

	/**
	 * The trough, its lip and the strip lying in it.
	 *
	 * Left to FHFCoveProfile's own defaults, which are the researched figures - 100 channel, 75 lip,
	 * 30 upstand - and are asserted against the sizing rules by
	 * HouseForge.Model.CoveProfileObeysItsOwnSizingRules rather than left as prose.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	FHFCoveProfile Cove;

	// ---------------------------------------------------------------------------- stepped tray

	/** Outer band of a two-level tray, in centimetres. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	double TrayBandWidth = 45.0;

	/** Drop of the outer band, in centimetres. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	double TrayDrop = 20.0;

	/** Drop of the step inside it, in centimetres. Two levels plus the slab; three reads fussy. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	double TrayInnerDrop = 10.0;

	// ---------------------------------------------------------------------------- framed panel

	/** The frame band around a centre panel, in centimetres. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	double PanelFrameWidth = 45.0;

	/** Drop of the frame band, in centimetres. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	double PanelFrameDrop = 15.0;

	/**
	 * How far the centre panel hangs below the slab, in centimetres.
	 *
	 * SHALLOWER THAN THE FRAME, which is the way round the reference photographs almost always show
	 * it: the panel is recessed ABOVE the frame, catching the cove's light, not hung below it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	double PanelDrop = 4.0;

	// ------------------------------------------------------------------------------ downlights

	/** The fitting itself: cut-out, trim and how far up the can the aperture sits. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	FHFDownlightProfile Downlight;

	/** Centre to centre along the band, in centimetres. Under 60 the run reads as a strip; over 90 as dots. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	double DownlightSpacing = 75.0;

	/**
	 * Setback of the run from the wall, in centimetres.
	 *
	 * Under 200 the trim fouls the wall and the scallop it throws stops reading as one; the point of
	 * a perimeter run is the scalloped wash down the wall, not the dots.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	double DownlightSetback = 25.0;

	// ------------------------------------------------------------------- perimeter beam bulkhead

	/**
	 * Shoulder each side of a beam the ring has to box in, in centimetres: frame, board and tolerance.
	 *
	 * The ring is normally wider than this makes it - MinBeamBulkheadWidth wins for the beams in a
	 * flat of this class - but a deeper frame with a 300 beam in it would need the shoulder to be
	 * what sizes the ring, and a figure that only ever loses is a figure nobody can trust.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	double BeamBulkheadShoulder = 8.5;

	/** Board plus service gap below the beam soffit, in centimetres. Never zero: a face on the soffit flashes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	double BeamBulkheadClearance = 3.0;

	/** Narrowest ring worth building, in centimetres. Narrower reads as a fault line, not a level change. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	double MinBeamBulkheadWidth = 30.0;

	// ------------------------------------------------------- making the band answer to the room

	/**
	 * Narrowest a band may be squeezed to before it is dropped altogether, in centimetres.
	 *
	 * THE FIGURES ABOVE ARE FOR A ROOM THAT CAN TAKE THEM. Stamped without asking, a 450 band and a
	 * 300 ring consumed 750 off every side of the 1800 square foyer and left 300 - so the foyer got
	 * a 310 mm square hole punched through the middle of its plaster, open to the slab, which is the
	 * single most obviously wrong thing a render of the flat has shown. A band is a band because
	 * there is a middle for it to be around.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	double MinBandWidth = 30.0;

	/**
	 * How many band widths across the open centre has to be before the band still reads as one.
	 *
	 * A ratio, and the ratio is two: a frame whose opening is narrower than twice its own width has
	 * stopped being a frame and become a shaft. Below it the band is narrowed, and if even
	 * MinBandWidth will not fit, the treatment is abandoned and the room gets an honest flat soffit -
	 * which is what a 1.8 m foyer gets on site.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	double MinOpenCentreFactor = 2.0;

	// -------------------------------------------------------------------------- the flat soffit

	/**
	 * Drop of a plain flat ceiling, in centimetres - a kitchen, a bathroom, a corridor.
	 *
	 * NOT EVERY ROOM WANTS A DESIGN, and saying so is different from not having got to it. A wet
	 * area is ceiled flat across the whole room because that is what hides its plumbing and its
	 * extract, and a corridor is where the services run between them - but flat is not the same as
	 * DEEP. The four rooms left on Custom in the reference flat sat at 480 across their whole area
	 * for one reason only: the beams at their edges are 450, and before the ring existed the only
	 * way to bury one was to drop everything. With the ring taking the edges, the flat part answers
	 * to what actually runs above it, which is 250 of plenum and not 480.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	double FlatSoffitDrop = 25.0;

	// ------------------------------------------------------ what else in the room has to clear it

	/**
	 * Gap left between the head of a fitting and the finished soffit above it, in centimetres.
	 *
	 * THE FIGURE THAT MAKES A CEILING SOMETHING THE REST OF THE ROOM ANSWERS TO. Everything in this
	 * flat used to be set out against the structural slab, so a false ceiling and the fittings under
	 * it only agreed while the drop stayed small - and when the depth model changed, seven fittings
	 * ended up 30 mm inside their own ceilings: both bathroom geysers, both bathroom extracts, and
	 * all three curtain pelmets. Every one of them validated.
	 *
	 * FHFCeilingFit resolves each fixture against the soffit that actually ends up over it, and this
	 * is the clearance it leaves. Never zero: nothing on site is fixed dead tight to a plastered
	 * soffit, and a fitting whose top face lands exactly in the soffit plane is two faces in one
	 * plane facing the same way - which is the flashing FHFStructuralCut exists to prevent.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	double FixtureSoffitClearance = 1.0;
};

/**
 * Turning a named ceiling design into the figures a generator and a validator both understand.
 *
 * THE STAMP HAPPENS ONCE, HERE, AND BEFORE ANYTHING ELSE SEES THE SPEC. That is the whole point of
 * resolving rather than deferring: the validator asks how deep a ceiling is, the serializer writes
 * it out, the generator builds it, and the reference JSON is read by a human - and any of those
 * looking at an unresolved template would be looking at a ceiling with a drop of zero.
 *
 * Idempotent. Applying twice with the same defaults produces the same spec, which is what lets the
 * editor re-apply on a settings change without accumulating anything.
 *
 * Nothing here reads a settings object. The defaults arrive as a value, exactly as they do for
 * every generator - see .claude/rules/04-conventions.md.
 */
class HOUSEFORGE_API FHFCeilingTemplates
{
public:
	/**
	 * Resolve every templated ceiling in a spec, in the spec's own units.
	 *
	 * Also derives each ceiling's perimeter beam bulkhead, which needs the spec rather than the
	 * ceiling: which beams show in a room is a fact about the frame and the partitions under it.
	 */
	static void Apply(FHFHouseSpec& Spec, const FHFCeilingDefaults& Defaults);

	/**
	 * One ceiling, given the room it covers and what shows along each of its edges.
	 *
	 * @param ShowingBeamPerEdge One entry per boundary edge, in order: the deepest beam standing
	 *        proud along that edge, or null where nothing does. From
	 *        FHFHouseSpec::DeepestBeamOnRoomEdge. A shorter array is read as "nothing on the rest".
	 * @param UnitScale Spec units per centimetre: 10 for a millimetre spec, 1 for a centimetre one.
	 */
	static void Apply(FHFFalseCeiling& Ceiling, const FHFRoom& Room,
		const TArray<const FHFBeam*>& ShowingBeamPerEdge,
		const FHFCeilingDefaults& Defaults, double UnitScale);

	/**
	 * A run of downlights round a loop, evenly spaced.
	 *
	 * EVENLY, not "every Spacing until the wall runs out". A run laid out by repeated addition
	 * leaves whatever is left over as a short gap at one corner, and that gap is the first thing the
	 * eye finds in a photograph of a ceiling. The count is chosen from the perimeter and the
	 * spacing then divides it exactly, so the last fitting is the same distance from the first as
	 * from its neighbour.
	 *
	 * @param Loop    The band's outline - already inside any perimeter ring.
	 * @param Setback How far in from that outline the run sits.
	 * @param Spacing Target centre to centre; the achieved spacing is the nearest that divides.
	 */
	static TArray<FVector2D> PlaceDownlights(const TArray<FVector2D>& Loop, double Setback, double Spacing);
};
