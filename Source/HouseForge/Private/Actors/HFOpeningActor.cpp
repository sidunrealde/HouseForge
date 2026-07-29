// Copyright Siddartha G. All Rights Reserved.

#include "Actors/HFOpeningActor.h"

#include "Geometry/HFGenerators.h"

using namespace UE::Geometry;

const FName AHFOpeningActor::LeafPartId(TEXT("Leaf"));
const FName AHFOpeningActor::FixedPanelPartId(TEXT("PanelFixed"));

FDynamicMesh3 AHFOpeningActor::BuildMesh() const
{
	return FHFGenerators::GenerateOpeningFixedInfill(Opening, HostWall);
}

void AHFOpeningActor::BuildParts(TArray<FHFMeshPart>& OutParts) const
{
	FHFGenerators::BuildOpeningParts(Opening, HostWall, OutParts);
}
