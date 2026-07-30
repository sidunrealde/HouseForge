// Copyright Siddartha G. All Rights Reserved.

#include "Actors/HFOpeningActor.h"

#include "Geometry/HFGenerators.h"
#include "Model/HFBuildDefaults.h"

using namespace UE::Geometry;

const FName AHFOpeningActor::LeafPartId(TEXT("Leaf"));
const FName AHFOpeningActor::FixedPanelPartId(TEXT("PanelFixed"));
const FName AHFOpeningActor::SashPartId(TEXT("Sash"));
const FName AHFOpeningActor::FixedSashPartId(TEXT("SashFixed"));

void AHFOpeningActor::ApplyProjectDefaults()
{
	// The composing layer's job, and the only line in this file that knows a settings object could
	// exist. By the time either generator below runs, everything it needs is already on the actor.
	BuildParams = FHFBuildDefaults::FromProjectSettings().Opening;
}

FDynamicMesh3 AHFOpeningActor::BuildMesh() const
{
	return FHFGenerators::GenerateOpeningFixedInfill(Opening, HostWall, BuildParams);
}

void AHFOpeningActor::BuildParts(TArray<FHFMeshPart>& OutParts) const
{
	FHFGenerators::BuildOpeningParts(Opening, HostWall, OutParts, BuildParams);
}
