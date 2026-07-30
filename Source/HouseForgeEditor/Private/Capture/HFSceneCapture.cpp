// Copyright Siddartha G. All Rights Reserved.

#include "Capture/HFSceneCapture.h"

#include "Capture/HFViewingLight.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "HAL/PlatformFileManager.h"
#include "HouseForgeEditor.h"
#include "ImageUtils.h"
#include "Misc/App.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"

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

	// Never left to eye adaptation. A capture renders exactly one frame, and adaptation needs a
	// previous frame to adapt from - so the one frame that gets written is exposed by an empty
	// adaptation buffer and comes back black. This pins it to the figure the placeholder rig is
	// balanced around.
	FHFViewingLight::ApplyExposureTo(Capture->PostProcessSettings);
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
