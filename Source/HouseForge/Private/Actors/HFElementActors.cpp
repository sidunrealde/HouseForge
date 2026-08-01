// Copyright Siddartha G. All Rights Reserved.

#include "Actors/HFElementActors.h"

#include "Components/DynamicMeshComponent.h"
#include "Components/RectLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Geometry/HFGenerators.h"
#include "Geometry/HFMeshOps.h"
#include "HouseForge.h"
#include "Materials/HFMaterialLibrary.h"

using namespace UE::Geometry;

AHFElementActor::AHFElementActor()
{
	PrimaryActorTick.bCanEverTick = false;

	Mesh = CreateDefaultSubobject<UDynamicMeshComponent>(TEXT("Mesh"));
	SetRootComponent(Mesh);

	// Complex collision only: these are thin boxed shapes, and simple collision would fill the
	// door openings back in - you could not walk through a doorway that had been cut out.
	//
	// Both flags, and they are not the same flag. CollisionType says which collision to use;
	// bEnableComplexCollision says whether to build any. Setting only the first asks for complex
	// collision that was never cooked, and a component with no simple shapes either then has no
	// collision at all - it renders correctly and a walkthrough falls straight through it.
	Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Mesh->SetCollisionProfileName(TEXT("BlockAll"));
	Mesh->CollisionType = ECollisionTraceFlag::CTF_UseComplexAsSimple;
	Mesh->bEnableComplexCollision = true;
	Mesh->SetGenerateOverlapEvents(false);

	// Tangents derived from the mesh's own UVs and normals rather than taken from it.
	//
	// The default is "From Dynamic Mesh", and nothing here ever calls EnableTangents, so
	// HasTangentSpace() is false and the component silently falls back to MakePerpVectors - an
	// arbitrary basis with no relationship to the surface's UVs. Nothing fails and nothing logs;
	// with the flat colours that exist today the output is indistinguishable from correct, and it
	// only becomes visible when the materials milestone puts normal maps on top of it, at which
	// point it reads as a material bug rather than a geometry-attribute one.
	Mesh->SetTangentsType(EDynamicMeshComponentTangentsMode::AutoCalculated);
}

void AHFElementActor::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	WatchForEdits();
}

void AHFElementActor::PostRegisterAllComponents()
{
	Super::PostRegisterAllComponents();

	// Edit detection has to be armed here, not only in PostInitializeComponents.
	//
	// AActor::PostActorConstruction gates PostInitializeComponents on World->AreActorsInitialized(),
	// which is false for an editor world - so that path runs in PIE only. Element actors are also
	// deliberately not regenerated on load, so CommitMesh does not run either. Between the two,
	// an element that came back from a saved level had no binding at all: take the Modeling Tools
	// to a wall, press Build Geometry, and the modelling work is gone without a word. That is the
	// silent, unrecoverable loss .claude/rules/04-conventions.md calls out.
	//
	// WatchForEdits is idempotent, so this sits safely alongside the CommitMesh path.
	WatchForEdits();
}

void AHFElementActor::WatchForEdits()
{
	if (bWatching || Mesh == nullptr)
	{
		return;
	}

	// These are dynamic meshes so an artist can take Unreal's Modeling Tools to them after
	// generation. Watching for changes is what makes that safe - anything that edits the mesh and
	// is not us marks the element as hand-edited, and it then opts out of regeneration.
	Mesh->OnMeshChanged.AddUObject(this, &AHFElementActor::HandleMeshChanged);
	bWatching = true;
}

void AHFElementActor::HandleMeshChanged()
{
	if (bGenerating || bArtistEdited)
	{
		return;
	}

	bArtistEdited = true;

	UE_LOG(LogHouseForge, Log,
		TEXT("'%s' was edited by hand; it will keep those edits and no longer regenerate. Use Revert To Generated to undo that."),
		*GetName());
}

void AHFElementActor::Regenerate()
{
	if (bArtistEdited)
	{
		// Silently refusing is deliberate. Rebuilding here would throw away modelling work, and
		// that loss tends to be noticed only long after it happened.
		UE_LOG(LogHouseForge, Verbose,
			TEXT("Skipping regeneration of '%s': it has been edited by hand."), *GetName());
		return;
	}

	CommitMesh(BuildMesh());
}

void AHFElementActor::RevertToGenerated()
{
	bArtistEdited = false;
	CommitMesh(BuildMesh());
}

void AHFElementActor::CommitMesh(FDynamicMesh3&& Generated)
{
	if (Mesh == nullptr)
	{
		return;
	}

	WatchForEdits();

	// Our own write must not look like an artist edit.
	TGuardValue<bool> Guard(bGenerating, true);

	// The composing layer's single pass over generated geometry: chamfer the arrises, re-project
	// UV0 over the facets that produced, and lay out the lightmap channel. Here rather than in the
	// generators because a bevel is the one operation in this plugin that is NOT idempotent, and a
	// generator that beveled its own output would have its chamfers chamfered again by every
	// composition it was appended into. See FHFRenderFinish.
	FHFMeshOps::FinishForRender(Generated, RenderFinish, FlushVolumes);

	// The last thing done to a generated mesh, after every boolean and every append. The material
	// id is a pure function of the polygroup, so deriving it here rather than inside the generators
	// means no mesh operation has to be trusted to carry it - and no generator has to reach for an
	// asset to know what it is being materialled with.
	FHFMeshOps::AssignMaterialIdsFromRoles(Generated);

	Mesh->SetMesh(MoveTemp(Generated));
	FHFMaterialLibrary::ApplyPlaceholders(Mesh);
	Mesh->NotifyMeshUpdated();
	Mesh->UpdateCollision(false);
}

#if WITH_EDITOR
void AHFElementActor::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// Clearing the flag by hand is a deliberate request to go back to generated geometry.
	const FName Changed = PropertyChangedEvent.GetPropertyName();
	if (Changed == GET_MEMBER_NAME_CHECKED(AHFElementActor, bArtistEdited))
	{
		if (!bArtistEdited)
		{
			RevertToGenerated();
		}
		return;
	}

	// ONLY OUR OWN PROPERTIES REBUILD ANYTHING.
	//
	// PostEditChangeProperty is not only the details panel. The engine fires it for its own
	// properties too, and AActor::SetActorLabel is the one that matters here: naming an actor sends a
	// property change for AActor::ActorLabel, which used to land on the Regenerate below. Every
	// element the house builds is labelled the instant it is spawned, so EVERY element generated
	// itself once with default parameters before the composing layer had told it what it was, and
	// again properly a moment later.
	//
	// Wasteful on a wall. Destructive on anything with pose state, because the ghost generation
	// creates the parts: a fan's rotor came into existence at phase 0, and the real phase applied
	// straight afterwards then lost to the rule that an existing part's pose beats a generated
	// default. All six fans in the reference flat came out stopped on the same blade, and every
	// individual step in the chain was correct.
	//
	// MemberProperty first, so a change inside a nested struct - Fan.SweepDiameter, Wall.Thickness -
	// is attributed to the struct's owner rather than to the struct. A null property is the engine
	// saying "assume everything changed", which undo does, so that still rebuilds.
	const FProperty* Edited = PropertyChangedEvent.MemberProperty != nullptr
		? PropertyChangedEvent.MemberProperty
		: PropertyChangedEvent.Property;

	if (Edited != nullptr)
	{
		const UClass* DeclaredOn = Edited->GetOwnerClass();
		if (DeclaredOn == nullptr || !DeclaredOn->IsChildOf(AHFElementActor::StaticClass()))
		{
			return;
		}
	}

	// Editing any parameter rebuilds only this element, which is the point of one actor per
	// element rather than one mesh for the whole house.
	Regenerate();
}
#endif

FDynamicMesh3 AHFWallActor::BuildMesh() const
{
	return FHFGenerators::GenerateWall(Wall, Openings, Structure);
}

FDynamicMesh3 AHFRoomActor::BuildMesh() const
{
	// A ROOM ACTOR CAN EXIST WITHOUT A HOUSE. Dropped into a level by hand, or with its boundary
	// retyped in the details panel, it has no composed plan and no walls to compose one from - and
	// the honest answer for a room with nothing known round it is to skirt the whole perimeter, which
	// is exactly what the resolver returns when handed no walls, openings or fixtures.
	//
	// Keyed on the edge count because that is the one way the stored plan can be wrong without being
	// absent: a boundary edited to a different number of corners leaves runs measured along edges
	// that no longer exist.
	FHFSkirtingParams Section;
	Section.Depth = Skirting.Depth;

	const FHFSkirtingPlan Resolved = (Skirting.Edges.Num() == Room.Boundary.Num())
		? Skirting
		: FHFSkirting::For(Room, {}, {}, {}, {}, Section);

	FDynamicMesh3 Result = FHFGenerators::GenerateFloor(Room, SlabThickness, Resolved);

	if (bGenerateCeilingSlab)
	{
		FHFMeshOps::AppendPreservingRoles(Result, FHFGenerators::GenerateCeilingSlab(Room, SlabThickness));
	}

	return Result;
}

TArray<FVector> AHFCeilingActor::DownlightPositions() const
{
	TArray<FVector> Local = FHFGenerators::CeilingDownlights(Ceiling, Room);

	const FTransform ToWorld = GetActorTransform();
	for (FVector& Position : Local)
	{
		Position = ToWorld.TransformPosition(Position);
	}

	return Local;
}

FDynamicMesh3 AHFCeilingActor::BuildMesh() const
{
	// The mesh is what a bare BuildMesh call is for, but a ceiling is the one element whose design
	// IS partly its lighting, so the two are rebuilt together. See RebuildLights.
	const_cast<AHFCeilingActor*>(this)->RebuildLights();

	return FHFGenerators::GenerateCeiling(Ceiling, Room, FanDrops, FanDropRadius);
}

int32 AHFCeilingActor::RebuildLights()
{
	// Thrown away and rebuilt rather than adjusted: a ceiling that changes template changes how
	// many lights it has and where they are, and reconciling two lists is how a level ends up with
	// the previous design's downlights still burning in the plasterboard.
	for (const TObjectPtr<ULightComponent>& Light : Lights)
	{
		if (Light != nullptr)
		{
			Light->DestroyComponent();
		}
	}
	Lights.Reset();

	if (!bBuildLights)
	{
		return 0;
	}

	const FTransform ToWorld = GetActorTransform();

	auto Common = [this](ULightComponent* Light)
	{
		// Movable, because everything here is regenerated on a property change and a static light
		// would need its lighting rebuilt to notice. The bake milestone is where that changes.
		Light->SetMobility(EComponentMobility::Movable);
		Light->SetUseTemperature(true);
		Light->SetTemperature(static_cast<float>(LightTemperatureKelvin));
		Light->SetCastShadows(true);
		Light->RegisterComponent();
		Light->AttachToComponent(GetRootComponent(),
			FAttachmentTransformRules::KeepWorldTransform);
		Lights.Add(Light);
	};

	// ---------------------------------------------------------------------------- the cove
	//
	// A rect light per straight run, lying in the trough and facing UP - which is the direction a
	// cove throws and the reason its light is worth having. A point light in the middle of the room
	// would light the middle of the room.
	for (const FHFCoveLightRun& Run : FHFGenerators::CeilingCoveLights(Ceiling, Room))
	{
		URectLightComponent* Rect = NewObject<URectLightComponent>(this);
		if (Rect == nullptr)
		{
			continue;
		}

		// X is the direction a rect light emits and Y is its width, so the frame is built from
		// "up" and the direction the run travels.
		const FVector Direction(FMath::Cos(FMath::DegreesToRadians(Run.YawDegrees)),
			FMath::Sin(FMath::DegreesToRadians(Run.YawDegrees)), 0.0);

		Rect->SetWorldTransform(FTransform(
			FRotationMatrix::MakeFromXY(FVector::UpVector, Direction).Rotator(),
			ToWorld.TransformPosition(Run.Centre)));

		Rect->SetSourceWidth(static_cast<float>(Run.Length));
		Rect->SetSourceHeight(static_cast<float>(Run.Width));

		// Barn doors down to the channel, so the wash stays in the trough's own aperture instead of
		// spilling out over the lip into the room - which is the difference between a cove and a
		// bright line at the ceiling.
		Rect->SetBarnDoorAngle(60.0f);
		Rect->SetBarnDoorLength(static_cast<float>(Run.Width * 0.5));

		Rect->SetIntensityUnits(ELightUnits::Lumens);
		Rect->SetIntensity(static_cast<float>(CoveLumensPerMetre * Run.Length / 100.0));

		// It only ever has to reach the surface above it, and a cove that lights the far wall is a
		// cove nobody would recognise.
		Rect->SetAttenuationRadius(static_cast<float>(FMath::Max(Run.ThrowHeight * 6.0, 100.0)));

		Common(Rect);
	}

	// ---------------------------------------------------------------------- the downlights
	//
	// At the APERTURE, up inside the can, which is what CeilingDownlights has always returned and
	// what nothing has ever asked it for. Parented at the soffit instead, a spotlight is shaded by
	// its own trim ring.
	const double ConeDegrees = 45.0;

	for (const FVector& Position : FHFGenerators::CeilingDownlights(Ceiling, Room))
	{
		USpotLightComponent* Spot = NewObject<USpotLightComponent>(this);
		if (Spot == nullptr)
		{
			continue;
		}

		Spot->SetWorldTransform(FTransform(
			FRotator(-90.0, 0.0, 0.0), ToWorld.TransformPosition(Position)));

		Spot->SetInnerConeAngle(static_cast<float>(ConeDegrees * 0.5));
		Spot->SetOuterConeAngle(static_cast<float>(ConeDegrees));
		Spot->SetIntensityUnits(ELightUnits::Lumens);
		Spot->SetIntensity(static_cast<float>(DownlightLumens));
		Spot->SetAttenuationRadius(1200.0f);

		// A real COB has a lens a few centimetres across, and that width is most of what makes the
		// scallop on the wall soft rather than a hard-edged circle.
		Spot->SetSourceRadius(static_cast<float>(Ceiling.Downlight.CutoutRadius()));

		Common(Spot);
	}

	return Lights.Num();
}

FDynamicMesh3 AHFBeamActor::BuildMesh() const
{
	return FHFGenerators::GenerateBeam(Beam, Structure);
}

FDynamicMesh3 AHFColumnActor::BuildMesh() const
{
	return FHFGenerators::GenerateColumn(Column);
}
