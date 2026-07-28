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
