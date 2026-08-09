// Copyright Siddartha G. All Rights Reserved.

//
// BAKING AN ARTICULATED FIXTURE WITHOUT WELDING IT INTO A BLOCK.
//
// .claude/rules/04-conventions.md: "Baking a fixture bakes each part separately and keeps the
// articulation; a bake must not weld a chest of drawers into a block." And, one paragraph earlier:
// "Collision must match the visual mesh, including on open doors, or a walkthrough passes through
// things."
//
// HFBakeTests.cpp proves the switch itself - that the dynamic mesh survives, that unbake is exact,
// that a Modeling Tool still targets the live mesh. It proves the per-part model on ONE door. This
// file is about the rest of the flat: every wardrobe shutter, every sliding leaf, every drawer, the
// WC seat and lid, the appliance doors, and the fans, which are the awkward case because they are
// posed by an unbounded phase rather than by an open amount.
//
// Four questions, none of which the single-door test can answer:
//
//   1. Does EVERY articulated fixture in the flat bake into parts, or only the one we tested?
//   2. Does every baked part FOLLOW its live part, exactly, through the whole range - or does it
//      merely exist somewhere near it?
//   3. Does a baked part COLLIDE the way the live one did - which for a fan rotor means blocking
//      nothing at all?
//   4. Do open amounts and spin phases survive a bake, an unbake and a house rebuild?
//
// Two defects were found by writing them, and both are the silent kind:
//
//   * A BAKED ROTOR BLOCKED A PAWN. AdoptBakedMesh copied the source's collision PROFILE NAME, and a
//     rotor has no profile - ApplyPartCollision writes its responses by hand, which leaves the name
//     reading "Custom" and carries none of the meaning. The baked stand-in fell back to its own
//     block-everything default, and QueryOnly is quite enough to stop a walking character, because
//     character movement is a sweep and a sweep is a query. Every baked ceiling fan in the flat was
//     an invisible wall at head height, frozen at whatever azimuth its blades were left at.
//     Guarded by ABakedRotorStillBlocksNothingButTraces.
//
//   * A PART DROPPED FROM THE MIDDLE SHUFFLED EVERY BAKED MESH AFTER IT. SyncBakedPartsToSources
//     trimmed the list from the END, which is right only if parts can vanish from the end alone.
//     Narrow a wardrobe by one bay and it loses a BODY leaf while every loft leaf above it stays, so
//     from that moment the loft leaves wore the body leaves' assets and the orphaned baked component
//     re-parented itself to the carcass and hung there. Guarded by
//     ADroppedMiddlePartDoesNotShuffleTheBakedMeshes.
//

#include "HouseForgeEditor.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Actors/HFArticulatedActor.h"
#include "Actors/HFBakeTypes.h"
#include "Actors/HFElementActors.h"
#include "Actors/HFFanActor.h"
#include "Actors/HFHouseActor.h"
#include "Actors/HFWardrobeActor.h"
#include "Bake/HFBakeService.h"
#include "Components/DynamicMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Editor.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Geometry/HFFanKit.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Model/HFArticulation.h"
#include "Model/HFSampleHouse.h"
#include "UDynamicMesh.h"

using namespace UE::Geometry;

#define HF_TEST_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace HFBakeArticulation
{
	/**
	 * Somewhere nothing else in the suite builds.
	 *
	 * One editor world is shared by the whole run and a destroyed actor keeps its physics body until
	 * the world next ticks, so a trace near the origin can be answered by a door two test files ago.
	 * The wardrobe tests learned this and it read convincingly as a collision defect; the site below
	 * is a different corner again, so this file cannot be answered by theirs either.
	 */
	const FVector TestSite(-3000.0, 3000.0, 0.0);

	/** Every HouseForge actor already standing, gone, so a flat-wide sweep sees one flat. */
	void ClearHouseForgeActors(UWorld* World)
	{
		TArray<AActor*> Doomed;

		for (TActorIterator<AHFHouseActor> It(World); It; ++It)
		{
			It->ClearGeometry();
			Doomed.Add(*It);
		}
		for (TActorIterator<AHFElementActor> It(World); It; ++It)
		{
			Doomed.Add(*It);
		}

		for (AActor* Actor : Doomed)
		{
			if (IsValid(Actor))
			{
				Actor->Destroy();
			}
		}
	}

	AHFHouseActor* BuildReferenceFlat(UWorld* World)
	{
		ClearHouseForgeActors(World);

		AHFHouseActor* House = World->SpawnActor<AHFHouseActor>();
		if (House == nullptr)
		{
			return nullptr;
		}

		House->SetSpec(FHFSampleHouse::Make2BHK());
		House->BuildGeometry();
		return House;
	}

	/** Leaves no dirty packages behind, so the automation run does not try to save test assets. */
	void ForgetAssets(AHFElementActor* Element)
	{
		if (!IsValid(Element))
		{
			return;
		}

		for (const FHFBakedPart& Part : Element->BakedParts)
		{
			if (Part.BakedMesh != nullptr && Part.BakedMesh->GetOutermost() != nullptr)
			{
				Part.BakedMesh->GetOutermost()->SetDirtyFlag(false);
			}
		}
	}

	void ForgetAssets(const TArray<AHFArticulatedActor*>& Elements)
	{
		for (AHFArticulatedActor* Element : Elements)
		{
			ForgetAssets(Element);
		}
	}

	/** Every articulated element the house built, in build order. */
	TArray<AHFArticulatedActor*> ArticulatedIn(AHFHouseActor* House)
	{
		TArray<AHFArticulatedActor*> Out;
		for (AActor* Element : House->ElementActors)
		{
			if (AHFArticulatedActor* Typed = Cast<AHFArticulatedActor>(Element))
			{
				if (Typed->NumParts() > 0)
				{
					Out.Add(Typed);
				}
			}
		}
		return Out;
	}

	/** The local-space box of whatever geometry a component is drawing, or false if it has none. */
	bool LocalBoxOf(const USceneComponent* Component, FBox& OutBox)
	{
		if (const UStaticMeshComponent* Baked = Cast<UStaticMeshComponent>(Component))
		{
			if (Baked->GetStaticMesh() == nullptr)
			{
				return false;
			}
			OutBox = Baked->GetStaticMesh()->GetBoundingBox();
			return OutBox.IsValid != 0;
		}

		if (const UDynamicMeshComponent* Live = Cast<UDynamicMeshComponent>(Component))
		{
			bool bAny = false;
			FAxisAlignedBox3d Bounds = FAxisAlignedBox3d::Empty();

			const_cast<UDynamicMeshComponent*>(Live)->GetDynamicMesh()->ProcessMesh(
				[&bAny, &Bounds](const FDynamicMesh3& Mesh)
				{
					bAny = Mesh.TriangleCount() > 0;
					Bounds = Mesh.GetBounds();
				});

			if (!bAny)
			{
				return false;
			}

			OutBox = FBox(static_cast<FVector>(Bounds.Min), static_cast<FVector>(Bounds.Max));
			return true;
		}

		return false;
	}

	/**
	 * Where a component's geometry stands in its ACTOR's space, right now.
	 *
	 * Actor space rather than world, because that is the frame a fixture's own dimensions are quoted
	 * in - a wardrobe's aperture is measured along the run, not along world X - and because it makes
	 * the live and the baked answer directly comparable whatever the actor is doing.
	 */
	bool ActorSpaceBox(const AActor& Actor, const USceneComponent* Component, FBox& OutBox)
	{
		FBox Local;
		if (!IsValid(Component) || !LocalBoxOf(Component, Local))
		{
			return false;
		}

		const FTransform ToActor = Component->GetComponentTransform() * Actor.GetActorTransform().Inverse();

		OutBox = FBox(ForceInit);
		FVector Corners[8];
		Local.GetVertices(Corners);
		for (const FVector& Corner : Corners)
		{
			OutBox += ToActor.TransformPosition(Corner);
		}
		return true;
	}

	/**
	 * The trace a walking character makes: the world, a pawn's channel, and SIMPLE collision.
	 *
	 * Both halves matter, and both are what this plugin learned the hard way in the wardrobe tests.
	 * UPrimitiveComponent::LineTraceComponent goes straight to one body and never asks the world, so
	 * it passes for a body that is absent from the physics scene or whose responses do not block the
	 * channel a character sweeps on. And bTraceComplex FALSE is the bigger one: what makes any of this
	 * geometry solid is CTF_UseComplexAsSimple redirecting the simple query onto the triangles, so
	 * asking for the complex geometry explicitly proves nothing about a walkthrough.
	 */
	bool WalkTraceHits(UWorld* World, const FVector& Start, const FVector& End, FHitResult& OutHit)
	{
		const FCollisionQueryParams Params(TEXT("HFBakeArticulation"), /*bTraceComplex*/ false);
		return World->LineTraceSingleByChannel(OutHit, Start, End, ECC_Pawn, Params);
	}

	/** The trace editor picking and any line-of-sight query makes. A rotor must answer this one. */
	bool SightTraceHits(UWorld* World, const FVector& Start, const FVector& End, FHitResult& OutHit)
	{
		const FCollisionQueryParams Params(TEXT("HFBakeArticulationSight"), /*bTraceComplex*/ false);
		return World->LineTraceSingleByChannel(OutHit, Start, End, ECC_Visibility, Params);
	}

	/**
	 * A short line straight through the middle of whatever a component is drawing, taken off its LIVE
	 * transform rather than worked out from the fixture's dimensions.
	 *
	 * So it aims at wherever the part has actually swung, slid or spun to, and needs no arithmetic of
	 * its own at each open amount.
	 */
	bool AimThrough(const USceneComponent* Component, FVector& OutStart, FVector& OutEnd)
	{
		FBox Local;
		if (!IsValid(Component) || !LocalBoxOf(Component, Local))
		{
			return false;
		}

		const FTransform Pose = Component->GetComponentTransform();
		const FVector Centre = Pose.TransformPosition(Local.GetCenter());

		// Along the part's own thinnest axis, which for a leaf, a shutter, a lid or a blade is the
		// board's thickness - so the line goes through the face rather than along it.
		const FVector Extent = Local.GetExtent();
		const int32 Thinnest = (Extent.X <= Extent.Y && Extent.X <= Extent.Z) ? 0 : (Extent.Y <= Extent.Z ? 1 : 2);

		FVector LocalAxis = FVector::ZeroVector;
		LocalAxis[Thinnest] = 1.0;

		const FVector Axis = Pose.TransformVectorNoScale(LocalAxis).GetSafeNormal();
		const double Reach = FMath::Max(Extent[Thinnest] * 4.0, 20.0);

		OutStart = Centre - Axis * Reach;
		OutEnd = Centre + Axis * Reach;
		return true;
	}

	/** A three-bay wardrobe with a loft over it, on its own, out of everybody's way. */
	AHFWardrobeActor* SpawnLoftedWardrobe(UWorld* World, int32 Bays)
	{
		AHFWardrobeActor* Actor = World->SpawnActor<AHFWardrobeActor>(TestSite, FRotator::ZeroRotator);
		if (Actor == nullptr)
		{
			return nullptr;
		}

		Actor->ElementId = TEXT("WD_BakeParts");
		Actor->Wardrobe.Width = 60.0 * static_cast<double>(Bays);
		Actor->Wardrobe.Depth = 60.0;
		Actor->Wardrobe.Height = 240.0;
		Actor->Wardrobe.BayCount = Bays;
		Actor->Wardrobe.PlinthHeight = 10.0;
		Actor->Wardrobe.bHangingRail = true;
		Actor->Wardrobe.MotionKind = EHFShutterMotion::SideHung;

		// THE LOFT IS THE POINT. It puts parts AFTER the body leaves in the part list, so losing a body
		// leaf is losing one out of the middle - which is the case a truncating sync cannot see.
		Actor->Wardrobe.bHasLoft = true;
		Actor->Wardrobe.LoftHeight = 45.0;
		Actor->Wardrobe.LoftMotionKind = EHFShutterMotion::SideHung;

		Actor->Regenerate();
		return Actor;
	}
}

// ===================================================================== the flat, part by part

/**
 * EVERY articulated fixture in the reference flat bakes into its parts, and every part still moves.
 *
 * The flat has doors that swing, sliding leaves, wardrobe shutters, drawers, a WC seat and lid,
 * appliance doors, curtain folds and fans. Rule 04 forbids welding any of them into a block, and a
 * bake that welded ONE type would be invisible to a test written against a door.
 *
 * Three claims, on every fixture:
 *
 *   SEPARATE   one baked part per source component, and no two parts sharing an asset. Two parts
 *              sharing one asset is a welded fixture with extra steps.
 *   FOLLOWING  the baked component's world transform equals the live component's at every open
 *              amount sampled, not merely near it. The baked mesh hangs off the dynamic component it
 *              stands in for, so this is what proves the parenting is what it claims to be.
 *   MOVABLE    because a Static-mobility component that is moved destroys and rebuilds its scene
 *              proxy (UStaticMeshComponent::ShouldRecreateProxyOnUpdateTransform), which forces
 *              LumenRemovePrimitive + LumenAddPrimitive and a full surface-cache re-capture every
 *              time a door opens. Mobility appears nowhere in the chain that decides Lumen scene
 *              membership - it is eight proxy flags and mobility is not one of them, and mesh cards
 *              live on the asset - so Movable costs nothing there and saves the re-capture.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFBakeFlatArticulationTest,
	"HouseForge.Bake.Articulation.EveryArticulatedFixtureInTheFlatBakesAndStillMoves", HF_TEST_FLAGS)

bool FHFBakeFlatArticulationTest::RunTest(const FString& Parameters)
{
	using namespace HFBakeArticulation;

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("An editor world is open"), World))
	{
		return false;
	}

	AHFHouseActor* House = BuildReferenceFlat(World);
	if (!TestNotNull(TEXT("The reference flat builds"), House))
	{
		return false;
	}

	TArray<AHFArticulatedActor*> Fixtures = ArticulatedIn(House);
	ON_SCOPE_EXIT
	{
		ForgetAssets(Fixtures);
		if (IsValid(House)) { House->ClearGeometry(); House->Destroy(); }
	};

	AddInfo(FString::Printf(TEXT("The flat has %d articulated element(s)."), Fixtures.Num()));
	if (!TestTrue(TEXT("The flat has articulated elements to bake"), Fixtures.Num() > 0))
	{
		return false;
	}

	int32 TotalParts = 0;
	int32 TotalMoving = 0;
	int32 Welded = 0;
	int32 Drifted = 0;
	int32 Stuck = 0;
	int32 NotMovable = 0;
	int32 FailedToBake = 0;

	// Sampled rather than end-to-end. A part can be right shut and right open and wrong in between:
	// a baked component parented to the ACTOR rather than to its part would sit still while the live
	// one swung, and 0 and 1 are exactly the two amounts where a mis-parented door looks plausible if
	// the fixture happens to be at the origin.
	const double Samples[] = { 0.0, 0.25, 0.5, 0.75, 1.0 };

	for (AHFArticulatedActor* Fixture : Fixtures)
	{
		const FString Where = FString::Printf(TEXT("%s '%s'"),
			*Fixture->GetClass()->GetName(), *Fixture->ElementId.ToString());

		TArray<UDynamicMeshComponent*> Sources;
		Fixture->GetBakeSourceComponents(Sources);

		FHFBakeReport Report;
		if (!FHFBakeService::BakeElement(Fixture, Report))
		{
			AddError(FString::Printf(TEXT("%s did not bake: %s"), *Where, *Report.Summary()));
			++FailedToBake;
			continue;
		}

		// ------------------------------------------------------------------------------- SEPARATE
		if (Fixture->BakedParts.Num() != Sources.Num())
		{
			AddError(FString::Printf(
				TEXT("%s baked %d part(s) for %d source component(s). A fixture must bake one asset per part."),
				*Where, Fixture->BakedParts.Num(), Sources.Num()));
			++Welded;
			continue;
		}

		TSet<UStaticMesh*> Distinct;
		int32 WithAsset = 0;
		for (const FHFBakedPart& Part : Fixture->BakedParts)
		{
			if (Part.BakedMesh != nullptr)
			{
				Distinct.Add(Part.BakedMesh);
				++WithAsset;
			}
		}

		if (Distinct.Num() != WithAsset)
		{
			AddError(FString::Printf(
				TEXT("%s baked %d part(s) into only %d distinct asset(s) - two parts share a mesh, which is a welded fixture."),
				*Where, WithAsset, Distinct.Num()));
			++Welded;
			continue;
		}

		TotalParts += Fixture->BakedParts.Num();

		// -------------------------------------------------------------------------------- MOVABLE
		for (const FHFBakedPart& Part : Fixture->BakedParts)
		{
			if (IsValid(Part.Component) && Part.Component->Mobility != EComponentMobility::Movable)
			{
				AddError(FString::Printf(
					TEXT("%s baked part '%s' has mobility %d, not Movable (%d). Moving a non-Movable component rebuilds its scene proxy and forces a Lumen surface-cache re-capture on every open."),
					*Where, *Part.SourceComponentName.ToString(),
					static_cast<int32>(Part.Component->Mobility.GetValue()),
					static_cast<int32>(EComponentMobility::Movable)));
				++NotMovable;
			}
		}

		// ------------------------------------------------------------------------------ FOLLOWING
		//
		// Part 0 is the fixed shell and has no pose to follow. Which source belongs to which PART is
		// looked up rather than assumed: GetBakeSourceComponents skips a part with no valid component,
		// so "source i is part i-1" is right until the day it is quietly wrong about every part after
		// a dropped one.
		TMap<const UDynamicMeshComponent*, int32> PartIndexByComponent;
		{
			const TArray<TObjectPtr<UDynamicMeshComponent>>& PartComponents = Fixture->GetPartComponents();
			for (int32 Index = 0; Index < PartComponents.Num(); ++Index)
			{
				if (IsValid(PartComponents[Index]))
				{
					PartIndexByComponent.Add(PartComponents[Index].Get(), Index);
				}
			}
		}

		// THE WHOLE FIXTURE IS DRIVEN, not one part at a time, and that is not a convenience.
		// FHFPartMotion::SequencedAfterPartId means a drawer inside a wardrobe cannot come out until
		// its shutter is open, and a curtain's folds are sequenced along the leaf. Asking one part on
		// its own for a full open amount and then complaining that it did not move would be measuring
		// the interlock and reporting it as a bake defect.
		Fixture->CloseAllParts();

		TArray<FTransform> Shut;
		Shut.SetNum(Sources.Num());
		TBitArray<> Moved(false, Sources.Num());

		for (int32 Index = 1; Index < Sources.Num(); ++Index)
		{
			if (IsValid(Sources[Index]))
			{
				Shut[Index] = Sources[Index]->GetComponentTransform();
			}
		}

		auto CompareAt = [&](int32 Index)
		{
			UDynamicMeshComponent* Live = Sources[Index];
			UStaticMeshComponent* Baked = Fixture->BakedParts[Index].Component;
			if (!IsValid(Live) || !IsValid(Baked) || Fixture->BakedParts[Index].BakedMesh == nullptr)
			{
				return true;
			}

			const FTransform LiveNow = Live->GetComponentTransform();
			if (!LiveNow.Equals(Shut[Index], 0.01f))
			{
				Moved[Index] = true;
			}
			return Baked->GetComponentTransform().Equals(LiveNow, 0.01f);
		};

		TSet<int32> Adrift;
		for (const double Amount : Samples)
		{
			Fixture->SetAllPartsOpenAmount(Amount);
			for (int32 Index = 1; Index < Sources.Num(); ++Index)
			{
				if (!CompareAt(Index))
				{
					Adrift.Add(Index);
				}
			}
		}

		// A SPINNING PART IS DELIBERATELY LEFT OUT of "open everything" - a fan has no open amount and
		// stopping one at an arbitrary blade is not a pose anybody asked for - so it is driven by its
		// own phase, past a whole turn, which is the thing a 0..1 amount cannot express at all.
		for (int32 Index = 1; Index < Sources.Num(); ++Index)
		{
			const int32* Part = PartIndexByComponent.Find(Sources[Index]);
			if (Part == nullptr || !Fixture->Parts[*Part].Motion.Revolves())
			{
				continue;
			}

			Fixture->SetPartSpinTurns(Fixture->Parts[*Part].PartId, 2.75);
			if (!CompareAt(Index))
			{
				Adrift.Add(Index);
			}
		}

		// A leaf that a master open deliberately holds shut - one of a sliding pair, where driving both
		// out together would exchange tracks and uncover nothing - gets its own turn through the verb
		// that exists for exactly that.
		for (int32 Index = 1; Index < Sources.Num(); ++Index)
		{
			const int32* Part = PartIndexByComponent.Find(Sources[Index]);
			if (Part == nullptr || Moved[Index])
			{
				continue;
			}

			Fixture->OpenRunFrom(Fixture->Parts[*Part].PartId, 1.0);
			if (!CompareAt(Index))
			{
				Adrift.Add(Index);
			}
		}

		for (const int32 Index : Adrift)
		{
			const int32* Part = PartIndexByComponent.Find(Sources[Index]);
			AddError(FString::Printf(
				TEXT("%s baked part '%s' does not stay on its live part through its range. The baked mesh is not riding the component it stands in for."),
				*Where, Part != nullptr ? *Fixture->Parts[*Part].PartId.ToString() : TEXT("?")));
			++Drifted;
		}

		for (int32 Index = 1; Index < Sources.Num(); ++Index)
		{
			const int32* Part = PartIndexByComponent.Find(Sources[Index]);
			if (Part == nullptr || Fixture->BakedParts[Index].BakedMesh == nullptr)
			{
				continue;
			}

			++TotalMoving;

			// A part whose declared travel is zero is allowed to sit still. Anything that declares real
			// travel has to be SEEN travelling, or "the fixture baked" is a claim about a fixture that
			// is shut - which is the shape of every articulation defect this project has had.
			const FHFPartMotion& Motion = Fixture->Parts[*Part].Motion;
			const bool bClaimsTravel =
				(Motion.Type == EHFMotionType::Hinge && FMath::Abs(Motion.MaxAngleDegrees) > 1.0)
				|| (Motion.Type == EHFMotionType::Slide && FMath::Abs(Motion.MaxTravelCm) > 0.5)
				|| Motion.Revolves();

			if (bClaimsTravel && !Moved[Index])
			{
				AddError(FString::Printf(
					TEXT("%s part '%s' declares motion type %d and never moved through its whole range."),
					*Where, *Fixture->Parts[*Part].PartId.ToString(), static_cast<int32>(Motion.Type)));
				++Stuck;
			}
		}

		Fixture->CloseAllParts();
	}

	AddInfo(FString::Printf(
		TEXT("%d articulated element(s) baked into %d part(s), %d of them moving parts."),
		Fixtures.Num() - FailedToBake, TotalParts, TotalMoving));

	TestEqual(TEXT("Every articulated fixture in the flat bakes"), FailedToBake, 0);
	TestEqual(TEXT("No fixture is welded into fewer meshes than it has parts"), Welded, 0);
	TestEqual(TEXT("Every baked part rides the live part it stands in for"), Drifted, 0);
	TestEqual(TEXT("Every part that claims to move, moves"), Stuck, 0);
	TestEqual(TEXT("Every baked moving part is Movable"), NotMovable, 0);
	TestTrue(TEXT("The flat really has moving parts to have baked"), TotalMoving > 0);

	return true;
}

// ================================================================== collision, at every amount

/**
 * A baked part blocks where it is, at every open amount - and a rotor still blocks nothing.
 *
 * COLLISION IS NOT A PROPERTY OF THE MESH, it is a property of the part. AHFArticulatedActor::
 * ApplyPartCollision gives a shutter BlockAll and gives a fan rotor QueryOnly with every response set
 * to Ignore except Visibility, because collision geometry cannot spin with the render - a blocking
 * rotor is one blade frozen across a third of its own sweep.
 *
 * The bake has to carry both of those across, and carrying the profile NAME across carries neither:
 * a hand-written response set leaves the name reading "Custom", and loading a name that is not a real
 * profile rebuilds the responses from the TARGET's own defaults, which block everything. So the baked
 * rotor came out QueryOnly and blocking - and QueryOnly stops a walking character, because character
 * movement is a sweep and a sweep is a query.
 *
 * Asserted by TRACING the world rather than by reading flags, and on both channels, because the two
 * halves of "blocks nothing but answers traces" fail independently.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFBakeRotorCollisionTest,
	"HouseForge.Bake.Articulation.ABakedRotorStillBlocksNothingButTraces", HF_TEST_FLAGS)

bool FHFBakeRotorCollisionTest::RunTest(const FString& Parameters)
{
	using namespace HFBakeArticulation;

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("An editor world is open"), World))
	{
		return false;
	}

	AHFFanActor* Fan = World->SpawnActor<AHFFanActor>(TestSite, FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("A fan spawns"), Fan))
	{
		return false;
	}
	ON_SCOPE_EXIT{ ForgetAssets(Fan); if (IsValid(Fan)) { Fan->Destroy(); } };

	Fan->ElementId = TEXT("FAN_Bake");
	Fan->ApplyProjectDefaults(EHFFanKind::Ceiling);
	Fan->Regenerate();

	UDynamicMeshComponent* LiveRotor = Fan->GetPartComponent(FHFFanKit::RotorPartId);
	if (!TestNotNull(TEXT("The fan has a rotor"), LiveRotor))
	{
		return false;
	}

	// What the LIVE rotor does, established first so the baked one is compared against the plugin's
	// own behaviour rather than against a number written down here.
	FVector Start = FVector::ZeroVector;
	FVector End = FVector::ZeroVector;
	if (!TestTrue(TEXT("A line can be aimed through the rotor"), AimThrough(LiveRotor, Start, End)))
	{
		return false;
	}

	FHitResult Hit;
	const bool bLiveBlocksWalk = WalkTraceHits(World, Start, End, Hit);
	const bool bLiveAnswersSight = SightTraceHits(World, Start, End, Hit);

	AddInfo(FString::Printf(TEXT("The live rotor blocks a pawn: %s. It answers a sight trace: %s."),
		bLiveBlocksWalk ? TEXT("yes") : TEXT("no"), bLiveAnswersSight ? TEXT("yes") : TEXT("no")));

	TestFalse(TEXT("The live rotor blocks nothing a pawn does - EHFPartCollision::TraceOnly"), bLiveBlocksWalk);
	TestTrue(TEXT("And it still answers a sight trace, which is what editor picking needs"), bLiveAnswersSight);

	FHFBakeReport Report;
	if (!TestTrue(TEXT("The fan bakes"), FHFBakeService::BakeElement(Fan, Report)))
	{
		AddError(Report.Summary());
		return false;
	}

	const int32 RotorIndex = Fan->BakedParts.Num() - 1;
	UStaticMeshComponent* BakedRotor = Fan->BakedParts.IsValidIndex(RotorIndex)
		? Fan->BakedParts[RotorIndex].Component : nullptr;
	if (!TestNotNull(TEXT("The rotor has a baked component"), BakedRotor))
	{
		return false;
	}

	TestEqual(TEXT("The baked rotor keeps the rotor's own collision, not the default"),
		static_cast<int32>(BakedRotor->GetCollisionEnabled()),
		static_cast<int32>(ECollisionEnabled::QueryOnly));

	// Re-aimed at the BAKED component: same place, but taken off its own transform so the test says
	// nothing about where it expects the rotor to be.
	if (!TestTrue(TEXT("A line can be aimed through the baked rotor"), AimThrough(BakedRotor, Start, End)))
	{
		return false;
	}

	const bool bBakedBlocksWalk = WalkTraceHits(World, Start, End, Hit);
	const bool bBakedAnswersSight = SightTraceHits(World, Start, End, Hit);

	AddInfo(FString::Printf(TEXT("The baked rotor blocks a pawn: %s. It answers a sight trace: %s."),
		bBakedBlocksWalk ? TEXT("yes") : TEXT("no"), bBakedAnswersSight ? TEXT("yes") : TEXT("no")));

	// THE DEFECT. A baked ceiling fan that blocks a pawn is an invisible wall at head height, frozen
	// at whatever azimuth its blades were left at, in every room of a baked flat.
	TestFalse(TEXT("A BAKED rotor blocks nothing a pawn does either - a walkthrough passes through it"),
		bBakedBlocksWalk);
	TestTrue(TEXT("And it still answers a sight trace, so picking and line-of-sight still work"),
		bBakedAnswersSight);

	return true;
}

/**
 * A baked shutter blocks where the shutter IS, at every open amount - including half way.
 *
 * Rule 04 names this exact case: "Collision must match the visual mesh, including on open doors, or a
 * walkthrough passes through things." The dynamic side gets it from CTF_UseComplexAsSimple on the
 * component; the baked side gets it from the same trace flag baked into the ASSET, and from the
 * component riding the part. Both halves have to hold at once or a walkthrough finds a door that
 * blocks where it used to be.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFBakeShutterCollisionRangeTest,
	"HouseForge.Bake.Articulation.BakedCollisionFollowsAPartThroughItsRange", HF_TEST_FLAGS)

bool FHFBakeShutterCollisionRangeTest::RunTest(const FString& Parameters)
{
	using namespace HFBakeArticulation;

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("An editor world is open"), World))
	{
		return false;
	}

	AHFWardrobeActor* Wardrobe = SpawnLoftedWardrobe(World, 3);
	if (!TestNotNull(TEXT("A wardrobe spawns"), Wardrobe))
	{
		return false;
	}
	ON_SCOPE_EXIT{ ForgetAssets(Wardrobe); if (IsValid(Wardrobe)) { Wardrobe->Destroy(); } };

	const FName LeafId = AHFWardrobeActor::ShutterPartId(0);
	UDynamicMeshComponent* LiveLeaf = Wardrobe->GetPartComponent(LeafId);
	if (!TestNotNull(TEXT("The wardrobe has a leaf"), LiveLeaf))
	{
		return false;
	}

	FHFBakeReport Report;
	if (!TestTrue(TEXT("The wardrobe bakes"), FHFBakeService::BakeElement(Wardrobe, Report)))
	{
		AddError(Report.Summary());
		return false;
	}

	// The leaf's index in the source list, found rather than assumed - a wardrobe's part order is the
	// kit's business and this test has no standing to encode it.
	TArray<UDynamicMeshComponent*> Sources;
	Wardrobe->GetBakeSourceComponents(Sources);
	const int32 LeafIndex = Sources.IndexOfByKey(LiveLeaf);

	UStaticMeshComponent* BakedLeaf = Wardrobe->BakedParts.IsValidIndex(LeafIndex)
		? Wardrobe->BakedParts[LeafIndex].Component : nullptr;
	if (!TestNotNull(TEXT("The leaf has a baked component"), BakedLeaf))
	{
		return false;
	}

	TestEqual(TEXT("The baked leaf blocks, as the live one does"),
		static_cast<int32>(BakedLeaf->GetCollisionEnabled()),
		static_cast<int32>(ECollisionEnabled::QueryAndPhysics));

	// Only the baked geometry is on now, so a hit is the baked leaf's or nothing's.
	TestEqual(TEXT("The wardrobe is showing its baked geometry"),
		static_cast<int32>(Wardrobe->RenderMode), static_cast<int32>(EHFRenderMode::Baked));
	TestEqual(TEXT("And the live leaf has stood its collision down, so nothing double-traces"),
		static_cast<int32>(LiveLeaf->GetCollisionEnabled()),
		static_cast<int32>(ECollisionEnabled::NoCollision));

	int32 Missed = 0;
	for (const double Amount : { 0.0, 0.25, 0.5, 0.75, 1.0 })
	{
		Wardrobe->SetPartOpenAmount(LeafId, Amount);

		FVector Start = FVector::ZeroVector;
		FVector End = FVector::ZeroVector;
		if (!AimThrough(BakedLeaf, Start, End))
		{
			continue;
		}

		FHitResult Hit;
		const bool bHit = WalkTraceHits(World, Start, End, Hit);

		AddInfo(FString::Printf(TEXT("At %.0f%% open a walk trace through the baked leaf %s."),
			Amount * 100.0, bHit ? TEXT("hits it") : TEXT("passes straight through")));

		if (!bHit)
		{
			// A leaf that blocks shut and not open is precisely the walkthrough failure rule 04
			// singles out: you can walk through an open door.
			AddError(FString::Printf(
				TEXT("The baked leaf blocks nothing at %.0f%% open. Its collision has not followed it."),
				Amount * 100.0));
			++Missed;
		}
	}

	TestEqual(TEXT("The baked leaf blocks at every open amount, not only shut"), Missed, 0);

	Wardrobe->CloseAllParts();
	return true;
}

// ========================================================================== poses and phases

/**
 * Open amounts and spin phases survive a bake, an unbake and a house rebuild.
 *
 * A pose is user state in exactly the way a hand edit is - somebody opened those doors on purpose,
 * usually to photograph them - and the bake has three chances to lose it. On the way in, because
 * baking touches components; on the way out, because unbake touches them again; and at a house
 * rebuild, which respawns elements, except for the baked ones, which it must now preserve instead.
 *
 * The fan is the awkward one and it is here on purpose. It has no open amount at all: it is posed by
 * an unbounded SpinTurns, so anything that carried poses as a 0..1 value would silently put every fan
 * in the flat back to its first turn, and every fan would come out stopped on the same blade.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFBakePosesSurviveTest,
	"HouseForge.Bake.Articulation.PosesAndSpinPhasesSurviveBakeUnbakeAndRebuild", HF_TEST_FLAGS)

bool FHFBakePosesSurviveTest::RunTest(const FString& Parameters)
{
	using namespace HFBakeArticulation;

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("An editor world is open"), World))
	{
		return false;
	}

	AHFHouseActor* House = BuildReferenceFlat(World);
	if (!TestNotNull(TEXT("The reference flat builds"), House))
	{
		return false;
	}

	TArray<AHFArticulatedActor*> Fixtures = ArticulatedIn(House);
	ON_SCOPE_EXIT
	{
		ForgetAssets(Fixtures);
		if (IsValid(House)) { House->ClearGeometry(); House->Destroy(); }
	};

	// One of each kind of pose, from the flat's own fixtures: something hinged or sliding, and
	// something that spins. Chosen by what the parts DECLARE rather than by element id, so this keeps
	// working when the drawing changes.
	struct FPosed
	{
		TWeakObjectPtr<AHFArticulatedActor> Fixture;
		FName PartId;
		bool bSpins = false;
		double Value = 0.0;
	};

	TArray<FPosed> Posed;
	int32 Openers = 0;
	int32 Spinners = 0;

	// THE SPINNER IS SOUGHT SEPARATELY, and it has to be. A flat has far more doors and shutters than
	// fans, so a plain "take the first few" fills up on openers and never reaches one - which is how
	// this test first passed while proving nothing at all about the case it exists for.
	for (AHFArticulatedActor* Fixture : Fixtures)
	{
		if (Openers >= 4 && Spinners >= 2)
		{
			break;
		}

		for (const FHFPartState& Part : Fixture->Parts)
		{
			const bool bSpins = Part.Motion.Revolves();
			const bool bOpens = (Part.Motion.Type == EHFMotionType::Hinge && FMath::Abs(Part.Motion.MaxAngleDegrees) > 1.0)
				|| (Part.Motion.Type == EHFMotionType::Slide && FMath::Abs(Part.Motion.MaxTravelCm) > 0.5);

			if (bSpins && Spinners >= 2)
			{
				continue;
			}
			if (bOpens && Openers >= 4)
			{
				continue;
			}
			if (!bSpins && !bOpens)
			{
				continue;
			}

			// A phase past a whole turn, and an open amount that is neither end, so a reset to a
			// default cannot be mistaken for the value surviving.
			Posed.Add({ Fixture, Part.PartId, bSpins, bSpins ? 2.75 : 0.6 });
			bSpins ? ++Spinners : ++Openers;
			break;
		}
	}

	AddInfo(FString::Printf(TEXT("Posing %d opening part(s) and %d spinning part(s) across the flat."),
		Openers, Spinners));

	if (!TestTrue(TEXT("The flat has posable fixtures to test"), Posed.Num() > 0))
	{
		return false;
	}

	const bool bAnySpin = Posed.ContainsByPredicate([](const FPosed& P) { return P.bSpins; });
	TestTrue(TEXT("And at least one of them spins, which is the case with no open amount"), bAnySpin);

	auto ApplyPoses = [&Posed]()
	{
		for (const FPosed& P : Posed)
		{
			if (AHFArticulatedActor* Fixture = P.Fixture.Get())
			{
				if (P.bSpins)
				{
					Fixture->SetPartSpinTurns(P.PartId, P.Value);
				}
				else
				{
					Fixture->SetPartOpenAmount(P.PartId, P.Value);
				}
			}
		}
	};

	auto CheckPoses = [this, &Posed](const TCHAR* Stage)
	{
		int32 Lost = 0;
		for (const FPosed& P : Posed)
		{
			AHFArticulatedActor* Fixture = P.Fixture.Get();
			if (Fixture == nullptr)
			{
				AddError(FString::Printf(TEXT("%s: a posed fixture no longer exists."), Stage));
				++Lost;
				continue;
			}

			const double Now = P.bSpins
				? Fixture->GetPartSpinTurns(P.PartId)
				: Fixture->GetPartOpenAmount(P.PartId);

			if (!FMath::IsNearlyEqual(Now, P.Value, 1e-6))
			{
				AddError(FString::Printf(
					TEXT("%s: '%s' part '%s' was posed at %.4f and now reads %.4f."),
					Stage, *Fixture->ElementId.ToString(), *P.PartId.ToString(), P.Value, Now));
				++Lost;
			}
		}
		return Lost;
	};

	ApplyPoses();
	TestEqual(TEXT("The poses take"), CheckPoses(TEXT("posed")), 0);

	// ------------------------------------------------------------------------------------- bake
	TSet<AHFArticulatedActor*> ToBake;
	for (const FPosed& P : Posed)
	{
		if (AHFArticulatedActor* Fixture = P.Fixture.Get())
		{
			ToBake.Add(Fixture);
		}
	}

	for (AHFArticulatedActor* Fixture : ToBake)
	{
		FHFBakeReport Report;
		if (!FHFBakeService::BakeElement(Fixture, Report))
		{
			AddError(FString::Printf(TEXT("'%s' did not bake: %s"),
				*Fixture->ElementId.ToString(), *Report.Summary()));
		}
	}

	TestEqual(TEXT("A bake does not disturb a pose"), CheckPoses(TEXT("after bake")), 0);

	// And the baked geometry is standing where the posed part is, not where it was shut.
	int32 Adrift = 0;
	for (const FPosed& P : Posed)
	{
		AHFArticulatedActor* Fixture = P.Fixture.Get();
		if (Fixture == nullptr)
		{
			continue;
		}

		UDynamicMeshComponent* Live = Fixture->GetPartComponent(P.PartId);
		TArray<UDynamicMeshComponent*> Sources;
		Fixture->GetBakeSourceComponents(Sources);
		const int32 Index = Sources.IndexOfByKey(Live);

		UStaticMeshComponent* Baked = Fixture->BakedParts.IsValidIndex(Index)
			? Fixture->BakedParts[Index].Component : nullptr;

		if (IsValid(Live) && IsValid(Baked)
			&& !Baked->GetComponentTransform().Equals(Live->GetComponentTransform(), 0.01f))
		{
			AddError(FString::Printf(
				TEXT("'%s' part '%s' is posed, but its baked mesh is not standing where the part is."),
				*Fixture->ElementId.ToString(), *P.PartId.ToString()));
			++Adrift;
		}
	}
	TestEqual(TEXT("The baked geometry stands in the posed position"), Adrift, 0);

	// ----------------------------------------------------------------------------------- unbake
	for (AHFArticulatedActor* Fixture : ToBake)
	{
		Fixture->SetRenderMode(EHFRenderMode::Dynamic);
	}
	TestEqual(TEXT("An unbake does not disturb a pose either"), CheckPoses(TEXT("after unbake")), 0);

	for (AHFArticulatedActor* Fixture : ToBake)
	{
		Fixture->SetRenderMode(EHFRenderMode::Baked);
	}
	TestEqual(TEXT("Nor does switching back"), CheckPoses(TEXT("after re-bake")), 0);

	// ---------------------------------------------------------------------------------- rebuild
	//
	// The whole house, which is what a re-read of the drawing does. A baked element is preserved
	// rather than respawned, and a respawned one has its pose put back from the captured table; both
	// paths have to arrive at the same place or half the flat slams shut on a rebuild.
	House->BuildGeometry();

	TestEqual(TEXT("A house rebuild puts every pose back, spin phases included"),
		CheckPoses(TEXT("after rebuild")), 0);

	// The bake has to have survived the rebuild too, or the flat quietly leaves the Lumen scene.
	int32 StillBaked = 0;
	for (const FPosed& P : Posed)
	{
		if (AHFArticulatedActor* Fixture = P.Fixture.Get())
		{
			if (Fixture->RenderMode == EHFRenderMode::Baked)
			{
				++StillBaked;
			}
		}
	}
	AddInfo(FString::Printf(TEXT("%d of %d posed fixture(s) are still showing baked geometry after the rebuild."),
		StillBaked, ToBake.Num()));
	TestEqual(TEXT("Every baked fixture is still baked after a rebuild"), StillBaked, ToBake.Num());

	return true;
}

// =============================================================== the aperture, in centimetres

/**
 * A BAKED sliding run still opens, measured in centimetres of visible aperture.
 *
 * THIS PROJECT HAS BEEN FOOLED BY EXACTLY THIS BEFORE. A pair of sliding leaves both driven out
 * together reported their full declared travel, passed every assertion about motion, and uncovered
 * nothing at all - they simply exchanged tracks. FHFPartMotion::bMasterOpens exists because of it.
 *
 * A bake can reproduce that failure in a new way: baked leaves that ride the wrong components, or
 * that ride the actor instead of the parts, can travel on paper while the baked geometry stays put.
 * So the assertion is not "the leaf moved", it is "this many centimetres of the run have nothing in
 * front of them" - and it is measured off the BAKED meshes, in the wardrobe's own space.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFBakeSliderApertureTest,
	"HouseForge.Bake.Articulation.ABakedSliderStillOpensInCentimetres", HF_TEST_FLAGS)

bool FHFBakeSliderApertureTest::RunTest(const FString& Parameters)
{
	using namespace HFBakeArticulation;

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("An editor world is open"), World))
	{
		return false;
	}

	AHFWardrobeActor* Wardrobe = World->SpawnActor<AHFWardrobeActor>(TestSite, FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("A wardrobe spawns"), Wardrobe))
	{
		return false;
	}
	ON_SCOPE_EXIT{ ForgetAssets(Wardrobe); if (IsValid(Wardrobe)) { Wardrobe->Destroy(); } };

	Wardrobe->ElementId = TEXT("WD_BakeSlider");
	Wardrobe->Wardrobe.Width = 180.0;
	Wardrobe->Wardrobe.Depth = 60.0;
	Wardrobe->Wardrobe.Height = 210.0;
	Wardrobe->Wardrobe.BayCount = 3;
	Wardrobe->Wardrobe.PlinthHeight = 10.0;
	Wardrobe->Wardrobe.MotionKind = EHFShutterMotion::Sliding;
	Wardrobe->Regenerate();

	const FName LeftId = AHFWardrobeActor::ShutterPartId(0);
	const FName RightId = AHFWardrobeActor::ShutterPartId(1);

	UDynamicMeshComponent* LiveLeft = Wardrobe->GetPartComponent(LeftId);
	UDynamicMeshComponent* LiveRight = Wardrobe->GetPartComponent(RightId);
	if (!TestNotNull(TEXT("The run has a left leaf"), LiveLeft)
		|| !TestNotNull(TEXT("The run has a right leaf"), LiveRight))
	{
		return false;
	}

	FHFBakeReport Report;
	if (!TestTrue(TEXT("The sliding wardrobe bakes"), FHFBakeService::BakeElement(Wardrobe, Report)))
	{
		AddError(Report.Summary());
		return false;
	}

	TArray<UDynamicMeshComponent*> Sources;
	Wardrobe->GetBakeSourceComponents(Sources);

	auto BakedFor = [&](UDynamicMeshComponent* Live) -> UStaticMeshComponent*
	{
		const int32 Index = Sources.IndexOfByKey(Live);
		return Wardrobe->BakedParts.IsValidIndex(Index) ? Wardrobe->BakedParts[Index].Component : nullptr;
	};

	UStaticMeshComponent* BakedLeft = BakedFor(LiveLeft);
	UStaticMeshComponent* BakedRight = BakedFor(LiveRight);
	if (!TestNotNull(TEXT("The left leaf baked"), BakedLeft) || !TestNotNull(TEXT("The right leaf baked"), BakedRight))
	{
		return false;
	}

	// Measured off the BAKED meshes. A sliding leaf is a slab, so its box is the leaf; nothing here
	// needs the sampling a curtain's overlapping folds do.
	auto SpanOf = [&](UStaticMeshComponent* Component, double& OutMin, double& OutMax)
	{
		FBox Box;
		if (!ActorSpaceBox(*Wardrobe, Component, Box))
		{
			return false;
		}
		OutMin = Box.Min.X;
		OutMax = Box.Max.X;
		return true;
	};

	Wardrobe->CloseAllParts();

	double LeftShutMin = 0.0, LeftShutMax = 0.0, RightShutMin = 0.0, RightShutMax = 0.0;
	if (!TestTrue(TEXT("The baked leaves can be measured"),
		SpanOf(BakedLeft, LeftShutMin, LeftShutMax) && SpanOf(BakedRight, RightShutMin, RightShutMax)))
	{
		return false;
	}

	const double RunMin = FMath::Min(LeftShutMin, RightShutMin);
	const double RunMax = FMath::Max(LeftShutMax, RightShutMax);
	const double RunWidth = RunMax - RunMin;

	// Sliding leaves lap at the meeting line rather than being separated by a reveal, because a reveal
	// between two sliding leaves is a hole straight into the wardrobe. It is also exactly what the
	// aperture is allowed to fall short of half the run by.
	const double Lap = LeftShutMax - RightShutMin;
	const double Required = RunWidth * 0.5 - Lap;

	AddInfo(FString::Printf(TEXT("Baked run %.1f cm wide, lapping %.1f cm; half the run less the lap is %.1f cm."),
		RunWidth, Lap, Required));

	TestTrue(TEXT("The baked leaves lap at the meeting line when shut"), Lap > 0.0);
	TestTrue(TEXT("The run is a real width, not a degenerate one"), RunWidth > 100.0);

	// ------------------------------------------------------------------- run the left-hand leaf
	Wardrobe->OpenRunFrom(LeftId, 1.0);

	double LeftOpenMin = 0.0, LeftOpenMax = 0.0, RightHeldMin = 0.0, RightHeldMax = 0.0;
	SpanOf(BakedLeft, LeftOpenMin, LeftOpenMax);
	SpanOf(BakedRight, RightHeldMin, RightHeldMax);

	const double LeftAperture = FMath::Min(LeftOpenMin, RightHeldMin) - RunMin;

	AddInfo(FString::Printf(TEXT("BAKED: running the left leaf uncovers %.1f cm at the left end."), LeftAperture));
	TestTrue(*FString::Printf(
			TEXT("A baked sliding run really opens at the LEFT end: %.1f cm uncovered, %.1f cm required"),
			LeftAperture, Required),
		LeftAperture >= Required - 0.01);

	// ------------------------------------------------------------------ run the right-hand leaf
	Wardrobe->OpenRunFrom(RightId, 1.0);

	double RightOpenMin = 0.0, RightOpenMax = 0.0, LeftHeldMin = 0.0, LeftHeldMax = 0.0;
	SpanOf(BakedRight, RightOpenMin, RightOpenMax);
	SpanOf(BakedLeft, LeftHeldMin, LeftHeldMax);

	const double RightAperture = RunMax - FMath::Max(RightOpenMax, LeftHeldMax);

	AddInfo(FString::Printf(TEXT("BAKED: running the right leaf uncovers %.1f cm at the right end."), RightAperture));
	TestTrue(*FString::Printf(
			TEXT("A baked sliding run really opens at the RIGHT end: %.1f cm uncovered, %.1f cm required"),
			RightAperture, Required),
		RightAperture >= Required - 0.01);

	// AND BOTH TOGETHER UNCOVER NOTHING NEW, which is the failure this whole measurement exists for:
	// two leaves of one run driven out together exchange tracks, report their full travel, and leave
	// the elevation exactly as covered as it was shut.
	Wardrobe->SetAllPartsOpenAmount(1.0);

	double BothLeftMin = 0.0, BothLeftMax = 0.0, BothRightMin = 0.0, BothRightMax = 0.0;
	SpanOf(BakedLeft, BothLeftMin, BothLeftMax);
	SpanOf(BakedRight, BothRightMin, BothRightMax);

	const double MasterAperture = FMath::Max(
		FMath::Min(BothLeftMin, BothRightMin) - RunMin,
		RunMax - FMath::Max(BothLeftMax, BothRightMax));

	AddInfo(FString::Printf(TEXT("BAKED: opening everything uncovers %.1f cm."), MasterAperture));
	TestTrue(*FString::Printf(
			TEXT("Opening everything on a baked run still uncovers %.1f cm rather than exchanging tracks"),
			MasterAperture),
		MasterAperture >= Required - 0.01);

	Wardrobe->CloseAllParts();
	return true;
}

// ================================================================= a part lost from the middle

/**
 * A part that disappears from the MIDDLE of a fixture does not hand its baked mesh to the next part.
 *
 * A wardrobe's parts are its body leaves and then its loft leaves. Narrow it by one bay and it loses
 * a BODY leaf, out of the middle, while every loft leaf above it stays. A sync that trimmed the list
 * to length dropped the LAST entry instead, and from that moment BakedParts[i] stood for a part it
 * was never baked from: the loft leaves wore the body leaves' assets, one slot out, and every re-bake
 * afterwards wrote the wrong geometry into the wrong asset path.
 *
 * The dropped part's baked component was the visible half. USceneComponent::OnComponentDestroyed
 * re-attaches a live child to its GRANDPARENT rather than destroying it, so the orphaned baked leaf
 * came back parented to the carcass and hung there - a baked shutter floating in the room, no longer
 * moving with anything.
 *
 * Nothing about either half logs, and both survive a screenshot.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFBakeMiddlePartDropTest,
	"HouseForge.Bake.Articulation.ADroppedMiddlePartDoesNotShuffleTheBakedMeshes", HF_TEST_FLAGS)

bool FHFBakeMiddlePartDropTest::RunTest(const FString& Parameters)
{
	using namespace HFBakeArticulation;

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("An editor world is open"), World))
	{
		return false;
	}

	AHFWardrobeActor* Wardrobe = SpawnLoftedWardrobe(World, 3);
	if (!TestNotNull(TEXT("A lofted wardrobe spawns"), Wardrobe))
	{
		return false;
	}
	ON_SCOPE_EXIT{ ForgetAssets(Wardrobe); if (IsValid(Wardrobe)) { Wardrobe->Destroy(); } };

	// Off, so the assertions below see the state the drop leaves behind rather than the state a
	// rebake would tidy it into. The defect is what a user's wardrobe looks like in between.
	Wardrobe->bAutoRebakeOnRegenerate = false;

	FHFBakeReport Report;
	if (!TestTrue(TEXT("The wardrobe bakes"), FHFBakeService::BakeElement(Wardrobe, Report)))
	{
		AddError(Report.Summary());
		return false;
	}

	// The loft leaves must come AFTER the body leaves, or this test is not testing anything.
	TArray<UDynamicMeshComponent*> Before;
	Wardrobe->GetBakeSourceComponents(Before);

	const FName LostId = AHFWardrobeActor::ShutterPartId(2);
	const FName KeptId = AHFWardrobeActor::LoftPartId(0);

	UDynamicMeshComponent* LostLeaf = Wardrobe->GetPartComponent(LostId);
	UDynamicMeshComponent* KeptLoft = Wardrobe->GetPartComponent(KeptId);
	if (!TestNotNull(TEXT("There is a third body leaf to lose"), LostLeaf)
		|| !TestNotNull(TEXT("And a loft leaf above it to keep"), KeptLoft))
	{
		return false;
	}

	const int32 LostIndex = Before.IndexOfByKey(LostLeaf);
	const int32 KeptIndex = Before.IndexOfByKey(KeptLoft);

	AddInfo(FString::Printf(TEXT("Body leaf '%s' is source %d; loft leaf '%s' is source %d of %d."),
		*LostId.ToString(), LostIndex, *KeptId.ToString(), KeptIndex, Before.Num()));

	if (!TestTrue(TEXT("The part about to be lost is not the last one - otherwise this proves nothing"),
		LostIndex >= 0 && KeptIndex > LostIndex))
	{
		return false;
	}

	UStaticMesh* LostAsset = Wardrobe->BakedParts[LostIndex].BakedMesh;
	UStaticMesh* KeptAsset = Wardrobe->BakedParts[KeptIndex].BakedMesh;
	if (!TestNotNull(TEXT("The doomed leaf baked an asset"), LostAsset)
		|| !TestNotNull(TEXT("And so did the loft leaf"), KeptAsset))
	{
		return false;
	}

	// ------------------------------------------------------------------ lose the middle part
	Wardrobe->Wardrobe.BayCount = 2;
	Wardrobe->Regenerate();

	TArray<UDynamicMeshComponent*> After;
	Wardrobe->GetBakeSourceComponents(After);

	TestEqual(TEXT("The baked part list follows the source components down"),
		Wardrobe->BakedParts.Num(), After.Num());
	TestTrue(TEXT("Which is fewer than it was"), After.Num() < Before.Num());

	UDynamicMeshComponent* LoftNow = Wardrobe->GetPartComponent(KeptId);
	if (!TestNotNull(TEXT("The loft leaf is still there"), LoftNow))
	{
		return false;
	}

	const int32 LoftIndexNow = After.IndexOfByKey(LoftNow);
	if (!TestTrue(TEXT("The loft leaf is still a bake source"), LoftIndexNow != INDEX_NONE))
	{
		return false;
	}

	AddInfo(FString::Printf(TEXT("After the drop the loft leaf is source %d of %d."),
		LoftIndexNow, After.Num()));

	// THE ASSERTION. The loft leaf kept its OWN asset rather than inheriting the lost body leaf's.
	UStaticMesh* LoftAssetNow = Wardrobe->BakedParts[LoftIndexNow].BakedMesh;

	TestEqual(TEXT("The loft leaf still wears its own baked mesh, not the lost leaf's"),
		LoftAssetNow, KeptAsset);
	TestTrue(TEXT("And in particular it has not inherited the dropped body leaf's mesh"),
		LoftAssetNow != LostAsset);

	// Every surviving baked component hangs on the part it stands in for, and on nothing else. A
	// component re-parented to the carcass renders in the right room and never moves again.
	int32 Misparented = 0;
	for (int32 Index = 0; Index < Wardrobe->BakedParts.Num(); ++Index)
	{
		UStaticMeshComponent* Component = Wardrobe->BakedParts[Index].Component;
		if (!IsValid(Component))
		{
			continue;
		}

		if (Component->GetAttachParent() != After[Index])
		{
			AddError(FString::Printf(
				TEXT("Baked part %d hangs on '%s' but stands in for '%s'."), Index,
				Component->GetAttachParent() ? *Component->GetAttachParent()->GetName() : TEXT("nothing"),
				*After[Index]->GetName()));
			++Misparented;
		}
	}
	TestEqual(TEXT("Every baked component hangs on the part it stands in for"), Misparented, 0);

	// And the dropped part's component is gone rather than left hanging on the carcass.
	for (UActorComponent* Component : Wardrobe->GetComponents())
	{
		UStaticMeshComponent* Baked = Cast<UStaticMeshComponent>(Component);
		if (Baked == nullptr || Baked->GetStaticMesh() != LostAsset)
		{
			continue;
		}

		AddError(FString::Printf(
			TEXT("The dropped leaf's baked mesh is still being drawn by '%s', attached to '%s'."),
			*Baked->GetName(),
			Baked->GetAttachParent() ? *Baked->GetAttachParent()->GetName() : TEXT("nothing")));
	}

	// The whole fixture still moves, which is the thing the shuffle would have broken silently.
	Wardrobe->OpenAllParts();

	int32 Adrift = 0;
	for (int32 Index = 1; Index < Wardrobe->BakedParts.Num(); ++Index)
	{
		UStaticMeshComponent* Baked = Wardrobe->BakedParts[Index].Component;
		if (IsValid(Baked) && IsValid(After[Index])
			&& !Baked->GetComponentTransform().Equals(After[Index]->GetComponentTransform(), 0.01f))
		{
			++Adrift;
		}
	}
	TestEqual(TEXT("Every surviving baked part still rides its live part once the wardrobe is opened"),
		Adrift, 0);

	Wardrobe->CloseAllParts();
	return true;
}

#undef HF_TEST_FLAGS

#endif // WITH_DEV_AUTOMATION_TESTS
