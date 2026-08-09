// Copyright Siddartha G. All Rights Reserved.

#include "Geometry/HFCurtainKit.h"

#include "DynamicMesh/MeshTransforms.h"
#include "Geometry/HFMeshOps.h"

using namespace UE::Geometry;

namespace
{
	/**
	 * Steps the arc-length integral is evaluated over, per fold.
	 *
	 * Simpson's rule on a smooth periodic integrand converges very fast; 64 panels is exact to about
	 * one part in 10^9 here, which is far below anything the fabric cares about and cheap enough to
	 * run inside a bisection.
	 */
	constexpr int32 ArcLengthSteps = 64;

	/** Bisection steps for the inverse. 60 halvings takes any sane bracket below a micrometre. */
	constexpr int32 InverseSteps = 60;

	/** Height sections a fold's skin is lofted through, top to bottom. */
	struct FFoldLevel
	{
		/** Height below the glider line, as a fraction of the drop. 0 is the heading, 1 the hem. */
		double DropFraction = 0.0;

		/** Fold amplitude here, as a fraction of the full reach. */
		double AmplitudeFactor = 1.0;

		/** Cloth thickness here, as a multiple of the single-layer thickness. */
		double ThicknessFactor = 1.0;
	};

	/**
	 * Where a curtain's section changes, and by how much.
	 *
	 * A CURTAIN IS NOT AN EXTRUSION AND MODELLING IT AS ONE IS MOST OF WHY FABRIC LOOKS LIKE PLASTIC.
	 * Three things vary down the drop and all three are visible from across a room:
	 *
	 *   - the HEADING is pinched. The fabric is sewn to a stiff tape and hooked to gliders on the
	 *     track line, so the fold is at its shallowest at the very top and reaches full depth a
	 *     heading's height below it. Modelled as an amplitude ramp rather than as separate geometry,
	 *     because the whole heading is inside the pelmet and the only part of it the room sees is
	 *     the shape the fold has arrived at by the time it comes out from under the fascia.
	 *   - the heading is also THICK: tape, buckram and the doubled cloth of the pleat, two to three
	 *     times a single layer.
	 *   - the HEM is thick too, and for the same reason - it is a double turn-up, usually weighted -
	 *     which is what makes a curtain hang straight instead of flapping at the bottom.
	 *
	 * Between them the fabric is one layer at full fold. Listed top-down; the loft wants them the
	 * other way up and reverses them.
	 */
	const TArray<FFoldLevel>& FoldLevels()
	{
		static const TArray<FFoldLevel> Levels = {
			// The heading's factor is FHFCurtainParams::MaxThicknessFactor itself rather than a copy
			// of it, because every depth budget in the header allows for exactly that figure and two
			// of them drifting apart would be a curtain that fits on paper and not in the pelmet.
			{ 0.00, 0.55, FHFCurtainParams::MaxThicknessFactor },
			{ 0.03, 0.82, 2.0 },	// out of the tape, opening up
			{ 0.08, 1.00, 1.0 },	// full fold, one layer - and still above the fascia's bottom edge
			{ 0.45, 1.00, 1.0 },
			{ 0.94, 1.00, 1.0 },
			{ 0.97, 1.00, 1.9 },	// into the turn-up
			{ 1.00, 1.00, 1.9 }		// the hem
		};
		return Levels;
	}

	/**
	 * Angles the rounded end of a fold's section is drawn at, from the front edge round to the back.
	 *
	 * THE ZERO IS NOT A SPARE POINT. It is the one that lands on the cap's own extreme, so the fold's
	 * extent along the track is exactly the inset plus HalfT and the hairline between folds is exactly
	 * the gap. Drawn at 30 and -30 alone the widest point of the arc is HalfT cos(30), the fold comes
	 * out 0.08 mm short of its repeat, and the gap it leaves is that much too wide - measurable,
	 * harmless, and the sort of drift that makes a figure untestable.
	 */
	constexpr double EndCapAngles[3] = { 45.0, 0.0, -45.0 };

	/**
	 * One fold's plan section at a given amplitude and thickness: a closed loop round a cosine arc.
	 *
	 * The centreline is y = A cos(2 pi x / L) over one whole repeat, so the fabric snakes THROUGH the
	 * track line rather than bulging to one side of it. That is what a real curtain does - the cloth
	 * passes the gliders alternately in front of and behind the track - and it is also what makes the
	 * fold fit a pelmet: a one-sided bow of the same fullness would need the whole slot depth on one
	 * side of the track and would stand through the fascia.
	 *
	 * ONE FOLD IS ONE WHOLE REPEAT, so every fold of a leaf is the same object and the leaf itself is
	 * one continuous wave: the curve leaves a fold at the same depth and the same slope as it enters
	 * the next.
	 *
	 * IT IS CUT AT THE CRESTS, and that is a cosine rather than a sine for one reason. Each fold is
	 * its own solid, so consecutive folds present end faces to one another and have to be parted by a
	 * hairline or the two coincident planes flash - the defect FHFCoplanarScan exists for. Cut at the
	 * ZERO CROSSINGS, where the curve runs at 68 degrees to the track, the cloth's own thickness
	 * projects further along the track than a 1.5 mm gap is wide and the folds overlap anyway: the
	 * first build of this measured 13.535 cm of fold in a 13.535 cm repeat, gap and all. Cut at the
	 * crest the tangent is along the track, the cap projects exactly HalfT, and insetting the sample
	 * span by that much leaves the fold exactly one repeat less the gap wide - which is a figure a
	 * test can assert to the hundredth of a millimetre.
	 *
	 * It is also where a curtain is really seamed. Fabric comes 1.4 m wide and a 2.6 m leaf is two
	 * widths joined; a curtain-maker puts that seam in the back of a fold, never on the face.
	 *
	 * @param Reach   Half the peak-to-peak depth, before the amplitude factor.
	 * @param HalfT   Half the cloth thickness, after the thickness factor.
	 * @param Gap     Hairline between this fold and its neighbours, total.
	 */
	TArray<FVector2D> FoldSection(double Pitch, double Gap, double Reach, double HalfT, int32 Segments)
	{
		const int32 Count = FMath::Max(Segments, 4);
		const double Omega = UE_DOUBLE_TWO_PI / FMath::Max(Pitch, UE_DOUBLE_KINDA_SMALL_NUMBER);

		// Inset by the cap's own projection so that, once the ends are rounded, the fold occupies
		// exactly [Gap/2, Pitch - Gap/2] however thick this level's cloth is.
		const double Start = FMath::Min(Gap * 0.5 + HalfT, Pitch * 0.5 - UE_DOUBLE_KINDA_SMALL_NUMBER);
		const double End = FMath::Max(Pitch - Gap * 0.5 - HalfT, Pitch * 0.5 + UE_DOUBLE_KINDA_SMALL_NUMBER);

		TArray<FVector2D> Centre;
		TArray<FVector2D> Normal;
		Centre.Reserve(Count + 1);
		Normal.Reserve(Count + 1);

		for (int32 Index = 0; Index <= Count; ++Index)
		{
			const double X = FMath::Lerp(Start, End, double(Index) / double(Count));
			const double Y = Reach * FMath::Cos(Omega * X);

			// The tangent of the analytic curve, not a chord: a chord normal at the crest of a coarse
			// cosine is visibly off, and the offset surface it produces pinches there.
			const FVector2D Tangent = FVector2D(1.0, -Reach * Omega * FMath::Sin(Omega * X)).GetSafeNormal();

			Centre.Add(FVector2D(X, Y));
			Normal.Add(FVector2D(-Tangent.Y, Tangent.X));
		}

		TArray<FVector2D> Loop;
		Loop.Reserve(2 * Count + 6);

		// Front face, start to end.
		for (int32 Index = 0; Index <= Count; ++Index)
		{
			Loop.Add(Centre[Index] + Normal[Index] * HalfT);
		}

		// Round the far end. A 1.2 mm edge left square is still an arris, and .claude/rules says
		// there are none - see the edge-quality bar. Two facets is all a millimetre needs.
		{
			const FVector2D N = Normal[Count];
			const FVector2D T(N.Y, -N.X);
			for (const double Degrees : EndCapAngles)
			{
				const double R = FMath::DegreesToRadians(Degrees);
				Loop.Add(Centre[Count] + (N * FMath::Sin(R) + T * FMath::Cos(R)) * HalfT);
			}
		}

		// Back face, end to start.
		for (int32 Index = Count; Index >= 0; --Index)
		{
			Loop.Add(Centre[Index] - Normal[Index] * HalfT);
		}

		// Round the near end, the other way about.
		{
			const FVector2D N = Normal[0];
			const FVector2D T(-N.Y, N.X);
			for (int32 i = UE_ARRAY_COUNT(EndCapAngles) - 1; i >= 0; --i)
			{
				const double R = FMath::DegreesToRadians(EndCapAngles[i]);
				Loop.Add(Centre[0] + (N * FMath::Sin(R) + T * FMath::Cos(R)) * HalfT);
			}
		}

		return Loop;
	}

	/**
	 * One fold as a closed solid, in its own local space with the origin at its near end on the
	 * track line, hanging into -Z.
	 */
	bool AppendFold(FDynamicMesh3& Mesh, const FHFCurtainParams& P, double Pitch)
	{
		const double Gap = FMath::Clamp(P.FoldGap, 0.0, Pitch * 0.5);
		const double HalfT = FMath::Max(P.FabricThickness, 0.001) * 0.5;
		const double Reach = P.FoldReach();

		const TArray<FFoldLevel>& Levels = FoldLevels();

		TArray<TArray<FVector2D>> Sections;
		TArray<double> SectionZ;
		Sections.Reserve(Levels.Num());
		SectionZ.Reserve(Levels.Num());

		// Bottom-up, which is the order AppendLoft insists on.
		for (int32 Index = Levels.Num() - 1; Index >= 0; --Index)
		{
			const FFoldLevel& Level = Levels[Index];

			Sections.Add(FoldSection(Pitch, Gap,
				Reach * Level.AmplitudeFactor, HalfT * Level.ThicknessFactor, P.FoldSegments));
			SectionZ.Add(-P.Drop * Level.DropFraction);
		}

		return FHFMeshOps::AppendLoft(Mesh, Sections, SectionZ, /*bCapBottom*/ true, /*bCapTop*/ true,
			EHFSurfaceRole::Fabric);
	}
}

// ------------------------------------------------------------------------------- the arithmetic

double FHFCurtainKit::FullnessFor(double Pitch, double Depth)
{
	if (Pitch <= UE_DOUBLE_KINDA_SMALL_NUMBER)
	{
		return 1.0;
	}
	if (Depth <= 0.0)
	{
		return 1.0;
	}

	// Arc length of y = (H/2) sin(2 pi x / L) over one L, divided by L. Substituting u = 2 pi x / L
	// leaves an integrand of sqrt(1 + k^2 cos^2 u) with k = pi H / L, whose mean over a quarter period
	// IS the fullness - so the whole figure is one integral over [0, pi/2] and needs no elliptic
	// function table.
	const double K = UE_DOUBLE_PI * Depth / Pitch;

	const auto Integrand = [K](double U)
	{
		const double C = FMath::Cos(U);
		return FMath::Sqrt(1.0 + K * K * C * C);
	};

	// Simpson over an even number of panels.
	const double Upper = UE_DOUBLE_HALF_PI;
	const double Step = Upper / ArcLengthSteps;

	double Sum = Integrand(0.0) + Integrand(Upper);
	for (int32 Index = 1; Index < ArcLengthSteps; ++Index)
	{
		Sum += Integrand(Index * Step) * ((Index % 2 == 0) ? 2.0 : 4.0);
	}

	return (Sum * Step / 3.0) / Upper;
}

double FHFCurtainKit::DepthForFullness(double Pitch, double Fullness)
{
	if (Pitch <= UE_DOUBLE_KINDA_SMALL_NUMBER || Fullness <= 1.0)
	{
		return 0.0;
	}

	// Monotonic in depth, so bisection is exact and cannot be tripped by a bad initial guess. The
	// bracket is generous: even at 6x fullness the depth is under four pitches.
	double Low = 0.0;
	double High = Pitch * 8.0;

	for (int32 Step = 0; Step < InverseSteps; ++Step)
	{
		const double Mid = (Low + High) * 0.5;
		if (FullnessFor(Pitch, Mid) < Fullness)
		{
			Low = Mid;
		}
		else
		{
			High = Mid;
		}
	}

	return (Low + High) * 0.5;
}

double FHFCurtainParams::HeadingRepeat() const
{
	switch (Heading)
	{
	// Pinches at 100-140 mm centres, and the fabric comes back at every pinch: one out-and-back per
	// two pinches.
	case EHFCurtainHeading::PinchPleat:
		return 24.0;

	// Denser and shallower. A gathering tape pulls up on an 80-110 mm repeat.
	case EHFCurtainHeading::PencilPleat:
		return 18.0;

	// Rings on a pole at 150-200 mm centres. The deepest, softest wave of the four - and the one a
	// pelmet cannot conceal, because there is no track to conceal.
	case EHFCurtainHeading::Eyelet:
		return 34.0;

	// Corded tape at 60 or 80 mm. The most regular of the four, and the easiest to state.
	case EHFCurtainHeading::Wave:
	default:
		return 16.0;
	}
}

double FHFCurtainParams::NaturalFoldDepth() const
{
	return FHFCurtainKit::DepthForFullness(BuiltFoldPitch(), Fullness);
}

double FHFCurtainParams::FoldDepth() const
{
	const double Natural = NaturalFoldDepth();
	if (MaxFoldDepth <= 0.0)
	{
		return Natural;
	}

	// The fan and the cloth's own thickness come out of the same budget, and both are taken FIRST: a
	// bundle standing through a fascia is the failure this clamp exists for, and a slightly flatter
	// fold is not.
	const double Taken = 2.0 * (SpreadReach() + ClothReachAllowance());
	return FMath::Clamp(FMath::Min(Natural, MaxFoldDepth - Taken), 0.0, Natural);
}

double FHFCurtainParams::AchievedFullness() const
{
	return FHFCurtainKit::FullnessFor(BuiltFoldPitch(), FoldDepth());
}

// ------------------------------------------------------------------------------------ the build

FName FHFCurtainKit::FoldPartId(int32 LeafIndex, int32 FoldIndex)
{
	return FName(*FString::Printf(TEXT("Leaf%d_Fold%d"), LeafIndex, FoldIndex));
}

FName FHFCurtainKit::LeadFoldPartId(int32 LeafIndex, int32 FoldsInLeaf)
{
	return FoldPartId(LeafIndex, FMath::Max(FoldsInLeaf - 1, 0));
}

FHFCurtainParams FHFCurtainKit::Sanitise(const FHFCurtainParams& Params)
{
	FHFCurtainParams P = Params;

	P.TrackWidth = FMath::Max(P.TrackWidth, 0.0);
	P.Drop = FMath::Max(P.Drop, 0.0);
	P.Fullness = FMath::Clamp(P.Fullness, 1.0, 4.0);
	P.FabricThickness = FMath::Clamp(P.FabricThickness, 0.01, 2.0);
	P.FoldSegments = FMath::Clamp(P.FoldSegments, 4, 64);
	P.StackRatio = FMath::Clamp(P.StackRatio, 0.05, 0.6);

	// The clearance may not eat the run. A track whose end stops meet is not a shallow curtain, it
	// is no curtain, and clamping is the honest answer to a drawing that asked for one.
	P.EndGap = FMath::Clamp(P.EndGap, 0.0, FMath::Max(P.TrackWidth * 0.25, 0.0));

	// A repeat wider than the leaf gives a one-fold curtain, which is a flat panel with a bend in
	// it. Half a leaf is the coarsest thing worth calling a fold.
	const double Leaf = P.LeafWidth();
	P.FoldPitch = FMath::Clamp(P.FoldPitch, 1.0, FMath::Max(Leaf * 0.5, 1.0));

	P.HeadingHeight = FMath::Clamp(P.HeadingHeight, 0.0, FMath::Max(P.Drop * 0.25, 0.0));
	P.HemHeight = FMath::Clamp(P.HemHeight, 0.0, FMath::Max(P.Drop * 0.25, 0.0));

	// NARROWER THAN THE CLOTH IS THICK, always. A parting wider than the fabric is a window rather
	// than a slot, and forty of them down a drawn curtain read as a venetian blind - see
	// FHFCurtainParams::FoldGap, which was measured off a render rather than reasoned about. Clamped
	// structurally so no caller can reintroduce it by setting the figure back.
	P.FoldGap = FMath::Clamp(P.FoldGap, 0.0,
		FMath::Max(FMath::Min(P.BuiltFoldPitch() * 0.1, P.FabricThickness * 0.5), 0.0));

	// The fan is capped at a quarter of whatever depth the pelmet allows, so it stays a hint that
	// the bundle has body rather than something that decides the fold depth.
	if (P.MaxFoldDepth > 0.0)
	{
		const int32 Steps = FMath::Max(P.FoldsPerLeaf() / 2, 1);
		P.StackSpread = FMath::Clamp(P.StackSpread, 0.0, P.MaxFoldDepth * 0.125 / Steps);
	}
	else
	{
		P.StackSpread = FMath::Max(P.StackSpread, 0.0);
	}

	return P;
}

FHFCurtainBuild FHFCurtainKit::Build(const FHFCurtainParams& Params)
{
	FHFCurtainBuild Out;
	FHFMeshOps::InitialiseMesh(Out.Shell);
	FHFMeshOps::InitialiseMesh(Out.Anchors);

	const FHFCurtainParams P = Sanitise(Params);
	Out.Used = P;

	if (!P.IsValid())
	{
		return Out;
	}

	const int32 Leaves = P.LeafCount();
	const int32 Folds = P.FoldsPerLeaf();
	const double Pitch = P.BuiltFoldPitch();
	const double Stack = P.StackPitch();
	const double LeafWidth = P.LeafWidth();

	// The run, in the curtain's own frame: centred on the origin, clearance taken off both ends.
	const double RunMin = -P.ClearWidth() * 0.5;

	for (int32 Leaf = 0; Leaf < Leaves; ++Leaf)
	{
		// WHICH END THIS LEAF STACKS AT decides everything else about it. Leaf 0 of a pair stacks at
		// -X, leaf 1 at +X, and a single leaf stacks at whichever end it was told to. The folds are
		// numbered FROM the stack end, so fold 0 is the one against the end stop and never moves, and
		// the last one is the leading edge that travels nearly the whole leaf.
		const bool bStacksLeft = (Leaf == 0) && (P.Draw != EHFCurtainDraw::SingleStackRight);

		const double LeafMin = RunMin + Leaf * LeafWidth;
		const double LeafMax = LeafMin + LeafWidth;
		Out.LeafSpans.Add(FVector2D(LeafMin, LeafMax));

		for (int32 Fold = 0; Fold < Folds; ++Fold)
		{
			// Where this fold's near end sits when the curtain is shut, and where it ends up when it
			// is drawn. Measured from the stack end in both cases, which is what makes the travel come
			// out as the difference between a hanging repeat and a pressed one.
			const double ClosedNear = bStacksLeft
				? LeafMin + Fold * Pitch
				: LeafMax - (Fold + 1) * Pitch;

			const double OpenNear = bStacksLeft
				? LeafMin + Fold * Stack
				: LeafMax - Pitch - Fold * Stack;

			FDynamicMesh3 Mesh;
			FHFMeshOps::InitialiseMesh(Mesh);

			if (!AppendFold(Mesh, P, Pitch))
			{
				continue;
			}

			FHFMeshOps::ApplyWorldScaleUVs(Mesh);

			// The fold against the end stop is already where a drawn curtain would put it, so it does
			// not move. It is fabric all the same, so it goes into the fixed mesh rather than being
			// emitted as a part declaring a slide of zero.
			if (Fold == 0)
			{
				FDynamicMesh3 Placed = Mesh;
				MeshTransforms::Translate(Placed, FVector3d(ClosedNear, 0.0, 0.0));
				FHFMeshOps::AppendPreservingRoles(Out.Anchors, Placed);
				continue;
			}

			FHFMeshPart Part;
			Part.PartId = FoldPartId(Leaf, Fold);
			Part.Mesh = MoveTemp(Mesh);
			Part.PivotTransform = FTransform(FVector(ClosedNear, 0.0, 0.0));

			Part.Motion.Type = EHFMotionType::Slide;

			// TRAVEL ALONG THE TRACK, AND A LITTLE ACROSS IT. The along-track component is the whole
			// mechanism: this fold's glider ends up Stack from its neighbour instead of Pitch, which
			// is the gather. The across-track component is StackSpread, alternating side by side so
			// the bundle fans open in depth instead of collapsing into one plane - see
			// FHFCurtainParams::StackSpread.
			const double AlongTrack = OpenNear - ClosedNear;
			const double AcrossTrack = ((Fold % 2 == 0) ? 1.0 : -1.0) * P.StackSpread
				* double((Fold + 1) / 2);

			const FVector Travel(AlongTrack, AcrossTrack, 0.0);
			Part.Motion.MaxTravelCm = Travel.Size();
			Part.Motion.Axis = Travel.GetSafeNormal(UE_DOUBLE_SMALL_NUMBER, FVector::XAxisVector);

			// EVERY FOLD IN A LEAF MOVES TOGETHER OR THE CLOTH TEARS. They are geared to the leading
			// fold rather than each given an independent amount: the gearing is already expressed by
			// each fold's own travel, which is exactly the case FHFPartMotion::DrivenByPartId exists
			// for. What it buys is that a leaf is ONE control - "draw the left curtain" - and that no
			// hand pose can leave fold 5 out at the stack with fold 6 still across the window.
			if (Fold < Folds - 1)
			{
				Part.Motion.DrivenByPartId = LeadFoldPartId(Leaf, Folds);
			}

			// Both leaves of a pair are opened by one control, and they do NOT cancel: they stack at
			// OPPOSITE ends, so the aperture appears in the middle and grows. That is the difference
			// between this and a two-track slider, whose leaves share one aperture and exchange
			// tracks - see FHFPartMotion::bMasterOpens and the wardrobe that taught it.
			Part.Motion.bMasterOpens = true;

			// Cloth stops a walkthrough pawn exactly where it is drawn, at any pose.
			Part.Collision = EHFPartCollision::Blocking;

			Out.Parts.Add(MoveTemp(Part));
		}
	}

	FHFMeshOps::AppendPreservingRoles(Out.Shell, Out.Anchors);
	for (const FHFMeshPart& Part : Out.Parts)
	{
		FDynamicMesh3 Placed = Part.Mesh;
		MeshTransforms::ApplyTransform(Placed, FTransformSRT3d(Part.PivotTransform), /*bReverseOrientationIfNeeded*/ true);
		FHFMeshOps::AppendPreservingRoles(Out.Shell, Placed);
	}

	Out.bValid = Out.Shell.TriangleCount() > 0;
	return Out;
}
