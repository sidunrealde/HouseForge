// Copyright Siddartha G. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Model/HFTypes.h"

/**
 * The reference 2BHK.
 *
 * Built in code rather than hand-authored as JSON so it stays maintainable and cannot drift: the
 * automation tests, the drawing generator and the editor's "build sample" action all consume this
 * one definition. The committed Reference/Specs/Sample2BHK.json is a generated artifact of it.
 *
 * Layout is a realistic Indian 2BHK of roughly 91 sq m carpet area, in millimetres, with the
 * origin at the south-west internal corner:
 *
 *     Y=8400  +----------------+---------------------------+
 *             |    Kitchen     |     Master Bedroom        |
 *     Y=5400  +----+-----------+--------+---------+--------+
 *             |Foy.| Corridor  | C.Bath | M.Bath  |Utility |
 *     Y=3600  +----+-----------+--------+---------+--------+
 *             |     Living / Dining     |     Bedroom 2    |
 *     Y=0     +-------------------------+------------------+
 *            X=0                     X=6600             X=10800
 *
 * plus a balcony south of the living room, from Y=-1500 to Y=0.
 */
class HOUSEFORGE_API FHFSampleHouse
{
public:
	/** The reference 2BHK, in millimetres, complete with joinery, false ceilings and fittings. */
	static FHFHouseSpec Make2BHK();

	/**
	 * The same flat with its living room's sofa built to a named design, AND LAID OUT FOR IT.
	 *
	 * The second half of that sentence is the reason this exists rather than a caller just setting
	 * FHFFixtureParams::SofaDesign on the fixture. Three of the four designs are a swap: a
	 * low-profile sofa is 50 mm deeper and 100 mm lower than a square-arm one and everything else in
	 * the room stays exactly where it is. THE SECTIONAL IS NOT. Its drawn box is 1400 deep instead of
	 * 900, and dropping that into the existing layout puts the chaise's front 7 mm off the back of
	 * the coffee table - so the seating group moves, and where it moves to is a fact about this room
	 * that has to live somewhere a test and a render can both read.
	 *
	 * Where it moves to, and what each move is answering to, is written out at the call site. The
	 * short version: the sofa goes 250 west so the return clears the pulled-out dining chair, the
	 * chaise takes the east end so it does not stand in the balcony door's approach, and the coffee
	 * table goes into the crook of the L rather than in front of it.
	 *
	 * SquareArm - and Default - come back identical to Make2BHK() but for the named design itself,
	 * which is what makes the reference flat's own spec the SquareArm case rather than a fifth thing.
	 */
	static FHFHouseSpec Make2BHK(EHFSofaDesign SofaDesign);

	/**
	 * Writes one spec per named sofa design into <Plugin>/Saved/Review/sofa-designs/.
	 *
	 * Exposed as `HouseForge.ExportSofaDesignSpecs`, and kept out of Reference/Specs for the reason
	 * generated levels are kept out of the plugin repo: these are something to look at, not reference
	 * data. Saved/ is untracked, so a gate that runs this leaves the tree clean.
	 */
	static bool ExportSofaDesignSpecs(TArray<FString>& OutPaths, FString& OutError);

	/** Absolute path of the committed spec: <Plugin>/Reference/Specs/Sample2BHK.json. */
	static FString GetCommittedSpecPath();

	/**
	 * Regenerates the committed spec JSON from Make2BHK().
	 *
	 * Exposed as the `HouseForge.ExportSampleSpec` console command. Kept out of the test suite on
	 * purpose: a gate that rewrote tracked files would leave the tree dirty behind every merge.
	 * Instead the tests compare against the committed file, so drift fails the gate rather than
	 * being silently papered over.
	 */
	static bool ExportCommittedSpec(FString& OutError);
};
