// Copyright Siddartha G. All Rights Reserved.

#include "Materials/HFMaterialLibrary.h"

#include "Components/DynamicMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Geometry/HFMeshOps.h"
#include "HouseForge.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInterface.h"
#include "Misc/PackageName.h"
#include "Model/HFSettings.h"
#include "UObject/UObjectGlobals.h"

#if WITH_EDITOR
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialParameters.h"
#endif

namespace
{
	/**
	 * Loaded role instances, indexed by role. Strong pointers, because nothing else holds a reference
	 * to these until a component takes one and a garbage collect between two generations would
	 * otherwise make every element re-load the set.
	 */
	TArray<TStrongObjectPtr<UMaterialInterface>> GRoleMaterials;

	/** Roles already warned about, so a missing asset logs once rather than once per element. */
	TSet<EHFSurfaceRole> GWarnedRoles;

	/** The shipped library, once found. Cached for the same reason the instances are. */
	TStrongObjectPtr<UHFMaterialLibrary> GShippedLibrary;

	// =========================================================================================
	//
	// THE DEFAULT TABLE: what each of the eighteen surfaces of an Indian residential flat IS.
	//
	// Compiled in rather than shipped only as an asset, so a project with no library asset at all
	// still generates a flat that looks like a flat. The class default object is built from this,
	// and UHFMaterialLibrary::Get falls back to it.
	//
	// COLOURS ARE QUOTED IN sRGB and converted once, because a paint chart and a tile catalogue are
	// quoted in sRGB and a linear triple is unreadable as a colour.
	//
	// The same numbers author the MI_HF_* instances - Scripts/gen_materials.py builds the graphs and
	// the instances, this table says what goes in them - and
	// HouseForge.Materials.LibraryAndItsInstancesAgree measures the two against each other so they
	// cannot drift apart in silence.
	//
	// =========================================================================================

	/** Reads as the python authoring table does: name the field you mean, inherit the rest. */
	struct FFinishBuilder
	{
		FHFSurfaceFinish F;

		FFinishBuilder(const TCHAR* Description, float R, float G, float B, float Roughness)
		{
			F.Description = Description;
			F.BaseColor = FHFSurfaceFinish::FromSRGB(R, G, B);
			F.Roughness = Roughness;
		}

		FFinishBuilder& Metal(float V) { F.Metallic = V; return *this; }
		FFinishBuilder& Spec(float V) { F.Specular = V; return *this; }
		FFinishBuilder& Coat(float W, float Rough) { F.CoatWeight = W; F.CoatRoughness = Rough; return *this; }
		FFinishBuilder& Tiling(float MM) { F.TilingMM = MM; return *this; }
		FFinishBuilder& Macro(float Rough, float Albedo, float MM)
		{
			F.MacroRoughnessAmount = Rough; F.MacroAlbedoAmount = Albedo; F.MacroVariationMM = MM;
			return *this;
		}
		FFinishBuilder& Bump(float Strength, float MM)
		{
			F.DetailBumpStrength = Strength; F.DetailBumpMM = MM; return *this;
		}
		FFinishBuilder& NoBump() { F.bUseProceduralBump = false; F.DetailBumpStrength = 0.0f; return *this; }
		FFinishBuilder& Grout(float WidthMM, float R, float G, float B, float Rough)
		{
			F.GroutWidthMM = WidthMM; F.GroutColor = FHFSurfaceFinish::FromSRGB(R, G, B);
			F.GroutRoughness = Rough; return *this;
		}
		FFinishBuilder& ShadeVariation(float V) { F.TileShadeVariation = V; return *this; }
		FFinishBuilder& Speckle(float Amount, float MM, float R, float G, float B)
		{
			F.SpeckleAmount = Amount; F.SpeckleSizeMM = MM;
			F.SpeckleColor = FHFSurfaceFinish::FromSRGB(R, G, B); return *this;
		}
		FFinishBuilder& Glazed(float InOpacity)
		{
			F.Shading = EHFFinishShading::Glazed; F.Opacity = InOpacity; return *this;
		}
		FFinishBuilder& Emits(float Strength) { F.EmissiveColor = F.BaseColor; F.EmissiveStrength = Strength; return *this; }

		operator FHFSurfaceFinish() const { return F; }
	};

	/** The table, in enum order, built once. */
	const TArray<FHFSurfaceFinish>& DefaultFinishes()
	{
		static const TArray<FHFSurfaceFinish> Table = []
		{
			TArray<FHFSurfaceFinish> T;
			T.Reserve(18);

			// WallPaint. THE LARGEST SURFACE BY AREA IN THE RENDER, which is why the macro drift is
			// turned up above every other role: a four-metre wall at uniform roughness is the
			// strongest CG tell there is, and it is the cheapest one to fix.
			T.Add(FFinishBuilder(TEXT("Acrylic emulsion over gypsum putty on POP or cement plaster - Asian ")
				TEXT("Paints Tractor/Royale class, matt. Warm ivory, never pure white: a real emulsion sits ")
				TEXT("around 0.72-0.78 linear albedo."), 0.902f, 0.886f, 0.859f, 0.85f)
				.Macro(0.045f, 0.012f, 1200.0f).Bump(0.022f, 2.0f));

			// FloorFinish. The highest-gloss large surface in the flat and THE priority role. The
			// coat is the physics: a glazed body really is a rough-ish ceramic under a thin smooth
			// glaze. The joint's ROUGHNESS contrast reads far more strongly than its colour, which is
			// why the grout roughness is double the field.
			T.Add(FFinishBuilder(TEXT("Double-charge or GVT vitrified tile - Kajaria, Somany, Johnson - ")
				TEXT("600x600, glossy polished, laid with 2 mm spacers. Under Lumen this albedo tints ")
				TEXT("every bounce in the room."), 0.847f, 0.824f, 0.784f, 0.35f)
				.Spec(0.55f).Tiling(600.0f).Coat(0.6f, 0.08f)
				.Grout(2.0f, 0.722f, 0.698f, 0.659f, 0.70f)
				.Macro(0.020f, 0.006f, 1500.0f).ShadeVariation(0.020f));

			// CeilingSoffit. What sells a false ceiling is the AO in its step and the cove shadow,
			// not texture, so little is spent here. High albedo matters: this returns the uplight.
			T.Add(FFinishBuilder(TEXT("POP or gypsum board, trowelled and painted matt white distemper. ")
				TEXT("Matter than the walls - POP takes paint flatter than putty."),
				0.941f, 0.933f, 0.918f, 0.90f)
				.Macro(0.025f, 0.008f, 700.0f).Bump(0.004f, 40.0f));

			// CoveInterior. DELIBERATELY THE BRIGHTEST AND MATTEST SURFACE IN THE SET. Any gloss here
			// gives a hot streak reflection of the strip instead of a soft wash, and any albedo drop
			// kills the cove's throw.
			T.Add(FFinishBuilder(TEXT("The inside face of the cove pocket: the surface an LED strip washes ")
				TEXT("and the surface Lumen bounces that wash off. A lighting decision wearing a ")
				TEXT("material's clothes."), 0.957f, 0.949f, 0.933f, 0.92f)
				.Spec(0.35f).Macro(0.010f, 0.004f, 1500.0f).NoBump());

			// Skirting. Matches FloorFinish exactly, coat included. NO GRID: a 100 mm band cut from a
			// 600 mm tile shows a joint only where the floor's own joint runs into it, and drawing one
			// on the band is the tell that it was authored as a separate object.
			T.Add(FFinishBuilder(TEXT("Almost always the floor tile cut down to a 75-100 mm band rather than ")
				TEXT("a material of its own. Occasionally PVC or wood in bedrooms."),
				0.847f, 0.824f, 0.784f, 0.35f)
				.Spec(0.55f).Tiling(600.0f).Coat(0.6f, 0.08f).Macro(0.020f, 0.006f, 1500.0f));

			// JoineryCarcass. Seen mostly as the inside of a wardrobe and the sides of a base unit,
			// lit indirectly, so its job is to be a believable warm neutral rather than to be looked at.
			T.Add(FFinishBuilder(TEXT("Pre-laminated particle board or BWR ply carcass, matt to satin."),
				0.788f, 0.729f, 0.635f, 0.62f)
				.Tiling(1200.0f).Macro(0.025f, 0.010f, 1500.0f).Bump(0.004f, 10.0f));

			// ShutterLaminate. THE COAT IS THE POINT: a gloss shutter is a pigmented base under a
			// thick clear layer. With roughness alone it reads as painted metal.
			T.Add(FFinishBuilder(TEXT("High-gloss acrylic or post-laminated shutter fronts."),
				0.310f, 0.396f, 0.388f, 0.42f)
				.Spec(0.55f).Tiling(1200.0f).Coat(0.5f, 0.10f)
				.Macro(0.015f, 0.005f, 1500.0f).Bump(0.0015f, 30.0f));

			// CounterStone. A SPECKLE, NOT A VEIN, and that is a deliberate refusal: procedural
			// Statuario veining is camouflage every time, while a speckle genuinely is statistical.
			//
			// The speckle is ADDED, and it had to become its own mechanism before the intent could be
			// delivered at all. It was 11% of MacroAlbedoAmount at a 15 mm wavelength, which on a base
			// of 0.0222 linear came to +/-0.0024 - invisible - and the role rendered as grey powder-
			// coated metal. A bronzite fleck is bright grain ON black, so it is added light at a grain
			// size, and the macro drift goes back to being the slow sheen wash it is everywhere else.
			T.Add(FFinishBuilder(TEXT("Speckled granite - Black Galaxy or Steel Grey - polished but not ")
				TEXT("mirror. The coat carries the polish; the base carries the stone."),
				0.161f, 0.161f, 0.176f, 0.30f)
				.Spec(0.60f).Tiling(400.0f).Coat(0.5f, 0.09f)
				.Macro(0.050f, 0.010f, 900.0f).Speckle(0.075f, 5.0f, 0.827f, 0.784f, 0.706f)
				.Bump(0.010f, 1.5f));

			// Glass. THE ONE TRANSMISSIVE ROLE. A pane drawn opaque reads as a boarded-up hole; drawn
			// as flat translucency it reads as a plastic film.
			T.Add(FFinishBuilder(TEXT("Soda-lime float glass in a window or a glazed shutter. Real thickness ")
				TEXT("in the geometry, real refraction and Beer-Lambert absorption in the material."),
				0.925f, 0.965f, 0.949f, 0.02f)
				.Spec(1.0f).Glazed(0.06f).Macro(0.0f, 0.0f, 1500.0f).NoBump());

			// MetalHardware. Rough enough to be satin rather than a mirror: a mirror-finish handle in
			// an untextured room reflects nothing and reads as a grey blob.
			T.Add(FFinishBuilder(TEXT("Chrome-plated brass and satin stainless: handles, hinges, taps, rails."),
				0.706f, 0.714f, 0.722f, 0.24f)
				.Metal(1.0f).Tiling(200.0f).Macro(0.030f, 0.0f, 120.0f).Bump(0.008f, 1.0f));

			// DoorLeaf. FLAT TONE, CORRECT GLOSS, A FAINT PORE BUMP, AND NOTHING ELSE. Procedural wood
			// grain is the uncanny middle: real veneer figure is authored structure that noise cannot
			// produce, and stretched noise reads worse than an honest brown at the right sheen.
			T.Add(FFinishBuilder(TEXT("Flush door, membrane or veneered, semi-gloss PU. No procedural grain ")
				TEXT("- that is refused by name; a real veneer albedo is what would add figure."),
				0.478f, 0.325f, 0.216f, 0.45f)
				.Spec(0.5f).Tiling(900.0f).Coat(0.3f, 0.14f)
				.Macro(0.030f, 0.020f, 800.0f).Bump(0.015f, 3.0f));

			// WindowFrame. ORANGE PEEL AT 20-40 MM is the entire visual signature of powder coat, and
			// exactly the kind of statistical micro-relief noise is right for.
			T.Add(FFinishBuilder(TEXT("Powder-coated aluminium sliding window sections. Metallic under the ")
				TEXT("coat, which is what makes a section read as aluminium and not grey plastic."),
				0.290f, 0.298f, 0.310f, 0.38f)
				.Metal(1.0f).Tiling(300.0f).Macro(0.020f, 0.010f, 1500.0f).Bump(0.0015f, 30.0f));

			// Sanitary. A glaze over a body - one slab, two lobes, physically what the object is.
			// Sanitaryware with no coat reads as painted plaster.
			T.Add(FFinishBuilder(TEXT("Vitreous china: WC, basin, cistern. A hard glaze over a matt body."),
				0.965f, 0.965f, 0.957f, 0.30f)
				.Spec(0.5f).Tiling(600.0f).Coat(0.7f, 0.05f).Macro(0.008f, 0.004f, 1500.0f));

			// Fabric. The weave bump is the closest a non-cloth shading model gets to the
			// grazing-angle sheen that is cloth's real signature. NO PRINT: a motif is authored.
			T.Add(FFinishBuilder(TEXT("Upholstery, curtains, mattress ticking, bedding. Very rough, with a ")
				TEXT("weave bump at about a millimetre."), 0.522f, 0.463f, 0.408f, 0.95f)
				.Spec(0.2f).Tiling(150.0f).Macro(0.020f, 0.025f, 400.0f).Bump(0.045f, 1.2f));

			// Appliance.
			T.Add(FFinishBuilder(TEXT("Fridge, hob, chimney, washing machine: painted steel and brushed ")
				TEXT("stainless panels."), 0.741f, 0.749f, 0.757f, 0.26f)
				.Metal(1.0f).Tiling(400.0f).Coat(0.3f, 0.10f)
				.Macro(0.018f, 0.010f, 1500.0f).Bump(0.0015f, 25.0f));

			// Structure. One shade cooler and greyer than the walls so a dropped beam is legible as
			// structure rather than as a fold in the wall.
			T.Add(FFinishBuilder(TEXT("Exposed structure - beams and columns - in plastered RCC."),
				0.678f, 0.671f, 0.655f, 0.88f)
				.Macro(0.040f, 0.012f, 1500.0f).Bump(0.014f, 2.5f));

			// LightSource. THE ONE ROLE THAT EMITS. Warm white at 3000 K, which is what these flats
			// are lit with, a stop below where bloom takes the frame - the wash on the slab is the
			// subject, not the strip.
			T.Add(FFinishBuilder(TEXT("A surface that emits: the LED strip lying in a cove, the lens up ")
				TEXT("inside a downlight can. 3000 K warm white."), 1.0f, 0.894f, 0.769f, 0.35f)
				.Macro(0.0f, 0.0f, 1500.0f).NoBump().Emits(12.0f));

			// Mirror. A front-surface reflector: opaque, metallic, almost perfectly smooth, very
			// slightly warm because silver is. Its bevel is geometry, not material.
			T.Add(FFinishBuilder(TEXT("Silvered glass: a bathroom or wardrobe mirror plate. Opaque and ")
				TEXT("metallic - the opposite of Glass optically, which is why it is its own role."),
				0.972f, 0.960f, 0.915f, 0.02f)
				.Metal(1.0f).Spec(1.0f).Macro(0.004f, 0.0f, 1500.0f).NoBump());

			return T;
		}();

		return Table;
	}
}

UHFMaterialLibrary::UHFMaterialLibrary()
{
	ResetToCompiledDefaults();
}

void UHFMaterialLibrary::ResetToCompiledDefaults()
{
	const TArray<FHFSurfaceFinish>& Table = DefaultFinishes();

	Finishes.Reset();
	Finishes.Reserve(Table.Num());
	for (int32 Index = 0; Index < Table.Num(); ++Index)
	{
		Finishes.Add(static_cast<EHFSurfaceRole>(Index), Table[Index]);
	}
}

FString UHFMaterialLibrary::InstanceNameForRole(EHFSurfaceRole Role)
{
	// The enumerator's own name, so the asset set and the enum cannot drift apart silently. Adding
	// a role without authoring its material makes HouseForge.Materials.EveryRoleResolvesToAMaterial
	// fail by name, which is a far better signal than a room that renders one surface in checkerboard.
	return FString::Printf(TEXT("MI_HF_%s"),
		*StaticEnum<EHFSurfaceRole>()->GetNameStringByValue(static_cast<int64>(Role)));
}

FString UHFMaterialLibrary::AssetPathForRole(EHFSurfaceRole Role)
{
	const FString Name = InstanceNameForRole(Role);
	return FString::Printf(TEXT("%s/%s.%s"), MaterialFolder(), *Name, *Name);
}

FString UHFMaterialLibrary::ShippedAssetPath()
{
	return FString::Printf(TEXT("%s/DA_HF_MaterialLibrary.DA_HF_MaterialLibrary"), MaterialFolder());
}

FString UHFMaterialLibrary::MasterPathForShading(EHFFinishShading Shading)
{
	const TCHAR* const Name = (Shading == EHFFinishShading::Glazed)
		? TEXT("M_HF_SurfaceGlazed")
		: TEXT("M_HF_Surface");

	return FString::Printf(TEXT("%s/%s.%s"), MaterialFolder(), Name, Name);
}

UHFMaterialLibrary* UHFMaterialLibrary::Get()
{
	if (const UHFSettings* Settings = GetDefault<UHFSettings>())
	{
		if (!Settings->MaterialLibrary.IsNull())
		{
			if (UHFMaterialLibrary* Configured = Settings->MaterialLibrary.LoadSynchronous())
			{
				return Configured;
			}

			// Named and missing is worth saying out loud. Falling through silently would look
			// identical to never having configured one.
			static bool bWarnedAboutSetting = false;
			if (!bWarnedAboutSetting)
			{
				bWarnedAboutSetting = true;
				UE_LOG(LogHouseForge, Warning,
					TEXT("HouseForge material library '%s' could not be loaded; falling back."),
					*Settings->MaterialLibrary.ToString());
			}
		}
	}

	// The shipped asset, cached like the role instances are and for the same reason: this is asked
	// for once per element per generation, and a garbage collect between two houses would otherwise
	// make every element re-load it.
	if (GShippedLibrary.IsValid())
	{
		return GShippedLibrary.Get();
	}

	if (UHFMaterialLibrary* Shipped = LoadObject<UHFMaterialLibrary>(nullptr, *ShippedAssetPath()))
	{
		GShippedLibrary = TStrongObjectPtr<UHFMaterialLibrary>(Shipped);
		return Shipped;
	}

	// No content at all. Still a working library, because the table is compiled in.
	return GetMutableDefault<UHFMaterialLibrary>();
}

const FHFSurfaceFinish& UHFMaterialLibrary::DefaultFinishForRole(EHFSurfaceRole Role)
{
	const TArray<FHFSurfaceFinish>& Table = DefaultFinishes();
	const int32 Index = FHFMeshOps::MaterialIdForRole(Role);

	if (Table.IsValidIndex(Index))
	{
		return Table[Index];
	}

	// A role in the enum with no row in the table. Structurally impossible while
	// HouseForge.Materials.EveryRoleHasALibraryFinish passes, and this is what it fails on.
	static const FHFSurfaceFinish Blank;
	return Blank;
}

const FHFSurfaceFinish& UHFMaterialLibrary::FinishForRole(EHFSurfaceRole Role) const
{
	if (const FHFSurfaceFinish* Found = Finishes.Find(Role))
	{
		return *Found;
	}
	return DefaultFinishForRole(Role);
}

FString UHFMaterialLibrary::InstancePathForRole(EHFSurfaceRole Role) const
{
	const FString Name = InstanceNameForRole(Role);

	// A LIBRARY OWNS THE INSTANCES BESIDE IT, and only the shipped library owns the shipped ones.
	//
	// The path used to be the plugin's own Materials folder unconditionally, whichever library was
	// resolving. That made the header's promise - a job's own finishes, in the job's own project -
	// half true at best: the VALUES were per project and the assets they rendered through were one
	// shared set inside the plugin, on last-writer-wins. It also meant every colour tweak dirtied
	// files in the plugin's own git repository, which Rule 01 keeps separate from user output for
	// exactly this reason.
	//
	// Falling back to the shipped folder matters as much as looking beside the library first: a
	// project that points at its own library without having authored eighteen instances next to it
	// still renders, through the plugin's set, rather than turning the flat grey.
	if (const UPackage* Package = GetOutermost())
	{
		const FString Folder = FPackageName::GetLongPackagePath(Package->GetName());
		if (!Folder.IsEmpty() && Folder != MaterialFolder())
		{
			const FString Beside = FString::Printf(TEXT("%s/%s.%s"), *Folder, *Name, *Name);
			if (FPackageName::DoesPackageExist(FString::Printf(TEXT("%s/%s"), *Folder, *Name)))
			{
				return Beside;
			}
		}
	}

	return AssetPathForRole(Role);
}

UMaterialInterface* UHFMaterialLibrary::ResolveMaterial(EHFSurfaceRole Role) const
{
	const int32 Index = FHFMeshOps::MaterialIdForRole(Role);
	const int32 Count = FHFMeshOps::NumSurfaceRoles();
	if (Index < 0 || Index >= Count)
	{
		return nullptr;
	}

	// WHICH LIBRARY FILLED THE CACHE IS NOW PART OF THE CACHE, because which instance a role resolves
	// to depends on it - see InstancePathForRole. Keyed by role alone, switching a project to its own
	// library would have gone on rendering through the previous one's instances until a restart.
	if (const UPackage* Package = GetOutermost())
	{
		static FString CacheOwner;
		if (CacheOwner != Package->GetName())
		{
			CacheOwner = Package->GetName();
			GRoleMaterials.Reset();
			GWarnedRoles.Reset();
		}
	}

	if (GRoleMaterials.Num() != Count)
	{
		GRoleMaterials.SetNum(Count);
	}

	if (GRoleMaterials[Index].IsValid())
	{
		return GRoleMaterials[Index].Get();
	}

	const FString Path = InstancePathForRole(Role);
	UMaterialInterface* Loaded = LoadObject<UMaterialInterface>(nullptr, *Path);

	if (Loaded == nullptr)
	{
		bool bAlreadyWarned = false;
		GWarnedRoles.Add(Role, &bAlreadyWarned);
		if (!bAlreadyWarned)
		{
			UE_LOG(LogHouseForge, Warning,
				TEXT("No material for surface role '%s' at '%s'; that role will render as the default material."),
				*StaticEnum<EHFSurfaceRole>()->GetNameStringByValue(static_cast<int64>(Role)), *Path);
		}
		return nullptr;
	}

	GRoleMaterials[Index] = TStrongObjectPtr<UMaterialInterface>(Loaded);
	return Loaded;
}

TArray<UMaterialInterface*> UHFMaterialLibrary::ResolveMaterialSet() const
{
	const int32 Count = FHFMeshOps::NumSurfaceRoles();

	TArray<UMaterialInterface*> Set;
	Set.Reserve(Count);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		Set.Add(ResolveMaterial(static_cast<EHFSurfaceRole>(Index)));
	}
	return Set;
}

void UHFMaterialLibrary::ApplyTo(UDynamicMeshComponent* Component) const
{
	if (Component == nullptr)
	{
		return;
	}

	const TArray<UMaterialInterface*> Set = ResolveMaterialSet();

	// ConfigureMaterialSet rather than a SetMaterial loop with bDeleteExtraSlots: the slot count is
	// fixed at one per role, so an element regenerated after a role was removed from the enum must
	// shed the slot rather than keep a dangling one that the scene proxy would still allocate a
	// render section for.
	Component->ConfigureMaterialSet(Set, /*bDeleteExtraSlots*/ true);
}

void UHFMaterialLibrary::ApplyTo(UStaticMeshComponent* Component) const
{
	if (Component == nullptr || Component->GetStaticMesh() == nullptr)
	{
		return;
	}

	const TArray<UMaterialInterface*> Set = ResolveMaterialSet();

	// Bounded by the asset's own slot count rather than by the role count. They are equal on anything
	// HouseForge bakes - FHFBakeService asks for NumSurfaceRoles() slots - but a user who re-imported
	// over the asset, or a future role added to the enum after an old bake, must not send this past
	// the end of the material array and assert.
	const int32 Slots = Component->GetStaticMesh()->GetStaticMaterials().Num();
	for (int32 Index = 0; Index < Slots; ++Index)
	{
		Component->SetMaterial(Index, Set.IsValidIndex(Index) ? Set[Index] : nullptr);
	}
}

void UHFMaterialLibrary::InvalidateCache()
{
	GRoleMaterials.Reset();
	GWarnedRoles.Reset();
	GShippedLibrary.Reset();
}

#if WITH_EDITOR

namespace
{
	/**
	 * Writes a static switch ONLY WHERE IT DIFFERS FROM THE PARENT'S OWN VALUE.
	 *
	 * Not tidiness. A static switch override - even one whose value equals the parent's - makes the
	 * instance's static parameter set differ from an empty one, and UpdateStaticPermutation compares
	 * exactly that to decide whether to recache shaders. Writing all seven switches unconditionally
	 * would mean a shader compile on the first push to every role, for switches nobody changed.
	 */
	void SetSwitchIfDifferent(FMaterialInstanceParameterUpdateContext& Ctx, UMaterialInterface* Parent,
		const TCHAR* Name, bool bWanted)
	{
		const FMaterialParameterInfo Info(Name);

		bool bParentValue = false;
		FGuid Unused;
		const bool bParentHasIt = Parent != nullptr
			&& Parent->GetStaticSwitchParameterValue(FHashedMaterialParameterInfo(Info), bParentValue, Unused);

		if (bParentHasIt && bParentValue == bWanted)
		{
			return;
		}

		FMaterialParameterMetadata Meta;
		Meta.Value = FMaterialParameterValue(bWanted);
		Ctx.SetParameterValueEditorOnly(Info, Meta);
	}

	/**
	 * Where the numeric half of a finish is written to.
	 *
	 * An interface with two implementations, rather than two copies of the parameter list, because
	 * the whole hazard of a two-tier update is the tiers disagreeing about what a finish is: a value
	 * written on release but not during the drag makes a surface change the instant the mouse comes
	 * up, and the user reads that as the control being broken rather than as a missing line.
	 */
	struct FHFParameterSink
	{
		virtual ~FHFParameterSink() = default;
		virtual void Scalar(const TCHAR* Name, float Value) = 0;
		virtual void Vector(const TCHAR* Name, const FLinearColor& Value) = 0;
	};

	/** Commit path: through the engine's own update context, the way the Material Instance Editor does. */
	struct FHFUpdateContextSink final : FHFParameterSink
	{
		FMaterialInstanceParameterUpdateContext& Ctx;

		explicit FHFUpdateContextSink(FMaterialInstanceParameterUpdateContext& InCtx) : Ctx(InCtx) {}

		virtual void Scalar(const TCHAR* Name, float Value) override
		{
			FMaterialParameterMetadata Meta;
			Meta.Value = FMaterialParameterValue(Value);
			Ctx.SetParameterValueEditorOnly(FMaterialParameterInfo(Name), Meta);
		}

		virtual void Vector(const TCHAR* Name, const FLinearColor& Value) override
		{
			FMaterialParameterMetadata Meta;
			Meta.Value = FMaterialParameterValue(Value);
			Ctx.SetParameterValueEditorOnly(FMaterialParameterInfo(Name), Meta);
		}
	};

	/** Interactive path: straight onto the instance, with the render-side flush left to the caller. */
	struct FHFDirectSink final : FHFParameterSink
	{
		UMaterialInstanceConstant& Instance;

		explicit FHFDirectSink(UMaterialInstanceConstant& InInstance) : Instance(InInstance) {}

		virtual void Scalar(const TCHAR* Name, float Value) override
		{
			Instance.SetScalarParameterValueEditorOnly(FMaterialParameterInfo(Name), Value);
		}

		virtual void Vector(const TCHAR* Name, const FLinearColor& Value) override
		{
			Instance.SetVectorParameterValueEditorOnly(FMaterialParameterInfo(Name), Value);
		}
	};

	void SetTexture(FMaterialInstanceParameterUpdateContext& Ctx, const TCHAR* Name, UTexture* Value)
	{
		if (Value == nullptr)
		{
			// Left at the parent's default, which is inert: the matching UseXMap switch is off, so
			// the sampler is compiled out and the texture is never fetched.
			return;
		}

		FMaterialParameterMetadata Meta;
		Meta.Value = FMaterialParameterValue(Value);
		Ctx.SetParameterValueEditorOnly(FMaterialParameterInfo(Name), Meta);
	}

	/**
	 * The numeric half of a finish: everything that is a uniform expression rather than a switch.
	 *
	 * ONE LIST, WRITTEN ONCE. Both update tiers come through here, so neither can carry a parameter
	 * the other does not.
	 */
	void WriteNumericParameters(FHFParameterSink& Sink, const FHFSurfaceFinish& Finish)
	{
		Sink.Vector(TEXT("BaseColor"), Finish.BaseColor);
		Sink.Scalar(TEXT("Roughness"), Finish.Roughness);
		Sink.Scalar(TEXT("Metallic"), Finish.Metallic);
		Sink.Scalar(TEXT("Specular"), Finish.Specular);

		Sink.Scalar(TEXT("TilingMM"), Finish.TilingMM);
		Sink.Scalar(TEXT("TilingRotationDegrees"), Finish.TilingRotationDegrees);
		Sink.Vector(TEXT("TilingOffsetTiles"), FLinearColor(
			static_cast<float>(Finish.TilingOffsetTiles.X),
			static_cast<float>(Finish.TilingOffsetTiles.Y), 0.0f, 0.0f));

		Sink.Scalar(TEXT("NormalMapStrength"), Finish.NormalMapStrength);

		Sink.Scalar(TEXT("MacroRoughnessAmount"), Finish.MacroRoughnessAmount);
		Sink.Scalar(TEXT("MacroAlbedoAmount"), Finish.MacroAlbedoAmount);
		Sink.Scalar(TEXT("MacroVariationMM"), Finish.MacroVariationMM);
		Sink.Scalar(TEXT("DetailBumpStrength"), Finish.DetailBumpStrength);
		Sink.Scalar(TEXT("DetailBumpMM"), Finish.DetailBumpMM);

		Sink.Scalar(TEXT("SpeckleAmount"), Finish.SpeckleAmount);
		Sink.Scalar(TEXT("SpeckleSizeMM"), Finish.SpeckleSizeMM);
		Sink.Vector(TEXT("SpeckleColor"), Finish.SpeckleColor);

		Sink.Scalar(TEXT("GroutWidthMM"), Finish.GroutWidthMM);
		Sink.Vector(TEXT("GroutColor"), Finish.GroutColor);
		Sink.Scalar(TEXT("GroutRoughness"), Finish.GroutRoughness);
		Sink.Scalar(TEXT("TileShadeVariation"), Finish.TileShadeVariation);

		Sink.Vector(TEXT("EmissiveColor"), Finish.EmissiveColor);
		Sink.Scalar(TEXT("EmissiveStrength"), Finish.EmissiveStrength);

		// Only one of these two blocks exists on any given parent. Writing the other would leave an
		// override for a pin the master has not got - see EHFFinishShading.
		if (Finish.Shading == EHFFinishShading::Glazed)
		{
			Sink.Scalar(TEXT("Opacity"), Finish.Opacity);
			Sink.Scalar(TEXT("IndexOfRefraction"), Finish.IndexOfRefraction);
			Sink.Scalar(TEXT("GlassThicknessMM"), Finish.GlassThicknessMM);
		}
		else
		{
			Sink.Scalar(TEXT("CoatWeight"), Finish.CoatWeight);
			Sink.Scalar(TEXT("CoatRoughness"), Finish.CoatRoughness);
		}
	}
}

bool UHFMaterialLibrary::PushFinish(EHFSurfaceRole Role, EHFMaterialPush Mode) const
{
	UMaterialInstanceConstant* Instance = Cast<UMaterialInstanceConstant>(ResolveMaterial(Role));
	if (Instance == nullptr)
	{
		return false;
	}

	const FHFSurfaceFinish& Finish = FinishForRole(Role);

	if (Mode == EHFMaterialPush::Interactive)
	{
		// MID-GESTURE. Scalars and vectors ARE uniform expressions, so writing the values and
		// recaching them is the whole job: no shader map changes, no static permutation, no draw-list
		// rebuild, and no rendering-thread flush across 155 primitives per mouse-move.
		//
		// Deliberately NOT PostEditChange and NOT MarkPackageDirty. A gesture is not a decision until
		// it ends, and PostEditChange's FMaterialUpdateContext defaults to a render-state recreate
		// plus a rendering-thread sync across every primitive using the material.
		//
		// Deliberately no textures and no static switches either: those cost a shader compile, and a
		// control that silently costs seconds mid-drag reads as a hang. They land on release.
		FHFDirectSink Sink(*Instance);
		WriteNumericParameters(Sink, Finish);

		Instance->RecacheUniformExpressions(/*bRecreateUniformBuffer*/ false);
		return true;
	}

	// THE PARENT FIRST, BECAUSE EVERY PARAMETER BELOW IS MEANINGLESS AGAINST THE WRONG ONE.
	//
	// Shading is an editable property, so a user can turn FloorFinish from Opaque to Glazed in the
	// panel. WriteNumericParameters then stops writing CoatWeight and CoatRoughness and starts
	// writing Opacity, IndexOfRefraction and GlassThicknessMM - parameters the opaque master has no
	// pins for. Left unreparented, that click removed the clear coat from the single most important
	// surface in the flat and put nothing in its place, with no message and nothing to see until the
	// next render. See MasterPathForShading.
	//
	// Commit only. Reparenting rebuilds the static permutation and compiles shaders, which is exactly
	// what must never happen inside a drag - and Shading is a combo box, so it only ever arrives here.
	if (UMaterialInterface* const Wanted =
		LoadObject<UMaterialInterface>(nullptr, *MasterPathForShading(Finish.Shading)))
	{
		if (Instance->Parent != Wanted)
		{
			Instance->SetParentEditorOnly(Wanted, /*bRecacheShaders*/ true);
		}
	}

	{
		// EMaterialInstanceClearParameterFlag::All, so the instance ends up being exactly this
		// finish. Anything previously overridden on the instance and no longer named here goes back
		// to the master's default rather than surviving as a value nothing in the library explains.
		FMaterialInstanceParameterUpdateContext Ctx(Instance, EMaterialInstanceClearParameterFlag::All);

		FHFUpdateContextSink Sink(Ctx);
		WriteNumericParameters(Sink, Finish);

		UTexture* const Albedo = Finish.AlbedoMap.LoadSynchronous();
		UTexture* const Rough = Finish.RoughnessMap.LoadSynchronous();
		UTexture* const Metal = Finish.MetallicMap.LoadSynchronous();
		UTexture* const AO = Finish.AOMap.LoadSynchronous();
		UTexture* const Normal = Finish.NormalMap.LoadSynchronous();

		SetTexture(Ctx, TEXT("AlbedoMap"), Albedo);
		SetTexture(Ctx, TEXT("RoughnessMap"), Rough);
		SetTexture(Ctx, TEXT("MetallicMap"), Metal);
		SetTexture(Ctx, TEXT("AOMap"), AO);
		SetTexture(Ctx, TEXT("NormalMap"), Normal);

		// A map slot decides its own switch. There is no separate "use it" tickbox to get out of step
		// with the slot, which is a class of bug rather than a saving.
		UMaterialInterface* const Parent = Instance->Parent;
		SetSwitchIfDifferent(Ctx, Parent, TEXT("UseAlbedoMap"), Albedo != nullptr);
		SetSwitchIfDifferent(Ctx, Parent, TEXT("UseRoughnessMap"), Rough != nullptr);
		SetSwitchIfDifferent(Ctx, Parent, TEXT("UseMetallicMap"), Metal != nullptr);
		SetSwitchIfDifferent(Ctx, Parent, TEXT("UseAOMap"), AO != nullptr);
		SetSwitchIfDifferent(Ctx, Parent, TEXT("UseNormalMap"), Normal != nullptr);

		SetSwitchIfDifferent(Ctx, Parent, TEXT("UseProceduralBump"), Finish.bUseProceduralBump);
		SetSwitchIfDifferent(Ctx, Parent, TEXT("WorldAlignedProjection"), Finish.bWorldAlignedProjection);

		// The destructor applies the static set. UpdateStaticPermutation compares before recaching,
		// so a push that changed no switch costs no compile.
	}

	Instance->PostEditChange();
	Instance->MarkPackageDirty();
	return true;
}

int32 UHFMaterialLibrary::PushAllFinishes(EHFMaterialPush Mode) const
{
	int32 Written = 0;
	for (int32 Index = 0; Index < FHFMeshOps::NumSurfaceRoles(); ++Index)
	{
		Written += PushFinish(static_cast<EHFSurfaceRole>(Index), Mode) ? 1 : 0;
	}
	return Written;
}

void UHFMaterialLibrary::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// A drag gets the cheap path and a release gets the full one. See EHFMaterialPush for why the
	// difference is worth having rather than always doing the correct-looking thing.
	const EHFMaterialPush Mode = (PropertyChangedEvent.ChangeType == EPropertyChangeType::Interactive)
		? EHFMaterialPush::Interactive
		: EHFMaterialPush::Commit;

	PushAllFinishes(Mode);
}

#endif // WITH_EDITOR
