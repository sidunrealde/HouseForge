// Copyright Siddartha G. All Rights Reserved.

#include "HouseForgeEditor.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Actors/HFElementActors.h"
#include "Actors/HFFanActor.h"
#include "Actors/HFHouseActor.h"
#include "Actors/HFLightFixtureActor.h"
#include "Capture/HFViewingLight.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/DynamicMeshComponent.h"
#include "Components/LightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/RectLightComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Editor.h"
#include "Geometry/HFGenerators.h"
#include "Engine/DirectionalLight.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/SkyLight.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerStart.h"
#include "HFEditorSubsystem.h"
#include "Lighting/HFExposure.h"
#include "Lighting/HFInteriorLighting.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Model/HFCeilingTemplates.h"
#include "Model/HFSampleHouse.h"
#include "Model/HFTypes.h"
#include "Walkthrough/HFWalkthroughStart.h"

#define HF_TEST_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Does the flat actually have lights in it?
 *
 * The seam between the generators - which HFLightingTests measures on their own, headlessly - and
 * the level. Everything here needs a world, because everything here is about COMPONENTS: how many
 * were spawned, where they point, what units they are in, and whether asking twice produces twice
 * as many.
 *
 * The distinction is the same one HFWalkthroughTests draws about collision, and for the same
 * reason: a generator that produces a perfect list of light positions and a level with no lights in
 * it are indistinguishable from anywhere except here.
 */
namespace HouseForgeLightingActors
{
	using namespace UE::Geometry;

	UWorld* EditorWorld()
	{
		return GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	}

	void ClearHouseForgeActors(UWorld* World)
	{
		for (TActorIterator<AHFHouseActor> It(World); It; ++It)
		{
			It->ClearGeometry();
			It->Destroy();
		}
		for (TActorIterator<AHFElementActor> It(World); It; ++It)
		{
			It->Destroy();
		}
	}

	/** Every light component of a given kind belonging to one actor. */
	template <typename TLight>
	TArray<TLight*> LightsOn(const AActor* Actor)
	{
		TArray<TLight*> Found;
		if (IsValid(Actor))
		{
			Actor->GetComponents<TLight>(Found);
		}
		return Found;
	}

	/** Every light component of a given kind belonging to any element of one house. */
	template <typename TLight>
	TArray<TLight*> LightsIn(const AHFHouseActor* House)
	{
		TArray<TLight*> Found;
		for (AActor* Element : House->ElementActors)
		{
			Found.Append(LightsOn<TLight>(Element));
		}
		return Found;
	}

	/**
	 * The plain point lamps on an actor: a fitting's or a fan's, and NOT a ceiling's downlights.
	 *
	 * USpotLightComponent DERIVES FROM UPointLightComponent, so GetComponents<UPointLightComponent>
	 * returns every downlight in the room as well. Measured: a three-fitting test room came back
	 * with 23 point lights, twenty of which were the ceiling's spots, and the count assertion read
	 * as a catastrophic over-spawn rather than as the wrong question.
	 */
	TArray<UPointLightComponent*> PointLampsOn(const AActor* Actor)
	{
		TArray<UPointLightComponent*> Found;
		if (!IsValid(Actor))
		{
			return Found;
		}

		TArray<UPointLightComponent*> All;
		Actor->GetComponents<UPointLightComponent>(All);

		for (UPointLightComponent* Light : All)
		{
			if (Light != nullptr && Light->GetClass() == UPointLightComponent::StaticClass())
			{
				Found.Add(Light);
			}
		}
		return Found;
	}

	/** The plain point lamps on every element of one house. */
	TArray<UPointLightComponent*> PointLampsIn(const AHFHouseActor* House)
	{
		TArray<UPointLightComponent*> Found;
		for (AActor* Element : House->ElementActors)
		{
			Found.Append(PointLampsOn(Element));
		}
		return Found;
	}

	/**
	 * How many actors OF THE RIG are of a given class.
	 *
	 * Scoped to the rig rather than to the world, and deliberately. Counting the whole world makes
	 * the assertion depend on whatever map the editor happened to open - the engine's default
	 * template ships with a directional light, a sky light and an atmosphere of its own, and this
	 * test read that as the rig having spawned twice. What is being asserted is that HOUSEFORGE has
	 * one of each; a light somebody else put in the level is their business.
	 */
	int32 RigActorsOfClass(UWorld* World, UClass* Class)
	{
		int32 Count = 0;
		for (AActor* Actor : FHFInteriorLighting::FindIn(World))
		{
			if (IsValid(Actor) && Actor->IsA(Class))
			{
				++Count;
			}
		}
		return Count;
	}

	int32 CountOfClass(UWorld* World, UClass* Class)
	{
		int32 Count = 0;
		for (TActorIterator<AActor> It(World, Class); It; ++It)
		{
			if (IsValid(*It))
			{
				++Count;
			}
		}
		return Count;
	}

	/**
	 * A one-room house with whatever fixtures the caller wants, built into the level.
	 *
	 * Centimetres, so nothing here is converted: AHFHouseActor::SetSpec converts at ingest exactly
	 * once, and a spec declaring itself already in Unreal units passes through untouched.
	 */
	AHFHouseActor* BuildRoom(UWorld* World, const TArray<FHFFixture>& Fixtures,
		EHFCeilingTemplate Template = EHFCeilingTemplate::PlainBand,
		double SizeX = 460.0, double SizeY = 380.0)
	{
		ClearHouseForgeActors(World);

		FHFHouseSpec Spec;
		Spec.Name = TEXT("Lighting");
		Spec.Units = EHFUnits::Centimeters;
		Spec.UnitsSource = TEXT("test");

		FHFRoom& Room = Spec.Rooms.AddDefaulted_GetRef();
		Room.Id = TEXT("R1");
		Room.Type = EHFRoomType::Bedroom;
		Room.CeilingHeight = 300.0;
		Room.Boundary = { FVector2D(0, 0), FVector2D(SizeX, 0), FVector2D(SizeX, SizeY), FVector2D(0, SizeY) };

		auto AddWall = [&Spec](const FName& Id, const FVector2D& Start, const FVector2D& End)
		{
			FHFWall& Wall = Spec.Walls.AddDefaulted_GetRef();
			Wall.Id = Id;
			Wall.Start = Start;
			Wall.End = End;
			Wall.Thickness = 11.5;
			Wall.Height = 300.0;
		};

		// Short of the corners, so no two walls share a footprint. A boolean at a corner where two
		// equal walls meet exactly end to end is a coin toss the mesh library loses, and the
		// resulting z-fight warning would drown whatever the test is about. The same dodge
		// HFCeilingDependentsTests uses, for the same reason.
		AddWall(TEXT("W_South"), FVector2D(0, 0), FVector2D(SizeX, 0));
		AddWall(TEXT("W_East"), FVector2D(SizeX, 20), FVector2D(SizeX, SizeY - 20));
		AddWall(TEXT("W_North"), FVector2D(SizeX, SizeY), FVector2D(0, SizeY));
		AddWall(TEXT("W_West"), FVector2D(0, SizeY - 20), FVector2D(0, 20));

		FHFFalseCeiling& Ceiling = Spec.FalseCeilings.AddDefaulted_GetRef();
		Ceiling.Id = TEXT("FC1");
		Ceiling.RoomId = TEXT("R1");
		Ceiling.Template = Template;

		Spec.Fixtures = Fixtures;

		AHFHouseActor* House = World->SpawnActor<AHFHouseActor>();
		if (House == nullptr)
		{
			return nullptr;
		}

		House->SetSpec(Spec);
		House->BuildGeometry();

		return House;
	}

	/** A light fixture drawn at a plan position, marked this deep below the ceiling. */
	FHFFixture LightFixture(const FName& Id, const FVector2D& Position, double Height)
	{
		FHFFixture F;
		F.Id = Id;
		F.RoomId = TEXT("R1");
		F.Type = EHFFixtureType::LightFixture;
		F.Position = Position;
		F.Footprint = FVector2D(26.0, 26.0);
		F.Height = Height;
		return F;
	}

	struct FMeshFingerprint
	{
		int32 Vertices = 0;
		int32 Triangles = 0;
		TArray<FVector3d> Positions;
		TArray<int32> Groups;

		bool operator==(const FMeshFingerprint& Other) const
		{
			return Vertices == Other.Vertices && Triangles == Other.Triangles
				&& Positions == Other.Positions && Groups == Other.Groups;
		}
	};

	FMeshFingerprint Fingerprint(const UDynamicMeshComponent* Component)
	{
		FMeshFingerprint Print;
		if (Component == nullptr)
		{
			return Print;
		}

		Component->ProcessMesh([&Print](const FDynamicMesh3& Mesh)
		{
			Print.Vertices = Mesh.VertexCount();
			Print.Triangles = Mesh.TriangleCount();

			for (const int32 Vid : Mesh.VertexIndicesItr())
			{
				Print.Positions.Add(Mesh.GetVertex(Vid));
			}
			for (const int32 Tid : Mesh.TriangleIndicesItr())
			{
				Print.Groups.Add(Mesh.GetTriangleGroup(Tid));
			}
		});
		return Print;
	}
}

// ---------------------------------------------------------------------------------------------
//
// A LIGHT FIXTURE IN A DRAWING BECOMES A LIGHT IN THE LEVEL.
//
// EHFFixtureType::LightFixture had no row in AHFHouseActor's recipe table, so this is the assertion
// that the row is there and that it does what a row is for: one fixture, one actor, one lamp.
//
// The counts come off the SPEC rather than being literals, so adding a fitting to the test house
// cannot leave a stale number behind.
//
// ---------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFLightFixturesBecomeLightsTest,
	"HouseForge.Lighting.LightFixturesInASpecBecomeLights", HF_TEST_FLAGS)

bool FHFLightFixturesBecomeLightsTest::RunTest(const FString& Parameters)
{
	using namespace HouseForgeLightingActors;

	UWorld* World = EditorWorld();
	if (!TestNotNull(TEXT("An editor world is open"), World))
	{
		return false;
	}

	// Three fittings, and deliberately not three of the same thing: two panels screwed to the
	// soffit and one pendant hanging on a flex, because the panel-or-pendant decision is made per
	// fixture and a test of three identical rows would never exercise it.
	const TArray<FHFFixture> Fixtures =
	{
		LightFixture(TEXT("F_Light_A"), FVector2D(150.0, 190.0), 8.0),
		LightFixture(TEXT("F_Light_B"), FVector2D(310.0, 190.0), 8.0),
		LightFixture(TEXT("F_Pendant"), FVector2D(230.0, 300.0), 75.0)
	};

	AHFHouseActor* House = BuildRoom(World, Fixtures);
	if (!TestNotNull(TEXT("The test room was built"), House))
	{
		return false;
	}
	ON_SCOPE_EXIT{ if (IsValid(House)) { House->ClearGeometry(); House->Destroy(); } };

	// ------------------------------------------------------------------ one actor per fixture
	TMap<FName, AHFLightFixtureActor*> Built;
	for (AActor* Element : House->ElementActors)
	{
		if (AHFLightFixtureActor* Fitting = Cast<AHFLightFixtureActor>(Element))
		{
			Built.Add(Fitting->ElementId, Fitting);
		}
	}

	int32 Declared = 0;
	for (const FHFFixture& Fixture : House->Spec.Fixtures)
	{
		if (Fixture.Type != EHFFixtureType::LightFixture)
		{
			continue;
		}

		++Declared;
		TestTrue(*FString::Printf(TEXT("'%s' became an actor"), *Fixture.Id.ToString()),
			Built.Contains(Fixture.Id));
	}

	if (!TestEqual(TEXT("Every light fixture in the spec became an actor"), Built.Num(), Declared))
	{
		return false;
	}
	TestEqual(TEXT("...and the spec declared the three that were put in it"), Declared, 3);

	// ------------------------------------------------------------------ one lamp per fitting
	const TArray<UPointLightComponent*> Lamps = PointLampsIn(House);

	AddInfo(FString::Printf(TEXT("%d light fixtures, %d point lamps (excluding ceiling downlights, ")
		TEXT("which are spot lights and therefore point lights too)."), Built.Num(), Lamps.Num()));

	TestEqual(TEXT("A spec with three light positions produces three lights"), Lamps.Num(), Declared);

	for (const TPair<FName, AHFLightFixtureActor*>& Pair : Built)
	{
		AHFLightFixtureActor* Fitting = Pair.Value;
		const FString Name = Pair.Key.ToString();

		const TArray<UPointLightComponent*> Own = PointLampsOn(Fitting);
		if (!TestEqual(*FString::Printf(TEXT("'%s' has exactly one lamp"), *Name), Own.Num(), 1))
		{
			continue;
		}

		UPointLightComponent* Lamp = Own[0];

		// AT THE LENS. A lamp anywhere else is a lamp the fitting's own body does not shade, which
		// is how a surface-mounted panel ends up lighting the plenum above it as brightly as the
		// room below.
		const FVector Expected = Fitting->GetActorTransform().TransformPosition(
			FVector(FHFLuminaireKit::LensCentre(Fitting->Luminaire)));

		TestTrue(*FString::Printf(TEXT("'%s' has its lamp at its lens"), *Name),
			FVector::Dist(Lamp->GetComponentLocation(), Expected) < 0.01);

		// BELOW THE CEILING AND ABOVE THE FLOOR. The cheapest check that the ceiling-mounted
		// reading of BaseZ was applied at all: a fitting placed from the floor instead would be at
		// ankle height, and one placed at the slab would be inside it.
		const double Z = Lamp->GetComponentLocation().Z;
		TestTrue(*FString::Printf(TEXT("'%s' hangs in the room at %.0f cm"), *Name, Z),
			Z > 100.0 && Z < 300.0);

		// Movable, or it is a light waiting for a lighting build that a project with static
		// lighting off can never give it.
		TestEqual(*FString::Printf(TEXT("'%s' is movable"), *Name),
			Lamp->Mobility, EComponentMobility::Movable);
	}

	// ------------------------------------------------------------------ and asking twice does not double
	//
	// Every one of these actors regenerates on a property change, and a rebuild that adds a lamp
	// rather than replacing one is a room that gets a stop brighter every time somebody drags a
	// slider - which is invisible until it is far too bright.
	for (const TPair<FName, AHFLightFixtureActor*>& Pair : Built)
	{
		Pair.Value->RebuildLights();
		Pair.Value->RebuildLights();
		Pair.Value->Regenerate();
	}

	TestEqual(TEXT("Rebuilding does not accumulate lamps"), PointLampsIn(House).Num(), Declared);

	// And switching one off actually turns it off, which is what makes bBuildLights a control
	// rather than a comment.
	AHFLightFixtureActor* First = Built.CreateIterator().Value();
	First->bBuildLights = false;
	TestEqual(TEXT("A fitting with its lighting switched off builds no lamp"), First->RebuildLights(), 0);
	TestEqual(TEXT("...and the level has one fewer"), PointLampsIn(House).Num(), Declared - 1);

	return true;
}

// ---------------------------------------------------------------------------------------------
//
// RECESSED LIGHT POSITIONS ON A CEILING BECOME SPOTLIGHTS POINTING DOWN.
//
// FHFFalseCeiling::LightPositions has carried plan coordinates since the beginning. The geometry
// milestone gave them a bore, a trim ring and an aperture; this is the assertion that a lamp
// actually sits up that aperture, and that it points at the floor rather than at whatever
// MakeFromZ felt like.
//
// ---------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFDownlightsBecomeSpotsTest,
	"HouseForge.Lighting.RecessedLightPositionsBecomeSpotLightsAimedDown", HF_TEST_FLAGS)

bool FHFDownlightsBecomeSpotsTest::RunTest(const FString& Parameters)
{
	using namespace HouseForgeLightingActors;

	UWorld* World = EditorWorld();
	if (!TestNotNull(TEXT("An editor world is open"), World))
	{
		return false;
	}

	// A plain band, which is the treatment whose entire point is a run of recessed downlights in it.
	// The template lays the positions out itself, so the count under test is one this plugin
	// actually generates rather than one invented here.
	AHFHouseActor* House = BuildRoom(World, {}, EHFCeilingTemplate::PlainBand);
	if (!TestNotNull(TEXT("The test room was built"), House))
	{
		return false;
	}
	ON_SCOPE_EXIT{ if (IsValid(House)) { House->ClearGeometry(); House->Destroy(); } };

	AHFCeilingActor* Ceiling = nullptr;
	for (AActor* Element : House->ElementActors)
	{
		if (AHFCeilingActor* Found = Cast<AHFCeilingActor>(Element))
		{
			Ceiling = Found;
			break;
		}
	}

	if (!TestNotNull(TEXT("The room has a false ceiling actor"), Ceiling))
	{
		return false;
	}

	const int32 Recorded = Ceiling->Ceiling.LightPositions.Num();
	const TArray<FVector> Apertures = Ceiling->DownlightPositions();

	AddInfo(FString::Printf(TEXT("%d light positions recorded, %d apertures resolved."),
		Recorded, Apertures.Num()));

	if (!TestTrue(TEXT("The band template records light positions at all"), Recorded > 0))
	{
		return false;
	}

	// EVERY RECORDED POSITION IS BUILT. The generator filters positions to the band zone, which is
	// right - a downlight drawn out in the open centre of a peripheral ceiling has no soffit to be
	// recessed into - but a band template that laid its own lights outside its own band would be a
	// template quietly losing most of them, and nothing else would say so.
	TestEqual(TEXT("Every position the ceiling records lands in its band"),
		Apertures.Num(), Recorded);

	const TArray<USpotLightComponent*> Spots = LightsOn<USpotLightComponent>(Ceiling);

	TestEqual(TEXT("A ceiling with N light positions has N spot lights"), Spots.Num(), Recorded);

	// --------------------------------------------------------------------------- and they point down
	for (int32 Index = 0; Index < Spots.Num(); ++Index)
	{
		USpotLightComponent* Spot = Spots[Index];

		// A spot emits along its own +X. Straight down is a forward vector of (0, 0, -1), and the
		// tolerance is tight because there is no reason for a ceiling downlight to be off axis at
		// all - this is checking a rotation that is either right or has been left at identity.
		const FVector Forward = Spot->GetForwardVector();

		TestTrue(*FString::Printf(TEXT("Downlight %d points at the floor (forward Z %.3f)"),
			Index, Forward.Z), Forward.Z < -0.999);

		TestTrue(*FString::Printf(TEXT("Downlight %d has a cone to shape the scallop"), Index),
			Spot->OuterConeAngle > 0.0f && Spot->OuterConeAngle < 90.0f);

		// A real COB has a lens a couple of centimetres across, and that width is most of what
		// makes the pool on the wall soft rather than a stencil.
		TestTrue(*FString::Printf(TEXT("Downlight %d has a source size"), Index),
			Spot->SourceRadius > 0.0f);
	}

	// ---------------------------------------------------------- and the lamp is up inside the can
	//
	// Not at the plasterboard. A light parented at the soffit is shaded by its own trim ring, which
	// is the failure AHFCeilingActor's own comment records; the aperture is the whole reason
	// DownlightPositions returns what it returns.
	if (Spots.Num() > 0 && Apertures.Num() > 0)
	{
		double Nearest = TNumericLimits<double>::Max();
		for (const FVector& Aperture : Apertures)
		{
			Nearest = FMath::Min(Nearest, FVector::Dist(Spots[0]->GetComponentLocation(), Aperture));
		}

		TestTrue(FString::Printf(TEXT("A downlight sits at a resolved aperture (%.2f cm away)"), Nearest),
			Nearest < 0.01);
	}

	// ------------------------------------------------------------------ and rebuilding does not double
	Ceiling->RebuildLights();
	Ceiling->RebuildLights();

	TestEqual(TEXT("Rebuilding a ceiling does not accumulate downlights"),
		LightsOn<USpotLightComponent>(Ceiling).Num(), Recorded);

	return true;
}

// ---------------------------------------------------------------------------------------------
//
// A COVE CEILING IN A LEVEL HAS A LIGHT IN ITS TROUGH.
//
// The generator half of this is HouseForge.Lighting.ACoveProducesAnEmissiveStrip. This is the
// level half: a rect light per run, lying in the trough, FACING UP. A cove whose light faces down
// is a bright line at the ceiling and nothing else, which is exactly what the detail is not.
//
// ---------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFCoveWashTest,
	"HouseForge.Lighting.ACoveCeilingWashesTheSurfaceAboveIt", HF_TEST_FLAGS)

bool FHFCoveWashTest::RunTest(const FString& Parameters)
{
	using namespace HouseForgeLightingActors;

	UWorld* World = EditorWorld();
	if (!TestNotNull(TEXT("An editor world is open"), World))
	{
		return false;
	}

	AHFHouseActor* House = BuildRoom(World, {}, EHFCeilingTemplate::Cove);
	if (!TestNotNull(TEXT("The test room was built"), House))
	{
		return false;
	}
	ON_SCOPE_EXIT{ if (IsValid(House)) { House->ClearGeometry(); House->Destroy(); } };

	AHFCeilingActor* Ceiling = nullptr;
	for (AActor* Element : House->ElementActors)
	{
		if (AHFCeilingActor* Found = Cast<AHFCeilingActor>(Element))
		{
			Ceiling = Found;
			break;
		}
	}

	if (!TestNotNull(TEXT("The room has a false ceiling actor"), Ceiling))
	{
		return false;
	}

	if (!TestEqual(TEXT("The template resolved to a cove"),
		Ceiling->Ceiling.Style, EHFCeilingStyle::Cove))
	{
		return false;
	}

	const TArray<URectLightComponent*> Strips = LightsOn<URectLightComponent>(Ceiling);
	const int32 Runs = FHFGenerators::CeilingCoveLights(Ceiling->Ceiling, Ceiling->Room).Num();

	AddInfo(FString::Printf(TEXT("%d cove runs, %d rect lights."), Runs, Strips.Num()));

	if (!TestTrue(TEXT("The cove has runs to light"), Runs > 0))
	{
		return false;
	}
	TestEqual(TEXT("One rect light per cove run"), Strips.Num(), Runs);

	for (int32 Index = 0; Index < Strips.Num(); ++Index)
	{
		URectLightComponent* Strip = Strips[Index];

		// A rect light emits along its own +X. THE WHOLE POINT OF A COVE is that this is up.
		TestTrue(*FString::Printf(TEXT("Cove run %d faces up (forward Z %.3f)"),
			Index, Strip->GetForwardVector().Z), Strip->GetForwardVector().Z > 0.99);

		// A run, not a square. A cove strip is metres long and centimetres wide, and a rect light
		// whose two dimensions had been swapped would still pass every count above.
		TestTrue(*FString::Printf(TEXT("Cove run %d is long and thin (%.0f by %.0f cm)"),
			Index, Strip->SourceWidth, Strip->SourceHeight),
			Strip->SourceWidth > Strip->SourceHeight * 4.0);

		// It sits above the finished soffit, in the trough, where nobody standing in the room can
		// see it.
		const double SoffitZ = Ceiling->Room.FloorZ + Ceiling->Room.CeilingHeight - Ceiling->Ceiling.Drop;
		TestTrue(*FString::Printf(TEXT("Cove run %d is up in the trough, not hanging below the lip"), Index),
			Strip->GetComponentLocation().Z > SoffitZ);
	}

	return true;
}

// ---------------------------------------------------------------------------------------------
//
// A CEILING FAN CARRIES A LAMP; AN EXTRACT DOES NOT.
//
// In these flats the fan is very often the only thing on a bedroom ceiling, so a fan with no lamp
// is a bedroom lit only round its edges. The extract half of the assertion is what stops that
// becoming "everything with blades glows", which would put a light inside every bathroom wall.
//
// ---------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFFanLightKitTest,
	"HouseForge.Lighting.ACeilingFanCarriesALampAndAnExtractDoesNot", HF_TEST_FLAGS)

bool FHFFanLightKitTest::RunTest(const FString& Parameters)
{
	using namespace HouseForgeLightingActors;

	UWorld* World = EditorWorld();
	if (!TestNotNull(TEXT("An editor world is open"), World))
	{
		return false;
	}

	FHFFixture Fan;
	Fan.Id = TEXT("FAN1");
	Fan.RoomId = TEXT("R1");
	Fan.Type = EHFFixtureType::CeilingFan;
	Fan.Position = FVector2D(230.0, 190.0);
	Fan.Footprint = FVector2D(120.0, 120.0);
	Fan.Height = 30.0;

	FHFFixture Extract;
	Extract.Id = TEXT("EXH1");
	Extract.RoomId = TEXT("R1");
	Extract.Type = EHFFixtureType::ExhaustFan;
	Extract.Position = FVector2D(230.0, 6.0);
	Extract.Footprint = FVector2D(25.0, 10.0);
	Extract.BaseZ = 230.0;
	Extract.Height = 25.0;
	Extract.AnchorWallId = TEXT("W_South");

	AHFHouseActor* House = BuildRoom(World, { Fan, Extract });
	if (!TestNotNull(TEXT("The test room was built"), House))
	{
		return false;
	}
	ON_SCOPE_EXIT{ if (IsValid(House)) { House->ClearGeometry(); House->Destroy(); } };

	AHFFanActor* CeilingFan = nullptr;
	AHFFanActor* ExtractFan = nullptr;

	for (AActor* Element : House->ElementActors)
	{
		if (AHFFanActor* Found = Cast<AHFFanActor>(Element))
		{
			(Found->Fan.Kind == EHFFanKind::Ceiling ? CeilingFan : ExtractFan) = Found;
		}
	}

	if (!TestNotNull(TEXT("The ceiling fan was built"), CeilingFan)
		|| !TestNotNull(TEXT("The extract was built"), ExtractFan))
	{
		return false;
	}

	const TArray<UPointLightComponent*> FanLamps = PointLampsOn(CeilingFan);
	const TArray<UPointLightComponent*> ExtractLamps = PointLampsOn(ExtractFan);

	TestTrue(TEXT("A ceiling fan is seeded with a light kit"), CeilingFan->bHasLightKit);
	TestFalse(TEXT("An extract is not"), ExtractFan->bHasLightKit);

	if (!TestEqual(TEXT("The ceiling fan has one lamp"), FanLamps.Num(), 1))
	{
		return false;
	}
	TestEqual(TEXT("The extract has none"), ExtractLamps.Num(), 0);

	// UNDER THE MOTOR, not above it. A lamp on the wrong side of the housing lights the ceiling
	// through the fan, and the blades below it strobe across the room in every frame.
	UPointLightComponent* Lamp = FanLamps[0];

	const FVector FanOrigin = CeilingFan->GetActorLocation();
	const double Below = FanOrigin.Z - Lamp->GetComponentLocation().Z;

	AddInfo(FString::Printf(TEXT("Fan lamp %.1f cm below the mounting point; overall depth %.1f."),
		Below, CeilingFan->Fan.OverallDepth()));

	TestTrue(TEXT("The fan lamp hangs below the point the fan is fixed at"), Below > 0.0);
	TestTrue(TEXT("...and at least as far down as the motor housing reaches"),
		Below >= CeilingFan->Fan.OverallDepth());

	// Not miles below it either: a lamp much past the motor would be hanging in the blade plane.
	TestTrue(TEXT("...and not below the blades"),
		Below <= CeilingFan->Fan.OverallDepth() + 10.0);

	// ------------------------------------------------------------------ switching the kit off works
	CeilingFan->bHasLightKit = false;
	TestEqual(TEXT("A fan with no light kit builds no lamp"), CeilingFan->RebuildLights(), 0);
	TestEqual(TEXT("...and has none afterwards"), PointLampsOn(CeilingFan).Num(), 0);

	CeilingFan->bHasLightKit = true;
	CeilingFan->RebuildLights();
	CeilingFan->RebuildLights();
	TestEqual(TEXT("Turning it back on and rebuilding twice leaves exactly one"),
		PointLampsOn(CeilingFan).Num(), 1);

	return true;
}

// ---------------------------------------------------------------------------------------------
//
// EVERY LIGHT IN THE FLAT IS A REAL LAMP, IN PHOTOMETRIC UNITS.
//
// The brief's condition, asserted where it can actually be broken: on the components of the
// reference flat as built. ELightUnits::Unitless is the engine default, and a light left on it is
// a light whose "intensity 5000" means nothing at all - it is neither lumens nor candelas, and no
// exposure reasoned about in EV100 can be right for it.
//
// The bands are wide on purpose. This is not a test of taste; it is a test that catches a watt
// mistaken for a lumen, a candela figure typed into a lumens field, and a stray zero.
//
// ---------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFPhotometricUnitsTest,
	"HouseForge.Lighting.EveryLightInTheFlatIsADomesticLamp", HF_TEST_FLAGS)

bool FHFPhotometricUnitsTest::RunTest(const FString& Parameters)
{
	using namespace HouseForgeLightingActors;

	UWorld* World = EditorWorld();
	if (!TestNotNull(TEXT("An editor world is open"), World))
	{
		return false;
	}

	ClearHouseForgeActors(World);

	AHFHouseActor* House = World->SpawnActor<AHFHouseActor>();
	if (!TestNotNull(TEXT("A house actor spawns"), House))
	{
		return false;
	}
	ON_SCOPE_EXIT{ if (IsValid(House)) { House->ClearGeometry(); House->Destroy(); } };

	House->SetSpec(FHFSampleHouse::Make2BHK());
	House->BuildGeometry();

	const TArray<ULocalLightComponent*> Lights = LightsIn<ULocalLightComponent>(House);

	AddInfo(FString::Printf(TEXT("The reference flat has %d local lights in it."), Lights.Num()));

	if (!TestTrue(TEXT("The reference flat is lit at all"), Lights.Num() > 0))
	{
		return false;
	}

	int32 Unitless = 0;
	int32 Implausible = 0;
	double TotalLumens = 0.0;

	for (ULocalLightComponent* Light : Lights)
	{
		const FString Name = Light->GetOwner() != nullptr
			? Light->GetOwner()->GetName() : Light->GetName();

		if (Light->IntensityUnits == ELightUnits::Unitless)
		{
			++Unitless;
			AddError(FString::Printf(
				TEXT("'%s' is in Unitless intensity: its figure means neither lumens nor candelas, ")
				TEXT("so no exposure reasoned about in EV100 can be right for it."), *Name));
			continue;
		}

		// Lumens throughout, because every one of these is a LAMP - a thing sold by its total
		// output. Candelas would be the unit for something aimed, and nothing here is.
		if (Light->IntensityUnits == ELightUnits::Lumens)
		{
			// From a 5 W night light to a bright architectural strip run. A cove light is scaled by
			// its own length and is legitimately the largest figure in the flat.
			if (Light->Intensity < 100.0f || Light->Intensity > 12000.0f)
			{
				++Implausible;
				AddError(FString::Printf(TEXT("'%s' is %.0f lm, which is not a domestic lamp."),
					*Name, Light->Intensity));
			}

			TotalLumens += Light->Intensity;
		}

		// A light with no reach lights nothing; one with unbounded reach is evaluated for every
		// pixel of every room in the flat.
		TestTrue(*FString::Printf(TEXT("'%s' has a bounded reach"), *Name),
			Light->AttenuationRadius > 0.0f && Light->AttenuationRadius <= 5000.0f);

		// Warm white, which is what these flats are lit with, and set as a TEMPERATURE rather than
		// as a colour filter - SetLightColor changes effective intensity, so carrying the warmth
		// there would make every lumens figure above a lie.
		TestTrue(*FString::Printf(TEXT("'%s' states a colour temperature"), *Name),
			Light->bUseTemperature);
		TestTrue(*FString::Printf(TEXT("'%s' is a domestic white (%.0f K)"), *Name, Light->Temperature),
			Light->Temperature >= 2400.0f && Light->Temperature <= 6500.0f);
	}

	TestEqual(TEXT("No light in the flat is in Unitless intensity"), Unitless, 0);
	TestEqual(TEXT("Every light in the flat is a plausible domestic lamp"), Implausible, 0);

	AddInfo(FString::Printf(TEXT("Total installed output: %.0f lm across %d lights."),
		TotalLumens, Lights.Num()));

	// A whole 2BHK lit to a domestic standard is a few tens of thousands of lumens installed - the
	// rule of thumb is 10 to 20 lm per square foot of floor, and this flat is about 980 sq ft. A
	// figure far under that is a flat nobody could read a book in; far over it is a showroom.
	TestTrue(TEXT("The flat is lit to something like a domestic level"),
		TotalLumens > 5000.0 && TotalLumens < 200000.0);

	return true;
}

// ---------------------------------------------------------------------------------------------
//
// THE FLAT SITS UNDER A SKY, AT AN EXPOSURE SOMEBODY CHOSE.
//
// Without this the level is black: generated geometry in an empty world has no sun, no sky and no
// exposure, and a render of it says nothing about what was built.
//
// The assertion that matters most is the SINGULARITY one. Two unbound post-process volumes in a
// level do not average - they are resolved by priority and the loser contributes nothing - so a
// level that has quietly acquired a second rig is a level where changing the exposure appears to
// do nothing at all.
//
// ---------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFInteriorLightingRigTest,
	"HouseForge.Lighting.TheLevelGetsASkyAndAnInteriorExposure", HF_TEST_FLAGS)

bool FHFInteriorLightingRigTest::RunTest(const FString& Parameters)
{
	using namespace HouseForgeLightingActors;

	UWorld* World = EditorWorld();
	UHFEditorSubsystem* Editor = GEditor ? GEditor->GetEditorSubsystem<UHFEditorSubsystem>() : nullptr;

	if (!TestNotNull(TEXT("An editor world is open"), World)
		|| !TestNotNull(TEXT("There is a subsystem"), Editor))
	{
		return false;
	}

	// From nothing, so this measures spawning rather than whatever an earlier test left behind.
	Editor->RemoveInteriorLighting();
	Editor->RemoveViewingLight();

	TestEqual(TEXT("The level starts with no lighting rig"), FHFInteriorLighting::FindIn(World).Num(), 0);

	// AND WITH A PLACEHOLDER RIG IN IT, because the interesting case is the one where a level has
	// been screenshotted before it was lit - which is every level this plugin has ever built.
	Editor->EnsureViewingLight();
	const int32 Placeholders = FHFViewingLight::FindIn(World).Num();
	TestTrue(TEXT("A placeholder rig can be put in first"), Placeholders > 0);

	const int32 Actors = Editor->EnsureInteriorLighting();
	AddInfo(FString::Printf(TEXT("The lighting rig is %d actors."), Actors));

	if (!TestTrue(TEXT("The rig spawns"), Actors > 0))
	{
		return false;
	}

	// The placeholder is scaffolding and its own header says the lighting milestone deletes it.
	TestEqual(TEXT("The placeholder rig is gone"), FHFViewingLight::FindIn(World).Num(), 0);

	TestEqual(TEXT("Exactly one sun"), RigActorsOfClass(World, ADirectionalLight::StaticClass()), 1);
	TestEqual(TEXT("Exactly one sky light"), RigActorsOfClass(World, ASkyLight::StaticClass()), 1);
	TestEqual(TEXT("Exactly one sky atmosphere"), RigActorsOfClass(World, ASkyAtmosphere::StaticClass()), 1);
	TestEqual(TEXT("Exactly one exposure volume"), RigActorsOfClass(World, APostProcessVolume::StaticClass()), 1);

	// ------------------------------------------------------------------------------- idempotence
	Editor->EnsureInteriorLighting();
	Editor->EnsureInteriorLighting();

	TestEqual(TEXT("Asking again does not add a second rig"), FHFInteriorLighting::FindIn(World).Num(), Actors);
	TestEqual(TEXT("Still exactly one sun"), RigActorsOfClass(World, ADirectionalLight::StaticClass()), 1);

	// AND THE PLACEHOLDER REFUSES TO COME BACK. Captures call this, so without the guard every
	// screenshot of a lit flat would add a second sun to it.
	TestEqual(TEXT("The placeholder refuses to spawn over a lit level"), Editor->EnsureViewingLight(), 0);
	TestEqual(TEXT("...and none appeared"), FHFViewingLight::FindIn(World).Num(), 0);

	// ------------------------------------------------------------------------------ what it is set to
	ADirectionalLight* Sun = nullptr;
	APostProcessVolume* Volume = nullptr;
	ASkyLight* Sky = nullptr;

	for (AActor* Actor : FHFInteriorLighting::FindIn(World))
	{
		if (ADirectionalLight* AsSun = Cast<ADirectionalLight>(Actor)) { Sun = AsSun; }
		if (APostProcessVolume* AsVolume = Cast<APostProcessVolume>(Actor)) { Volume = AsVolume; }
		if (ASkyLight* AsSky = Cast<ASkyLight>(Actor)) { Sky = AsSky; }
	}

	if (TestNotNull(TEXT("The rig has a sun"), Sun))
	{
		UDirectionalLightComponent* Light = Sun->GetComponent();

		// Lux, and daylight rather than a studio lamp. A directional light's intensity is already
		// in lux, so this is a plausibility check on the figure rather than on the unit.
		TestTrue(FString::Printf(TEXT("The sun is a daylight figure (%.0f lux)"), Light->Intensity),
			Light->Intensity >= 1000.0f && Light->Intensity <= 120000.0f);

		TestEqual(TEXT("The sun is movable, since nothing here is ever lighting-built"),
			Sun->GetRootComponent()->Mobility, EComponentMobility::Movable);

		TestTrue(TEXT("The sun drives the atmosphere, so a window frames a sky"),
			Light->bAtmosphereSunLight);
	}

	if (TestNotNull(TEXT("The rig has a sky light"), Sky))
	{
		USkyLightComponent* Light = Sky->GetLightComponent();

		// Movable, or a sky light contributes nothing without a lighting build. ASkyLight has no
		// SetMobility of its own, which is exactly the trap worth a standing check.
		TestEqual(TEXT("The sky light is movable"), Light->Mobility, EComponentMobility::Movable);

		// Occluded, which is the substantive difference from the placeholder rig: an unoccluded sky
		// light indoors puts skylight on the inside face of every wall in the dwelling.
		TestTrue(TEXT("The sky light is occluded"), Light->CastShadows);
	}

	if (TestNotNull(TEXT("The rig has an exposure volume"), Volume))
	{
		// UNBOUND. A bounded volume misses a camera put outside the flat looking in, and it misses
		// the PIE pawn until it walks inside.
		TestTrue(TEXT("The exposure volume is unbound"), Volume->bUnbound);

		const FPostProcessSettings& S = Volume->Settings;

		// Every bOverride_ bit, because a field written without its override is silently ignored -
		// there is no warning, and the volume simply does nothing.
		TestTrue(TEXT("Exposure method is overridden"), S.bOverride_AutoExposureMethod != 0);
		TestEqual(TEXT("Exposure is manual, not adapted"), S.AutoExposureMethod, AEM_Manual);

		TestTrue(TEXT("The physical camera is applied"),
			S.bOverride_AutoExposureApplyPhysicalCameraExposure != 0
			&& S.AutoExposureApplyPhysicalCameraExposure != 0);

		TestTrue(TEXT("ISO is overridden"), S.bOverride_CameraISO != 0);
		TestTrue(TEXT("Shutter is overridden"), S.bOverride_CameraShutterSpeed != 0);
		TestTrue(TEXT("Aperture is overridden"), S.bOverride_DepthOfFieldFstop != 0);

		// The figure actually landed. The ISO is what carries it, precisely because the aperture is
		// clamped to f/1..f/32 and would silently pin an interior EV100 - which this one is, at 7,
		// below the ~6.6 floor an aperture-carried figure would hit at some settings.
		TestEqual(TEXT("The ISO is the one this EV100 asks for"),
			S.CameraISO, FHFExposure::IsoFor(FHFInteriorLighting::InteriorEV100()), 0.01f);

		TestEqual(TEXT("The aperture is the fixed one"), S.DepthOfFieldFstop, FHFExposure::Fstop(), 1e-4f);

		// AND THE ARITHMETIC ROUND TRIPS. EV100 = log2(N^2 / t * 100 / ISO). Checked rather than
		// assumed, because the formula is inverted in IsoFor and an inverted formula that is wrong
		// is wrong consistently in both directions and would agree with itself.
		const float Recovered = FMath::Log2(
			S.DepthOfFieldFstop * S.DepthOfFieldFstop * S.CameraShutterSpeed * 100.0f / S.CameraISO);

		TestEqual(TEXT("The camera really is at the stated EV100"),
			Recovered, FHFInteriorLighting::InteriorEV100(), 0.01f);

		// Local exposure, which is what lets a pinned exposure survive a window five stops brighter
		// than the wall beside it.
		TestTrue(TEXT("Local exposure compresses highlights"),
			S.bOverride_LocalExposureHighlightContrastScale != 0
			&& S.LocalExposureHighlightContrastScale < 1.0f);
		TestTrue(TEXT("...and shadows"),
			S.bOverride_LocalExposureShadowContrastScale != 0
			&& S.LocalExposureShadowContrastScale < 1.0f);

		// Lumen, asked for rather than inherited from whatever scalability the machine happens to
		// be on. An interior lit by fittings is almost entirely lit by bounce.
		TestTrue(TEXT("Global illumination is asked for explicitly"),
			S.bOverride_DynamicGlobalIlluminationMethod != 0);
		TestEqual(TEXT("...and it is Lumen"),
			S.DynamicGlobalIlluminationMethod.GetValue(), EDynamicGlobalIlluminationMethod::Lumen);

		// Skylight leaking is the cheat that stops an enclosed interior going black, which is to
		// say it hides the failure this rig exists to make visible.
		TestTrue(TEXT("Skylight leaking is pinned off"), S.bOverride_LumenSkylightLeaking != 0);
		TestEqual(TEXT("...at zero"), S.LumenSkylightLeaking, 0.0f, 1e-6f);
	}

	// ------------------------------------------------------------------------------- and it comes out
	const int32 Removed = Editor->RemoveInteriorLighting();
	TestEqual(TEXT("Removing the rig removes all of it"), Removed, Actors);
	TestEqual(TEXT("And leaves none of it behind"), FHFInteriorLighting::FindIn(World).Num(), 0);

	return true;
}

// ---------------------------------------------------------------------------------------------
//
// PRESS PLAY AND YOU ARE STANDING IN THE FOYER, NOT IN A WALL.
//
// A PlayerStart in the wrong place is worse than none: the character either falls through the world
// or is ejected sideways through the masonry, and the flat looks broken for a reason that has
// nothing to do with the flat.
//
// Three of the four assertions below are independent of how FHFWalkthroughStart searches. Inside
// the room outline, and inset from it by a capsule radius, are pure geometry against the spec. The
// downward trace asks the built level whether there is a floor. Only the capsule overlap repeats
// the resolver's own question, and it is kept because it is the regression guard: it is what would
// fail if somebody removed the search and went back to the room centre.
//
// ---------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFWalkthroughStartTest,
	"HouseForge.Lighting.AWalkthroughStartsInTheFoyer", HF_TEST_FLAGS)

bool FHFWalkthroughStartTest::RunTest(const FString& Parameters)
{
	using namespace HouseForgeLightingActors;

	UWorld* World = EditorWorld();
	if (!TestNotNull(TEXT("An editor world is open"), World))
	{
		return false;
	}

	FHFWalkthroughStart::RemoveFrom(World);
	ClearHouseForgeActors(World);

	AHFHouseActor* House = World->SpawnActor<AHFHouseActor>();
	if (!TestNotNull(TEXT("A house actor spawns"), House))
	{
		return false;
	}
	ON_SCOPE_EXIT
	{
		FHFWalkthroughStart::RemoveFrom(World);
		if (IsValid(House)) { House->ClearGeometry(); House->Destroy(); }
	};

	House->SetSpec(FHFSampleHouse::Make2BHK());
	House->BuildGeometry();

	// ------------------------------------------------------------------ it starts in the foyer
	const FHFRoom* Foyer = FHFWalkthroughStart::StartRoom(House->Spec);

	if (!TestNotNull(TEXT("The reference flat has a room to start in"), Foyer))
	{
		return false;
	}
	TestEqual(TEXT("And it is the foyer, which is where a person comes in"),
		Foyer->Type, EHFRoomType::Foyer);

	APlayerStart* Start = FHFWalkthroughStart::EnsureIn(World, House->Spec);
	if (!TestNotNull(TEXT("A walkthrough start is placed"), Start))
	{
		return false;
	}

	TestEqual(TEXT("Exactly one, so which one wins is not up to iteration order"),
		FHFWalkthroughStart::FindIn(World).Num(), 1);

	const FVector Where = Start->GetActorLocation();
	const FVector2D Plan(Where.X, Where.Y);

	AddInfo(FString::Printf(TEXT("Start in '%s' at (%.0f, %.0f, %.0f), floor at %.0f."),
		*Foyer->Id.ToString(), Where.X, Where.Y, Where.Z, Foyer->FloorZ));

	// ------------------------------------------------------- inside the room, not inside a wall
	//
	// Pure geometry against the spec's own outline: independent of anything the resolver did.
	TestTrue(TEXT("The start point is inside the foyer outline"), Foyer->ContainsPoint(Plan));

	// AND CLEAR OF IT BY A WHOLE CAPSULE. A point one millimetre inside a boundary is inside the
	// wall that stands on that boundary, and "inside the room" alone would accept it.
	const double R = FHFWalkthroughStart::CapsuleRadius();
	const FVector2D Compass[] =
	{
		FVector2D(R, 0.0), FVector2D(-R, 0.0), FVector2D(0.0, R), FVector2D(0.0, -R)
	};

	for (const FVector2D& Offset : Compass)
	{
		TestTrue(*FString::Printf(TEXT("A capsule at the start clears the outline at (%+.0f, %+.0f)"),
			Offset.X, Offset.Y), Foyer->ContainsPoint(Plan + Offset));
	}

	// ------------------------------------------------------------- and standing on a real floor
	//
	// The built level, not the spec. A start point resolved perfectly against an outline over a
	// room whose slab failed to generate is a start point somebody falls out of.
	// MULTI, not single, and the reason is written out at length in HFWalkthroughTests: an editor
	// world is shared between every test in a run, and whatever else is in it answers a trace on
	// equal terms.
	//
	// WHAT THIS DOES NOT ASSERT, and why. It does not claim the thing under the start point belongs
	// to this house. Measured: the editor world these tests run in is an open world with a
	// landscape in it, the landscape sits at Z = 0, the flat's foyer slab sits at Z = 0, and a
	// multi trace returns overlaps plus the FIRST BLOCKING hit - so the landscape answers and the
	// slab a centimetre behind it is never reached. Ignoring the landscape by name would be writing
	// this test around one particular map.
	//
	// The question is properly asked elsewhere and against every room rather than one:
	// HouseForge.Walkthrough.EveryRoomHasAFloorToStandOn. What is worth asserting HERE is the
	// height - that the start was placed at the same lift its capsule was proved clear at, which is
	// a fact about this class and about nothing else.
	TArray<FHitResult> Hits;
	const FVector Down = Where - FVector(0.0, 0.0, 400.0);

	FCollisionQueryParams Params(TEXT("HFStartFloor"), /*bTraceComplex*/ false);
	World->LineTraceMultiByChannel(Hits, Where, Down, ECC_Pawn, Params);

	if (TestTrue(TEXT("There is something solid under the start point to stand on"), Hits.Num() > 0))
	{
		TArray<FString> WhatIsDownThere;
		for (const FHitResult& Hit : Hits)
		{
			WhatIsDownThere.Add(Hit.GetActor() != nullptr ? Hit.GetActor()->GetName() : TEXT("<none>"));
		}

		const double Drop = Where.Z - Hits[0].ImpactPoint.Z;

		AddInfo(FString::Printf(TEXT("Under the start point: %s, %.1f cm down (standing height %.0f)."),
			*FString::Join(WhatIsDownThere, TEXT(", ")), Drop, FHFWalkthroughStart::StandingHeight()));

		// The actor sits at the capsule's CENTRE, lifted clear of the slab. A start placed with its
		// origin on the slab would spawn a character half inside the floor, and the movement
		// component would resolve that by pushing it somewhere unpredictable - which is precisely
		// what happened while this was being written, because the search tested the capsule at one
		// height and the placement used another.
		TestTrue(TEXT("The start stands at exactly the height its capsule was proved clear at"),
			FMath::Abs(Drop - FHFWalkthroughStart::StandingHeight()) < 1.0);
	}

	// AND THE ROOM IT IS IN HAS A SLAB OF ITS OWN, asked of the house rather than of the world so
	// that the landscape above cannot answer it.
	//
	// The Z claim is that the slab STRADDLES the room floor level, not that it tops out there:
	// AHFRoomActor carries the ceiling slab as well as the floor one, so its bounds legitimately
	// run from under the floor to over the room - measured, 315 for a 300 room on a 15 slab. A
	// "tops out at the floor" assertion was written first and failed for exactly that reason.
	bool bFoyerHasASlab = false;

	for (AActor* Element : House->ElementActors)
	{
		AHFRoomActor* RoomActor = Cast<AHFRoomActor>(Element);
		if (RoomActor == nullptr || RoomActor->Room.Id != Foyer->Id)
		{
			continue;
		}

		UDynamicMeshComponent* Component = RoomActor->GetMeshComponent();
		if (Component == nullptr)
		{
			continue;
		}

		const FBox Bounds = Component->Bounds.GetBox();
		bFoyerHasASlab = true;

		AddInfo(FString::Printf(TEXT("The foyer slab spans Z %.1f to %.1f; the room floor is %.1f."),
			Bounds.Min.Z, Bounds.Max.Z, Foyer->FloorZ));

		TestTrue(TEXT("The foyer slab is under the start point in plan"),
			Plan.X >= Bounds.Min.X && Plan.X <= Bounds.Max.X
			&& Plan.Y >= Bounds.Min.Y && Plan.Y <= Bounds.Max.Y);

		TestTrue(TEXT("...and there is slab below the room floor level to stand on"),
			Bounds.Min.Z < Foyer->FloorZ && Bounds.Max.Z >= Foyer->FloorZ);

		break;
	}

	TestTrue(TEXT("The room the walkthrough starts in has a slab of its own"), bFoyerHasASlab);

	// ------------------------------------------------------------------- and a person fits there
	const bool bBlocked = World->OverlapAnyTestByChannel(Where, FQuat::Identity, ECC_Pawn,
		FCollisionShape::MakeCapsule(
			static_cast<float>(FHFWalkthroughStart::CapsuleRadius()),
			static_cast<float>(FHFWalkthroughStart::CapsuleHalfHeight())),
		FCollisionQueryParams(TEXT("HFStartFits"), /*bTraceComplex*/ false));

	TestFalse(TEXT("Nothing solid is standing where the walkthrough starts"), bBlocked);

	// ------------------------------------------------------------------- and it faces into the flat
	//
	// A start facing the front door starts the walkthrough looking at the inside of a door. Checked
	// against the mean of the other rooms, which is what the resolver aims at and what "into the
	// flat" means for a plan of any shape.
	FVector2D Elsewhere = FVector2D::ZeroVector;
	int32 Others = 0;

	for (const FHFRoom& Room : House->Spec.Rooms)
	{
		if (Room.Id == Foyer->Id || Room.Boundary.Num() < 3)
		{
			continue;
		}

		FVector2D Mean = FVector2D::ZeroVector;
		for (const FVector2D& Point : Room.Boundary)
		{
			Mean += Point;
		}

		Elsewhere += Mean / static_cast<double>(Room.Boundary.Num());
		++Others;
	}

	if (Others > 0)
	{
		const FVector2D Inward = ((Elsewhere / static_cast<double>(Others)) - Plan).GetSafeNormal();
		const FVector Facing = Start->GetActorForwardVector();

		const double Alignment = Inward.X * Facing.X + Inward.Y * Facing.Y;

		AddInfo(FString::Printf(TEXT("The start faces the body of the flat with alignment %.2f."), Alignment));

		TestTrue(TEXT("The walkthrough starts facing into the flat, not at the front door"),
			Alignment > 0.0);
	}

	// ------------------------------------------------------------------- and rebuilding moves it
	//
	// Rather than adding a second. A level that gained a start point per build is a level nobody
	// could tell had one.
	APlayerStart* Again = FHFWalkthroughStart::EnsureIn(World, House->Spec);

	TestEqual(TEXT("Building again does not add a second start point"),
		FHFWalkthroughStart::FindIn(World).Num(), 1);
	TestEqual(TEXT("...it is the same actor, moved rather than replaced"), Again, Start);

	return true;
}

// ---------------------------------------------------------------------------------------------
//
// RE-LIGHTING AN ELEMENT DOES NOT TOUCH ITS GEOMETRY.
//
// The standing rule (.claude/rules/04-conventions.md): an element flagged bArtistEdited opts out of
// regeneration, and overwriting modelling work is a silent, unrecoverable loss. AHFElementActor::
// Regenerate refuses to run at all on such an actor - silently, deliberately - so a lighting change
// routed through Regenerate would simply be LOST on exactly the elements somebody had invested work
// in. That is why RebuildLights is its own entry point on all three light-carrying actors, and this
// is the assertion that it stays one.
//
// ---------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFRelightHandEditedTest,
	"HouseForge.Lighting.ReLightingLeavesHandEditedGeometryAlone", HF_TEST_FLAGS)

bool FHFRelightHandEditedTest::RunTest(const FString& Parameters)
{
	using namespace HouseForgeLightingActors;

	UWorld* World = EditorWorld();
	if (!TestNotNull(TEXT("An editor world is open"), World))
	{
		return false;
	}

	FHFFixture Fan;
	Fan.Id = TEXT("FAN1");
	Fan.RoomId = TEXT("R1");
	Fan.Type = EHFFixtureType::CeilingFan;
	Fan.Position = FVector2D(230.0, 190.0);
	Fan.Footprint = FVector2D(120.0, 120.0);
	Fan.Height = 30.0;

	AHFHouseActor* House = BuildRoom(World,
		{ LightFixture(TEXT("F_Light"), FVector2D(120.0, 120.0), 8.0), Fan },
		EHFCeilingTemplate::Cove);

	if (!TestNotNull(TEXT("The test room was built"), House))
	{
		return false;
	}
	ON_SCOPE_EXIT{ if (IsValid(House)) { House->ClearGeometry(); House->Destroy(); } };

	// All three light-carrying actors, because the guarantee has to hold for each of them and they
	// arrived by three different routes.
	TArray<AHFElementActor*> Carriers;
	for (AActor* Element : House->ElementActors)
	{
		if (AHFElementActor* Actor = Cast<AHFElementActor>(Element))
		{
			if (Actor->IsA<AHFLightFixtureActor>() || Actor->IsA<AHFFanActor>()
				|| Actor->IsA<AHFCeilingActor>())
			{
				Carriers.Add(Actor);
			}
		}
	}

	if (!TestEqual(TEXT("The room has a fitting, a fan and a ceiling in it"), Carriers.Num(), 3))
	{
		return false;
	}

	for (AHFElementActor* Actor : Carriers)
	{
		const FString Name = Actor->GetClass()->GetName();

		// Hand-edited, as though somebody had taken the modelling tools to it.
		Actor->bArtistEdited = true;

		const FMeshFingerprint Before = Fingerprint(Actor->GetMeshComponent());

		if (!TestTrue(*FString::Printf(TEXT("'%s' has geometry to protect"), *Name),
			Before.Triangles > 0))
		{
			continue;
		}

		// A LIGHTING CHANGE, of the kind the material and lighting panels make.
		int32 Rebuilt = 0;
		if (AHFLightFixtureActor* Fitting = Cast<AHFLightFixtureActor>(Actor))
		{
			Fitting->Lumens = 1234.0;
			Fitting->TemperatureKelvin = 4000.0;
			Rebuilt = Fitting->RebuildLights();
		}
		else if (AHFFanActor* FanActor = Cast<AHFFanActor>(Actor))
		{
			FanActor->Lumens = 777.0;
			Rebuilt = FanActor->RebuildLights();
		}
		else if (AHFCeilingActor* Ceiling = Cast<AHFCeilingActor>(Actor))
		{
			Ceiling->DownlightLumens = 555.0;
			Ceiling->CoveLumensPerMetre = 1100.0;
			Rebuilt = Ceiling->RebuildLights();
		}

		TestTrue(*FString::Printf(TEXT("'%s' re-lit something"), *Name), Rebuilt > 0);

		const FMeshFingerprint After = Fingerprint(Actor->GetMeshComponent());

		TestTrue(*FString::Printf(
			TEXT("'%s' kept its hand-edited mesh exactly: %d/%d vertices, %d/%d triangles"),
			*Name, Before.Vertices, After.Vertices, Before.Triangles, After.Triangles),
			Before == After);

		// And the flag survives, so the next rebuild still respects it. A re-light that silently
		// cleared bArtistEdited would hand the element back to the generator on the following
		// build, which is the same loss one step removed.
		TestTrue(*FString::Printf(TEXT("'%s' is still flagged as hand-edited"), *Name),
			Actor->bArtistEdited);
		TestTrue(*FString::Printf(TEXT("'%s' still opts out of a rebuild"), *Name),
			Actor->ShouldPreserveOnRebuild());

		// And the lighting change actually took, which is what makes the assertion above a
		// statement about isolation rather than about nothing having happened.
		if (AHFLightFixtureActor* Fitting = Cast<AHFLightFixtureActor>(Actor))
		{
			const TArray<UPointLightComponent*> Lamps = PointLampsOn(Fitting);
			if (Lamps.Num() == 1)
			{
				TestEqual(TEXT("The fitting is at its new output"), Lamps[0]->Intensity, 1234.0f, 0.5f);
				TestEqual(TEXT("...and its new colour"), Lamps[0]->Temperature, 4000.0f, 0.5f);
			}
		}
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
