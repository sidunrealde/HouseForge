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
	 * How far a flange reaches past the CORNER of the square hole it covers.
	 *
	 * Past the corner, not past the side. A cored duct is square and its corners reach the
	 * half-diagonal; a plate sized on the side alone leaves four bright wedges of cut masonry where
	 * a fitter would see a plate. Used by the room-side bezel and by the cowl on the far face, so
	 * the two cannot be given different answers to the same question.
	 */
	constexpr double BezelLap = 0.8;

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

	/** A square, centred on the axis, as a closed loop. */
	TArray<FVector2D> SquareLoop(double HalfSide)
	{
		return {
			FVector2D(-HalfSide, -HalfSide), FVector2D(HalfSide, -HalfSide),
			FVector2D(HalfSide, HalfSide), FVector2D(-HalfSide, HalfSide)
		};
	}

	/**
	 * A circle, centred on the axis, as a closed loop wound the same way as SquareLoop.
	 *
	 * The throat, as a HOLE IN A PROFILE rather than as a cylinder subtracted afterwards. The
	 * boolean was the right tool while the case was one solid box standing on the wall; it is the
	 * wrong one now that the case is a body plus a bezel, because two overlapping solids are not
	 * valid input to a mesh boolean and it simply declined - "computed=0, target left uncut", twelve
	 * times per house build, leaving every extract in the flat a solid block with a fan sealed
	 * inside it. Extruding the annulus cannot fail, is watertight by construction, and carries its
	 * roles without a boolean having to preserve them.
	 */
	TArray<FVector2D> RoundLoop(double Radius)
	{
		TArray<FVector2D> Loop;
		Loop.Reserve(RevolveSides);

		for (int32 Index = 0; Index < RevolveSides; ++Index)
		{
			const double Angle = 2.0 * UE_DOUBLE_PI * Index / RevolveSides;
			Loop.Add(FVector2D(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius));
		}

		return Loop;
	}

	/**
	 * What the far side of the wall sees: a sleeve through the masonry and a louvred cowl over it.
	 *
	 * AN EXTRACT HAS TWO SIDES, and only one of them was built. The duct was cored and then left as a
	 * bare square opening in a finished wall - the only opening in the flat with no lining, where
	 * every door and window gets a frame from AHFOpeningActor. From the corridor it read as a raw
	 * 15 cm hole at head height with the impeller turning inside it and the blade tips clipped by the
	 * masonry; on an external wall the same hole opened straight to the sky.
	 *
	 * The duct is deliberately kept out of Spec.Openings, correctly, so that no ventilator sash is
	 * built in it - which is exactly why it gets nothing from the opening system and why the
	 * treatment belongs to the FAN. It is a property of the extract, not of the wall.
	 *
	 * Built in the fan's own frame, where +Z is the axis into the room, so the wall occupies
	 * -HostWallThickness..0 and the discharge face is at -HostWallThickness. Two closed solids, both
	 * of them square annuli: nothing here needs a boolean.
	 */
	void AppendDischargeSide(FDynamicMesh3& Shell, const FHFFanParams& P)
	{
		const double Thickness = P.HostWallThickness;
		const double DuctHalf = P.DuctSide() * 0.5;

		// Sheet metal, so thin. The bore has to stay wide enough to be a duct rather than a slot.
		const double Sleeve = FMath::Clamp(DuctHalf * 0.08, 0.15, 0.6);
		const double BoreHalf = DuctHalf - Sleeve;

		if (BoreHalf <= MinStock || Thickness <= MinStock)
		{
			return;
		}

		// ---------------------------------------------------------------- the sleeve through the wall
		//
		// Lines the cored hole so a glance into the duct from either side finds a spigot rather than
		// the cut face of the masonry. Held a hair inside the hole so the two surfaces are never
		// coincident - two faces in the same plane flicker under any camera move, which is the sort
		// of thing only a walkthrough ever shows.
		//
		// FROM THE FAR FACE TO THE BACK OF THE CASE, not the full thickness. The case is housed in
		// this same hole now, so a sleeve run to the room face would be a second box inside the
		// first for the case's whole depth - four coplanar pairs, and a flicker exactly where the
		// room can see it through the throat.
		const double SleeveFront = -P.HousedDepth();
		if (SleeveFront - -Thickness > MinStock)
		{
			FHFMeshOps::AppendPrismWithHoles(Shell, SquareLoop(DuctHalf - 0.05), { SquareLoop(BoreHalf) },
				-Thickness, SleeveFront, EHFSurfaceRole::MetalHardware);
		}

		// ------------------------------------------------------------------------ the cowl outside it
		//
		// A flanged frame standing proud of the discharge face. The flange is what covers the arris of
		// the cored hole - the same job the bezel does on the room side, and sized the same way: past
		// the hole's CORNERS, which is what a 1.18 multiple of the half-side did not do.
		const double FlangeHalf = DuctHalf * UE_DOUBLE_SQRT_2 + BezelLap;
		const double CowlDepth = FMath::Max(DuctHalf * 0.22, 1.2);

		FHFMeshOps::AppendPrismWithHoles(Shell, SquareLoop(FlangeHalf), { SquareLoop(BoreHalf) },
			-Thickness - CowlDepth, -Thickness, EHFSurfaceRole::MetalHardware);

		// ----------------------------------------------------------------------------- and its louvres
		//
		// Weather blades across the mouth, set so they shed water and so that nothing looking at the
		// cowl straight on sees through to the impeller. They are why AHFFanActor::PlacementFor pins
		// the extract's roll to world up: local X is horizontal by construction, so these lie flat
		// rather than at whatever angle a bare MakeFromZ happened to produce.
		constexpr int32 LouvreCount = 4;
		const double Pitch = (BoreHalf * 2.0) / LouvreCount;

		// Overlapped in plan, or the gaps between them are a straight line of sight through the cowl.
		const double Chord = Pitch * 1.45;
		const double Blade = FMath::Clamp(Sleeve * 0.7, 0.12, 0.4);

		for (int32 Index = 0; Index < LouvreCount; ++Index)
		{
			FDynamicMesh3 Louvre;
			FHFMeshOps::InitialiseMesh(Louvre);

			FHFMeshOps::AppendBox(Louvre, FVector3d::Zero(),
				FVector3d(BoreHalf * 0.98, Chord * 0.5, Blade * 0.5), 0.0, EHFSurfaceRole::MetalHardware);

			// Tilted about their own long axis, which is local X.
			RotateAboutOrigin(Louvre, FVector3d::UnitX(), -35.0);

			MeshTransforms::Translate(Louvre, FVector3d(0.0,
				-BoreHalf + Pitch * (Index + 0.5),
				-Thickness - CowlDepth * 0.55));

			// Never the raw append: it renumbers polygroups, and the polygroup IS the surface role.
			FHFMeshOps::AppendPreservingRoles(Shell, Louvre);
		}
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

double FHFFanParams::ThroatRadius() const
{
	return FMath::Max(SweepRadius() * 1.05, MotorDiameter * 0.5 + MinStock);
}

double FHFFanParams::CaseBodyHalfWidth() const
{
	// The bore plus the sheet it is pressed from. What the body has to contain is the impeller, so
	// this is derived from the throat and not from the bezel - which is the whole difference between
	// a case that goes INTO the hole and one that sits over it.
	return ThroatRadius() + FMath::Max(ThroatRadius() * 0.06, MinStock);
}

double FHFFanParams::DuctSide() const
{
	// THE HOLE NOW HAS TO SWALLOW THE FAN, not merely pass its air.
	//
	// It used to be min(SweepDiameter, inscribed-in-the-case), which made a hole NARROWER than the
	// blades - correct while the impeller turned in the room in front of the wall, and impossible
	// once it turns inside the masonry. A 22 cm sweep was cored a 17.5 cm hole; the blade tips had
	// 2 cm of brick either side of them.
	//
	// So it is set out from the body it houses, with a fitting gap round it, and the bezel is sized
	// from this rather than the other way about.
	constexpr double FittingGap = 0.4;

	return FMath::Max(CaseBodyHalfWidth() * 2.0 + FittingGap, 0.0);
}

double FHFFanParams::CaseHalfWidth() const
{
	// COVERS THE HOLE'S CORNERS, not merely its sides. The chase is square and a square reaches its
	// HALF-DIAGONAL, so a bezel sized on the side alone leaves four wedges of raw cut masonry
	// showing at the corners - the same trap CanopyRadius is written against on the ceiling side.
	//
	// Floored at the old case width so a bezel is never smaller than the box it replaced.
	return FMath::Max(DuctSide() * 0.5 * UE_DOUBLE_SQRT_2 + BezelLap, SweepRadius() * 1.25);
}

double FHFFanParams::HousedDepth() const
{
	// Masonry left behind the body so the case can never break the far face of the wall.
	constexpr double MinBackReveal = 1.0;

	const double WhatTheWallCanTake = FMath::Max(0.0, HostWallThickness - MinBackReveal);

	return FMath::Clamp(FMath::Max(CaseDepth, 0.0), 0.0, WhatTheWallCanTake);
}

double FHFFanParams::ProudDepth() const
{
	return FMath::Max(0.0, FMath::Max(CaseDepth, 0.0) - HousedDepth());
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
		// The extract's reach INTO THE ROOM, which is now the bezel and whatever the wall could not
		// swallow - not the whole case, which is behind the plaster. Everything that asks a fan how
		// much room it needs is asking about the room, so this is the figure that had to change.
		: FMath::Max(ProudDepth() + FMath::Max(BezelProud, 0.0), 0.0);
}

bool FHFFanParams::IsValid() const
{
	if (SweepDiameter <= 0.0 || MotorDiameter <= 0.0)
	{
		return false;
	}

	// An extract is measured by its BODY, not by how far it reaches into the room: a fan housed
	// flush, with the bezel dialled to nothing, is a buildable fan and OverallDepth is legitimately
	// zero for it. Asking OverallDepth here - which it used to - made "fits the wall perfectly" and
	// "describes nothing" the same answer.
	return Kind == EHFFanKind::Ceiling ? OverallDepth() > 0.0 : CaseDepth > 0.0;
}

FHFFanParams FHFFanKit::DefaultsFor(EHFFanKind Kind)
{
	FHFFanParams P;
	P.Kind = Kind;

	if (Kind == EHFFanKind::Exhaust)
	{
		// A bathroom extract: small, fast, many blades, and set into the wall rather than hung off it.
		//
		// 10 rather than 12, so the body houses whole in a 115 mm internal wall with a centimetre of
		// masonry behind it. CaseDepth is the body now, and the body is inside the wall.
		P.SweepDiameter = 22.0;
		P.CaseDepth = 10.0;
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
	P.HostWallThickness = FMath::Max(P.HostWallThickness, 0.0);

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
		// -------------------------------------------------------------- an extract, IN the wall
		//
		// THE DATUM IS THE FINISHED PLASTER AND THE CASE IS BEHIND IT. What used to be here built the
		// case at 0..+CaseDepth - twelve centimetres of box standing in the bathroom with the
		// impeller turning in the open air of the room, which is what the flat was reported for. A
		// real extract is a body pushed into a cored hole with a flange over the arris; the only
		// thing in the room is that flange.
		//
		// So the body occupies -HousedDepth..+ProudDepth, which on every wall in the reference flat
		// is -CaseDepth..0, and the bezel occupies +ProudDepth..+ProudDepth+BezelProud. See
		// FHFFanParams::HousedDepth for the wall too thin to take it.
		//
		// The aperture is subtracted rather than triangulated as an annulus because the tool carries
		// the role the cut faces end up with - the inside of the throat is the cutter's own wall -
		// and SubtractInPlace is what keeps that role intact.
		const double BezelHalf = P.CaseHalfWidth();
		const double BodyHalf = P.CaseBodyHalfWidth();
		const double ThroatRadius = P.ThroatRadius();

		const double Housed = P.HousedDepth();
		const double Proud = P.ProudDepth();

		// The body, sunk into the cored hole. Its front face is the plaster line when the wall took
		// the whole of it.
		const double BodyBack = -Housed;
		const double BodyFront = Proud;

		// THE BEZEL BITES INTO THE WALL rather than sitting on its face. A plate whose back is exactly
		// in the plaster plane is two coplanar surfaces with nothing between them, which flickers
		// under any camera move - the defect the whole flat was reported for. Three millimetres of
		// embedment costs nothing and cannot fight.
		constexpr double BezelBite = 0.3;

		const double BezelBack = BodyFront - BezelBite;

		// The case as an extruded annulus, NOT a box with a cylinder booleaned out of it. See
		// RoundLoop: the old subtraction had one solid to work on and now has two, and it declines
		// rather than failing loudly.
		if (BezelBack - BodyBack > MinStock)
		{
			FHFMeshOps::AppendPrismWithHoles(Out.Shell, SquareLoop(BodyHalf), { RoundLoop(ThroatRadius) },
				BodyBack, BezelBack, EHFSurfaceRole::Appliance);
		}

		// The bezel over it: past the CORNERS of the cored hole, so the chase's arris and the fitting
		// gap round the body are both covered.
		if (P.BezelProud > MinStock)
		{
			FHFMeshOps::AppendPrismWithHoles(Out.Shell, SquareLoop(BezelHalf), { RoundLoop(ThroatRadius) },
				BezelBack, BodyFront + P.BezelProud, EHFSurfaceRole::Appliance);
		}

		// AND WHAT THE OTHER SIDE OF THE WALL SEES.
		if (P.HostWallThickness > MinStock)
		{
			AppendDischargeSide(Out.Shell, P);
		}

		// Mid-body, which is now mid-WALL. The impeller is inside the masonry where it belongs, and
		// the bezel's aperture is what you see it through.
		RotorZ = (BodyBack + BodyFront) * 0.5;

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
