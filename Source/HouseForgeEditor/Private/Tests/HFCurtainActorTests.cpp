// Copyright Siddartha G. All Rights Reserved.

#include "HouseForgeEditor.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Actors/HFCurtainActor.h"
#include "Actors/HFElementActors.h"
#include "Actors/HFHouseActor.h"
#include "Actors/HFTrimActors.h"
#include "Components/DynamicMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Model/HFSampleHouse.h"
#include "Model/HFTypes.h"
#include "UDynamicMesh.h"

#define HF_TEST_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The curtains in the flat, rather than on the bench.
 *
 * HouseForge.Curtain.* already measures the cloth as a kit: the fullness integral, the fold that is
 * exactly one repeat less its hairline, the aperture at both ends of the travel. All of it is
 * arithmetic over parameters a test chose, and every figure in it was right while the flat's own
 * curtains hung 71 mm inside a bed - because the composing layer picks the drop, the pelmet picks
 * the depth, and the room picks whether there is any floor to hang to. NONE OF THOSE THREE EXIST ON
 * A BENCH.
 *
 * So this file asks the same questions of the four curtains that are actually in the flat, against
 * the pelmets they actually hang in and the floors they actually stop above:
 *
 *   1. Does it COVER the window when drawn, and CLEAR it when open - in centimetres of aperture,
 *      sampled off the posed world geometry.
 *   2. Is the cloth inside its pelmet, front to back, at every open amount? A curtain standing
 *      through its own fascia is the defect the 65 mm track setback was measured to prevent.
 *   3. Does the drawn-back STACK stay on the track, rather than fouling the reveal or the wall?
 *   4. Does the hem clear the floor?
 *
 * Every one of them is a length in centimetres. Not one of them asks whether a fold moved: the
 * master bedroom's sliding wardrobe travelled its full 118.45 cm on both leaves and never opened by
 * a millimetre, and a curtain pair is the same shape of object.
 */
namespace HouseForgeCurtainActors
{
	using namespace UE::Geometry;

	void ClearHouseForgeActors(UWorld* World)
	{
		TArray<AActor*> Doomed;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (It->IsA(AHFHouseActor::StaticClass()) || It->IsA(AHFElementActor::StaticClass()))
			{
				Doomed.Add(*It);
			}
		}
		for (AActor* Actor : Doomed)
		{
			if (IsValid(Actor))
			{
				Actor->Destroy();
			}
		}
	}

	AHFHouseActor* BuildReferenceFlat(UWorld* World, FHFHouseSpec& OutSpec)
	{
		ClearHouseForgeActors(World);

		AHFHouseActor* House = World->SpawnActor<AHFHouseActor>();
		if (House == nullptr)
		{
			return nullptr;
		}

		House->SetSpec(FHFSampleHouse::Make2BHK());
		House->BuildGeometry();

		// The house's own spec, which is in CENTIMETRES: SetSpec converts exactly once, at ingest.
		OutSpec = House->Spec;
		return House;
	}

	TArray<AHFCurtainActor*> CurtainsIn(const AHFHouseActor* House)
	{
		TArray<AHFCurtainActor*> Out;
		for (const TObjectPtr<AActor>& Actor : House->ElementActors)
		{
			if (AHFCurtainActor* Curtain = Cast<AHFCurtainActor>(Actor))
			{
				Out.Add(Curtain);
			}
		}
		return Out;
	}

	/**
	 * The pelmet this curtain hangs in, found the way the composing layer found it: by coincidence
	 * in plan.
	 *
	 * A curtain and its pelmet are one line on a drawing, so the nearest pelmet in plan IS the one -
	 * and matching that way rather than on a stored id is deliberate. An id would be a second
	 * statement of the same relationship, and the point of this file is to check the fitting rather
	 * than to check that two fields agree.
	 */
	AHFPelmetActor* PelmetFor(const AHFHouseActor* House, const AHFCurtainActor* Curtain)
	{
		AHFPelmetActor* Best = nullptr;
		double BestDistance = TNumericLimits<double>::Max();

		const FVector Here = Curtain->GetActorLocation();

		for (const TObjectPtr<AActor>& Actor : House->ElementActors)
		{
			AHFPelmetActor* Pelmet = Cast<AHFPelmetActor>(Actor);
			if (!IsValid(Pelmet))
			{
				continue;
			}

			const double Distance = FVector::Dist2D(Pelmet->GetActorLocation(), Here);
			if (Distance < BestDistance)
			{
				BestDistance = Distance;
				Best = Pelmet;
			}
		}

		// A metre in plan is far more than the coincidence allows and far less than the gap to the
		// next pelmet in the flat, so this either finds the right one or finds nothing.
		return BestDistance <= 100.0 ? Best : nullptr;
	}

	/** Every vertex of the curtain's cloth, fixed and posed alike, in the CURTAIN'S own frame. */
	TArray<FVector> ClothPoints(const AHFCurtainActor* Curtain)
	{
		TArray<FVector> Out;

		const FTransform ToLocal = Curtain->GetActorTransform().Inverse();

		auto Take = [&Out, &ToLocal](UDynamicMeshComponent* Component)
		{
			if (Component == nullptr || Component->GetDynamicMesh() == nullptr)
			{
				return;
			}

			const FDynamicMesh3& Mesh = Component->GetDynamicMesh()->GetMeshRef();
			const FTransform ToWorld = Component->GetComponentTransform();

			for (const int32 Vid : Mesh.VertexIndicesItr())
			{
				Out.Add(ToLocal.TransformPosition(ToWorld.TransformPosition(FVector(Mesh.GetVertex(Vid)))));
			}
		};

		Take(Curtain->GetMeshComponent());
		for (const TObjectPtr<UDynamicMeshComponent>& Part : Curtain->GetPartComponents())
		{
			Take(Part.Get());
		}

		return Out;
	}

	/**
	 * The widest unbroken run of track with no cloth in front of it, in centimetres. THE APERTURE.
	 *
	 * Measured on the POSED components at a height well down the drop, where a person looking out of
	 * the window is actually looking. Widest unbroken run rather than total uncovered, because a
	 * curtain that gathered into clumps with daylight between them would score well on the second and
	 * still be a curtain nobody can see through.
	 */
	double ApertureCm(const AHFCurtainActor* Curtain, double AtZ)
	{
		const double Clear = Curtain->Curtain.ClearWidth();
		const double Half = Clear * 0.5;
		if (Clear <= 0.0)
		{
			return 0.0;
		}

		constexpr int32 Samples = 1200;
		const double Cell = Clear / Samples;

		TArray<bool> Covered;
		Covered.Init(false, Samples);

		const FTransform ToLocal = Curtain->GetActorTransform().Inverse();

		auto MarkMesh = [&Covered, Half, Cell, AtZ, &ToLocal](UDynamicMeshComponent* Component)
		{
			if (Component == nullptr || Component->GetDynamicMesh() == nullptr)
			{
				return;
			}

			const FDynamicMesh3& Mesh = Component->GetDynamicMesh()->GetMeshRef();
			const FTransform Combined = Component->GetComponentTransform() * ToLocal;

			for (const int32 Tid : Mesh.TriangleIndicesItr())
			{
				FVector3d P, Q, R;
				Mesh.GetTriVertices(Tid, P, Q, R);

				const FVector A = Combined.TransformPosition(FVector(P));
				const FVector B = Combined.TransformPosition(FVector(Q));
				const FVector C = Combined.TransformPosition(FVector(R));

				if (AtZ < FMath::Min3(A.Z, B.Z, C.Z) || AtZ > FMath::Max3(A.Z, B.Z, C.Z))
				{
					continue;
				}

				const int32 First = FMath::Clamp(
					FMath::FloorToInt32((FMath::Min3(A.X, B.X, C.X) + Half) / Cell), 0, Samples - 1);
				const int32 Last = FMath::Clamp(
					FMath::CeilToInt32((FMath::Max3(A.X, B.X, C.X) + Half) / Cell), 0, Samples - 1);

				for (int32 Index = First; Index <= Last; ++Index)
				{
					Covered[Index] = true;
				}
			}
		};

		MarkMesh(Curtain->GetMeshComponent());
		for (const TObjectPtr<UDynamicMeshComponent>& Part : Curtain->GetPartComponents())
		{
			MarkMesh(Part.Get());
		}

		int32 Best = 0;
		int32 Run = 0;
		for (int32 Index = 0; Index < Samples; ++Index)
		{
			Run = Covered[Index] ? 0 : Run + 1;
			Best = FMath::Max(Best, Run);
		}

		return Best * Cell;
	}
}

using namespace HouseForgeCurtainActors;

/**
 * DOES THE FLAT'S OWN CLOTH COVER ITS WINDOW, AND THEN GET OUT OF THE WAY?
 *
 * Both ends of the travel, in centimetres of visible aperture, on the four curtains the reference
 * flat actually has. The bench test asks this of a curtain it built itself out of a pelmet it made
 * up; this asks it of the ones hanging in the rooms, whose track width came from a real pelmet that
 * was itself lowered 40 mm to clear a false ceiling.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFFlatCurtainDrawsTest,
	"HouseForge.Flat.EveryCurtainCoversItsWindowAndThenClearsIt", HF_TEST_FLAGS)

bool FHFFlatCurtainDrawsTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEditor != nullptr ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("An editor world is open"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT{ ClearHouseForgeActors(World); };

	FHFHouseSpec Spec;
	AHFHouseActor* House = BuildReferenceFlat(World, Spec);
	if (!TestNotNull(TEXT("The reference flat builds"), House))
	{
		return false;
	}

	const TArray<AHFCurtainActor*> Curtains = CurtainsIn(House);

	// FOUR, AND THE COUNT IS PART OF THE CLAIM. A run of this that found none would pass every loop
	// below by having nothing to iterate, which is how a test comes to assert nothing at all.
	if (!TestEqual(TEXT("The flat has its four curtains"), Curtains.Num(), 4))
	{
		return false;
	}

	for (AHFCurtainActor* Curtain : Curtains)
	{
		const FHFCurtainParams& P = Curtain->Curtain;
		const FString Id = Curtain->ElementId.ToString();

		// Eye height down the drop: below the pelmet, below the heading, in the glass.
		const double AtZ = -P.Drop * 0.6;

		Curtain->SetAllPartsOpenAmount(0.0);
		const double Shut = ApertureCm(Curtain, AtZ);

		Curtain->SetAllPartsOpenAmount(1.0);
		const double Open = ApertureCm(Curtain, AtZ);

		AddInfo(FString::Printf(
			TEXT("'%s': %.1f cm of track, %.1f cm of aperture drawn, %.1f cm open (%.0f%%)."),
			*Id, P.ClearWidth(), Shut, Open, 100.0 * Open / FMath::Max(P.ClearWidth(), 1.0)));

		// DRAWN, IT IS A WALL OF CLOTH. Only the hairlines between folds are open, and the widest of
		// them is a millimetre and a half by construction - see FHFCurtainParams::FoldGap.
		TestTrue(*FString::Printf(
			TEXT("'%s' covers its window when drawn: the widest gap is %.2f cm"), *Id, Shut),
			Shut < 1.0);

		// OPEN, THERE IS A WINDOW TO LOOK THROUGH. Two leaves stacking to opposite ends leave the
		// middle clear; the figure is what the pelmet was widened for in the first place.
		TestTrue(*FString::Printf(
			TEXT("'%s' clears its window when open: %.1f cm of %.1f cm is clear"),
			*Id, Open, P.ClearWidth()),
			Open > P.ClearWidth() * 0.45);

		const_cast<AHFCurtainActor*>(Curtain)->SetAllPartsOpenAmount(0.0);
	}

	return true;
}

/**
 * IS THE CLOTH INSIDE THE PELMET, AND DOES THE HEM CLEAR THE FLOOR?
 *
 * The three ways a correctly-drawing curtain is still wrong, and none of them is visible from the
 * aperture:
 *
 *   - it stands through the fascia. The fabric hangs CENTRED on its gliders and snakes either side
 *     of the track, so half its depth plus the cloth's own thickness has to fit in front of the
 *     track line. At the 35 mm setback the pelmet shipped with, the front folds stood 10 mm through
 *     the fascia - which is a curtain hanging in front of the box that is supposed to hide it.
 *   - the stack fouls the reveal. Drawn back, a leaf is a rope of cloth that has to end up ON the
 *     track and not past the end return.
 *   - the hem sweeps the floor.
 *
 * Measured at both ends of the travel, because the bundle is deeper than the hanging fabric and
 * wider than one fold - the two failures live at opposite ends.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFFlatCurtainHangsClearTest,
	"HouseForge.Flat.EveryCurtainHangsInsideItsPelmetAndClearsTheFloor", HF_TEST_FLAGS)

bool FHFFlatCurtainHangsClearTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEditor != nullptr ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("An editor world is open"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT{ ClearHouseForgeActors(World); };

	FHFHouseSpec Spec;
	AHFHouseActor* House = BuildReferenceFlat(World, Spec);
	if (!TestNotNull(TEXT("The reference flat builds"), House))
	{
		return false;
	}

	const TArray<AHFCurtainActor*> Curtains = CurtainsIn(House);
	if (!TestEqual(TEXT("The flat has its four curtains"), Curtains.Num(), 4))
	{
		return false;
	}

	for (AHFCurtainActor* Curtain : Curtains)
	{
		const FHFCurtainParams& P = Curtain->Curtain;
		const FString Id = Curtain->ElementId.ToString();

		// THE FLOOR IS THE ROOM'S, not the flat's. A drop is measured off the finished floor the
		// curtain hangs over, and rooms in this flat do not all sit at the same level.
		const FHFFixture* Fixture = Spec.Fixtures.FindByPredicate(
			[Curtain](const FHFFixture& F) { return F.Id == Curtain->ElementId; });
		const FHFRoom* Room = Fixture != nullptr ? Spec.FindRoom(Fixture->RoomId) : nullptr;

		if (!TestNotNull(*FString::Printf(TEXT("'%s' is a fixture in a room"), *Id), Room))
		{
			continue;
		}

		const double FloorZ = Room->FloorZ;

		AHFPelmetActor* Pelmet = PelmetFor(House, Curtain);
		if (!TestNotNull(*FString::Printf(TEXT("'%s' hangs in a pelmet"), *Id), Pelmet))
		{
			continue;
		}

		const FHFPelmetParams& Box = Pelmet->Pelmet;

		// The slot, in the CURTAIN'S frame. The curtain's origin is on the track line, so the slot's
		// two faces sit either side of it at whatever setback centred the track - see
		// FHFPelmetParams::TrackCentreY, which is the figure both the pelmet and the cloth read.
		const double ToFascia = Box.TrackCentreY() - (-Box.Depth * 0.5 + Box.BoardThickness);
		const double ToPlaster = Box.Depth * 0.5 - Box.TrackCentreY();

		// The run, in the same frame: the slot between the end returns, which the curtain's own
		// TrackWidth was set from.
		const double HalfRun = P.TrackWidth * 0.5;

		for (const double Amount : { 0.0, 0.5, 1.0 })
		{
			Curtain->SetAllPartsOpenAmount(Amount);

			const TArray<FVector> Points = ClothPoints(Curtain);
			if (!TestTrue(*FString::Printf(TEXT("'%s' has cloth in it"), *Id), Points.Num() > 0))
			{
				break;
			}

			double FrontMost = TNumericLimits<double>::Max();	// most negative Y: towards the fascia
			double BackMost = -TNumericLimits<double>::Max();	// most positive Y: towards the plaster
			double LeftMost = TNumericLimits<double>::Max();
			double RightMost = -TNumericLimits<double>::Max();
			double LowestZ = TNumericLimits<double>::Max();
			double HighestZ = -TNumericLimits<double>::Max();

			for (const FVector& Point : Points)
			{
				FrontMost = FMath::Min(FrontMost, Point.Y);
				BackMost = FMath::Max(BackMost, Point.Y);
				LeftMost = FMath::Min(LeftMost, Point.X);
				RightMost = FMath::Max(RightMost, Point.X);
				LowestZ = FMath::Min(LowestZ, Point.Z);
				HighestZ = FMath::Max(HighestZ, Point.Z);
			}

			// IN FRONT OF THE TRACK, THE FASCIA. Nothing may reach it, at any open amount - and the
			// drawn-back bundle is the deepest the cloth ever gets, because the folds fan in depth
			// rather than flattening. See FHFCurtainParams::StackedReach.
			TestTrue(*FString::Printf(
				TEXT("'%s' at %.0f%% open stays behind the fascia: %.2f cm of cloth into %.2f cm of slot"),
				*Id, Amount * 100.0, -FrontMost, ToFascia),
				-FrontMost < ToFascia);

			// And behind it, the plaster the pelmet is screwed to.
			TestTrue(*FString::Printf(
				TEXT("'%s' at %.0f%% open stays off the wall: %.2f cm of cloth into %.2f cm of slot"),
				*Id, Amount * 100.0, BackMost, ToPlaster),
				BackMost < ToPlaster);

			// THE STACK STAYS ON THE TRACK. A leaf drawn back past the end return is a stack fouling
			// the reveal, which is the failure a pelmet is made wider than its window to prevent.
			TestTrue(*FString::Printf(
				TEXT("'%s' at %.0f%% open keeps its cloth on the run: %.2f cm to %.2f cm of +/- %.2f"),
				*Id, Amount * 100.0, LeftMost, RightMost, HalfRun),
				LeftMost > -HalfRun - 0.01 && RightMost < HalfRun + 0.01);


			// THE HEM CLEARS THE FLOOR. In world Z, because the floor is the flat's and not the
			// curtain's - a drop measured off the wrong datum is exactly the defect ApplyDrop exists
			// to prevent.
			const double HemWorldZ = Curtain->GetActorTransform().TransformPosition(
				FVector(0.0, 0.0, LowestZ)).Z;

			TestTrue(*FString::Printf(
				TEXT("'%s' at %.0f%% open hangs %.2f cm clear of the floor"),
				*Id, Amount * 100.0, HemWorldZ - FloorZ),
				HemWorldZ - FloorZ > 0.5);

			// And the heading is where the gliders are, not above them: cloth over the track line
			// would be cloth through the pelmet's own top board.
			TestTrue(*FString::Printf(
				TEXT("'%s' at %.0f%% open hangs from the track rather than through it (top at %.2f cm)"),
				*Id, Amount * 100.0, HighestZ),
				HighestZ < 0.01);
		}

		Curtain->SetAllPartsOpenAmount(0.0);

		AddInfo(FString::Printf(
			TEXT("'%s' in '%s': %.0f cm drop, %.2f cm folds at %.2f fullness, inside a %.1f cm slot."),
			*Id, *Pelmet->ElementId.ToString(), P.Drop, P.FoldDepth(), P.AchievedFullness(),
			Box.SlotDepth()));
	}

	return true;
}

#undef HF_TEST_FLAGS

#endif	// WITH_DEV_AUTOMATION_TESTS
