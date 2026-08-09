// Copyright Siddartha G. All Rights Reserved.

#include "Capture/HFLumenCoverage.h"

#include "Actors/HFElementActors.h"
#include "Components/DynamicMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"

namespace
{
	/**
	 * The console object, looked up once.
	 *
	 * Cached because Judge runs per primitive and a flat is ~350 of them: the first version looked
	 * the card threshold up every time and the engine's own console manager complained about it
	 * ("Performance warning: ... shows many (500) FindConsoleObject() calls"). Caching the POINTER
	 * and not the value is what keeps the test scope in HFLumenCoverageTests honest - it sets the
	 * variable through the same object, so the reads below still see the change.
	 */
	IConsoleVariable* CVar(const TCHAR* Name)
	{
		return IConsoleManager::Get().FindConsoleVariable(Name);
	}

	/** A cvar's integer value, or the stated default when the cvar does not exist in this build. */
	int32 ValueOr(const IConsoleVariable* Variable, int32 Fallback)
	{
		return (Variable != nullptr) ? Variable->GetInt() : Fallback;
	}

	/**
	 * FLumenSceneData::AddMeshCardsFromBuildData's orthogonality test, copied rather than approximated.
	 *
	 * LumenMeshCards.cpp's IsMatrixOrthogonal is a file-static, so it cannot be called; it is short
	 * enough to reproduce exactly, and reproducing it exactly is the point - a shear tolerance
	 * invented here would put the guard and the renderer on different sides of a marginal transform.
	 */
	bool IsMatrixOrthogonal(const FMatrix& Matrix)
	{
		const FVector MatrixScale = Matrix.GetScaleVector();
		if (MatrixScale.GetAbsMin() < UE_KINDA_SMALL_NUMBER)
		{
			return false;
		}

		FVector AxisX;
		FVector AxisY;
		FVector AxisZ;
		Matrix.GetUnitAxes(AxisX, AxisY, AxisZ);

		return FMath::Abs(AxisX | AxisY) < UE_KINDA_SMALL_NUMBER
			&& FMath::Abs(AxisX | AxisZ) < UE_KINDA_SMALL_NUMBER
			&& FMath::Abs(AxisY | AxisZ) < UE_KINDA_SMALL_NUMBER;
	}

	/** Surface area of a box of this size. The weight behind "how much of the flat is missing". */
	double BoxSurfaceArea(const FVector& Size)
	{
		return 2.0 * (Size.X * Size.Y + Size.Y * Size.Z + Size.Z * Size.X);
	}

	/**
	 * The largest face of the mesh's own bounds under the component's scale.
	 *
	 * Deliberately the LOCAL bounds times the scale rather than the world bounding box, because that
	 * is what AddMeshCardsFromBuildData measures: MeshCardsBuildData.Bounds.GetSize() *
	 * LocalToWorldScale. A rotated wall's world box is bigger than its own box, and using it would
	 * quietly raise every rotated element over the threshold.
	 */
	double LargestFaceOf(const FVector& LocalSize, const FVector& Scale)
	{
		const FVector Scaled(LocalSize.X * FMath::Abs(Scale.X),
			LocalSize.Y * FMath::Abs(Scale.Y),
			LocalSize.Z * FMath::Abs(Scale.Z));

		return FMath::Max3(Scaled.Y * Scaled.Z, Scaled.X * Scaled.Z, Scaled.X * Scaled.Y);
	}

	/**
	 * Whether this component would draw anything at all.
	 *
	 * const_cast because UDynamicMeshComponent::GetDynamicMesh is non-const, and every caller here
	 * only reads: ProcessMesh takes a lambda over a `const FDynamicMesh3&` and is the engine's own
	 * read-only accessor. Casting the constness away at the one line that needs it is honest about
	 * where the engine's API is not const-correct, and keeps it out of the inspector's signatures -
	 * which do promise not to touch the level they are judging.
	 */
	bool HasAnyTriangles(const UDynamicMeshComponent* Component)
	{
		UDynamicMeshComponent* Mutable = const_cast<UDynamicMeshComponent*>(Component);
		if (Mutable == nullptr || Mutable->GetDynamicMesh() == nullptr)
		{
			return false;
		}

		int32 Triangles = 0;
		Mutable->GetDynamicMesh()->ProcessMesh(
			[&Triangles](const UE::Geometry::FDynamicMesh3& Mesh) { Triangles = Mesh.TriangleCount(); });

		return Triangles > 0;
	}
}

const TCHAR* FHFLumenCoverage::VerdictName(EHFLumenVerdict Verdict)
{
	switch (Verdict)
	{
	case EHFLumenVerdict::Radiant:                return TEXT("radiant");
	case EHFLumenVerdict::TooSmallForCards:       return TEXT("too small for cards");
	case EHFLumenVerdict::LiveDynamicMesh:        return TEXT("live dynamic mesh - not baked");
	case EHFLumenVerdict::NoDistanceField:        return TEXT("asset has no distance field");
	case EHFLumenVerdict::IndirectLightingOff:    return TEXT("indirect lighting switched off");
	case EHFLumenVerdict::NonOrthogonalTransform: return TEXT("sheared or zero-scaled transform");
	default:                                      return TEXT("unrecognised primitive");
	}
}

double FHFLumenCoverage::CardMinFaceAreaCm2()
{
	// r.LumenScene.SurfaceCache.MeshCardsMinSize, default 10, squared - LumenMeshCards.cpp:130.
	static IConsoleVariable* Variable = CVar(TEXT("r.LumenScene.SurfaceCache.MeshCardsMinSize"));

	const double MinSize = (Variable != nullptr) ? Variable->GetFloat() : 10.0;
	return MinSize * MinSize;
}

bool FHFLumenCoverage::IsCandidate(const UPrimitiveComponent* Primitive)
{
	if (!IsValid(Primitive) || !Primitive->IsRegistered())
	{
		return false;
	}

	// Editor gizmos, sprites and billboards are not the building. They are not in a game frame and
	// they are not what a render is judged on.
	if (Primitive->IsEditorOnly() || Primitive->IsVisualizationComponent())
	{
		return false;
	}

	// USceneComponent::IsVisible() is !bHiddenInGame && GetVisibleFlag(), which is exactly what the
	// scene proxy's DrawInGame flag is initialised from (PrimitiveSceneProxy.cpp:318 through
	// FPrimitiveSceneProxyDesc::bIsVisible). So this is the renderer's own term, not editor
	// visibility - and it is what correctly removes the hidden dynamic twin of a baked element.
	return Primitive->IsVisible() || (Primitive->bAffectDynamicIndirectLighting && Primitive->bAffectIndirectLightingWhileHidden);
}

EHFLumenVerdict FHFLumenCoverage::Judge(const UPrimitiveComponent* Primitive, double& OutLargestFaceAreaCm2)
{
	OutLargestFaceAreaCm2 = 0.0;

	if (!IsValid(Primitive))
	{
		return EHFLumenVerdict::Unknown;
	}

	// bAffectsLumen in UpdateVisibleInLumenScene is AffectsDynamicIndirectLighting() and nothing
	// else, so this one flag removes a primitive from the Lumen scene on both tracing paths.
	// bAffectDistanceFieldLighting only bites on the software path, but a project can be switched
	// between them by one cvar and a render must not depend on which is set today.
	if (!Primitive->bAffectDynamicIndirectLighting || !Primitive->bAffectDistanceFieldLighting)
	{
		return EHFLumenVerdict::IndirectLightingOff;
	}

	if (const UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(Primitive))
	{
		const UStaticMesh* Mesh = StaticMeshComponent->GetStaticMesh();
		if (Mesh == nullptr)
		{
			// Nothing is drawn, so nothing is missing - InspectElements drops these before they are
			// counted either way. Answering Radiant rather than Unknown keeps Judge safe to call on
			// its own, which is what a test that pins one component's verdict does.
			return EHFLumenVerdict::Radiant;
		}

#if WITH_EDITORONLY_DATA
		// The per-asset half of the distance field decision. The project cvar is an OR with
		// UStaticMesh::bGenerateMeshDistanceField (StaticMesh.cpp:4564), so the per-asset flag can
		// only opt IN - but DistanceFieldResolutionScale at zero opts OUT unconditionally, and that is
		// the value UE::AssetUtils writes when FStaticMeshAssetOptions::bAllowDistanceField is false.
		if (Mesh->GetNumSourceModels() > 0
			&& Mesh->GetSourceModel(0).BuildSettings.DistanceFieldResolutionScale <= 0.0f)
		{
			return EHFLumenVerdict::NoDistanceField;
		}
#endif

		const FMatrix LocalToWorld = StaticMeshComponent->GetComponentTransform().ToMatrixWithScale();
		if (!IsMatrixOrthogonal(LocalToWorld))
		{
			return EHFLumenVerdict::NonOrthogonalTransform;
		}

		OutLargestFaceAreaCm2 = LargestFaceOf(Mesh->GetBoundingBox().GetSize(),
			StaticMeshComponent->GetComponentScale());

		return (OutLargestFaceAreaCm2 > CardMinFaceAreaCm2())
			? EHFLumenVerdict::Radiant
			: EHFLumenVerdict::TooSmallForCards;
	}

	if (const UDynamicMeshComponent* DynamicMeshComponent = Cast<UDynamicMeshComponent>(Primitive))
	{
		// An empty part is not an absent one. FHFBakedPart::bSourceWasEmpty exists for the same case:
		// a wall whose openings have eaten all of it produces no triangles, and no amount of baking
		// will put light into a room from a mesh that is not there.
		if (!HasAnyTriangles(DynamicMeshComponent))
		{
			return EHFLumenVerdict::Radiant;
		}

		const FBoxSphereBounds LocalBounds = DynamicMeshComponent->CalcLocalBounds();
		OutLargestFaceAreaCm2 = LargestFaceOf(LocalBounds.GetBox().GetSize(),
			DynamicMeshComponent->GetComponentScale());

		return EHFLumenVerdict::LiveDynamicMesh;
	}

	return EHFLumenVerdict::Unknown;
}

FString FHFLumenPrimitive::Describe() const
{
	return FString::Printf(TEXT("%s.%s (%s): %s"),
		*ElementId.ToString(), *ComponentName, *ElementClass,
		FHFLumenCoverage::VerdictName(Verdict));
}

double FHFLumenCoverageReport::CoverageFraction() const
{
	const double Total = AreaRadiantCm2 + AreaAbsentCm2;
	return (Total > 0.0) ? (AreaRadiantCm2 / Total) : 1.0;
}

bool FHFLumenCoverageReport::IsCovered() const
{
	if (!IsApplicable())
	{
		return true;
	}

	// Radiant > 0 as well as Absent == 0, because an empty level trivially has nothing absent and
	// "there is no house" must not read as "the house is covered". A capture of an empty level is a
	// different problem, but it is not this one's job to call it a success.
	return ProjectProblems.IsEmpty() && Absent == 0 && Radiant > 0;
}

FString FHFLumenCoverageReport::Summary() const
{
	return FString::Printf(
		TEXT("Lumen coverage: %d of %d drawn primitives radiant (%.1f%% of surface area), %d too small to card, %d absent, across %d elements. GI method %s, %s tracing."),
		Radiant, PrimitivesDrawn, CoverageFraction() * 100.0, TooSmall, Absent, ElementsSeen,
		bLumenIsTheGiMethod ? TEXT("Lumen") : TEXT("not Lumen"),
		bHardwareRayTracing ? TEXT("hardware") : TEXT("software"));
}

FString FHFLumenCoverageReport::WhyNot() const
{
	if (IsCovered())
	{
		return FString();
	}

	TArray<FString> Lines;

	Lines.Add(TEXT("THE FLAT IS NOT IN THE LUMEN SCENE, so a render of it will be wrong - and wrong in ")
		TEXT("the direction that looks fine. Measured on this project: the broken configuration renders ")
		TEXT("BRIGHTER than the correct one (whole-frame luminance 0.260 against 0.083), because sky ")
		TEXT("light floods through walls Lumen cannot see. Do not judge this by eye."));

	Lines.Add(Summary());

	for (const FString& Problem : ProjectProblems)
	{
		Lines.Add(FString::Printf(TEXT("PROJECT: %s"), *Problem));
	}

	if (Radiant == 0 && Absent == 0 && ProjectProblems.IsEmpty())
	{
		Lines.Add(TEXT("Nothing was found to check - there is no HouseForge geometry drawn in this level."));
	}

	// Grouped by cause first, because 150 identical lines saying "not baked" is a wall of text that
	// hides the one line saying "and this one has its indirect lighting switched off".
	for (const TPair<EHFLumenVerdict, int32>& Pair : AbsentByVerdict)
	{
		Lines.Add(FString::Printf(TEXT("%d primitive(s): %s"),
			Pair.Value, FHFLumenCoverage::VerdictName(Pair.Key)));
	}

	// Named examples, capped. A list of every wall in the flat is not more informative than five of
	// them, and it is much harder to read.
	const int32 NameCount = FMath::Min(Absentees.Num(), 5);
	for (int32 Index = 0; Index < NameCount; ++Index)
	{
		Lines.Add(FString::Printf(TEXT("  e.g. %s"), *Absentees[Index].Describe()));
	}
	if (Absentees.Num() > NameCount)
	{
		Lines.Add(FString::Printf(TEXT("  ... and %d more"), Absentees.Num() - NameCount));
	}

	if (AbsentByVerdict.Contains(EHFLumenVerdict::LiveDynamicMesh))
	{
		Lines.Add(TEXT("REMEDY: bake the house. A UDynamicMeshComponent gets no mesh cards and so no ")
			TEXT("surface cache, on either tracing path; a baked UStaticMesh gets both. The bake is ")
			TEXT("reversible and keeps the live mesh - run HouseForge's BakeHouse tool, or set ")
			TEXT("RenderMode to Baked on the elements named above."));
	}

	return FString::Join(Lines, TEXT("\n"));
}

void FHFLumenCoverage::InspectElements(TArrayView<AHFElementActor* const> Elements, FHFLumenCoverageReport& OutReport)
{
	OutReport = FHFLumenCoverageReport();

	// ---------------------------------------------------------------- the project-level half
	//
	// Checked even when there is no geometry, because these settings defeat a perfectly good bake and
	// the resulting render is indistinguishable from the unbaked one.

	static IConsoleVariable* GiMethod = CVar(TEXT("r.DynamicGlobalIlluminationMethod"));
	static IConsoleVariable* DistanceFields = CVar(TEXT("r.GenerateMeshDistanceFields"));
	static IConsoleVariable* MeshCards = CVar(TEXT("r.MeshCardRepresentation"));
	static IConsoleVariable* HardwareRayTracing = CVar(TEXT("r.Lumen.HardwareRayTracing"));

	OutReport.bLumenIsTheGiMethod = ValueOr(GiMethod, 1) == 1;
	OutReport.bProjectGeneratesDistanceFields = ValueOr(DistanceFields, 0) != 0;
	OutReport.bProjectGeneratesMeshCards = ValueOr(MeshCards, 1) != 0;
	OutReport.bHardwareRayTracing = ValueOr(HardwareRayTracing, 0) != 0;

	if (OutReport.bLumenIsTheGiMethod && !OutReport.bProjectGeneratesDistanceFields)
	{
		OutReport.ProjectProblems.Add(TEXT("r.GenerateMeshDistanceFields is 0, so no static mesh in ")
			TEXT("the project gets a distance field. The mesh card build is chained off the distance ")
			TEXT("field build, so baking cannot help until this is on. Set it in Project Settings > ")
			TEXT("Rendering > Generate Mesh Distance Fields and restart the editor."));
	}

	if (OutReport.bLumenIsTheGiMethod && !OutReport.bProjectGeneratesMeshCards)
	{
		OutReport.ProjectProblems.Add(TEXT("r.MeshCardRepresentation is 0, so no mesh cards are built ")
			TEXT("and the surface cache stays empty however well the flat is baked."));
	}

	// ---------------------------------------------------------------- the per-primitive half

	for (AHFElementActor* Element : Elements)
	{
		if (!IsValid(Element))
		{
			continue;
		}

		++OutReport.ElementsSeen;

		for (const UActorComponent* ActorComponent : Element->GetComponents())
		{
			const UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(ActorComponent);
			if (!IsCandidate(Primitive))
			{
				continue;
			}

			// A component holding no geometry draws nothing, whatever its visibility flag says, so it
			// is not a candidate after all. Two real cases, neither of them a defect: a baked
			// component while the element shows its live mesh - ApplyRenderMode CLEARS the asset
			// rather than hiding the component, for tool-targeting reasons - and a part whose
			// generator legitimately produced nothing (FHFBakedPart::bSourceWasEmpty).
			const UStaticMeshComponent* AsStatic = Cast<UStaticMeshComponent>(Primitive);
			if (AsStatic != nullptr && AsStatic->GetStaticMesh() == nullptr)
			{
				continue;
			}

			const UDynamicMeshComponent* AsDynamic = Cast<UDynamicMeshComponent>(Primitive);
			if (AsDynamic != nullptr && !HasAnyTriangles(AsDynamic))
			{
				continue;
			}

			double LargestFace = 0.0;
			const EHFLumenVerdict Verdict = Judge(Primitive, LargestFace);

			++OutReport.PrimitivesDrawn;

			FHFLumenPrimitive Record;
			Record.Component = Primitive;
			Record.ElementId = Element->ElementId;
			Record.ComponentName = Primitive->GetName();
			Record.ElementClass = Element->GetClass()->GetName();
			Record.Verdict = Verdict;
			Record.LargestFaceAreaCm2 = LargestFace;
			Record.SurfaceAreaCm2 = BoxSurfaceArea(Primitive->Bounds.GetBox().GetSize());

			switch (Verdict)
			{
			case EHFLumenVerdict::Radiant:
				++OutReport.Radiant;
				OutReport.AreaRadiantCm2 += Record.SurfaceAreaCm2;
				break;

			case EHFLumenVerdict::TooSmallForCards:
				// Counted, never held against the flat. The engine will not card a 10 cm face at any
				// size of asset, so a guard that failed on these would fail on every door handle in
				// the building and teach its user to ignore it.
				++OutReport.TooSmall;
				break;

			default:
				++OutReport.Absent;
				OutReport.AreaAbsentCm2 += Record.SurfaceAreaCm2;
				OutReport.Absentees.Add(Record);
				OutReport.AbsentByVerdict.FindOrAdd(Verdict) += 1;
				break;
			}
		}
	}

	// Biggest first, so the five named examples are the five that matter rather than the five that
	// happened to be spawned first.
	OutReport.Absentees.Sort([](const FHFLumenPrimitive& A, const FHFLumenPrimitive& B)
	{
		return A.SurfaceAreaCm2 > B.SurfaceAreaCm2;
	});
}

void FHFLumenCoverage::Inspect(UWorld* World, FHFLumenCoverageReport& OutReport)
{
	TArray<AHFElementActor*> Elements;

	if (World != nullptr)
	{
		for (TActorIterator<AHFElementActor> It(World); It; ++It)
		{
			Elements.Add(*It);
		}
	}

	InspectElements(Elements, OutReport);
}
