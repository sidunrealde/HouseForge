// Copyright Siddartha G. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Where one leaf of a two-track sliding pair sits, and how far it runs.
 *
 * The same rule serves a sliding door, a sliding window and a sliding wardrobe, and it is stated
 * once here because getting it wrong is not obvious in a still: one leaf the full width of the
 * opening has nowhere to go, and sliding it its own width buries it in the masonry beside the jamb
 * or drives it through the next unit along. That is exactly what the reference flat's 1800 balcony
 * units did before they were rebuilt.
 *
 * The rule: each leaf takes half the clear width and laps past the meeting line, they run in
 * separate tracks so they pass one another, and the running leaf travels until its far edge reaches
 * where its partner's far edge is - at which point it is stacked exactly on its partner and still
 * wholly inside the opening.
 *
 * Everything is a distance along the run from the jamb the leaf is set out from, in centimetres.
 * The cross-run offset - which track a leaf runs in - is deliberately not here: a door's two tracks
 * straddle the centre of a wall, a wardrobe's stand in front of a carcass, and that is placement
 * rather than set-out.
 */
struct FHFSlidingSetOut
{
	/** Near edge, from the jamb this leaf is set out from. The running clearance at that jamb. */
	double NearEdge = 0.0;

	/** Far edge: past the meeting line by the lap, which is what leaves no daylight between leaves. */
	double FarEdge = 0.0;

	/** How far this leaf runs before it comes to rest over its partner. */
	double Travel = 0.0;

	double Width() const { return FarEdge - NearEdge; }

	/**
	 * One leaf of the pair.
	 *
	 * @param HalfWidth      Half the clear width of the unit: the share this leaf covers.
	 * @param LapPastMeeting How far it reaches past the meeting line, over its partner.
	 * @param EndGap         Running clearance between its near edge and the jamb.
	 */
	static FHFSlidingSetOut Leaf(double HalfWidth, double LapPastMeeting, double EndGap)
	{
		FHFSlidingSetOut Out;
		Out.NearEdge = EndGap;
		Out.FarEdge = HalfWidth + LapPastMeeting;
		Out.Travel = FMath::Max(0.0, HalfWidth - LapPastMeeting - EndGap);
		return Out;
	}

	/**
	 * The same leaf set out from the other jamb of a unit this wide, travelling the other way.
	 *
	 * The pair is symmetrical, so the second leaf is never worked out separately - which is what
	 * stops the two halves of one unit drifting apart when the lap or the clearance changes.
	 */
	FHFSlidingSetOut MirroredIn(double ClearWidth) const
	{
		FHFSlidingSetOut Out;
		Out.NearEdge = ClearWidth - FarEdge;
		Out.FarEdge = ClearWidth - NearEdge;
		Out.Travel = -Travel;
		return Out;
	}
};
