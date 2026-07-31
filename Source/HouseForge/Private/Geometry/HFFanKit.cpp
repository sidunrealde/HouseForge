// Copyright Siddartha G. All Rights Reserved.

#include "Geometry/HFFanKit.h"

#include "DynamicMesh/MeshTransforms.h"
#include "Geometry/HFMeshOps.h"

using namespace UE::Geometry;

const FName FHFFanKit::RotorPartId(TEXT("Rotor"));

namespace
{
	/** Sides on anything revolved. Enough that a 120 sweep's hub does not read as a polygon. */
	constexpr int32 RevolveSides = 32;

	/** Below this a blade is a wire and a hub is a point. */
	constexpr double MinStock = 0.05;

	/**
	 * Turns a mesh about an axis through the origin.
	 *
	 * MeshTransforms::Rotate takes an FRotator, which cannot express a rotation about an arbitrary
	 * axis without going through Euler angles - and a blade's pitch is about its own long axis, which
	 * is not an Euler axis at any station but the first. A quaternion says it directly.
	 */
	void RotateAboutOrigin(FDynamicMesh3& Mesh, const FVector3d& Axis, double Degrees)
	{
		const FTransformSRT3d Rotation(FQuaterniond(Axis, Degrees, /*bAngleIsDegrees*/ true),
			FVector3d::Zero(), FVector3d::One());

		MeshTransforms::ApplyTransform(Mesh, Rotation, /*bReverseOrientationIfNeeded*/ true);
	}

	/**
	 * One blade, in the rotor's own space, lying along +X before it is turned to its station.
	 *
	 * Built by sweeping a chamfered section along the radius rather than as a box, so the two long
	 * edges carry a real bevel. That is the edge a fan actually shows: it is the only object in the
	 * room that is both moving and lit from above, and a mathematically sharp arris on it reads as CG
	 * in every frame of a walkthrough.
	 *
	 * The section is authored in (chord, thickness) - AppendExtrudedSection derives its second
	 * in-plane axis as SweepDir x SectionU, so sweeping along +X with u on +Y puts v on +Z exactly.
	 */
	FDynamicMesh3 MakeBlade(const FHFFanParams& P, double RootRadius)
	{
		FDynamicMesh3 Blade;
		FHFMeshOps::InitialiseMesh(Blade);

		const double Half = FMath::Max(P.BladeChord, MinStock) * 0.5;
		const double Thick = FMath::Max(P.BladeThickness, MinStock) * 0.5;

		// The chamfer cannot eat the section it is cut from - a bevel wider than the stock is half a
		// blade, not a chamfered one.
		const double Bevel = FMath::Clamp(P.BladeEdgeBevel, 0.0, FMath::Min(Half, Thick) * 0.8);

		TArray<FVector2D> Section;
		if (Bevel > UE_KINDA_SMALL_NUMBER)
		{
			Section = {
				FVector2D(-Half + Bevel, -Thick), FVector2D(Half - Bevel, -Thick),
				FVector2D(Half, -Thick + Bevel), FVector2D(Half, Thick - Bevel),
				FVector2D(Half - Bevel, Thick), FVector2D(-Half + Bevel, Thick),
				FVector2D(-Half, Thick - Bevel), FVector2D(-Half, -Thick + Bevel)
			};
		}
		else
		{
			Section = {
				FVector2D(-Half, -Thick), FVector2D(Half, -Thick),
				FVector2D(Half, Thick), FVector2D(-Half, Thick)
			};
		}

		// ------------------------------------------------------------------------- which way it is set
		//
		// THE SIGN OF THE PITCH IS THE DIRECTION OF THE AIRFLOW, and getting it backwards builds a fan
		// that is perfect in every measurable way and blows at the ceiling.
		//
		// The derivation, in the fan's own frame, where +Z is the axis pointing into the room. A
		// positive phase turns the rotor about +Z, which carries +X towards +Y, so a blade lying along
		// +X is travelling in +Y and ITS LEADING EDGE IS ITS +Y EDGE. A blade wants to throw air along
		// +Z, so - by the same reasoning as a wing at a positive angle of attack, whose lift is opposite
		// the air it deflects - its leading edge has to be tilted towards -Z, the ceiling side. That is
		// the rule of thumb a real fan is checked against: IN DOWNDRAFT THE LEADING EDGE IS THE HIGHER
		// ONE.
		//
		// A rotation of +theta about +X carries +Y towards +Z, which puts the leading edge on the ROOM
		// side - the wrong way round. Hence the minus. AirflowSign then flips the whole thing for an
		// extract, which has to draw air out through the wall rather than push it into the room.
		const double PitchDegrees = -P.BladePitchDegrees * P.AirflowSign();
		const double PitchRadians = FMath::DegreesToRadians(PitchDegrees);
		const double CosPitch = FMath::Cos(PitchRadians);
		const double SinPitch = FMath::Sin(PitchRadians);

		// ------------------------------------------------------------------------------ how long it is
		//
		// THE SWEEP IS A CIRCLE, so a blade run out to the sweep radius along its own centreline puts
		// its outer CORNERS outside that circle - by 2.4 mm on a standard 1200 fan, which is small but
		// is the difference between a declared dimension being true and being nearly true. A fan is
		// bought by its sweep; a 1200 fan that measures 1205 is a fan nothing can be dimensioned
		// against.
		//
		// So the length is set from the corner rather than the centreline: find how far the pitched
		// section reaches to either side of the centreline IN PLAN - the section's (u, v) lands on
		// (Y, Z), and the pitch turns about X, so a section point is u*cos - v*sin off the centreline -
		// and pull the tip back until sqrt(tip^2 + that^2) is exactly the sweep radius.
		double HalfWidthInPlan = 0.0;
		for (const FVector2D& Point : Section)
		{
			HalfWidthInPlan = FMath::Max(HalfWidthInPlan,
				FMath::Abs(Point.X * CosPitch - Point.Y * SinPitch));
		}

		const double TipRadius = FMath::Sqrt(
			FMath::Max(FMath::Square(P.SweepRadius()) - FMath::Square(HalfWidthInPlan), 0.0));

		const double Length = TipRadius - RootRadius;

		// A blade wider than the fan it is on has nowhere to go. Refused rather than clamped: a sliver
		// carries through every volume and bounds measurement taken afterwards.
		if (Length <= MinStock)
		{
			return Blade;
		}

		FHFMeshOps::AppendExtrudedSection(Blade, Section, FVector3d(RootRadius, 0.0, 0.0),
			FVector3d::UnitY(), FVector3d::UnitX(), Length, EHFSurfaceRole::MetalHardware);

		// Pitch, about the blade's own long axis, which is +X and passes through the fan's centre. A
		// flat blade is what makes a generated fan read as a paper cut-out.
		if (Blade.TriangleCount() > 0 && FMath::Abs(PitchDegrees) > UE_KINDA_SMALL_NUMBER)
		{
			RotateAboutOrigin(Blade, FVector3d::UnitX(), PitchDegrees);
		}

		return Blade;
	}

	/** The blades, evenly spaced about the axis, merged into one mesh in rotor space. */
	void AppendBlades(FDynamicMesh3& Rotor, const FHFFanParams& P, double RootRadius, double PlaneZ)
	{
		const int32 Count = FMath::Clamp(P.BladeCount, 2, 12);

		for (int32 Index = 0; Index < Count; ++Index)
		{
			FDynamicMesh3 Blade = MakeBlade(P, RootRadius);
			if (Blade.TriangleCount() == 0)
			{
				continue;
			}

			const double Yaw = (360.0 * Index) / static_cast<double>(Count);
			RotateAboutOrigin(Blade, FVector3d::UnitZ(), Yaw);
			MeshTransforms::Translate(Blade, FVector3d(0.0, 0.0, PlaneZ));

			// Never the raw append: it renumbers polygroups, and the polygroup IS the surface role.
			FHFMeshOps::AppendPreservingRoles(Rotor, Blade);
		}
	}
}

// ------------------------------------------------------------------------------------ parameters

double FHFFanParams::CaseHalfWidth() const
{
	return FMath::Max(SweepRadius() * 1.25, MotorDiameter * 0.5 + MinStock);
}

double FHFFanParams::ThroatRadius() const
{
	return FMath::Max(SweepRadius() * 1.05, MotorDiameter * 0.5 + MinStock);
}

double FHFFanParams::DuctSide() const
{
	// Inscribed in the case - a square hole's corners reach its half-diagonal, so the side is bounded
	// by the case width over root two - with a tenth held back as the flange that hides the arris.
	const double InsideTheCase = CaseHalfWidth() * 2.0 * 0.9 / UE_DOUBLE_SQRT_2;

	return FMath::Max(FMath::Min(SweepDiameter, InsideTheCase), 0.0);
}

double FHFFanParams::RodHoleHalfSide() const
{
	// A hole a rod hangs plumb in with a working tolerance round it, and no more. Half again on the
	// rod's radius plus a fixed few millimetres, so a thin rod still gets a hole somebody could cut
	// rather than a slot the rod jams in.
	return FMath::Max(RodDiameter, MinStock) * 0.75 + 0.4;
}

double FHFFanParams::CanopyRadius() const
{
	// The corners are what show. A square hole reaches its half-diagonal, so the canopy is sized
	// against that rather than against the hole's side, with a lap so the two are never flush.
	const double CoversTheHole = RodHoleHalfSide() * UE_DOUBLE_SQRT_2 + 0.5;

	return FMath::Max3(FMath::Max(MotorDiameter, 0.0) * 0.5 * 0.55,
		FMath::Max(RodDiameter, MinStock), CoversTheHole);
}

double FHFFanParams::OverallDepth() const
{
	return Kind == EHFFanKind::Ceiling
		? FMath::Max(DropLength, 0.0) + FMath::Max(MotorHeight, 0.0)
		: FMath::Max(CaseDepth, 0.0);
}

bool FHFFanParams::IsValid() const
{
	return SweepDiameter > 0.0 && MotorDiameter > 0.0 && OverallDepth() > 0.0;
}

FHFFanParams FHFFanKit::DefaultsFor(EHFFanKind Kind)
{
	FHFFanParams P;
	P.Kind = Kind;

	if (Kind == EHFFanKind::Exhaust)
	{
		// A bathroom extract: small, fast, many blades, and set into the wall rather than hung off it.
		P.SweepDiameter = 22.0;
		P.CaseDepth = 12.0;
		P.BladeCount = 5;
		P.BladeChord = 4.5;
		P.BladeThickness = 0.25;
		P.BladeEdgeBevel = 0.08;
		P.BladePitchDegrees = 22.0;
		P.MotorDiameter = 7.0;
		P.MotorHeight = 5.0;
		P.RevolutionsPerMinute = 1350.0;
	}

	return P;
}

FHFFanParams FHFFanKit::Sanitise(const FHFFanParams& Params)
{
	FHFFanParams P = Params;

	P.SweepDiameter = FMath::Max(P.SweepDiameter, 0.0);
	P.BladeCount = FMath::Clamp(P.BladeCount, 2, 12);
	P.BladeChord = FMath::Max(P.BladeChord, MinStock);
	P.BladeThickness = FMath::Max(P.BladeThickness, MinStock);
	P.BladeEdgeBevel = FMath::Max(P.BladeEdgeBevel, 0.0);
	P.BladePitchDegrees = FMath::Clamp(P.BladePitchDegrees, 0.0, 45.0);
	P.MotorDiameter = FMath::Max(P.MotorDiameter, MinStock);
	P.MotorHeight = FMath::Max(P.MotorHeight, MinStock);
	P.RodDiameter = FMath::Max(P.RodDiameter, MinStock);
	P.DropLength = FMath::Max(P.DropLength, 0.0);
	P.CaseDepth = FMath::Max(P.CaseDepth, 0.0);

	// A canopy below the motor it hangs is not a canopy. Clamped rather than refused, so a ceiling
	// drop deeper than the rod resolved for it still builds something rather than nothing.
	P.CanopyDrop = FMath::Clamp(P.CanopyDrop, 0.0, P.DropLength);

	// A hub wider than the sweep is a disc, not a fan. Clamped rather than refused: a drawing that
	// gave a sweep and left the motor at a catalogue figure is a drawing worth building from.
	P.MotorDiameter = FMath::Min(P.MotorDiameter, FMath::Max(P.SweepDiameter * 0.8, MinStock));

	// The rod hangs inside the canopy, so it cannot be fatter than the motor it hangs.
	P.RodDiameter = FMath::Min(P.RodDiameter, P.MotorDiameter);

	return P;
}

// ------------------------------------------------------------------------------------ generation

FHFFanBuild FHFFanKit::Build(const FHFFanParams& Params)
{
	FHFFanBuild Out;
	Out.Used = Sanitise(Params);

	const FHFFanParams& P = Out.Used;
	if (!P.IsValid())
	{
		// Empty rather than degenerate. An empty mesh appends harmlessly; a sliver carries through
		// every volume and bounds measurement taken afterwards.
		return Out;
	}

	FHFMeshOps::InitialiseMesh(Out.Shell);

	FDynamicMesh3 Rotor;
	FHFMeshOps::InitialiseMesh(Rotor);

	const double MotorRadius = P.MotorDiameter * 0.5;

	// Where the rotor's own origin sits on the axis, measured from the mounting surface.
	double RotorZ = 0.0;

	if (P.Kind == EHFFanKind::Ceiling)
	{
		// ------------------------------------------------------------------------- what does not turn
		//
		// The canopy is the shallow cone that covers the ceiling rose, and the rod hangs the motor
		// below the slab. Neither moves, so both stay on the shell: a fan welded into one mesh is a
		// fan that cannot run.

		const double RodRadius = P.RodDiameter * 0.5;

		// WHERE THE CANOPY SITS IS NOT ALWAYS WHERE THE FAN IS FIXED. The fan hangs off the
		// structural slab, so in a room with a false ceiling the rod runs down through the void and
		// through a hole cut in the panel, and the canopy belongs at the SOFFIT covering that hole.
		// At the slab it would be invisible inside the void and the hole left showing its corners.
		const double CanopyAt = FMath::Clamp(P.CanopyDrop, 0.0, P.DropLength);

		if (CanopyAt > MinStock)
		{
			// The bare rod through the ceiling void. Seen from the room only through the hole it
			// passes through, but a rod that started at the canopy would leave the fan hanging off
			// nothing when the void is looked into from above or in section.
			FHFMeshOps::AppendRevolvedProfile(Out.Shell,
				{ FVector2D(0.0, RodRadius), FVector2D(CanopyAt, RodRadius) },
				FVector3d::Zero(), FVector3d::UnitZ(), RevolveSides, EHFSurfaceRole::MetalHardware);
		}

		// Proportioned against the rod that shows BELOW the canopy, not against the whole drop: a
		// fan through a 40 cm ceiling would otherwise get a canopy scaled to a rod nobody can see.
		const double CanopyHeight = FMath::Min(4.0, FMath::Max((P.DropLength - CanopyAt) * 0.4, 0.5));

		FHFMeshOps::AppendRevolvedProfile(Out.Shell,
			{ FVector2D(CanopyAt, P.CanopyRadius()), FVector2D(CanopyAt + CanopyHeight, RodRadius) },
			FVector3d::Zero(), FVector3d::UnitZ(), RevolveSides, EHFSurfaceRole::MetalHardware);

		const double RodTop = FMath::Min(CanopyAt + CanopyHeight, P.DropLength);
		if (P.DropLength > RodTop)
		{
			FHFMeshOps::AppendRevolvedProfile(Out.Shell,
				{ FVector2D(RodTop, RodRadius), FVector2D(P.DropLength, RodRadius) },
				FVector3d::Zero(), FVector3d::UnitZ(), RevolveSides, EHFSurfaceRole::MetalHardware);
		}

		// -------------------------------------------------------------------------- what does turn
		//
		// On a real ceiling fan the visible motor body revolves with the blades bolted round it, so
		// the housing is on the rotor and not on the rod above it.

		RotorZ = P.DropLength;

		FHFMeshOps::AppendRevolvedProfile(Rotor,
			{
				FVector2D(0.0, MotorRadius * 0.35),
				FVector2D(P.MotorHeight * 0.25, MotorRadius),
				FVector2D(P.MotorHeight * 0.8, MotorRadius),
				FVector2D(P.MotorHeight, MotorRadius * 0.5)
			},
			FVector3d::Zero(), FVector3d::UnitZ(), RevolveSides, EHFSurfaceRole::Appliance);

		Out.BladePlaneZ = RotorZ + P.MotorHeight * P.BladePlaneFraction();
		AppendBlades(Rotor, P, MotorRadius * 0.9, P.MotorHeight * P.BladePlaneFraction());
	}
	else
	{
		// ------------------------------------------------------------ an extract, set into the wall
		//
		// A square case standing proud of the wall with a round aperture through it, which is what a
		// window or wall extract is. The aperture is subtracted rather than triangulated as an
		// annulus because the tool carries the role the cut faces end up with - the inside of the
		// throat is the cutter's own wall - and SubtractInPlace is what keeps that role intact.

		// Shared with the hole cored through the wall behind it, so the two cannot drift apart and
		// leave the masonry showing round the edge of the case. See FHFFanParams::DuctSide.
		const double CaseHalf = P.CaseHalfWidth();
		const double ThroatRadius = P.ThroatRadius();

		FHFMeshOps::AppendBox(Out.Shell, FVector3d(0.0, 0.0, P.CaseDepth * 0.5),
			FVector3d(CaseHalf, CaseHalf, P.CaseDepth * 0.5), 0.0, EHFSurfaceRole::Appliance);

		FDynamicMesh3 Throat;
		FHFMeshOps::InitialiseMesh(Throat);

		// Overshooting both faces, so the cutter never leaves coplanar surfaces for the boolean to
		// resolve - the same reason a wall's opening cutters overshoot.
		FHFMeshOps::AppendRevolvedProfile(Throat,
			{ FVector2D(0.0, ThroatRadius), FVector2D(P.CaseDepth + 2.0, ThroatRadius) },
			FVector3d(0.0, 0.0, -1.0), FVector3d::UnitZ(), RevolveSides, EHFSurfaceRole::Appliance);

		FHFMeshOps::SubtractInPlace(Out.Shell, Throat);

		RotorZ = P.CaseDepth * 0.5;

		FHFMeshOps::AppendRevolvedProfile(Rotor,
			{
				FVector2D(-P.MotorHeight * 0.5, MotorRadius * 0.4),
				FVector2D(-P.MotorHeight * 0.2, MotorRadius),
				FVector2D(P.MotorHeight * 0.5, MotorRadius)
			},
			FVector3d::Zero(), FVector3d::UnitZ(), RevolveSides, EHFSurfaceRole::Appliance);

		Out.BladePlaneZ = RotorZ;
		AppendBlades(Rotor, P, MotorRadius * 0.85, 0.0);
	}

	if (Rotor.TriangleCount() == 0)
	{
		return Out;
	}

	FHFMeshOps::ApplyWorldScaleUVs(Out.Shell);
	FHFMeshOps::ApplyWorldScaleUVs(Rotor);

	FHFMeshPart& Part = Out.Parts.AddDefaulted_GetRef();
	Part.PartId = RotorPartId;
	Part.Mesh = MoveTemp(Rotor);
	Part.PivotTransform = FTransform(FVector(0.0, 0.0, RotorZ));
	Part.Motion.Type = EHFMotionType::Spin;
	Part.Motion.Axis = FVector::ZAxisVector;
	Part.Motion.RevolutionsPerMinute = P.RevolutionsPerMinute;

	// A ROTOR MUST NOT BE A BLENDER. Collision geometry does not spin with the render - the mesh
	// stays put and only the component's transform turns - so a blocking rotor is a blade frozen at
	// whatever angle the fan was left at: a pawn walks through the gap between two blades and hits an
	// invisible wall a few degrees later. It keeps its complex collision, so traces and editor
	// picking still hit the real blades, and blocks nothing. See EHFPartCollision::TraceOnly.
	Part.Collision = EHFPartCollision::TraceOnly;

	// Where this fan's blades are stopped as generated. Not clamped and not wrapped, exactly as
	// FHFPartState::SpinTurns is not: a phase that keeps counting is the whole difference between a
	// part that revolves and one that opens.
	Part.DefaultSpinTurns = P.PhaseTurns;

	// The array grew, so the mesh inside it has been relocated by a raw memmove and its attribute
	// set's back-pointer is aimed at the freed buffer. Nothing crashes; the next overlay operation
	// just reads whatever is there now.
	FHFMeshOps::AdoptAttributes(Out.Parts.Last().Mesh);

	Out.bValid = true;
	return Out;
}
