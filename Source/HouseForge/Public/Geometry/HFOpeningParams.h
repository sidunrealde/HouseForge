// Copyright Siddartha G. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HFOpeningParams.generated.h"

/**
 * How a door leaf and a sliding door's panels are built.
 *
 * Centimetres. These were file-scope constants inside HFGenerators.cpp until they became things an
 * artist can override; the defaults here are exactly the figures that were compiled in, so a project
 * that never opens Project Settings builds the geometry it built before.
 *
 * A struct rather than a settings lookup, because the generators that read it are pure functions and
 * must stay that way - see .claude/rules/04-conventions.md. Settings are resolved into this by the
 * composing layer and handed in; nothing under Private/Geometry ever reaches for a settings object.
 */
USTRUCT(BlueprintType)
struct HOUSEFORGE_API FHFDoorParams
{
	GENERATED_BODY()

	/**
	 * Finished thickness of a door leaf, in centimetres.
	 *
	 * 4.0 is a 40 mm flush shutter, which is what an Indian internal door is. A main door is 45.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge",
		meta = (ClampMin = "0.5", UIMin = "2.5", UIMax = "6.0"))
	double LeafThickness = 4.0;

	/**
	 * Gap left all round a leaf inside its opening, in centimetres.
	 *
	 * Not cosmetic. A leaf cut to the exact opening shares faces with the reveal and the two z-fight
	 * through every frame of a walkthrough - the one artefact a still screenshot will not show you.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge",
		meta = (ClampMin = "0.0", UIMin = "0.1", UIMax = "1.5"))
	double LeafFrameGap = 0.5;

	/**
	 * How far the meeting stiles of a sliding door's two panels overlap when shut, in centimetres.
	 *
	 * Per panel: each panel runs this far past the centreline, so the panels cross by twice it. It is
	 * what stops daylight showing down the middle of a closed unit, and it is subtracted from the
	 * travel so the running panel comes to rest exactly over its fixed partner.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge",
		meta = (ClampMin = "0.0", UIMin = "1.0", UIMax = "6.0"))
	double SlidingPanelOverlap = 2.5;

	/**
	 * Clearance between the two tracks of a sliding door, in centimetres.
	 *
	 * The panels have to pass rather than collide, so the two tracks are offset across the wall by
	 * the leaf thickness plus this.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge",
		meta = (ClampMin = "0.0", UIMin = "0.2", UIMax = "4.0"))
	double SlidingTrackGap = 1.0;
};

/**
 * The aluminium sliding window this plugin builds, in centimetres.
 *
 * Modelled on the 27 mm Domal series, which is THE commodity window of the flats this plugin exists
 * for: a two-track outer frame 65 mm deep carrying two 27 mm sashes. The arithmetic is the check
 * that these are a real section rather than plausible numbers - two 27 mm sashes plus running
 * clearance is exactly the published 65 mm frame. Change SashDepth or TrackPitch and FrameDepth
 * should move with them, or the section stops closing.
 */
USTRUCT(BlueprintType)
struct HOUSEFORGE_API FHFSlidingWindowParams
{
	GENERATED_BODY()

	/** Outer frame depth front to back, in centimetres. Sits inside the wall reveal. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge",
		meta = (ClampMin = "1.0", UIMin = "4.0", UIMax = "12.0"))
	double FrameDepth = 6.5;

	/**
	 * How far the outer frame eats into the clear opening on each side, in centimetres.
	 *
	 * Fabricators quote 40-50 mm. It is subtracted from the opening on all four sides, so it sets how
	 * much glass a given hole in the wall actually gets.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge",
		meta = (ClampMin = "0.5", UIMin = "3.0", UIMax = "8.0"))
	double FrameFace = 4.5;

	/** Sash section depth along the wall normal, in centimetres. The series is named for it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge",
		meta = (ClampMin = "0.5", UIMin = "2.0", UIMax = "5.0"))
	double SashDepth = 2.7;

	/**
	 * Track pitch, centre to centre between the two tracks, in centimetres.
	 *
	 * 2.75-3.0 is the band. At 3.0 a pair of 27 mm sashes clears itself by 3 mm, which is why the two
	 * numbers are stated separately rather than one derived from the other.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge",
		meta = (ClampMin = "0.5", UIMin = "2.5", UIMax = "5.0"))
	double TrackPitch = 3.0;

	/** Sight line of the sash stiles and rails around the glass, in centimetres. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge",
		meta = (ClampMin = "0.5", UIMin = "3.0", UIMax = "6.0"))
	double SashFaceWidth = 4.0;

	/**
	 * Total overlap of the two meeting stiles when closed, in centimetres.
	 *
	 * TOTAL, not per sash - each sash runs half of it past the midline. No manufacturer publishes the
	 * figure; 15-25 mm is what the section geometry allows and 25 is the top of that band. It is the
	 * one number here visible in a render rather than only in a section: too little and daylight shows
	 * between the sashes, too much and the running sash stops short of its partner.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge",
		meta = (ClampMin = "0.0", UIMin = "1.0", UIMax = "4.0"))
	double InterlockOverlap = 2.5;

	/** Pane thickness, in centimetres. 5 mm clear toughened is the near-universal included spec. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge",
		meta = (ClampMin = "0.1", UIMin = "0.4", UIMax = "1.2"))
	double GlassThickness = 0.5;

	/** How far the pane sits into the sash's glazing groove, in centimetres. The groove is 18 mm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge",
		meta = (ClampMin = "0.0", UIMin = "0.4", UIMax = "1.8"))
	double GlassRebate = 0.9;

	/** Upstand the sash rollers ride on, standing proud of the frame's sill member, in centimetres. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge",
		meta = (ClampMin = "0.0", UIMin = "0.8", UIMax = "3.0"))
	double TrackUpstand = 1.5;

	/** Width of one track section, in centimetres. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge",
		meta = (ClampMin = "0.1", UIMin = "0.3", UIMax = "1.5"))
	double TrackWidth = 0.6;

	/** How far the catch on the meeting stile stands proud, in centimetres. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge",
		meta = (ClampMin = "0.0", UIMin = "0.5", UIMax = "3.0"))
	double HandleProjection = 1.2;

	/** Width of the catch across the stile, in centimetres. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge",
		meta = (ClampMin = "0.1", UIMin = "1.0", UIMax = "4.0"))
	double HandleWidth = 1.6;

	/** Height of the catch up the stile, in centimetres. The only part of the window anybody touches. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge",
		meta = (ClampMin = "0.1", UIMin = "4.0", UIMax = "20.0"))
	double HandleHeight = 8.0;

	/**
	 * Narrowest sash worth building, in centimetres.
	 *
	 * Below twice this the clear opening cannot be divided into two sashes at all, and the window is
	 * honestly built as fixed glazing instead. Both the sash builder and the fixed infill ask the
	 * same question, so they cannot disagree and leave a framed hole.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge",
		meta = (ClampMin = "1.0", UIMin = "10.0", UIMax = "60.0"))
	double MinSashWidth = 20.0;

	/** Shortest sash worth building, in centimetres. Below it the window is fixed glazing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge",
		meta = (ClampMin = "1.0", UIMin = "10.0", UIMax = "60.0"))
	double MinSashHeight = 25.0;

	/** Clear width inside the outer frame - what the sashes actually have to fill. */
	double ClearWidth(double OpeningWidth) const { return OpeningWidth - FrameFace * 2.0; }

	/** True when the opening is big enough to be built as a real two-sash unit. */
	bool HasSashes(double OpeningWidth, double OpeningHeight) const
	{
		return OpeningWidth - FrameFace * 2.0 >= MinSashWidth * 2.0
			&& OpeningHeight - FrameFace * 2.0 >= MinSashHeight;
	}
};

/**
 * A top-hung ventilator sash, in centimetres.
 *
 * A ventilator can be a fixed louvre, in which case nothing about it moves and the rule that
 * anything which moves must be able to move is already satisfied. A top-hung pivot sash is the other
 * half of the category and it does move: it hangs on hinges at its head and its bottom edge swings
 * out. That is what these describe.
 */
USTRUCT(BlueprintType)
struct HOUSEFORGE_API FHFVentilatorParams
{
	GENERATED_BODY()

	/** Outer frame depth front to back, in centimetres. 60 mm is the commodity uPVC section. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge",
		meta = (ClampMin = "1.0", UIMin = "3.0", UIMax = "12.0"))
	double FrameDepth = 6.0;

	/** How far the frame eats into the clear opening on each side, in centimetres. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge",
		meta = (ClampMin = "0.5", UIMin = "2.0", UIMax = "6.0"))
	double FrameFace = 3.5;

	/**
	 * Sash thickness, in centimetres.
	 *
	 * IS practice is 20/25/30 mm by opening size; a ventilator is at the small end of that, so 25.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge",
		meta = (ClampMin = "0.5", UIMin = "1.5", UIMax = "4.0"))
	double SashThickness = 2.5;

	/** Sight line of the sash stiles and rails around the pane, in centimetres. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge",
		meta = (ClampMin = "0.5", UIMin = "2.0", UIMax = "6.0"))
	double SashFaceWidth = 3.0;

	/** Pane thickness, in centimetres. 4 mm, as a ventilator pane or a louvre blade is. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge",
		meta = (ClampMin = "0.1", UIMin = "0.3", UIMax = "0.8"))
	double GlassThickness = 0.4;

	/** How far the pane runs under the frame all round, in centimetres. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge",
		meta = (ClampMin = "0.0", UIMin = "0.3", UIMax = "1.5"))
	double GlassRebate = 0.6;

	/**
	 * How far a top-hung sash comes open, in degrees.
	 *
	 * Past 30 the stay fouls the reveal. This is the travel limit of the hinge, so raising it opens
	 * the sash further into the room and eventually through the wall it sits in.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge",
		meta = (ClampMin = "1.0", ClampMax = "90.0", UIMin = "10.0", UIMax = "45.0"))
	double OpenAngleDegrees = 30.0;

	/** How far the pull on the bottom rail stands proud, in centimetres. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge",
		meta = (ClampMin = "0.0", UIMin = "0.5", UIMax = "4.0"))
	double PullProjection = 1.5;

	/** Width of that pull along the rail, in centimetres. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge",
		meta = (ClampMin = "0.1", UIMin = "3.0", UIMax = "15.0"))
	double PullWidth = 6.0;

	/** Height of that pull, in centimetres. How a ventilator this high up is reached at all. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge",
		meta = (ClampMin = "0.1", UIMin = "0.8", UIMax = "4.0"))
	double PullHeight = 1.4;

	/** Narrowest sash worth building, in centimetres. Below it the ventilator is fixed glazing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge",
		meta = (ClampMin = "1.0", UIMin = "10.0", UIMax = "60.0"))
	double MinSashWidth = 20.0;

	/** Shortest sash worth building, in centimetres. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge",
		meta = (ClampMin = "1.0", UIMin = "10.0", UIMax = "60.0"))
	double MinSashHeight = 25.0;

	/** True when the opening can carry an opening sash rather than being glazed shut. */
	bool HasSash(double OpeningWidth, double OpeningHeight) const
	{
		return OpeningWidth - FrameFace * 2.0 >= MinSashWidth
			&& OpeningHeight - FrameFace * 2.0 >= MinSashHeight;
	}
};

/**
 * A window with nothing that moves: a frame around the reveal with a pane in it, in centimetres.
 *
 * Used for EHFOpeningKind::Window, and as the fallback for a sliding window or ventilator too small
 * to divide into sashes.
 */
USTRUCT(BlueprintType)
struct HOUSEFORGE_API FHFFixedWindowParams
{
	GENERATED_BODY()

	/** Frame depth front to back, in centimetres. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge",
		meta = (ClampMin = "1.0", UIMin = "3.0", UIMax = "15.0"))
	double FrameDepth = 6.0;

	/** How far the frame eats into the clear opening on each side, in centimetres. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge",
		meta = (ClampMin = "0.5", UIMin = "3.0", UIMax = "10.0"))
	double FrameFace = 5.0;

	/** Pane thickness, in centimetres. A solid, never a plane, or refraction reads wrong. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge",
		meta = (ClampMin = "0.1", UIMin = "0.4", UIMax = "2.5"))
	double GlassThickness = 0.8;

	/**
	 * Opening width above which a central mullion is added, in centimetres.
	 *
	 * A single unbroken pane wider than this reads as shopfront glazing rather than as a residential
	 * window. A sliding unit never gets one whatever this says: its meeting stiles are the mullion.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge",
		meta = (ClampMin = "1.0", UIMin = "60.0", UIMax = "300.0"))
	double MullionAboveWidth = 120.0;
};

/**
 * Everything the opening generators need beyond the spec itself.
 *
 * Passed by value into pure generator functions, which is the whole point: a headless test builds
 * one of these by hand and no settings object need exist. Default-constructed, it reproduces the
 * constants that were compiled into HFGenerators.cpp before any of this was overridable.
 */
USTRUCT(BlueprintType)
struct HOUSEFORGE_API FHFOpeningBuildParams
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	FHFDoorParams Door;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	FHFSlidingWindowParams SlidingWindow;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	FHFVentilatorParams Ventilator;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	FHFFixedWindowParams FixedWindow;
};
