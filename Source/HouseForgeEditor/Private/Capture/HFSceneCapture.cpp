// Copyright Siddartha G. All Rights Reserved.

#include "Capture/HFSceneCapture.h"

#include "Capture/HFViewingLight.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/PlatformFileManager.h"
#include "HouseForgeEditor.h"
#include "ImageUtils.h"
#include "MaterialShared.h"
#include "Materials/MaterialInterface.h"
#include "Misc/App.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "RHIGlobals.h"
#include "RenderingThread.h"

bool FHFSceneCapture::CanRender(FString& OutWhyNot)
{
	OutWhyNot.Reset();

	if (!FApp::CanEverRender())
	{
		OutWhyNot = TEXT("this process was started without a renderer (-nullrhi or a server target), ")
			TEXT("so nothing can be drawn at all. Run the editor normally - the window may be minimised ")
			TEXT("or covered, which is fine; it is -nullrhi specifically that makes rendering impossible.");
		return false;
	}

	if (GIsCriticalError)
	{
		OutWhyNot = TEXT("the engine is in a critical error state.");
		return false;
	}

	return true;
}

namespace
{
	/** How a material is named in a refusal: the asset path, which is what a reader can go and open. */
	FString DescribeMaterial(const UMaterialInterface* Material)
	{
		return (Material != nullptr) ? Material->GetPathName() : TEXT("(null)");
	}

	/**
	 * Whether this material's shader map is compiled for the platform the capture will render at.
	 *
	 * Deliberately NOT UMaterialInterface::IsComplete(). That answers a different question than it
	 * appears to for a material INSTANCE, which is all of ours: it only consults
	 * StaticPermutationMaterialResources, and an instance that merely overrides parameters has no
	 * static permutation at all - so IsComplete() returns true for MI_HF_Wall without ever looking at
	 * the parent whose shader map actually does the drawing. It would answer "ready" for precisely
	 * the material that is about to render as checkerboard. The engine's own EnsureIsComplete carries
	 * a TODO saying the same thing.
	 *
	 * GetMaterialResource is the accessor that does resolve through the parent, and
	 * IsGameThreadShaderMapComplete is the same flag the renderer's fallback test reads - the render
	 * thread's copy of it is what FMaterialRenderProxy::GetMaterialWithFallback branches on when it
	 * swaps in DefaultMaterial.
	 */
	bool IsShaderMapReady(const UMaterialInterface* Material, EShaderPlatform ShaderPlatform)
	{
		if (Material == nullptr)
		{
			return false;
		}

		const FMaterialResource* Resource = Material->GetMaterialResource(ShaderPlatform);
		return Resource != nullptr && Resource->IsGameThreadShaderMapComplete();
	}
}

TArray<UMaterialInterface*> FHFSceneCapture::GatherRenderedMaterials(UWorld* World, const FHFCaptureRequest& Request)
{
	TArray<UMaterialInterface*> Materials;
	if (World == nullptr)
	{
		return Materials;
	}

	TSet<const UMaterialInterface*> Seen;

	auto TakeFrom = [&Materials, &Seen](const AActor* Actor)
	{
		if (!IsValid(Actor))
		{
			return;
		}

		for (const UActorComponent* Component : Actor->GetComponents())
		{
			const UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Component);

			// Unregistered or hidden components are not drawn, so their materials cannot be
			// substituted for in an image they do not appear in.
			if (Primitive == nullptr || !Primitive->IsRegistered() || !Primitive->IsVisible())
			{
				continue;
			}

			const int32 SlotCount = Primitive->GetNumMaterials();
			for (int32 Slot = 0; Slot < SlotCount; ++Slot)
			{
				UMaterialInterface* Material = Primitive->GetMaterial(Slot);

				// An empty slot is not a missing material - the renderer draws it with the default
				// on purpose, and a component may legitimately have more slots than sections.
				if (Material == nullptr)
				{
					continue;
				}

				bool bAlready = false;
				Seen.Add(Material, &bAlready);
				if (!bAlready)
				{
					Materials.Add(Material);
				}
			}
		}
	};

	if (!Request.ShowOnly.IsEmpty())
	{
		for (const AActor* Actor : Request.ShowOnly)
		{
			TakeFrom(Actor);
		}
	}
	else
	{
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			TakeFrom(*It);
		}
	}

	return Materials;
}

bool FHFSceneCapture::EnsureMaterialsReady(UWorld* World, const FHFCaptureRequest& Request, FString& OutWhyNot)
{
	OutWhyNot.Reset();

	if (World == nullptr)
	{
		return true;
	}

	// The platform this capture will actually draw at, not GMaxRHIFeatureLevel: a scene capture
	// renders through its world's scene, so the world's feature level is the one whose shader map
	// has to exist.
	const EShaderPlatform ShaderPlatform = GetFeatureLevelShaderPlatform(World->GetFeatureLevel());

	const TArray<UMaterialInterface*> Materials = GatherRenderedMaterials(World, Request);

	TArray<FString> Unready;
	int32 Compiled = 0;

	for (UMaterialInterface* Material : Materials)
	{
		if (IsShaderMapReady(Material, ShaderPlatform))
		{
			continue;
		}

		// The engine facility, rather than a sleep or a spin on a frame counter. EnsureIsComplete
		// resubmits the material's outstanding jobs at ForceLocal priority and blocks on them, which
		// is what the engine itself does before drawing a material to a render target
		// (UKismetRenderingLibrary::DrawMaterialToRenderTarget) for this exact reason. Sleeping would
		// be both slower and unsound - nothing guarantees the job was ever submitted.
		Material->EnsureIsComplete();
		++Compiled;

		if (!IsShaderMapReady(Material, ShaderPlatform))
		{
			Unready.Add(DescribeMaterial(Material));
		}
	}

	if (!Unready.IsEmpty())
	{
		OutWhyNot = FString::Printf(
			TEXT("%d material(s) still have no compiled shader map after waiting for compilation: %s. ")
			TEXT("Rendering now would silently substitute the engine's DefaultMaterial for them and write ")
			TEXT("a grey checkerboard image that looks like a real capture. Fix or reimport the material(s) ")
			TEXT("and capture again."),
			Unready.Num(), *FString::Join(Unready, TEXT(", ")));
		return false;
	}

	if (Compiled > 0)
	{
		// The completeness flag just checked is the GAME thread's. The renderer branches on the
		// render thread's copy, which is published by a command queued behind us, so without this the
		// very frame we are about to capture could still be drawn with the fallback.
		FlushRenderingCommands();

		UE_LOG(LogHouseForgeEditor, Log,
			TEXT("Capture waited for %d of %d material(s) to finish compiling before rendering."),
			Compiled, Materials.Num());
	}

	return true;
}

bool FHFSceneCapture::Render(UWorld* World, const FHFCaptureRequest& Request, FIntPoint& OutSize, FString& OutError)
{
	OutSize = FIntPoint::ZeroValue;
	OutError.Reset();

	if (World == nullptr)
	{
		OutError = TEXT("there is no world to capture.");
		return false;
	}

	if (!CanRender(OutError))
	{
		return false;
	}

	// Before anything is drawn, and refusing on the same terms as -nullrhi does above. A capture
	// that cannot be correct must not be written: an image of the wrong thing is acted on, whereas a
	// refusal is read.
	if (!EnsureMaterialsReady(World, Request, OutError))
	{
		return false;
	}

	const int32 Width = FMath::Clamp(Request.Width, 64, 8192);
	const int32 Height = FMath::Clamp(Request.Height, 64, 8192);

	UTextureRenderTarget2D* Target = NewObject<UTextureRenderTarget2D>(GetTransientPackage(), NAME_None, RF_Transient);
	Target->RenderTargetFormat = RTF_RGBA8_SRGB;
	Target->ClearColor = FLinearColor::Black;
	Target->bAutoGenerateMips = false;
	Target->InitAutoFormat(Width, Height);
	Target->UpdateResourceImmediate(true);

	// A bare actor to hang the capture component on. Transient and hidden from the outliner: it
	// exists for the duration of one render and a user should never see it appear in their level.
	FActorSpawnParameters SpawnParams;
	SpawnParams.ObjectFlags = RF_Transient;
	SpawnParams.bHideFromSceneOutliner = true;
	SpawnParams.bTemporaryEditorActor = true;

	AActor* Rig = World->SpawnActor<AActor>(AActor::StaticClass(),
		FTransform(Request.Rotation, Request.Location), SpawnParams);
	if (Rig == nullptr)
	{
		OutError = TEXT("could not create a camera in the level.");
		return false;
	}

	ON_SCOPE_EXIT
	{
		if (IsValid(Rig))
		{
			World->DestroyActor(Rig);
		}
	};

	USceneCaptureComponent2D* Capture = NewObject<USceneCaptureComponent2D>(Rig, NAME_None, RF_Transient);
	Rig->SetRootComponent(Capture);
	Capture->RegisterComponent();
	Capture->SetWorldLocationAndRotation(Request.Location, Request.Rotation);

	Capture->TextureTarget = Target;
	Capture->CaptureSource = SCS_FinalColorLDR;

	// One frame, taken when we ask for it. Left on, this component would render every tick of the
	// editor for as long as it existed, which for a 4096-square target is a visible cost for no
	// reason at all.
	Capture->bCaptureEveryFrame = false;
	Capture->bCaptureOnMovement = false;
	Capture->bAlwaysPersistRenderingState = true;

	if (Request.bOrthographic)
	{
		Capture->ProjectionType = ECameraProjectionMode::Orthographic;
		Capture->OrthoWidth = static_cast<float>(FMath::Max(Request.OrthoWidth, 1.0));
	}
	else
	{
		Capture->ProjectionType = ECameraProjectionMode::Perspective;
		Capture->FOVAngle = static_cast<float>(FMath::Clamp(Request.FieldOfViewDegrees, 5.0, 170.0));
	}

	if (!Request.ShowOnly.IsEmpty())
	{
		Capture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
		for (AActor* Actor : Request.ShowOnly)
		{
			if (IsValid(Actor))
			{
				Capture->ShowOnlyActors.Add(Actor);
			}
		}
	}

	if (!Request.bShowSky)
	{
		Capture->ShowFlags.SetAtmosphere(false);
		Capture->ShowFlags.SetFog(false);
		Capture->ShowFlags.SetVolumetricFog(false);
	}

	// Exposure and ambient fill, both pinned rather than left to the scene.
	//
	// Eye adaptation is the first half: a capture renders exactly one frame, and adaptation needs a
	// previous frame to adapt FROM, so the one frame that gets written would be exposed by an empty
	// adaptation buffer and come back black. The ambient fill is the second: a scene capture runs
	// no global illumination, so the sky light lights nothing inside an enclosed room and an
	// interior view is black for an entirely different reason. Both figures come from the
	// placeholder rig, so a capture and the editor viewport agree.
	FHFViewingLight::ApplyViewingSettingsTo(Capture->PostProcessSettings);
	Capture->PostProcessBlendWeight = 1.0f;

	Capture->CaptureScene();

	FTextureRenderTargetResource* Resource = Target->GameThread_GetRenderTargetResource();
	if (Resource == nullptr)
	{
		OutError = TEXT("the render target has no resource to read back.");
		return false;
	}

	TArray<FColor> Pixels;
	if (!Resource->ReadPixels(Pixels) || Pixels.Num() != Width * Height)
	{
		OutError = FString::Printf(
			TEXT("read back %d pixels from a %dx%d render target, which is not a complete image."),
			Pixels.Num(), Width, Height);
		return false;
	}

	// The scene renders without a meaningful alpha channel. Left as it comes, a PNG of a perfectly
	// good render is saved fully transparent and reads as an empty image in every viewer.
	for (FColor& Pixel : Pixels)
	{
		Pixel.A = 255;
	}

	const FString Directory = FPaths::GetPath(Request.OutputPath);
	if (!Directory.IsEmpty())
	{
		FPlatformFileManager::Get().GetPlatformFile().CreateDirectoryTree(*Directory);
	}

	FImage Image;
	Image.Init(Width, Height, ERawImageFormat::BGRA8, EGammaSpace::sRGB);
	FMemory::Memcpy(Image.RawData.GetData(), Pixels.GetData(), Pixels.Num() * sizeof(FColor));

	if (!FImageUtils::SaveImageByExtension(*Request.OutputPath, Image))
	{
		OutError = FString::Printf(TEXT("could not write '%s'."), *Request.OutputPath);
		return false;
	}

	OutSize = FIntPoint(Width, Height);
	return true;
}
