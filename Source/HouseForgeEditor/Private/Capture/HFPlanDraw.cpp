// Copyright Siddartha G. All Rights Reserved.

#include "Capture/HFPlanDraw.h"

#include "Components/DynamicMeshComponent.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Engine/Scene.h"
#include "Geometry/HFMeshOps.h"
#include "Materials/HFMaterialLibrary.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Model/HFTypes.h"

using namespace UE::Geometry;

namespace
{
	/**
	 * THE DRAWING'S TONES, and every one of them is a separation rather than a taste.
	 *
	 * A base-colour capture leaves the ground BLACK - the base pass clears the buffer and nothing was
	 * drawn there - so the palette is read on a black page. That fixes the layout of the scale:
	 *
	 *   0    the page, outside the flat
	 *   64   the floor. Well clear of the page, so the flat's extent reads, and low enough to leave
	 *        the whole upper half of the scale for what stands on it.
	 *   96+  everything standing on the floor, spread by what it is, never closer than 32 levels to
	 *        the floor it sits on. That is the "furniture reads only as faint outlines" complaint,
	 *        answered as a number.
	 *   235  the poche - the faces the cut exposed. The strongest thing on the page, because in a
	 *        plan the cut IS the drawing.
	 *
	 * Vertical surfaces get a tone too even though a plan barely sees them edge-on, because a slot
	 * left empty renders as the engine's default material and a checkerboard in a drawing is worse
	 * than a wrong grey.
	 *
	 * These are stated in sRGB because that is what a reader measures off the PNG. They are converted
	 * once, on the way to the material, for the same reason the finish library converts once.
	 */
	uint8 ToneForRole(EHFSurfaceRole Role)
	{
		switch (Role)
		{
		// The floor and the band cut from the same tile as the floor. One thing, one tone.
		case EHFSurfaceRole::FloorFinish:    return 64;
		case EHFSurfaceRole::Skirting:       return 64;

		// Fitted joinery: the largest thing standing on the floor after the walls.
		case EHFSurfaceRole::JoineryCarcass: return 128;
		case EHFSurfaceRole::ShutterLaminate:return 144;
		case EHFSurfaceRole::CounterStone:   return 112;

		// Openings. A door leaf swung into a room is a thing a plan is read for.
		case EHFSurfaceRole::DoorLeaf:       return 160;
		case EHFSurfaceRole::WindowFrame:    return 176;
		case EHFSurfaceRole::Glass:          return 96;

		case EHFSurfaceRole::Sanitary:       return 200;
		case EHFSurfaceRole::Appliance:      return 184;
		case EHFSurfaceRole::MetalHardware:  return 176;
		case EHFSurfaceRole::Fabric:         return 112;
		case EHFSurfaceRole::Mirror:         return 200;

		// Structure that is NOT cut - a beam over an opening, hanging below the cut plane. Anything
		// the plane passes through is poche instead, which is where a column actually ends up.
		case EHFSurfaceRole::Structure:      return 208;

		// Vertical wall faces, and the three ceiling roles, which live above the cut and are in the
		// table only so no slot is empty.
		case EHFSurfaceRole::WallPaint:      return 150;
		case EHFSurfaceRole::CeilingSoffit:  return 150;
		case EHFSurfaceRole::CoveInterior:   return 150;
		case EHFSurfaceRole::LightSource:    return 150;
		}

		return 150;
	}

	/** The poche, and the floor, named once because the test reads them back. */
	constexpr uint8 GPocheTone = 235;
	constexpr uint8 GFloorTone = 64;

	/**
	 * A flat tone, as a material.
	 *
	 * Off the finish library's own opaque master, so the plan draws through a material this project
	 * already ships and already knows compiles - rather than through an asset that would have to be
	 * authored, checked in and kept in step for the sake of one throwaway render.
	 *
	 * EVERY PATTERN AMOUNT IS ZEROED, and that is the whole difference between this and a role
	 * instance. The master carries grout grids, speckle, macro albedo drift and a detail bump, all of
	 * which modulate BASE COLOUR - which is precisely what a base-colour capture reads. A drawing
	 * wants one number per surface, not a tile pattern, so the pattern is turned off by amount rather
	 * than by static switch: switches cannot be set on a dynamic instance, and an amount of zero
	 * leaves the same shader drawing nothing.
	 */
	UMaterialInstanceDynamic* MakeFlatTone(UObject* Outer, UMaterialInterface* Master, uint8 Tone)
	{
		if (Master == nullptr)
		{
			return nullptr;
		}

		UMaterialInstanceDynamic* Flat = UMaterialInstanceDynamic::Create(Master, Outer);
		if (Flat == nullptr)
		{
			return nullptr;
		}

		Flat->SetVectorParameterValue(TEXT("BaseColor"),
			FLinearColor::FromSRGBColor(FColor(Tone, Tone, Tone, 255)));

		for (const TCHAR* Silenced : { TEXT("MacroAlbedoAmount"), TEXT("MacroRoughnessAmount"),
			TEXT("SpeckleAmount"), TEXT("GroutWidthMM"), TEXT("TileShadeVariation"),
			TEXT("DetailBumpStrength"), TEXT("NormalMapStrength"), TEXT("EmissiveStrength") })
		{
			Flat->SetScalarParameterValue(FName(Silenced), 0.0f);
		}

		return Flat;
	}
}

int32 FHFPlanDraw::PocheSlot()
{
	return FHFMeshOps::NumSurfaceRoles();
}

int32 FHFPlanDraw::SlotCount()
{
	return FHFMeshOps::NumSurfaceRoles() + 1;
}

uint8 FHFPlanDraw::PocheTone()
{
	return GPocheTone;
}

uint8 FHFPlanDraw::FloorTone()
{
	return GFloorTone;
}

uint8 FHFPlanDraw::ToneForSlot(int32 Slot)
{
	if (Slot == PocheSlot())
	{
		return GPocheTone;
	}

	if (Slot < 0 || Slot >= FHFMeshOps::NumSurfaceRoles())
	{
		return 0;
	}

	return ToneForRole(static_cast<EHFSurfaceRole>(Slot));
}

int32 FHFPlanDraw::PocheTheCut(FDynamicMesh3& Mesh, double CutZ)
{
	if (!Mesh.HasAttributes() || !Mesh.Attributes()->HasMaterialID())
	{
		return 0;
	}

	FDynamicMeshMaterialAttribute* Materials = Mesh.Attributes()->GetMaterialID();
	if (Materials == nullptr)
	{
		return 0;
	}

	// Generous against the cut plane and mean about everything else. The cut writes vertices onto the
	// plane exactly, so this only has to survive the double arithmetic that put them there; a
	// millimetre is four orders of magnitude more room than that needs and still an order of
	// magnitude tighter than the thinnest thing in the flat.
	constexpr double PlaneTolerance = 0.1;

	const int32 Slot = PocheSlot();
	int32 Retagged = 0;

	for (const int32 Tid : Mesh.TriangleIndicesItr())
	{
		FVector3d A, B, C;
		Mesh.GetTriVertices(Tid, A, B, C);

		const bool bInPlane =
			FMath::Abs(A.Z - CutZ) <= PlaneTolerance &&
			FMath::Abs(B.Z - CutZ) <= PlaneTolerance &&
			FMath::Abs(C.Z - CutZ) <= PlaneTolerance;

		if (bInPlane)
		{
			Materials->SetValue(Tid, Slot);
			++Retagged;
		}
	}

	return Retagged;
}

void FHFPlanDraw::ApplyPaletteTo(UDynamicMeshComponent* Component)
{
	if (Component == nullptr)
	{
		return;
	}

	UMaterialInterface* Master = LoadObject<UMaterialInterface>(nullptr,
		*UHFMaterialLibrary::MasterPathForShading(EHFFinishShading::Opaque));

	TArray<UMaterialInterface*> Set;
	Set.Reserve(SlotCount());

	for (int32 Slot = 0; Slot < SlotCount(); ++Slot)
	{
		Set.Add(MakeFlatTone(Component, Master, ToneForSlot(Slot)));
	}

	Component->ConfigureMaterialSet(Set, /*bDeleteExtraSlots*/ true);
}

void FHFPlanDraw::ApplyDrawingPostProcess(FPostProcessSettings& Settings)
{
	// THE HALO, BY NAME. Bloom left at the post-process default is what took the clipped white and
	// spread it into the black around the flat.
	Settings.bOverride_BloomIntensity = 1;
	Settings.BloomIntensity = 0.0f;

	Settings.bOverride_BloomThreshold = 1;
	Settings.BloomThreshold = 1.0f;

	// Nothing that adapts. A drawing is the same drawing whatever was in the last frame, and there is
	// no last frame - a capture renders exactly one.
	Settings.bOverride_AutoExposureMethod = 1;
	Settings.AutoExposureMethod = AEM_Manual;

	Settings.bOverride_AutoExposureBias = 1;
	Settings.AutoExposureBias = 0.0f;

	Settings.bOverride_AutoExposureApplyPhysicalCameraExposure = 1;
	Settings.AutoExposureApplyPhysicalCameraExposure = 0;

	// No lens. Every one of these is a way for a bright surface to put light somewhere the geometry
	// is not, which is the entire failure being fixed.
	Settings.bOverride_LensFlareIntensity = 1;
	Settings.LensFlareIntensity = 0.0f;

	Settings.bOverride_VignetteIntensity = 1;
	Settings.VignetteIntensity = 0.0f;

	Settings.bOverride_SceneFringeIntensity = 1;
	Settings.SceneFringeIntensity = 0.0f;

	Settings.bOverride_AmbientCubemapIntensity = 1;
	Settings.AmbientCubemapIntensity = 0.0f;
}
