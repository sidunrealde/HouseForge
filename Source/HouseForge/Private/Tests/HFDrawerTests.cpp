// Copyright Siddartha G. All Rights Reserved.

#include "HouseForge.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Geometry/HFJoineryKit.h"
#include "Geometry/HFMeshOps.h"
#include "MeshQueries.h"
#include "Misc/AutomationTest.h"
#include "Model/HFArticulation.h"
#include "Model/HFTypes.h"

using namespace UE::Geometry;

#define HF_TEST_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
	/** Standard Indian modular base unit: 450 module, 580 carcass, 18 mm sides, 3 mm reveal. */
	FHFDrawerParams MakeDrawerParams()
	{
		FHFDrawerParams Params;
		Params.ModuleWidth = 45.0;
		Params.ModuleHeight = 25.0;
		Params.CarcassDepth = 58.0;
		Params.CarcassSideThickness = 1.8;
		Params.RevealGap = 0.3;
		Params.BackClearance = 0.1;
		return Params;
	}

	FHFDrawerBankParams MakeBankParams(double BankHeight, int32 Count)
	{
		FHFDrawerBankParams Bank;
		Bank.Drawer = MakeDrawerParams();
		Bank.BankHeight = BankHeight;
		Bank.DrawerCount = Count;
		Bank.GradationRatio = 2.0;
		return Bank;
	}

	/** Heights fronts are actually cut to: 150 to 500 mm in 50 mm steps. */
	const TArray<double>& FrontLadder()
	{
		static const TArray<double> Ladder = { 15.0, 20.0, 25.0, 30.0, 35.0, 40.0, 45.0, 50.0 };
		return Ladder;
	}

	double Volume(const FDynamicMesh3& Mesh)
	{
		return TMeshQueries<FDynamicMesh3>::GetVolumeArea(Mesh).X;
	}
}

/**
 * Graduating a bank of drawer fronts.
 *
 * Real banks are graduated and snapped to the sizes fronts are cut to, and both halves of that have
 * to hold at every bank height - not just at the 720 carcass the arithmetic was tuned on.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFDrawerGraduationTest, "HouseForge.Joinery.DrawerGraduation", HF_TEST_FLAGS)

bool FHFDrawerGraduationTest::RunTest(const FString& Parameters)
{
	const double Reveal = MakeDrawerParams().RevealGap;
	const double DeepestFront = FrontLadder().Last();
	const double Rung = 5.0;

	auto SumOf = [](const TArray<double>& Values)
	{
		double Total = 0.0;
		for (const double Value : Values)
		{
			Total += Value;
		}
		return Total;
	};

	// The two banks the header names, unchanged. Anything that alters these has altered what a
	// kitchen looks like, not just an edge case.
	{
		TArray<double> Heights;
		if (!TestTrue(TEXT("A 72 cm bank takes three drawers"),
			FHFJoineryKit::GraduateDrawerFronts(MakeBankParams(72.0, 3), Heights)))
		{
			return false;
		}
		TestEqual(TEXT("Three fronts come back"), Heights.Num(), 3);
		TestNearlyEqual(TEXT("A 72 cm bank of three is 15 / 25 / 31.1: the top"), Heights[0], 15.0, 1e-6);
		TestNearlyEqual(TEXT("...the middle"), Heights[1], 25.0, 1e-6);
		TestNearlyEqual(TEXT("...and the bottom, carrying the remainder"), Heights[2], 31.1, 1e-6);
	}
	{
		TArray<double> Heights;
		if (!TestTrue(TEXT("A 72 cm bank takes four drawers"),
			FHFJoineryKit::GraduateDrawerFronts(MakeBankParams(72.0, 4), Heights)))
		{
			return false;
		}
		TestNearlyEqual(TEXT("A 72 cm bank of four is 15 / 15 / 20 / 20.8: the top"), Heights[0], 15.0, 1e-6);
		TestNearlyEqual(TEXT("...the second"), Heights[1], 15.0, 1e-6);
		TestNearlyEqual(TEXT("...the third"), Heights[2], 20.0, 1e-6);
		TestNearlyEqual(TEXT("...and the bottom"), Heights[3], 20.8, 1e-6);
	}

	// The defect: all unallocated height used to land on the bottom front with no bound at all, so a
	// 2 m bank of three came back 45 / 50 / 104.1 and reported success. A 1 m drawer front is not a
	// thing any kitchen or wardrobe in a 2BHK contains.
	//
	// Swept across every bank height and count that could arise, and asserted as a property rather
	// than a table: whatever comes back must be fronts somebody could cut.
	int32 Refused = 0;
	int32 Graduated = 0;

	for (int32 Count = 1; Count <= 6; ++Count)
	{
		for (double BankHeight = 20.0; BankHeight <= 220.0; BankHeight += 2.5)
		{
			TArray<double> Heights;
			if (!FHFJoineryKit::GraduateDrawerFronts(MakeBankParams(BankHeight, Count), Heights))
			{
				++Refused;
				continue;
			}
			++Graduated;

			const FString Where = FString::Printf(TEXT("%d fronts in a %.1f cm bank"), Count, BankHeight);

			if (!TestEqual(*FString::Printf(TEXT("%s: one height per drawer"), *Where), Heights.Num(), Count))
			{
				return false;
			}

			// Sums exactly, with one reveal per front. That is what keeps a bank's shadow lines in
			// step with the shutters beside it.
			TestNearlyEqual(*FString::Printf(TEXT("%s: fills the bank exactly"), *Where),
				SumOf(Heights) + Count * Reveal, BankHeight, 1e-6);

			for (int32 Index = 0; Index < Count; ++Index)
			{
				if (Index > 0 && Heights[Index] < Heights[Index - 1] - 1e-9)
				{
					AddError(FString::Printf(
						TEXT("%s: front %d is shallower than the one above it, which reads as a mistake."),
						*Where, Index));
					return false;
				}

				// Every front is a rung, or a rung plus the sub-rung remainder the bottom one
				// absorbs. Never a size nobody cuts.
				double Nearest = FrontLadder()[0];
				for (const double Ladder : FrontLadder())
				{
					if (FMath::Abs(Heights[Index] - Ladder) < FMath::Abs(Heights[Index] - Nearest))
					{
						Nearest = Ladder;
					}
				}

				const double OffLadder = Heights[Index] - Nearest;
				if (Heights[Index] > DeepestFront + Rung + 1e-9 || OffLadder < -1e-9 - Rung)
				{
					AddError(FString::Printf(
						TEXT("%s: front %d came out %.2f cm, which is not a front anybody cuts."),
						*Where, Index, Heights[Index]));
					return false;
				}
			}
		}
	}

	TestTrue(TEXT("The sweep graduated real banks"), Graduated > 50);
	TestTrue(TEXT("And refused the ones that cannot be built out of fronts"), Refused > 0);

	// Named cases, so the refusal is a decision rather than a side effect.
	{
		TArray<double> Heights;
		TestFalse(TEXT("A 2 m bank of three is refused, not built out of metre-tall fronts"),
			FHFJoineryKit::GraduateDrawerFronts(MakeBankParams(200.0, 3), Heights));
		TestFalse(TEXT("A 110 cm bank of two is refused too"),
			FHFJoineryKit::GraduateDrawerFronts(MakeBankParams(110.0, 2), Heights));

		// And the same height WITH enough drawers in it builds, which is what the refusal is telling
		// the caller to do.
		TestTrue(TEXT("A 2 m bank of five is a bank"),
			FHFJoineryKit::GraduateDrawerFronts(MakeBankParams(200.0, 5), Heights));
	}

	return true;
}

/**
 * The runner is what a drawer is built to, so a length that is not a runner cannot be honoured.
 *
 * MakeDrawerLayout guarded the box's width and nothing else, and SanitiseDrawer honoured any
 * positive RunnerLength that would fit. Between them, a 10 mm "runner" produced a box whose front
 * and back boards passed through each other and whose back board broke out through the carcass front
 * plane into the applied front - reported as a valid drawer, with the overlap counted twice.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFDrawerParametersTest, "HouseForge.Joinery.DrawerParameters", HF_TEST_FLAGS)

bool FHFDrawerParametersTest::RunTest(const FString& Parameters)
{
	const FHFDrawerParams Base = MakeDrawerParams();

	// A 580 carcass takes a 500 runner, which is what a kitchen base unit is actually built with.
	TestNearlyEqual(TEXT("A 580 carcass selects a 500 runner"),
		FHFJoineryKit::SanitiseDrawer(Base).RunnerLength, 50.0, 1e-9);

	{
		// An explicit length that is real hardware and fits is a decision, and is kept.
		FHFDrawerParams Explicit = Base;
		Explicit.RunnerLength = 45.0;
		TestNearlyEqual(TEXT("An explicit 450 runner is honoured"),
			FHFJoineryKit::SanitiseDrawer(Explicit).RunnerLength, 45.0, 1e-9);
	}
	{
		// Longer than the carcass can take: dropped to the longest that fits, as documented.
		FHFDrawerParams TooLong = Base;
		TooLong.RunnerLength = 80.0;
		TestNearlyEqual(TEXT("A runner longer than the carcass drops to the longest that fits"),
			FHFJoineryKit::SanitiseDrawer(TooLong).RunnerLength, 50.0, 1e-9);
	}
	{
		// Shorter than any runner made. Not a decision, and not buildable.
		FHFDrawerParams Absurd = Base;
		Absurd.RunnerLength = 1.0;

		const FHFDrawerParams Fitted = FHFJoineryKit::SanitiseDrawer(Absurd);
		TestNearlyEqual(TEXT("A length shorter than the shortest runner made is not honoured"),
			Fitted.RunnerLength, 50.0, 1e-9);
		TestTrue(TEXT("The fitted runner is at least the shortest one made"), Fitted.RunnerLength >= 25.0);

		// And the box that comes out of it is a real box: front and back boards clear of each other,
		// and nothing reaching in front of the carcass except the applied front.
		const FDynamicMesh3 Drawer = FHFJoineryKit::GenerateDrawer(Absurd);
		if (!TestTrue(TEXT("It still builds a drawer"), Drawer.TriangleCount() > 0))
		{
			return false;
		}
		TestTrue(TEXT("That drawer is watertight"), FHFMeshOps::IsClosed(Drawer));

		const FAxisAlignedBox3d Bounds = Drawer.GetBounds();

		// The deepest thing on a drawer is its own rail, which runs the full nominal length of the
		// runner from its front setback.
		TestNearlyEqual(TEXT("It reaches back exactly the runner it was fitted with"),
			Bounds.Max.Y, 0.3 + Fitted.RunnerLength, 1e-6);
		TestNearlyEqual(TEXT("And its box is that runner deep"),
			FHFJoineryKit::DrawerBoxDepth(Absurd), Fitted.RunnerLength, 1e-6);
		TestNearlyEqual(TEXT("And nothing stands in front of the carcass but the front"),
			Bounds.Min.Y, -(Base.BackClearance + Base.FrontThickness), 1e-6);

		// The volume the boards really occupy. An overlapping front and back board would report more
		// than the drawer contains, which is the half of this defect no bound would catch.
		TestTrue(TEXT("The drawer holds less board than its own bounding box"),
			Volume(Drawer) < Bounds.Width() * Bounds.Height() * Bounds.Depth());
	}
	{
		// A module too short to take a box with a floor is not a drawer. Silently dropping the
		// bottom board gave back a drawer box with no floor, and reported it as built.
		FHFDrawerParams Shallow = Base;
		Shallow.ModuleHeight = 1.0;

		TestEqual(TEXT("A module too short for a box with a floor produces no drawer"),
			FHFJoineryKit::GenerateDrawer(Shallow).TriangleCount(), 0);
		TestNearlyEqual(TEXT("And no travel to go with it"),
			FHFJoineryKit::DrawerTravel(Shallow), 0.0, 1e-9);
	}

	return true;
}

#undef HF_TEST_FLAGS

#endif // WITH_DEV_AUTOMATION_TESTS
