// Copyright Siddartha G. All Rights Reserved.

#include "HouseForge.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Geometry/HFCurtainKit.h"
#include "Geometry/HFMeshOps.h"
#include "Geometry/HFWallPlateKit.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "DynamicMesh/MeshTransforms.h"
#include "Misc/AutomationTest.h"
#include "Model/HFArticulation.h"

using namespace UE::Geometry;

#define HF_TEST_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// ---------------------------------------------------------------------------------------------
//
// Curtains, on the bench.
//
// EVERYTHING HERE IS MEASURED AS AN APERTURE IN CENTIMETRES, and that is not a stylistic preference.
// The master bedroom's 2400 sliding wardrobe passed every assertion anybody thought to write - both
// leaves travelled their full 118.45 cm, every part reported a real slide, every motion resolved -
// and the wardrobe never opened by a millimetre, because the two leaves exchanged tracks. A curtain
// is the same shape of object and invites the same mistake: two leaves, one control, one opening. So
// nothing below asks whether a fold moved. It asks how much of the window a person can see through,
// sampled off the posed geometry, and it asks it at both ends of the travel.
//
// The second thing measured is FULLNESS, because fullness is what separates a curtain from a blind.
// A flat panel that slides passes every "does it open" test ever written. The arc length of the
// built fold is what says there is cloth in it.
//
// ---------------------------------------------------------------------------------------------

namespace HouseForgeCurtain
{
	/**
	 * The reference flat's living-room curtain: a pair in a 2200 pelmet over a 1500 window.
	 *
	 * The pelmet is the real one, so MaxFoldDepth is whatever the drawn 180 mm box actually leaves
	 * once the track is in it, rather than a figure chosen to make the test pass.
	 */
	FHFPelmetParams ReferencePelmet()
	{
		FHFPelmetParams P;
		P.Width = 220.0;
		P.Depth = 18.0;
		P.Height = 20.0;
		P.BoardThickness = 1.8;
		return FHFWallPlateKit::SanitisePelmet(P);
	}

	FHFCurtainParams ReferenceCurtain()
	{
		const FHFPelmetParams Pelmet = ReferencePelmet();

		FHFCurtainParams C;
		C.TrackWidth = Pelmet.ClearWidth();
		C.Drop = 235.0;
		C.Draw = EHFCurtainDraw::Pair;
		C.Heading = EHFCurtainHeading::PinchPleat;
		C.MaxFoldDepth = Pelmet.ConcealedCurtainDepth();
		return FHFCurtainKit::Sanitise(C);
	}

	/** One part's mesh, posed at an open amount and put into the curtain's own frame. */
	FDynamicMesh3 Posed(const FHFMeshPart& Part, double OpenAmount)
	{
		FHFPartState State;
		State.PartId = Part.PartId;
		State.PivotTransform = Part.PivotTransform;
		State.Motion = Part.Motion;
		State.OpenAmount = OpenAmount;

		FDynamicMesh3 Mesh = Part.Mesh;
		MeshTransforms::ApplyTransform(Mesh, FTransformSRT3d(State.PoseAt(OpenAmount)), true);
		return Mesh;
	}

	/** The whole curtain at one open amount: anchors, which never move, plus every posed fold. */
	FDynamicMesh3 WholeCurtainAt(const FHFCurtainBuild& Build, double OpenAmount)
	{
		FDynamicMesh3 Out;
		FHFMeshOps::InitialiseMesh(Out);
		FHFMeshOps::AppendPreservingRoles(Out, Build.Anchors);

		for (const FHFMeshPart& Part : Build.Parts)
		{
			const FDynamicMesh3 Mesh = Posed(Part, OpenAmount);
			FHFMeshOps::AppendPreservingRoles(Out, Mesh);
		}
		return Out;
	}

	/**
	 * The widest unbroken run of track, in centimetres, with no cloth in front of it. THE APERTURE.
	 *
	 * Measured off the built geometry by occupancy along X, at a height well down the drop where a
	 * person actually looks through the window - not off the parameters, and not off bounding boxes
	 * of whole leaves. A leaf's bounding box is useless here: the folds of a drawn-back leaf overlap
	 * one another, so the union of their extents is the only honest answer and it has to be sampled.
	 *
	 * WIDEST UNBROKEN RUN rather than total uncovered, because those are different failures. A
	 * curtain that gathered into eight separate clumps with gaps between them would report a large
	 * uncovered fraction and would still be a curtain nobody can see through.
	 */
	double ApertureCm(const FHFCurtainBuild& Build, double OpenAmount, double AtZ, double& OutCentreX)
	{
		const FHFCurtainParams& P = Build.Used;
		const double Half = P.ClearWidth() * 0.5;

		constexpr int32 Samples = 1200;
		const double Cell = P.ClearWidth() / Samples;

		TArray<bool> Covered;
		Covered.Init(false, Samples);

		auto MarkMesh = [&Covered, Half, Cell, AtZ](const FDynamicMesh3& Mesh)
		{
			for (const int32 Tid : Mesh.TriangleIndicesItr())
			{
				FVector3d A, B, C;
				Mesh.GetTriVertices(Tid, A, B, C);

				const double MinZ = FMath::Min3(A.Z, B.Z, C.Z);
				const double MaxZ = FMath::Max3(A.Z, B.Z, C.Z);
				if (AtZ < MinZ || AtZ > MaxZ)
				{
					continue;
				}

				const double MinX = FMath::Min3(A.X, B.X, C.X);
				const double MaxX = FMath::Max3(A.X, B.X, C.X);

				const int32 First = FMath::Clamp(FMath::FloorToInt32((MinX + Half) / Cell), 0, Samples - 1);
				const int32 Last = FMath::Clamp(FMath::CeilToInt32((MaxX + Half) / Cell), 0, Samples - 1);
				for (int32 i = First; i <= Last; ++i)
				{
					Covered[i] = true;
				}
			}
		};

		MarkMesh(Build.Anchors);
		for (const FHFMeshPart& Part : Build.Parts)
		{
			MarkMesh(Posed(Part, OpenAmount));
		}

		int32 Best = 0;
		int32 BestStart = 0;
		int32 Run = 0;
		for (int32 i = 0; i < Samples; ++i)
		{
			Run = Covered[i] ? 0 : Run + 1;
			if (Run > Best)
			{
				Best = Run;
				BestStart = i - Run + 1;
			}
		}

		OutCentreX = Best > 0 ? (BestStart + Best * 0.5) * Cell - Half : 0.0;
		return Best * Cell;
	}
}

using namespace HouseForgeCurtain;

/**
 * The arithmetic that ties fullness, repeat and depth together.
 *
 * These three cannot be chosen independently and every later claim rests on that. A curtain is as
 * deep as its own spare fabric makes it, so if this is wrong the fold depths are wrong, the pelmet
 * check is wrong, and a curtain that "fits" stands through its own fascia.
 *
 * Checked against the closed form rather than against itself: the arc length of one wavelength of
 * y = (H/2) sin(2 pi x / L) is 2 L / pi * sqrt(1 + k^2) * E(k / sqrt(1 + k^2)) with k = pi H / L, and
 * at k = 3 that gives a fullness of 2.224. Any change that quietly broke the integral would have to
 * break it to exactly that value at exactly that point to get past this.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFCurtainFullnessArithmeticTest,
	"HouseForge.Curtain.FullnessArithmetic", HF_TEST_FLAGS)

bool FHFCurtainFullnessArithmeticTest::RunTest(const FString& Parameters)
{
	// Flat cloth is flat: no depth, no spare fabric, no folds.
	TestNearlyEqual(TEXT("A fold with no depth has no fullness"),
		FHFCurtainKit::FullnessFor(14.0, 0.0), 1.0, 1e-9);

	// k = pi H / L = 3 exactly, whose fullness is a published elliptic value.
	{
		const double Pitch = 14.0;
		const double Depth = 3.0 * Pitch / UE_DOUBLE_PI;
		TestNearlyEqual(*FString::Printf(TEXT("k=3 gives 2.224 fullness (got %.4f)"),
			FHFCurtainKit::FullnessFor(Pitch, Depth)),
			FHFCurtainKit::FullnessFor(Pitch, Depth), 2.2241, 1e-3);
	}

	// Scale invariance: fullness is a shape, so the same aspect ratio at any size is the same figure.
	TestNearlyEqual(TEXT("Fullness depends on the ratio, not the size"),
		FHFCurtainKit::FullnessFor(28.0, 24.0), FHFCurtainKit::FullnessFor(14.0, 12.0), 1e-9);

	// And the inverse really inverts, over the whole range a curtain is ever made at.
	for (const double Target : { 1.5, 1.8, 2.0, 2.2, 2.5, 3.0 })
	{
		const double Depth = FHFCurtainKit::DepthForFullness(14.0, Target);
		TestNearlyEqual(*FString::Printf(TEXT("%.1f fullness round-trips through its own depth"), Target),
			FHFCurtainKit::FullnessFor(14.0, Depth), Target, 1e-6);
	}

	// THE FIGURE THE WHOLE KIT IS SIZED ON. A 140 mm repeat at 2.0 fullness is a 116 mm deep curtain,
	// which is what has to fit behind a 180 mm pelmet. Stated as a test so nobody can move the
	// default fullness or repeat without seeing what it does to the depth.
	const double Reference = FHFCurtainKit::DepthForFullness(14.0, 2.0);
	TestTrue(*FString::Printf(TEXT("A 140 mm repeat at 2.0 fullness hangs 116 mm deep (got %.1f mm)"),
		Reference * 10.0), FMath::IsNearlyEqual(Reference, 11.6, 0.15));

	return true;
}

/**
 * A curtain has cloth in it. Fullness, measured off the built mesh rather than off the parameter.
 *
 * A FLAT PANEL THAT SLIDES PASSES EVERY OTHER TEST IN THIS FILE. It covers its opening at 0, it
 * clears it at 1, it never fouls anything and it is not a curtain. The only thing that separates the
 * two is that a curtain is longer than the track it hangs on, so that length is measured directly:
 * the plan outline of one fold at mid-drop, walked as a polyline, against the width of track that
 * fold occupies.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFCurtainHasFabricInItTest,
	"HouseForge.Curtain.HasFabricInIt", HF_TEST_FLAGS)

bool FHFCurtainHasFabricInItTest::RunTest(const FString& Parameters)
{
	const FHFCurtainParams P = ReferenceCurtain();
	const FHFCurtainBuild Build = FHFCurtainKit::Build(P);

	if (!TestTrue(TEXT("The curtain builds"), Build.bValid))
	{
		return false;
	}

	TestTrue(*FString::Printf(TEXT("It is folded, not panelled: %d folds a leaf at %.1f cm"),
		Build.Used.FoldsPerLeaf(), Build.Used.BuiltFoldPitch()), Build.Used.FoldsPerLeaf() >= 5);

	// The cloth's own length, off the mesh. Sampled as the extreme Y at each of many stations along
	// one fold and walked as a polyline: a flat panel gives a path exactly as long as its own width,
	// and 2.0 fullness gives one twice as long.
	const FHFMeshPart& Fold = Build.Parts[1];
	const FAxisAlignedBox3d FoldBounds = Fold.Mesh.GetBounds();

	const double MidZ = FoldBounds.Min.Z * 0.5;
	constexpr int32 Stations = 200;

	TArray<double> FrontY;
	FrontY.Init(-BIG_NUMBER, Stations);
	const double Span = FoldBounds.Max.X - FoldBounds.Min.X;

	for (const int32 Vid : Fold.Mesh.VertexIndicesItr())
	{
		const FVector3d V = Fold.Mesh.GetVertex(Vid);
		if (FMath::Abs(V.Z - MidZ) > FoldBounds.Depth() * 0.4)
		{
			continue;
		}
		const int32 Station = FMath::Clamp(
			FMath::FloorToInt32((V.X - FoldBounds.Min.X) / Span * Stations), 0, Stations - 1);
		FrontY[Station] = FMath::Max(FrontY[Station], V.Y);
	}

	double PathLength = 0.0;
	double PreviousX = 0.0;
	double PreviousY = 0.0;
	bool bHavePrevious = false;

	for (int32 Station = 0; Station < Stations; ++Station)
	{
		if (FrontY[Station] <= -BIG_NUMBER * 0.5)
		{
			continue;
		}
		const double X = FoldBounds.Min.X + (Station + 0.5) * Span / Stations;
		if (bHavePrevious)
		{
			PathLength += FMath::Sqrt(FMath::Square(X - PreviousX) + FMath::Square(FrontY[Station] - PreviousY));
		}
		PreviousX = X;
		PreviousY = FrontY[Station];
		bHavePrevious = true;
	}

	const double MeasuredFullness = PathLength / Span;

	// Against the fullness the parameters claim, not against a hopeful floor. Loose on the tolerance
	// because the path is sampled off a faceted surface and the outer face of the cloth runs slightly
	// longer than the centreline it was offset from - both of which only ever read HIGH.
	TestTrue(*FString::Printf(TEXT("The built fold carries %.2f of fabric per unit of track, against %.2f asked for"),
		MeasuredFullness, Build.Used.AchievedFullness()),
		MeasuredFullness > Build.Used.AchievedFullness() * 0.9);

	TestTrue(TEXT("...which is a curtain rather than a blind"), MeasuredFullness > 1.6);

	// And the depth it needed to carry that is the depth it was given, plus the cloth wrapped round
	// the outside of it. Bracketed rather than equated, because FoldDepth is the CENTRELINE and the
	// mesh is the centreline plus half the thickness on each side - which is small, real, and is the
	// term a pelmet budget has to allow for. See FHFCurtainParams::ClothReachAllowance.
	const double Depth = FoldBounds.Max.Y - FoldBounds.Min.Y;
	TestTrue(*FString::Printf(TEXT("The fold hangs %.3f cm deep, of %.3f centreline plus its cloth"),
		Depth, Build.Used.FoldDepth()),
		Depth >= Build.Used.FoldDepth() - 0.01
		&& Depth <= Build.Used.FoldDepth() + 2.0 * Build.Used.ClothReachAllowance() + 0.01);

	return true;
}

/**
 * IT DRAWS. Shut it covers the window; open it clears it. Both in centimetres of visible aperture.
 *
 * The one test this whole milestone exists to pass, and it is written the way the wardrobe's is -
 * off the posed geometry, as the widest run of track a person can see through - because the wardrobe
 * is what proved that every other way of asking is answerable by a fixture that does nothing.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFCurtainDrawsTest, "HouseForge.Curtain.Draws", HF_TEST_FLAGS)

bool FHFCurtainDrawsTest::RunTest(const FString& Parameters)
{
	const FHFCurtainParams P = ReferenceCurtain();
	const FHFCurtainBuild Build = FHFCurtainKit::Build(P);

	if (!TestTrue(TEXT("The curtain builds"), Build.bValid))
	{
		return false;
	}

	// Two thirds of the way down the drop: below the pelmet, below the sill, in the middle of the
	// glass. Where a person looks through a window.
	const double AtZ = -Build.Used.Drop * 0.66;

	double CentreX = 0.0;

	const double Shut = ApertureCm(Build, 0.0, AtZ, CentreX);
	TestTrue(*FString::Printf(TEXT("Shut, the widest gap anywhere across the run is %.2f cm"), Shut),
		Shut < 1.0);

	const double Open = ApertureCm(Build, 1.0, AtZ, CentreX);

	// AGAINST THE WINDOW, NOT AGAINST A FRACTION. The pelmet is 2200 over a 1500 window, which is
	// exactly the stack allowance a designer adds so the drawn curtain clears the glass, and the
	// point of the whole exercise is that the arithmetic delivers it.
	constexpr double WindowWidth = 150.0;
	TestTrue(*FString::Printf(TEXT("Drawn open it clears the 150 cm window: %.1f cm of aperture"), Open),
		Open > WindowWidth);

	// AND THE APERTURE IS IN THE MIDDLE, which is the half of the claim a pair can fail on its own.
	// Two leaves stacking to the SAME end would open just as much track and would leave the window
	// covered by one of them - the cancelling pose, one step sideways.
	TestTrue(*FString::Printf(TEXT("...and it is centred on the opening (at %.1f cm off centre)"), CentreX),
		FMath::Abs(CentreX) < 5.0);

	// Halfway is halfway: monotone, so scrubbing a Sequencer track is a curtain drawing rather than
	// a curtain jumping.
	double Ignored = 0.0;
	const double Half = ApertureCm(Build, 0.5, AtZ, Ignored);
	TestTrue(*FString::Printf(TEXT("Half open is between the two (%.1f cm)"), Half),
		Half > Shut + 1.0 && Half < Open - 1.0);

	// THE STACK IS WHERE IT SAID IT WOULD BE. OpenApertureWidth is what a pelmet gets sized against
	// before anything is built, so it has to agree with what the cloth actually does.
	TestTrue(*FString::Printf(TEXT("The predicted aperture (%.1f) matches the built one (%.1f)"),
		Build.Used.OpenApertureWidth(), Open),
		FMath::Abs(Build.Used.OpenApertureWidth() - Open) < 4.0);

	return true;
}

/**
 * A single-draw curtain, which is what a small window gets, and it stacks at the end it was told to.
 *
 * Worth its own test rather than a parameter sweep of the one above: the side is the whole content
 * of the choice. A single curtain that always stacked left would pass an aperture test on a
 * symmetric opening and would put the bundle over the bedroom-2 wardrobe every time.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFCurtainSingleDrawTest, "HouseForge.Curtain.SingleDraw", HF_TEST_FLAGS)

bool FHFCurtainSingleDrawTest::RunTest(const FString& Parameters)
{
	auto Measure = [this](EHFCurtainDraw Draw, double& OutAperture, double& OutCentre)
	{
		FHFCurtainParams P;
		P.TrackWidth = 131.4;
		P.Drop = 235.0;
		P.Draw = Draw;
		P.MaxFoldDepth = 13.0;

		const FHFCurtainBuild Build = FHFCurtainKit::Build(P);
		TestTrue(TEXT("The single curtain builds"), Build.bValid);
		TestEqual(TEXT("One leaf, not two"), Build.Used.LeafCount(), 1);

		OutAperture = ApertureCm(Build, 1.0, -Build.Used.Drop * 0.66, OutCentre);

		double Ignored = 0.0;
		TestTrue(TEXT("Shut, it covers its run"),
			ApertureCm(Build, 0.0, -Build.Used.Drop * 0.66, Ignored) < 1.0);
	};

	double LeftAperture = 0.0;
	double LeftCentre = 0.0;
	Measure(EHFCurtainDraw::SingleStackLeft, LeftAperture, LeftCentre);

	double RightAperture = 0.0;
	double RightCentre = 0.0;
	Measure(EHFCurtainDraw::SingleStackRight, RightAperture, RightCentre);

	// The 900 window in bedroom 2 is what this size is: it has to come clear of it.
	TestTrue(*FString::Printf(TEXT("A single draw clears a 90 cm window (%.1f cm)"), LeftAperture),
		LeftAperture > 90.0);

	TestTrue(*FString::Printf(TEXT("Stacking left leaves the aperture to the RIGHT (%.1f cm off centre)"),
		LeftCentre), LeftCentre > 5.0);
	TestTrue(*FString::Printf(TEXT("Stacking right leaves it to the LEFT (%.1f cm off centre)"),
		RightCentre), RightCentre < -5.0);

	TestTrue(TEXT("Both hands give the same aperture"),
		FMath::IsNearlyEqual(LeftAperture, RightAperture, 1.0));

	return true;
}

/**
 * The fabric stays inside the pelmet, drawn AND open.
 *
 * Two separate failures and the second is the one that hides. Hanging fabric is a shape the fold
 * depth decides and it is easy to check; the drawn-back BUNDLE is fanned in depth so it reads as a
 * rope of cloth rather than a flattened stack, and that fan comes out of the same 180 mm the fascia
 * and the plaster leave. A curtain that fitted its pelmet closed and burst through the fascia open
 * would look perfect in every still anybody took of it shut.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFCurtainFitsItsPelmetTest,
	"HouseForge.Curtain.FitsItsPelmet", HF_TEST_FLAGS)

bool FHFCurtainFitsItsPelmetTest::RunTest(const FString& Parameters)
{
	const FHFPelmetParams Pelmet = ReferencePelmet();
	const FHFCurtainParams P = ReferenceCurtain();
	const FHFCurtainBuild Build = FHFCurtainKit::Build(P);

	if (!TestTrue(TEXT("The curtain builds"), Build.bValid))
	{
		return false;
	}

	// The two surfaces the cloth has to stay between, in the PELMET's frame: the back of the fascia
	// and the plaster. Both are real faces of a real box - see FHFWallPlateKit::BuildPelmet.
	const double FasciaBackY = -Pelmet.Depth * 0.5 + Pelmet.BoardThickness;
	const double PlasterY = Pelmet.Depth * 0.5;
	const double TrackY = Pelmet.TrackCentreY();

	TestTrue(*FString::Printf(TEXT("The pelmet can hide a %.1f cm curtain"), Pelmet.ConcealedCurtainDepth()),
		Pelmet.ConcealedCurtainDepth() > 11.0);

	for (const double Amount : { 0.0, 0.25, 0.5, 0.75, 1.0 })
	{
		const FDynamicMesh3 Cloth = WholeCurtainAt(Build, Amount);
		const FAxisAlignedBox3d Bounds = Cloth.GetBounds();

		// The curtain hangs on the track, so its own frame is offset into the pelmet's by TrackY.
		const double FrontY = Bounds.Min.Y + TrackY;
		const double BackY = Bounds.Max.Y + TrackY;

		TestTrue(*FString::Printf(TEXT("At %.0f%% open the cloth clears the fascia by %.2f cm"),
			Amount * 100.0, FrontY - FasciaBackY), FrontY > FasciaBackY + 0.3);

		TestTrue(*FString::Printf(TEXT("At %.0f%% open the cloth clears the plaster by %.2f cm"),
			Amount * 100.0, PlasterY - BackY), BackY < PlasterY - 0.3);

		// And it never runs off the ends of its own track.
		TestTrue(*FString::Printf(TEXT("At %.0f%% open the cloth stays between the end stops"),
			Amount * 100.0),
			Bounds.Min.X > -P.TrackWidth * 0.5 - 0.01 && Bounds.Max.X < P.TrackWidth * 0.5 + 0.01);

		// The hem never rises: a curtain drawn back does not shorten.
		TestNearlyEqual(*FString::Printf(TEXT("At %.0f%% open the hem is still at the hem"), Amount * 100.0),
			Bounds.Min.Z, -P.Drop, 0.01);
	}

	// THE FAN IS INSIDE THE BUDGET IT WAS GIVEN, as arithmetic, so a pelmet check can be run without
	// building anything.
	TestTrue(*FString::Printf(TEXT("The bundle reaches %.2f cm either side of the track, of %.2f allowed"),
		Build.Used.StackedReach(), Build.Used.MaxFoldDepth * 0.5),
		Build.Used.StackedReach() <= Build.Used.MaxFoldDepth * 0.5 + 1e-6);

	return true;
}

/**
 * A pelmet too shallow for a curtain flattens it rather than being burst through.
 *
 * The reference flat's pelmets are drawn 180 deep and that is enough. A drawing that asked for 100
 * is not invalid - shallow pelmets exist - and what has to happen is that the cloth gives way and
 * says so, rather than the fascia acquiring a curtain through it.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFCurtainFlattensRatherThanBurstsTest,
	"HouseForge.Curtain.FlattensRatherThanBursts", HF_TEST_FLAGS)

bool FHFCurtainFlattensRatherThanBurstsTest::RunTest(const FString& Parameters)
{
	FHFCurtainParams P = ReferenceCurtain();
	P.MaxFoldDepth = 5.0;

	const FHFCurtainBuild Build = FHFCurtainKit::Build(P);
	if (!TestTrue(TEXT("A curtain in a shallow pelmet still builds"), Build.bValid))
	{
		return false;
	}

	TestTrue(*FString::Printf(TEXT("The fold gave way: %.2f cm deep of the 5.0 allowed"),
		Build.Used.FoldDepth()), Build.Used.StackedReach() <= 2.5 + 1e-6);

	// AND IT SAYS WHAT IT COST. A curtain flattened to fit is a curtain with less fabric showing,
	// and silently reporting the fullness that was asked for would be the lie.
	TestTrue(*FString::Printf(TEXT("It reports the fullness it actually delivers: %.2f, not the %.2f asked"),
		Build.Used.AchievedFullness(), Build.Used.Fullness),
		Build.Used.AchievedFullness() < Build.Used.Fullness - 0.1);

	const FDynamicMesh3 Cloth = WholeCurtainAt(Build, 1.0);
	TestTrue(TEXT("And the flattened bundle is inside the depth it was given"),
		Cloth.GetBounds().Height() <= 5.0 + 0.01);

	return true;
}

/**
 * Every fold is a moving part with a real pivot, and they move TOGETHER.
 *
 * The gearing is the mechanism, not bookkeeping: a leaf whose folds each carried their own amount
 * could be posed with fold five stacked and fold six across the window, which is cloth torn in half.
 * So every fold but the leading one is driven by it, and this measures that the resolve delivers it.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFCurtainFoldsAreGearedTest,
	"HouseForge.Curtain.FoldsAreGeared", HF_TEST_FLAGS)

bool FHFCurtainFoldsAreGearedTest::RunTest(const FString& Parameters)
{
	const FHFCurtainBuild Build = FHFCurtainKit::Build(ReferenceCurtain());
	if (!TestTrue(TEXT("The curtain builds"), Build.bValid))
	{
		return false;
	}

	const int32 Folds = Build.Used.FoldsPerLeaf();

	// Every travelling fold really travels, and the further from the stack end the further it goes.
	double PreviousTravel = -1.0;
	int32 InLeafZero = 0;

	for (const FHFMeshPart& Part : Build.Parts)
	{
		TestEqual(*FString::Printf(TEXT("'%s' slides"), *Part.PartId.ToString()),
			Part.Motion.Type, EHFMotionType::Slide);
		TestTrue(*FString::Printf(TEXT("'%s' has real travel (%.2f cm)"),
			*Part.PartId.ToString(), Part.Motion.MaxTravelCm), Part.Motion.MaxTravelCm > 0.5);

		if (Part.PartId == FHFCurtainKit::FoldPartId(0, InLeafZero + 1))
		{
			++InLeafZero;
			TestTrue(*FString::Printf(TEXT("Fold %d travels further than fold %d"),
				InLeafZero, InLeafZero - 1), Part.Motion.MaxTravelCm > PreviousTravel);
			PreviousTravel = Part.Motion.MaxTravelCm;
		}
	}

	// The leading fold of each leaf is the driver, and it is driven by nobody.
	for (int32 Leaf = 0; Leaf < Build.Used.LeafCount(); ++Leaf)
	{
		const FName LeadId = FHFCurtainKit::LeadFoldPartId(Leaf, Folds);

		const FHFMeshPart* Lead = Build.Parts.FindByPredicate(
			[&LeadId](const FHFMeshPart& Part) { return Part.PartId == LeadId; });

		if (!TestNotNull(*FString::Printf(TEXT("Leaf %d has a leading fold"), Leaf), Lead))
		{
			continue;
		}

		TestTrue(TEXT("...which is geared to nothing"), Lead->Motion.DrivenByPartId.IsNone());
		TestTrue(TEXT("...and which a single control opens"), Lead->Motion.bMasterOpens);

		// It travels nearly the whole leaf, less its own width and its stack. That is the number that
		// says the leaf gathers rather than shuffles.
		const double Expected = (Folds - 1) * (Build.Used.BuiltFoldPitch() - Build.Used.StackPitch());
		TestTrue(*FString::Printf(TEXT("The leading fold travels %.1f cm, of %.1f expected"),
			Lead->Motion.MaxTravelCm, Expected),
			Lead->Motion.MaxTravelCm > Expected * 0.98);
	}

	// AND THE RESOLVE ACTUALLY GEARS THEM. Asking one fold to open and letting the assembly settle
	// must leave the whole leaf where its driver is, not one fold out on its own.
	TArray<FHFPartState> States;
	for (const FHFMeshPart& Part : Build.Parts)
	{
		FHFPartState State;
		State.PartId = Part.PartId;
		State.Motion = Part.Motion;
		State.PivotTransform = Part.PivotTransform;
		State.OpenAmount = (Part.PartId == FHFCurtainKit::FoldPartId(0, 1)) ? 1.0 : 0.0;
		States.Add(State);
	}

	TestTrue(TEXT("The assembly resolves without a cycle"), FHFArticulation::ResolvePartAmounts(States));

	for (const FHFPartState& State : States)
	{
		if (State.PartId == FHFCurtainKit::FoldPartId(0, 1))
		{
			TestNearlyEqual(TEXT("A geared fold takes its driver's amount, not the one it was asked for"),
				State.OpenAmount, 0.0, 1e-9);
		}
	}

	return true;
}

/**
 * Every triangle is fabric, carries a UV and carries a normal.
 *
 * The three invisible failures. Untagged geometry cannot be re-materialled by the SURFACES panel; a
 * mesh with no UV element renders untextured at any tiling; a mesh with no normal element shades off
 * the constant normal and looks like a flat cut-out, which on a curtain is precisely the defect the
 * folds were built to avoid.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFCurtainIsDressedForRenderTest,
	"HouseForge.Curtain.IsDressedForRender", HF_TEST_FLAGS)

bool FHFCurtainIsDressedForRenderTest::RunTest(const FString& Parameters)
{
	const FHFCurtainBuild Build = FHFCurtainKit::Build(ReferenceCurtain());
	if (!TestTrue(TEXT("The curtain builds"), Build.bValid))
	{
		return false;
	}

	auto CheckMesh = [this](const FDynamicMesh3& Mesh, const TCHAR* What)
	{
		if (Mesh.TriangleCount() == 0)
		{
			return;
		}

		const TSet<EHFSurfaceRole> Roles = FHFMeshOps::RolesPresent(Mesh);
		TestEqual(*FString::Printf(TEXT("%s carries exactly one role"), What), Roles.Num(), 1);
		TestTrue(*FString::Printf(TEXT("%s is Fabric"), What), Roles.Contains(EHFSurfaceRole::Fabric));

		TestTrue(*FString::Printf(TEXT("%s has UVs"), What),
			Mesh.HasAttributes() && Mesh.Attributes()->NumUVLayers() > 0
			&& Mesh.Attributes()->GetUVLayer(0)->ElementCount() > 0);

		TestTrue(*FString::Printf(TEXT("%s has shading normals"), What),
			Mesh.HasAttributes() && Mesh.Attributes()->PrimaryNormals() != nullptr
			&& Mesh.Attributes()->PrimaryNormals()->ElementCount() > 0);

		// Each fold is its own closed solid, which is what lets the clash scan ask whether a point is
		// inside it. A cloth surface with no thickness answers that question meaninglessly.
		TestTrue(*FString::Printf(TEXT("%s is closed"), What), FHFMeshOps::IsClosed(Mesh));
	};

	CheckMesh(Build.Anchors, TEXT("The anchor folds"));
	for (const FHFMeshPart& Part : Build.Parts)
	{
		CheckMesh(Part.Mesh, *FString::Printf(TEXT("Fold '%s'"), *Part.PartId.ToString()));
	}

	return true;
}

/**
 * The folds do not share a plane with each other when the curtain is shut.
 *
 * Every fold is a separate solid, so consecutive folds present end faces to one another. Butted,
 * those are coincident surfaces - the flashing FHFCoplanarScan exists to catch - and the whole run
 * of a closed curtain would shimmer. FHFCurtainParams::FoldGap is what parts them, and this is the
 * measurement that says it is still doing so.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFCurtainFoldsDoNotTouchTest,
	"HouseForge.Curtain.FoldsDoNotTouch", HF_TEST_FLAGS)

bool FHFCurtainFoldsDoNotTouchTest::RunTest(const FString& Parameters)
{
	const FHFCurtainBuild Build = FHFCurtainKit::Build(ReferenceCurtain());
	if (!TestTrue(TEXT("The curtain builds"), Build.bValid))
	{
		return false;
	}

	const double Pitch = Build.Used.BuiltFoldPitch();
	const double Gap = Build.Used.FoldGap;

	TestTrue(*FString::Printf(TEXT("There is a hairline between folds (%.2f mm)"), Gap * 10.0),
		Gap > 0.02);

	// Measured on the built mesh: one fold's own extent along the track is its pitch less the gap,
	// EXACTLY. That exactness is the whole point of cutting the fold at its crest - see FoldSection.
	// The first build cut at the zero crossings instead, where the cloth's thickness projects along
	// the track, and every fold came out a full 13.535 cm repeat wide with the 1.5 mm gap in it.
	for (const FHFMeshPart& Part : Build.Parts)
	{
		const FAxisAlignedBox3d Bounds = Part.Mesh.GetBounds();
		TestNearlyEqual(*FString::Printf(TEXT("'%s' is one repeat less its hairline"),
			*Part.PartId.ToString()), Bounds.Width(), Pitch - Gap, 0.002);
	}

	// And therefore no two folds of a shut leaf occupy the same track, in the built geometry rather
	// than in the arithmetic. Compared as spans in either order, because a leaf that stacks at the
	// +X end numbers its folds the other way along the run.
	TArray<TPair<double, double>> Spans;
	for (const FHFMeshPart& Part : Build.Parts)
	{
		const FAxisAlignedBox3d Bounds = Part.Mesh.GetBounds();
		const double Offset = Part.PivotTransform.GetLocation().X;
		Spans.Add({ Offset + Bounds.Min.X, Offset + Bounds.Max.X });
	}
	Spans.Sort([](const TPair<double, double>& A, const TPair<double, double>& B)
		{ return A.Key < B.Key; });

	for (int32 Index = 1; Index < Spans.Num(); ++Index)
	{
		const double Between = Spans[Index].Key - Spans[Index - 1].Value;
		TestTrue(*FString::Printf(TEXT("Fold %d of the shut run is parted from its neighbour by %.3f cm"),
			Index, Between), Between > Gap * 0.9);
	}

	// And shut, the leaf still reads as continuous cloth: the widest gap anywhere is the hairline,
	// which is what HouseForge.Curtain.Draws asserts from the other side.
	double Ignored = 0.0;
	const double WidestGap = ApertureCm(Build, 0.0, -Build.Used.Drop * 0.66, Ignored);
	TestTrue(*FString::Printf(TEXT("The widest gap in a shut curtain is %.2f mm"), WidestGap * 10.0),
		WidestGap < FMath::Max(Gap * 3.0, 0.5));

	return true;
}

#undef HF_TEST_FLAGS

#endif	// WITH_DEV_AUTOMATION_TESTS
