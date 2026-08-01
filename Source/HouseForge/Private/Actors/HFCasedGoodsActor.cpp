// Copyright Siddartha G. All Rights Reserved.

#include "Actors/HFCasedGoodsActor.h"

#include "Model/HFBuildDefaults.h"

using namespace UE::Geometry;

namespace
{
	/**
	 * The bays of one run: cupboards, and a bank of drawers at the right-hand end.
	 *
	 * ShutterCount IS THE BAY COUNT, exactly as AHFWardrobeActor reads it, and DrawerCount adds one
	 * bay more when the drawing marked drawers. How many LEAVES each bay gets is a separate question
	 * with a separate answer - an 800 base unit has two 400 doors - and FHFCaseBay::LeafCount is
	 * where it is settled, from the project's module width rather than from the drawing.
	 *
	 * THE BANK GOES AT THE END OF THE RUN, and that is a decision rather than an accident. A sink's
	 * bowl hangs 200 mm below the counter and a hob's body 50, so the bay under either of them has to
	 * be a cupboard: a full-height drawer bank's top box reaches within 32 mm of the counter, and a
	 * bowl put through it would be a drawer that cannot shut. Both runs in the reference flat have
	 * their bowl and their hob in the middle of the run, which is where they belong - the drainer and
	 * the working space go beside them - so an end bay is where a bank can actually go.
	 * HouseForge.Editor.NothingIsSetIntoADrawer measures it in the built flat rather than trusting it.
	 */
	void ComposeBays(FHFCaseUnit& Unit, const FHFFixtureParams& Spec, EHFShutterMotion Motion,
		EHFShelfMaterial ShelfMaterial)
	{
		const int32 ShutterBays = FMath::Max(Spec.ShutterCount, 0);
		const int32 DrawerBays = Spec.DrawerCount > 0 ? 1 : 0;

		// Nothing counted is not "one bay": it is a drawing that said nothing, and the project has a
		// module width for exactly that. Left at zero, FHFCasedGoodsKit::Sanitise resolves it.
		Unit.BayCount = ShutterBays + DrawerBays;

		FHFCaseBay Cupboard;
		Cupboard.Front = EHFCaseFront::Shutter;
		Cupboard.Motion = Motion;
		Cupboard.bGlassInsert = Spec.bHasGlassInsert;
		Cupboard.Interior = EHFCaseInterior::Shelves;
		Cupboard.ShelfCount = Spec.ShelfCount;
		Cupboard.ShelfMaterial = ShelfMaterial;

		Unit.Bays.Reset();

		const int32 Bays = FMath::Max(Unit.BayCount, 1);
		for (int32 Bay = 0; Bay < Bays; ++Bay)
		{
			Unit.Bays.Add(Cupboard);
		}

		if (DrawerBays > 0)
		{
			FHFCaseBay Bank;
			Bank.Front = EHFCaseFront::DrawerBank;
			Bank.DrawerCount = Spec.DrawerCount;

			// The boxes fill the bay, so there is nothing left for a shelf to be in. Said out loud
			// rather than left to the kit, because a bay carrying both is a shelf through a drawer.
			Bank.Interior = EHFCaseInterior::None;

			Unit.Bays.Last() = Bank;
		}
	}

	/** Everything the drawing states about a run, and what its type makes of it. */
	void ReadFixture(const FHFFixture& Fixture, FHFCasedGoodsParams& P)
	{
		const FHFFixtureParams& Spec = Fixture.Params;

		P.Width = Fixture.Footprint.X;
		P.Depth = Fixture.Footprint.Y;
		P.Height = Fixture.Height;

		P.HandleStyle = Spec.HandleStyle;
		P.CorniceHeight = Spec.CorniceHeight;

		// Copied straight through, zero included: zero is the sentinel FHFCasedGoodsKit::Sanitise
		// resolves against the project's figure. Resolving it here instead would stamp a number onto
		// the actor and freeze it - the same rule AHFWardrobeActor::ApplyFixture states for a
		// wardrobe's plinth and bay count, and for the same reason.
		P.PlinthHeight = Spec.PlinthHeight;

		// A run standing against one wall has both its ends on show. An end dying into a return wall
		// is set on the actor afterwards, because nothing on the fixture says which.
		P.bLeftEndExposed = true;
		P.bRightEndExposed = true;

		FHFCaseUnit Unit;

		switch (Fixture.Type)
		{
		case EHFFixtureType::KitchenBaseCabinet:
			P.Mount = EHFCaseMount::Plinth;

			// NO TOP BOARD. The granite is this carcass's top, which is both how a base unit is built
			// and the only construction a sink or a hob can be set into - see FHFCarcassParams::bHasTop.
			Unit.bHasTop = false;
			ComposeBays(Unit, Spec, Spec.ShutterMotion, EHFShelfMaterial::Ply);
			break;

		case EHFFixtureType::KitchenWallCabinet:
			// SCREWED TO THE WALL WITH NOTHING UNDER IT. Not a plinth of zero height: the underside is
			// a finished surface somebody looks straight up at from across the kitchen, and the
			// skirting below runs on past rather than dying into a toe kick.
			P.Mount = EHFCaseMount::WallHung;

			// A display bay behind glass gets glass shelves, which is what a crockery unit is. At 8 mm
			// they sag past 600, and FHFShelfStackParams' own span rule breaks the stack with a mid
			// partition rather than letting one bow on camera.
			ComposeBays(Unit, Spec, Spec.ShutterMotion,
				Spec.bHasGlassInsert ? EHFShelfMaterial::Glass : EHFShelfMaterial::Ply);
			break;

		default:
			P.Mount = EHFCaseMount::Plinth;
			ComposeBays(Unit, Spec, Spec.ShutterMotion, EHFShelfMaterial::Ply);
			break;
		}

		P.Units.Reset();
		P.Units.Add(MoveTemp(Unit));
	}
}

bool AHFCasedGoodsActor::Builds(EHFFixtureType Type)
{
	switch (Type)
	{
	case EHFFixtureType::KitchenBaseCabinet:
	case EHFFixtureType::KitchenWallCabinet:
		return true;

	default:
		// The other five cased-goods types - TV unit, nightstand, shoe rack, vanity, study table -
		// land with the groups that own them. Their recipes are the only thing missing; the kit under
		// this actor already builds all seven shapes.
		return false;
	}
}

void AHFCasedGoodsActor::ApplyProjectDefaults()
{
	// The composing layer's job, and the only line in this class that knows a settings object could
	// exist. By the time the generator runs, everything it needs is already on the actor.
	Case.Joinery = FHFBuildDefaults::FromProjectSettings().Joinery;
}

void AHFCasedGoodsActor::ApplyFixture(const FHFFixture& Fixture)
{
	ReadFixture(Fixture, Case);
}

FHFCasedGoodsParams AHFCasedGoodsActor::ParamsFor(const FHFFixture& Fixture)
{
	FHFCasedGoodsParams P;
	P.Joinery = FHFBuildDefaults::FromProjectSettings().Joinery;
	ReadFixture(Fixture, P);

	return FHFCasedGoodsKit::Sanitise(P);
}

// ------------------------------------------------------------------------------------ generation
//
// Two calls to Build per regeneration, one for the shell and one for the parts, because that is the
// shape of AHFElementActor's contract - BuildMesh and BuildParts are separate const hooks and
// neither may leave anything behind for the other.

FDynamicMesh3 AHFCasedGoodsActor::BuildMesh() const
{
	return FHFCasedGoodsKit::Build(Case).Shell;
}

void AHFCasedGoodsActor::BuildParts(TArray<FHFMeshPart>& OutParts) const
{
	FHFCasedGoodsBuild Built = FHFCasedGoodsKit::Build(Case);
	OutParts.Append(MoveTemp(Built.Parts));
}
