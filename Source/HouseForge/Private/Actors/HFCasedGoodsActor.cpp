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
		EHFShelfMaterial ShelfMaterial, bool bBankAtStart)
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

			// WHICH end, decided by the composing layer, because a pull-out needs somewhere to pull
			// out TO. In an L-shaped kitchen the return run stands in front of one end of the other
			// run, and a bank put there is a bank that cannot open - see bBankAtRunStart.
			if (bBankAtStart)
			{
				Unit.Bays[0] = Bank;
			}
			else
			{
				Unit.Bays.Last() = Bank;
			}
		}
	}

	/**
	 * A shoe rack: tilt-out flaps STACKED, one over another, with a shelf inside each.
	 *
	 * ## Why the flaps stack rather than standing side by side
	 *
	 * Because that is what a shoe cabinet is. A pair of side-hung doors on a 350-deep box is a
	 * cupboard that happens to have shoes in it; the thing every Indian foyer actually has is a
	 * shallow cabinet whose fronts tip forward out of the way, precisely because 350 mm is too little
	 * depth to open a door into a hallway somebody is standing in. So the drawing's ShutterCount is
	 * read as the number of TIERS, and each tier is its own carcass in the stack - a real board
	 * between them, exactly as a wardrobe's loft is a real box on top of its body.
	 *
	 * ## And why the drawn shelf count still adds up
	 *
	 * ShelfCount is every horizontal division the drawing shows inside the rack. One of those
	 * divisions per tier boundary is the carcass board between two tiers and is not a shelf at all,
	 * so what is left over is what actually gets shelved out - 3 divisions over 2 tiers is one tier
	 * board and one shelf inside each tier, which is the rack that was drawn and not a rack with two
	 * extra boards in it.
	 */
	void ComposeShoeRackTiers(const FHFFixtureParams& Spec, FHFCasedGoodsParams& P)
	{
		const int32 Tiers = FMath::Clamp(Spec.ShutterCount, 1, 6);

		// The divisions the drawing marked, less the ones that are tier boundaries, shared out. Never
		// negative: a drawing that counted fewer divisions than there are tiers has described the tier
		// boards themselves, and the answer to that is tiers with nothing extra in them.
		const int32 Shelved = FMath::Max(Spec.ShelfCount - (Tiers - 1), 0);
		const int32 PerTier = Shelved / Tiers;

		FHFCaseBay Compartment;
		Compartment.Front = EHFCaseFront::Shutter;

		// THE FLAP, and the one thing that makes this a shoe rack rather than a cupboard. Its stop
		// angle is not this file's business - FHFCasedGoodsKit resolves it from the project's
		// TiltOutFlapAngleDegrees, because only the kit is holding the joinery figures.
		Compartment.Motion = EHFShutterMotion::BottomHung;
		Compartment.LeafCount = 1;
		Compartment.Interior = PerTier > 0 ? EHFCaseInterior::Shelves : EHFCaseInterior::None;

		// Stated rather than left at the sentinel. Zero asks the project how many shelves fit in the
		// clear height, and the project's answer is a 375 wardrobe compartment - one shelf, or none at
		// all, in a tier a shoe is meant to stand in.
		Compartment.ShelfCount = PerTier;
		Compartment.ShelfMaterial = EHFShelfMaterial::Ply;

		P.Units.Reset();

		for (int32 Tier = 0; Tier < Tiers; ++Tier)
		{
			FHFCaseUnit Unit;

			// Left at zero so Sanitise shares the stack equally. Stating a height here would be this
			// file working out the plinth a second time, and the two copies would drift.
			Unit.Height = 0.0;
			Unit.BayCount = 1;
			Unit.Bays.Add(Compartment);
			P.Units.Add(MoveTemp(Unit));
		}
	}

	/** Everything the drawing states about a run, and what its type makes of it. */
	void ReadFixture(const FHFFixture& Fixture, FHFCasedGoodsParams& P, bool bBankAtStart)
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
			ComposeBays(Unit, Spec, Spec.ShutterMotion, EHFShelfMaterial::Ply, bBankAtStart);
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
				Spec.bHasGlassInsert ? EHFShelfMaterial::Glass : EHFShelfMaterial::Ply, bBankAtStart);
			break;

		case EHFFixtureType::ShoeRack:
			// A stack rather than a run of bays, so it does not go through ComposeBays at all - see
			// ComposeShoeRackTiers. Returns early because it fills P.Units itself.
			P.Mount = EHFCaseMount::Plinth;
			ComposeShoeRackTiers(Spec, P);
			return;

		case EHFFixtureType::TVUnit:
			// A TALL STORAGE COLUMN AND A LOW CONSOLE ARE THE SAME OBJECT AT TWO HEIGHTS, and the
			// drawing already separates them: the column is drawn with shutters and shelves, the
			// console with drawers. ComposeBays reads exactly that, so there is nothing here beyond
			// the mount - which is the point of the shared kit.
			P.Mount = EHFCaseMount::Plinth;
			ComposeBays(Unit, Spec, Spec.ShutterMotion, EHFShelfMaterial::Ply, bBankAtStart);
			break;

		case EHFFixtureType::Nightstand:
			// A BEDSIDE UNIT IS A DRAWER BANK AND NOTHING ELSE. ComposeBays would give it a cupboard
			// bay beside the bank the moment a drawing marked a shutter count, which is not what
			// stands beside a bed: it is 450 wide, there is no room for two bays in it, and a hinged
			// door at that width swings across the bed rather than into the room.
			//
			// So the whole of it is one bay of drawers, and the count is the drawing's - falling back
			// to two, because that is what a nightstand has and a drawing that marked none has said
			// nothing rather than asked for a box with no fronts.
			{
				P.Mount = EHFCaseMount::Plinth;

				FHFCaseBay Bank;
				Bank.Front = EHFCaseFront::DrawerBank;
				Bank.DrawerCount = Spec.DrawerCount > 0 ? Spec.DrawerCount : 2;

				// Nearly equal, not the kitchen's 2:1. A bedside unit's drawers hold the same sort of
				// thing as each other; the deep-pan-drawer graduation belongs under a worktop.
				Bank.GradationRatio = 1.25;
				Bank.Interior = EHFCaseInterior::None;

				Unit.BayCount = 1;
				Unit.Bays.Add(Bank);
			}
			break;

		default:
			P.Mount = EHFCaseMount::Plinth;
			ComposeBays(Unit, Spec, Spec.ShutterMotion, EHFShelfMaterial::Ply, bBankAtStart);
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
	case EHFFixtureType::TVUnit:
	case EHFFixtureType::Nightstand:
	case EHFFixtureType::ShoeRack:
		return true;

	default:
		// The vanity lands with the sanitary group. A study table is deliberately NOT here: it is a
		// top on legs with a pedestal under one end, and a stack of carcasses cannot express the
		// knee space that makes it a desk rather than a sideboard. See FHFDeskKit.
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
	ReadFixture(Fixture, Case, bBankAtRunStart);
}

FHFCasedGoodsParams AHFCasedGoodsActor::ParamsFor(const FHFFixture& Fixture)
{
	FHFCasedGoodsParams P;
	P.Joinery = FHFBuildDefaults::FromProjectSettings().Joinery;
	ReadFixture(Fixture, P, /*bBankAtStart*/ false);

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
