// Copyright Siddartha G. All Rights Reserved.

#include "Bake/HFBakeService.h"

#include "Actors/HFBakeTypes.h"
#include "Actors/HFElementActors.h"
#include "Actors/HFHouseActor.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Components/DynamicMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "AssetUtils/CreateStaticMeshUtil.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMeshToMeshDescription.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "FileHelpers.h"
#include "Geometry/HFMeshOps.h"
#include "HouseForgeEditor.h"
#include "Materials/HFMaterialLibrary.h"
#include "MeshDescription.h"
#include "Misc/PackageName.h"
#include "ObjectTools.h"
#include "StaticMeshAttributes.h"
#include "StaticMeshCompiler.h"
#include "UObject/Package.h"

using namespace UE::Geometry;

#define LOCTEXT_NAMESPACE "HFBakeService"

namespace
{
	/** /Game/HouseForge/Baked is where generated USER OUTPUT goes. Never plugin content - rule 01. */
	const TCHAR* GBakedRoot = TEXT("/Game/HouseForge/Baked");

	/** "AHFWallActor" -> "Wall". Short, readable, and unique enough to keep two categories apart. */
	FString KindTagFor(const AHFElementActor* Element)
	{
		if (Element == nullptr)
		{
			return TEXT("Element");
		}

		FString Name = Element->GetClass()->GetName();
		Name.RemoveFromStart(TEXT("HF"));
		Name.RemoveFromEnd(TEXT("Actor"));
		return Name.IsEmpty() ? TEXT("Element") : Name;
	}

	/** Anything that is not a legal object-name character becomes an underscore. */
	FString Sanitise(const FString& In)
	{
		FString Out;
		Out.Reserve(In.Len());
		for (const TCHAR Char : In)
		{
			Out.AppendChar(FChar::IsAlnum(Char) ? Char : TEXT('_'));
		}
		return Out;
	}

	/** The house that owns an element, by ownership first and by level scan second. */
	AHFHouseActor* HouseFor(AHFElementActor* Element)
	{
		if (Element == nullptr)
		{
			return nullptr;
		}

		if (AHFHouseActor* Owner = Cast<AHFHouseActor>(Element->GetOwner()))
		{
			return Owner;
		}

		// An element dropped into a level by hand, or one whose owner did not survive a reload. It
		// still has to be bakeable; it just lands in the level's folder without a house to ask.
		UWorld* World = Element->GetWorld();
		if (World != nullptr)
		{
			for (TActorIterator<AHFHouseActor> It(World); It; ++It)
			{
				return *It;
			}
		}
		return nullptr;
	}

	FName LevelPackageNameOf(UWorld* World)
	{
		if (World == nullptr || World->GetOutermost() == nullptr)
		{
			return NAME_None;
		}
		return World->GetOutermost()->GetFName();
	}

	/**
	 * Writes the provenance stamp, replacing any previous one.
	 *
	 * Unconditional after every create OR update, because a re-create re-runs the asset's constructor
	 * and resets its AssetUserData array - measured, not assumed. Without re-applying it, the second
	 * bake of an element silently un-stamps its own asset and the orphan scan stops recognising it.
	 */
	void StampAsset(UStaticMesh* Asset, AHFElementActor* Element, FName SourceComponentName, FName LevelPackage)
	{
		if (Asset == nullptr || Element == nullptr)
		{
			return;
		}

		Asset->RemoveUserDataOfClass(UHFBakedMeshUserData::StaticClass());

		UHFBakedMeshUserData* Stamp = NewObject<UHFBakedMeshUserData>(Asset);
		Stamp->OwnerGuid = Element->EnsureBakeOwnerGuid();
		Stamp->ElementId = Element->ElementId;
		Stamp->ElementClassName = Element->GetClass()->GetFName();
		Stamp->SourceComponentName = SourceComponentName;
		Stamp->LevelPackageName = LevelPackage;
		Stamp->SourceMeshRevision = Element->MeshRevision;
		Stamp->BakedAtUtc = FDateTime::UtcNow();
		Asset->AddAssetUserData(Stamp);
	}
}

FString FHFBakeReport::Summary() const
{
	FString Text = FString::Printf(
		TEXT("%d element(s) baked (%d part(s)), %d unbaked, %d skipped, %d failed."),
		ElementsBaked, PartsBaked, ElementsUnbaked, ElementsSkipped, ElementsFailed);

	if (!Orphaned.IsEmpty())
	{
		Text += FString::Printf(TEXT(" %d baked asset(s) are now unclaimed."), Orphaned.Num());
	}

	for (const FString& Line : Messages)
	{
		Text += TEXT("\n  ") + Line;
	}
	return Text;
}

// ================================================================================ registration

void FHFBakeService::Register()
{
	// Bound rather than called: the runtime actors reach asset creation through this and nothing
	// else, so a cooked build simply cannot bake instead of failing to link.
	FHFBakeHooks::BakeElement.BindLambda([](AHFElementActor* Element, FString& OutError) -> bool
	{
		FHFBakeReport Report;
		const bool bOk = FHFBakeService::BakeElement(Element, Report);
		if (!bOk)
		{
			OutError = Report.Messages.IsEmpty() ? TEXT("unknown error") : Report.Messages[0];
		}
		return bOk;
	});
}

void FHFBakeService::Unregister()
{
	FHFBakeHooks::BakeElement.Unbind();
}

// ======================================================================================= paths

FString FHFBakeService::ResolveBakedAssetFolder(AHFHouseActor* House, UWorld* World)
{
	if (House != nullptr && !House->BakedAssetFolder.IsEmpty())
	{
		return House->BakedAssetFolder;
	}

	FString LevelName = TEXT("Untitled");
	if (World != nullptr && World->GetOutermost() != nullptr)
	{
		LevelName = FPackageName::GetShortName(World->GetOutermost()->GetFName());
	}

	const FString Folder = FString::Printf(TEXT("%s/%s"), GBakedRoot, *Sanitise(LevelName));

	if (House != nullptr)
	{
		// WRITTEN BACK on first use. Recomputing it every time would scatter a flat's assets across
		// two folders the moment somebody renamed the level halfway through baking it, and the orphan
		// scan would then report the first half as unclaimed.
		House->BakedAssetFolder = Folder;
	}

	return Folder;
}

FString FHFBakeService::AssetNameFor(const AHFElementActor* Element, int32 PartIndex)
{
	const FString Id = (Element != nullptr && !Element->ElementId.IsNone())
		? Element->ElementId.ToString()
		: (Element != nullptr ? Element->GetName() : TEXT("Unnamed"));

	// The KIND is in the name as well as the id, because ids are unique within a category and not
	// across them: a wall and a room may both be called W1, and two elements sharing an asset path
	// would silently overwrite each other's geometry on every bake.
	FString Name = FString::Printf(TEXT("SM_%s_%s"), *KindTagFor(Element), *Sanitise(Id));

	if (PartIndex > 0)
	{
		Name += FString::Printf(TEXT("_p%d"), PartIndex);
	}
	return Name;
}

// ==================================================================================== one part

UStaticMesh* FHFBakeService::BakeOnePart(AHFElementActor* Element, int32 PartIndex, const FString& Folder,
	FString& OutError, bool& bOutSourceWasEmpty)
{
	bOutSourceWasEmpty = false;

	TArray<UDynamicMeshComponent*> Sources;
	Element->GetBakeSourceComponents(Sources);
	if (!Sources.IsValidIndex(PartIndex) || !IsValid(Sources[PartIndex]))
	{
		OutError = TEXT("the source component went away mid-bake");
		return nullptr;
	}

	UDynamicMeshComponent* Source = Sources[PartIndex];

	// COPIED, NOT MOVED, and this is the whole safety property of the feature. ProcessMesh hands out
	// a const reference; the asset is built from a copy of it and the FDynamicMesh3 on the component
	// is never touched. That is why unbake is exact and why bake on a hand-edited element is safe.
	FDynamicMesh3 Copy;
	Source->GetDynamicMesh()->ProcessMesh([&Copy](const FDynamicMesh3& Mesh) { Copy = Mesh; });

	if (Copy.TriangleCount() == 0)
	{
		// Not a failure. See FHFBakedPart::bSourceWasEmpty.
		bOutSourceWasEmpty = true;
		return nullptr;
	}

	const FString AssetPath = Folder / AssetNameFor(Element, PartIndex);

	// An existing asset is UPDATED rather than re-created. A re-create reuses the object in place -
	// measured - but re-runs its constructor over a live asset, which resets AssetUserData and so
	// wipes the provenance stamp everything downstream depends on.
	UStaticMesh* Existing = LoadObject<UStaticMesh>(nullptr, *AssetPath, nullptr, LOAD_NoWarn | LOAD_Quiet);

	if (Existing != nullptr)
	{
		FStaticMeshCompilingManager::Get().FinishCompilation({ Existing });

		FMeshDescription* Description = Existing->GetMeshDescription(0);
		if (Description == nullptr)
		{
			Description = Existing->CreateMeshDescription(0);
		}

		if (Description == nullptr)
		{
			OutError = FString::Printf(TEXT("could not open a mesh description on '%s'"), *AssetPath);
			return nullptr;
		}

		Existing->Modify();

		*Description = FMeshDescription();
		FStaticMeshAttributes(*Description).Register();

		FDynamicMeshToMeshDescription Converter;
		Converter.Convert(&Copy, *Description, /*bCopyTangents*/ true);

		// Slots have to be re-asserted on the update path too: the material slot table is what makes
		// section index mean role index, and an asset created by an older build - or trimmed by a
		// user - would silently lose the roles above its slot count.
		while (Existing->GetStaticMaterials().Num() < FHFMeshOps::NumSurfaceRoles())
		{
			Existing->GetStaticMaterials().Add(FStaticMaterial());
		}

		Existing->CommitMeshDescription(0);
		Existing->SetLightMapCoordinateIndex(1);
		Existing->Build(/*bInSilent*/ true);
		Existing->PostEditChange();
		Existing->MarkPackageDirty();

		FStaticMeshCompilingManager::Get().FinishCompilation({ Existing });
		return Existing;
	}

	UE::AssetUtils::FStaticMeshAssetOptions Options;
	Options.NewAssetPath = AssetPath;
	Options.NumSourceModels = 1;

	// ONE SLOT PER SURFACE ROLE, so slot index means role index on a baked element exactly as it does
	// on a live one, and the material panel can still reach it. Without this the material system
	// loses every baked element - which is the failure that would make a fully baked flat
	// unmaterialable.
	Options.NumMaterialSlots = FHFMeshOps::NumSurfaceRoles();

	// See the class comment. Off, deliberately: it would unwrap the world-scale tiling UV0 over the
	// packed lightmap channel milestone 10 already generated into UV1.
	Options.bGenerateLightmapUVs = false;

	// Left at their defaults, and named here because turning either off would quietly break Lumen:
	// bAllowDistanceField false sets DistanceFieldResolutionScale to 0, and the mesh card build is
	// chained off the distance field build, so no DF means no surface cache means no radiance.
	Options.bAllowDistanceField = true;
	Options.bSupportRayTracing = true;

	Options.bCreatePhysicsBody = true;
	Options.CollisionType = ECollisionTraceFlag::CTF_UseComplexAsSimple;
	Options.SourceMeshes.DynamicMeshes.Add(&Copy);

	UE::AssetUtils::FStaticMeshResults Results;
	const UE::AssetUtils::ECreateStaticMeshResult Code = UE::AssetUtils::CreateStaticMeshAsset(Options, Results);

	if (Code != UE::AssetUtils::ECreateStaticMeshResult::Ok || Results.StaticMesh == nullptr)
	{
		OutError = FString::Printf(TEXT("could not create '%s'"), *AssetPath);
		return nullptr;
	}

	UStaticMesh* Asset = Results.StaticMesh;
	Asset->SetLightMapCoordinateIndex(1);

	// The asset is not queryable until the build finishes: UStaticMesh implements
	// IInterface_AsyncCompilation, and until FinishCompilation returns GetPhysicsTriMeshData yields
	// nothing and UStaticMeshToolTarget::HasNonGeneratedLOD early-outs false. Measured in
	// HouseForge.Bake.Probe.CollisionAndCompilation. Without this an element is briefly baked,
	// collisionless and untargetable, which is a walkthrough falling through a wall.
	FStaticMeshCompilingManager::Get().FinishCompilation({ Asset });

	FAssetRegistryModule::AssetCreated(Asset);
	Asset->MarkPackageDirty();

	return Asset;
}

// ================================================================================= one element

bool FHFBakeService::BakeElement(AHFElementActor* Element, FHFBakeReport& Report)
{
	if (!IsValid(Element))
	{
		Report.Messages.Add(TEXT("A bake was asked for on an element that no longer exists."));
		++Report.ElementsFailed;
		return false;
	}

	UWorld* World = Element->GetWorld();
	AHFHouseActor* House = HouseFor(Element);
	const FString Folder = ResolveBakedAssetFolder(House, World);
	const FName LevelPackage = LevelPackageNameOf(World);

	// NEVER Regenerate() FIRST. Bake bakes what is on screen. A hand-edited wall bakes its sculpted
	// form and unbakes back to that same sculpted form; regenerating first would throw the sculpt
	// away in the name of making the asset "correct", which is the exact loss rule 04 forbids.

	// The source list is authoritative and may have changed since the last bake - a fixture that
	// dropped a drawer, for instance. Reconciling first is what stops a stale part index writing an
	// asset for a component that is not there.
	Element->SyncBakedPartsToSources(&Report.Orphaned);

	TArray<UDynamicMeshComponent*> Sources;
	Element->GetBakeSourceComponents(Sources);

	if (Sources.IsEmpty())
	{
		Report.Messages.Add(FString::Printf(TEXT("'%s' has no geometry components to bake."), *Element->GetName()));
		++Report.ElementsSkipped;
		return false;
	}

	const int32 RevisionAtBake = Element->MeshRevision;
	int32 Failures = 0;
	int32 Parts = 0;

	Element->Modify();

	for (int32 Index = 0; Index < Sources.Num(); ++Index)
	{
		FString Error;
		bool bEmpty = false;
		UStaticMesh* Asset = BakeOnePart(Element, Index, Folder, Error, bEmpty);

		if (Asset == nullptr && !bEmpty)
		{
			Report.Messages.Add(FString::Printf(TEXT("'%s' part %d: %s"), *Element->GetName(), Index, *Error));
			++Failures;
			continue;
		}

		const FName SourceName = (Index == 0) ? NAME_None : Sources[Index]->GetFName();
		StampAsset(Asset, Element, SourceName, LevelPackage);
		Element->AdoptBakedMesh(Index, SourceName, Asset, RevisionAtBake, bEmpty);

		if (Asset != nullptr)
		{
			++Parts;
		}
	}

	if (Failures > 0)
	{
		// PARTIAL BAKES DO NOT SWITCH. HasAllBakedAssets is false, so a switch to Baked would fall
		// straight back to Dynamic anyway - but saying so here is what turns "the wardrobe looks
		// wrong" into "three of its shutters could not be written".
		Report.Messages.Add(FString::Printf(
			TEXT("'%s' was not switched to baked geometry: %d of %d parts could not be written."),
			*Element->GetName(), Failures, Sources.Num()));
		++Report.ElementsFailed;
		Element->SetRenderMode(EHFRenderMode::Dynamic);
		return false;
	}

	Element->SetRenderMode(EHFRenderMode::Baked);

	++Report.ElementsBaked;
	Report.PartsBaked += Parts;
	return true;
}

void FHFBakeService::UnbakeElement(AHFElementActor* Element, FHFBakeReport& Report)
{
	if (!IsValid(Element))
	{
		return;
	}

	// EVERY ASSET IS KEPT. Unbake is a pointer assignment on a component, nothing more. Re-baking
	// afterwards therefore costs nothing, and an accidental unbake costs nothing either - which is
	// why the switch needs no confirmation dialog.
	Element->Modify();
	Element->SetRenderMode(EHFRenderMode::Dynamic);
	++Report.ElementsUnbaked;
}

// ======================================================================================== bulk

void FHFBakeService::GatherElements(UWorld* World, TArray<AHFElementActor*>& OutElements)
{
	OutElements.Reset();
	if (World == nullptr)
	{
		return;
	}

	// Every element in the level, house-owned or not. An element dropped in by hand is still an
	// element, and a bulk bake that skipped it would leave one object in the flat invisible to Lumen
	// with nothing saying why.
	for (TActorIterator<AHFElementActor> It(World); It; ++It)
	{
		if (IsValid(*It))
		{
			OutElements.Add(*It);
		}
	}
}

void FHFBakeService::SetRenderModeMany(TArrayView<AHFElementActor* const> Elements, bool bBaked, FHFBakeReport& Report)
{
	for (AHFElementActor* Element : Elements)
	{
		if (!IsValid(Element))
		{
			continue;
		}

		if (!bBaked)
		{
			UnbakeElement(Element, Report);
			continue;
		}

		// Already baked and current: switch it on and move on. Re-writing an asset that is already
		// right is the difference between "Bake all" taking two seconds and taking two minutes.
		if (Element->HasAllBakedAssets() && !Element->IsBakeStale())
		{
			Element->Modify();
			Element->SetRenderMode(EHFRenderMode::Baked);
			++Report.ElementsBaked;
			continue;
		}

		BakeElement(Element, Report);
	}
}

void FHFBakeService::SetHouseRenderMode(UWorld* World, bool bBaked, FHFBakeReport& Report)
{
	TArray<AHFElementActor*> Elements;
	GatherElements(World, Elements);
	SetRenderModeMany(Elements, bBaked, Report);
}

void FHFBakeService::RebakeStale(UWorld* World, FHFBakeReport& Report)
{
	TArray<AHFElementActor*> Elements;
	GatherElements(World, Elements);

	for (AHFElementActor* Element : Elements)
	{
		if (IsValid(Element) && Element->HasAnyBakedAsset() && Element->IsBakeStale())
		{
			BakeElement(Element, Report);
		}
	}
}

void FHFBakeService::CountBakeState(UWorld* World, int32& OutBaked, int32& OutTotal, int32& OutStale)
{
	OutBaked = 0;
	OutTotal = 0;
	OutStale = 0;

	TArray<AHFElementActor*> Elements;
	GatherElements(World, Elements);

	for (const AHFElementActor* Element : Elements)
	{
		++OutTotal;
		if (Element->RenderMode == EHFRenderMode::Baked)
		{
			++OutBaked;
		}
		if (Element->HasAnyBakedAsset() && Element->IsBakeStale())
		{
			++OutStale;
		}
	}
}

// ===================================================================================== orphans

void FHFBakeService::FindOrphans(UWorld* World, TArray<FAssetData>& OutOrphans)
{
	OutOrphans.Reset();
	if (World == nullptr)
	{
		return;
	}

	const FName LevelPackage = LevelPackageNameOf(World);
	if (LevelPackage.IsNone())
	{
		return;
	}

	// Everything the level's elements still claim.
	TSet<FSoftObjectPath> Claimed;
	TArray<AHFElementActor*> Elements;
	GatherElements(World, Elements);

	for (const AHFElementActor* Element : Elements)
	{
		for (const FHFBakedPart& Part : Element->BakedParts)
		{
			if (Part.BakedAssetPath.IsValid())
			{
				Claimed.Add(Part.BakedAssetPath);
			}
		}
	}

	IAssetRegistry& Registry = FAssetRegistryModule::GetRegistry();

	TArray<FAssetData> Candidates;
	FARFilter Filter;
	Filter.ClassPaths.Add(UStaticMesh::StaticClass()->GetClassPathName());
	Filter.PackagePaths.Add(FName(*FString(GBakedRoot)));
	Filter.bRecursivePaths = true;
	Registry.GetAssets(Filter, Candidates);

	for (const FAssetData& Candidate : Candidates)
	{
		if (Claimed.Contains(Candidate.GetSoftObjectPath()))
		{
			continue;
		}

		// THE STAMP IS THE WHOLE OF THE SAFETY. An asset with no HouseForge stamp was not made by
		// HouseForge and is never a candidate; an asset stamped for a different level belongs to a
		// level that is not open, whose elements cannot be asked whether they still want it. Both are
		// left completely alone. A scan can only see the open level, so those are the only two answers
		// that can be given honestly.
		UStaticMesh* Asset = Cast<UStaticMesh>(Candidate.GetAsset());
		if (Asset == nullptr)
		{
			continue;
		}

		const UHFBakedMeshUserData* Stamp =
			Cast<UHFBakedMeshUserData>(Asset->GetAssetUserDataOfClass(UHFBakedMeshUserData::StaticClass()));

		if (Stamp == nullptr || Stamp->LevelPackageName != LevelPackage)
		{
			continue;
		}

		OutOrphans.Add(Candidate);
	}
}

int32 FHFBakeService::DeleteOrphans(const TArray<FAssetData>& Orphans, FString& OutError)
{
	if (Orphans.IsEmpty())
	{
		return 0;
	}

	// ObjectTools::DeleteAssets rather than a raw package delete: it is what the Content Browser
	// itself calls, so it checks references, prompts where a prompt is warranted, and does not leave
	// a half-deleted package behind. Deletion is not transactional and cannot be undone, which is why
	// the caller is required to have confirmed first.
	TArray<UObject*> Objects;
	Objects.Reserve(Orphans.Num());
	for (const FAssetData& Data : Orphans)
	{
		if (UObject* Object = Data.GetAsset())
		{
			Objects.Add(Object);
		}
	}

	const int32 Deleted = ObjectTools::DeleteObjects(Objects, /*bShowConfirmation*/ false);
	if (Deleted < Objects.Num())
	{
		OutError = FString::Printf(TEXT("%d of %d asset(s) could not be deleted; they may still be referenced."),
			Objects.Num() - Deleted, Objects.Num());
	}
	return Deleted;
}

#undef LOCTEXT_NAMESPACE
