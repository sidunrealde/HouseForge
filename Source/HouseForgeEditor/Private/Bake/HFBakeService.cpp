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
#include "Editor.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "FileHelpers.h"
#include "Framework/Application/SlateApplication.h"
#include "Geometry/HFMeshOps.h"
#include "HouseForgeEditor.h"
#include "Materials/HFMaterialLibrary.h"
#include "MeshDescription.h"
#include "MeshDescriptionToDynamicMesh.h"
#include "Misc/PackageName.h"
#include "Misc/ScopedSlowTask.h"
#include "ObjectTools.h"
#include "PhysicsEngine/BodySetup.h"
#include "StaticMeshAttributes.h"
#include "StaticMeshCompiler.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

using namespace UE::Geometry;

#define LOCTEXT_NAMESPACE "HFBakeService"

namespace
{
	/** /Game/HouseForge/Baked is where generated USER OUTPUT goes. Never plugin content - rule 01. */
	const TCHAR* GBakedRoot = TEXT("/Game/HouseForge/Baked");

	/** Set only inside an FHFBakeSaveScope. See the comment on that struct. */
	bool GHasSaveOverride = false;
	bool GSaveOverride = false;

	/**
	 * Whether a bake writes its packages.
	 *
	 * On everywhere a person or a model is driving the editor; off during an automation run, so the
	 * gate does not deposit test assets in the user's Content folder on every pass. A test that
	 * wants to prove the durability promise opts back in with FHFBakeSaveScope.
	 */
	bool ShouldSaveBakedAssets()
	{
		return GHasSaveOverride ? GSaveOverride : !GIsAutomationTesting;
	}

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

	/** A level that has never been saved has no stable path, so its name is /Temp/Untitled_N. */
	bool LevelIsUnsaved(UWorld* World)
	{
		const FName Package = LevelPackageNameOf(World);
		return Package.IsNone() || !FPackageName::IsValidLongPackageName(Package.ToString())
			|| Package.ToString().StartsWith(TEXT("/Temp/"));
	}

	/**
	 * A FINGERPRINT OF THE ASSET'S OWN GEOMETRY, taken from the same mesh description a re-bake writes.
	 *
	 * The whole point is that it is a fact about the ASSET rather than about the element, because the
	 * thing it has to catch is somebody writing to the asset without the element hearing about it -
	 * which is precisely what happens when a Modeling Tool is pointed at a baked element. See
	 * FHFBakedPart::BakedContentHash for the measurement behind that.
	 *
	 * Positions and counts only. Not a checksum of the package: re-saving, a build, a lightmap
	 * regeneration and a material assignment all change the bytes on disk and none of them is an edit
	 * to the geometry, and a fingerprint that cried wolf on a re-save would be turned off within a day.
	 *
	 * Zero means "could not be taken", and every comparison treats zero as "do not judge".
	 */
	int64 ContentHashOf(UStaticMesh* Asset)
	{
		if (Asset == nullptr)
		{
			return 0;
		}

		// GetMeshDescription on a compiling asset is not answerable. Measured in
		// HouseForge.Bake.Probe.CollisionAndCompilation; the same wait the collision read needs.
		FStaticMeshCompilingManager::Get().FinishCompilation({ Asset });

		const FMeshDescription* Description = Asset->GetMeshDescription(0);
		if (Description == nullptr)
		{
			return 0;
		}

		const FStaticMeshConstAttributes Attributes(*Description);
		TVertexAttributesConstRef<FVector3f> Positions = Attributes.GetVertexPositions();

		// The raw bits of each position rather than a float hash: two meshes that differ by less than a
		// float's precision are the same mesh, and anything a Modeling Tool did will move a vertex by
		// far more than that. MemCrc32 over the three components is order-dependent, which is wanted -
		// a remesh that keeps every position and renumbers the vertices IS an edit.
		uint32 Hash = ::GetTypeHash(Description->Vertices().Num());
		for (const FVertexID Vertex : Description->Vertices().GetElementIDs())
		{
			const FVector3f Position = Positions[Vertex];
			Hash = FCrc::MemCrc32(&Position, sizeof(Position), Hash);
		}

		// The triangle count in the high word, so a mesh whose positions collide by chance still has to
		// collide on its topology size as well. Never zero for a real mesh, which keeps zero meaning
		// "not taken" rather than "an empty mesh".
		const int64 Triangles = static_cast<int64>(Description->Triangles().Num()) + 1;
		return (Triangles << 32) | static_cast<int64>(Hash);
	}

	/** The stamp HouseForge wrote on an asset, or null if it did not write one. */
	const UHFBakedMeshUserData* StampOf(UStaticMesh* Asset)
	{
		return Asset != nullptr
			? Cast<UHFBakedMeshUserData>(Asset->GetAssetUserDataOfClass(UHFBakedMeshUserData::StaticClass()))
			: nullptr;
	}

	/**
	 * Writes the provenance stamp, replacing any previous one.
	 *
	 * Unconditional after every create OR update, because a re-create re-runs the asset's constructor
	 * and resets its AssetUserData array - measured, not assumed. Without re-applying it, the second
	 * bake of an element silently un-stamps its own asset and the orphan scan stops recognising it.
	 */
	void StampAsset(UStaticMesh* Asset, AHFElementActor* Element, FName SourceComponentName, FName LevelPackage,
		int64 ContentHash)
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
		Stamp->ContentHash = ContentHash;
		Stamp->BakedAtUtc = FDateTime::UtcNow();
		Asset->AddAssetUserData(Stamp);
	}
}

FHFBakeSaveScope::FHFBakeSaveScope(bool bInAllowSave)
	: bPreviousHasOverride(GHasSaveOverride)
	, bPreviousAllow(GSaveOverride)
{
	GHasSaveOverride = true;
	GSaveOverride = bInAllowSave;
}

FHFBakeSaveScope::~FHFBakeSaveScope()
{
	GHasSaveOverride = bPreviousHasOverride;
	GSaveOverride = bPreviousAllow;
}

FString FHFBakeReport::Summary() const
{
	FString Text = FString::Printf(
		TEXT("%d element(s) baked (%d part(s)), %d unbaked, %d skipped, %d failed."),
		ElementsBaked, PartsBaked, ElementsUnbaked, ElementsSkipped, ElementsFailed);

	if (PackagesSaved > 0)
	{
		Text += FString::Printf(TEXT(" %d package(s) written."), PackagesSaved);
	}

	if (!Orphaned.IsEmpty())
	{
		Text += FString::Printf(TEXT(" %d baked asset(s) are now unclaimed."), Orphaned.Num());
	}

	if (!HandEdited.IsEmpty())
	{
		// FIRST-CLASS IN THE SUMMARY, not buried in the message list. This is the line that says work
		// was kept, and a report that only said "1 skipped" would leave a user hunting for why.
		Text += FString::Printf(
			TEXT(" %d element(s) had their baked asset edited since it was baked and were NOT overwritten."),
			HandEdited.Num());
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
	const FName LevelPackage = LevelPackageNameOf(World);

	// A REMEMBERED FOLDER IS ONLY GOOD FOR THE LEVEL IT WAS RESOLVED FOR.
	//
	// BakedAssetFolder is a plain serialised property, so DUPLICATING a level - or saving it under a
	// new name - carries the original's folder into the copy. The asset names are functions of the
	// element id alone and are identical in both, so the copy's first bake would rewrite the
	// original's meshes in place and re-stamp them for the copy, at which point the original's orphan
	// scan can no longer see them either. The level this was resolved for is recorded alongside it
	// precisely so the copy notices and re-resolves. See AHFHouseActor::BakedAssetFolderLevel.
	if (House != nullptr && !House->BakedAssetFolder.IsEmpty()
		&& (House->BakedAssetFolderLevel.IsNone() || House->BakedAssetFolderLevel == LevelPackage))
	{
		if (House->BakedAssetFolderLevel.IsNone() && !LevelPackage.IsNone())
		{
			// An older level, saved before the field existed. Adopted rather than re-resolved: the
			// assets are already there and moving them would strand them.
			House->Modify();
			House->BakedAssetFolderLevel = LevelPackage;
		}
		return House->BakedAssetFolder;
	}

	FString LevelName = TEXT("Untitled");
	if (World != nullptr && World->GetOutermost() != nullptr)
	{
		LevelName = FPackageName::GetShortName(World->GetOutermost()->GetFName());
	}

	// AN UNSAVED LEVEL IS NAMED Untitled_N AND N IS NOT UNIQUE ACROSS PROCESSES.
	//
	// Measured on this machine: /Game/HouseForge/Baked held Untitled_0 through Untitled_4, and two
	// sessions both resolved to Untitled_1 - the second rewrote the first's 410 meshes. Nothing
	// announced it, because the elements still rendered; they were simply another flat's geometry.
	// The write-back below cannot help, because an unsaved level never persists the actor that holds
	// it, so every session recomputes from the same colliding name.
	//
	// So an unsaved level is keyed on the house's own BakeOwnerGuid instead. That is stable across
	// sessions for one house, unique between two, and it makes the folder name visibly a placeholder
	// rather than something that reads like a level name. AActor::GetActorGuid is the engine's own
	// persistent identity for an actor and a duplicate gets a fresh one, which is exactly the property
	// needed here.
	if (House != nullptr && LevelIsUnsaved(World))
	{
		const FString Short = House->GetActorGuid().ToString(EGuidFormats::Digits).Left(8);
		LevelName = FString::Printf(TEXT("Unsaved_%s"), *Short);

		UE_LOG(LogHouseForgeEditor, Warning,
			TEXT("This level has never been saved, so its baked assets are going to '%s/%s'. Save the level and re-bake to give them a folder named after it."),
			GBakedRoot, *LevelName);
	}

	const FString Folder = FString::Printf(TEXT("%s/%s"), GBakedRoot, *Sanitise(LevelName));

	if (House != nullptr)
	{
		// WRITTEN BACK on first use. Recomputing it every time would scatter a flat's assets across
		// two folders the moment somebody renamed the level halfway through baking it, and the orphan
		// scan would then report the first half as unclaimed.
		//
		// Modify() rather than a bare assignment, because the write only does that job if it SURVIVES
		// the session. Without marking the actor dirty the field is never saved with the level, the
		// next session recomputes it from the level name, and a rename scatters the assets exactly as
		// if the field had never existed.
		House->Modify();
		House->BakedAssetFolder = Folder;
		House->BakedAssetFolderLevel = LevelPackage;
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

FHFBakePartResult FHFBakeService::BakeOnePart(AHFElementActor* Element, int32 PartIndex, const FString& Folder)
{
	FHFBakePartResult Out;

	TArray<UDynamicMeshComponent*> Sources;
	Element->GetBakeSourceComponents(Sources);
	if (!Sources.IsValidIndex(PartIndex) || !IsValid(Sources[PartIndex]))
	{
		Out.Error = TEXT("the source component went away mid-bake");
		return Out;
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
		Out.bSourceWasEmpty = true;
		return Out;
	}

	FString AssetPath = Folder / AssetNameFor(Element, PartIndex);

	// An existing asset is UPDATED rather than re-created. A re-create reuses the object in place -
	// measured - but re-runs its constructor over a live asset, which resets AssetUserData and so
	// wipes the provenance stamp everything downstream depends on.
	UStaticMesh* Existing = LoadObject<UStaticMesh>(nullptr, *AssetPath, nullptr, LOAD_NoWarn | LOAD_Quiet);

	if (Existing != nullptr)
	{
		// ================================================== WHOSE ASSET IS THIS, AND HAS IT MOVED ON
		//
		// Two questions, in this order, and neither used to be asked. The update path below replaces
		// the WHOLE mesh description, so anything it finds at this path it destroys.
		const UHFBakedMeshUserData* Stamp = StampOf(Existing);
		const FGuid& Owner = Element->EnsureBakeOwnerGuid();

		if (Stamp == nullptr || (Stamp->OwnerGuid.IsValid() && Stamp->OwnerGuid != Owner))
		{
			// SOMEBODY ELSE'S ASSET SITTING AT OUR PATH. The commonest way to get here is a DUPLICATED
			// level: the asset name is a function of the element id alone, so both copies want the same
			// path, and the copy would rewrite the original's geometry in place and re-stamp it - after
			// which the original's orphan scan cannot even see it. An unstamped asset is the other
			// case: something HouseForge did not make, which it has no business overwriting at all.
			//
			// Answered by moving out of the way rather than by failing. A fresh, unique path costs one
			// asset and loses nothing; failing would leave the element unbakeable, which on the copy of
			// a fully baked flat is every element in it.
			const FString Unique = FString::Printf(TEXT("%s_%s"), *AssetPath,
				*Owner.ToString(EGuidFormats::Digits).Left(8));

			UE_LOG(LogHouseForgeEditor, Warning,
				TEXT("'%s' was not written to '%s': that asset %s. Baking to '%s' instead."),
				*Element->GetName(), *AssetPath,
				Stamp == nullptr ? TEXT("was not made by HouseForge") : TEXT("belongs to a different element"),
				*Unique);

			AssetPath = Unique;
			Existing = LoadObject<UStaticMesh>(nullptr, *AssetPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
		}
	}

	if (Existing != nullptr)
	{
		// ============================================ AND HAS ANYBODY WRITTEN TO IT SINCE WE DID
		//
		// THE ONE HOLE MeshRevision CANNOT SEE. In Baked mode the Modeling Tools are handed the baked
		// asset - measured, HouseForge.Bake.Probe.ToolTargetSelection row D - so an artist can sculpt a
		// baked element and the sculpt lands here. bArtistEdited and bUnbakeOnHandEdit are both driven
		// by UDynamicMeshComponent::OnMeshChanged, which a static-mesh edit never raises, and
		// MeshRevision never moves, so IsBakeStale stays false. Every guard was structurally unable to
		// fire, and the next parameter change flattened the afternoon's work with no log line.
		//
		// So the asset's own geometry is fingerprinted at bake time and re-checked here. A mismatch is
		// somebody else's work, and this refuses to overwrite it. FHFBakeService::AdoptBakedAssetEdits
		// is how that work gets home; discarding it is possible too, but only by asking.
		const UHFBakedMeshUserData* Stamp = StampOf(Existing);
		const int64 Expected = Element->BakedParts.IsValidIndex(PartIndex)
			? Element->BakedParts[PartIndex].BakedContentHash
			: (Stamp != nullptr ? Stamp->ContentHash : 0);

		if (Expected != 0 && ContentHashOf(Existing) != Expected)
		{
			Out.bRefusedHandEdited = true;
			Out.Asset = Existing;
			Out.Error = FString::Printf(
				TEXT("the baked asset '%s' has been edited since it was baked, so it was NOT overwritten. Adopt those edits into the live mesh (AdoptBakedAssetEdits) or delete the asset to bake over it."),
				*Existing->GetName());
			return Out;
		}

		FStaticMeshCompilingManager::Get().FinishCompilation({ Existing });

		FMeshDescription* Description = Existing->GetMeshDescription(0);
		if (Description == nullptr)
		{
			Description = Existing->CreateMeshDescription(0);
		}

		if (Description == nullptr)
		{
			Out.Error = FString::Printf(TEXT("could not open a mesh description on '%s'"), *AssetPath);
			return Out;
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

		// RE-ASSERTED ON THE UPDATE PATH, not only at creation. Collision Complexity is editable in the
		// Static Mesh Editor, and an asset a user had set to "Use Simple As Complex" would keep that
		// setting through every subsequent re-bake - so the element would render exactly right and a
		// walkthrough pawn would pass straight through it. Rule 04: collision must match the visual
		// mesh. The same class of loss as the SourceCollisionEnabled defect, one layer down.
		if (UBodySetup* Body = Existing->GetBodySetup())
		{
			if (Body->CollisionTraceFlag != CTF_UseComplexAsSimple)
			{
				Body->Modify();
				Body->CollisionTraceFlag = CTF_UseComplexAsSimple;
			}
		}

		Existing->CommitMeshDescription(0);
		Existing->SetLightMapCoordinateIndex(1);
		Existing->Build(/*bInSilent*/ true);
		Existing->PostEditChange();
		Existing->MarkPackageDirty();

		Out.Asset = Existing;
		Out.ContentHash = ContentHashOf(Existing);
		return Out;
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
		Out.Error = FString::Printf(TEXT("could not create '%s'"), *AssetPath);
		return Out;
	}

	UStaticMesh* Asset = Results.StaticMesh;
	Asset->SetLightMapCoordinateIndex(1);

	// The asset is not queryable until the build finishes: UStaticMesh implements
	// IInterface_AsyncCompilation, and until FinishCompilation returns GetPhysicsTriMeshData yields
	// nothing and UStaticMeshToolTarget::HasNonGeneratedLOD early-outs false. Measured in
	// HouseForge.Bake.Probe.CollisionAndCompilation. Without this an element is briefly baked,
	// collisionless and untargetable, which is a walkthrough falling through a wall.
	//
	// Inside ContentHashOf, which cannot read a mesh description without it either - so the wait is
	// paid once rather than twice.
	Out.ContentHash = ContentHashOf(Asset);

	FAssetRegistryModule::AssetCreated(Asset);
	Asset->MarkPackageDirty();

	Out.Asset = Asset;
	return Out;
}

// ================================================================================= one element

bool FHFBakeService::BakeElement(AHFElementActor* Element, FHFBakeReport& Report)
{
	// The single-element entry point still writes its own packages. A bulk pass opts out and saves
	// once at the end - 160 SavePackages round trips over a flat is most of what makes a whole-house
	// bake feel like a hang.
	return BakeElement(Element, Report, /*bSaveNow*/ true);
}

bool FHFBakeService::BakeElement(AHFElementActor* Element, FHFBakeReport& Report, bool bSaveNow)
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
	const int32 WrittenStart = Report.Written.Num();
	int32 Failures = 0;
	int32 Refusals = 0;
	int32 Parts = 0;

	Element->Modify();

	for (int32 Index = 0; Index < Sources.Num(); ++Index)
	{
		const FHFBakePartResult Baked = BakeOnePart(Element, Index, Folder);

		if (Baked.bRefusedHandEdited)
		{
			// NOT A FAILURE, AND NOT A SUCCESS. The asset on disk is the artist's work and it is still
			// what this part draws; what did not happen is the overwrite. The part keeps its existing
			// record - revision and all - so the element reads as STALE, which is exactly what it is,
			// and the panel and the report can say why.
			if (Element->BakedParts.IsValidIndex(Index))
			{
				Element->BakedParts[Index].bBakedAssetHandEdited = true;
			}

			Report.Messages.Add(FString::Printf(TEXT("'%s' part %d: %s"), *Element->GetName(), Index, *Baked.Error));
			Report.HandEdited.Add(Element);
			++Refusals;
			continue;
		}

		if (Baked.Asset == nullptr && !Baked.bSourceWasEmpty)
		{
			Report.Messages.Add(FString::Printf(TEXT("'%s' part %d: %s"), *Element->GetName(), Index, *Baked.Error));
			++Failures;
			continue;
		}

		const FName SourceName = (Index == 0) ? NAME_None : Sources[Index]->GetFName();
		StampAsset(Baked.Asset, Element, SourceName, LevelPackage, Baked.ContentHash);
		Element->AdoptBakedMesh(Index, SourceName, Baked.Asset, RevisionAtBake, Baked.bSourceWasEmpty,
			Baked.ContentHash);

		if (Baked.Asset != nullptr)
		{
			Report.Written.Add(Baked.Asset);
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

	// A REFUSAL STAYS BAKED. Falling back to the live mesh here would hide the very thing that was
	// preserved: the element would draw generated geometry while the artist's sculpt sat in a file
	// nobody was looking at. Baked keeps it on screen, stale marks it as needing a decision, and
	// AdoptBakedAssetEdits or a deliberate delete is that decision.
	Element->SetRenderMode(EHFRenderMode::Baked);

	if (bSaveNow)
	{
		// WRITTEN TO DISK HERE, not left for whoever saves the level next. See SaveBakedAssets: saving a
		// level does not save the asset packages it references, and there is no Save Content dialog on
		// the path this plugin is actually driven down. A failure to save is reported and does not fail
		// the bake - the geometry is right and on screen either way.
		FString SaveError;
		SaveBakedAssets(Report, WrittenStart, SaveError);
		if (!SaveError.IsEmpty())
		{
			Report.Messages.Add(SaveError);
		}
	}

	if (Refusals > 0)
	{
		++Report.ElementsSkipped;
		return false;
	}

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
	// A WHOLE-FLAT BAKE IS TENS OF SECONDS OF GAME-THREAD WORK. Measured from a gate run: 327 static
	// meshes built in 6.82 s, plus the package writes. Without this Windows paints "Not Responding"
	// over the operation this milestone exists to make routine, and there is no way to stop it.
	//
	// Cancellable, and cancelling is SAFE by construction rather than by unwinding: every element is
	// finished or untouched, and an element left unbaked is an element still drawing its live mesh.
	const int32 Steps = Elements.Num();
	FScopedSlowTask Task(static_cast<float>(FMath::Max(Steps, 1)),
		bBaked
			? LOCTEXT("BakingElements", "Baking HouseForge elements to static meshes")
			: LOCTEXT("UnbakingElements", "Switching HouseForge elements back to live meshes"));

	// Only in a real editor. MakeDialog under -unattended pumps Slate for nothing, and the suite bakes
	// hundreds of elements per gate run.
	if (!GIsAutomationTesting && !IsRunningCommandlet() && FSlateApplication::IsInitialized())
	{
		Task.MakeDialog(/*bShowCancelButton*/ true);
	}

	const int32 WrittenStart = Report.Written.Num();
	bool bCancelled = false;

	for (AHFElementActor* Element : Elements)
	{
		if (Task.ShouldCancel())
		{
			bCancelled = true;
			Report.Messages.Add(FString::Printf(
				TEXT("Cancelled. %d element(s) were left as they were; nothing was lost, and the operation can simply be run again."),
				Steps - Report.ElementsBaked - Report.ElementsUnbaked));
			break;
		}

		Task.EnterProgressFrame(1.0f, IsValid(Element)
			? FText::FromString(Element->GetName())
			: LOCTEXT("BakingElement", "..."));

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

		BakeElement(Element, Report, /*bSaveNow*/ false);
	}

	// ONE SAVE FOR THE WHOLE PASS. Per element this was 160 separate SavePackages round trips over a
	// flat, and on the failure path 160 chances to open a modal box. Also saved on cancel: what was
	// baked before the cancel is baked, and leaving it unsaved would be the one way this operation
	// could lose something.
	if (bBaked && Report.Written.Num() > WrittenStart)
	{
		FString SaveError;
		SaveBakedAssets(Report, WrittenStart, SaveError);
		if (!SaveError.IsEmpty())
		{
			Report.Messages.Add(SaveError);
		}
	}

	if (bCancelled)
	{
		UE_LOG(LogHouseForgeEditor, Log, TEXT("The bake was cancelled by the user."));
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

	TArray<AHFElementActor*> Wanted;
	for (AHFElementActor* Element : Elements)
	{
		if (!IsValid(Element))
		{
			continue;
		}

		// bBakeAssetMissing AS WELL AS stale, and it is not a widening for its own sake. An element
		// whose asset was force-deleted in the Content Browser comes back with a null pointer and a
		// path that still names it - correctly, that is what bBakeAssetMissing is for - and
		// HasAnyBakedAsset is then FALSE, so "Rebake stale" skipped precisely the elements that most
		// needed it. The one repair button in the feature could not repair the one thing that breaks
		// on its own.
		if ((Element->HasAnyBakedAsset() && Element->IsBakeStale()) || Element->bBakeAssetMissing)
		{
			Wanted.Add(Element);
		}
	}

	// Through the bulk path, so it gets the progress bar, the cancel button and the single save.
	SetRenderModeMany(Wanted, /*bBaked*/ true, Report);
}

// ============================================================== bringing a sculpted asset home

int32 FHFBakeService::AdoptBakedAssetEdits(TArrayView<AHFElementActor* const> Elements, FHFBakeReport& Report)
{
	int32 Adopted = 0;

	for (AHFElementActor* Element : Elements)
	{
		if (!IsValid(Element))
		{
			continue;
		}

		for (int32 Index = 0; Index < Element->BakedParts.Num(); ++Index)
		{
			const FHFBakedPart& Part = Element->BakedParts[Index];
			if (!Part.bBakedAssetHandEdited || Part.BakedMesh == nullptr)
			{
				continue;
			}

			UStaticMesh* Asset = Part.BakedMesh;
			FStaticMeshCompilingManager::Get().FinishCompilation({ Asset });

			const FMeshDescription* Description = Asset->GetMeshDescription(0);
			if (Description == nullptr)
			{
				Report.Messages.Add(FString::Printf(
					TEXT("'%s' part %d: the edited asset '%s' has no readable mesh, so nothing was adopted."),
					*Element->GetName(), Index, *Asset->GetName()));
				continue;
			}

			// The conversion the bake does, run backwards. bPrintDebugMessages off; the failure that
			// matters is an empty result, which is checked rather than logged about.
			FDynamicMesh3 Live;
			FMeshDescriptionToDynamicMesh Converter;
			Converter.Convert(Description, Live);

			if (Live.TriangleCount() == 0)
			{
				Report.Messages.Add(FString::Printf(
					TEXT("'%s' part %d: the edited asset '%s' converted to nothing, so it was left alone."),
					*Element->GetName(), Index, *Asset->GetName()));
				continue;
			}

			Element->Modify();
			Element->AdoptHandEditedMesh(Index, MoveTemp(Live));
			++Adopted;

			// AND THE FINGERPRINT, which is what the refusal actually keys on.
			//
			// AdoptHandEditedMesh clears bBakedAssetHandEdited, but the refusal in WritePart compares
			// BakedContentHash against the asset's geometry - not the flag. Leaving the stale hash
			// there made adoption a half-measure that read as a whole one: the element said the asset
			// was its own again while still carrying the exact evidence that it was not, so the next
			// bake refused all over again and the only way out was to delete the asset.
			//
			// The asset is the authority here, not the live mesh. Adoption converted it through
			// FMeshDescriptionToDynamicMesh, and a round trip back out is not guaranteed bit-identical
			// - so hash what is actually on disk rather than what a re-bake is predicted to write.
			if (Element->BakedParts.IsValidIndex(Index))
			{
				Element->BakedParts[Index].BakedContentHash = ContentHashOf(Asset);
			}

			Report.Messages.Add(FString::Printf(
				TEXT("'%s' part %d: the edits made to '%s' are now its live mesh, and it is marked hand-edited."),
				*Element->GetName(), Index, *Asset->GetName()));
		}
	}

	return Adopted;
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

int32 FHFBakeService::SaveBakedAssets(FHFBakeReport& Report, int32 FromIndex, FString& OutError)
{
	if (!ShouldSaveBakedAssets() || !Report.Written.IsValidIndex(FromIndex))
	{
		return 0;
	}

	TArray<UPackage*> Packages;
	for (int32 Index = FromIndex; Index < Report.Written.Num(); ++Index)
	{
		if (UStaticMesh* Loaded = Report.Written[Index].Get())
		{
			Packages.AddUnique(Loaded->GetOutermost());
		}
	}

	if (Packages.IsEmpty())
	{
		return 0;
	}

	// UPackage::SavePackage DIRECTLY, and not through any of the editor's save helpers.
	//
	// This was UEditorLoadingAndSavingUtils::SavePackages, chosen over SavePackagesWithDialog with a
	// comment saying there is nobody at a dialog. Measured: on failure SavePackages routes through
	// InternalPromptForCheckoutAndSave and opens a modal message box PER ASSET - 22 of them in one
	// run, at about five seconds each. Under -unattended they auto-dismiss; in the interactive editor
	// a model driving over MCP hangs on the first one and there is one waiting per element. It also
	// pumps Slate, so editor tick callbacks re-enter in the middle of a bake.
	//
	// SavePackage opens no UI, pumps nothing, and returns a result per package - which is also how
	// this can now say WHICH package failed instead of only how many.
	int32 Saved = 0;
	TArray<FString> Failed;

	for (UPackage* Package : Packages)
	{
		if (Package == nullptr)
		{
			continue;
		}

		const FString FileName = FPackageName::LongPackageNameToFilename(
			Package->GetName(), FPackageName::GetAssetPackageExtension());

		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		Args.SaveFlags = SAVE_NoError | SAVE_KeepDirty;
		Args.Error = GWarn;

		const FSavePackageResultStruct Result = GEditor != nullptr
			? UPackage::Save(Package, nullptr, *FileName, Args)
			: FSavePackageResultStruct(ESavePackageResult::Error);

		if (Result.IsSuccessful())
		{
			// Cleared only on success, which is why SAVE_KeepDirty is set above: a package that failed
			// to write must stay dirty so the next Ctrl+S still offers it.
			Package->SetDirtyFlag(false);
			++Saved;
		}
		else
		{
			Failed.Add(Package->GetName());
		}
	}

	if (!Failed.IsEmpty())
	{
		// Not fatal, and deliberately not a bake failure: the geometry is correct and in memory, and
		// the element is showing it. What is at risk is only durability, so it is said plainly, with
		// the package names, and the caller decides.
		OutError = FString::Printf(
			TEXT("%d baked asset package(s) could not be written to disk (%s%s). The bake is correct in this session but will not survive closing the editor."),
			Failed.Num(), *FString::Join(TArrayView<const FString>(Failed.GetData(), FMath::Min(Failed.Num(), 5)), TEXT(", ")),
			Failed.Num() > 5 ? TEXT(", ...") : TEXT(""));
		UE_LOG(LogHouseForgeEditor, Warning, TEXT("%s"), *OutError);
	}

	Report.PackagesSaved += Saved;
	return Saved;
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
