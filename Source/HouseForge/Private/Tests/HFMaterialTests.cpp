// Copyright Siddartha G. All Rights Reserved.

#include "HouseForge.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Geometry/HFGenerators.h"
#include "Geometry/HFMeshOps.h"
#include "Geometry/HFRenderFinish.h"
#include "Materials/HFMaterialLibrary.h"
#include "MaterialShared.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionTextureObjectParameter.h"
#include "Materials/MaterialExpressionTextureSample.h"
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

	/** A parameter's resolved value, wherever in the instance chain it was set. */
	bool ResolvedScalar(const UMaterialInterface* Material, const TCHAR* Param, float& OutValue)
	{
		return Material != nullptr
			&& Material->GetScalarParameterValue(FMaterialParameterInfo(FName(Param)), OutValue);
	}

	/** How many expressions of a given kind a master's graph contains. */
	template <typename ExpressionType>
	int32 CountExpressions(const UMaterial* Master)
	{
		int32 Count = 0;
		if (Master != nullptr)
		{
			for (const UMaterialExpression* Expression : Master->GetExpressions())
			{
				if (Cast<ExpressionType>(Expression) != nullptr)
				{
					++Count;
				}
			}
		}
		return Count;
	}

	/** The two masters every role instances, by the roles that reach them. */
	TArray<TPair<FString, UMaterial*>> Masters()
	{
		TArray<TPair<FString, UMaterial*>> Found;
		for (const EHFSurfaceRole Role : { EHFSurfaceRole::WallPaint, EHFSurfaceRole::Glass })
		{
			UMaterialInterface* Instance = UHFMaterialLibrary::Get()->ResolveMaterial(Role);
			UMaterial* Master = Instance ? Instance->GetMaterial() : nullptr;
			if (Master != nullptr)
			{
				Found.Add({ Master->GetName(), Master });
			}
		}
		return Found;
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
	// Eighteen: sixteen finishes, LightSource - the one role that is not a finish, so a cove's strip
	// and a downlight's lens have something to be made of - and Mirror, split out from Glass once
	// Glass became genuinely transmissive and a mirror tagged Glass became a hole in the wall.
	TestEqual(TEXT("There are eighteen surface roles"), FHFMeshOps::NumSurfaceRoles(), 18);

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
		UMaterialInterface* Material = UHFMaterialLibrary::Get()->ResolveMaterial(Role);
		TestNotNull(*FString::Printf(TEXT("Surface role '%s' resolves to a material (expected at '%s')"),
			*RoleName(Role), *UHFMaterialLibrary::AssetPathForRole(Role)), Material);
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
		UMaterialInterface* Material = UHFMaterialLibrary::Get()->ResolveMaterial(Role);
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

	// THE ONE PAIR THAT IS DELIBERATELY THE SAME FINISH.
	//
	// In an Indian flat the skirting is almost always the floor tile itself, cut down to a 75-100 mm
	// band, and giving it its own colour to satisfy a test would be inventing a material that does not
	// exist to make a screenshot easier to read. What tells a skirting from a floor in a render is
	// that it is vertical and stands at a step, which is geometry, not tone.
	//
	// It is exempted as a PAIR rather than by loosening the threshold, and the exemption is paid for
	// below: an intentional match has to match in every respect, so a colour pasted twice by accident
	// still fails here.
	const TSet<TPair<EHFSurfaceRole, EHFSurfaceRole>> SameFinishByDesign = {
		{ EHFSurfaceRole::FloorFinish, EHFSurfaceRole::Skirting }
	};

	for (int32 i = 0; i < Colours.Num(); ++i)
	{
		for (int32 j = i + 1; j < Colours.Num(); ++j)
		{
			const EHFSurfaceRole A = Colours[i].Key;
			const EHFSurfaceRole B = Colours[j].Key;

			if (SameFinishByDesign.Contains({ A, B }) || SameFinishByDesign.Contains({ B, A }))
			{
				continue;
			}

			// In linear space. A tenth of a percent apart on all three channels is far below what
			// the eye resolves, so anything under it is the same colour entered twice.
			const FLinearColor Delta = Colours[i].Value - Colours[j].Value;
			const double Distance = FMath::Sqrt(Delta.R * Delta.R + Delta.G * Delta.G + Delta.B * Delta.B);

			TestTrue(*FString::Printf(TEXT("'%s' and '%s' are different colours"),
				*RoleName(A), *RoleName(B)), Distance > 0.001);
		}
	}

	// AND THE EXEMPTION IS EARNED, NOT ASSERTED. A pair claiming to be the same finish must agree on
	// everything that makes a finish - tile module, gloss, coat - not merely on its colour. This is
	// what stops the allowlist becoming a place to hide a copy-paste.
	for (const TPair<EHFSurfaceRole, EHFSurfaceRole>& Pair : SameFinishByDesign)
	{
		UMaterialInterface* A = UHFMaterialLibrary::Get()->ResolveMaterial(Pair.Key);
		UMaterialInterface* B = UHFMaterialLibrary::Get()->ResolveMaterial(Pair.Value);
		if (A == nullptr || B == nullptr)
		{
			continue;
		}

		const FString Names = FString::Printf(TEXT("'%s' and '%s'"),
			*RoleName(Pair.Key), *RoleName(Pair.Value));

		for (const TCHAR* Param : { TEXT("TilingMM"), TEXT("Roughness"), TEXT("CoatWeight"),
			TEXT("CoatRoughness"), TEXT("Specular") })
		{
			float ValueA = 0.0f;
			float ValueB = 0.0f;
			if (ResolvedScalar(A, Param, ValueA) && ResolvedScalar(B, Param, ValueB))
			{
				TestEqual(*FString::Printf(TEXT("%s are the same finish, so they agree on %s"),
					*Names, Param), ValueA, ValueB, 0.0001f);
			}
		}

		FLinearColor ColourA = FLinearColor::White;
		FLinearColor ColourB = FLinearColor::Black;
		if (OverriddenColour(A, TEXT("BaseColor"), ColourA)
			&& OverriddenColour(B, TEXT("BaseColor"), ColourB))
		{
			TestTrue(*FString::Printf(TEXT("%s are the same colour deliberately"), *Names),
				ColourA.Equals(ColourB, 0.0001f));
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
		UMaterialInterface* Material = UHFMaterialLibrary::Get()->ResolveMaterial(Role);
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

/**
 * THE DEFECT THIS MILESTONE EXISTS TO FIX, asserted directly on the graph.
 *
 * Both masters used to expose BaseColor, Metallic, Specular and Roughness as flat constants and
 * sample nothing whatsoever - no albedo, no normal, no roughness, no AO - and read no TexCoord at
 * all. Every one of those materials passed every test in this file: they resolved, they were
 * distinct colours, glass was translucent, the slot mapping was right. Nothing said that the
 * world-scale UVs FHFMeshOps::ApplyWorldScaleUVs works so hard to make correct DROVE NOTHING.
 *
 * So this asserts on the presence of the machinery rather than on how it looks, because "reads a
 * texture coordinate" is the exact property that was silently absent and it is not observable from
 * any resolved parameter value.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFMastersSampleSomethingTest,
	"HouseForge.Materials.MastersActuallySampleTextures", HF_TEST_FLAGS)

bool FHFMastersSampleSomethingTest::RunTest(const FString& Parameters)
{
	const TArray<TPair<FString, UMaterial*>> Found = Masters();

	if (!TestEqual(TEXT("Both masters are reachable through their roles"), Found.Num(), 2))
	{
		return false;
	}

	// The five maps the parameter set promises. One texture object parameter each, shared between the
	// UV-mapped and world-aligned branches, so a user assigns a map once however it is projected.
	const TArray<FString> Maps = { TEXT("AlbedoMap"), TEXT("NormalMap"), TEXT("RoughnessMap"),
		TEXT("MetallicMap"), TEXT("AOMap") };

	for (const TPair<FString, UMaterial*>& Entry : Found)
	{
		const FString& Name = Entry.Key;
		UMaterial* Master = Entry.Value;

		TestTrue(*FString::Printf(TEXT("'%s' reads a texture coordinate"), *Name),
			CountExpressions<UMaterialExpressionTextureCoordinate>(Master) > 0);

		TestEqual(*FString::Printf(TEXT("'%s' exposes one texture parameter per map"), *Name),
			CountExpressions<UMaterialExpressionTextureObjectParameter>(Master), Maps.Num());

		// Four samplers per map: one for the UV branch and three for the world-aligned one. The
		// unchosen branch is compiled out by a static switch, so this is a count of what CAN be
		// sampled rather than of what every permutation pays for.
		//
		// The texture parameters have to be subtracted off: UMaterialExpressionTextureObjectParameter
		// derives from UMaterialExpressionTextureSample, so a naive count of samplers counts every
		// parameter as one of its own consumers and reports 25 where 20 are drawn.
		const int32 Samplers = CountExpressions<UMaterialExpressionTextureSample>(Master)
			- CountExpressions<UMaterialExpressionTextureObjectParameter>(Master);

		TestEqual(*FString::Printf(TEXT("'%s' samples every map through both projections"), *Name),
			Samplers, Maps.Num() * 4);

		for (const FString& Map : Maps)
		{
			UTexture* Texture = nullptr;
			TestTrue(*FString::Printf(TEXT("'%s' exposes a '%s' parameter"), *Name, *Map),
				Master->GetTextureParameterValue(FMaterialParameterInfo(FName(*Map)), Texture));
		}

		// Every map defaults OFF, which is what makes the engine textures they default to inert: the
		// sampler is compiled out, so nothing is fetched until a user assigns their own map. A map
		// switched on by default would sample a white square and quietly wash out the base colour.
		for (const FString& Map : Maps)
		{
			const FString Switch = FString::Printf(TEXT("Use%s"), *Map);
			bool bValue = true;
			FGuid Guid;
			if (TestTrue(*FString::Printf(TEXT("'%s' exposes '%s'"), *Name, *Switch),
				Master->GetStaticSwitchParameterValue(FHashedMaterialParameterInfo(FName(*Switch)), bValue, Guid)))
			{
				TestFalse(*FString::Printf(TEXT("'%s' defaults off, so its default texture is never fetched"),
					*Switch), bValue);
			}
		}

		// The projection mode the brief asked for, and it must default to the mesh's own unwrap:
		// world-aligned triplanar costs three samplers per map and exists for meshes whose UVs cannot
		// be trusted, not as the everyday path.
		bool bWorldAligned = true;
		FGuid Guid;
		if (TestTrue(*FString::Printf(TEXT("'%s' exposes WorldAlignedProjection"), *Name),
			Master->GetStaticSwitchParameterValue(
				FHashedMaterialParameterInfo(TEXT("WorldAlignedProjection")), bWorldAligned, Guid)))
		{
			TestFalse(TEXT("Projection defaults to the mesh's own world-scale UV0"), bWorldAligned);
		}

		// The rest of the promised parameter set. Named individually rather than counted, because the
		// useful failure is which one went missing in a re-author.
		for (const TCHAR* Param : { TEXT("TilingMM"), TEXT("UVWorldSizeCm"), TEXT("TilingRotationDegrees"),
			TEXT("Roughness"), TEXT("Metallic"), TEXT("Specular"), TEXT("NormalMapStrength"),
			TEXT("MacroRoughnessAmount"), TEXT("MacroAlbedoAmount"), TEXT("MacroVariationMM"),
			TEXT("DetailBumpStrength"), TEXT("DetailBumpMM"), TEXT("GroutWidthMM"),
			TEXT("GroutRoughness"), TEXT("TileShadeVariation"), TEXT("EmissiveStrength") })
		{
			float Value = 0.0f;
			TestTrue(*FString::Printf(TEXT("'%s' exposes '%s'"), *Name, Param),
				Master->GetScalarParameterValue(FMaterialParameterInfo(FName(Param)), Value));
		}

		FLinearColor Colour = FLinearColor::White;
		for (const TCHAR* Param : { TEXT("BaseColor"), TEXT("GroutColor"), TEXT("TilingOffsetTiles"),
			TEXT("EmissiveColor") })
		{
			TestTrue(*FString::Printf(TEXT("'%s' exposes '%s'"), *Name, Param),
				Master->GetVectorParameterValue(FMaterialParameterInfo(FName(Param)), Colour));
		}
	}

	return true;
}

/**
 * THE MILLIMETRE PROMISE, ON THE MESH SIDE OF THE BOUNDARY.
 *
 * Tiling is expressed in millimetres only because UV0 is world-scale: FHFMeshOps::ApplyWorldScaleUVs
 * unwraps so that one UV unit is exactly FHFRenderFinish::TexelSizeCm of surface, and the material
 * turns a millimetre figure into a repeat count by dividing into that. Two numbers therefore have to
 * agree across a boundary that nothing connects - the unwrap's TexelSizeCm and the material's
 * UVWorldSizeCm - and if they drift, EVERY tiling figure in the flat is wrong by the same ratio
 * while every individual value still reads as reasonable.
 *
 * This project converts millimetres to centimetres exactly once, at spec ingest, and has been bitten
 * at that boundary before. The material is the second place the two units meet, so it gets the same
 * treatment: assert the agreement rather than trusting two constants to be edited together.
 *
 * The pixels are asserted separately, by HouseForge.Materials.TilingIsInMillimetres, which needs a
 * renderer. What is proven here is everything up to the sampler.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFTilingMatchesUnwrapTest,
	"HouseForge.Materials.TilingMillimetresMatchTheUnwrap", HF_TEST_FLAGS)

bool FHFTilingMatchesUnwrapTest::RunTest(const FString& Parameters)
{
	const double TexelSizeCm = FHFRenderFinish().TexelSizeCm;

	for (const TPair<FString, UMaterial*>& Entry : Masters())
	{
		float UVWorldSizeCm = 0.0f;
		if (TestTrue(*FString::Printf(TEXT("'%s' exposes UVWorldSizeCm"), *Entry.Key),
			ResolvedScalar(Entry.Value, TEXT("UVWorldSizeCm"), UVWorldSizeCm)))
		{
			TestEqual(*FString::Printf(
				TEXT("'%s' agrees with FHFRenderFinish::TexelSizeCm about how big a UV unit is"),
				*Entry.Key), static_cast<double>(UVWorldSizeCm), TexelSizeCm, 0.0001);
		}
	}

	// And the unwrap really does deliver that, measured on generator output rather than assumed.
	// Without this the test above only proves two constants match each other.
	const FHFRoom Room = MakeRoom();
	FDynamicMesh3 Mesh = FHFGenerators::GenerateFloor(Room, 15.0, FHFSkirting::For(Room, {}, {}, {}, {}));

	const FDynamicMeshUVOverlay* UVs = Mesh.HasAttributes() ? Mesh.Attributes()->PrimaryUV() : nullptr;
	if (!TestNotNull(TEXT("A generated floor carries UV0"), UVs))
	{
		return false;
	}

	double WorstErrorCm = 0.0;
	int32 Measured = 0;
	for (const int32 Tid : Mesh.TriangleIndicesItr())
	{
		if (!UVs->IsSetTriangle(Tid))
		{
			continue;
		}

		FVector3d P[3];
		Mesh.GetTriVertices(Tid, P[0], P[1], P[2]);
		FVector2f UV[3];
		UVs->GetTriElements(Tid, UV[0], UV[1], UV[2]);

		for (int32 Edge = 0; Edge < 3; ++Edge)
		{
			const int32 Next = (Edge + 1) % 3;
			const double World = Distance(P[Edge], P[Next]);
			const double InUV = (FVector2d(UV[Next]) - FVector2d(UV[Edge])).Length();

			if (World > 0.01)
			{
				WorstErrorCm = FMath::Max(WorstErrorCm, FMath::Abs(InUV * TexelSizeCm - World));
				++Measured;
			}
		}
	}

	TestTrue(TEXT("There were edges to measure"), Measured > 0);

	// The whole contract in one number: a UV unit is TexelSizeCm of floor along EVERY edge, so a
	// material dividing a millimetre figure into it lands on a real world length.
	TestTrue(*FString::Printf(
		TEXT("One UV unit is %.0f cm of floor along every edge (worst error %.4f cm over %d edges)"),
		TexelSizeCm, WorstErrorCm, Measured), WorstErrorCm < 0.01);

	return true;
}

/**
 * Every role is a finish somebody chose, not a default nobody set.
 *
 * The failure this catches is a role added to EHFSurfaceRole and to the authoring table without its
 * numbers being thought about - it resolves, it is a distinct colour, and it tiles at whatever the
 * master's default happens to be. Given tiling is now load-bearing, a role silently inheriting a
 * one-metre tile is a real defect that nothing else here would see.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFEveryRoleIsTunedTest,
	"HouseForge.Materials.EveryRoleCarriesItsOwnFinish", HF_TEST_FLAGS)

bool FHFEveryRoleIsTunedTest::RunTest(const FString& Parameters)
{
	for (const EHFSurfaceRole Role : AllRoles())
	{
		UMaterialInterface* Material = UHFMaterialLibrary::Get()->ResolveMaterial(Role);
		if (Material == nullptr)
		{
			continue;
		}

		const FString Name = RoleName(Role);

		float Tiling = 0.0f;
		if (TestTrue(*FString::Printf(TEXT("'%s' resolves TilingMM"), *Name),
			ResolvedScalar(Material, TEXT("TilingMM"), Tiling)))
		{
			// Between a fabric weave module and a very large slab. Zero or negative would divide the
			// repeat count by nothing and put the whole surface inside one texel.
			TestTrue(*FString::Printf(TEXT("'%s' tiles at a real size (%.0f mm)"), *Name, Tiling),
				Tiling >= 50.0f && Tiling <= 5000.0f);
		}

		float Grout = 0.0f;
		if (ResolvedScalar(Material, TEXT("GroutWidthMM"), Grout))
		{
			TestTrue(*FString::Printf(TEXT("'%s' has a joint narrower than its own tile"), *Name),
				Grout >= 0.0f && Grout < Tiling * 0.25f);
		}

		// Macro variation is the one thing the research called out as mattering more than any map:
		// a four-metre wall at perfectly uniform roughness is the strongest CG tell there is. Only
		// the roles that are deliberately Lambertian or emissive are allowed to skip it.
		const bool bMayBeUniform = Role == EHFSurfaceRole::Glass || Role == EHFSurfaceRole::LightSource;

		float MacroRoughness = 0.0f;
		if (ResolvedScalar(Material, TEXT("MacroRoughnessAmount"), MacroRoughness) && !bMayBeUniform)
		{
			TestTrue(*FString::Printf(TEXT("'%s' does not render at perfectly uniform roughness"), *Name),
				MacroRoughness > 0.0f);
		}

		float Bump = 0.0f;
		if (ResolvedScalar(Material, TEXT("DetailBumpStrength"), Bump))
		{
			// Amplitude has to stay tiny. At a one-to-three millimetre feature size this only
			// perturbs the specular lobe; turned up it reads as shrink-wrap, which is worse than flat.
			TestTrue(*FString::Printf(TEXT("'%s' keeps its procedural bump subtle (%.3f)"), *Name, Bump),
				Bump >= 0.0f && Bump <= 0.06f);
		}
	}

	return true;
}

/**
 * Glass has to transmit, and a mirror has to reflect. They used to be the same role.
 *
 * Opacity alone was never enough: the old glass passed a translucency check and an opacity check and
 * still read as a boarded hole with a film over it, because nothing about it bent light or absorbed
 * any. Thickness and an index of refraction are what make a pane a pane.
 *
 * And the moment glass became genuinely transmissive, every mirror tagged Glass became a hole
 * through the wall it was hung on - which is why EHFSurfaceRole::Mirror exists and why it is
 * asserted here rather than left to be noticed in a render.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFGlassAndMirrorTest,
	"HouseForge.Materials.GlassTransmitsAndMirrorsDoNot", HF_TEST_FLAGS)

bool FHFGlassAndMirrorTest::RunTest(const FString& Parameters)
{
	UMaterialInterface* Glass = UHFMaterialLibrary::Get()->ResolveMaterial(EHFSurfaceRole::Glass);
	UMaterialInterface* Mirror = UHFMaterialLibrary::Get()->ResolveMaterial(EHFSurfaceRole::Mirror);

	if (!TestNotNull(TEXT("Glass resolves"), Glass) || !TestNotNull(TEXT("Mirror resolves"), Mirror))
	{
		return false;
	}

	TestTrue(TEXT("Glass is translucent"), IsTranslucentBlendMode(Glass->GetBlendMode()));

	float Ior = 0.0f;
	if (TestTrue(TEXT("Glass exposes an index of refraction"),
		ResolvedScalar(Glass, TEXT("IndexOfRefraction"), Ior)))
	{
		// Soda-lime float glass is 1.52. Anything at 1.0 refracts nothing at all, which is the state
		// this replaced: a translucent tint that displaced none of what was behind it.
		TestTrue(*FString::Printf(TEXT("Glass actually bends light (IOR %.2f)"), Ior),
			Ior > 1.3f && Ior < 1.8f);
	}

	float ThicknessMM = 0.0f;
	if (TestTrue(TEXT("Glass exposes a thickness in millimetres"),
		ResolvedScalar(Glass, TEXT("GlassThicknessMM"), ThicknessMM)))
	{
		// 4 mm is a ventilator, 6 mm standard window float, 12 mm a shower screen.
		TestTrue(*FString::Printf(TEXT("Glass is a real pane thickness (%.1f mm)"), ThicknessMM),
			ThicknessMM >= 3.0f && ThicknessMM <= 15.0f);
	}

	// A MIRROR IS NOT GLAZING. Opaque, so it is not a hole; metallic and smooth, so it reflects the
	// room rather than tinting it.
	TestFalse(TEXT("A mirror is opaque"), IsTranslucentBlendMode(Mirror->GetBlendMode()));

	float Metallic = 0.0f;
	if (TestTrue(TEXT("Mirror resolves Metallic"), ResolvedScalar(Mirror, TEXT("Metallic"), Metallic)))
	{
		TestTrue(TEXT("A mirror is a front-surface reflector"), Metallic > 0.9f);
	}

	float Roughness = 1.0f;
	if (TestTrue(TEXT("Mirror resolves Roughness"), ResolvedScalar(Mirror, TEXT("Roughness"), Roughness)))
	{
		TestTrue(*FString::Printf(TEXT("A mirror is smooth enough to be a mirror (%.3f)"), Roughness),
			Roughness < 0.08f);
	}

	// The two must not be the same material by accident of both being shiny and pale.
	TestTrue(TEXT("Glass and a mirror are different materials"), Glass != Mirror);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
