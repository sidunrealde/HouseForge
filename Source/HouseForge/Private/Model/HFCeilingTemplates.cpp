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
	const FHFBeam* DeepestShowingBeam, const FHFCeilingDefaults& Defaults, double UnitScale)
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
	Ceiling.PerimeterBulkheadWidth = 0.0;
	Ceiling.PerimeterBulkheadDrop = 0.0;

	if (DeepestShowingBeam != nullptr && DeepestShowingBeam->Depth > Ceiling.Drop)
	{
		Ceiling.PerimeterBulkheadDrop = DeepestShowingBeam->Depth + Defaults.BeamBulkheadClearance * S;
		Ceiling.PerimeterBulkheadWidth = FMath::Max(
			Defaults.MinBeamBulkheadWidth * S,
			DeepestShowingBeam->Width * 0.5 + Defaults.BeamBulkheadShoulder * S);
	}

	// ------------------------------------------------------------------------------ downlights
	//
	// Placed INSIDE the ring, which is why this comes last. A run laid out on the room boundary
	// would put half of it in the deep ring, where the band it is meant to light does not exist.
	const TArray<FVector2D>& Outline = (Ceiling.ExplicitPolygon.Num() >= 3)
		? Ceiling.ExplicitPolygon
		: Room.Boundary;

	TArray<TArray<FVector2D>> BandLoops = { Outline };
	if (Ceiling.HasPerimeterBulkhead())
	{
		BandLoops = FHFMeshOps::InsetPolygon(Outline, Ceiling.PerimeterBulkheadWidth);
	}

	Ceiling.LightPositions.Reset();
	for (const TArray<FVector2D>& Loop : BandLoops)
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

		Apply(Ceiling, *Room, Spec.DeepestBeamOverRoom(Ceiling.RoomId), Defaults, UnitScale);
	}
}
