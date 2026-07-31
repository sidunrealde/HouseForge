// Copyright Siddartha G. All Rights Reserved.

#include "Capture/HFPlanSection.h"

#include "Actors/HFElementActors.h"
#include "Actors/HFHouseActor.h"
#include "Components/DynamicMeshComponent.h"
#include "DynamicMesh/MeshTransforms.h"
#include "Engine/World.h"
#include "Geometry/HFMeshOps.h"
#include "Geometry/HFSectionCut.h"
#include "HouseForgeEditor.h"
#include "Materials/HFMaterialLibrary.h"

using namespace UE::Geometry;

namespace
{
	/**
	 * The role that covers most of a mesh's surface.
	 *
	 * What the cut faces of an element should be tagged as, worked out from the element rather
	 * than from a table of actor classes. A wall is mostly wall paint, a room is mostly floor
	 * finish, a wardrobe leaf is mostly shutter laminate - so the cut through each of them is
	 * tagged the way a reader would describe it, and a new element type gets a sensible answer
	 * without anyone remembering to extend a switch.
	 *
	 * By area, not by triangle count: a wall's two big faces are two triangles each, and its
	 * skirting is dozens.
	 */
	EHFSurfaceRole DominantRole(const FDynamicMesh3& Mesh)
	{
		TMap<int32, double> AreaByGroup;
		for (const int32 Tid : Mesh.TriangleIndicesItr())
		{
			FVector3d A, B, C;
			Mesh.GetTriVertices(Tid, A, B, C);
			AreaByGroup.FindOrAdd(Mesh.GetTriangleGroup(Tid)) += 0.5 * ((B - A).Cross(C - A)).Length();
		}

		int32 BestGroup = FHFMeshOps::GroupForRole(EHFSurfaceRole::WallPaint);
		double BestArea = -1.0;
		for (const TPair<int32, double>& Pair : AreaByGroup)
		{
			if (Pair.Value > BestArea)
			{
				BestArea = Pair.Value;
				BestGroup = Pair.Key;
			}
		}

		return FHFMeshOps::RoleForGroup(BestGroup);
	}
}

TArray<AActor*> FHFPlanSection::Build(UWorld* World, const AHFHouseActor* House, double CutZ, FBox& OutBounds)
{
	OutBounds = FBox(ForceInit);

	TArray<AActor*> Built;
	if (World == nullptr || House == nullptr)
	{
		return Built;
	}

	FActorSpawnParameters Params;
	Params.ObjectFlags = RF_Transient;
	Params.bHideFromSceneOutliner = true;
	Params.bTemporaryEditorActor = true;

	for (AActor* Element : House->ElementActors)
	{
		if (!IsValid(Element))
		{
			continue;
		}

		TArray<UDynamicMeshComponent*> Sources;
		Element->GetComponents<UDynamicMeshComponent>(Sources);
		if (Sources.IsEmpty())
		{
			continue;
		}

		AActor* SectionActor = nullptr;

		for (UDynamicMeshComponent* Source : Sources)
		{
			if (!IsValid(Source))
			{
				continue;
			}

			// Read a copy, and bring it into world space before cutting.
			//
			// Baking the transform in rather than transforming the cut plane into each component's
			// space is not laziness: a pivot ventilator's sash hangs on a HORIZONTAL hinge, so an
			// open one has a component transform with pitch in it, and in that component's own
			// space the world's horizontal plane is not horizontal at all. Working in world space
			// makes the plane the same plane for every element in the flat, whatever pose it is in.
			FDynamicMesh3 Working;
			Source->ProcessMesh([&Working](const FDynamicMesh3& Mesh) { Working = Mesh; });
			if (Working.TriangleCount() == 0)
			{
				continue;
			}

			FHFMeshOps::AdoptAttributes(Working);
			MeshTransforms::ApplyTransform(Working, FTransformSRT3d(Source->GetComponentTransform()),
				/*bReverseOrientationIfNeeded*/ true);

			FHFSectionCutParams CutParams;
			CutParams.CutZ = CutZ;
			CutParams.bCap = true;
			CutParams.CapRole = DominantRole(Working);

			FDynamicMesh3 Section = FHFSectionCut::CutBelow(Working, CutParams);
			if (Section.TriangleCount() == 0)
			{
				// Entirely above the cut - a loft, a bulkhead, a high-level cabinet. Correct to
				// have nothing left, and there is nothing to spawn for it.
				continue;
			}

			if (SectionActor == nullptr)
			{
				SectionActor = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, Params);
				if (SectionActor == nullptr)
				{
					continue;
				}
				SectionActor->SetFlags(RF_Transient);
				Built.Add(SectionActor);
			}

			UDynamicMeshComponent* Component = NewObject<UDynamicMeshComponent>(SectionActor, NAME_None, RF_Transient);
			if (SectionActor->GetRootComponent() == nullptr)
			{
				SectionActor->SetRootComponent(Component);
			}
			else
			{
				Component->SetupAttachment(SectionActor->GetRootComponent());
			}

			OutBounds += FBox(Section.GetBounds().Min, Section.GetBounds().Max);

			// No collision on any of this. It exists for one frame of one render; a physics body
			// per element would be the expensive half of the operation, and a world trace run
			// while a capture was in flight would be answered by geometry that is about to stop
			// existing - which is precisely the class of failure HFWalkthroughTests documents.
			Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Component->SetMesh(MoveTemp(Section));

			// The same placeholder materials the real geometry wears, so the plan is drawn in the
			// same colours as the model and a wall reads differently from the floor it stands on.
			FHFMaterialLibrary::ApplyPlaceholders(Component);

			Component->RegisterComponent();
			Component->SetWorldTransform(FTransform::Identity);
		}
	}

	UE_LOG(LogHouseForgeEditor, Verbose,
		TEXT("Plan section at z=%.1f produced %d transient actor(s)."), CutZ, Built.Num());

	return Built;
}

void FHFPlanSection::DestroyAll(UWorld* World, TArray<AActor*>& Actors)
{
	for (AActor* Actor : Actors)
	{
		if (IsValid(Actor) && World != nullptr)
		{
			World->DestroyActor(Actor);
		}
	}
	Actors.Reset();
}
