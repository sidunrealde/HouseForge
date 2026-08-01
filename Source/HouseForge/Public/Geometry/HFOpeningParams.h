// Copyright Siddartha G. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HFOpeningParams.generated.h"

/**
 * The section of one leaf of a sliding unit, whichever kind of unit it belongs to.
 *
 * A sliding balcony door and a sliding window are the same object at two sizes: a framed sash on a
 * track, glazed, with a catch on the meeting stile. The door's section is heavier - 40 mm instead of
 * 27, 8 mm glass instead of 5, a bottom rail wider than its top rail because a door has to be pushed
 * around by its bottom edge - but nothing about how it is BUILT differs, so it is built once.
 *
 * A plain struct, not a USTRUCT: it is the argument the one sash builder takes, resolved from
 * whichever params struct the opening actually has. Nothing edits one of these directly.
 */
struct FHFSlidingSashSection
{
	/** Section depth along the wall normal. The window series is named for it. */
	double SashDepth = 2.7;

	/** Sight line of the stiles and the top rail. */
	double FaceWidth = 4.0;

	/**
	 * Sight line of the BOTTOM rail, which on a door is deeper than the rest of the sash.
	 *
	 * A window's frame is uniform all round; a door's bottom rail is the member a hand or a foot
	 * pushes, and it reads wrong at the stile width. Equal to FaceWidth reproduces a window.
	 */
	double BottomRailWidth = 4.0;

	/** Pane thickness. A solid, never a plane, or refraction and reflection read wrong. */
	double GlassThickness = 0.5;

	/** How far the pane engages into the glazing groove of each member. */
	double GlassRebate = 0.9;

	/** How far the catch or pull on the meeting stile stands proud of the sash. */
	double HandleProjection = 1.2;

	/** Width of that catch across the stile. */
	double HandleWidth = 1.6;

	/** Height of it up the stile. On a door this is a D-pull, and it is much taller. */
	double HandleHeight = 8.0;

	/**
	 * Where the centre of it sits above the bottom of the sash. Zero centres it on the sash.
	 *
	 * A window's catch is at mid height because a window is small. A door's pull is at hand height,
	 * about a metre off the floor, wherever the top of the door happens to be - centring one on a
	 * 2 m sliding door would put it at chest height on a tall person and above the head of a child.
	 */
	double HandleAboveSill = 0.0;
};

/**
 * How a door leaf and its frame are built.
 *
 * Centimetres. These were file-scope constants inside HFGenerators.cpp until they became things an
 * artist can override; the defaults here are exactly the figures that were compiled in, so a project
 * that never opens Project Settings builds the geometry it built before.
 *
 * A struct rather than a settings lookup, because the generators that read it are pure functions and
 * must stay that way - see .claude/rules/04-conventions.md. Settings are resolved into this by the
 * composing layer and handed in; nothing under Private/Geometry ever reaches for a settings object.
 *
 * The frame figures are the timber chowkhat an Indian flat's doors are hung in: a 100 x 62 section
 * with a 15 mm check cut in it, three members - head and two jambs - and no bottom member, the jambs
 * running down to the finished floor. There was no such thing here until the reference flat was
 * walked: every door in it was a bare leaf floating in a bare hole, which is the one thing about a
 * door that a plan view can never show you.
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
		meta = (ClampMin = "0.5", ClampMax = "6.0", UIMin = "2.5", UIMax = "6.0"))
	double LeafThickness = 4.0;

	/**
	 * Gap left all round a leaf inside its opening, in centimetres.
	 *
	 * Not cosmetic. A leaf cut to the exact opening shares faces with the reveal and the two z-fight
	 * through every frame of a walkthrough - the one artefact a still screenshot will not show you.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge",
		meta = (ClampMin = "0.0", ClampMax = "1.5", UIMin = "0.1", UIMax = "1.5"))
	double LeafFrameGap = 0.5;

	/**
	 * Depth of the frame section across the wall, in centimetres.
	 *
	 * 100 mm is the internal chowkhat; a main door is 125. It is deliberately NOT stretched to the
	 * wall thickness. A frame does not grow to fill a 230 mm external wall - the section is fixed, set
	 * at the room-side face, and the rest of the reveal is left as plastered masonry. That is why one
	 * section serves a 115 wall and a 230 wall, which is what the flat needs.
	 *
	 * Clamped to the wall it lands in, so it can never come out through the far face.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge",
		meta = (ClampMin = "1.0", ClampMax = "30.0", UIMin = "6.0", UIMax = "15.0"))
	double FrameDepth = 10.0;

	/**
	 * Sight line: how far the frame eats into the opening on each jamb and at the head, in centimetres.
	 *
	 * 62 mm. Subtracted from the masonry opening on both jambs and at the head, so a 900 doorway has a
	 * 776 clear daylight opening between frame faces - which is why a 900 door leaf is not 900 wide.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge",
		meta = (ClampMin = "0.5", ClampMax = "15.0", UIMin = "3.0", UIMax = "10.0"))
	double FrameFace = 6.2;

	/**
	 * The stop the shut leaf comes up against, measured into the opening, in centimetres.
	 *
	 * The rebate - the check - is the rest of the sight line, and the leaf laps into it by this much
	 * on both jambs and at the head. 12-15 mm internal, 15-18 on a main door.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge",
		meta = (ClampMin = "0.0", ClampMax = "4.0", UIMin = "0.8", UIMax = "2.5"))
	double RebateStop = 1.5;

	/**
	 * How far the frame's outer faces are buried in the masonry around it, in centimetres.
	 *
	 * 3-5 mm. A frame built exactly to the reveal shares a face with it, and two surfaces in one plane
	 * are the flicker the reference flat was full of. Embedding costs nothing and cannot be seen.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge",
		meta = (ClampMin = "0.0", ClampMax = "3.0", UIMin = "0.2", UIMax = "1.0"))
	double FrameEmbed = 0.4;

	/**
	 * How far the frame's room-side face stands proud of the wall face, in centimetres.
	 *
	 * A chowkhat is fixed to the masonry and the plaster is brought up to it, so it finishes a few
	 * millimetres forward of the wall rather than flush with it. The shut leaf finishes flush with the
	 * FRAME, so this carries the leaf forward too.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge",
		meta = (ClampMin = "0.0", ClampMax = "3.0", UIMin = "0.0", UIMax = "1.5"))
	double FrameProud = 0.6;

	/**
	 * How far the leaf's bottom edge is lifted off the finished floor, in centimetres.
	 *
	 * 6-12 mm. There is no bottom member to a door frame - the jambs run to the floor - so the leaf
	 * has nothing to close down onto, and an undercut is what a real door has there. It is also what
	 * keeps the leaf's bottom face out of the floor finish's plane.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge",
		meta = (ClampMin = "0.0", ClampMax = "5.0", UIMin = "0.4", UIMax = "2.0"))
	double LeafUndercut = 1.0;

	/**
	 * How far each jamb carries the leaf's edge in from the masonry opening, in centimetres.
	 *
	 * The sight line less the lap into the check. The hinge line moves in by exactly this, which is
	 * what keeps a leaf hung in a frame rather than pivoting about a jamb it no longer touches.
	 */
	double LeafInset() const { return FMath::Max(FrameFace - RebateStop, 0.0); }

	/**
	 * Depth of the check the leaf shuts into, across the wall, in centimetres.
	 *
	 * The leaf plus its running clearance, so the shut leaf's back face clears the stop instead of
	 * resting exactly in its plane - two coplanar faces in contact through every frame of a
	 * walkthrough, which is the flicker this whole pass exists to remove.
	 */
	double RebateDepth() const { return LeafThickness + LeafFrameGap; }

	/** Width of the leaf hung in a frame in an opening this wide. */
	double LeafWidth(double OpeningWidth) const
	{
		return OpeningWidth - (LeafInset() + LeafFrameGap) * 2.0;
	}

	/** Height of that leaf: down from the head's check to the undercut over the floor. */
	double LeafHeight(double OpeningHeight) const
	{
		return OpeningHeight - LeafInset() - LeafFrameGap - LeafUndercut;
	}

	/** @see FHFOpeningBuildParams::Sanitised */
	FHFDoorParams Sanitised(double OpeningWidth, double OpeningHeight) const;
};

/**
 * The aluminium sliding door this plugin builds, in centimetres.
 *
 * A 1800 x 2100 balcony door in an Indian flat is GLAZED. It is a full-height sliding window with a
 * threshold under it: a 92 mm two-track outer frame carrying two 40 mm sashes, 8 mm toughened glass,
 * a bottom rail deeper than the top one, and a track you step over. It was built here as two opaque
 * slabs in a bare hole until the flat was walked, which is a defect no plan view can show.
 *
 * The same arithmetic check the window section makes: two sash sections plus running clearance must
 * come to the frame depth. 2 x 40 + 12 = 92.
 */
USTRUCT(BlueprintType)
struct HOUSEFORGE_API FHFSlidingDoorParams
{
	GENERATED_BODY()

	/** Outer frame depth front to back, in centimetres. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge",
		meta = (ClampMin = "1.0", ClampMax = "16.0", UIMin = "6.0", UIMax = "14.0"))
	double FrameDepth = 9.2;

	/** How far the outer frame eats into the clear opening on each side, in centimetres. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge",
		meta = (ClampMin = "0.5", ClampMax = "12.0", UIMin = "3.0", UIMax = "9.0"))
	double FrameFace = 5.5;

	/** Sash section depth along the wall normal, in centimetres. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge",
		meta = (ClampMin = "0.5", ClampMax = "8.0", UIMin = "3.0", UIMax = "6.0"))
	double SashDepth = 4.0;

	/** Track pitch, centre to centre between the two tracks, in centimetres. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge",
		meta = (ClampMin = "0.5", ClampMax = "9.0", UIMin = "4.0", UIMax = "6.0"))
	double TrackPitch = 4.6;

	/** Sight line of the sash stiles and the top rail, in centimetres. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge",
		meta = (ClampMin = "0.5", ClampMax = "10.0", UIMin = "3.5", UIMax = "7.0"))
	double SashFaceWidth = 5.0;

	/**
	 * Sight line of the sash's BOTTOM rail, in centimetres.
	 *
	 * A door's bottom rail is deeper than its top rail - 80 to 120 mm - because it is the member that
	 * gets pushed, kicked and dragged. A window's frame is uniform, and building a door as one is the
	 * quickest way to make a 2 m sliding door read as an oversized window.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge",
		meta = (ClampMin = "0.5", ClampMax = "20.0", UIMin = "6.0", UIMax = "14.0"))
	double BottomRailWidth = 10.0;

	/** Total overlap of the two meeting stiles when shut, in centimetres. Half of it per sash. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge",
		meta = (ClampMin = "0.0", ClampMax = "8.0", UIMin = "2.0", UIMax = "4.0"))
	double InterlockOverlap = 3.0;

	/**
	 * Pane thickness, in centimetres.
	 *
	 * 8 mm toughened, not the window's 5. A door pane is a bigger sheet, it gets slammed, and it is
	 * the one figure that must not be inherited from the window path just because the code is shared.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge",
		meta = (ClampMin = "0.1", ClampMax = "2.0", UIMin = "0.6", UIMax = "1.2"))
	double GlassThickness = 0.8;

	/** How far the pane sits into the sash's glazing groove, in centimetres. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge",
		meta = (ClampMin = "0.0", ClampMax = "2.5", UIMin = "1.0", UIMax = "1.5"))
	double GlassRebate = 1.2;

	/**
	 * Overall height of the threshold member, floor to the top of it, in centimetres.
	 *
	 * The one member a sliding door has that a hinged door does not: a bottom track you step over.
	 * The sashes stand on it, so it also sets where the glazing starts.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge",
		meta = (ClampMin = "0.1", ClampMax = "10.0", UIMin = "2.0", UIMax = "4.0"))
	double ThresholdHeight = 3.0;

	/** Upstand the rollers ride on, standing proud of the threshold, in centimetres. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge",
		meta = (ClampMin = "0.0", ClampMax = "5.0", UIMin = "1.0", UIMax = "3.0"))
	double TrackUpstand = 2.0;

	/** Width of one track rail section, in centimetres. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge",
		meta = (ClampMin = "0.1", ClampMax = "2.0", UIMin = "0.6", UIMax = "1.0"))
	double TrackWidth = 0.8;

	/** How far the D-pull on the meeting stile stands proud of the sash, in centimetres. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge",
		meta = (ClampMin = "0.0", ClampMax = "6.0", UIMin = "1.5", UIMax = "4.0"))
	double HandleProjection = 2.5;

	/** Width of that pull across the stile, in centimetres. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge",
		meta = (ClampMin = "0.1", ClampMax = "6.0", UIMin = "2.0", UIMax = "4.0"))
	double HandleWidth = 3.0;

	/** Height of that pull up the stile, in centimetres. A door pull, so 200-300 mm of it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge",
		meta = (ClampMin = "0.1", ClampMax = "60.0", UIMin = "15.0", UIMax = "35.0"))
	double HandleHeight = 25.0;

	/** Centre height of that pull above the threshold, in centimetres. Hand height, not mid-panel. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge",
		meta = (ClampMin = "1.0", ClampMax = "180.0", UIMin = "80.0", UIMax = "120.0"))
	double HandleAboveSill = 100.0;

	/** Narrowest sash worth building, in centimetres. Below twice this the unit is fixed glazing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge",
		meta = (ClampMin = "1.0", ClampMax = "80.0", UIMin = "15.0", UIMax = "60.0"))
	double MinSashWidth = 20.0;

	/** Shortest sash worth building, in centimetres. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge",
		meta = (ClampMin = "1.0", ClampMax = "150.0", UIMin = "20.0", UIMax = "120.0"))
	double MinSashHeight = 40.0;

	/** Clear width inside the outer frame - what the two sashes actually have to fill. */
	double ClearWidth(double OpeningWidth) const { return OpeningWidth - FrameFace * 2.0; }

	/** Clear height between the threshold's top and the head member's underside. */
	double ClearHeight(double OpeningHeight) const { return OpeningHeight - FrameFace - ThresholdHeight; }

	/** True when the opening is big enough to be built as a real two-panel unit. */
	bool HasSashes(double OpeningWidth, double OpeningHeight) const
	{
		return ClearWidth(OpeningWidth) >= MinSashWidth * 2.0
			&& ClearHeight(OpeningHeight) >= MinSashHeight;
	}

	/** The sash section, in the form the one shared sash builder takes. */
	FHFSlidingSashSection SashSection() const
	{
		FHFSlidingSashSection Section;
		Section.SashDepth = SashDepth;
		Section.FaceWidth = SashFaceWidth;
		Section.BottomRailWidth = BottomRailWidth;
		Section.GlassThickness = GlassThickness;
		Section.GlassRebate = GlassRebate;
		Section.HandleProjection = HandleProjection;
		Section.HandleWidth = HandleWidth;
		Section.HandleHeight = HandleHeight;
		Section.HandleAboveSill = HandleAboveSill;
		return Section;
	}

	/** @see FHFOpeningBuildParams::Sanitised */
	FHFSlidingDoorParams Sanitised(double OpeningWidth, double OpeningHeight) const;
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
		meta = (ClampMin = "1.0", ClampMax = "12.0", UIMin = "4.0", UIMax = "12.0"))
	double FrameDepth = 6.5;

	/**
	 * How far the outer frame eats into the clear opening on each side, in centimetres.
	 *
	 * Fabricators quote 40-50 mm. It is subtracted from the opening on all four sides, so it sets how
	 * much glass a given hole in the wall actually gets.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge",
		meta = (ClampMin = "0.5", ClampMax = "8.0", UIMin = "3.0", UIMax = "8.0"))
	double FrameFace = 4.5;

	/** Sash section depth along the wall normal, in centimetres. The series is named for it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge",
		meta = (ClampMin = "0.5", ClampMax = "5.0", UIMin = "2.0", UIMax = "5.0"))
	double SashDepth = 2.7;

	/**
	 * Track pitch, centre to centre between the two tracks, in centimetres.
	 *
	 * 2.75-3.0 is the band. At 3.0 a pair of 27 mm sashes clears itself by 3 mm, which is why the two
	 * numbers are stated separately rather than one derived from the other.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge",
		meta = (ClampMin = "0.5", ClampMax = "5.0", UIMin = "2.5", UIMax = "5.0"))
	double TrackPitch = 3.0;

	/** Sight line of the sash stiles and rails around the glass, in centimetres. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge",
		meta = (ClampMin = "0.5", ClampMax = "6.0", UIMin = "3.0", UIMax = "6.0"))
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
		meta = (ClampMin = "0.0", ClampMax = "4.0", UIMin = "1.0", UIMax = "4.0"))
	double InterlockOverlap = 2.5;

	/** Pane thickness, in centimetres. 5 mm clear toughened is the near-universal included spec. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge",
		meta = (ClampMin = "0.1", ClampMax = "1.2", UIMin = "0.4", UIMax = "1.2"))
	double GlassThickness = 0.5;

	/** How far the pane sits into the sash's glazing groove, in centimetres. The groove is 18 mm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge",
		meta = (ClampMin = "0.0", ClampMax = "1.8", UIMin = "0.4", UIMax = "1.8"))
	double GlassRebate = 0.9;

	/** Upstand the sash rollers ride on, standing proud of the frame's sill member, in centimetres. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge",
		meta = (ClampMin = "0.0", ClampMax = "3.0", UIMin = "0.8", UIMax = "3.0"))
	double TrackUpstand = 1.5;

	/** Width of one track section, in centimetres. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge",
		meta = (ClampMin = "0.1", ClampMax = "1.5", UIMin = "0.3", UIMax = "1.5"))
	double TrackWidth = 0.6;

	/** How far the catch on the meeting stile stands proud, in centimetres. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge",
		meta = (ClampMin = "0.0", ClampMax = "3.0", UIMin = "0.5", UIMax = "3.0"))
	double HandleProjection = 1.2;

	/** Width of the catch across the stile, in centimetres. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge",
		meta = (ClampMin = "0.1", ClampMax = "4.0", UIMin = "1.0", UIMax = "4.0"))
	double HandleWidth = 1.6;

	/** Height of the catch up the stile, in centimetres. The only part of the window anybody touches. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge",
		meta = (ClampMin = "0.1", ClampMax = "20.0", UIMin = "4.0", UIMax = "20.0"))
	double HandleHeight = 8.0;

	/**
	 * Narrowest sash worth building, in centimetres.
	 *
	 * Below twice this the clear opening cannot be divided into two sashes at all, and the window is
	 * honestly built as fixed glazing instead. Both the sash builder and the fixed infill ask the
	 * same question, so they cannot disagree and leave a framed hole.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge",
		meta = (ClampMin = "1.0", ClampMax = "60.0", UIMin = "10.0", UIMax = "60.0"))
	double MinSashWidth = 20.0;

	/** Shortest sash worth building, in centimetres. Below it the window is fixed glazing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge",
		meta = (ClampMin = "1.0", ClampMax = "60.0", UIMin = "10.0", UIMax = "60.0"))
	double MinSashHeight = 25.0;

	/** Clear width inside the outer frame - what the sashes actually have to fill. */
	double ClearWidth(double OpeningWidth) const { return OpeningWidth - FrameFace * 2.0; }

	/** True when the opening is big enough to be built as a real two-sash unit. */
	bool HasSashes(double OpeningWidth, double OpeningHeight) const
	{
		return OpeningWidth - FrameFace * 2.0 >= MinSashWidth * 2.0
			&& OpeningHeight - FrameFace * 2.0 >= MinSashHeight;
	}

	/**
	 * The sash section, in the form the one shared sash builder takes.
	 *
	 * A window's frame is uniform all round, so its bottom rail is its face width - which is exactly
	 * what makes a door's deeper bottom rail a parameter rather than a special case in the builder.
	 */
	FHFSlidingSashSection SashSection() const
	{
		FHFSlidingSashSection Section;
		Section.SashDepth = SashDepth;
		Section.FaceWidth = SashFaceWidth;
		Section.BottomRailWidth = SashFaceWidth;
		Section.GlassThickness = GlassThickness;
		Section.GlassRebate = GlassRebate;
		Section.HandleProjection = HandleProjection;
		Section.HandleWidth = HandleWidth;
		Section.HandleHeight = HandleHeight;
		return Section;
	}

	/**
	 * A copy with the section made to close.
	 *
	 * @see FHFOpeningBuildParams::Sanitised for why this exists at all.
	 */
	FHFSlidingWindowParams Sanitised(double OpeningWidth, double OpeningHeight) const;
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
		meta = (ClampMin = "1.0", ClampMax = "12.0", UIMin = "3.0", UIMax = "12.0"))
	double FrameDepth = 6.0;

	/** How far the frame eats into the clear opening on each side, in centimetres. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge",
		meta = (ClampMin = "0.5", ClampMax = "6.0", UIMin = "2.0", UIMax = "6.0"))
	double FrameFace = 3.5;

	/**
	 * Sash thickness, in centimetres.
	 *
	 * IS practice is 20/25/30 mm by opening size; a ventilator is at the small end of that, so 25.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge",
		meta = (ClampMin = "0.5", ClampMax = "4.0", UIMin = "1.5", UIMax = "4.0"))
	double SashThickness = 2.5;

	/** Sight line of the sash stiles and rails around the pane, in centimetres. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge",
		meta = (ClampMin = "0.5", ClampMax = "6.0", UIMin = "2.0", UIMax = "6.0"))
	double SashFaceWidth = 3.0;

	/** Pane thickness, in centimetres. 4 mm, as a ventilator pane or a louvre blade is. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge",
		meta = (ClampMin = "0.1", ClampMax = "0.8", UIMin = "0.3", UIMax = "0.8"))
	double GlassThickness = 0.4;

	/** How far the pane runs under the frame all round, in centimetres. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge",
		meta = (ClampMin = "0.0", ClampMax = "1.5", UIMin = "0.3", UIMax = "1.5"))
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
		meta = (ClampMin = "0.0", ClampMax = "4.0", UIMin = "0.5", UIMax = "4.0"))
	double PullProjection = 1.5;

	/** Width of that pull along the rail, in centimetres. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge",
		meta = (ClampMin = "0.1", ClampMax = "15.0", UIMin = "3.0", UIMax = "15.0"))
	double PullWidth = 6.0;

	/** Height of that pull, in centimetres. How a ventilator this high up is reached at all. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge",
		meta = (ClampMin = "0.1", ClampMax = "4.0", UIMin = "0.8", UIMax = "4.0"))
	double PullHeight = 1.4;

	/** Narrowest sash worth building, in centimetres. Below it the ventilator is fixed glazing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge",
		meta = (ClampMin = "1.0", ClampMax = "60.0", UIMin = "10.0", UIMax = "60.0"))
	double MinSashWidth = 20.0;

	/** Shortest sash worth building, in centimetres. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge",
		meta = (ClampMin = "1.0", ClampMax = "60.0", UIMin = "10.0", UIMax = "60.0"))
	double MinSashHeight = 25.0;

	/** True when the opening can carry an opening sash rather than being glazed shut. */
	bool HasSash(double OpeningWidth, double OpeningHeight) const
	{
		return OpeningWidth - FrameFace * 2.0 >= MinSashWidth
			&& OpeningHeight - FrameFace * 2.0 >= MinSashHeight;
	}

	/** @see FHFOpeningBuildParams::Sanitised */
	FHFVentilatorParams Sanitised(double OpeningWidth, double OpeningHeight) const;
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
		meta = (ClampMin = "1.0", ClampMax = "15.0", UIMin = "3.0", UIMax = "15.0"))
	double FrameDepth = 6.0;

	/** How far the frame eats into the clear opening on each side, in centimetres. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge",
		meta = (ClampMin = "0.5", ClampMax = "10.0", UIMin = "3.0", UIMax = "10.0"))
	double FrameFace = 5.0;

	/** Pane thickness, in centimetres. A solid, never a plane, or refraction reads wrong. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge",
		meta = (ClampMin = "0.1", ClampMax = "2.5", UIMin = "0.4", UIMax = "2.5"))
	double GlassThickness = 0.8;

	/**
	 * Opening width above which a central mullion is added, in centimetres.
	 *
	 * A single unbroken pane wider than this reads as shopfront glazing rather than as a residential
	 * window. A sliding unit never gets one whatever this says: its meeting stiles are the mullion.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge",
		meta = (ClampMin = "1.0", ClampMax = "300.0", UIMin = "60.0", UIMax = "300.0"))
	double MullionAboveWidth = 120.0;

	/** @see FHFOpeningBuildParams::Sanitised */
	FHFFixedWindowParams Sanitised(double OpeningWidth, double OpeningHeight) const;
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
	FHFSlidingDoorParams SlidingDoor;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	FHFSlidingWindowParams SlidingWindow;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	FHFVentilatorParams Ventilator;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	FHFFixedWindowParams FixedWindow;

	/**
	 * A copy in which every figure is compatible with every other figure, for this opening.
	 *
	 * ClampMin and ClampMax bound each field on its own, and that is all they can do. Every rule that
	 * actually keeps a unit well formed is a rule BETWEEN two fields, and no amount of metadata can
	 * state one:
	 *
	 *   - the glazing rebate must be shallower than the sash section it is cut into, or the pane is
	 *     wider than the sash that holds it and stands out through the frame into the reveal;
	 *   - the track pitch must exceed the sash depth, or the two sashes occupy the same band of the
	 *     wall and the running one passes bodily through the fixed one as it opens;
	 *   - the outer frame must be deeper than the tracks and sashes it contains, or they stand proud
	 *     of it into the room;
	 *   - the frame face and the leaf gap are subtracted from the opening on every side, so either
	 *     one, taken far enough, leaves nothing - and AppendBox declines a non-positive box, so what
	 *     the artist sees is not a warning but a pane, or a whole door leaf, that is simply absent.
	 *
	 * Each of those is reachable with two values that are individually inside their own clamps. This
	 * is where they are reconciled, and it is the same answer the joinery kit reached with its
	 * SanitiseShutter/SanitisePlinth family - the opening params were the half of the codebase that
	 * had not caught up.
	 *
	 * Sanitising here rather than at the settings page is deliberate. Values loaded from an ini go
	 * straight into the UPROPERTY without passing through the details panel, so ClampMin and ClampMax
	 * never see them; and BuildParams is editable per actor, so the settings page is not the only way
	 * in. The generator is the one place every route converges, and it is still a pure function of
	 * its arguments - see .claude/rules/04-conventions.md.
	 */
	FHFOpeningBuildParams Sanitised(double OpeningWidth, double OpeningHeight) const;
};
