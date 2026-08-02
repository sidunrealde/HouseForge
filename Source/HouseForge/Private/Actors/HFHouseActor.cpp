// Copyright Siddartha G. All Rights Reserved.

#include "Actors/HFHouseActor.h"

#include "Actors/HFArticulatedActor.h"
#include "Actors/HFCasedGoodsActor.h"
#include "Actors/HFCounterActor.h"
#include "Actors/HFElementActors.h"
#include "Actors/HFFittingActors.h"
#include "Actors/HFFurnitureActors.h"
#include "Actors/HFLooseFurnitureActors.h"
#include "Actors/HFOpeningActor.h"
#include "Actors/HFFanActor.h"
#include "Actors/HFSanitaryActors.h"
#include "Actors/HFServiceActors.h"
#include "Actors/HFWardrobeActor.h"
#include "Components/LineBatchComponent.h"
#include "Engine/World.h"
#include "Geometry/HFGenerators.h"
#include "HouseForge.h"
#include "Model/HFBuildDefaults.h"
#include "Model/HFCeilingFit.h"
#include "Model/HFCeilingTemplates.h"
#include "Model/HFFixturePlacement.h"
#include "Model/HFSkirtingPlan.h"

namespace
{
	// Preview palette. Roles are colour-coded so a top-down screenshot can be compared against the
	// source drawing without needing labels.
	const FLinearColor ColourWall(0.10f, 0.10f, 0.12f);
	const FLinearColor ColourOpening(0.95f, 0.65f, 0.10f);
	const FLinearColor ColourSwing(0.95f, 0.35f, 0.05f);
	const FLinearColor ColourRoom(0.20f, 0.55f, 0.85f);
	const FLinearColor ColourBeam(0.85f, 0.25f, 0.25f);
	const FLinearColor ColourColumn(0.55f, 0.20f, 0.55f);
	const FLinearColor ColourFixture(0.25f, 0.70f, 0.35f);
	const FLinearColor ColourCeiling(0.70f, 0.35f, 0.75f);

	constexpr uint8 DepthPriority = SDPG_World;

	FVector2D RotateAbout(const FVector2D& Point, const FVector2D& Centre, double Degrees)
	{
		const double Radians = FMath::DegreesToRadians(Degrees);
		const double C = FMath::Cos(Radians);
		const double S = FMath::Sin(Radians);
		const FVector2D D = Point - Centre;
		return Centre + FVector2D(D.X * C - D.Y * S, D.X * S + D.Y * C);
	}

	/** Plan footprint of a wall: centreline expanded by half its thickness on each side. */
	TArray<FVector2D> WallFootprint(const FHFWall& Wall)
	{
		TArray<FVector2D> Out;
		const double Length = Wall.Length();
		if (Length <= UE_KINDA_SMALL_NUMBER)
		{
			return Out;
		}

		const FVector2D Direction = (Wall.End - Wall.Start) / Length;
		const FVector2D Normal(-Direction.Y, Direction.X);
		const double Half = Wall.Thickness * 0.5;

		Out.Add(Wall.Start + Normal * Half);
		Out.Add(Wall.End + Normal * Half);
		Out.Add(Wall.End - Normal * Half);
		Out.Add(Wall.Start - Normal * Half);
		return Out;
	}

	TArray<FVector2D> RectFootprint(const FVector2D& Centre, const FVector2D& Size, double RotationDegrees)
	{
		const FVector2D Half = Size * 0.5;
		TArray<FVector2D> Out;
		Out.Add(RotateAbout(Centre + FVector2D(-Half.X, -Half.Y), Centre, RotationDegrees));
		Out.Add(RotateAbout(Centre + FVector2D( Half.X, -Half.Y), Centre, RotationDegrees));
		Out.Add(RotateAbout(Centre + FVector2D( Half.X,  Half.Y), Centre, RotationDegrees));
		Out.Add(RotateAbout(Centre + FVector2D(-Half.X,  Half.Y), Centre, RotationDegrees));
		return Out;
	}

	// ------------------------------------------------------------------- the build order of a frame
	//
	// A house is not a bag of solids. Concrete is cast before masonry is laid, columns before the
	// beams that frame into them, and where two members want the same volume exactly one of them
	// gets it. Modelling every member as if it were alone gave two of them the same faces to draw,
	// and the whole flat flashed - see FHFStructuralCut.
	//
	// So the composing layer, which is the only thing that can see more than one element, works out
	// what displaces what and hands each member the list. The precedence is the build sequence:
	//
	//   1. Columns - cast first, full height, displaced by nothing.
	//   2. Beams   - frame into every column they land on. Where two cross, the LONGER one runs
	//                through and the shorter stops at its face, which is what primary and secondary
	//                beams are. Equal lengths break on id so a rebuild is deterministic.
	//   3. Walls   - blockwork infills around all of it, and around each other. Walls are set out on
	//                their CENTRELINES, so wherever two meet, both own the footprint of the junction.
	//                On site one run is built through and the other butts to its face: the THICKER
	//                wall runs, a 230 external through a 115 partition, and at equal thickness the
	//                longer one does. Without it a balcony parapet buried its last 115 in the main
	//                wall and the two undersides fought - which is the one defect the flat had left
	//                once the frame was right, and it is on show from outside the building.

	/** World-space bounds of an oriented structural volume. */
	FBox BoundsOf(const FHFStructuralCut& Cut)
	{
		if (!Cut.IsValid())
		{
			return FBox(ForceInit);
		}

		FBox Bounds(ForceInit);
		const FVector2D Centre(Cut.Centre.X, Cut.Centre.Y);

		for (const FVector2D& Corner : RectFootprint(Centre,
			FVector2D(Cut.Extents.X * 2.0, Cut.Extents.Y * 2.0), Cut.YawDegrees))
		{
			Bounds += FVector(Corner.X, Corner.Y, Cut.BottomZ());
			Bounds += FVector(Corner.X, Corner.Y, Cut.TopZ());
		}

		return Bounds;
	}

	FBox BoundsOf(const FHFWall& Wall)
	{
		FBox Bounds(ForceInit);
		for (const FVector2D& Corner : WallFootprint(Wall))
		{
			Bounds += FVector(Corner.X, Corner.Y, Wall.BaseZ);
			Bounds += FVector(Corner.X, Corner.Y, Wall.BaseZ + Wall.Height);
		}
		return Bounds;
	}

	/**
	 * True when two members want the same material, rather than merely meeting.
	 *
	 * Shrunk before the test on purpose. Two members that touch face to face are a butt joint - the
	 * everyday case, every wall against every other wall - and cutting one with the other there
	 * would run a mesh boolean per junction to remove exactly nothing.
	 */
	bool VolumesOverlap(const FBox& A, const FBox& B)
	{
		constexpr double Touching = 0.01;
		return A.IsValid && B.IsValid && A.ExpandBy(-Touching).Intersect(B.ExpandBy(-Touching));
	}

	/** True when the first beam is the one that runs through at a crossing. */
	bool BeamRunsThrough(const FHFBeam& Candidate, const FHFBeam& Other)
	{
		const double A = Candidate.Length();
		const double B = Other.Length();
		if (!FMath::IsNearlyEqual(A, B, 0.1))
		{
			return A > B;
		}
		return Candidate.Id.LexicalLess(Other.Id);
	}

	/**
	 * What a wall stands on, as a volume its underside dies into.
	 *
	 * A wall's bottom arris is never an arris: there is a slab, a plinth or the ground under every
	 * one of them, and the plaster turns the corner onto whatever it is. Modelled as a thin slice
	 * under the wall's own footprint rather than as the room's floor, because a wall does not know
	 * which rooms are on either side of it and does not need to - the answer is the same.
	 */
	FHFStructuralCut FootingUnder(const FHFWall& Wall)
	{
		FHFStructuralCut Footing = FHFGenerators::StructuralCutFor(Wall);
		Footing.SourceId = FName(*FString::Printf(TEXT("%s_Footing"), *Wall.Id.ToString()));
		Footing.Centre.Z = Wall.BaseZ - 1.0;
		Footing.Extents.Z = 1.0;
		return Footing;
	}

	/** True when the first wall is the one built through at a junction, and the other butts to it. */
	bool WallRunsThrough(const FHFWall& Candidate, const FHFWall& Other)
	{
		if (!FMath::IsNearlyEqual(Candidate.Thickness, Other.Thickness, 0.1))
		{
			return Candidate.Thickness > Other.Thickness;
		}

		const double A = Candidate.Length();
		const double B = Other.Length();
		if (!FMath::IsNearlyEqual(A, B, 0.1))
		{
			return A > B;
		}

		// Two identical walls meeting is not a real building, but a rebuild still has to make the
		// same choice twice or the flat changes shape between runs.
		return Candidate.Id.LexicalLess(Other.Id);
	}

	/**
	 * Every hole cut into one wall: the openings the drawing put in it, and the ducts it is cored for.
	 *
	 * ONE FUNCTION BECAUSE THE HOLE HAS TO FOLLOW THE FAN. An extract that a false ceiling forces
	 * down takes its duct with it - the case covers exactly the spot where the hole is, so a hole
	 * that stayed put would be a bare square opening in a finished wall with the fan 40 mm below it.
	 * The house builds walls once and re-seeds them when the ceilings move, and the two paths have to
	 * cut the same holes or the second one would quietly undo the first.
	 *
	 * @param Fixtures The FITTED fixtures, not the spec's. The hole is derived from the fan that ends
	 *        up standing in it, which is not always the fan the drawing described.
	 */
	TArray<FHFOpening> OpeningsInWall(const FHFHouseSpec& Spec, const TArray<FHFFixture>& Fixtures,
		const FHFWall& Wall)
	{
		TArray<FHFOpening> Out;

		for (const FHFOpening& Opening : Spec.Openings)
		{
			if (Opening.WallId == Wall.Id)
			{
				Out.Add(Opening);
			}
		}

		// AN EXTRACT HAS TO BLOW THROUGH THE WALL IT IS SCREWED TO. The fan's case carries an aperture
		// and its blades turn inside it, and none of that is worth anything while the masonry behind is
		// solid - which it was for all three extracts in the flat. Invisible from the room, because the
		// case covers precisely the spot where the hole is not.
		//
		// Derived from the fan rather than asked of the drawing, and never added to the spec's
		// openings - only to the wall's - so the hole is cut but no ventilator sash is built in it.
		// See AHFFanActor::DuctOpeningFor.
		for (const FHFFixture& Fixture : Fixtures)
		{
			if (Fixture.Type == EHFFixtureType::ExhaustFan && Fixture.AnchorWallId == Wall.Id)
			{
				// The ROOM as well as the wall: a fixture's BaseZ is measured above the room floor and
				// an opening's sill above the wall's base, and the hole has to land on the fan's own
				// centre rather than on whichever of the two datums happened to be handy.
				Out.Add(AHFFanActor::DuctOpeningFor(Fixture, Wall, Spec.FindRoom(Fixture.RoomId)));
			}
		}

		return Out;
	}

	// ---------------------------------------------------------------------- the fixture spawn table
	//
	// THIRTY MORE TYPES MUST NOT MEAN THIRTY MORE LOOPS IN TWO PLACES THAT CAN DISAGREE.
	//
	// Until this table existed the composing layer had one hand-written loop per fixture family in
	// BuildGeometry and a parallel switch in ApplyProjectSettingsToCeilings, and the two carried the
	// same seeding sequence written out twice. They were already one edit apart from drifting: a
	// wardrobe was placed from its anchor wall in both, an extract had its host wall thickness set in
	// both, and every new type would have added a third and a fourth copy. A type seeded one way on a
	// fresh build and another way after somebody dragged a ceiling slider is a defect that only
	// appears on the second build, which is exactly the kind this project keeps finding by eye.
	//
	// So there is one row per type and one seeding function per row, and BOTH loops run it. Whatever
	// a fresh build does to a fixture is by construction what a rebuild does to it.
	//
	// AHFHouseActor::BuildsGeometryFor is derived from this table rather than written out beside it,
	// which is what keeps the skirting resolver, the build report and the spawn loop agreeing about
	// what actually exists. A type that is in the table builds; a type that is not is a row in the
	// spec and nothing in the level.

	/**
	 * Everything a fixture actor needs from the rest of the house in order to be seeded.
	 *
	 * A generator may not go looking for the rest of the house and neither may an actor, so anything
	 * that depends on more than one element is resolved by the composing layer and handed over as a
	 * plain value - the same rule the wall's structural cuts and the ceiling's fan holes follow.
	 */
	/**
	 * Which set-in fixtures land on which host, resolved once for the whole house.
	 *
	 * THE ONE CROSS-FIXTURE DEPENDENCY IN THE CATALOGUE. A sink and a hob are set INTO a counter:
	 * the counter has to be cut for them, and they have to sit at the counter's built top rather than
	 * at the height the drawing gave them. Neither of those can be worked out by either fixture -
	 * a generator may not go looking for the rest of the house, and nor may an actor - so it is
	 * resolved here, in the composing layer, and handed to both sides as plain values.
	 *
	 * Exactly the shape of the answer AHFFanActor::DuctOpeningFor already gives for an extract's hole
	 * through its host wall, and for the same reason.
	 */
	struct FHFSetInResolution
	{
		/** Holes to cut, by host fixture id, already in that host's own local frame. */
		TMap<FName, TArray<FHFCounterAperture>> AperturesByHost;

		/** World Z of the host's finished top, by SET-IN fixture id. */
		TMap<FName, double> SurfaceZ;

		/** The host's resolved yaw, by set-in fixture id. A hob set into a run turns WITH the run. */
		TMap<FName, double> SurfaceYaw;

		/** True when this fixture found a host to stand on at all. */
		bool HasHost(const FName& Id) const { return SurfaceZ.Contains(Id) && SurfaceYaw.Contains(Id); }
	};

	struct FHFFixtureContext
	{
		const FHFHouseSpec* Spec = nullptr;
		const FHFFixture* Fixture = nullptr;
		const FHFRoom* Room = nullptr;
		const FHFWall* AnchorWall = nullptr;

		/** Where the sinks and hobs are, for the counters they are cut into and for themselves. */
		const FHFSetInResolution* SetIn = nullptr;

		/** Every fitted fixture, for the few decisions that depend on what else is standing nearby. */
		const TArray<FHFFixture>* Fixtures = nullptr;

		/** How far the false ceiling over this fixture hangs below the slab, at its own position. */
		double SoffitDrop = 0.0;

		/**
		 * Walls whose cored duct has to be re-cut because the fitting blowing through them moved.
		 *
		 * Null on a fresh build, where the walls are generated after the fixtures are resolved and
		 * already carry every hole. Only a rebuild has to go back and re-cut one.
		 */
		TSet<FName>* WallsToRecut = nullptr;

		double FloorZ() const { return Room != nullptr ? Room->FloorZ : 0.0; }
	};

	/** Seeds one freshly spawned or rebuilt actor: project figures, then the drawing, then placement. */
	using FHFSeedFixtureFn = void (*)(const FHFFixtureContext&, AHFElementActor&);

	struct FHFFixtureRecipe
	{
		EHFFixtureType Type;
		UClass* Class;
		const TCHAR* NamePrefix;
		FHFSeedFixtureFn Seed;
	};

	// The order inside every seed function below is load-bearing and the same in each: the project's
	// figures FIRST, then the drawing over them, then anything the rest of the house decides, then the
	// transform. ApplyFixture reads figures that ApplyProjectDefaults puts there - a module width, a
	// plinth height - to fill in what a drawing did not state.

	void SeedWardrobe(const FHFFixtureContext& C, AHFElementActor& Element)
	{
		AHFWardrobeActor& Actor = static_cast<AHFWardrobeActor&>(Element);

		// Re-seeded in full rather than adjusted, because the bay count and the loft are derived from
		// the height: half-applying a new one leaves a carcass built for the old.
		Actor.ApplyProjectDefaults();
		Actor.ApplyFixture(*C.Fixture);

		// A wardrobe under a ceiling that has come down is CUT SHORTER, not lowered - it stands on the
		// floor, and FHFCeilingFit has already taken the height out of the fitted fixture.
		Actor.SetActorTransform(FHFFixturePlacement::AgainstWall(*C.Fixture, C.FloorZ(), C.AnchorWall));
	}

	/**
	 * Is there another run standing in front of one end of this one?
	 *
	 * A PULL-OUT NEEDS SOMEWHERE TO PULL OUT TO. In an L-shaped kitchen the return run stands in
	 * front of one end of the other run, and a drawer bank put at that end has 2.5 cm of clear travel
	 * out of 55 before it drives into the return's carcasses - a drawer that sweeps its whole declared
	 * distance, satisfies every assertion about motion, and cannot be opened. That is the wardrobe's
	 * cancelling-leaf failure again in a different fitting, and only this layer can see it, because
	 * neither run knows the other exists.
	 *
	 * Probed rather than reasoned about: a point out in front of the end bay, at the distance a drawer
	 * actually comes out to, tested against everything else the house is going to build on the floor.
	 *
	 * @param bStartEnd True to probe the -X end of the run, false for the +X end.
	 */
	bool RunEndIsObstructed(const FHFFixtureContext& C, bool bStartEnd)
	{
		if (C.Fixtures == nullptr || C.Fixture->Footprint.X <= 0.0)
		{
			return false;
		}

		// How far out a drawer in this run comes. A full-extension runner clears the carcass, so the
		// probe goes to where the FRONT of an open drawer ends up rather than to the door face.
		const double PullOut = C.Fixture->Footprint.Y * 0.6;

		const double Yaw = FHFFixturePlacement::FacingYaw(*C.Fixture, C.AnchorWall);
		const FRotator Rotation(0.0, Yaw, 0.0);

		// The run's own frame: origin at the front-left corner of the footprint, +Y back into it.
		const FVector Corner = Rotation.RotateVector(
			FVector(-C.Fixture->Footprint.X * 0.5, -C.Fixture->Footprint.Y * 0.5, 0.0));
		const FVector2D Origin(C.Fixture->Position.X + Corner.X, C.Fixture->Position.Y + Corner.Y);

		// SEVERAL POINTS ACROSS THE END BAY AND ALONG THE PULL, not one. A single probe at the middle
		// of the end bay landed exactly on the return run's front edge, where the inside test is a
		// coin toss - and the answer came back "clear" for a drawer with 2.5 cm of travel. The
		// obstruction is a rectangle overlapping part of a bay, so the question is whether ANY of the
		// space the drawer sweeps is occupied, not whether one particular point is.
		static constexpr double AlongEnd[] = { 0.62, 0.80, 0.96 };
		static constexpr double OutBy[] = { 0.35, 0.70, 1.0 };

		for (const double Along : AlongEnd)
		{
			const double AlongRun = C.Fixture->Footprint.X * (bStartEnd ? 1.0 - Along : Along);

			for (const double Out : OutBy)
			{
				const FVector Local = Rotation.RotateVector(FVector(AlongRun, -PullOut * Out, 0.0));
				const FVector2D Probe(Origin.X + Local.X, Origin.Y + Local.Y);

				for (const FHFFixture& Other : *C.Fixtures)
				{
					if (Other.Id == C.Fixture->Id || !AHFHouseActor::BuildsGeometryFor(Other.Type))
					{
						continue;
					}

					// Only what stands ON THE FLOOR can block a drawer. A wall cabinet at 140 is over
					// it, and a counter is the thing the drawer is under.
					if (Other.IsCeilingMounted() || Other.BaseZ > 50.0
						|| AHFCounterActor::Builds(Other.Type))
					{
						continue;
					}

					if (FHFFixturePlacement::FootprintContains(Other, Probe))
					{
						return true;
					}
				}
			}
		}

		return false;
	}

	void SeedCasedGoods(const FHFFixtureContext& C, AHFElementActor& Element)
	{
		AHFCasedGoodsActor& Actor = static_cast<AHFCasedGoodsActor&>(Element);

		Actor.ApplyProjectDefaults();

		// Which end the drawer bank goes at, decided before ApplyFixture reads it. The far end by
		// default, because that is where a bank belongs when nothing is in the way - it keeps the bay
		// under a sink or a hob free for a cupboard - and the near end when the far one is blocked.
		Actor.bBankAtRunStart = RunEndIsObstructed(C, /*bStartEnd*/ false)
			&& !RunEndIsObstructed(C, /*bStartEnd*/ true);

		Actor.ApplyFixture(*C.Fixture);

		// A WALL UNIT IS PLACED BY EXACTLY THE SAME RULE AS A FLOOR-STANDING RUN, which is why there is
		// no second entry point for one. AgainstWall puts the origin at the front-left corner of the
		// footprint at the room floor plus the fixture's own BaseZ, and a wall cabinet is simply a
		// carcass whose BaseZ is 140 rather than 0 - see FHFCasedGoodsParams' frame note, where Z = 0 is
		// the underside of the plinth on a floor-standing run and the underside of the carcass on a
		// wall-hung one. That is what makes both kinds placeable without the composing layer knowing
		// which it has.
		Actor.SetActorTransform(FHFFixturePlacement::AgainstWall(*C.Fixture, C.FloorZ(), C.AnchorWall));
	}

	void SeedBed(const FHFFixtureContext& C, AHFElementActor& Element)
	{
		AHFBedActor& Actor = static_cast<AHFBedActor&>(Element);

		Actor.ApplyProjectDefaults();
		Actor.ApplyFixture(*C.Fixture);

		// PLACED LIKE A RUN OF JOINERY AND NOT LIKE LOOSE FURNITURE, which is the one thing about a bed
		// that is easy to get wrong. A bed has a front and a back - the headboard goes against the wall
		// and the foot into the room - and a drawing states a yaw that is a one-in-two chance of being
		// the half turn that puts the headboard in the middle of the floor. FHFFixturePlacement resolves
		// it from the anchor wall, exactly as it does for a wardrobe.
		Actor.SetActorTransform(FHFFixturePlacement::AgainstWall(*C.Fixture, C.FloorZ(), C.AnchorWall));
	}

	void SeedDesk(const FHFFixtureContext& C, AHFElementActor& Element)
	{
		AHFDeskActor& Actor = static_cast<AHFDeskActor&>(Element);

		// The project's figures FIRST: ApplyFixture deliberately preserves the skirting setback this
		// resolves, and re-seeding in the other order would leave the desk standing inside the board.
		Actor.ApplyProjectDefaults();
		Actor.ApplyFixture(*C.Fixture);

		Actor.SetActorTransform(FHFFixturePlacement::AgainstWall(*C.Fixture, C.FloorZ(), C.AnchorWall));
	}

	void SeedSofa(const FHFFixtureContext& C, AHFElementActor& Element)
	{
		AHFSofaActor& Actor = static_cast<AHFSofaActor&>(Element);

		Actor.ApplyProjectDefaults();
		Actor.ApplyFixture(*C.Fixture);

		// PLACED LIKE A BED AND NOT LIKE A TABLE, and for the same reason: a sofa has a front and a
		// back, the back goes against the wall, and a drawing's yaw is a one-in-two chance of being
		// the half turn that puts a wall of cushions facing the plaster. FHFFixturePlacement resolves
		// it from the anchor wall.
		Actor.SetActorTransform(FHFFixturePlacement::AgainstWall(*C.Fixture, C.FloorZ(), C.AnchorWall));
	}

	void SeedTable(const FHFFixtureContext& C, AHFElementActor& Element)
	{
		AHFTableActor& Actor = static_cast<AHFTableActor&>(Element);

		Actor.ApplyProjectDefaults();
		Actor.ApplyFixture(*C.Fixture);

		// FREE-STANDING, and this is the one group in the catalogue where that is the right answer. A
		// table has no back to put against anything: the drawn position IS where it sits, and pulling
		// it to a wall face - the fix every bought wall fitting needed - would be inventing an
		// intention the drawing never had. Origin at the centre of the footprint, for the same reason.
		Actor.SetActorTransform(FHFFixturePlacement::FreeStanding(*C.Fixture, C.FloorZ()));
	}

	void SeedChair(const FHFFixtureContext& C, AHFElementActor& Element)
	{
		AHFChairActor& Actor = static_cast<AHFChairActor&>(Element);

		Actor.ApplyProjectDefaults();
		Actor.ApplyFixture(*C.Fixture);

		// A CHAIR'S YAW IS REAL INFORMATION, unlike a run of joinery's. Which side of the table it is
		// on is exactly what the yaw says, and there is no wall to resolve it against - so it is
		// honoured rather than re-derived. FreeStanding does precisely that.
		Actor.SetActorTransform(FHFFixturePlacement::FreeStanding(*C.Fixture, C.FloorZ()));
	}

	void SeedCounter(const FHFFixtureContext& C, AHFElementActor& Element)
	{
		AHFCounterActor& Actor = static_cast<AHFCounterActor&>(Element);

		Actor.ApplyProjectDefaults();
		Actor.ApplyFixture(*C.Fixture);

		// THE HOLES, WORKED OUT BY THE ONLY LAYER THAT CAN SEE TWO FIXTURES AT ONCE. Empty for a
		// counter with nothing set into it, which is an uncut slab and not an error.
		if (C.SetIn != nullptr)
		{
			if (const TArray<FHFCounterAperture>* Apertures = C.SetIn->AperturesByHost.Find(C.Fixture->Id))
			{
				Actor.Counter.Apertures = *Apertures;
			}
		}

		Actor.SetActorTransform(FHFFixturePlacement::AgainstWall(*C.Fixture, C.FloorZ(), C.AnchorWall));
	}

	/**
	 * A fixture set INTO a counter: levelled to the host's built top and turned with the host.
	 *
	 * Falls back to the drawing's own base and rotation where there is no host, which is what the
	 * utility sink is - a bowl at counter height with no counter under it, and the drawn figure is
	 * all there is to go on.
	 */
	FTransform SetInPlacement(const FHFFixtureContext& C)
	{
		if (C.SetIn != nullptr)
		{
			const double* SurfaceZ = C.SetIn->SurfaceZ.Find(C.Fixture->Id);
			const double* SurfaceYaw = C.SetIn->SurfaceYaw.Find(C.Fixture->Id);

			if (SurfaceZ != nullptr && SurfaceYaw != nullptr)
			{
				return FHFFixturePlacement::OnSurface(*C.Fixture, *SurfaceZ, *SurfaceYaw);
			}
		}

		// The drawn rim: a sink's height is its bowl depth measured DOWN from the rim, so the rim is
		// the top of the drawn box and not its base.
		return FHFFixturePlacement::OnSurface(*C.Fixture,
			C.FloorZ() + C.Fixture->BaseZ + C.Fixture->Height, C.Fixture->RotationDegrees);
	}

	void SeedSink(const FHFFixtureContext& C, AHFElementActor& Element)
	{
		AHFSinkActor& Actor = static_cast<AHFSinkActor&>(Element);

		Actor.ApplyProjectDefaults();
		Actor.ApplyFixture(*C.Fixture);
		Actor.SetActorTransform(SetInPlacement(C));
	}

	void SeedHob(const FHFFixtureContext& C, AHFElementActor& Element)
	{
		AHFHobActor& Actor = static_cast<AHFHobActor&>(Element);

		Actor.ApplyProjectDefaults();
		Actor.ApplyFixture(*C.Fixture);

		// A HOB'S Z = 0 IS THE STONE, so it goes exactly where the counter's top is - not where the
		// drawing's BaseZ put it, for the same reason a sink does not.
		Actor.SetActorTransform(SetInPlacement(C));
	}

	void SeedChimney(const FHFFixtureContext& C, AHFElementActor& Element)
	{
		AHFChimneyActor& Actor = static_cast<AHFChimneyActor&>(Element);

		Actor.ApplyProjectDefaults();
		Actor.ApplyFixture(*C.Fixture);

		// THE DUCT HAS TO REACH THE SOFFIT, and the soffit is a false ceiling whose depth is a project
		// setting. Measured here, where the room and its ceilings are both visible, and handed in as a
		// length - exactly as a ceiling fan's rod is. A chimney built to a fixed duct length in a room
		// whose ceiling somebody deepened has its flue buried in plasterboard.
		double SoffitAboveCanopy = 0.0;

		if (C.Room != nullptr)
		{
			const double SoffitZ = C.Room->CeilingHeight - C.SoffitDrop;
			const double CanopyTopZ = C.Fixture->BaseZ + C.Fixture->Height;
			SoffitAboveCanopy = SoffitZ - CanopyTopZ;
		}

		Actor.ApplyCeilingAbove(SoffitAboveCanopy);
		Actor.SetActorTransform(FHFFixturePlacement::AgainstWall(*C.Fixture, C.FloorZ(), C.AnchorWall));
	}

	// -------------------------------------------------------------------------- the bathroom fittings
	//
	// SIX TYPES, ONE PLACEMENT RULE. A WC, a shower, a wall-hung basin, a geyser, a mirror and a towel
	// rail are all bought to a fixed size and screwed to plaster, so every one of them is placed by
	// FHFFixturePlacement::OnWallFace - back on the finished face, position along the wall and height
	// exactly as drawn. Not one of them is scribed to a gap the way a run of joinery is.
	//
	// That is not a tidiness argument. The drawn depth positions in this flat are approximate by
	// nature and two of them are badly wrong: both geysers are drawn with 107.5 mm of their back
	// INSIDE W_Mid_Upper, and both mirrors hang 47.5 mm off the plaster. See OnWallFace.

	void SeedWC(const FHFFixtureContext& C, AHFElementActor& Element)
	{
		AHFWCActor& Actor = static_cast<AHFWCActor&>(Element);

		Actor.ApplyProjectDefaults();
		Actor.ApplyFixture(*C.Fixture);
		Actor.SetActorTransform(FHFFixturePlacement::OnWallFace(*C.Fixture, C.FloorZ(), C.AnchorWall));
	}

	/**
	 * A basin, which is the one fitting in the group that may or may not have something under it.
	 *
	 * BOTH BATHROOMS DRAW A "COUNTER BASIN" AND ONLY ONE OF THEM HAS A COUNTER. The master's sits on
	 * the vanity at exactly the vanity's own centre; the common bathroom has no vanity at all, so the
	 * same drawn box is a wall-hung basin and everything holding it up - the shroud over its trap - is
	 * below the drawn box and outside it.
	 *
	 * Which it is cannot be answered by the basin, by its actor or by its generator: it is a question
	 * about another fixture. So it is resolved here, in the one layer that can see both, exactly as
	 * "which counter is this sink cut into" already is.
	 */
	void SeedBasin(const FHFFixtureContext& C, AHFElementActor& Element)
	{
		AHFBasinActor& Actor = static_cast<AHFBasinActor&>(Element);

		Actor.ApplyProjectDefaults();
		Actor.ApplyFixture(*C.Fixture);

		const bool bOnAHost = C.SetIn != nullptr && C.SetIn->HasHost(C.Fixture->Id);

		Actor.ApplyMount(bOnAHost ? EHFBasinMount::CounterTop : EHFBasinMount::WallHung);

		if (bOnAHost)
		{
			// Levelled to what the vanity's stone actually came out at, not to the 800 the drawing
			// gave it: that figure is a carcass plus a slab added up, and the slab's thickness is a
			// figure this project owns. The same rule a sink's rim follows.
			Actor.SetActorTransform(SetInPlacement(C));
			return;
		}

		// Nothing under it, so it hangs on the wall - and the wall face is where its back goes.
		Actor.SetActorTransform(FHFFixturePlacement::OnWallFace(*C.Fixture, C.FloorZ(), C.AnchorWall));
	}

	void SeedShower(const FHFFixtureContext& C, AHFElementActor& Element)
	{
		AHFShowerActor& Actor = static_cast<AHFShowerActor&>(Element);

		Actor.ApplyProjectDefaults();
		Actor.ApplyFixture(*C.Fixture);
		Actor.SetActorTransform(FHFFixturePlacement::OnWallFace(*C.Fixture, C.FloorZ(), C.AnchorWall));
	}

	void SeedGeyser(const FHFFixtureContext& C, AHFElementActor& Element)
	{
		AHFGeyserActor& Actor = static_cast<AHFGeyserActor&>(Element);

		Actor.ApplyProjectDefaults();
		Actor.ApplyFixture(*C.Fixture);
		Actor.SetActorTransform(FHFFixturePlacement::OnWallFace(*C.Fixture, C.FloorZ(), C.AnchorWall));
	}

	void SeedMirror(const FHFFixtureContext& C, AHFElementActor& Element)
	{
		AHFMirrorActor& Actor = static_cast<AHFMirrorActor&>(Element);

		Actor.ApplyProjectDefaults();
		Actor.ApplyFixture(*C.Fixture);
		Actor.SetActorTransform(FHFFixturePlacement::OnWallFace(*C.Fixture, C.FloorZ(), C.AnchorWall));
	}

	void SeedTowelRail(const FHFFixtureContext& C, AHFElementActor& Element)
	{
		AHFTowelRailActor& Actor = static_cast<AHFTowelRailActor&>(Element);

		Actor.ApplyProjectDefaults();
		Actor.ApplyFixture(*C.Fixture);
		Actor.SetActorTransform(FHFFixturePlacement::OnWallFace(*C.Fixture, C.FloorZ(), C.AnchorWall));
	}

	// ------------------------------------------------------------------------------- the services
	//
	// TWO PLACEMENT RULES, AND THE SPLIT IS NOT ALPHABETICAL. Everything bolted to plaster - the
	// sockets, the switch plates, the consumer unit, the split AC heads - goes on the finished FACE, by
	// FHFFixturePlacement::OnWallFace, exactly as the bathroom fittings do. Everything standing on the
	// floor - the refrigerator, the washing machine, the two condensing units - keeps the gap the
	// drawing gave it, by AgainstWall.
	//
	// That second rule is deliberate rather than a leftover. An appliance is pushed up near a wall, not
	// screwed to it, and all four of these need air behind them: pulling a condensing unit flush to a
	// parapet would bury the coil it rejects heat through, and the two in this flat are drawn 267 and
	// 367 mm clear for exactly that reason.

	void SeedAccessoryPlate(const FHFFixtureContext& C, AHFElementActor& Element)
	{
		AHFAccessoryPlateActor& Actor = static_cast<AHFAccessoryPlateActor&>(Element);

		Actor.ApplyProjectDefaults();
		Actor.ApplyFixture(*C.Fixture);
		Actor.SetActorTransform(FHFFixturePlacement::OnWallFace(*C.Fixture, C.FloorZ(), C.AnchorWall));
	}

	void SeedDistributionBoard(const FHFFixtureContext& C, AHFElementActor& Element)
	{
		AHFDistributionBoardActor& Actor = static_cast<AHFDistributionBoardActor&>(Element);

		Actor.ApplyProjectDefaults();
		Actor.ApplyFixture(*C.Fixture);
		Actor.SetActorTransform(FHFFixturePlacement::OnWallFace(*C.Fixture, C.FloorZ(), C.AnchorWall));
	}

	void SeedSplitAC(const FHFFixtureContext& C, AHFElementActor& Element)
	{
		AHFSplitACActor& Actor = static_cast<AHFSplitACActor&>(Element);

		Actor.ApplyProjectDefaults();
		Actor.ApplyFixture(*C.Fixture);

		// NOTHING ABOUT THE CEILING IS DONE HERE, and that is the point rather than an omission. A
		// split head answers to the soffit over it by FHFCeilingFit::RuleFor returning Lowers, which
		// runs on the SPEC before the fixture reaches this function at all - so the BaseZ arriving here
		// is already the fitted one. Adjusting it a second time would drop the unit by the soffit
		// twice, which is precisely the cumulative fault AHFFanActor::ApplyCeilingAbove is documented
		// against.
		Actor.SetActorTransform(FHFFixturePlacement::OnWallFace(*C.Fixture, C.FloorZ(), C.AnchorWall));
	}

	void SeedCondenser(const FHFFixtureContext& C, AHFElementActor& Element)
	{
		AHFCondenserActor& Actor = static_cast<AHFCondenserActor&>(Element);

		Actor.ApplyProjectDefaults();
		Actor.ApplyFixture(*C.Fixture);
		Actor.SetActorTransform(FHFFixturePlacement::AgainstWall(*C.Fixture, C.FloorZ(), C.AnchorWall));
	}

	void SeedRefrigerator(const FHFFixtureContext& C, AHFElementActor& Element)
	{
		AHFRefrigeratorActor& Actor = static_cast<AHFRefrigeratorActor&>(Element);

		// The project's figures FIRST: ApplyFixture reads the drawn box and the skirting setback this
		// resolves is subtracted from it, so re-seeding in the other order would leave the cabinet
		// standing inside the board.
		Actor.ApplyProjectDefaults();
		Actor.ApplyFixture(*C.Fixture);
		Actor.SetActorTransform(FHFFixturePlacement::AgainstWall(*C.Fixture, C.FloorZ(), C.AnchorWall));
	}

	void SeedWashingMachine(const FHFFixtureContext& C, AHFElementActor& Element)
	{
		AHFWashingMachineActor& Actor = static_cast<AHFWashingMachineActor&>(Element);

		Actor.ApplyProjectDefaults();
		Actor.ApplyFixture(*C.Fixture);
		Actor.SetActorTransform(FHFFixturePlacement::AgainstWall(*C.Fixture, C.FloorZ(), C.AnchorWall));
	}

	void SeedCeilingFan(const FHFFixtureContext& C, AHFElementActor& Element)
	{
		AHFFanActor& Actor = static_cast<AHFFanActor&>(Element);

		// Re-seeded in full because the rod length is CUMULATIVE and there is no way to subtract the
		// ceiling that used to be there. ApplyCeilingAbove adds to the project's figure by design, so
		// calling it twice would hang the fan a ceiling lower each time.
		Actor.ApplyProjectDefaults(EHFFanKind::Ceiling);
		Actor.ApplyFixture(*C.Fixture);
		Actor.ApplyCeilingAbove(C.SoffitDrop);
		Actor.SetActorTransform(AHFFanActor::PlacementFor(*C.Fixture, C.Room, C.AnchorWall));
	}

	void SeedExhaustFan(const FHFFixtureContext& C, AHFElementActor& Element)
	{
		AHFFanActor& Actor = static_cast<AHFFanActor&>(Element);

		Actor.ApplyProjectDefaults(EHFFanKind::Exhaust);
		Actor.ApplyFixture(*C.Fixture);

		// AN EXTRACT HAS A FAR SIDE. The duct is cored through the masonry, so the wall's thickness
		// comes to the fan as a dimension: a generator may not reach for the wall it stands in.
		if (C.AnchorWall != nullptr)
		{
			Actor.Fan.HostWallThickness = C.AnchorWall->Thickness;

			// The hole goes with the fan. A case that moved and a duct that did not is a bare square
			// opening in a finished wall with the fan sitting below it.
			if (C.WallsToRecut != nullptr)
			{
				C.WallsToRecut->Add(C.AnchorWall->Id);
			}
		}

		Actor.SetActorTransform(AHFFanActor::PlacementFor(*C.Fixture, C.Room, C.AnchorWall));
	}

	/** One row per fixture type that becomes an element actor. Everything else is spec-only. */
	const TArray<FHFFixtureRecipe>& FixtureRecipes()
	{
		// A function-local static rather than a file-scope one: the rows name UClasses, and
		// StaticClass() is only answerable once the module has been loaded.
		static const TArray<FHFFixtureRecipe> Recipes = {
			{ EHFFixtureType::Wardrobe, AHFWardrobeActor::StaticClass(),
				TEXT("Wardrobe"), &SeedWardrobe },

			{ EHFFixtureType::KitchenBaseCabinet, AHFCasedGoodsActor::StaticClass(),
				TEXT("Case"), &SeedCasedGoods },
			{ EHFFixtureType::KitchenWallCabinet, AHFCasedGoodsActor::StaticClass(),
				TEXT("Case"), &SeedCasedGoods },

			// THE SAME ACTOR AND THE SAME SEEDING FOR ALL FIVE, which is the whole return on the cased
			// goods kit: a TV console, a bedside unit and a shoe rack differ from a kitchen base unit in
			// their proportions and in which front each bay carries, and in nothing that this layer can
			// see. What each of them IS lives in AHFCasedGoodsActor's recipe, in one switch, where the
			// five can be read against each other.
			{ EHFFixtureType::TVUnit, AHFCasedGoodsActor::StaticClass(),
				TEXT("Case"), &SeedCasedGoods },
			{ EHFFixtureType::Nightstand, AHFCasedGoodsActor::StaticClass(),
				TEXT("Case"), &SeedCasedGoods },
			{ EHFFixtureType::ShoeRack, AHFCasedGoodsActor::StaticClass(),
				TEXT("Case"), &SeedCasedGoods },

			// A VANITY IS A CASED GOOD, and it goes through the same actor and the same seeding as the
			// five above it. What separates it from a kitchen base unit is that its stone top is part of
			// the same object rather than a fixture of its own - which is a line in the recipe, not a
			// difference this layer can see.
			{ EHFFixtureType::Vanity, AHFCasedGoodsActor::StaticClass(),
				TEXT("Case"), &SeedCasedGoods },

			{ EHFFixtureType::Bed, AHFBedActor::StaticClass(),
				TEXT("Bed"), &SeedBed },
			{ EHFFixtureType::StudyTable, AHFDeskActor::StaticClass(),
				TEXT("Desk"), &SeedDesk },

			// THE LOOSE FURNITURE, and the only group in the table that is not fitted to anything. A
			// sofa is placed against its wall like a bed; the two tables and the chairs are placed
			// where they were drawn, because there is nothing for them to be scribed to.
			{ EHFFixtureType::Sofa, AHFSofaActor::StaticClass(),
				TEXT("Sofa"), &SeedSofa },
			{ EHFFixtureType::DiningTable, AHFTableActor::StaticClass(),
				TEXT("Table"), &SeedTable },
			{ EHFFixtureType::CoffeeTable, AHFTableActor::StaticClass(),
				TEXT("Table"), &SeedTable },
			{ EHFFixtureType::Chair, AHFChairActor::StaticClass(),
				TEXT("Chair"), &SeedChair },

			{ EHFFixtureType::CounterTop, AHFCounterActor::StaticClass(),
				TEXT("Counter"), &SeedCounter },

			{ EHFFixtureType::Sink, AHFSinkActor::StaticClass(),
				TEXT("Sink"), &SeedSink },
			{ EHFFixtureType::Hob, AHFHobActor::StaticClass(),
				TEXT("Hob"), &SeedHob },
			{ EHFFixtureType::Chimney, AHFChimneyActor::StaticClass(),
				TEXT("Chimney"), &SeedChimney },

			{ EHFFixtureType::WC, AHFWCActor::StaticClass(),
				TEXT("WC"), &SeedWC },
			{ EHFFixtureType::Basin, AHFBasinActor::StaticClass(),
				TEXT("Basin"), &SeedBasin },
			{ EHFFixtureType::Shower, AHFShowerActor::StaticClass(),
				TEXT("Shower"), &SeedShower },
			{ EHFFixtureType::Geyser, AHFGeyserActor::StaticClass(),
				TEXT("Geyser"), &SeedGeyser },
			{ EHFFixtureType::Mirror, AHFMirrorActor::StaticClass(),
				TEXT("Mirror"), &SeedMirror },
			{ EHFFixtureType::TowelRail, AHFTowelRailActor::StaticClass(),
				TEXT("TowelRail"), &SeedTowelRail },

			// THE SERVICES. Two actors carry six of the seven types, and both splits are the cased
			// goods argument again: a socket and a switch plate are one construction with two
			// fillings, and the four remaining appliances differ in what is inside the box.
			{ EHFFixtureType::PowerSocket, AHFAccessoryPlateActor::StaticClass(),
				TEXT("Plate"), &SeedAccessoryPlate },
			{ EHFFixtureType::SwitchPlate, AHFAccessoryPlateActor::StaticClass(),
				TEXT("Plate"), &SeedAccessoryPlate },
			{ EHFFixtureType::DistributionBoard, AHFDistributionBoardActor::StaticClass(),
				TEXT("DB"), &SeedDistributionBoard },

			{ EHFFixtureType::ACIndoorUnit, AHFSplitACActor::StaticClass(),
				TEXT("ACIndoor"), &SeedSplitAC },
			{ EHFFixtureType::ACOutdoorUnit, AHFCondenserActor::StaticClass(),
				TEXT("ACOutdoor"), &SeedCondenser },

			{ EHFFixtureType::Refrigerator, AHFRefrigeratorActor::StaticClass(),
				TEXT("Fridge"), &SeedRefrigerator },
			{ EHFFixtureType::WashingMachine, AHFWashingMachineActor::StaticClass(),
				TEXT("Washer"), &SeedWashingMachine },

			{ EHFFixtureType::CeilingFan, AHFFanActor::StaticClass(),
				TEXT("Fan"), &SeedCeilingFan },
			{ EHFFixtureType::ExhaustFan, AHFFanActor::StaticClass(),
				TEXT("Fan"), &SeedExhaustFan },
		};

		return Recipes;
	}

	const FHFFixtureRecipe* RecipeFor(EHFFixtureType Type)
	{
		for (const FHFFixtureRecipe& Recipe : FixtureRecipes())
		{
			if (Recipe.Type == Type)
			{
				return &Recipe;
			}
		}
		return nullptr;
	}

	/** True for a fixture that stands ON another fixture's top rather than on the floor. */
	bool IsSetIntoASurface(EHFFixtureType Type)
	{
		return Type == EHFFixtureType::Sink || Type == EHFFixtureType::Hob
			|| Type == EHFFixtureType::Basin;
	}

	/**
	 * True for a fixture that can carry another one on its top.
	 *
	 * A VANITY IS A HOST TOO, and it is a host of a different kind from a worktop: it carries its own
	 * stone rather than having a separate CounterTop fixture over it. Both present a finished surface
	 * at a height this project's figures decide, which is all "host" means here.
	 */
	bool IsSetInHost(EHFFixtureType Type)
	{
		return AHFCounterActor::Builds(Type) || Type == EHFFixtureType::Vanity;
	}

	/**
	 * How far above the room floor a host's finished top comes out.
	 *
	 * Resolved from what is BUILT rather than from the drawn box, for both kinds of host and for the
	 * same reason: a counter's slab thickness and a vanity's top are project figures, so a set-in
	 * fixture placed at its own drawn BaseZ goes stale the moment either is edited.
	 */
	double HostTopZ(const FHFFixture& Host)
	{
		if (AHFCounterActor::Builds(Host.Type))
		{
			return AHFCounterActor::BuiltTopZ(Host);
		}

		// A cased good's stone is INSIDE its drawn height - see FHFCasedGoodsParams::TopThickness -
		// so its finished surface is the run's own height, and a run with no top at all presents its
		// top board at exactly the same place.
		return Host.BaseZ + AHFCasedGoodsActor::ParamsFor(Host).Height;
	}

	/**
	 * True when a set-in fixture drops THROUGH its host's top rather than standing on it.
	 *
	 * A sink and a hob are cut in; a counter basin is a vessel that stands on the stone with only its
	 * waste through it. Cutting the basin's footprint out of the vanity would leave a 500 x 400 hole
	 * with a bowl balanced over it and daylight round three of its sides.
	 */
	bool CutsThroughItsHost(EHFFixtureType Type)
	{
		return Type != EHFFixtureType::Basin;
	}

	/**
	 * Which set-in fixtures land on which host, and where the host's finished top actually is.
	 *
	 * A set-in fixture is matched to a host by FOOTPRINT rather than by an id on the spec, because
	 * the drawing does not carry one: a sink is drawn where it is, and the counter it is drawn on top
	 * of is the counter it is set into. Matching on geometry also means a sink that has been dragged
	 * off its counter stops being cut into it, which is the honest answer rather than a hole in the
	 * wrong slab.
	 *
	 * @param Fixtures The FITTED fixtures, so a host that a ceiling moved is the host that is built.
	 */
	FHFSetInResolution ResolveSetInFixtures(const FHFHouseSpec& Spec, const TArray<FHFFixture>& Fixtures)
	{
		FHFSetInResolution Out;

		for (const FHFFixture& SetIn : Fixtures)
		{
			if (!IsSetIntoASurface(SetIn.Type))
			{
				continue;
			}

			for (const FHFFixture& Host : Fixtures)
			{
				if (!IsSetInHost(Host.Type) || Host.RoomId != SetIn.RoomId
					|| !FHFFixturePlacement::FootprintContains(Host, SetIn.Position))
				{
					continue;
				}

				const FHFWall* HostWall = Spec.FindWall(Host.AnchorWallId);
				const double HostYaw = FHFFixturePlacement::FacingYaw(Host, HostWall);

				if (CutsThroughItsHost(SetIn.Type))
				{
					// Into the HOST's own frame: undo the host's yaw about its footprint centre, then
					// measure from the front-left corner the host is set out from. Done here rather than
					// inside the counter because only this layer knows both transforms.
					const double Radians = FMath::DegreesToRadians(HostYaw);
					const double C = FMath::Cos(Radians);
					const double S = FMath::Sin(Radians);

					const FVector2D Delta = SetIn.Position - Host.Position;
					const FVector2D InHost(Delta.X * C + Delta.Y * S, -Delta.X * S + Delta.Y * C);

					FHFCounterAperture Aperture;
					Aperture.FixtureId = SetIn.Id;
					Aperture.Centre = InHost + Host.Footprint * 0.5;

					// THE HOLE IS SMALLER THAN THE APPLIANCE. Both of these sit on a rim that laps the
					// cut edge, and cutting the footprint itself would leave the appliance resting on
					// nothing with a slot of daylight all round it.
					const double Lap = AHFCounterActor::RimLapFor(SetIn.Type);
					Aperture.Size = FVector2D(
						FMath::Max(SetIn.Footprint.X - 2.0 * Lap, 0.0),
						FMath::Max(SetIn.Footprint.Y - 2.0 * Lap, 0.0));

					Out.AperturesByHost.FindOrAdd(Host.Id).Add(Aperture);
				}

				// Where the fitting's own datum goes, resolved from what is BUILT rather than from
				// the BaseZ the drawing gave it: that figure was arrived at by adding up a carcass, a
				// plinth and a slab, and it goes stale the moment any of the three changes.
				const FHFRoom* Room = Spec.FindRoom(Host.RoomId);
				Out.SurfaceZ.Add(SetIn.Id, (Room != nullptr ? Room->FloorZ : 0.0) + HostTopZ(Host));

				// And turned WITH the host. A hob set square to the drawing, in a run turned through a
				// right angle, is a hob across the run.
				Out.SurfaceYaw.Add(SetIn.Id, HostYaw);
				break;
			}
		}

		return Out;
	}

	/**
	 * How far the false ceiling in a room hangs below the slab at a point in it.
	 *
	 * Asked per fixture rather than per room, because the answer depends on WHERE in the room it is:
	 * the same ceiling covers its perimeter band and leaves its centre open.
	 */
	double SoffitDropAt(const FHFHouseSpec& Spec, const FHFFixture& Fixture, const FHFRoom* Room)
	{
		if (Room == nullptr)
		{
			return 0.0;
		}

		double Drop = 0.0;
		for (const FHFFalseCeiling& Ceiling : Spec.FalseCeilings)
		{
			if (Ceiling.RoomId == Fixture.RoomId)
			{
				Drop = FMath::Max(Drop,
					FHFGenerators::CeilingSoffitDropAt(Ceiling, *Room, Fixture.Position));
			}
		}
		return Drop;
	}
}

AHFHouseActor::AHFHouseActor()
{
	PrimaryActorTick.bCanEverTick = false;

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	Lines = CreateDefaultSubobject<ULineBatchComponent>(TEXT("Preview"));
	Lines->SetupAttachment(Root);
	// Lines must persist rather than expire on a timer: this is a static preview of a saved level,
	// not transient debug output.
	Lines->DefaultLifeTime = 0.0f;
	Lines->bCalculateAccurateBounds = true;
}

void AHFHouseActor::SetSpec(const FHFHouseSpec& InSpec)
{
	Spec = InSpec;

	// One conversion, here. Everything downstream - preview, tools, geometry - works in
	// centimetres and never has to ask what units a spec arrived in.
	FHFUnits::ConvertToCentimeters(Spec);

	// AND ONE TEMPLATE RESOLUTION, here, for the same reason. A ceiling that names a design carries
	// no figures until this runs, and after it the spec holds plain numbers that the preview, the
	// tools and the geometry all read the same way. Re-applied rather than trusted from the incoming
	// spec, because the design belongs to the project: a drawing says "cove", and how deep a cove is
	// in this project is what the settings page is for.
	//
	// Idempotent, so a spec that arrived already resolved is unchanged, and Custom ceilings are left
	// exactly as authored.
	FHFCeilingTemplates::Apply(Spec, FHFBuildDefaults::FromProjectSettings().Ceiling);

	if (!Spec.SourceDrawing.IsEmpty())
	{
		SourceDrawing = Spec.SourceDrawing;
	}

	Rebuild();
	BuildGeometry();
}

void AHFHouseActor::Destroyed()
{
	ClearGeometry();
	Super::Destroyed();
}

void AHFHouseActor::ClearGeometry()
{
	for (AActor* Element : ElementActors)
	{
		if (IsValid(Element))
		{
			Element->Destroy();
		}
	}
	ElementActors.Reset();
}

void AHFHouseActor::BuildGeometry()
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	// Hand-edited elements survive a rebuild. Destroying and respawning everything would throw
	// away modelling work without warning, which is exactly the failure the per-element edit flag
	// exists to prevent - it would be pointless if the house-level rebuild ignored it.
	TMap<TPair<UClass*, FName>, AHFElementActor*> Preserved;
	TArray<TObjectPtr<AActor>> Survivors;

	// Open amounts are user state, exactly as a hand edit is. The elements themselves are respawned
	// here, so a pose held only on the actor would die with it and every door in the flat would slam
	// shut on a rebuild. Poses are carried across by element id and put back once the parts exist.
	TMap<TPair<UClass*, FName>, FHFPartPoses> PosedElements;

	for (AActor* Element : ElementActors)
	{
		const AHFArticulatedActor* Articulated = Cast<AHFArticulatedActor>(Element);
		if (IsValid(Articulated))
		{
			FHFPartPoses Poses = Articulated->CapturePartPoses();
			if (!Poses.IsEmpty())
			{
				PosedElements.Add({ Articulated->GetClass(), Articulated->ElementId }, MoveTemp(Poses));
			}
		}

		AHFElementActor* Typed = Cast<AHFElementActor>(Element);
		if (IsValid(Typed) && Typed->ShouldPreserveOnRebuild())
		{
			Preserved.Add({ Typed->GetClass(), Typed->ElementId }, Typed);
			Survivors.Add(Typed);
		}
		else if (IsValid(Element))
		{
			Element->Destroy();
		}
	}

	ElementActors = MoveTemp(Survivors);
	const int32 PreservedCount = ElementActors.Num();

	// ------------------------------------------------- what the ceilings do to everything under them
	//
	// RESOLVED ONCE, HERE, AND READ BY EVERYTHING BELOW. A fitting near the ceiling is placed by its
	// actor, drawn by the preview, and - for an extract - is the thing a hole in a wall is cored for.
	// Three consumers of one answer, and while each of them read Spec.Fixtures directly they were three
	// chances to resolve it differently. See FHFCeilingFit for why this is recomputed rather than
	// declared in the spec.
	TArray<FString> Moved;
	const TArray<FHFFixture> Fixtures = ResolveFixtures(&Moved);

	for (const FString& Line : Moved)
	{
		UE_LOG(LogHouseForge, Log, TEXT("HouseForge ceiling fit: %s"), *Line);
	}

	FActorSpawnParameters Params;
	Params.Owner = this;

	// The project's answer to chamfers, UVs and the lightmap channel, resolved once for the whole
	// house. Stamped on every element below rather than left to each actor's compiled-in default,
	// which is the same rule the ceiling and skirting figures follow: only the composing layer reads
	// the settings, and everything under it is handed a plain value.
	const FHFRenderFinish RenderDefaults = FHFBuildDefaults::FromProjectSettings().Render;

	// Returns null when an element was preserved, which tells the caller to leave it alone.
	auto Spawn = [&](UClass* Class, const FName& Id, const FString& Label) -> AActor*
	{
		if (Preserved.Contains({ Class, Id }))
		{
			return nullptr;
		}

		AActor* Actor = World->SpawnActor<AActor>(Class, FTransform::Identity, Params);
		if (Actor != nullptr)
		{
#if WITH_EDITOR
			Actor->SetActorLabel(Label);
#endif
			if (AHFElementActor* Typed = Cast<AHFElementActor>(Actor))
			{
				Typed->ElementId = Id;
				Typed->RenderFinish = RenderDefaults;
			}
			Actor->AttachToActor(this, FAttachmentTransformRules::KeepWorldTransform);
			ElementActors.Add(Actor);
		}
		return Actor;
	};

	// ------------------------------------------------------------------ resolve the frame, once
	//
	// Worked out here, before anything is generated, because only the composing layer can see more
	// than one element and a generator may not go looking for the rest of the house. See the note
	// on build order above, and .claude/rules/04-conventions.md.
	TArray<FHFStructuralCut> ColumnCuts;
	TArray<FBox> ColumnBounds;
	ColumnCuts.Reserve(Spec.Columns.Num());
	ColumnBounds.Reserve(Spec.Columns.Num());

	for (const FHFColumn& Column : Spec.Columns)
	{
		const FHFStructuralCut Cut = FHFGenerators::StructuralCutFor(Column);
		if (Cut.IsValid())
		{
			ColumnCuts.Add(Cut);
			ColumnBounds.Add(BoundsOf(Cut));
		}
	}

	TArray<FHFStructuralCut> BeamCuts;
	TArray<FBox> BeamBounds;
	BeamCuts.Reserve(Spec.Beams.Num());
	BeamBounds.Reserve(Spec.Beams.Num());

	for (const FHFBeam& Beam : Spec.Beams)
	{
		BeamCuts.Add(FHFGenerators::StructuralCutFor(Beam));
		BeamBounds.Add(BoundsOf(BeamCuts.Last()));
	}

	// What each beam is cut by: every column it lands on, and every beam that runs through it.
	TArray<TArray<FHFStructuralCut>> BeamStructure;
	BeamStructure.SetNum(Spec.Beams.Num());

	for (int32 Index = 0; Index < Spec.Beams.Num(); ++Index)
	{
		if (!BeamCuts[Index].IsValid())
		{
			continue;
		}

		for (int32 Column = 0; Column < ColumnCuts.Num(); ++Column)
		{
			if (VolumesOverlap(BeamBounds[Index], ColumnBounds[Column]))
			{
				BeamStructure[Index].Add(ColumnCuts[Column]);
			}
		}

		for (int32 Other = 0; Other < Spec.Beams.Num(); ++Other)
		{
			if (Other != Index && BeamCuts[Other].IsValid()
				&& VolumesOverlap(BeamBounds[Index], BeamBounds[Other])
				&& BeamRunsThrough(Spec.Beams[Other], Spec.Beams[Index]))
			{
				BeamStructure[Index].Add(BeamCuts[Other]);
			}
		}
	}

	TArray<FHFStructuralCut> WallCuts;
	TArray<FBox> WallBounds;
	WallCuts.Reserve(Spec.Walls.Num());
	WallBounds.Reserve(Spec.Walls.Num());

	for (const FHFWall& Wall : Spec.Walls)
	{
		WallCuts.Add(FHFGenerators::StructuralCutFor(Wall));
		WallBounds.Add(BoundsOf(WallCuts.Last()));
	}

	// Masonry is displaced by all of it, including by the masonry that outranks it.
	auto StructureInWall = [&Spec = Spec, &ColumnCuts, &ColumnBounds, &BeamCuts, &BeamBounds,
		&WallCuts, &WallBounds](const FHFWall& Wall)
	{
		const FBox Bounds = BoundsOf(Wall);
		TArray<FHFStructuralCut> Cuts;

		for (int32 Index = 0; Index < WallCuts.Num(); ++Index)
		{
			if (Spec.Walls[Index].Id != Wall.Id && WallCuts[Index].IsValid()
				&& VolumesOverlap(Bounds, WallBounds[Index])
				&& WallRunsThrough(Spec.Walls[Index], Wall))
			{
				Cuts.Add(WallCuts[Index]);
			}
		}

		for (int32 Index = 0; Index < ColumnCuts.Num(); ++Index)
		{
			if (VolumesOverlap(Bounds, ColumnBounds[Index]))
			{
				Cuts.Add(ColumnCuts[Index]);
			}
		}

		for (int32 Index = 0; Index < BeamCuts.Num(); ++Index)
		{
			if (BeamCuts[Index].IsValid() && VolumesOverlap(Bounds, BeamBounds[Index]))
			{
				Cuts.Add(BeamCuts[Index]);
			}
		}

		return Cuts;
	};

	// Walls carry their own openings, so each wall owns everything it needs to rebuild itself
	// when its thickness or height is edited.
	for (const FHFWall& Wall : Spec.Walls)
	{
		AHFWallActor* WallActor = Cast<AHFWallActor>(Spawn(AHFWallActor::StaticClass(), Wall.Id,
			FString::Printf(TEXT("Wall_%s"), *Wall.Id.ToString())));
		if (WallActor == nullptr)
		{
			continue;
		}

		WallActor->Wall = Wall;
		WallActor->Structure = StructureInWall(Wall);
		WallActor->Openings = OpeningsInWall(Spec, Fixtures, Wall);

		// WHERE THE PLASTER RUNS ON. The masonry is built around all of that structure, so every
		// arris it leaves against a column, a beam or the wall it butts into is a boundary between
		// two solids and not an edge of the building - and a chamfer there scores a groove down a
		// junction that ought to read as one continuous plane. The footing goes on the list for the
		// same reason: a wall stands on a slab, so its bottom arris is not an arris either, and that
		// one shows wherever no skirting covers it.
		WallActor->FlushVolumes = WallActor->Structure;
		WallActor->FlushVolumes.Add(FootingUnder(Wall));

		WallActor->Regenerate();
	}

	// WHERE EACH ROOM'S SKIRTING RUNS, resolved once per room and handed over as a value.
	//
	// The fixtures are the FITTED ones, so a wardrobe that lost height to a ceiling is still the
	// wardrobe the skirting is cut around - the fit never moves a carcass in plan, but taking the
	// same list everything else takes is what stops the two drifting.
	const FHFSkirtingParams SkirtingParams = FHFBuildDefaults::FromProjectSettings().Skirting;

	// AND WHICH OF THEM WILL ACTUALLY STAND THERE. A skirting break is a hole in the board, so it
	// has to be paid for by a carcass in front of it - see FHFSkirting::IsScribedJoinery.
	const TSet<FName> BuiltIds = BuiltFixtureIds(Fixtures);

	for (const FHFRoom& Room : Spec.Rooms)
	{
		AHFRoomActor* RoomActor = Cast<AHFRoomActor>(Spawn(AHFRoomActor::StaticClass(), Room.Id,
			FString::Printf(TEXT("Room_%s"), *Room.Id.ToString())));
		if (RoomActor == nullptr)
		{
			continue;
		}

		RoomActor->Room = Room;
		RoomActor->SlabThickness = SlabThickness;
		RoomActor->Skirting = FHFSkirting::For(Room, Spec.Walls, Spec.Openings, Spec.Columns, Fixtures,
			SkirtingParams, &BuiltIds);
		RoomActor->Regenerate();
	}

	// False ceilings, with the fan positions they have to be cut for.
	for (const FHFFalseCeiling& Ceiling : Spec.FalseCeilings)
	{
		const FHFRoom* Room = Spec.FindRoom(Ceiling.RoomId);
		if (Room == nullptr || Ceiling.Style == EHFCeilingStyle::None)
		{
			continue;
		}

		AHFCeilingActor* CeilingActor = Cast<AHFCeilingActor>(Spawn(AHFCeilingActor::StaticClass(), Ceiling.Id,
			FString::Printf(TEXT("Ceiling_%s"), *Ceiling.Id.ToString())));
		if (CeilingActor == nullptr)
		{
			continue;
		}

		CeilingActor->Ceiling = Ceiling;
		CeilingActor->Room = *Room;

		// THE HOLE IS A CONSEQUENCE OF THE FAN, exactly as an extract's duct is. It was a fixed 8 -
		// a 16 cm square opening for a 2.2 cm rod - whose corners showed past the 15 cm motor
		// housing as four bright wedges from below. Sized from the fan's own rod now, and the
		// canopy is sized to cover it, so the two cannot drift.
		double HoleHalfSide = 0.0;

		for (const FHFFixture& Fixture : Fixtures)
		{
			if (Fixture.Type == EHFFixtureType::CeilingFan && Fixture.RoomId == Ceiling.RoomId)
			{
				CeilingActor->FanDrops.Add(Fixture.Position);

				// The largest, because one radius cuts every hole in this ceiling and a hole too
				// small for a rod is a rod through plasterboard.
				HoleHalfSide = FMath::Max(HoleHalfSide,
					AHFFanActor::ParamsFor(Fixture).RodHoleHalfSide());
			}
		}

		if (HoleHalfSide > 0.0)
		{
			CeilingActor->FanDropRadius = HoleHalfSide;
		}

		CeilingActor->Regenerate();
	}

	for (int32 Index = 0; Index < Spec.Beams.Num(); ++Index)
	{
		const FHFBeam& Beam = Spec.Beams[Index];
		if (AHFBeamActor* BeamActor = Cast<AHFBeamActor>(Spawn(AHFBeamActor::StaticClass(), Beam.Id,
			FString::Printf(TEXT("Beam_%s"), *Beam.Id.ToString()))))
		{
			BeamActor->Beam = Beam;
			BeamActor->Structure = BeamStructure[Index];

			// A beam frames into the columns it lands on and into the beam that runs through it, so
			// the faces it leaves against them are not arrises either.
			BeamActor->FlushVolumes = BeamActor->Structure;
			BeamActor->Regenerate();
		}
	}

	for (const FHFColumn& Column : Spec.Columns)
	{
		if (AHFColumnActor* ColumnActor = Cast<AHFColumnActor>(Spawn(AHFColumnActor::StaticClass(), Column.Id,
			FString::Printf(TEXT("Column_%s"), *Column.Id.ToString()))))
		{
			ColumnActor->Column = Column;
			ColumnActor->Regenerate();
		}
	}

	for (const FHFOpening& Opening : Spec.Openings)
	{
		const FHFWall* Wall = Spec.FindWall(Opening.WallId);
		if (Wall == nullptr || Opening.Kind == EHFOpeningKind::Archway)
		{
			continue;
		}

		if (AHFOpeningActor* OpeningActor = Cast<AHFOpeningActor>(Spawn(AHFOpeningActor::StaticClass(), Opening.Id,
			FString::Printf(TEXT("Opening_%s"), *Opening.Id.ToString()))))
		{
			OpeningActor->Opening = Opening;
			OpeningActor->HostWall = *Wall;

			// Settings resolve HERE, in the composing layer, and never inside a generator - see
			// .claude/rules/04-conventions.md. Before Regenerate, so the first mesh this actor ever
			// builds already has the project's figures on it.
			//
			// Only on a freshly spawned actor: Spawn returns null for a preserved one, so an opening
			// somebody has edited by hand keeps the figures it was built with rather than having a
			// project-wide setting reach in and change it.
			OpeningActor->ApplyProjectDefaults();
			OpeningActor->Regenerate();
		}
	}

	// ---------------------------------------------------------------------------------- fixtures
	//
	// ONE LOOP, DRIVEN BY THE RECIPE TABLE. Which types build, which actor class each becomes and how
	// each is seeded are all one answer, given once - so this loop, the rebuild in
	// ApplyProjectSettingsToCeilings, the skirting resolver and the build report cannot come to
	// different conclusions about what is in the level.

	// Resolved before the loop, because a counter has to be cut for a sink that has not been built
	// yet and the sink has to be levelled to a counter that has not been built yet either. Neither
	// can ask the other; only this layer can see both.
	const FHFSetInResolution SetIn = ResolveSetInFixtures(Spec, Fixtures);

	for (const FHFFixture& Fixture : Fixtures)
	{
		const FHFFixtureRecipe* Recipe = RecipeFor(Fixture.Type);
		if (Recipe == nullptr)
		{
			continue;
		}

		// Spawn returns null for a preserved element, which is what leaves a fixture somebody has
		// modelled on holding the figures it was built with rather than having a project-wide setting
		// reach in and change it.
		AHFElementActor* Actor = Cast<AHFElementActor>(
			Spawn(Recipe->Class, Fixture.Id,
				FString::Printf(TEXT("%s_%s"), Recipe->NamePrefix, *Fixture.Id.ToString())));

		if (Actor == nullptr)
		{
			continue;
		}

		FHFFixtureContext Context;
		Context.Spec = &Spec;
		Context.Fixture = &Fixture;
		Context.Room = Spec.FindRoom(Fixture.RoomId);
		Context.AnchorWall = Spec.FindWall(Fixture.AnchorWallId);
		Context.SetIn = &SetIn;
		Context.Fixtures = &Fixtures;

		// WHAT IS BETWEEN A CEILING-HUNG FITTING AND THE ROOM. A ceiling fan hangs from the structural
		// slab, so a false ceiling over it is something its rod has to get through - and a rod that was
		// a fixed project figure built the whole rotor inside the plasterboard of any room with a full
		// drop. Resolved for every fixture rather than only for fans: the chimney's duct has exactly
		// the same problem one milestone later, and a second way of asking the same question is how the
		// two answers drift.
		Context.SoffitDrop = SoffitDropAt(Spec, Fixture, Context.Room);

		// Null: the walls above already carry every duct, because OpeningsInWall was handed this same
		// fitted list before any of them was generated. Only a rebuild has to go back and re-cut one.
		Context.WallsToRecut = nullptr;

		Recipe->Seed(Context, *Actor);
		Actor->Regenerate();
	}

	// Once every element has been regenerated its parts exist again, so the poses captured above can
	// go back on. Done in one pass at the end rather than per element type, so any future articulated
	// element gets it without having to remember to ask.
	for (AActor* Element : ElementActors)
	{
		AHFArticulatedActor* Articulated = Cast<AHFArticulatedActor>(Element);
		if (Articulated == nullptr)
		{
			continue;
		}

		if (const FHFPartPoses* Poses = PosedElements.Find({ Articulated->GetClass(), Articulated->ElementId }))
		{
			Articulated->RestorePartPoses(*Poses);
		}
	}

	UE_LOG(LogHouseForge, Log,
		TEXT("HouseForge built '%s': %d element actors, %d preserved as hand-edited."),
		*Spec.Name, ElementActors.Num(), PreservedCount);
}

int32 AHFHouseActor::ApplyProjectSettingsToCeilings()
{
	// The project's designs, resolved onto this house's own spec. Everything on the spec is already
	// in centimetres - SetSpec converts exactly once, at ingest - so the templates need no scaling.
	FHFCeilingTemplates::Apply(Spec, FHFBuildDefaults::FromProjectSettings().Ceiling);

	auto FindElement = [this](UClass* Class, const FName& Id) -> AHFElementActor*
	{
		for (AActor* Element : ElementActors)
		{
			AHFElementActor* Typed = Cast<AHFElementActor>(Element);
			if (IsValid(Typed) && Typed->GetClass() == Class && Typed->ElementId == Id)
			{
				return Typed;
			}
		}
		return nullptr;
	};

	int32 Rebuilt = 0;

	// ----------------------------------------------------------------------------- the ceilings
	for (const FHFFalseCeiling& Ceiling : Spec.FalseCeilings)
	{
		AHFCeilingActor* CeilingActor =
			Cast<AHFCeilingActor>(FindElement(AHFCeilingActor::StaticClass(), Ceiling.Id));

		// Asked before anything is touched, so a hand-modelled ceiling keeps the parameters it was
		// built with as well as the mesh - a re-seed would change what Revert To Generated produced.
		if (CeilingActor == nullptr || CeilingActor->ShouldPreserveOnRebuild())
		{
			continue;
		}

		// The fan holes and their radius are properties of the fans, not of the settings, so they
		// are left exactly as the build worked them out.
		CeilingActor->Ceiling = Ceiling;
		CeilingActor->Regenerate();
		++Rebuilt;
	}

	// ---------------------------------------------------------- and everything that answers to them
	//
	// THE DEPENDENCY SET, not the two elements that were noticed first. A ceiling figure does not
	// change one thing in place: it changes what hangs between the room and the slab, and every
	// fitting near the top of a room is set out against it. FHFCeilingFit works out what gives, once,
	// and the loops below put the answer on whatever carries part of it.
	const TArray<FHFFixture> Fixtures = ResolveFixtures(nullptr);

	// Walls whose duct has moved with the extract that blows through it. Collected rather than
	// rebuilt in place: a wall can carry more than one, and it is cheaper and safer to cut all of its
	// holes once from the fitted list than to edit its opening array.
	TSet<FName> WallsToRecut;

	// THE SAME TABLE AND THE SAME SEEDING FUNCTIONS THE FRESH BUILD USES. This loop used to be a
	// switch carrying its own copy of each type's seeding sequence, one edit away from disagreeing
	// with the build about how a fixture is put together - a difference that would only ever show on
	// the SECOND build, after somebody dragged a settings slider. Whatever a fresh build does to a
	// fixture is now by construction what this does to it.
	//
	// Re-resolved rather than remembered, for the same reason: the slab's thickness is a project
	// figure, so the height a sink is levelled to and the hole it drops through both move when
	// somebody changes it.
	const FHFSetInResolution SetIn = ResolveSetInFixtures(Spec, Fixtures);

	for (const FHFFixture& Fixture : Fixtures)
	{
		const FHFRoom* FixtureRoom = Spec.FindRoom(Fixture.RoomId);
		if (FixtureRoom == nullptr)
		{
			continue;
		}

		const FHFFixtureRecipe* Recipe = RecipeFor(Fixture.Type);
		if (Recipe == nullptr)
		{
			continue;
		}

		AHFElementActor* Actor = FindElement(Recipe->Class, Fixture.Id);

		// Asked before anything is touched, so a hand-modelled fixture keeps its parameters as well as
		// its mesh - a re-seed would change what Revert To Generated produced.
		if (Actor == nullptr || Actor->ShouldPreserveOnRebuild())
		{
			continue;
		}

		FHFFixtureContext Context;
		Context.Spec = &Spec;
		Context.Fixture = &Fixture;
		Context.Room = FixtureRoom;
		Context.AnchorWall = Spec.FindWall(Fixture.AnchorWallId);
		Context.SetIn = &SetIn;
		Context.Fixtures = &Fixtures;
		Context.SoffitDrop = SoffitDropAt(Spec, Fixture, FixtureRoom);

		// The difference from a fresh build, and the only one: the walls already exist and were cored
		// for the fitting where it used to be, so a fitting that has moved takes its hole with it.
		Context.WallsToRecut = &WallsToRecut;

		Recipe->Seed(Context, *Actor);
		Actor->Regenerate();
		++Rebuilt;
	}

	for (const FHFWall& Wall : Spec.Walls)
	{
		if (!WallsToRecut.Contains(Wall.Id))
		{
			continue;
		}

		AHFWallActor* WallActor = Cast<AHFWallActor>(FindElement(AHFWallActor::StaticClass(), Wall.Id));
		if (WallActor == nullptr || WallActor->ShouldPreserveOnRebuild())
		{
			continue;
		}

		WallActor->Openings = OpeningsInWall(Spec, Fixtures, Wall);
		WallActor->Regenerate();
		++Rebuilt;
	}

	return Rebuilt;
}

TArray<FHFFixture> AHFHouseActor::FittedFixtures() const
{
	return ResolveFixtures(nullptr);
}

bool AHFHouseActor::BuildsGeometryFor(EHFFixtureType Type)
{
	// DERIVED FROM THE SPAWN TABLE RATHER THAN WRITTEN OUT BESIDE IT. This used to be a second list of
	// the same types, kept in step with the spawn loops by hand, and it is read by the skirting
	// resolver - which cuts a break in a room's skirting for every fitted run standing against a wall.
	// A type in this list but not in the table is a length of missing skirting with nothing standing
	// in it: bare plaster meeting bare floor for the width of a unit nobody modelled. A type in the
	// table but not in this list is a carcass driven through a skirting board. Both read as correct in
	// every test that does not render the room.
	return RecipeFor(Type) != nullptr;
}

TSet<FName> AHFHouseActor::BuiltFixtureIds(const TArray<FHFFixture>& Fixtures)
{
	TSet<FName> Ids;
	for (const FHFFixture& Fixture : Fixtures)
	{
		if (BuildsGeometryFor(Fixture.Type))
		{
			Ids.Add(Fixture.Id);
		}
	}
	return Ids;
}

TArray<FHFFixture> AHFHouseActor::ResolveFixtures(TArray<FString>* OutMoved) const
{
	// The composing layer's job, and the only line here that knows a settings object could exist.
	// FHFCeilingFit takes the clearance as a value, exactly as every generator takes its figures -
	// see .claude/rules/04-conventions.md.
	//
	// No unit conversion: the spec on this actor is already in centimetres, because SetSpec converts
	// exactly once at ingest, and the settings page is in centimetres too.
	const double Clearance = FHFBuildDefaults::FromProjectSettings().Ceiling.FixtureSoffitClearance;

	// ------------------------------------------------- how big the fittings actually come out
	//
	// THE DRAWN BOX IS NOT ALWAYS THE OBJECT. A plan marks an extract at the fan that was bought and
	// the case built for it carries a bezel sized to lap the CORNERS of the chase cored behind it, so
	// a fan drawn 250 stands 316 tall. Fitting the drawn box under the soffit leaves the difference
	// inside the plasterboard - which is this very defect, one step later, and it was found by
	// rendering the room rather than by any assertion about the spec.
	//
	// Answered HERE and not inside the resolver, for the same reason the ceiling's rod hole and the
	// wall's duct are: AHFFanActor::ParamsFor is the one place that knows what fan ends up standing
	// there, project figures and all, and three things sized from three different ideas of one fan is
	// exactly what that function exists to prevent.
	TMap<FName, double> BuiltHeights;
	for (const FHFFixture& Fixture : Spec.Fixtures)
	{
		if (Fixture.Type == EHFFixtureType::ExhaustFan)
		{
			BuiltHeights.Add(Fixture.Id, AHFFanActor::ParamsFor(Fixture).CaseHalfWidth() * 2.0);
		}
		else if (AHFBedActor::Builds(Fixture.Type))
		{
			// A BED DRAWN 600 HIGH STANDS 1050, and the difference is the headboard. The drawn height
			// is the top of the mattress, because that is the figure a plan dimensions a bed by and the
			// one that has to agree with the nightstand beside it - so the drawn box is 450 mm shorter
			// than the object every time. Nothing in this flat has a ceiling low enough for it to
			// matter, and that is exactly why it is supplied: the answer must not depend on the room
			// happening to be tall.
			BuiltHeights.Add(Fixture.Id, AHFBedActor::ParamsFor(Fixture).BuiltHeight());
		}
		else if (AHFWCActor::Builds(Fixture.Type))
		{
			// A WC DRAWN 400 HIGH STANDS 764, and the difference is its cistern. The drawn figure is
			// the SEAT, because that is what a plan dimensions a WC by and what has to agree with the
			// rest of the room - so the drawn box is barely half the object every single time. As with
			// the bed, nothing in this flat has a ceiling low enough for it to matter, and that is
			// exactly why it is supplied: the answer must not depend on the room happening to be tall.
			BuiltHeights.Add(Fixture.Id, AHFWCActor::ParamsFor(Fixture).BuiltHeight());
		}
		else if (AHFSplitACActor::Builds(Fixture.Type))
		{
			// A SPLIT HEAD COMES OUT EXACTLY ITS DRAWN HEIGHT, and it is supplied anyway. The casing
			// is an extruded section built strictly inside the drawn envelope, so this is a no-op
			// today - and that is the reason to state it here rather than to leave the type out. It is
			// the one fitting in this catalogue that hangs at 2200 in three rooms with a false ceiling
			// over it, so the day somebody gives the moulding a top grille standing 20 mm proud, the
			// ceiling fit finds out from this line instead of from a render.
			BuiltHeights.Add(Fixture.Id, AHFSplitACActor::ParamsFor(Fixture).BuiltHeight());
		}
		else if (AHFCasedGoodsActor::Builds(Fixture.Type))
		{
			// And a run capped with a cornice stands proud of its own carcass by the moulding's height,
			// for the same reason and with the same consequence: a ceiling fitted to the drawn box
			// would leave the cornice inside the plasterboard. Zero cornice returns the drawn height,
			// so this is a no-op for the four cased-goods types that do not carry one.
			BuiltHeights.Add(Fixture.Id, AHFCasedGoodsActor::ParamsFor(Fixture).BuiltHeight());
		}
	}

	return FHFCeilingFit::FitAll(Spec, Clearance, &BuiltHeights, OutMoved);
}

void AHFHouseActor::PostLoad()
{
	Super::PostLoad();
	Rebuild();
}

void AHFHouseActor::PostRegisterAllComponents()
{
	Super::PostRegisterAllComponents();
	Rebuild();
}

#if WITH_EDITOR
void AHFHouseActor::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	Rebuild();
}
#endif

void AHFHouseActor::Rebuild()
{
	if (Lines == nullptr)
	{
		return;
	}

	Lines->Flush();

	if (!bShowPreview)
	{
		Lines->MarkRenderStateDirty();
		return;
	}

	DrawWalls();

	if (bShowOpenings)   { DrawOpenings(); }
	if (bShowRooms)      { DrawRooms(); }
	if (bShowStructure)  { DrawStructure(); }
	if (bShowFixtures)   { DrawFixtures(); }
	if (bShowCeilings)   { DrawCeilings(); }

	Lines->MarkRenderStateDirty();

	UE_LOG(LogHouseForge, Verbose, TEXT("HouseForge preview rebuilt: %d walls, %d rooms, %d fixtures."),
		Spec.Walls.Num(), Spec.Rooms.Num(), Spec.Fixtures.Num());
}

void AHFHouseActor::DrawPrism(const TArray<FVector2D>& Polygon, double BottomZ, double TopZ,
	const FLinearColor& Color, float Thickness, bool bVerticals)
{
	const int32 Count = Polygon.Num();
	if (Count < 2 || Lines == nullptr)
	{
		return;
	}

	for (int32 i = 0; i < Count; ++i)
	{
		const FVector2D& A = Polygon[i];
		const FVector2D& B = Polygon[(i + 1) % Count];

		const FVector A0(A.X, A.Y, BottomZ);
		const FVector B0(B.X, B.Y, BottomZ);
		const FVector A1(A.X, A.Y, TopZ);
		const FVector B1(B.X, B.Y, TopZ);

		Lines->DrawLine(A0, B0, Color, DepthPriority, Thickness, 0.0f);
		if (!FMath::IsNearlyEqual(BottomZ, TopZ))
		{
			Lines->DrawLine(A1, B1, Color, DepthPriority, Thickness, 0.0f);
			if (bVerticals)
			{
				Lines->DrawLine(A0, A1, Color, DepthPriority, Thickness, 0.0f);
			}
		}
	}
}

void AHFHouseActor::DrawWalls()
{
	for (const FHFWall& Wall : Spec.Walls)
	{
		const TArray<FVector2D> Footprint = WallFootprint(Wall);
		if (Footprint.Num() == 4)
		{
			DrawPrism(Footprint, Wall.BaseZ, Wall.BaseZ + Wall.Height, ColourWall, 2.0f);
		}
	}
}

void AHFHouseActor::DrawOpenings()
{
	for (const FHFOpening& Opening : Spec.Openings)
	{
		const FHFWall* Wall = Spec.FindWall(Opening.WallId);
		if (Wall == nullptr)
		{
			continue;
		}

		const double Length = Wall->Length();
		if (Length <= UE_KINDA_SMALL_NUMBER)
		{
			continue;
		}

		const FVector2D Direction = (Wall->End - Wall->Start) / Length;
		const FVector2D Normal(-Direction.Y, Direction.X);
		const double HalfWidth = Opening.Width * 0.5;
		const double HalfThickness = Wall->Thickness * 0.5;

		const FVector2D Near = Wall->Start + Direction * (Opening.OffsetAlongWall - HalfWidth);
		const FVector2D Far  = Wall->Start + Direction * (Opening.OffsetAlongWall + HalfWidth);

		TArray<FVector2D> Reveal;
		Reveal.Add(Near + Normal * HalfThickness);
		Reveal.Add(Far + Normal * HalfThickness);
		Reveal.Add(Far - Normal * HalfThickness);
		Reveal.Add(Near - Normal * HalfThickness);

		const double BottomZ = Wall->BaseZ + Opening.SillHeight;
		const double TopZ = Wall->BaseZ + Opening.HeadHeight();
		DrawPrism(Reveal, BottomZ, TopZ, ColourOpening, 2.5f);

		DrawSwing(Opening, *Wall, Direction, Normal, Near, Far, BottomZ);
	}
}

void AHFHouseActor::DrawSwing(const FHFOpening& Opening, const FHFWall& Wall,
	const FVector2D& Direction, const FVector2D& Normal,
	const FVector2D& Near, const FVector2D& Far, double BaseZ)
{
	if (Opening.Kind != EHFOpeningKind::Door || Opening.Swing == EHFSwing::None || Lines == nullptr)
	{
		return;
	}

	// Without this a door hung on the wrong side is completely invisible from above, which makes
	// the swing impossible to check against the drawing's swing arc.
	const bool bHingeAtNear = (Opening.Swing == EHFSwing::InwardLeft || Opening.Swing == EHFSwing::OutwardLeft);
	const FVector2D Hinge = bHingeAtNear ? Near : Far;
	const double Side = (Opening.Swing == EHFSwing::InwardLeft || Opening.Swing == EHFSwing::InwardRight) ? 1.0 : -1.0;

	const double Width = Opening.Width;
	const FVector2D LeafTip = Hinge + Normal * (Width * Side);

	// The leaf, drawn open at ninety degrees as it is on a plan.
	const FVector HingePoint(Hinge.X, Hinge.Y, BaseZ);
	Lines->DrawLine(HingePoint, FVector(LeafTip.X, LeafTip.Y, BaseZ), ColourSwing, DepthPriority, 2.0f, 0.0f);

	// The arc it sweeps, from the open leaf back to the closed position in the wall.
	const FVector2D Closed = bHingeAtNear ? Far : Near;
	constexpr int32 Segments = 12;
	FVector2D Previous = LeafTip;
	for (int32 i = 1; i <= Segments; ++i)
	{
		const double Alpha = static_cast<double>(i) / Segments;
		const FVector2D Swept =
			Normal * Side * FMath::Cos(Alpha * HALF_PI) +
			(Closed - Hinge).GetSafeNormal() * FMath::Sin(Alpha * HALF_PI);

		const FVector2D Point = Hinge + Swept * Width;
		Lines->DrawLine(FVector(Previous.X, Previous.Y, BaseZ), FVector(Point.X, Point.Y, BaseZ),
			ColourSwing, DepthPriority, 1.0f, 0.0f);
		Previous = Point;
	}
}

void AHFHouseActor::DrawRooms()
{
	for (const FHFRoom& Room : Spec.Rooms)
	{
		if (Room.Boundary.Num() < 3)
		{
			continue;
		}

		// Floor outline plus the slab line above it, so room height reads in a perspective view.
		DrawPrism(Room.Boundary, Room.FloorZ, Room.FloorZ + Room.CeilingHeight,
			ColourRoom, 1.0f, /*bVerticals*/ false);
	}
}

void AHFHouseActor::DrawStructure()
{
	for (const FHFBeam& Beam : Spec.Beams)
	{
		const double Length = Beam.Length();
		if (Length <= UE_KINDA_SMALL_NUMBER)
		{
			continue;
		}

		const FVector2D Direction = (Beam.End - Beam.Start) / Length;
		const FVector2D Normal(-Direction.Y, Direction.X);
		const double Half = Beam.Width * 0.5;

		TArray<FVector2D> Footprint;
		Footprint.Add(Beam.Start + Normal * Half);
		Footprint.Add(Beam.End + Normal * Half);
		Footprint.Add(Beam.End - Normal * Half);
		Footprint.Add(Beam.Start - Normal * Half);

		// Beams hang down from the slab soffit, so they occupy ClearHeight..SoffitZ.
		DrawPrism(Footprint, Beam.ClearHeight(), Beam.SoffitZ, ColourBeam, 2.0f);
	}

	for (const FHFColumn& Column : Spec.Columns)
	{
		DrawPrism(RectFootprint(Column.Position, Column.Size, Column.RotationDegrees),
			Column.BaseZ, Column.BaseZ + Column.Height, ColourColumn, 2.0f);
	}
}

void AHFHouseActor::DrawFixtures()
{
	// The fitted list, not the spec's. A preview drawn from the drawing while the level is built from
	// the resolved figures is a preview that disagrees with the thing it is previewing, and the whole
	// point of the wireframe is to be checkable against what was built.
	for (const FHFFixture& Fixture : ResolveFixtures(nullptr))
	{
		if (Fixture.Footprint.X <= 0.0 || Fixture.Footprint.Y <= 0.0)
		{
			continue;
		}

		const FHFRoom* Room = Spec.FindRoom(Fixture.RoomId);
		const double FloorZ = Room ? Room->FloorZ : 0.0;

		// A CEILING-MOUNTED FIXTURE HANGS, and its BaseZ is a drop measured DOWN from the ceiling -
		// see FHFFixture::IsCeilingMounted. Drawn from the floor it came out at 30 to 60 cm above the
		// carpet, which is not where any fan in this flat is and is why the preview never showed one
		// where the level put it.
		//
		// WHICH ceiling depends on how the thing is fixed, and that is the whole of the difference
		// between the two hanging rules: a fan reaches the structural slab on a rod that lengthens to
		// suit, and a surface-mounted fitting is screwed to the finished soffit and follows it down.
		double TopZ = FloorZ + Fixture.BaseZ + Fixture.Height;

		if (Fixture.IsCeilingMounted() && Room != nullptr)
		{
			const double MountZ =
				(FHFCeilingFit::RuleFor(Fixture.Type) == EHFCeilingFitRule::HangsFromSoffit)
					? FHFCeilingFit::LowestSoffitZOver(Fixture, *Room, Spec.FalseCeilings)
					: FloorZ + Room->CeilingHeight;

			TopZ = MountZ - Fixture.BaseZ;
		}

		DrawPrism(RectFootprint(Fixture.Position, Fixture.Footprint, Fixture.RotationDegrees),
			TopZ - Fixture.Height, TopZ, ColourFixture, 1.5f);
	}
}

void AHFHouseActor::DrawCeilings()
{
	for (const FHFFalseCeiling& Ceiling : Spec.FalseCeilings)
	{
		if (Ceiling.Style == EHFCeilingStyle::None)
		{
			continue;
		}

		const FHFRoom* Room = Spec.FindRoom(Ceiling.RoomId);
		const TArray<FVector2D>& Polygon = Ceiling.ExplicitPolygon.Num() >= 3
			? Ceiling.ExplicitPolygon
			: (Room ? Room->Boundary : Ceiling.ExplicitPolygon);

		if (Polygon.Num() < 3 || Room == nullptr)
		{
			continue;
		}

		// The finished soffit sits Drop below the slab.
		const double SoffitZ = Room->FloorZ + Room->CeilingHeight - Ceiling.Drop;
		DrawPrism(Polygon, SoffitZ, SoffitZ, ColourCeiling, 2.0f, /*bVerticals*/ false);

		for (const FVector2D& Light : Ceiling.LightPositions)
		{
			const FVector Centre(Light.X, Light.Y, SoffitZ);
			Lines->DrawCircle(Centre, FVector::XAxisVector, FVector::YAxisVector,
				ColourCeiling, 8.0f, 12, DepthPriority);
		}
	}
}
