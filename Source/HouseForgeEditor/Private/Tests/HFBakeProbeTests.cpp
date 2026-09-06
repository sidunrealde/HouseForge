// Copyright Siddartha G. All Rights Reserved.

//
// Bake probes.
//
// Docs/PanelAndBakeDesign.md ends with seven things its author would not assert without checking.
// These tests are how they were checked. They are deliberately written as measurements rather than
// assertions of a design: each one reports what the engine actually does through AddInfo, and only
// asserts the parts that must never silently change underneath the bake.
//
// The first probe is the dangerous one. A baked element carries a UDynamicMeshComponent holding the
// live mesh and a UStaticMeshComponent holding the baked asset. If a Modeling Tool picks the static
// mesh component, an artist who thinks they are sculpting the wall is editing a derived asset that
// the next rebake throws away - which breaks the artist-editable guarantee the whole plugin rests
// on. Each probe logs its measurement through AddInfo, so the automation log is the evidence:
//
//   Automation RunTests HouseForge.Bake.Probe
//
// What they measured, on 5.8, in a -nullrhi run:
//   1. The static mesh component WINS. Hiding it does nothing. UNREGISTERING IT DOES NOTHING -
//      ToolBuilderUtil::FindAllComponents goes through AActor::GetComponents, which walks
//      OwnedComponents with no registration check. Only clearing the component's UStaticMesh (or
//      destroying the component) gives the live mesh back. With both targetable the candidate count
//      is 2, and USingleSelectionMeshEditingToolBuilder::CanBuildTool requires exactly 1, so every
//      single-selection modelling tool also goes dead.
//   2. GetBodySetup() is valid immediately; the cooked triangle data needs FinishCompilation first.
//   3. NewObject over an existing same-class name reuses the object in place - no rename, no
//      duplicate - but re-runs the constructor over a live asset.
//   4. PolyTriGroups survives CommitMeshDescription with its ids intact, and the non-empty section
//      index is the role index as well, so both routes to role targeting work.
//   5. BuildScale3D is 1 on an asset we create, so bUseBuildScale is inert.
//   6. bGenerateLightmapUVs unwraps UV0 into UV1 and so OVERWRITES the packed lightmap channel
//      milestone 10 already generated. It must stay off.
//   7. GEditor is valid headless, GUndo is null, and UE::AssetUtils::CreateStaticMeshAsset touches
//      neither.
//

#include "HouseForgeEditor.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Actors/HFElementActors.h"
#include "AssetUtils/CreateStaticMeshUtil.h"
#include "Components/DynamicMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "DynamicMeshToMeshDescription.h"
#include "Editor.h"
#include "Engine/StaticMesh.h"
#include "Geometry/HFMeshOps.h"
#include "Interfaces/Interface_CollisionDataProvider.h"
#include "MeshDescription.h"
#include "MeshDescriptionBuilder.h"
#include "Misc/AutomationTest.h"
#include "PhysicsEngine/BodySetup.h"
#include "StaticMeshAttributes.h"
#include "StaticMeshCompiler.h"
#include "TargetInterfaces/DynamicMeshCommitter.h"
#include "TargetInterfaces/DynamicMeshProvider.h"
#include "TargetInterfaces/MaterialProvider.h"
#include "TargetInterfaces/PrimitiveComponentBackedTarget.h"
#include "ToolContextInterfaces.h"
#include "ToolTargetManager.h"
#include "ToolTargets/ToolTarget.h"
#include "UObject/UObjectHash.h"
#include "ToolTargets/DynamicMeshComponentToolTarget.h"
#include "ToolTargets/StaticMeshComponentToolTarget.h"
#include "ToolTargets/VolumeComponentToolTarget.h"
#include "UObject/Package.h"

using namespace UE::Geometry;

#define HF_TEST_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace HFBakeProbe
{
	/** A wall standing alone in the editor world, generated and nothing else. */
	static AHFWallActor* SpawnWall(UWorld* World)
	{
		AHFWallActor* Actor = World->SpawnActor<AHFWallActor>();
		if (Actor == nullptr)
		{
			return nullptr;
		}
		Actor->Wall.Id = TEXT("W_Probe");
		Actor->Wall.Start = FVector2D(0.0, 0.0);
		Actor->Wall.End = FVector2D(400.0, 0.0);
		Actor->Wall.Thickness = 20.0;
		Actor->Wall.Height = 300.0;
		Actor->Regenerate();
		return Actor;
	}

	/**
	 * Creates a UStaticMesh asset from a dynamic mesh, the way the bake service will.
	 *
	 * UE::AssetUtils::CreateStaticMeshAsset rather than the GeometryScript
	 * CreateNewStaticMeshAssetFromMesh: the low-level one nulls GUndo for the duration and never
	 * touches GEditor, so it is safe in a headless run and cannot half-transact an asset creation
	 * that undo cannot reverse anyway.
	 */
	static UStaticMesh* CreateAsset(const FDynamicMesh3& Mesh, const FString& AssetPath,
		bool bLightmapUVs = false, UPackage* ExistingPackage = nullptr)
	{
		UE::AssetUtils::FStaticMeshAssetOptions Options;
		Options.NewAssetPath = AssetPath;
		Options.UsePackage = ExistingPackage;
		Options.NumSourceModels = 1;
		Options.NumMaterialSlots = FHFMeshOps::NumSurfaceRoles();
		Options.bGenerateLightmapUVs = bLightmapUVs;
		Options.bCreatePhysicsBody = true;
		Options.CollisionType = ECollisionTraceFlag::CTF_UseComplexAsSimple;
		Options.SourceMeshes.DynamicMeshes.Add(&Mesh);

		UE::AssetUtils::FStaticMeshResults Results;
		const UE::AssetUtils::ECreateStaticMeshResult Code =
			UE::AssetUtils::CreateStaticMeshAsset(Options, Results);
		return (Code == UE::AssetUtils::ECreateStaticMeshResult::Ok) ? Results.StaticMesh : nullptr;
	}

	/** Attaches a static mesh component carrying Asset, the way a baked part will be attached. */
	static UStaticMeshComponent* AttachBakedComponent(AActor* Owner, USceneComponent* AttachTo, UStaticMesh* Asset)
	{
		UStaticMeshComponent* Component = NewObject<UStaticMeshComponent>(Owner, TEXT("HFBakeProbe_Baked"));
		Component->SetStaticMesh(Asset);
		Component->SetupAttachment(AttachTo);
		Owner->AddInstanceComponent(Component);
		Component->RegisterComponent();
		return Component;
	}

	/** The target set every single-selection mesh editing tool asks for (PolyEdit, Sculpt, ...). */
	static const FToolTargetTypeRequirements& MeshEditingRequirements()
	{
		static FToolTargetTypeRequirements Requirements({
			UMaterialProvider::StaticClass(),
			UDynamicMeshCommitter::StaticClass(),
			UDynamicMeshProvider::StaticClass(),
			UPrimitiveComponentBackedTarget::StaticClass()
			});
		return Requirements;
	}

	/** A target manager loaded with exactly the factories UModelingToolsEditorMode::Enter loads, in its order. */
	static UToolTargetManager* MakeModelingModeTargetManager()
	{
		UToolTargetManager* Manager = NewObject<UToolTargetManager>();
		Manager->Initialize();
		// ModelingToolsEditorMode.cpp:320-322. The order matters: BuildFirstSelectedTargetable
		// returns from the FIRST factory that can build anything, and static mesh comes first.
		Manager->AddTargetFactory(NewObject<UStaticMeshComponentToolTargetFactory>(Manager));
		Manager->AddTargetFactory(NewObject<UVolumeComponentToolTargetFactory>(Manager));
		Manager->AddTargetFactory(NewObject<UDynamicMeshComponentToolTargetFactory>(Manager));
		return Manager;
	}

	/** What a modelling tool would end up editing, and how many candidates it saw. */
	struct FProbeResult
	{
		int32 Count = 0;
		FString TargetClass = TEXT("<none>");
		FString BackingComponent = TEXT("<none>");
	};

	static FProbeResult Probe(UToolTargetManager* Manager, UWorld* World, AActor* Actor)
	{
		FToolBuilderState State;
		State.World = World;
		State.SelectedActors.Add(Actor);

		FProbeResult Result;
		Result.Count = Manager->CountSelectedAndTargetable(State, MeshEditingRequirements());

		if (UToolTarget* Target = Manager->BuildFirstSelectedTargetable(State, MeshEditingRequirements()))
		{
			Result.TargetClass = Target->GetClass()->GetName();
			if (IPrimitiveComponentBackedTarget* Backed = Cast<IPrimitiveComponentBackedTarget>(Target))
			{
				if (UPrimitiveComponent* Component = Backed->GetOwnerComponent())
				{
					Result.BackingComponent = Component->GetClass()->GetName();
				}
			}
		}
		return Result;
	}
}

//
// QUESTION 1 - the dangerous one.
//
// When one actor carries a UDynamicMeshComponent (live mesh) and a UStaticMeshComponent (baked
// asset), which does UToolTargetManager hand a Modeling Tool?
//
// Six configurations, because the answer decides how ApplyRenderMode has to be written. The design
// assumed unregistering the baked component removes it as a candidate; ToolBuilderUtil::FindAllComponents
// goes through AActor::GetComponents, which walks OwnedComponents with no registration check, so
// that assumption is the one under test.
//
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFBakeProbeToolTargetTest,
	"HouseForge.Bake.Probe.ToolTargetSelection", HF_TEST_FLAGS)

bool FHFBakeProbeToolTargetTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("An editor world is open"), World))
	{
		return false;
	}

	AHFWallActor* Wall = HFBakeProbe::SpawnWall(World);
	if (!TestNotNull(TEXT("A wall actor spawns"), Wall))
	{
		return false;
	}
	ON_SCOPE_EXIT{ if (IsValid(Wall)) { Wall->Destroy(); } };

	UDynamicMeshComponent* Dynamic = Wall->GetMeshComponent();
	if (!TestNotNull(TEXT("The wall has a dynamic mesh component"), Dynamic))
	{
		return false;
	}

	UStaticMesh* Asset = nullptr;
	Dynamic->GetDynamicMesh()->ProcessMesh([&Asset](const FDynamicMesh3& Mesh)
	{
		Asset = HFBakeProbe::CreateAsset(Mesh, TEXT("/Game/HouseForge/BakeProbe/SM_Probe_Target"));
	});
	if (!TestNotNull(TEXT("A static mesh asset is created from the wall"), Asset))
	{
		return false;
	}
	// HasNonGeneratedLOD early-outs to false while the mesh is still compiling, which would make
	// every configuration below read "Dynamic" for the wrong reason.
	FStaticMeshCompilingManager::Get().FinishCompilation({ Asset });
	AddInfo(FString::Printf(TEXT("Asset compiled: IsCompiling=%d NumSourceModels=%d"),
		Asset->IsCompiling() ? 1 : 0, Asset->GetNumSourceModels()));

	UStaticMeshComponent* Baked = HFBakeProbe::AttachBakedComponent(Wall, Dynamic, Asset);
	UToolTargetManager* Manager = HFBakeProbe::MakeModelingModeTargetManager();

	auto Report = [this, Manager, World, Wall](const TCHAR* Label)
	{
		const HFBakeProbe::FProbeResult R = HFBakeProbe::Probe(Manager, World, Wall);
		AddInfo(FString::Printf(TEXT("[%s] candidates=%d target=%s backing=%s"),
			Label, R.Count, *R.TargetClass, *R.BackingComponent));
		return R;
	};

	// A. Both live and visible - what a naive bake would leave behind.
	const HFBakeProbe::FProbeResult BothVisible = Report(TEXT("A both visible"));

	// B. Baked component hidden, dynamic still editable - the "just hide it" mitigation.
	Baked->SetVisibility(false);
	Baked->SetHiddenInGame(true);
	const HFBakeProbe::FProbeResult BakedHidden = Report(TEXT("B baked hidden"));

	// C. Baked component unregistered - the fix the design proposed.
	Baked->UnregisterComponent();
	const HFBakeProbe::FProbeResult BakedUnregistered = Report(TEXT("C baked unregistered"));
	Baked->RegisterComponent();
	Baked->SetVisibility(true);
	Baked->SetHiddenInGame(false);

	// D. Baked mode as designed: dynamic marked non-editable, baked visible.
	Dynamic->SetIsEditable(false);
	const HFBakeProbe::FProbeResult BakedMode = Report(TEXT("D baked mode (dynamic not editable)"));
	Dynamic->SetIsEditable(true);

	// E. Baked component keeps existing but holds no asset - the candidate is gone at the source.
	Baked->SetStaticMesh(nullptr);
	const HFBakeProbe::FProbeResult MeshCleared = Report(TEXT("E baked component mesh cleared"));
	Baked->SetStaticMesh(Asset);

	// F. No baked component at all - the control.
	Baked->DestroyComponent();
	const HFBakeProbe::FProbeResult NoBaked = Report(TEXT("F no baked component"));

	// The load-bearing assertions. These are what a future engine change must not break silently.
	TestEqual(TEXT("With no baked component, a tool edits the live dynamic mesh"),
		NoBaked.TargetClass, FString(TEXT("DynamicMeshComponentToolTarget")));
	TestEqual(TEXT("With no baked component there is exactly one candidate, so single-selection tools can start"),
		NoBaked.Count, 1);
	TestEqual(TEXT("Clearing the baked component's asset restores the live mesh as the only candidate"),
		MeshCleared.TargetClass, FString(TEXT("DynamicMeshComponentToolTarget")));
	TestEqual(TEXT("Clearing the baked component's asset leaves exactly one candidate"),
		MeshCleared.Count, 1);
	TestEqual(TEXT("In baked mode a non-editable dynamic mesh leaves the baked asset as the only candidate"),
		BakedMode.Count, 1);

	// AND WHAT THAT ONE CANDIDATE IS. Asserted rather than recorded, which is the change this row
	// needed: it has been printing StaticMeshComponentToolTarget in every gate run since the probe was
	// written, and nothing read it. That is the whole hazard - in Baked mode a Modeling Tool edits the
	// BAKED ASSET - and the plugin now depends on knowing it, because FHFBakeService refuses to
	// overwrite an asset whose geometry has moved on and AdoptBakedAssetEdits is the way back.
	//
	// If a future engine version makes Baked mode target the dynamic mesh instead, this fails and the
	// refusal machinery gets re-derived rather than quietly becoming dead weight.
	TestEqual(TEXT("In baked mode the one candidate is the BAKED ASSET, which is why a re-bake must refuse to overwrite an edited one"),
		BakedMode.TargetClass, FString(TEXT("StaticMeshComponentToolTarget")));
	TestEqual(TEXT("In baked mode the target is backed by the static mesh component, not the live one"),
		BakedMode.BackingComponent, FString(TEXT("StaticMeshComponent")));

	// Recorded rather than asserted: these are the measurements the design needed.
	AddInfo(FString::Printf(
		TEXT("SUMMARY visible=%s/%d hidden=%s/%d unregistered=%s/%d bakedmode=%s/%d cleared=%s/%d none=%s/%d"),
		*BothVisible.TargetClass, BothVisible.Count,
		*BakedHidden.TargetClass, BakedHidden.Count,
		*BakedUnregistered.TargetClass, BakedUnregistered.Count,
		*BakedMode.TargetClass, BakedMode.Count,
		*MeshCleared.TargetClass, MeshCleared.Count,
		*NoBaked.TargetClass, NoBaked.Count));

	if (Asset->GetOutermost())
	{
		Asset->GetOutermost()->SetDirtyFlag(false);
	}
	return true;
}

//
// QUESTION 2 - is cooked collision available immediately, in a headless -nullrhi run?
// QUESTION 7 - does the headless editor have a GEditor the asset path can rely on?
//
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFBakeProbeCollisionTest,
	"HouseForge.Bake.Probe.CollisionAndCompilation", HF_TEST_FLAGS)

bool FHFBakeProbeCollisionTest::RunTest(const FString& Parameters)
{
	AddInfo(FString::Printf(TEXT("GEditor=%s GUndo=%s IsRunningCommandlet=%d"),
		GEditor ? TEXT("valid") : TEXT("null"),
		GUndo ? TEXT("valid") : TEXT("null"),
		IsRunningCommandlet() ? 1 : 0));
	TestNotNull(TEXT("The headless automation editor has a GEditor"), GEditor);

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	AHFWallActor* Wall = World ? HFBakeProbe::SpawnWall(World) : nullptr;
	if (!TestNotNull(TEXT("A wall actor spawns"), Wall))
	{
		return false;
	}
	ON_SCOPE_EXIT{ if (IsValid(Wall)) { Wall->Destroy(); } };

	UStaticMesh* Asset = nullptr;
	Wall->GetMeshComponent()->GetDynamicMesh()->ProcessMesh([&Asset](const FDynamicMesh3& Mesh)
	{
		Asset = HFBakeProbe::CreateAsset(Mesh, TEXT("/Game/HouseForge/BakeProbe/SM_Probe_Collision"));
	});
	if (!TestNotNull(TEXT("A static mesh asset is created"), Asset))
	{
		return false;
	}

	// Before any wait.
	const bool bCompilingBefore = Asset->IsCompiling();
	UBodySetup* SetupBefore = Asset->GetBodySetup();
	const bool bTriMeshBefore = SetupBefore ? Asset->ContainsPhysicsTriMeshData(true) : false;
	AddInfo(FString::Printf(TEXT("Immediately after create: IsCompiling=%d BodySetup=%s ContainsTriMesh=%d"),
		bCompilingBefore ? 1 : 0, SetupBefore ? TEXT("valid") : TEXT("null"), bTriMeshBefore ? 1 : 0));

	FStaticMeshCompilingManager::Get().FinishCompilation({ Asset });

	UBodySetup* SetupAfter = Asset->GetBodySetup();
	FTriMeshCollisionData CollisionData;
	const bool bGotTriMesh = SetupAfter ? Asset->GetPhysicsTriMeshData(&CollisionData, true) : false;
	AddInfo(FString::Printf(TEXT("After FinishCompilation: IsCompiling=%d TraceFlag=%d TriMeshVerts=%d TriMeshTris=%d"),
		Asset->IsCompiling() ? 1 : 0,
		SetupAfter ? static_cast<int32>(SetupAfter->CollisionTraceFlag) : -1,
		CollisionData.Vertices.Num(), CollisionData.Indices.Num()));

	if (!TestNotNull(TEXT("The asset has a body setup"), SetupAfter))
	{
		return false;
	}
	TestEqual(TEXT("Collision is complex-as-simple, so it matches the visual mesh"),
		static_cast<int32>(SetupAfter->CollisionTraceFlag),
		static_cast<int32>(ECollisionTraceFlag::CTF_UseComplexAsSimple));
	TestTrue(TEXT("Triangle collision data is readable after FinishCompilation"), bGotTriMesh);
	TestTrue(TEXT("The collision mesh has triangles"), CollisionData.Indices.Num() > 0);

	if (Asset->GetOutermost())
	{
		Asset->GetOutermost()->SetDirtyFlag(false);
	}
	return true;
}

//
// QUESTION 3 - what does NewObject<UStaticMesh> do when that name is already taken in the package?
//
// This is the whole reason the design routes repeat bakes through an update path rather than
// re-creating. Measure it rather than guess at the failure mode.
//
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFBakeProbeRebakeNameTest,
	"HouseForge.Bake.Probe.RepeatCreateSameName", HF_TEST_FLAGS)

bool FHFBakeProbeRebakeNameTest::RunTest(const FString& Parameters)
{
	FDynamicMesh3 Mesh;
	FHFMeshOps::InitialiseMesh(Mesh);
	FHFMeshOps::AppendBox(Mesh, FVector3d::Zero(), FVector3d(50, 50, 50), 0.0, EHFSurfaceRole::WallPaint);

	const FString Path = TEXT("/Game/HouseForge/BakeProbe/SM_Probe_Repeat");

	UStaticMesh* First = HFBakeProbe::CreateAsset(Mesh, Path);
	if (!TestNotNull(TEXT("The first create succeeds"), First))
	{
		return false;
	}
	UPackage* Package = First->GetOutermost();
	const FString FirstName = First->GetName();
	const FString FirstPath = First->GetPathName();

	// Second create into the same path, exactly as a naive rebake would.
	UStaticMesh* Second = HFBakeProbe::CreateAsset(Mesh, Path);
	AddInfo(FString::Printf(TEXT("Second create: ptr%s first, first now named '%s' (%s), second '%s' (%s)"),
		(Second == First) ? TEXT("==") : TEXT("!="),
		*First->GetName(), *First->GetPathName(),
		Second ? *Second->GetName() : TEXT("<null>"),
		Second ? *Second->GetPathName() : TEXT("<null>")));
	AddInfo(FString::Printf(TEXT("First object after second create: IsValid=%d HasAnyFlags(RF_NewerVersionExists)=%d"),
		IsValid(First) ? 1 : 0,
		First->HasAnyFlags(RF_NewerVersionExists) ? 1 : 0));

	// Count how many UStaticMesh objects the package now holds. More than one means every rebake
	// leaks a renamed corpse into the package - the thing the design was trying to avoid.
	int32 MeshesInPackage = 0;
	ForEachObjectWithOuter(Package, [&MeshesInPackage](UObject* Object)
	{
		if (Object->IsA<UStaticMesh>())
		{
			++MeshesInPackage;
		}
	}, EGetObjectsFlags::None);
	AddInfo(FString::Printf(TEXT("UStaticMesh objects in package after two creates: %d (started as '%s')"),
		MeshesInPackage, *FirstName));

	TestNotNull(TEXT("The second create returns an asset"), Second);
	TestEqual(TEXT("The second create lands at the same asset path"),
		Second ? Second->GetPathName() : FString(), FirstPath);

	// The update path: reuse the existing object instead of minting a new one.
	UStaticMesh* Existing = Second ? Second : First;
	const int32 SourceModelsBefore = Existing->GetNumSourceModels();
	FMeshDescription* Description = Existing->GetMeshDescription(0);
	AddInfo(FString::Printf(TEXT("Update path: SourceModels=%d MeshDescription=%s"),
		SourceModelsBefore, Description ? TEXT("readable") : TEXT("null")));

	// The case that decides the rebake strategy: re-create the asset in place while a registered
	// component is rendering it, and while an FHFBakedPart-style stamp is attached to it. If a
	// re-create silently wipes the stamp or leaves the component pointing at a torn-down object,
	// rebake must go through an update path rather than a re-create.
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	AHFWallActor* Holder = World ? HFBakeProbe::SpawnWall(World) : nullptr;
	if (Holder)
	{
		ON_SCOPE_EXIT{ if (IsValid(Holder)) { Holder->Destroy(); } };

		UStaticMeshComponent* Component =
			HFBakeProbe::AttachBakedComponent(Holder, Holder->GetMeshComponent(), Existing);

		// Stand in for UHFBakedMeshUserData: any UAssetUserData attached to the asset.
		const int32 UserDataBefore = Existing->GetAssetUserDataArray()
			? Existing->GetAssetUserDataArray()->Num() : -1;
		const FGuid GuidBefore = Existing->GetLightingGuid();

		UStaticMesh* Third = HFBakeProbe::CreateAsset(Mesh, Path);
		FStaticMeshCompilingManager::Get().FinishCompilation({ Third ? Third : Existing });

		const int32 UserDataAfter = (Third && Third->GetAssetUserDataArray())
			? Third->GetAssetUserDataArray()->Num() : -1;
		AddInfo(FString::Printf(
			TEXT("Re-create under a live component: ptr%s existing, component still points at %s, mesh valid=%d, AssetUserData %d -> %d, LightingGuid changed=%d"),
			(Third == Existing) ? TEXT("==") : TEXT("!="),
			Component->GetStaticMesh() == Third ? TEXT("the same asset") : TEXT("a DIFFERENT asset"),
			(Component->GetStaticMesh() != nullptr) ? 1 : 0,
			UserDataBefore, UserDataAfter,
			(Third && Third->GetLightingGuid() != GuidBefore) ? 1 : 0));

		TestNotNull(TEXT("A component still holds a mesh after the asset underneath it is re-created"),
			Component->GetStaticMesh().Get());
	}

	if (Package)
	{
		Package->SetDirtyFlag(false);
	}
	return true;
}

//
// QUESTION 4 - do the surface-role polygroups survive CommitMeshDescription in a readable form?
//
// If they do not, the material panel cannot target faces by role on a baked element, and role
// targeting has to go through material sections instead. The material library already assigns
// role -> MaterialID, so measure both and find out whether the fallback is already in place.
//
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFBakeProbePolygroupsTest,
	"HouseForge.Bake.Probe.SurfaceRolesSurviveTheBake", HF_TEST_FLAGS)

bool FHFBakeProbePolygroupsTest::RunTest(const FString& Parameters)
{
	// Three roles, so a lost or renumbered group is visible rather than coincidentally right.
	FDynamicMesh3 Mesh;
	FHFMeshOps::InitialiseMesh(Mesh);
	FHFMeshOps::AppendBox(Mesh, FVector3d(0, 0, 0), FVector3d(50, 50, 50), 0.0, EHFSurfaceRole::WallPaint);
	FHFMeshOps::AppendBox(Mesh, FVector3d(200, 0, 0), FVector3d(50, 50, 50), 0.0, EHFSurfaceRole::FloorFinish);
	FHFMeshOps::AppendBox(Mesh, FVector3d(400, 0, 0), FVector3d(50, 50, 50), 0.0, EHFSurfaceRole::Glass);
	FHFMeshOps::AssignMaterialIdsFromRoles(Mesh);

	const TSet<EHFSurfaceRole> RolesBefore = FHFMeshOps::RolesPresent(Mesh);
	AddInfo(FString::Printf(TEXT("Roles on the dynamic mesh before bake: %d"), RolesBefore.Num()));

	UStaticMesh* Asset = HFBakeProbe::CreateAsset(Mesh, TEXT("/Game/HouseForge/BakeProbe/SM_Probe_Roles"));
	if (!TestNotNull(TEXT("A static mesh asset is created"), Asset))
	{
		return false;
	}
	FStaticMeshCompilingManager::Get().FinishCompilation({ Asset });

	FMeshDescription* Description = Asset->GetMeshDescription(0);
	if (!TestNotNull(TEXT("The committed mesh description is readable back"), Description))
	{
		return false;
	}

	// Polygroups: FDynamicMeshToMeshDescription writes them as the custom per-triangle
	// PolyTriGroups polygon attribute (bSetPolyGroups defaults true).
	const bool bHasPolyTriGroups =
		Description->PolygonAttributes().HasAttribute(ExtendedMeshAttribute::PolyTriGroups);
	TSet<int32> GroupsAfter;
	if (bHasPolyTriGroups)
	{
		const auto Groups =
			Description->PolygonAttributes().GetAttributesRef<int32>(ExtendedMeshAttribute::PolyTriGroups);
		for (const FPolygonID PolygonID : Description->Polygons().GetElementIDs())
		{
			GroupsAfter.Add(Groups[PolygonID]);
		}
	}

	// Material sections: the fallback route, and the one the renderer actually uses.
	// Sections come out dense - one per material id from 0 up to the highest used - so the count is
	// not the role count. What matters is that a non-empty section's INDEX is still the role index.
	TSet<int32> NonEmptySections;
	FString SectionList;
	for (const FPolygonGroupID GroupID : Description->PolygonGroups().GetElementIDs())
	{
		const int32 Triangles = Description->GetPolygonGroupPolygons(GroupID).Num();
		if (Triangles > 0)
		{
			NonEmptySections.Add(GroupID.GetValue());
			SectionList += FString::Printf(TEXT("%d(role %d, %d tris) "), GroupID.GetValue(),
				static_cast<int32>(FHFMeshOps::RoleForMaterialId(GroupID.GetValue())), Triangles);
		}
	}

	AddInfo(FString::Printf(
		TEXT("After CommitMeshDescription: PolyTriGroups attribute=%d distinct groups=%d sections total=%d non-empty=%d material slots=%d"),
		bHasPolyTriGroups ? 1 : 0, GroupsAfter.Num(),
		Description->PolygonGroups().Num(), NonEmptySections.Num(),
		Asset->GetStaticMaterials().Num()));

	TArray<int32> SortedGroups = GroupsAfter.Array();
	SortedGroups.Sort();
	FString GroupList;
	for (const int32 Group : SortedGroups)
	{
		GroupList += FString::Printf(TEXT("%d(role %d) "), Group,
			static_cast<int32>(FHFMeshOps::RoleForGroup(Group)));
	}
	AddInfo(FString::Printf(TEXT("Polygroups read back: %s"), *GroupList));
	AddInfo(FString::Printf(TEXT("Non-empty sections read back: %s"), *SectionList));

	TestTrue(TEXT("The PolyTriGroups attribute survives CommitMeshDescription"), bHasPolyTriGroups);
	TestEqual(TEXT("Every surface role that went in comes back as its own polygroup"),
		GroupsAfter.Num(), RolesBefore.Num());
	TestEqual(TEXT("Every surface role also comes back as its own non-empty material section"),
		NonEmptySections.Num(), RolesBefore.Num());

	// The fallback the design asked about: role targeting through sections instead of polygroups.
	// It works, and it needs no lookup table - the section index IS the role index, because
	// AssignMaterialIdsFromRoles writes MaterialIdForRole and the converter turns material ids into
	// polygon groups one for one.
	for (const EHFSurfaceRole Role : RolesBefore)
	{
		TestTrue(FString::Printf(TEXT("Role %d has a section at its own index"), static_cast<int32>(Role)),
			NonEmptySections.Contains(FHFMeshOps::MaterialIdForRole(Role)));
	}
	TestEqual(TEXT("The asset carries one material slot per surface role, so slot index means the same thing everywhere"),
		Asset->GetStaticMaterials().Num(), FHFMeshOps::NumSurfaceRoles());

	if (Asset->GetOutermost())
	{
		Asset->GetOutermost()->SetDirtyFlag(false);
	}
	return true;
}

//
// QUESTION 5 - bUseBuildScale, and a scaled element actor.
//
// BuildScale3D is a property of the ASSET's build settings, not of the actor transform. An asset we
// create ourselves has BuildScale3D = 1, so the flag is inert - but assert it rather than assume it,
// and check that a scaled actor scales the baked component exactly as it scales the dynamic one.
//
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFBakeProbeBuildScaleTest,
	"HouseForge.Bake.Probe.BuildScaleAndActorScale", HF_TEST_FLAGS)

bool FHFBakeProbeBuildScaleTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	AHFWallActor* Wall = World ? HFBakeProbe::SpawnWall(World) : nullptr;
	if (!TestNotNull(TEXT("A wall actor spawns"), Wall))
	{
		return false;
	}
	ON_SCOPE_EXIT{ if (IsValid(Wall)) { Wall->Destroy(); } };

	Wall->SetActorScale3D(FVector(2.0, 1.0, 0.5));

	UStaticMesh* Asset = nullptr;
	FBox DynamicLocalBounds(ForceInit);
	Wall->GetMeshComponent()->GetDynamicMesh()->ProcessMesh([&](const FDynamicMesh3& Mesh)
	{
		const FAxisAlignedBox3d Box = Mesh.GetBounds();
		DynamicLocalBounds = FBox(FVector(Box.Min), FVector(Box.Max));
		Asset = HFBakeProbe::CreateAsset(Mesh, TEXT("/Game/HouseForge/BakeProbe/SM_Probe_Scale"));
	});
	if (!TestNotNull(TEXT("A static mesh asset is created from a scaled actor"), Asset))
	{
		return false;
	}
	FStaticMeshCompilingManager::Get().FinishCompilation({ Asset });

	const FVector BuildScale = Asset->GetSourceModel(0).BuildSettings.BuildScale3D;
	const FBox AssetBounds = Asset->GetBoundingBox();
	AddInfo(FString::Printf(TEXT("BuildScale3D=%s dynamic local bounds=%s asset bounds=%s"),
		*BuildScale.ToString(), *DynamicLocalBounds.GetSize().ToString(), *AssetBounds.GetSize().ToString()));

	UStaticMeshComponent* Baked = HFBakeProbe::AttachBakedComponent(Wall, Wall->GetMeshComponent(), Asset);
	const FBoxSphereBounds DynamicWorld = Wall->GetMeshComponent()->Bounds;
	const FBoxSphereBounds BakedWorld = Baked->Bounds;
	AddInfo(FString::Printf(TEXT("World bounds under 2/1/0.5 actor scale: dynamic=%s baked=%s"),
		*DynamicWorld.BoxExtent.ToString(), *BakedWorld.BoxExtent.ToString()));

	TestTrue(TEXT("BuildScale3D on a freshly created asset is identity, so bUseBuildScale is inert"),
		BuildScale.Equals(FVector::OneVector, 1e-6));
	TestTrue(TEXT("The baked asset holds the mesh in local space, unscaled by the actor"),
		AssetBounds.GetSize().Equals(DynamicLocalBounds.GetSize(), 1.0));
	TestTrue(TEXT("Under a scaled actor the baked component matches the dynamic one in world space"),
		BakedWorld.BoxExtent.Equals(DynamicWorld.BoxExtent, 1.0));

	if (Asset->GetOutermost())
	{
		Asset->GetOutermost()->SetDirtyFlag(false);
	}
	return true;
}

//
// QUESTION 6 - is bGenerateLightmapUVs worth the build time?
//
// Measure the cost on one wall, and report what the mesh already carries. Milestone 10 did UV work;
// if a lightmap channel is already generated by the finish pass, paying for a second one at build
// time buys nothing.
//
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFBakeProbeLightmapTest,
	"HouseForge.Bake.Probe.LightmapUVCost", HF_TEST_FLAGS)

bool FHFBakeProbeLightmapTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	AHFWallActor* Wall = World ? HFBakeProbe::SpawnWall(World) : nullptr;
	if (!TestNotNull(TEXT("A wall actor spawns"), Wall))
	{
		return false;
	}
	ON_SCOPE_EXIT{ if (IsValid(Wall)) { Wall->Destroy(); } };

	int32 NumUVChannels = 0;
	int32 TriangleCount = 0;
	Wall->GetMeshComponent()->GetDynamicMesh()->ProcessMesh([&](const FDynamicMesh3& Mesh)
	{
		TriangleCount = Mesh.TriangleCount();
		NumUVChannels = (Mesh.HasAttributes() && Mesh.Attributes()) ? Mesh.Attributes()->NumUVLayers() : 0;
	});
	AddInfo(FString::Printf(TEXT("Generated wall: %d triangles, %d UV layers already on the dynamic mesh"),
		TriangleCount, NumUVChannels));

	double WithoutSeconds = 0.0;
	double WithSeconds = 0.0;
	UStaticMesh* Without = nullptr;
	UStaticMesh* With = nullptr;

	Wall->GetMeshComponent()->GetDynamicMesh()->ProcessMesh([&](const FDynamicMesh3& Mesh)
	{
		{
			const double Start = FPlatformTime::Seconds();
			Without = HFBakeProbe::CreateAsset(Mesh, TEXT("/Game/HouseForge/BakeProbe/SM_Probe_NoLightmap"), false);
			if (Without) { FStaticMeshCompilingManager::Get().FinishCompilation({ Without }); }
			WithoutSeconds = FPlatformTime::Seconds() - Start;
		}
		{
			const double Start = FPlatformTime::Seconds();
			With = HFBakeProbe::CreateAsset(Mesh, TEXT("/Game/HouseForge/BakeProbe/SM_Probe_Lightmap"), true);
			if (With) { FStaticMeshCompilingManager::Get().FinishCompilation({ With }); }
			WithSeconds = FPlatformTime::Seconds() - Start;
		}
	});

	if (!TestNotNull(TEXT("Both assets are created"), Without) || !TestNotNull(TEXT("Both assets are created"), With))
	{
		return false;
	}

	AddInfo(FString::Printf(TEXT("Build time for one wall: without lightmap UVs %.1f ms, with %.1f ms (delta %.1f ms, x150 = %.1f s)"),
		WithoutSeconds * 1000.0, WithSeconds * 1000.0,
		(WithSeconds - WithoutSeconds) * 1000.0, (WithSeconds - WithoutSeconds) * 150.0));
	AddInfo(FString::Printf(TEXT("LightMapCoordinateIndex: without=%d with=%d"),
		Without->GetLightMapCoordinateIndex(), With->GetLightMapCoordinateIndex()));

	// The question behind the question: milestone 10 already packs a gutter-sized, non-overlapping
	// lightmap unwrap into UV1 (FHFLightmapParams). If that channel reaches the asset intact, then
	// paying the build to generate a second one buys nothing - and worse, would overwrite a better
	// unwrap with an automatic one.
	auto CountUVs = [](UStaticMesh* Asset) -> int32
	{
		const FMeshDescription* Description = Asset ? Asset->GetMeshDescription(0) : nullptr;
		if (Description == nullptr)
		{
			return -1;
		}
		FStaticMeshConstAttributes Attributes(*Description);
		return Attributes.GetVertexInstanceUVs().GetNumChannels();
	};
	const int32 UVsWithout = CountUVs(Without);
	const int32 UVsWith = CountUVs(With);
	AddInfo(FString::Printf(TEXT("UV channels in the committed mesh description: without=%d with=%d (dynamic mesh had %d)"),
		UVsWithout, UVsWith, NumUVChannels));

	TestEqual(TEXT("The generated lightmap channel reaches the asset without asking the build to make one"),
		UVsWithout, NumUVChannels);
	TestTrue(TEXT("Milestone 10 already put a second UV channel on the mesh"), NumUVChannels >= 2);

	// Not asserted as a threshold - a timing assertion on a shared build machine is a flake
	// generator. The figure is the deliverable.
	TestTrue(TEXT("Both assets built"), Without->GetNumSourceModels() > 0 && With->GetNumSourceModels() > 0);

	if (Without->GetOutermost()) { Without->GetOutermost()->SetDirtyFlag(false); }
	if (With->GetOutermost()) { With->GetOutermost()->SetDirtyFlag(false); }
	return true;
}

/**
 * PROBE: what the mesh-description converter does with the SPARSE material ids HouseForge emits.
 *
 * Every element sets MaterialID = the surface ROLE index, so a wall using roles 0 and 14 hands the
 * converter ids 0 and 14 with fourteen gaps, against NumMaterialSlots = 18. That is unusual - most
 * callers number their materials densely from zero - and it is the shape that makes a converter
 * create polygon groups while it is walking them.
 *
 * Which is what the gate caught, once, inside a bake:
 *
 *   Ensure condition failed: !CurrentNum || CurrentNum[0] == InitialNum
 *   Array has changed during ranged-for iteration!
 *   TMeshAttributeArraySet<int>::Insert -> FMeshElementContainer::Add
 *     -> FDynamicMeshToMeshDescription::Convert_NoSharedInstances
 *     -> UE::AssetUtils::CreateStaticMeshAsset -> FHFBakeService::BakeOnePart
 *
 * A MEASUREMENT, NOT AN ASSERTION. It reports what the conversion does and only fails if the
 * conversion fails outright, because the question it exists to answer is whether sparse ids are
 * what provokes that ensure - and a probe that asserted an answer would be assuming one.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFBakeSparseMaterialIdProbe,
	"HouseForge.Bake.Probe.SparseMaterialIds", HF_TEST_FLAGS)

bool FHFBakeSparseMaterialIdProbe::RunTest(const FString& Parameters)
{
	using namespace UE::Geometry;

	// The widest gap the roles allow: first and last, nothing between.
	const int32 Roles = FHFMeshOps::NumSurfaceRoles();

	FDynamicMesh3 Mesh;
	FHFMeshOps::InitialiseMesh(Mesh);

	if (!FHFMeshOps::AppendPrism(Mesh,
		{ FVector2D(-10, -10), FVector2D(10, -10), FVector2D(10, 10), FVector2D(-10, 10) },
		0.0, 10.0, EHFSurfaceRole::WallPaint))
	{
		AddError(TEXT("The probe could not build a box to convert."));
		return false;
	}

	Mesh.EnableAttributes();
	Mesh.Attributes()->EnableMaterialID();
	FDynamicMeshMaterialAttribute* Ids = Mesh.Attributes()->GetMaterialID();

	int32 Index = 0;
	for (const int32 Tid : Mesh.TriangleIndicesItr())
	{
		// Half at 0, half at the last role: sparse, and both ends of the range in use.
		Ids->SetValue(Tid, (Index++ % 2 == 0) ? 0 : Roles - 1);
	}

	AddInfo(FString::Printf(TEXT("%d triangles, material ids 0 and %d, %d slots."),
		Mesh.TriangleCount(), Roles - 1, Roles));

	UE::AssetUtils::FStaticMeshAssetOptions Options;
	Options.NewAssetPath = TEXT("/Game/HouseForge/Probe/SM_HFSparseIds");
	Options.NumSourceModels = 1;
	Options.NumMaterialSlots = Roles;
	Options.bGenerateLightmapUVs = false;
	Options.bAllowDistanceField = true;
	Options.bSupportRayTracing = true;
	Options.bCreatePhysicsBody = true;
	Options.CollisionType = ECollisionTraceFlag::CTF_UseComplexAsSimple;
	Options.SourceMeshes.DynamicMeshes.Add(&Mesh);

	UE::AssetUtils::FStaticMeshResults Results;
	const UE::AssetUtils::ECreateStaticMeshResult Code =
		UE::AssetUtils::CreateStaticMeshAsset(Options, Results);

	TestTrue(TEXT("The conversion produced an asset"),
		Code == UE::AssetUtils::ECreateStaticMeshResult::Ok && Results.StaticMesh != nullptr);

	if (Results.StaticMesh != nullptr)
	{
		FStaticMeshCompilingManager::Get().FinishCompilation({ Results.StaticMesh });

		const FMeshDescription* Description = Results.StaticMesh->GetMeshDescription(0);
		AddInfo(FString::Printf(TEXT("Converted: %d polygon groups, %d triangles, %d slots on the asset."),
			Description ? Description->PolygonGroups().Num() : -1,
			Description ? Description->Triangles().Num() : -1,
			Results.StaticMesh->GetStaticMaterials().Num()));
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
