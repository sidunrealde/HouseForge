// Copyright Siddartha G. All Rights Reserved.

#include "HouseForge.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Geometry/HFGenerators.h"
#include "Geometry/HFMeshOps.h"
#include "Materials/HFMaterialLibrary.h"
#include "MaterialShared.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInterface.h"
#include "Misc/AutomationTest.h"
#include "Model/HFTypes.h"

using namespace UE::Geometry;

#define HF_TEST_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
	FString RoleName(EHFSurfaceRole Role)
	{
		return StaticEnum<EHFSurfaceRole>()->GetNameStringByValue(static_cast<int64>(Role));
	}

	/** Every role, in enum order. */
	TArray<EHFSurfaceRole> AllRoles()
	{
		TArray<EHFSurfaceRole> Roles;
		for (int32 Index = 0; Index < FHFMeshOps::NumSurfaceRoles(); ++Index)
		{
			Roles.Add(static_cast<EHFSurfaceRole>(Index));
		}
		return Roles;
	}

	/**
	 * A room's floor slab and skirting - two roles on one mesh, from real generator output rather
	 * than boxes assembled by the test.
	 */
	FHFRoom MakeRoom()
	{
		FHFRoom Room;
		Room.Id = TEXT("R_Test");
		Room.Boundary = { FVector2D(0, 0), FVector2D(400, 0), FVector2D(400, 300), FVector2D(0, 300) };
		Room.FloorZ = 0.0;
		Room.CeilingHeight = 300.0;
		Room.SkirtingHeight = 10.0;
		return Room;
	}

	/**
	 * A parameter the instance overrides itself.
	 *
	 * Read off the instance's own override arrays rather than through the resolved value, and that
	 * is the point: an instance that failed to override anything still answers every query, with
	 * the parent's placeholder grey. Reading the resolved value would report sixteen identical
	 * materials as sixteen working ones.
	 */
	bool OverriddenColour(const UMaterialInterface* Material, FName Param, FLinearColor& OutValue)
	{
		const UMaterialInstance* Instance = Cast<UMaterialInstance>(Material);
		if (Instance == nullptr)
		{
			return false;
		}

		for (const FVectorParameterValue& Value : Instance->VectorParameterValues)
		{
			if (Value.ParameterInfo.Name == Param)
			{
				OutValue = Value.ParameterValue;
				return true;
			}
		}
		return false;
	}

	bool OverriddenScalar(const UMaterialInterface* Material, FName Param, float& OutValue)
	{
		const UMaterialInstance* Instance = Cast<UMaterialInstance>(Material);
		if (Instance == nullptr)
		{
			return false;
		}

		for (const FScalarParameterValue& Value : Instance->ScalarParameterValues)
		{
			if (Value.ParameterInfo.Name == Param)
			{
				OutValue = Value.ParameterValue;
				return true;
			}
		}
		return false;
	}

	/** The polygroup of every triangle, in triangle order - the thing a material pass must not disturb. */
	TArray<int32> SnapshotGroups(const FDynamicMesh3& Mesh)
	{
		TArray<int32> Groups;
		Groups.Reserve(Mesh.TriangleCount());
		for (const int32 Tid : Mesh.TriangleIndicesItr())
		{
			Groups.Add(Mesh.GetTriangleGroup(Tid));
		}
		return Groups;
	}
}

/**
 * The whole material system is addressed by role, so a role with no material is a surface nothing
 * can ever reach. Named by role rather than counted, because the useful failure message is which
 * one is missing - typically a role added to the enum without the authoring script being re-run.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFEveryRoleResolvesTest,
	"HouseForge.Materials.EveryRoleResolvesToAMaterial", HF_TEST_FLAGS)

bool FHFEveryRoleResolvesTest::RunTest(const FString& Parameters)
{
	// Seventeen: the sixteen finishes, plus LightSource - the one role that is not a finish, added
	// so a cove's strip and a downlight's lens have something to be made of.
	TestEqual(TEXT("There are seventeen surface roles"), FHFMeshOps::NumSurfaceRoles(), 17);

	// AND THE LAST ONE ROUND-TRIPS. Both mapping tables used to bound themselves on Structure by
	// name, so everything past it fell back to WallPaint - a slot quietly rendering the wrong
	// material, which nothing else here would have caught.
	for (const EHFSurfaceRole Role : AllRoles())
	{
		TestTrue(*FString::Printf(TEXT("Role '%s' survives its material id"), *RoleName(Role)),
			FHFMeshOps::RoleForMaterialId(FHFMeshOps::MaterialIdForRole(Role)) == Role);
	}

	for (const EHFSurfaceRole Role : AllRoles())
	{
		UMaterialInterface* Material = FHFMaterialLibrary::GetPlaceholder(Role);
		TestNotNull(*FString::Printf(TEXT("Surface role '%s' resolves to a material (expected at '%s')"),
			*RoleName(Role), *FHFMaterialLibrary::AssetPathForRole(Role)), Material);
	}

	return true;
}

/**
 * A placeholder set whose entries are indistinguishable is no better than the checkerboard it
 * replaced: the point of it is to be able to tell a skirting from a floor in a screenshot.
 *
 * Asserts on base colour rather than on how it looks, which is the only measurable part of "at a
 * glance" - two roles sharing a colour would be a copy-paste in the authoring table, and that is
 * the mistake this catches.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFRolesLookDifferentTest,
	"HouseForge.Materials.RolesAreVisuallyDistinct", HF_TEST_FLAGS)

bool FHFRolesLookDifferentTest::RunTest(const FString& Parameters)
{
	TArray<TPair<EHFSurfaceRole, FLinearColor>> Colours;

	for (const EHFSurfaceRole Role : AllRoles())
	{
		UMaterialInterface* Material = FHFMaterialLibrary::GetPlaceholder(Role);
		if (Material == nullptr)
		{
			continue;
		}

		FLinearColor Colour = FLinearColor::White;
		if (!TestTrue(*FString::Printf(TEXT("'%s' overrides BaseColor rather than inheriting the parent's grey"),
			*RoleName(Role)), OverriddenColour(Material, TEXT("BaseColor"), Colour)))
		{
			continue;
		}

		Colours.Add({ Role, Colour });
	}

	for (int32 i = 0; i < Colours.Num(); ++i)
	{
		for (int32 j = i + 1; j < Colours.Num(); ++j)
		{
			// In linear space. A tenth of a percent apart on all three channels is far below what
			// the eye resolves, so anything under it is the same colour entered twice.
			const FLinearColor Delta = Colours[i].Value - Colours[j].Value;
			const double Distance = FMath::Sqrt(Delta.R * Delta.R + Delta.G * Delta.G + Delta.B * Delta.B);

			TestTrue(*FString::Printf(TEXT("'%s' and '%s' are different colours"),
				*RoleName(Colours[i].Key), *RoleName(Colours[j].Key)), Distance > 0.001);
		}
	}

	return true;
}

/**
 * Glass has to be translucent or a window reads as a boarded-up hole, and every other role has to
 * be opaque or the flat renders as a ghost of itself.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFGlassIsTranslucentTest,
	"HouseForge.Materials.GlassIsTranslucent", HF_TEST_FLAGS)

bool FHFGlassIsTranslucentTest::RunTest(const FString& Parameters)
{
	for (const EHFSurfaceRole Role : AllRoles())
	{
		UMaterialInterface* Material = FHFMaterialLibrary::GetPlaceholder(Role);
		if (Material == nullptr)
		{
			continue;
		}

		const bool bTranslucent = IsTranslucentBlendMode(Material->GetBlendMode());

		if (Role == EHFSurfaceRole::Glass)
		{
			TestTrue(TEXT("Glass is translucent"), bTranslucent);

			float Opacity = 1.0f;
			if (TestTrue(TEXT("Glass exposes an Opacity parameter"),
				OverriddenScalar(Material, TEXT("Opacity"), Opacity)))
			{
				// Fully opaque glass passes the blend-mode check and still looks like a wall.
				TestTrue(TEXT("Glass is actually see-through, not translucent at full opacity"),
					Opacity < 0.5f);
			}
		}
		else
		{
			TestFalse(*FString::Printf(TEXT("'%s' is opaque"), *RoleName(Role)), bTranslucent);
		}
	}

	return true;
}

/**
 * The bridge between the polygroups the generators emit and the material slots the component
 * renders through. Nothing in a generator knows about material ids, so if this mapping is wrong
 * every surface is materialled as WallPaint and looks entirely plausible.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFMaterialIdsFollowRolesTest,
	"HouseForge.Materials.MaterialIdsFollowSurfaceRoles", HF_TEST_FLAGS)

bool FHFMaterialIdsFollowRolesTest::RunTest(const FString& Parameters)
{
	const FHFRoom Room = MakeRoom();

	// Two roles from the generator, plus a third appended through AppendPreservingRoles - the same
	// composition AHFRoomActor performs, so this covers the case a raw append would have broken.
	FDynamicMesh3 Mesh = FHFGenerators::GenerateFloor(Room, 15.0, FHFSkirting::For(Room, {}, {}, {}, {}));
	FHFMeshOps::AppendPreservingRoles(Mesh, FHFGenerators::GenerateCeilingSlab(Room, 15.0));

	const TSet<EHFSurfaceRole> Expected = {
		EHFSurfaceRole::FloorFinish, EHFSurfaceRole::Skirting, EHFSurfaceRole::CeilingSoffit
	};
	TestTrue(TEXT("A floor, its skirting and its ceiling slab carry three distinct roles"),
		FHFMeshOps::RolesPresent(Mesh).Difference(Expected).IsEmpty()
			&& Expected.Difference(FHFMeshOps::RolesPresent(Mesh)).IsEmpty());

	TestFalse(TEXT("A generated mesh has no material ids until they are assigned"),
		Mesh.HasAttributes() && Mesh.Attributes()->HasMaterialID());

	FHFMeshOps::AssignMaterialIdsFromRoles(Mesh);

	if (!TestTrue(TEXT("Assignment enables the material id attribute"),
		Mesh.HasAttributes() && Mesh.Attributes()->HasMaterialID()))
	{
		return false;
	}

	const FDynamicMeshMaterialAttribute* Ids = Mesh.Attributes()->GetMaterialID();

	int32 Mismatched = 0;
	TSet<int32> IdsSeen;
	for (const int32 Tid : Mesh.TriangleIndicesItr())
	{
		const EHFSurfaceRole Role = FHFMeshOps::RoleForGroup(Mesh.GetTriangleGroup(Tid));
		const int32 Id = Ids->GetValue(Tid);
		IdsSeen.Add(Id);

		if (Id != FHFMeshOps::MaterialIdForRole(Role))
		{
			++Mismatched;
		}
	}

	TestEqual(TEXT("Every triangle's material id is its own role's slot"), Mismatched, 0);
	TestEqual(TEXT("Three roles produce three distinct material ids"), IdsSeen.Num(), 3);

	// The round trip, so a slot can be read back as a role - which is how the material panel will
	// have to work when it lets a user click a face.
	for (const int32 Id : IdsSeen)
	{
		TestTrue(TEXT("A material id maps back to the role it came from"),
			Expected.Contains(FHFMeshOps::RoleForMaterialId(Id)));
	}

	return true;
}

/**
 * The roles are what every material operation targets, and they have already been destroyed once
 * by a raw append that renumbered them. A pass that quietly renumbered them while assigning
 * materials would undo that fix and be just as invisible, so the groups are compared triangle for
 * triangle across the assignment.
 *
 * Deliberately run on a mesh that has been through a boolean and an append, because those are
 * where group ids have gone wrong before.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFMaterialPassKeepsRolesTest,
	"HouseForge.Materials.AssignmentLeavesSurfaceRolesIntact", HF_TEST_FLAGS)

bool FHFMaterialPassKeepsRolesTest::RunTest(const FString& Parameters)
{
	FHFWall Wall;
	Wall.Id = TEXT("W_Test");
	Wall.Start = FVector2D(0, 0);
	Wall.End = FVector2D(400, 0);
	Wall.Thickness = 20.0;
	Wall.Height = 300.0;

	FHFOpening Door;
	Door.Id = TEXT("D_Test");
	Door.WallId = Wall.Id;
	Door.OffsetAlongWall = 200.0;
	Door.Width = 90.0;
	Door.Height = 210.0;
	Door.Kind = EHFOpeningKind::Door;

	// A boolean, then an append: both of the operations that have renumbered groups before.
	FDynamicMesh3 Mesh = FHFGenerators::GenerateWall(Wall, { Door });
	const FHFRoom FloorRoom = MakeRoom();
	FHFMeshOps::AppendPreservingRoles(Mesh,
		FHFGenerators::GenerateFloor(FloorRoom, 15.0, FHFSkirting::For(FloorRoom, {}, {}, {}, {})));

	const TArray<int32> Before = SnapshotGroups(Mesh);
	const TSet<EHFSurfaceRole> RolesBefore = FHFMeshOps::RolesPresent(Mesh);

	FHFMeshOps::AssignMaterialIdsFromRoles(Mesh);

	const TArray<int32> After = SnapshotGroups(Mesh);

	if (!TestEqual(TEXT("Assignment adds and removes no triangles"), After.Num(), Before.Num()))
	{
		return false;
	}
	TestTrue(TEXT("Every triangle keeps the polygroup it had"), After == Before);
	TestTrue(TEXT("The set of surface roles is unchanged"),
		FHFMeshOps::RolesPresent(Mesh).Difference(RolesBefore).IsEmpty()
			&& RolesBefore.Difference(FHFMeshOps::RolesPresent(Mesh)).IsEmpty());

	// Idempotent, because it is derived rather than accumulated. Anything that re-runs it - a
	// rebuild, a bake, a later material pass - must be able to.
	const FDynamicMeshMaterialAttribute* Ids = Mesh.Attributes()->GetMaterialID();
	TArray<int32> FirstPass;
	for (const int32 Tid : Mesh.TriangleIndicesItr())
	{
		FirstPass.Add(Ids->GetValue(Tid));
	}

	FHFMeshOps::AssignMaterialIdsFromRoles(Mesh);

	TArray<int32> SecondPass;
	for (const int32 Tid : Mesh.TriangleIndicesItr())
	{
		SecondPass.Add(Mesh.Attributes()->GetMaterialID()->GetValue(Tid));
	}
	TestTrue(TEXT("Re-assigning changes nothing"), FirstPass == SecondPass);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
