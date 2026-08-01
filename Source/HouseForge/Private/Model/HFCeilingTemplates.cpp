// Copyright Siddartha G. All Rights Reserved.

#include "Model/HFCeilingTemplates.h"

#include "Geometry/HFMeshOps.h"
#include "HouseForge.h"

namespace
{
	/** Perimeter of a closed loop, closing edge included. */
	double LoopPerimeter(const TArray<FVector2D>& Loop)
	{
		double Length = 0.0;
		for (int32 Index = 0; Index < Loop.Num(); ++Index)
		{
			Length += FVector2D::Distance(Loop[Index], Loop[(Index + 1) % Loop.Num()]);
		}
		return Length;
	}

	/** The point a given distance along a loop, walking from its first vertex. */
	FVector2D PointAlongLoop(const TArray<FVector2D>& Loop, double Distance)
	{
		double Remaining = Distance;
		for (int32 Index = 0; Index < Loop.Num(); ++Index)
		{
			const FVector2D& A = Loop[Index];
			const FVector2D& B = Loop[(Index + 1) % Loop.Num()];
			const double EdgeLength = FVector2D::Distance(A, B);

			if (Remaining <= EdgeLength || Index == Loop.Num() - 1)
			{
				const double T = (EdgeLength > UE_KINDA_SMALL_NUMBER)
					? FMath::Clamp(Remaining / EdgeLength, 0.0, 1.0)
					: 0.0;
				return FMath::Lerp(A, B, T);
			}

			Remaining -= EdgeLength;
		}

		return Loop[0];
	}

	/** Shortest side of a loop's bounding box - a lower bound on how far across the opening is. */
	double NarrowestExtent(const TArray<TArray<FVector2D>>& Loops)
	{
		double Widest = 0.0;

		for (const TArray<FVector2D>& Loop : Loops)
		{
			if (Loop.Num() < 3)
			{
				continue;
			}

			FVector2D Min = Loop[0];
			FVector2D Max = Loop[0];
			for (const FVector2D& Point : Loop)
			{
				Min = FVector2D::Min(Min, Point);
				Max = FVector2D::Max(Max, Point);
			}

			// The widest loop wins: a deep ring can pinch an L-shaped room into two, and the
			// question is whether ANY of what is left can carry a band.
			Widest = FMath::Max(Widest, FMath::Min(Max.X - Min.X, Max.Y - Min.Y));
		}

		return Widest;
	}
}

TArray<FVector2D> FHFCeilingTemplates::PlaceDownlights(const TArray<FVector2D>& Loop,
	double Setback, double Spacing)
{
	TArray<FVector2D> Out;
	if (Loop.Num() < 3 || Spacing <= 0.0)
	{
		return Out;
	}

	// A room too small to set a run back into is a room with no run in it, which is the honest
	// answer - a bathroom gets one fitting in the middle rather than a perimeter run.
	const TArray<TArray<FVector2D>> Runs = (Setback > 0.0)
		? FHFMeshOps::InsetPolygon(Loop, Setback)
		: TArray<TArray<FVector2D>>{ Loop };

	for (const TArray<FVector2D>& Run : Runs)
	{
		const double Perimeter = LoopPerimeter(Run);
		if (Perimeter <= Spacing)
		{
			continue;
		}

		const int32 Count = FMath::Max(2, FMath::RoundToInt(Perimeter / Spacing));
		const double Step = Perimeter / Count;

		for (int32 Index = 0; Index < Count; ++Index)
		{
			Out.Add(PointAlongLoop(Run, Step * Index));
		}
	}

	return Out;
}

void FHFCeilingTemplates::Apply(FHFFalseCeiling& Ceiling, const FHFRoom& Room,
	const TArray<const FHFBeam*>& ShowingBeamPerEdge, const FHFCeilingDefaults& Defaults,
	double UnitScale)
{
	// Custom means custom. A ceiling somebody tuned by hand is not improved by having a project
	// default stamped over it the next time the settings page is opened, and the beam ring is part
	// of the design rather than a correction applied to whatever is there - the corridor's full drop
	// buries its own beams and does not want a ring round it.
	if (Ceiling.Template == EHFCeilingTemplate::Custom)
	{
		return;
	}

	const double S = (UnitScale > UE_KINDA_SMALL_NUMBER) ? UnitScale : 1.0;

	auto ScaleCove = [S](const FHFCoveProfile& In)
	{
		FHFCoveProfile Out = In;
		Out.ChannelWidth *= S;
		Out.LipHeight *= S;
		Out.Setback *= S;
		Out.StripWidth *= S;
		Out.StripHeight *= S;
		Out.StripSetback *= S;
		return Out;
	};

	// Written from scratch rather than edited in place, so re-applying cannot leave a figure behind
	// from whichever template this ceiling used to be.
	Ceiling.InnerDrop = 0.0;
	Ceiling.CentrePanelDrop = 0.0;

	// Stamped for every template, not only the ones that build a trough. A Tray ignores the cove
	// section entirely, but the figures are still written into the spec and read by a person, and
	// leaving the struct's centimetre defaults sitting in a millimetre spec is how a 100 channel
	// comes to be recorded as 10.
	Ceiling.Cove = ScaleCove(Defaults.Cove);

	switch (Ceiling.Template)
	{
	case EHFCeilingTemplate::PlainBand:
		Ceiling.Style = EHFCeilingStyle::Peripheral;
		Ceiling.Drop = Defaults.BandDrop * S;
		Ceiling.BandWidth = Defaults.BandWidth * S;
		Ceiling.Cove.bHasLedStrip = false;
		break;

	case EHFCeilingTemplate::Cove:
		Ceiling.Style = EHFCeilingStyle::Cove;
		Ceiling.Drop = Defaults.CoveDrop * S;
		Ceiling.BandWidth = Defaults.CoveBandWidth * S;
		Ceiling.Cove.bHasLedStrip = true;
		break;

	case EHFCeilingTemplate::SteppedTray:
		Ceiling.Style = EHFCeilingStyle::Tray;
		Ceiling.Drop = Defaults.TrayDrop * S;
		Ceiling.BandWidth = Defaults.TrayBandWidth * S;
		Ceiling.InnerDrop = Defaults.TrayInnerDrop * S;
		Ceiling.Cove.bHasLedStrip = false;
		break;

	case EHFCeilingTemplate::FramedPanel:
		// A cove band with the middle filled in. The channel between the lip and the panel IS the
		// shadow gap the reference photographs show round a framed panel, so there is no separate
		// gap figure to keep in step with it.
		Ceiling.Style = EHFCeilingStyle::Cove;
		Ceiling.Drop = Defaults.PanelFrameDrop * S;
		Ceiling.BandWidth = Defaults.PanelFrameWidth * S;
		Ceiling.Cove.bHasLedStrip = true;
		Ceiling.CentrePanelDrop = Defaults.PanelDrop * S;
		break;

	case EHFCeilingTemplate::FlatSoffit:
		// Flat right across, and shallow. The beams at the edges are the ring's business now, so
		// the soffit answers to the services above it instead of to the deepest beam in the room.
		Ceiling.Style = EHFCeilingStyle::FullDrop;
		Ceiling.Drop = Defaults.FlatSoffitDrop * S;
		Ceiling.BandWidth = 0.0;
		Ceiling.Cove.bHasLedStrip = false;
		break;

	default:
		return;
	}

	Ceiling.Downlight = Defaults.Downlight;
	Ceiling.Downlight.CutoutDiameter *= S;
	Ceiling.Downlight.FlangeDiameter *= S;
	Ceiling.Downlight.FlangeProjection *= S;
	Ceiling.Downlight.BodyDepth *= S;

	// ------------------------------------------------------------- the perimeter beam bulkhead
	//
	// Derived, not authored. The beams are a fact of the frame, and asking a drawing to work out
	// which rooms need a deeper ring round them is asking it to redo the calculation that produced
	// the uniform 500 drop in the first place.
	//
	// PER EDGE. A ring right round buries a nib on the two sides it shows and drops 480 for nothing
	// on the other two, and in the living room that is 15.0 of 23.8 square metres of ceiling below
	// the slab - a deep three-level frame where the reference designs are a shallow band.
	Ceiling.PerimeterBulkheadWidth = 0.0;
	Ceiling.PerimeterBulkheadDrop = 0.0;
	Ceiling.PerimeterBulkheadEdges.Reset();

	const TArray<FVector2D>& Outline = (Ceiling.ExplicitPolygon.Num() >= 3)
		? Ceiling.ExplicitPolygon
		: Room.Boundary;

	for (int32 Edge = 0; Edge < ShowingBeamPerEdge.Num() && Edge < Outline.Num(); ++Edge)
	{
		const FHFBeam* Beam = ShowingBeamPerEdge[Edge];
		if (Beam == nullptr || Beam->Depth <= Ceiling.Drop)
		{
			continue;
		}

		Ceiling.PerimeterBulkheadEdges.Add(Edge);

		// One depth and one width for the ring, taken from the worst edge, so the level change is
		// a level change rather than a staircase of near-misses round the room.
		Ceiling.PerimeterBulkheadDrop = FMath::Max(Ceiling.PerimeterBulkheadDrop,
			Beam->Depth + Defaults.BeamBulkheadClearance * S);
		Ceiling.PerimeterBulkheadWidth = FMath::Max(Ceiling.PerimeterBulkheadWidth,
			FMath::Max(Defaults.MinBeamBulkheadWidth * S,
				Beam->Width * 0.5 + Defaults.BeamBulkheadShoulder * S));
	}

	// ------------------------------------------------------------- and then: does the room fit it
	//
	// THE BAND HAS TO HAVE A MIDDLE TO BE AROUND. Stamped without asking, 450 of band and 300 of
	// ring took 750 off each side of the 1800 square foyer and left 300 - a 310 mm square shaft
	// punched clean through the plaster and open to the slab. The band is narrowed until the
	// opening it frames is at least twice its own width, and if even MinBandWidth will not fit,
	// the treatment is abandoned for a flat soffit, which is what a room that size gets on site.
	TArray<TArray<FVector2D>> StyledLoops = { Outline };
	if (Ceiling.HasPerimeterBulkhead())
	{
		StyledLoops = FHFMeshOps::SubtractPolygons(Outline,
			Ceiling.BulkheadStrips(Outline, Ceiling.PerimeterBulkheadWidth));
	}

	if (Ceiling.BandWidth > 0.0)
	{
		const double MinBand = Defaults.MinBandWidth * S;
		const double Factor = FMath::Max(Defaults.MinOpenCentreFactor, 0.0);

		double Fitted = 0.0;
		constexpr int32 Steps = 8;

		for (int32 Step = 0; Step <= Steps; ++Step)
		{
			const double Candidate = FMath::Lerp(Ceiling.BandWidth, MinBand,
				static_cast<double>(Step) / Steps);

			if (Candidate < MinBand)
			{
				break;
			}

			double Open = 0.0;
			for (const TArray<FVector2D>& Loop : StyledLoops)
			{
				Open = FMath::Max(Open, NarrowestExtent(FHFMeshOps::InsetPolygon(Loop, Candidate)));
			}

			if (Open >= Candidate * Factor)
			{
				Fitted = Candidate;
				break;
			}
		}

		if (Fitted > 0.0)
		{
			Ceiling.BandWidth = Fitted;
		}
		else
		{
			// No band this room can carry. A flat soffit inside whatever ring there is - honest,
			// and never a hole.
			Ceiling.Style = EHFCeilingStyle::FullDrop;
			Ceiling.BandWidth = 0.0;
			Ceiling.InnerDrop = 0.0;
			Ceiling.CentrePanelDrop = 0.0;
			Ceiling.Cove.bHasLedStrip = false;
		}
	}

	// ------------------------------------------------------------------------------ downlights
	//
	// Placed INSIDE the ring, which is why this comes last. A run laid out on the room boundary
	// would put half of it in the deep ring, where the band it is meant to light does not exist.
	Ceiling.LightPositions.Reset();
	for (const TArray<FVector2D>& Loop : StyledLoops)
	{
		Ceiling.LightPositions.Append(
			PlaceDownlights(Loop, Defaults.DownlightSetback * S, Defaults.DownlightSpacing * S));
	}
}

void FHFCeilingTemplates::Apply(FHFHouseSpec& Spec, const FHFCeilingDefaults& Defaults)
{
	const double ToCentimetres = FHFUnits::ToCentimeterScale(Spec.Units);
	const double UnitScale = (ToCentimetres > UE_KINDA_SMALL_NUMBER) ? (1.0 / ToCentimetres) : 1.0;

	for (FHFFalseCeiling& Ceiling : Spec.FalseCeilings)
	{
		if (Ceiling.Template == EHFCeilingTemplate::Custom)
		{
			continue;
		}

		const FHFRoom* Room = Spec.FindRoom(Ceiling.RoomId);
		if (Room == nullptr)
		{
			UE_LOG(LogHouseForge, Warning,
				TEXT("False ceiling '%s' names a template but no room '%s'; its figures are left as authored."),
				*Ceiling.Id.ToString(), *Ceiling.RoomId.ToString());
			continue;
		}

		// One question per edge of the room, because that is the grain the answer has.
		//
		// Only for a ceiling set out on the room itself: a Bulkhead following its own polygon has
		// edges that are not the room's, and it IS the local box a beam wants - putting a ring
		// round a bulkhead would box in a box.
		TArray<const FHFBeam*> ShowingBeamPerEdge;
		if (Ceiling.ExplicitPolygon.Num() < 3)
		{
			ShowingBeamPerEdge.Reserve(Room->Boundary.Num());
			for (int32 Edge = 0; Edge < Room->Boundary.Num(); ++Edge)
			{
				ShowingBeamPerEdge.Add(Spec.DeepestBeamOnRoomEdge(Ceiling.RoomId, Edge));
			}
		}

		Apply(Ceiling, *Room, ShowingBeamPerEdge, Defaults, UnitScale);
	}
}
