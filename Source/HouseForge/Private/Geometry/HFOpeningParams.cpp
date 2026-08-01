// Copyright Siddartha G. All Rights Reserved.

#include "Geometry/HFOpeningParams.h"

namespace
{
	/**
	 * The smallest member worth emitting, in centimetres.
	 *
	 * Not a domain figure and not something to expose: it is the floor below which a board is a
	 * rounding error, and it exists so a zero or a negative can never reach a box's extents.
	 */
	constexpr double MinSection = 0.05;

	/**
	 * Running clearance between two sashes sharing a frame, in centimetres.
	 *
	 * 3 mm, which is exactly what the shipped section already gives: a 30 mm track pitch carrying
	 * 27 mm sashes. So enforcing it changes nothing about the window this plugin builds by default,
	 * and only bites when a pitch has been set that the sashes do not fit in.
	 */
	constexpr double MinRunningClearance = 0.3;

	/**
	 * The deepest a glazing groove may be cut, as a fraction of the section it is cut into.
	 *
	 * A groove that eats the whole sight line leaves no shoulder holding the glass, and past the
	 * full width the pane is larger than the sash and stands out through it. Half is generous - the
	 * shipped sliding sash cuts 9 mm into a 40 mm face, which is under a quarter.
	 */
	constexpr double MaxRebateFraction = 0.5;

	/**
	 * The most of an opening one side of a frame may eat, as a fraction of that dimension.
	 *
	 * Both jambs are subtracted from the width and both head and sill from the height, so a face
	 * wider than half the opening leaves nothing at all. Kept below half so what is left is a window
	 * rather than a seam.
	 */
	constexpr double MaxFrameFaceFraction = 0.4;

	/** A frame face that leaves a real opening behind it, on both axes. */
	double ClampFrameFace(double Face, double OpeningWidth, double OpeningHeight)
	{
		const double Smallest = FMath::Min(FMath::Max(OpeningWidth, 0.0), FMath::Max(OpeningHeight, 0.0));
		return FMath::Clamp(Face, MinSection, FMath::Max(Smallest * MaxFrameFaceFraction, MinSection));
	}
}

FHFDoorParams FHFDoorParams::Sanitised(double OpeningWidth, double OpeningHeight) const
{
	FHFDoorParams Out = *this;

	Out.LeafThickness = FMath::Max(Out.LeafThickness, MinSection);
	Out.FrameEmbed = FMath::Max(Out.FrameEmbed, 0.0);
	Out.FrameProud = FMath::Max(Out.FrameProud, 0.0);

	// The frame eats into the opening on both jambs and at the head, so the same rule as every other
	// frame face: past this there is no daylight left to hang a leaf in.
	Out.FrameFace = ClampFrameFace(Out.FrameFace, OpeningWidth, OpeningHeight);

	// The check is cut out of the sight line, so a stop as wide as the face leaves no rebate for the
	// leaf to lap into and the leaf shuts against nothing. The same shoulder rule the glazing groove
	// obeys, for the same reason.
	Out.RebateStop = FMath::Clamp(Out.RebateStop, 0.0, Out.FrameFace * MaxRebateFraction);

	// What is left of the opening once both frames are in it. Everything below is measured from this
	// rather than from the masonry, which is the whole point of there being a frame at all.
	const double Inset = Out.LeafInset();
	const double ClearWidth = FMath::Max(OpeningWidth - Inset * 2.0, 0.0);
	const double ClearHeight = FMath::Max(OpeningHeight - Inset, 0.0);

	// The gap is taken off every side of the leaf, so twice it has to leave a leaf behind on both
	// axes. Past that the leaf is not thin, it is absent - AppendBox declines the box. Measured
	// against the DAYLIGHT rather than the masonry, or a face and a gap that each clear their own
	// clamp can still between them consume the whole opening.
	Out.LeafFrameGap = FMath::Clamp(Out.LeafFrameGap, 0.0,
		FMath::Max(FMath::Min(ClearWidth, ClearHeight) * MaxFrameFaceFraction, 0.0));

	// The leaf is lifted off the floor rather than closing onto a member that is not there. It comes
	// out of the leaf's height, so it cannot be allowed to eat the leaf.
	Out.LeafUndercut = FMath::Clamp(Out.LeafUndercut, 0.0,
		FMath::Max((ClearHeight - Out.LeafFrameGap) * 0.5, 0.0));

	// The section has to be deeper than the check cut into it, or the frame is nothing but rebate and
	// the leaf shuts against fresh air. The generator clamps this again to the wall the frame lands
	// in - a section deeper than the masonry would come out through the far face.
	Out.FrameDepth = FMath::Max(Out.FrameDepth, Out.RebateDepth() + MinSection);

	return Out;
}

FHFSlidingDoorParams FHFSlidingDoorParams::Sanitised(double OpeningWidth, double OpeningHeight) const
{
	FHFSlidingDoorParams Out = *this;

	Out.SashDepth = FMath::Max(Out.SashDepth, MinSection);
	Out.SashFaceWidth = FMath::Max(Out.SashFaceWidth, MinSection);
	Out.BottomRailWidth = FMath::Max(Out.BottomRailWidth, MinSection);
	Out.GlassThickness = FMath::Max(Out.GlassThickness, MinSection);
	Out.TrackWidth = FMath::Max(Out.TrackWidth, MinSection);
	Out.TrackUpstand = FMath::Max(Out.TrackUpstand, 0.0);
	Out.MinSashWidth = FMath::Max(Out.MinSashWidth, MinSection);
	Out.MinSashHeight = FMath::Max(Out.MinSashHeight, MinSection);

	Out.FrameFace = ClampFrameFace(Out.FrameFace, OpeningWidth, OpeningHeight);

	// The threshold and the head between them are subtracted from the height, so the threshold has to
	// leave a sash standing on it.
	Out.ThresholdHeight = FMath::Clamp(Out.ThresholdHeight, MinSection,
		FMath::Max(OpeningHeight - Out.FrameFace - MinSection, MinSection));

	// A sash has to fit between its own stiles and rails before it can hold any glass, on both axes.
	// Half the clear opening is one panel's share of the width; the clear height is what a panel has
	// between the threshold and the head.
	const double Half = FMath::Max(Out.ClearWidth(OpeningWidth), 0.0) * 0.5;
	const double PanelHeight = FMath::Max(Out.ClearHeight(OpeningHeight), 0.0);

	Out.SashFaceWidth = FMath::Clamp(Out.SashFaceWidth, MinSection,
		FMath::Max(FMath::Min(Half, PanelHeight) * MaxFrameFaceFraction, MinSection));
	Out.BottomRailWidth = FMath::Clamp(Out.BottomRailWidth, MinSection,
		FMath::Max(PanelHeight - Out.SashFaceWidth - MinSection, MinSection));

	// The upstand runs up inside the hollow bottom rail, which is where a real one sits. Taller than
	// the rail and it comes out through the top of it as a fin standing in the glass line.
	Out.TrackUpstand = FMath::Min(Out.TrackUpstand, Out.BottomRailWidth);

	// Every rule the sliding WINDOW's section obeys, because it is the same section at a bigger size:
	// a groove that leaves a shoulder, two tracks that do not share a band of wall, and a frame deep
	// enough to contain what runs in it.
	Out.GlassRebate = FMath::Clamp(Out.GlassRebate, 0.0,
		FMath::Min(Out.SashFaceWidth, Out.BottomRailWidth) * MaxRebateFraction);
	Out.TrackPitch = FMath::Max(Out.TrackPitch, Out.SashDepth + MinRunningClearance);
	Out.FrameDepth = FMath::Max(Out.FrameDepth, Out.TrackPitch + Out.SashDepth);

	// The interlock is shared between the two panels, half each side of the midline, and it is also
	// subtracted from the travel. Wider than half the clear opening and the running panel is asked to
	// come to rest before it starts.
	Out.InterlockOverlap = FMath::Clamp(Out.InterlockOverlap, 0.0, FMath::Max(Half, 0.0));

	return Out;
}

FHFSlidingWindowParams FHFSlidingWindowParams::Sanitised(double OpeningWidth, double OpeningHeight) const
{
	FHFSlidingWindowParams Out = *this;

	Out.SashDepth = FMath::Max(Out.SashDepth, MinSection);
	Out.SashFaceWidth = FMath::Max(Out.SashFaceWidth, MinSection);
	Out.GlassThickness = FMath::Max(Out.GlassThickness, MinSection);
	Out.TrackWidth = FMath::Max(Out.TrackWidth, MinSection);
	Out.TrackUpstand = FMath::Max(Out.TrackUpstand, 0.0);
	Out.MinSashWidth = FMath::Max(Out.MinSashWidth, MinSection);
	Out.MinSashHeight = FMath::Max(Out.MinSashHeight, MinSection);

	Out.FrameFace = ClampFrameFace(Out.FrameFace, OpeningWidth, OpeningHeight);

	// The pane sits in a groove cut into the sash section. Cut deeper than the section is wide and
	// the pane comes out larger than the sash holding it, standing through the frame and into the
	// reveal - a sheet of glass floating in the masonry, in a mesh that is otherwise well formed.
	Out.GlassRebate = FMath::Clamp(Out.GlassRebate, 0.0, Out.SashFaceWidth * MaxRebateFraction);

	// The two sashes sit at plus and minus half the pitch, each half a section deep. A pitch no
	// bigger than the section puts them in the same band of wall, and the running sash then passes
	// bodily through the fixed one on its way open. Nothing about the mesh reports this: both sashes
	// are closed solids, the travel is right, and it is only visible with the window half open.
	Out.TrackPitch = FMath::Max(Out.TrackPitch, Out.SashDepth + MinRunningClearance);

	// And the outer frame has to contain what runs in it, or the sashes stand proud of the section
	// they are supposedly housed in.
	Out.FrameDepth = FMath::Max(Out.FrameDepth, Out.TrackPitch + Out.SashDepth);

	// The interlock is shared between the two sashes, half each side of the midline, and it is also
	// subtracted from the travel. Wider than the sashes themselves and the running one is asked to
	// come to rest before it starts.
	const double Half = FMath::Max(Out.ClearWidth(OpeningWidth), 0.0) * 0.5;
	Out.InterlockOverlap = FMath::Clamp(Out.InterlockOverlap, 0.0, FMath::Max(Half, 0.0));

	return Out;
}

FHFVentilatorParams FHFVentilatorParams::Sanitised(double OpeningWidth, double OpeningHeight) const
{
	FHFVentilatorParams Out = *this;

	Out.SashThickness = FMath::Max(Out.SashThickness, MinSection);
	Out.SashFaceWidth = FMath::Max(Out.SashFaceWidth, MinSection);
	Out.GlassThickness = FMath::Max(Out.GlassThickness, MinSection);
	Out.MinSashWidth = FMath::Max(Out.MinSashWidth, MinSection);
	Out.MinSashHeight = FMath::Max(Out.MinSashHeight, MinSection);

	Out.FrameFace = ClampFrameFace(Out.FrameFace, OpeningWidth, OpeningHeight);

	// The same groove rule as the sliding sash, for the same reason.
	Out.GlassRebate = FMath::Clamp(Out.GlassRebate, 0.0, Out.SashFaceWidth * MaxRebateFraction);

	Out.FrameDepth = FMath::Max(Out.FrameDepth, Out.SashThickness);

	// A top-hung sash swings into the room about its head. The angle is the hinge's travel limit, so
	// this is what stops a sash being driven round far enough to end up inside the wall below it.
	Out.OpenAngleDegrees = FMath::Clamp(Out.OpenAngleDegrees, 0.0, 90.0);

	return Out;
}

FHFFixedWindowParams FHFFixedWindowParams::Sanitised(double OpeningWidth, double OpeningHeight) const
{
	FHFFixedWindowParams Out = *this;

	Out.GlassThickness = FMath::Max(Out.GlassThickness, MinSection);
	Out.FrameFace = ClampFrameFace(Out.FrameFace, OpeningWidth, OpeningHeight);
	Out.FrameDepth = FMath::Max(Out.FrameDepth, MinSection);
	Out.MullionAboveWidth = FMath::Max(Out.MullionAboveWidth, MinSection);

	return Out;
}

FHFOpeningBuildParams FHFOpeningBuildParams::Sanitised(double OpeningWidth, double OpeningHeight) const
{
	FHFOpeningBuildParams Out;

	Out.Door = Door.Sanitised(OpeningWidth, OpeningHeight);
	Out.SlidingDoor = SlidingDoor.Sanitised(OpeningWidth, OpeningHeight);
	Out.SlidingWindow = SlidingWindow.Sanitised(OpeningWidth, OpeningHeight);
	Out.Ventilator = Ventilator.Sanitised(OpeningWidth, OpeningHeight);
	Out.FixedWindow = FixedWindow.Sanitised(OpeningWidth, OpeningHeight);

	return Out;
}
