// Copyright Siddartha G. All Rights Reserved.

#include "Actors/HFCurtainActor.h"

#include "Model/HFBuildDefaults.h"

using namespace UE::Geometry;

void AHFCurtainActor::ApplyProjectDefaults()
{
	// Nothing. A curtain is cloth off a roll: its fullness, its heading and its hem are choices about
	// the soft furnishing rather than construction figures the flat shares with its joinery. The one
	// project figure anywhere near it is the pelmet's board, and the pelmet already reads that.
}

FHFCurtainParams AHFCurtainActor::ParamsFor(const FHFFixture& Fixture)
{
	FHFCurtainParams P;

	// The drawn box, which is all there is until ApplyPelmet arrives with the real track.
	P.TrackWidth = Fixture.Footprint.X;
	P.Drop = FMath::Max(Fixture.Height, 1.0);

	// PAIR OR SINGLE IS A FUNCTION OF WIDTH, and the threshold is where a designer's is. Under about
	// 1.2 m a pair gives two 600 leaves that stack to nothing useful and leave a seam down the middle
	// of a small window; over it, a single leaf's stack is wider than the pier beside the opening.
	// Read off the drawing where the drawing says - see the shutter count on a wardrobe - and derived
	// here when it does not, which is every curtain in this reference set.
	P.Draw = (Fixture.Footprint.X >= 120.0) ? EHFCurtainDraw::Pair : EHFCurtainDraw::SingleStackLeft;

	P.Heading = EHFCurtainHeading::PinchPleat;
	P.FoldPitch = P.HeadingRepeat() * 0.58;

	return FHFCurtainKit::Sanitise(P);
}

void AHFCurtainActor::ApplyFixture(const FHFFixture& Fixture)
{
	Curtain = ParamsFor(Fixture);
}

void AHFCurtainActor::ApplyPelmet(const FHFPelmetParams& Pelmet)
{
	// THE TRACK, NOT THE BOX. A pelmet's drawn width includes its two end returns, and a curtain that
	// took the outside dimension would run its leading edge into 18 mm of ply at each end - which is
	// invisible closed and is a leaf jammed against a board every time it is drawn.
	Curtain.TrackWidth = Pelmet.ClearWidth();

	// And the depth its slot has left with the track in it. This is what stops the cloth standing
	// through the fascia; see FHFPelmetParams::ConcealedCurtainDepth for why it is twice the SMALLER
	// of the two gaps rather than the whole slot.
	Curtain.MaxFoldDepth = Pelmet.ConcealedCurtainDepth();

	Curtain = FHFCurtainKit::Sanitise(Curtain);
}

void AHFCurtainActor::ApplyDrop(double TrackToFloor, double FloorClearance)
{
	Curtain.Drop = FMath::Max(TrackToFloor - FMath::Max(FloorClearance, 0.0), 1.0);
	Curtain = FHFCurtainKit::Sanitise(Curtain);
}

bool AHFCurtainActor::DrawLeaf(int32 LeafIndex, double OpenAmount)
{
	if (LeafIndex < 0 || LeafIndex >= Curtain.LeafCount())
	{
		return false;
	}

	// The LEADING fold, because every other fold in the leaf is geared to it. Setting any other one
	// would be overwritten by the resolve, which is the point of the gearing.
	return SetPartOpenAmount(LeadFoldPartId(LeafIndex), OpenAmount);
}

FDynamicMesh3 AHFCurtainActor::BuildMesh() const
{
	// The anchor fold at each leaf's stack end: cloth that is already where a drawn curtain would put
	// it, so it does not move. See FHFCurtainBuild::Anchors.
	return FHFCurtainKit::Build(Curtain).Anchors;
}

void AHFCurtainActor::BuildParts(TArray<FHFMeshPart>& OutParts) const
{
	FHFCurtainBuild Built = FHFCurtainKit::Build(Curtain);
	OutParts.Append(MoveTemp(Built.Parts));
}
