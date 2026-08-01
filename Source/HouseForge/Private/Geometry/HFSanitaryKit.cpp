// Copyright Siddartha G. All Rights Reserved.

#include "Geometry/HFSanitaryKit.h"

#include "DynamicMesh/MeshTransforms.h"
#include "Geometry/HFMeshOps.h"

using namespace UE::Geometry;

namespace
{
	/** Sides a revolved fitting is built from. 24 is round at arm's length and cheap. */
	constexpr int32 RevolveSides = 24;

	/** Corners a pressed bowl's rounded vertical arris is stepped in. */
	constexpr int32 BowlCornerSteps = 4;

	/** Thickness of the pressed steel a bowl is folded from. */
	constexpr double BowlWallThickness = 0.15;

	/**
	 * A rounded rectangle in plan, wound counter-clockwise.
	 *
	 * A PRESSED BOWL HAS NO SQUARE CORNERS, because it is drawn from a single sheet and a right angle
	 * cannot be drawn without tearing. It is also the detail that most gives a generated sink away: a
	 * box with a hole in it reads as a box however well it is proportioned, and the corner radius is
	 * the first thing the eye picks up when light runs round the inside of the bowl.
	 */
	TArray<FVector2D> RoundedRect(const FVector2D& Centre, const FVector2D& Half, double Radius)
	{
		const double R = FMath::Clamp(Radius, 0.0, FMath::Min(Half.X, Half.Y));

		TArray<FVector2D> Out;
		if (R <= UE_KINDA_SMALL_NUMBER)
		{
			Out.Add(Centre + FVector2D(-Half.X, -Half.Y));
			Out.Add(Centre + FVector2D(Half.X, -Half.Y));
			Out.Add(Centre + FVector2D(Half.X, Half.Y));
			Out.Add(Centre + FVector2D(-Half.X, Half.Y));
			return Out;
		}

		// Corner centres, counter-clockwise from the front-left.
		const FVector2D Corners[4] = {
			Centre + FVector2D(-(Half.X - R), -(Half.Y - R)),
			Centre + FVector2D(Half.X - R, -(Half.Y - R)),
			Centre + FVector2D(Half.X - R, Half.Y - R),
			Centre + FVector2D(-(Half.X - R), Half.Y - R)
		};

		// Each corner's arc starts a quarter turn on from the previous, beginning pointing at -Y.
		for (int32 Corner = 0; Corner < 4; ++Corner)
		{
			const double Start = -HALF_PI + Corner * HALF_PI;

			for (int32 Step = 0; Step <= BowlCornerSteps; ++Step)
			{
				const double Angle = Start + HALF_PI * static_cast<double>(Step)
					/ static_cast<double>(BowlCornerSteps);
				Out.Add(Corners[Corner] + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * R);
			}
		}

		return Out;
	}

	/** Scales a closed polygon about a centre - how a bowl's base is got from its mouth. */
	TArray<FVector2D> ScaledAbout(const TArray<FVector2D>& Polygon, const FVector2D& Centre, double Scale)
	{
		TArray<FVector2D> Out;
		Out.Reserve(Polygon.Num());
		for (const FVector2D& Point : Polygon)
		{
			Out.Add(Centre + (Point - Centre) * Scale);
		}
		return Out;
	}
}

FHFSinkParams FHFSanitaryKit::SanitiseSink(const FHFSinkParams& Params)
{
	FHFSinkParams P = Params;

	P.Width = FMath::Max(P.Width, 0.0);
	P.Depth = FMath::Max(P.Depth, 0.0);
	P.BowlDepth = FMath::Max(P.BowlDepth, 0.0);
	P.BowlCount = FMath::Clamp(P.BowlCount, 1, 3);
	P.RimThickness = FMath::Max(P.RimThickness, 0.05);

	// The rim cannot eat the bowls. Every bowl needs a flat each side of it and one between it and
	// its neighbour, so the widest usable rim is what leaves each bowl a positive width.
	const double MaxRim = P.Width / static_cast<double>(2 * P.BowlCount + 2);
	P.RimWidth = FMath::Clamp(P.RimWidth, 0.0, FMath::Max(MaxRim, 0.0));

	const double BowlWidth = P.Width / static_cast<double>(P.BowlCount) - 2.0 * P.RimWidth;
	const double BowlBreadth = P.Depth - 2.0 * P.RimWidth;

	P.BowlCornerRadius = FMath::Clamp(P.BowlCornerRadius, 0.0,
		FMath::Max(FMath::Min(BowlWidth, BowlBreadth) * 0.5, 0.0));

	P.Tap.BodyHeight = FMath::Max(P.Tap.BodyHeight, 0.0);
	P.Tap.BodyRadius = FMath::Max(P.Tap.BodyRadius, 0.0);
	P.Tap.SpoutRadius = FMath::Clamp(P.Tap.SpoutRadius, 0.0, FMath::Max(P.Tap.BodyRadius, 0.0));
	P.Tap.SpoutReach = FMath::Max(P.Tap.SpoutReach, 0.0);
	P.Tap.LeverLength = FMath::Max(P.Tap.LeverLength, 0.0);
	P.Tap.LeverLiftDegrees = FMath::Clamp(P.Tap.LeverLiftDegrees, 0.0, 90.0);
	P.Tap.SpoutSwivelDegrees = FMath::Clamp(P.Tap.SpoutSwivelDegrees, 0.0, 180.0);

	return P;
}

FHFSinkBuild FHFSanitaryKit::BuildSink(const FHFSinkParams& Params)
{
	FHFSinkBuild Out;
	FHFMeshOps::InitialiseMesh(Out.Shell);

	const FHFSinkParams P = SanitiseSink(Params);
	Out.Used = P;

	if (!P.IsValid())
	{
		return Out;
	}

	const double ModuleWidth = P.Width / static_cast<double>(P.BowlCount);
	const FVector2D BowlHalf(
		FMath::Max(ModuleWidth * 0.5 - P.RimWidth, 0.0),
		FMath::Max(P.Depth * 0.5 - P.RimWidth, 0.0));

	if (BowlHalf.X <= 0.0 || BowlHalf.Y <= 0.0)
	{
		return Out;
	}

	// -------------------------------------------------------------------------------------- the rim
	//
	// A pressed plate with a hole over each bowl, triangulated with its holes rather than cut. The
	// rim is what the sink is HUNG BY - the whole thing sits on the stone by this flange - so it is
	// the one part whose outline has to be exactly the drawn footprint.
	//
	// IT SITS ON THE STONE, NOT IN IT. The rim occupies 0 to RimThickness, ABOVE Z = 0, because a
	// top-mounted sink stands proud of the worktop by the thickness of its own flange. Modelled the
	// other way up - flange buried in the slab with its top face level with the stone - it put 318
	// cm2 of steel in exactly the same plane as the granite around it, both facing up: textbook
	// z-fighting, invisible in a still and strobing the moment the camera moves. The coplanar scan
	// caught it on the first build.

	// A BOWL IS A THIN PRESSING, AND ITS WALL IS BUILT AS A SOLID MINUS A SOLID rather than as a
	// thin annulus swept down. Swept, the annulus between the bowl's outer face and its inner is
	// about a millimetre and a half across a 20 cm drop, and a triangulator handed a ring that thin
	// resolves it as the outer polygon alone - which is a SOLID BLOCK the shape of the bowl, in
	// exactly the place a bowl should be, with the rim sitting correctly on top of it. It looks
	// perfect from every angle and it is not a sink. Two robust solid prisms and one boolean cannot
	// fail that way, and the volume test below is what says so.
	const double Wall = FMath::Min(BowlWallThickness * 2.0,
		FMath::Min(BowlHalf.X, BowlHalf.Y) * 0.5);

	TArray<FVector2D> BowlOuter[3];
	TArray<FVector2D> BowlInner[3];
	TArray<TArray<FVector2D>> RimHoles;

	for (int32 Bowl = 0; Bowl < P.BowlCount; ++Bowl)
	{
		const FVector2D Centre(-P.Width * 0.5 + ModuleWidth * (Bowl + 0.5), 0.0);

		BowlOuter[Bowl] = RoundedRect(Centre, BowlHalf, P.BowlCornerRadius);
		BowlInner[Bowl] = RoundedRect(Centre, BowlHalf - FVector2D(Wall, Wall),
			P.BowlCornerRadius - Wall);

		// The rim laps the TOP OF THE BOWL WALL, which is what a pressing does - the flange and the
		// bowl are one piece of steel folded over. Holed at the inner face, so the wall's top edge is
		// covered rather than meeting the rim's hole in a shared vertical plane.
		RimHoles.Add(BowlInner[Bowl]);
	}

	const TArray<FVector2D> RimOutline = {
		FVector2D(-P.Width * 0.5, -P.Depth * 0.5),
		FVector2D(P.Width * 0.5, -P.Depth * 0.5),
		FVector2D(P.Width * 0.5, P.Depth * 0.5),
		FVector2D(-P.Width * 0.5, P.Depth * 0.5)
	};

	if (!FHFMeshOps::AppendPrismWithHoles(Out.Shell, RimOutline, RimHoles,
		0.0, P.RimThickness, EHFSurfaceRole::Sanitary))
	{
		return Out;
	}

	// ------------------------------------------------------------------------------------ the bowls
	//
	// Each bowl is a tub: four tapered walls and a base, hollow, hung from the hole in the rim. Built
	// as a wall prism plus a base rather than as a solid block, because a sink that is not hollow is
	// the single most obvious failure this fixture can have and nothing but real geometry proves it.
	//
	// The walls draw IN towards the base, which is how a bowl is pressed and what makes water find
	// the waste rather than standing in the corners.

	const double BaseZ = -P.BowlDepth;

	for (int32 Bowl = 0; Bowl < P.BowlCount; ++Bowl)
	{
		// The tub as a solid, from the underside of its base up to the rim.
		FDynamicMesh3 Tub;
		FHFMeshOps::InitialiseMesh(Tub);

		if (!FHFMeshOps::AppendPrism(Tub, BowlOuter[Bowl], BaseZ - Wall, 0.0,
			EHFSurfaceRole::Sanitary))
		{
			continue;
		}

		// And the water's worth of it taken back out, open at the top. Carried up past the rim so the
		// cut face reaches daylight rather than leaving a skin of steel over the mouth.
		FDynamicMesh3 Cavity;
		FHFMeshOps::InitialiseMesh(Cavity);

		if (FHFMeshOps::AppendPrism(Cavity, BowlInner[Bowl], BaseZ, P.RimThickness + 1.0,
			EHFSurfaceRole::Sanitary))
		{
			FHFMeshOps::SubtractInPlace(Tub, Cavity);
		}

		FHFMeshOps::AppendPreservingRoles(Out.Shell, Tub);

		Out.BowlVolume += (2.0 * (BowlHalf.X - Wall)) * (2.0 * (BowlHalf.Y - Wall)) * P.BowlDepth;
	}

	// -------------------------------------------------------------------------------------- the tap
	//
	// Mounted on the rim at the back, which is where the tap hole is punched, and centred on the run.

	if (P.bHasTap && P.Tap.IsValid())
	{
		const FVector3d TapBase(0.0, P.Depth * 0.5 - P.RimWidth * 0.5, 0.0);

		FDynamicMesh3 Body;
		FHFMeshOps::InitialiseMesh(Body);

		// A slim column swelling very slightly at the base, which is what a monobloc actually is: a
		// straight cylinder reads as a pipe, and the flare is where it meets the stone.
		const TArray<FVector2D> BodyProfile = {
			FVector2D(0.0, 0.0),
			FVector2D(0.0, P.Tap.BodyRadius * 1.35),
			FVector2D(1.0, P.Tap.BodyRadius * 1.1),
			FVector2D(2.0, P.Tap.BodyRadius),
			FVector2D(P.Tap.BodyHeight, P.Tap.BodyRadius),
			FVector2D(P.Tap.BodyHeight, 0.0)
		};

		if (FHFMeshOps::AppendRevolvedProfile(Body, BodyProfile, TapBase, FVector3d::UnitZ(),
			RevolveSides, EHFSurfaceRole::MetalHardware))
		{
			FHFMeshOps::AppendPreservingRoles(Out.Shell, Body);
		}

		// ------------------------------------------------------------------------- the spout
		//
		// ITS OWN PART, TURNING ABOUT THE BODY'S AXIS. A spout that cannot swing cannot reach the far
		// bowl of a double sink, which is the entire reason a double sink has a swivel spout.
		//
		// Built in its own local space with the pivot on the origin, exactly as every other part is.

		FHFMeshPart Spout;
		Spout.PartId = TapSpoutPartId();
		FHFMeshOps::InitialiseMesh(Spout.Mesh);

		// A horizontal arm reaching forward out of the top of the body, capped by a downturned nose.
		const TArray<FVector2D> ArmProfile = {
			FVector2D(0.0, 0.0),
			FVector2D(0.0, P.Tap.SpoutRadius),
			FVector2D(P.Tap.SpoutReach, P.Tap.SpoutRadius),
			FVector2D(P.Tap.SpoutReach, 0.0)
		};

		FHFMeshOps::AppendRevolvedProfile(Spout.Mesh, ArmProfile,
			FVector3d(0.0, 0.0, P.Tap.BodyHeight - P.Tap.SpoutRadius),
			-FVector3d::UnitY(), RevolveSides, EHFSurfaceRole::MetalHardware);

		// The nose, pointing down into the bowl.
		const TArray<FVector2D> NoseProfile = {
			FVector2D(0.0, P.Tap.SpoutRadius),
			FVector2D(P.Tap.SpoutRadius * 2.2, P.Tap.SpoutRadius * 0.85),
			FVector2D(P.Tap.SpoutRadius * 2.2, 0.0)
		};

		FHFMeshOps::AppendRevolvedProfile(Spout.Mesh, NoseProfile,
			FVector3d(0.0, -P.Tap.SpoutReach, P.Tap.BodyHeight - P.Tap.SpoutRadius),
			-FVector3d::UnitZ(), RevolveSides, EHFSurfaceRole::MetalHardware);

		FHFMeshOps::ApplyWorldScaleUVs(Spout.Mesh);

		// About the BODY's axis, at the body's centre - so the spout sweeps a circle over the bowls
		// rather than swinging about its own root and pulling out of the tap.
		Spout.PivotTransform = FTransform(TapBase);
		Spout.Motion.Type = EHFMotionType::Hinge;
		Spout.Motion.Axis = FVector::ZAxisVector;
		Spout.Motion.MaxAngleDegrees = P.Tap.SpoutSwivelDegrees;
		Spout.DefaultOpenAmount = 0.0;

		Out.Parts.Add(MoveTemp(Spout));

		// -------------------------------------------------------------------------- the lever
		//
		// Lifts to turn the water on, on the side of the body, hinged across the run.

		if (P.Tap.LeverLength > 0.0)
		{
			FHFMeshPart Lever;
			Lever.PartId = TapLeverPartId();
			FHFMeshOps::InitialiseMesh(Lever.Mesh);

			const double LeverRadius = FMath::Max(P.Tap.SpoutRadius * 0.55, 0.25);

			const TArray<FVector2D> LeverProfile = {
				FVector2D(0.0, 0.0),
				FVector2D(0.0, LeverRadius),
				FVector2D(P.Tap.LeverLength, LeverRadius * 0.8),
				FVector2D(P.Tap.LeverLength, 0.0)
			};

			// Runs backwards from the top of the body, which is where a monobloc's lever sits.
			FHFMeshOps::AppendRevolvedProfile(Lever.Mesh, LeverProfile,
				FVector3d::ZeroVector, FVector3d::UnitY(), RevolveSides,
				EHFSurfaceRole::MetalHardware);

			FHFMeshOps::ApplyWorldScaleUVs(Lever.Mesh);

			Lever.PivotTransform = FTransform(TapBase + FVector3d(0.0, 0.0, P.Tap.BodyHeight));

			// Lifting is a rotation about the axis ACROSS the tap, and the SIGN is the whole content
			// of it: the lever runs backwards from the top of the body along +Y, so a positive turn
			// about +X carries its far end upwards. Negative, it pressed the handle down through the
			// worktop instead - a movement of exactly the same size, in the one direction a mixer
			// lever cannot go, and indistinguishable from correct in any still.
			Lever.Motion.Type = EHFMotionType::Hinge;
			Lever.Motion.Axis = FVector::XAxisVector;
			Lever.Motion.MaxAngleDegrees = P.Tap.LeverLiftDegrees;
			Lever.DefaultOpenAmount = 0.0;

			Out.Parts.Add(MoveTemp(Lever));
		}
	}

	FHFMeshOps::ApplyWorldScaleUVs(Out.Shell);

	Out.bValid = Out.Shell.TriangleCount() > 0;
	return Out;
}
