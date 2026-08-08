// Copyright Siddartha G. All Rights Reserved.

#include "Actors/HFElementActors.h"
#include "Actors/HFHouseActor.h"
#include "Components/DynamicMeshComponent.h"
#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Geometry/HFMeshOps.h"
#include "HFEditorSubsystem.h"
#include "HouseForgeEditor.h"
#include "Materials/HFMaterialLibrary.h"
#include "ScopedTransaction.h"
#include "UDynamicMesh.h"

#define LOCTEXT_NAMESPACE "HouseForgeEditor"

namespace
{
	/**
	 * Every dynamic mesh component a HouseForge actor renders through, moving parts included.
	 *
	 * GetComponents rather than GetMeshComponent, because an articulated actor keeps each moving
	 * part in its own component - a door leaf, a drawer box, a wardrobe shutter. Counting only the
	 * root would under-report every fixture in the flat, and would report a wardrobe as carrying no
	 * shutter laminate at all - which is exactly the surface somebody opening this panel wants.
	 */
	void GatherMeshComponents(const AActor* Actor, TArray<UDynamicMeshComponent*>& Out)
	{
		Out.Reset();
		if (IsValid(Actor))
		{
			Actor->GetComponents<UDynamicMeshComponent>(Out);
		}
	}
}

UHFMaterialLibrary* UHFEditorSubsystem::GetMaterialLibrary() const
{
	return UHFMaterialLibrary::Get();
}

FHFOperationResult UHFEditorSubsystem::GetEditableMaterialLibrary(UHFMaterialLibrary*& OutLibrary) const
{
	OutLibrary = UHFMaterialLibrary::Get();

	if (OutLibrary == nullptr)
	{
		return FHFOperationResult::Fail(TEXT("There is no HouseForge material library."));
	}

	// A class default object has no package to dirty and no asset to save, so an edit would render
	// for the session and be gone on restart with nothing having said so. Refused by name, with the
	// path, because the fix is to restore the plugin content rather than anything done in a panel.
	if (OutLibrary->HasAnyFlags(RF_ClassDefaultObject))
	{
		OutLibrary = nullptr;
		return FHFOperationResult::Fail(FString::Printf(
			TEXT("The material library asset is missing, so finishes are running from the compiled-in "
				 "defaults and cannot be edited. Expected it at '%s'. Surfaces still render correctly; "
				 "restoring the plugin Content folder makes them editable again."),
			*UHFMaterialLibrary::ShippedAssetPath()));
	}

	return FHFOperationResult::Ok(FString::Printf(TEXT("Editing '%s'."), *OutLibrary->GetName()));
}

FHFOperationResult UHFEditorSubsystem::GetSurfaceFinish(EHFSurfaceRole Role,
	FHFSurfaceFinish& OutFinish) const
{
	const UHFMaterialLibrary* Library = UHFMaterialLibrary::Get();
	if (Library == nullptr)
	{
		return FHFOperationResult::Fail(TEXT("There is no HouseForge material library."));
	}

	OutFinish = Library->FinishForRole(Role);
	return FHFOperationResult::Ok(FString::Printf(TEXT("Read the %s finish."),
		*StaticEnum<EHFSurfaceRole>()->GetNameStringByValue(static_cast<int64>(Role))));
}

FHFOperationResult UHFEditorSubsystem::SetSurfaceFinish(EHFSurfaceRole Role,
	const FHFSurfaceFinish& Finish, EHFMaterialPush Mode)
{
	UHFMaterialLibrary* Library = nullptr;
	const FHFOperationResult Editable = GetEditableMaterialLibrary(Library);
	if (!Editable.bSuccess)
	{
		return Editable;
	}

	const FString RoleName = StaticEnum<EHFSurfaceRole>()->GetNameStringByValue(static_cast<int64>(Role));

	if (Mode == EHFMaterialPush::Interactive)
	{
		// MID-GESTURE. No transaction and no dirty flag: a drag is not a decision until it ends, and
		// one transaction per mouse-move fills the undo buffer with a hundred steps nobody wants to
		// walk back through a frame at a time. The library is still written, so a drag abandoned by
		// clicking elsewhere leaves the library saying what the viewport is showing.
		Library->Finishes.Add(Role, Finish);
		Library->PushFinish(Role, EHFMaterialPush::Interactive);
		return FHFOperationResult::Ok(FString::Printf(TEXT("Previewing the %s finish."), *RoleName));
	}

	{
		// THE DECISION. Transacted so it undoes as one step, and Modify before the write so the
		// transaction captures the finish that was there rather than the one replacing it.
		const FScopedTransaction Transaction(
			FText::Format(LOCTEXT("SetSurfaceFinish", "Change the {0} finish"), FText::FromString(RoleName)));

		Library->Modify();
		Library->Finishes.Add(Role, Finish);
	}

	// Outside the transaction on purpose. The push writes a second asset - the role's material
	// instance - but that asset is the library compiled for the renderer rather than a second record
	// of it, so it is reproduced by pushing again after an undo rather than rolled back to a value
	// no library entry explains.
	const bool bPushed = Library->PushFinish(Role, EHFMaterialPush::Commit);
	Library->MarkPackageDirty();

	if (!bPushed)
	{
		return FHFOperationResult::Ok(FString::Printf(
			TEXT("Saved the %s finish to '%s', but there is no MI_HF_%s material instance for it to "
				 "render through, so nothing in the level will change."),
			*RoleName, *Library->GetName(), *RoleName));
	}

	return FHFOperationResult::Ok(FString::Printf(TEXT("Changed the %s finish in '%s'."),
		*RoleName, *Library->GetName()));
}

FHFOperationResult UHFEditorSubsystem::ResetSurfaceFinish(EHFSurfaceRole Role)
{
	return SetSurfaceFinish(Role, UHFMaterialLibrary::DefaultFinishForRole(Role),
		EHFMaterialPush::Commit);
}

TArray<FHFSurfaceUsage> UHFEditorSubsystem::GetSurfaceUsage() const
{
	const int32 RoleCount = FHFMeshOps::NumSurfaceRoles();

	TArray<FHFSurfaceUsage> Usage;
	Usage.SetNum(RoleCount);
	for (int32 Index = 0; Index < RoleCount; ++Index)
	{
		Usage[Index].Role = static_cast<EHFSurfaceRole>(Index);
	}

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (World == nullptr)
	{
		return Usage;
	}

	TArray<UDynamicMeshComponent*> Components;
	TBitArray<> Seen;

	// Every house in the level rather than the first. A level holding two houses would otherwise
	// report the second one as covering nothing, and the row would read "not in this level" about
	// surfaces plainly on screen.
	for (TActorIterator<AHFHouseActor> HouseIt(World); HouseIt; ++HouseIt)
	{
		for (AActor* Element : HouseIt->ElementActors)
		{
			const AHFElementActor* Typed = Cast<AHFElementActor>(Element);
			if (!IsValid(Typed))
			{
				continue;
			}

			// Per element, so an element covering a role in twenty triangles counts once.
			Seen.Init(false, RoleCount);

			GatherMeshComponents(Typed, Components);
			// Not a const pointer: UDynamicMeshComponent::GetDynamicMesh is non-const, though what it
			// hands back is only ever read here - ProcessMesh takes the mesh by const reference.
			for (UDynamicMeshComponent* Component : Components)
			{
				const UDynamicMesh* Wrapper = Component ? Component->GetDynamicMesh() : nullptr;
				if (Wrapper == nullptr)
				{
					continue;
				}

				Wrapper->ProcessMesh([&Usage, &Seen, RoleCount](const UE::Geometry::FDynamicMesh3& Mesh)
				{
					const UE::Geometry::FDynamicMeshMaterialAttribute* MaterialIds =
						Mesh.HasAttributes() ? Mesh.Attributes()->GetMaterialID() : nullptr;

					for (const int32 TriangleId : Mesh.TriangleIndicesItr())
					{
						// Untagged geometry falls to slot 0 exactly the way the renderer resolves it,
						// rather than being dropped from the tally. A surface rendering the wrong
						// finish should show up as area on the role it is actually rendering, which
						// is what makes this figure usable for finding one.
						const int32 MaterialId = MaterialIds ? MaterialIds->GetValue(TriangleId) : 0;
						const int32 RoleIndex =
							static_cast<int32>(FHFMeshOps::RoleForMaterialId(MaterialId));

						if (RoleIndex < 0 || RoleIndex >= RoleCount)
						{
							continue;
						}

						++Usage[RoleIndex].TriangleCount;

						// Centimetres squared to metres squared. This project converts millimetres to
						// centimetres exactly once, at spec ingest, and has been bitten at that
						// boundary - so the divisor is spelled out at the site it happens rather than
						// folded into a constant: cm2 over 10,000 cm2 per m2.
						Usage[RoleIndex].AreaSquareMetres += Mesh.GetTriArea(TriangleId) / 10000.0;

						Seen[RoleIndex] = true;
					}
				});
			}

			for (int32 Index = 0; Index < RoleCount; ++Index)
			{
				if (!Seen[Index])
				{
					continue;
				}

				++Usage[Index].ElementCount;
				Usage[Index].ArtistEditedElementCount += Typed->bArtistEdited ? 1 : 0;
			}
		}
	}

	return Usage;
}

FHFOperationResult UHFEditorSubsystem::ReapplyMaterialsToLevel(int32& OutComponents)
{
	OutComponents = 0;

	const UHFMaterialLibrary* Library = UHFMaterialLibrary::Get();
	if (Library == nullptr)
	{
		return FHFOperationResult::Fail(TEXT("There is no HouseForge material library."));
	}

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (World == nullptr)
	{
		return FHFOperationResult::Fail(TEXT("There is no open level to re-apply materials to."));
	}

	int32 Elements = 0;
	TArray<UDynamicMeshComponent*> Components;

	for (TActorIterator<AHFHouseActor> HouseIt(World); HouseIt; ++HouseIt)
	{
		for (AActor* Element : HouseIt->ElementActors)
		{
			AHFElementActor* Typed = Cast<AHFElementActor>(Element);
			if (!IsValid(Typed))
			{
				continue;
			}

			// Hand-edited elements included, deliberately. ApplyTo fills the component slot table and
			// never reads or writes FDynamicMesh3, so there is nothing here for a hand edit to lose -
			// and skipping them would strand exactly the elements somebody has spent time on with a
			// stale set of materials.
			++Elements;
			GatherMeshComponents(Typed, Components);
			for (UDynamicMeshComponent* Component : Components)
			{
				Library->ApplyTo(Component);
				++OutComponents;
			}
		}
	}

	if (Elements == 0)
	{
		return FHFOperationResult::Ok(
			TEXT("There is no house in this level, so there was nothing to re-apply materials to. "
				 "Finishes are still editable - they live in the library asset, not in the level."));
	}

	return FHFOperationResult::Ok(FString::Printf(
		TEXT("Re-applied the material set to %d component(s) across %d element(s)."),
		OutComponents, Elements));
}

#undef LOCTEXT_NAMESPACE
