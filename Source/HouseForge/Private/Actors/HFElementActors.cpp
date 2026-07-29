// Copyright Siddartha G. All Rights Reserved.

#include "Actors/HFElementActors.h"

#include "Components/DynamicMeshComponent.h"
#include "Geometry/HFGenerators.h"
#include "Geometry/HFMeshOps.h"
#include "HouseForge.h"

using namespace UE::Geometry;

AHFElementActor::AHFElementActor()
{
	PrimaryActorTick.bCanEverTick = false;

	Mesh = CreateDefaultSubobject<UDynamicMeshComponent>(TEXT("Mesh"));
	SetRootComponent(Mesh);

	// Complex collision only: these are thin boxed shapes, and simple collision would fill the
	// door openings back in - you could not walk through a doorway that had been cut out.
	//
	// Both flags, and they are not the same flag. CollisionType says which collision to use;
	// bEnableComplexCollision says whether to build any. Setting only the first asks for complex
	// collision that was never cooked, and a component with no simple shapes either then has no
	// collision at all - it renders correctly and a walkthrough falls straight through it.
	Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Mesh->SetCollisionProfileName(TEXT("BlockAll"));
	Mesh->CollisionType = ECollisionTraceFlag::CTF_UseComplexAsSimple;
	Mesh->bEnableComplexCollision = true;
	Mesh->SetGenerateOverlapEvents(false);

	// Tangents derived from the mesh's own UVs and normals rather than taken from it.
	//
	// The default is "From Dynamic Mesh", and nothing here ever calls EnableTangents, so
	// HasTangentSpace() is false and the component silently falls back to MakePerpVectors - an
	// arbitrary basis with no relationship to the surface's UVs. Nothing fails and nothing logs;
	// with the flat colours that exist today the output is indistinguishable from correct, and it
	// only becomes visible when the materials milestone puts normal maps on top of it, at which
	// point it reads as a material bug rather than a geometry-attribute one.
	Mesh->SetTangentsType(EDynamicMeshComponentTangentsMode::AutoCalculated);
}

void AHFElementActor::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	WatchForEdits();
}

void AHFElementActor::PostRegisterAllComponents()
{
	Super::PostRegisterAllComponents();

	// Edit detection has to be armed here, not only in PostInitializeComponents.
	//
	// AActor::PostActorConstruction gates PostInitializeComponents on World->AreActorsInitialized(),
	// which is false for an editor world - so that path runs in PIE only. Element actors are also
	// deliberately not regenerated on load, so CommitMesh does not run either. Between the two,
	// an element that came back from a saved level had no binding at all: take the Modeling Tools
	// to a wall, press Build Geometry, and the modelling work is gone without a word. That is the
	// silent, unrecoverable loss .claude/rules/04-conventions.md calls out.
	//
	// WatchForEdits is idempotent, so this sits safely alongside the CommitMesh path.
	WatchForEdits();
}

void AHFElementActor::WatchForEdits()
{
	if (bWatching || Mesh == nullptr)
	{
		return;
	}

	// These are dynamic meshes so an artist can take Unreal's Modeling Tools to them after
	// generation. Watching for changes is what makes that safe - anything that edits the mesh and
	// is not us marks the element as hand-edited, and it then opts out of regeneration.
	Mesh->OnMeshChanged.AddUObject(this, &AHFElementActor::HandleMeshChanged);
	bWatching = true;
}

void AHFElementActor::HandleMeshChanged()
{
	if (bGenerating || bArtistEdited)
	{
		return;
	}

	bArtistEdited = true;

	UE_LOG(LogHouseForge, Log,
		TEXT("'%s' was edited by hand; it will keep those edits and no longer regenerate. Use Revert To Generated to undo that."),
		*GetName());
}

void AHFElementActor::Regenerate()
{
	if (bArtistEdited)
	{
		// Silently refusing is deliberate. Rebuilding here would throw away modelling work, and
		// that loss tends to be noticed only long after it happened.
		UE_LOG(LogHouseForge, Verbose,
			TEXT("Skipping regeneration of '%s': it has been edited by hand."), *GetName());
		return;
	}

	CommitMesh(BuildMesh());
}

void AHFElementActor::RevertToGenerated()
{
	bArtistEdited = false;
	CommitMesh(BuildMesh());
}

void AHFElementActor::CommitMesh(FDynamicMesh3&& Generated)
{
	if (Mesh == nullptr)
	{
		return;
	}

	WatchForEdits();

	// Our own write must not look like an artist edit.
	TGuardValue<bool> Guard(bGenerating, true);

	Mesh->SetMesh(MoveTemp(Generated));
	Mesh->NotifyMeshUpdated();
	Mesh->UpdateCollision(false);
}

#if WITH_EDITOR
void AHFElementActor::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// Clearing the flag by hand is a deliberate request to go back to generated geometry.
	const FName Changed = PropertyChangedEvent.GetPropertyName();
	if (Changed == GET_MEMBER_NAME_CHECKED(AHFElementActor, bArtistEdited))
	{
		if (!bArtistEdited)
		{
			RevertToGenerated();
		}
		return;
	}

	// Editing any parameter rebuilds only this element, which is the point of one actor per
	// element rather than one mesh for the whole house.
	Regenerate();
}
#endif

FDynamicMesh3 AHFWallActor::BuildMesh() const
{
	return FHFGenerators::GenerateWall(Wall, Openings);
}

FDynamicMesh3 AHFRoomActor::BuildMesh() const
{
	FDynamicMesh3 Result = FHFGenerators::GenerateFloor(Room, SlabThickness, DoorwayCentres, DoorwayWidth);

	if (bGenerateCeilingSlab)
	{
		FHFMeshOps::AppendPreservingRoles(Result, FHFGenerators::GenerateCeilingSlab(Room, SlabThickness));
	}

	return Result;
}

FDynamicMesh3 AHFCeilingActor::BuildMesh() const
{
	return FHFGenerators::GenerateCeiling(Ceiling, Room, FanDrops, FanDropRadius);
}

FDynamicMesh3 AHFBeamActor::BuildMesh() const
{
	return FHFGenerators::GenerateBeam(Beam);
}

FDynamicMesh3 AHFColumnActor::BuildMesh() const
{
	return FHFGenerators::GenerateColumn(Column);
}
