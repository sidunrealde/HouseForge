// Copyright Siddartha G. All Rights Reserved.

#include "Geometry/HFUpholsteryKit.h"

#include "Geometry/HFMeshOps.h"

using namespace UE::Geometry;

namespace
{
	/** Below this a cushion is a sheet of fabric and a leg is a pencil line. */
	constexpr double MinSolid = 0.2;

	/**
	 * How far a leg runs up INTO the base, and a back panel into the arms it dies against.
	 *
	 * FHFFrameKit's rule, and it applies to soft goods for the same reason: two solids that TOUCH
	 * share a plane, and two coplanar faces at the join flicker against each other from any distance.
	 * So members run into what they land on. The overlap is inside the merged shell where nothing can
	 * see it, and the sub-assemblies are kept separately precisely so the clearances that DO matter -
	 * cushion to cushion, arm to base - stay measurable without it.
	 */
	constexpr double Lap = 2.0;

	/**
	 * Holds a soft box's radii off AppendSoftBox's ONE singular case, which is not a small thing.
	 *
	 * ## The case
	 *
	 * AppendSoftBox lofts rounded-rectangle rings. RoundedRectangle puts its corner arc centres at
	 * HalfExtents - Radius, so when the plan radius reaches HALF the smaller plan dimension the
	 * straight segment between two corners has zero length and the arcs meet: consecutive vertices
	 * coincide, the band between two rings becomes a fan of zero-area triangles, and their absent
	 * normals get averaged into the real ones. It renders as a hard crease with a black wedge behind
	 * it and it measures as a 180 degree dihedral and a metre of concavity.
	 *
	 * ## Why the kit has to be the one to prevent it
	 *
	 * AppendSoftBox already raises the plan radius above both rolls, so that a corner never has to
	 * converge on a point - and its own two ceilings are 0.45 of the section for a roll and 0.05 of it
	 * for the lift. Those add to exactly 0.50, so ANY caller whose roll reaches the roll ceiling comes
	 * out with a plan radius sitting precisely on the singular value. The primitive cannot fix that
	 * without either lowering its roll ceiling or refusing to keep the plan radius above the roll,
	 * and both would change every soft form in the flat.
	 *
	 * So the caller stays off it. 0.44 of the section for a roll leaves 0.49 after the lift, and the
	 * plan radius is capped at 0.49 as well: a straight segment 1% of the section long, which on a
	 * 240 mm arm is 2.4 mm and is a real edge rather than a coincident pair.
	 *
	 * FOUND BY ASKING FOR A HALF-ROUND ARM. Every soft form in the flat before that sat comfortably
	 * inside the bound by accident - a 70 mm roll on a 180 arm is 0.39 of the section - so the case
	 * was reachable, unguarded, and had never been reached. The two that were closest were a back
	 * cushion, at 0.45 exactly, and a back panel, also at 0.45; both fell over the moment
	 * WorstConcavityCm was pointed at them, and neither had ever been measured.
	 */
	void ClampSoftToBox(FHFSoftBoxParams& Soft, const FVector3d& Min, const FVector3d& Max)
	{
		const FVector3d Size = Max - Min;

		// The rake costs depth rather than adding it, so the section a roll turns round is the SLAB,
		// exactly as AppendSoftBox measures it. Taken off the declared depth instead, a raked back
		// cushion would be cleared against a section 50 mm wider than the one it is built in.
		const double SlabY = FMath::Max(Size.Y - FMath::Clamp(Soft.RakeY, 0.0, Size.Y * 0.9), 0.0);
		const double Section = FMath::Min(Size.X, SlabY);

		Soft.TopRadius = FMath::Min(Soft.TopRadius, Section * 0.44);
		Soft.BottomRadius = FMath::Min(Soft.BottomRadius, Section * 0.44);
		Soft.CornerRadius = FMath::Min(Soft.CornerRadius, Section * 0.49);
	}

	/** A soft box, skipped rather than degenerate. Mirrors AppendSolid in the bed kit. */
	void AppendSoft(FDynamicMesh3& Mesh, const FVector3d& Min, const FVector3d& Max,
		FHFSoftBoxParams Soft, EHFSurfaceRole Role)
	{
		const FVector3d Size = Max - Min;
		if (Size.X <= MinSolid || Size.Y <= MinSolid || Size.Z <= MinSolid)
		{
			return;
		}

		ClampSoftToBox(Soft, Min, Max);

		FHFMeshOps::AppendSoftBox(Mesh, Min, Max, Soft, Role);
	}
}

FHFSofaParams FHFUpholsteryKit::FiguresFor(EHFSofaDesign Design)
{
	// The struct's own defaults ARE SquareArm, figure for figure. That is not a coincidence and it is
	// load-bearing: it is what makes a spec written before designs existed - and a project that never
	// opens the settings page - come out with exactly the sofa this kit built beforehand.
	FHFSofaParams P;

	P.Design = (Design == EHFSofaDesign::Default) ? EHFSofaDesign::SquareArm : Design;

	switch (P.Design)
	{
	case EHFSofaDesign::ChaiseSectional:
		// THE STRAIGHT RUN IS A SQUARE-ARM SOFA. Everything that differs is in plan: a 1400 drawn
		// depth instead of 900, and a 900 wide return taking the 500 in front of the main run. The
		// elevation is deliberately the same object, because that is what a sectional is - the
		// manufacturer does not redesign the arm when they add a chaise to it.
		P.Depth = 140.0;
		P.ChaiseWidth = 90.0;
		break;

	case EHFSofaDesign::LowProfile:
		// LOW, LONG, AND UP IN THE AIR. Three figures do the work and none of them is the width: a
		// 400 seat instead of 430, a 700 back instead of 800, and 180 mm of splayed tapered leg
		// instead of 120 mm of turned one. The last is the one that changes the room - half a metre
		// more floor and skirting shows behind the seating from any standing eye height.
		//
		// The seat gets DEEPER as the back gets lower, which is the trade a lounge sofa makes: 950
		// overall buys a 625 seat, and a 625 seat is something to sit back into rather than up on.
		P.Depth = 95.0;
		P.Height = 70.0;

		P.SeatHeight = 40.0;
		P.SeatCushionThickness = 16.0;

		// SLIM ARMS, and low. An 180 mm track arm on a 700 sofa is a third of its whole elevation.
		P.ArmHeight = 55.0;
		P.ArmWidth = 12.0;
		P.ArmRoll = 5.0;

		P.BackThickness = 12.0;
		P.BackCushionThickness = 14.0;

		// More lean than the others: there is less back to lean, so it has to lean harder to read.
		P.BackRake = 5.0;

		// A deeper shadow under the deck, because there is more light under this sofa to shadow with.
		P.BaseInset = 3.0;

		P.LegHeight = 18.0;
		P.LegDiameter = 5.0;
		P.LegInset = 11.0;

		// THE SPLAY AND THE TAPER TOGETHER, or neither is worth having. A vertical tapered post is a
		// table leg; a raked cylinder is a stool leg. Twelve degrees of rake on a cone that closes to
		// a third of its head is the mid-century leg, and it is why this design reads as furniture
		// standing on the floor rather than as a box parked on it.
		P.LegSplayDegrees = 12.0;
		P.LegFootTaper = 0.32;
		P.bLegFootFlare = false;

		P.CushionRoll = 5.0;
		break;

	case EHFSofaDesign::RolledArm:
		// THE ARM IS THE DESIGN, and it is a FAT arm before it is a round one: 240 against a square
		// arm's 180. ArmRoll then takes almost the whole half-width, so the top of it is a half-round
		// in section rather than a square with the corners taken off - 30 mm of flat across a 240 arm,
		// where a square arm leaves 40 across 180. See the Chesterfield rejection in HFUpholsteryKit.h
		// for what is deliberately NOT attempted here, and ClampSoftToBox for why the roll stops at
		// 0.44 of the section rather than at 0.5 - half is the one value that makes the plan corner
		// degenerate, and this design is the first thing in the flat ever to ask for it.
		//
		// THE WIDTH DOES NOT SHRINK TO PAY FOR THE ARMS. At 2000 overall, 240 arms leave 1520 of clear
		// width and three 493 cushions, which is under the 500 anybody sells. It keeps the 2100 a
		// three-seater is drawn at and the room already holds.
		P.Width = 210.0;
		P.Depth = 95.0;
		P.Height = 85.0;

		// A traditional sofa sits you UP, not back: 450 to the seat against a contemporary 430.
		P.SeatHeight = 45.0;
		P.SeatCushionThickness = 15.0;

		P.ArmHeight = 66.0;
		P.ArmWidth = 24.0;
		P.ArmRoll = 10.5;

		P.BackThickness = 16.0;
		P.BackCushionThickness = 16.0;
		P.BackRake = 4.0;

		// Plumper cushions with a wider shadow between them. A roll arm's cushions are filled, not
		// foam blocks, so their edges turn over further and they sit further apart.
		P.CushionGap = 2.0;
		P.CushionRoll = 6.0;
		P.BaseInset = 2.5;

		// AND NOTHING TOUCHES THE FLOOR EXCEPT CLOTH. The skirt closes the 120 mm band of daylight
		// every other design here leaves open, which is the single largest silhouette change of the
		// four - a skirted sofa sits ON the floor where the other three hover over it.
		P.LegHeight = 12.0;
		P.LegDiameter = 0.0;
		P.bHasSkirt = true;
		break;

	case EHFSofaDesign::SquareArm:
	default:
		break;
	}

	return P;
}

FHFSofaParams FHFUpholsteryKit::SanitiseSofa(const FHFSofaParams& Params)
{
	FHFSofaParams P = Params;

	// DEFAULT IS "ASK THE PROJECT", AND THE PROJECT IS THE ONE THING A GENERATOR MAY NOT ASK. Reading
	// a settings object here would put world state inside a pure function - see
	// .claude/rules/04-conventions.md - so a Default that got this far is treated as the design the
	// kit's own figures already describe, and the resolving happens in the composing layer.
	if (P.Design == EHFSofaDesign::Default)
	{
		P.Design = EHFSofaDesign::SquareArm;
	}

	P.Width = FMath::Max(P.Width, 0.0);
	P.Depth = FMath::Max(P.Depth, 0.0);
	P.Height = FMath::Max(P.Height, 0.0);
	P.SeatCount = FMath::Clamp(P.SeatCount, 1, 6);

	// TWO ARMS CANNOT BE THE WHOLE SOFA. Clamped rather than refused, for the reason the bed kit
	// clamps a headboard: a drawing that gave a three-seater 400 mm of width has made a units
	// mistake, and the honest response is the widest arms that leave a seat rather than no sofa.
	P.ArmWidth = FMath::Clamp(P.ArmWidth, 0.0, P.Width * 0.25);
	P.BaseInset = FMath::Clamp(P.BaseInset, 0.0, FMath::Min(P.Width, P.Depth) * 0.05);

	// ------------------------------------------------------------------------------- the chaise
	//
	// FIRST, BECAUSE EVERYTHING BELOW MEASURES THE STRAIGHT RUN AND THE CHAISE DECIDES HOW DEEP IT IS.
	// Ordered any other way, a sectional's back would have been budgeted against the whole 1400 box
	// instead of against the 900 run it actually sits in, and a 14 cm back panel would have been
	// allowed to become 30 - which is a slab across the back of the sofa, not a panel.
	P.MinMainRunDepth = FMath::Clamp(P.MinMainRunDepth, 0.0, P.Depth);
	P.MinChaiseProjection = FMath::Max(P.MinChaiseProjection, 0.0);
	P.ChaiseProjection = FMath::Max(P.ChaiseProjection, 0.0);
	P.ChaiseWidth = FMath::Max(P.ChaiseWidth, 0.0);

	// Resolved rather than left derived, so Used carries the figure that was really built and a
	// caller does not have to re-run the derivation to find out whether it got an L at all.
	P.ChaiseProjection = P.BuiltChaiseProjection();

	// The back panel, the back cushion and its lean all come out of the STRAIGHT RUN's depth, and
	// between them they may not take the seat. Two thirds is the limit: at that point the seat is
	// 300 mm deep.
	const double BackBudget = P.MainRunDepth() * 0.66;
	const double BackAsked = P.BackThickness + P.BackCushionThickness + P.BackRake;
	if (BackAsked > BackBudget && BackAsked > 0.0)
	{
		const double Scale = BackBudget / BackAsked;
		P.BackThickness *= Scale;
		P.BackCushionThickness *= Scale;
		P.BackRake *= Scale;
	}

	P.BackThickness = FMath::Max(P.BackThickness, 0.0);
	P.BackCushionThickness = FMath::Max(P.BackCushionThickness, 0.0);
	P.BackRake = FMath::Max(P.BackRake, 0.0);

	// A seat above the arms is a bench, and arms above the back are a different object altogether.
	// Ordered outward from the seat because the seat is the figure a drawing actually states.
	P.SeatHeight = FMath::Clamp(P.SeatHeight, 0.0, P.Height * 0.75);
	P.ArmHeight = FMath::Clamp(P.ArmHeight, P.SeatHeight + MinSolid, FMath::Max(P.Height - MinSolid, 0.0));

	P.SeatCushionThickness = FMath::Clamp(P.SeatCushionThickness, 0.0, P.SeatHeight * 0.6);

	// The base has to stand on the legs and still be a base, so the legs cannot reach the deck.
	P.LegHeight = FMath::Clamp(P.LegHeight, 0.0, FMath::Max(P.DeckZ() - MinSolid, 0.0));
	P.LegDiameter = FMath::Max(P.LegDiameter, 0.0);
	P.LegInset = FMath::Clamp(P.LegInset, P.LegDiameter * 0.5,
		FMath::Max(FMath::Min(P.Width, P.Depth) * 0.5 - P.LegDiameter * 0.5, P.LegDiameter * 0.5));

	// A SPLAYED LEG'S FOOT MAY NOT LEAVE THE DRAWN BOX, and this is the clamp that keeps the
	// footprint checkable. The head stays under the corner of the base; the foot lands
	// LegHeight * tan(splay) further out, so the splay a leg may have is decided by how far in it
	// already stands. Twelve degrees on an 180 leg is 38 mm, against an inset of 110 - comfortable -
	// but the same twelve degrees on a 60 mm inset would put the toe 15 mm outside the sofa, and a
	// footprint that the drawing cannot be checked against is the failure this whole kit measures on.
	P.LegFootTaper = FMath::Clamp(P.LegFootTaper, 0.05, 1.0);
	P.LegSplayDegrees = FMath::Clamp(P.LegSplayDegrees, 0.0, 30.0);
	{
		const double MaxOffset = FMath::Max(P.LegInset - P.LegDiameter * 0.5 * P.LegFootTaper, 0.0);
		if (P.LegHeight > MinSolid && P.LegSplayOffset() > MaxOffset)
		{
			P.LegSplayDegrees = FMath::RadiansToDegrees(FMath::Atan2(MaxOffset, P.LegHeight));
		}
	}

	// The skirt hangs from the underside of the base to just off the floor, so it cannot start below
	// where it ends, and it stands inside the drawn faces so the arms above still oversail it.
	P.SkirtGroundClearance = FMath::Clamp(P.SkirtGroundClearance, 0.0,
		FMath::Max(P.LegHeight - MinSolid, 0.0));
	P.SkirtSetback = FMath::Clamp(P.SkirtSetback, 0.0, FMath::Min(P.Width, P.Depth) * 0.1);
	P.SkirtThickness = FMath::Clamp(P.SkirtThickness, 0.1,
		FMath::Max(FMath::Min(P.Width, P.Depth) * 0.1, 0.1));

	// EVERY GAP HAS TO COME OUT OF THE CLEAR WIDTH AND LEAVE CUSHIONS BEHIND. There is one more gap
	// than there are seats - one either side of the run as well as between each pair - so the whole
	// set is clamped to half the clear width and the cushions get the rest.
	//
	// InnerWidth is the STRAIGHT RUN's clear width, which on an L is what is left after the chaise
	// has taken its end. A sectional's cushions divide up what remains, not the whole box.
	P.CushionGap = FMath::Clamp(P.CushionGap, 0.0,
		P.InnerWidth() * 0.5 / FMath::Max(P.SeatCount + 1, 1));

	// THE BACK CUSHION IS WHAT TAKES UP THE DIFFERENCE BETWEEN THE ERGONOMICS AND THE DRAWING.
	//
	// Seat height and arm height belong to the design, because they are set by the human sitting
	// there rather than by the box the drawing put round the object; the top of the back is whatever
	// the drawing says the sofa is. So the cushion is derived to leave BackPanelShow of bare panel
	// above it, and a design can state its proportions without knowing what height it will be drawn
	// at. Left absolute, naming LowProfile on a box drawn 900 tall would have given a 260 cushion
	// under 210 mm of blank upholstered panel - a sofa with a headboard.
	//
	// A stated figure still wins. Zero is the sentinel, exactly as it is on FHFFalseCeiling::InnerDrop.
	P.BackPanelShow = FMath::Max(P.BackPanelShow, 0.0);
	if (P.BackCushionHeight <= 0.0)
	{
		P.BackCushionHeight = FMath::Max(P.Height - P.SeatHeight - P.BackPanelShow, 0.0);
	}

	// What shows above the cushions is the panel; a cushion as tall as its own back leaves none.
	P.BackCushionHeight = FMath::Clamp(P.BackCushionHeight, 0.0,
		FMath::Max(P.Height - P.SeatHeight - MinSolid, 0.0));

	P.ArmRoll = FMath::Max(P.ArmRoll, 0.0);
	P.CushionRoll = FMath::Max(P.CushionRoll, 0.0);

	return P;
}

FHFSofaBuild FHFUpholsteryKit::BuildSofa(const FHFSofaParams& Params)
{
	FHFSofaBuild Out;

	FHFMeshOps::InitialiseMesh(Out.Shell);
	FHFMeshOps::InitialiseMesh(Out.Legs);
	FHFMeshOps::InitialiseMesh(Out.Skirt);
	FHFMeshOps::InitialiseMesh(Out.Base);
	FHFMeshOps::InitialiseMesh(Out.Back);
	FHFMeshOps::InitialiseMesh(Out.ChaiseCushion);
	FHFMeshOps::InitialiseMesh(Out.ChaiseBackCushion);

	const FHFSofaParams P = SanitiseSofa(Params);
	Out.Used = P;

	if (!P.IsValid())
	{
		return Out;
	}

	const double DeckZ = P.DeckZ();
	const double InnerX0 = P.InnerX0();
	const double InnerX1 = P.InnerX1();
	const double ArmInnerX0 = P.ArmInnerX0();
	const double ArmInnerX1 = P.ArmInnerX1();
	const double BackFaceY = P.BackFaceY();

	// THE STRAIGHT RUN'S FRONT, which is zero on three of the four designs and the whole reason the
	// fourth is a different object. Every form below that belongs to the run is set out from here
	// rather than from the front of the drawn box, so an L is built by moving one line.
	const double MainY0 = P.MainRunFrontY();

	// The chaise's plan extent, INCLUDING the arm that runs down its outboard side. Only the seat
	// stops at ChaiseSeatX*; the base and the leg under it go out to the drawn face.
	const double ChaiseBaseX0 = P.bChaiseOnLeft ? P.BaseInset : P.ChaiseSeatX0();
	const double ChaiseBaseX1 = P.bChaiseOnLeft ? P.ChaiseSeatX1() : P.Width - P.BaseInset;

	// ------------------------------------------------------------------------------------- legs
	//
	// THE ONLY HARD MATERIAL ON THE OBJECT, and the thing that stops a sofa reading as built-in
	// joinery. A 120 mm gap of daylight under a sofa is most of what separates loose furniture from a
	// bench: without it the base meets the floor in an unbroken line and the whole piece looks
	// scribed in, which is precisely what a sofa is not.
	//
	// Turned and tapered rather than square, because a revolve welds smooth under
	// ComputeShadingNormals and a 60 mm square leg is four more sharp arrises at exactly the height
	// the eye follows the floor line.
	//
	// NONE AT ALL UNDER A SKIRT, and that is the point of a skirt. Four solids behind an opaque
	// valance on all four sides are triangles nobody will ever see, in every render, for the life of
	// the project - see FHFSofaParams::bHasSkirt.
	if (!P.bHasSkirt && P.LegDiameter > MinSolid && P.LegHeight > MinSolid)
	{
		const double R = P.LegDiameter * 0.5;

		// THE FOOT MOVES, NOT THE HEAD. A splayed leg's top stays under the corner of the base it
		// carries and its foot lands outboard; done the other way round the base would have to grow to
		// meet the legs and the drawn footprint would stop meaning anything.
		const double Splay = P.LegSplayOffset();
		const double AxisLength = FMath::Sqrt(FMath::Square(P.LegHeight) + 2.0 * FMath::Square(Splay));
		const double Scale = P.LegHeight > 0.0 ? AxisLength / P.LegHeight : 1.0;
		const double LegTopAlongAxis = (P.LegHeight + Lap) * Scale;

		// A TILTED RING IS NOT A FLAT FOOT. AppendRevolvedProfile lays every ring perpendicular to the
		// axis, so a raked leg's bottom ring is raked with it and its low side dips BELOW the floor -
		// 2.2 mm on the low-profile design, which is four legs through the tiles and a drawn box that
		// no longer starts at zero. Lifted by exactly the dip, so the leg meets the floor on its rim.
		//
		// A real splayed leg is cut flat at the foot instead. That needs an angled cap the revolve
		// cannot make, and on an 8 mm toe the difference is not visible from anywhere.
		const double SinTilt = AxisLength > 0.0 ? (UE_DOUBLE_SQRT_2 * Splay) / AxisLength : 0.0;
		const double FootLift = R * P.LegFootTaper * SinTilt;

		TArray<FVector2D> Profile;
		Profile.Add(FVector2D(0.0, R * P.LegFootTaper));
		if (P.bLegFootFlare)
		{
			// The ankle of a turned leg. A machined taper does not have one, and putting it there is
			// the difference between mid-century and reproduction.
			Profile.Add(FVector2D(FMath::Min(0.8, P.LegHeight * 0.2) * Scale,
				R * FMath::Min(P.LegFootTaper + 0.15, 1.0)));
		}
		Profile.Add(FVector2D(LegTopAlongAxis, R));

		// Four under the straight run's own box. On an L that box is the run rather than the drawing,
		// so the pair at the chaise end land where the two runs meet - which is where a sectional
		// really does carry one.
		TArray<FVector2D> Heads;
		for (const double X : { P.LegInset, P.Width - P.LegInset })
		{
			for (const double Y : { MainY0 + P.LegInset, P.Depth - P.LegInset })
			{
				Heads.Add(FVector2D(X, Y));
			}
		}

		// And two more at the front corners of the return, or the chaise stands on nothing.
		if (P.IsSectional())
		{
			const double OuterX = P.bChaiseOnLeft ? P.LegInset : P.Width - P.LegInset;
			const double InnerXAtChaise = P.bChaiseOnLeft
				? ChaiseBaseX1 - P.LegInset : ChaiseBaseX0 + P.LegInset;

			Heads.Add(FVector2D(OuterX, P.LegInset));
			Heads.Add(FVector2D(InnerXAtChaise, P.LegInset));
		}

		const FVector2D PlanCentre(P.Width * 0.5, P.Depth * 0.5);

		for (const FVector2D& Head : Heads)
		{
			// Outward is away from the plan centre, in both axes, so all four corners rake apart.
			const double DirX = Head.X >= PlanCentre.X ? 1.0 : -1.0;
			const double DirY = Head.Y >= PlanCentre.Y ? 1.0 : -1.0;

			const FVector3d Foot(Head.X + DirX * Splay, Head.Y + DirY * Splay, FootLift);
			const FVector3d Axis = (FVector3d(Head.X, Head.Y, P.LegHeight) - Foot).GetSafeNormal();

			FHFMeshOps::AppendRevolvedProfile(Out.Legs, Profile, Foot, Axis, 12,
				EHFSurfaceRole::JoineryCarcass);
		}

		FHFMeshOps::ApplyWorldScaleUVs(Out.Legs);
	}

	// ------------------------------------------------------------------------------------ skirt
	//
	// A fabric valance closing the gap the legs would have left open. Four panels rather than a solid
	// block: a skirt is cloth over a light frame with nothing behind it, and a filled plinth would
	// read as a block of foam wherever the light gets under the arm.
	//
	// Straight sofas only, and the guard is honest rather than lazy: an L's skirt is a six-sided run
	// and no design here combines the two. A sectional that asked for one would get its return
	// standing on nothing, which is worse than getting no skirt.
	if (P.bHasSkirt && !P.IsSectional())
	{
		const double Z0 = P.SkirtBottomZ();
		const double Z1 = P.LegHeight + Lap;
		const double T = P.SkirtThickness;

		const double SX0 = P.SkirtSetback;
		const double SX1 = P.Width - P.SkirtSetback;
		const double SY0 = P.SkirtSetback;
		const double SY1 = P.Depth - P.SkirtSetback;

		FHFSoftBoxParams Soft;
		Soft.CornerRadius = T * 0.45;
		Soft.TopRadius = FMath::Min(0.3, T * 0.3);
		Soft.BottomRadius = FMath::Min(0.4, T * 0.4);

		const double Panels[4][4] = {
			{ SX0,     SX1,     SY0,     SY0 + T },  // front
			{ SX0,     SX1,     SY1 - T, SY1     },  // back
			{ SX0,     SX0 + T, SY0 + T, SY1 - T },  // left return
			{ SX1 - T, SX1,     SY0 + T, SY1 - T }   // right return
		};

		for (const double(&Panel)[4] : Panels)
		{
			AppendSoft(Out.Skirt,
				FVector3d(Panel[0], Panel[2], Z0),
				FVector3d(Panel[1], Panel[3], Z1),
				Soft, EHFSurfaceRole::Fabric);
		}

		FHFMeshOps::ApplyWorldScaleUVs(Out.Skirt);
	}

	// ------------------------------------------------------------------------------------- base
	//
	// Set in from the drawn box on the front and the two sides so the arms and the cushions oversail
	// it. The bed's argument exactly: what is set back lies in shadow, and the silhouette of the sofa
	// becomes its arms rather than one slab running from the floor to the seat.
	//
	// TWO BOXES ON AN L, lapped into one another. The lap is inside the solid where nothing can see
	// it, and it is what stops the two plinths presenting a pair of coincident faces at the corner -
	// the flashing FHFCoplanarScan exists to catch.
	{
		FHFSoftBoxParams Soft;
		Soft.BottomRadius = 2.0;
		Soft.TopRadius = 1.0;
		Soft.CornerRadius = Soft.BottomRadius;

		AppendSoft(Out.Base,
			FVector3d(P.BaseInset, MainY0 + P.BaseInset, P.LegHeight),
			FVector3d(P.Width - P.BaseInset, P.Depth, DeckZ),
			Soft, EHFSurfaceRole::Fabric);

		if (P.IsSectional())
		{
			AppendSoft(Out.Base,
				FVector3d(ChaiseBaseX0, P.BaseInset, P.LegHeight),
				FVector3d(ChaiseBaseX1, MainY0 + P.BaseInset + Lap, DeckZ),
				Soft, EHFSurfaceRole::Fabric);
		}

		FHFMeshOps::ApplyWorldScaleUVs(Out.Base);
	}

	// ------------------------------------------------------------------------------------- arms
	//
	// The largest radius on the sofa and the one the light actually finds. Rolled in plan as well as
	// on top, so an arm reads as a bolster from above and in elevation rather than as a rectangle
	// with a rounded lid.
	{
		// The plan radius carries both rolls, so the corner of an arm is a sphere octant. See
		// FHFSoftBoxParams::CornerRadius - held below the roll it comes out as a flat lozenge, which on
		// the widest radius in the flat is the most conspicuous version of that defect there is.
		//
		// HALF THE ARM'S WIDTH, NOT 0.45 OF IT, and the difference is what makes a rolled arm a rolled
		// arm: at exactly half, the plan section is a stadium and the top of the arm is a half-round
		// rather than a square with the corners eased. AppendSoftBox holds its own ceiling at 0.45 of
		// the section for the roll and lifts the plan radius just above it, so the two still meet in
		// one surface - the roll comes out at 90% of a true semicircle and stays convex. Nothing
		// changes for a square arm, whose 70 mm roll on a 180 arm was never near either bound.
		FHFSoftBoxParams Soft;
		Soft.CornerRadius = FMath::Min(P.ArmRoll, P.ArmWidth * 0.5);
		Soft.TopRadius = Soft.CornerRadius;
		Soft.BottomRadius = FMath::Min(3.0, Soft.CornerRadius);

		// FINER THAN THE KIT'S DEFAULT, and this is the one place in the flat where that is worth
		// paying for. An arm's corner is a 70 mm sphere octant at eye level a metre from the camera:
		// drawn in four steps by four it shades as a diamond-shaped highlight patch, which is a
		// low-polygon tell rather than a soft form. Six by five is where it stops reading as one.
		Soft.CornerSteps = 6;
		Soft.RollSteps = 5;

		// THE CHAISE'S ARM RUNS THE WHOLE L AND THE OTHER ONE DOES NOT, which is most of what makes an
		// L read as an L in elevation: one arm 1400 long and one 900, with nothing at all on the
		// inside of the corner where the seat turns. An arm there would be a back rest facing across
		// the sofa, and the piece would be a corner unit rather than a chaise.
		const bool bLeftArmIsChaise = P.IsSectional() && P.bChaiseOnLeft;
		const bool bRightArmIsChaise = P.IsSectional() && !P.bChaiseOnLeft;

		const double ArmSpan[2][3] = {
			{ 0.0,        ArmInnerX0, bLeftArmIsChaise  ? 0.0 : MainY0 },
			{ ArmInnerX1, P.Width,    bRightArmIsChaise ? 0.0 : MainY0 }
		};

		for (const double(&Span)[3] : ArmSpan)
		{
			FDynamicMesh3 Arm;
			FHFMeshOps::InitialiseMesh(Arm);

			AppendSoft(Arm,
				FVector3d(Span[0], Span[2], P.LegHeight),
				FVector3d(Span[1], P.Depth, P.ArmHeight),
				Soft, EHFSurfaceRole::Fabric);

			FHFMeshOps::ApplyWorldScaleUVs(Arm);
			Out.Arms.Add(MoveTemp(Arm));
		}

		// After the array is final. A TArray of meshes relocates its elements with a raw Memmove, and
		// an attribute set left pointing at the freed buffer is undefined behaviour that looks right.
		// See FHFMeshOps::AdoptAttributes.
		for (FDynamicMesh3& Arm : Out.Arms)
		{
			FHFMeshOps::AdoptAttributes(Arm);
		}
	}

	// ------------------------------------------------------------------------------------- back
	//
	// Runs INTO both arms rather than up to them, and starts at the base rather than at the arm top:
	// a back panel that began where the arms end would leave a slot straight through the sofa at seat
	// height, which is exactly the failure the bed kit's headboard note describes.
	{
		FHFSoftBoxParams Soft;
		Soft.CornerRadius = FMath::Min(P.ArmRoll * 0.8, P.BackThickness * 0.45);
		Soft.TopRadius = Soft.CornerRadius;
		Soft.BottomRadius = FMath::Min(2.0, Soft.CornerRadius);

		// SPANS ARM TO ARM, not seat to seat, and on an L those are different lines: the chaise's own
		// arm reaches the back wall, so the panel runs the whole width behind the return as well as
		// behind the straight run. Taken to InnerX0/InnerX1 instead, a sectional's back would have
		// stopped where the chaise begins and left 900 mm of the sofa open to the room behind it.
		AppendSoft(Out.Back,
			FVector3d(FMath::Max(ArmInnerX0 - Lap, 0.0), BackFaceY, P.LegHeight),
			FVector3d(FMath::Min(ArmInnerX1 + Lap, P.Width), P.Depth, P.Height),
			Soft, EHFSurfaceRole::Fabric);

		FHFMeshOps::ApplyWorldScaleUVs(Out.Back);
	}

	// --------------------------------------------------------------------------------- cushions
	//
	// One seat and one back per seat, with a gap either side of every one. The gaps are the whole
	// point: without them a three-seater's seat is a single 1740 mm slab, and no amount of radius on
	// its edges makes it read as three cushions.

	const double CushionWidth = P.SeatCushionWidth();
	const double SeatBackY = P.CushionFrontY() + P.SeatCushionDepth();
	const double BackCushionY0 = P.BackCushionY0();
	const double BackCushionY1 = BackCushionY0 + P.BackCushionThickness + P.BackRake;

	// Drawn finer than the kit's default, for the reason the arms are: a cushion corner is the
	// closest soft form to the camera in the whole flat.
	FHFSoftBoxParams SeatSoft;
	SeatSoft.CornerRadius = P.CushionRoll * 1.25;
	SeatSoft.TopRadius = SeatSoft.CornerRadius;
	SeatSoft.BottomRadius = P.CushionRoll * 0.5;
	SeatSoft.CornerSteps = 6;
	SeatSoft.RollSteps = 5;

	FHFSoftBoxParams BackSoft;
	BackSoft.CornerRadius = P.CushionRoll * 1.5;
	BackSoft.TopRadius = BackSoft.CornerRadius;
	BackSoft.BottomRadius = P.CushionRoll;
	BackSoft.RakeY = P.BackRake;
	BackSoft.CornerSteps = 6;
	BackSoft.RollSteps = 5;

	for (int32 Seat = 0; Seat < P.SeatCount; ++Seat)
	{
		const double X0 = InnerX0 + P.CushionGap + Seat * (CushionWidth + P.CushionGap);
		const double X1 = X0 + CushionWidth;

		FDynamicMesh3 SeatCushion;
		FHFMeshOps::InitialiseMesh(SeatCushion);
		AppendSoft(SeatCushion,
			FVector3d(X0, P.CushionFrontY(), DeckZ),
			FVector3d(X1, SeatBackY, P.SeatHeight),
			SeatSoft, EHFSurfaceRole::Fabric);
		FHFMeshOps::ApplyWorldScaleUVs(SeatCushion);
		Out.SeatCushions.Add(MoveTemp(SeatCushion));

		FDynamicMesh3 BackCushion;
		FHFMeshOps::InitialiseMesh(BackCushion);
		AppendSoft(BackCushion,
			FVector3d(X0, BackCushionY0, P.SeatHeight),
			FVector3d(X1, BackCushionY1, P.BackCushionTopZ()),
			BackSoft, EHFSurfaceRole::Fabric);
		FHFMeshOps::ApplyWorldScaleUVs(BackCushion);
		Out.BackCushions.Add(MoveTemp(BackCushion));
	}

	for (FDynamicMesh3& Cushion : Out.SeatCushions)
	{
		FHFMeshOps::AdoptAttributes(Cushion);
	}
	for (FDynamicMesh3& Cushion : Out.BackCushions)
	{
		FHFMeshOps::AdoptAttributes(Cushion);
	}

	// ------------------------------------------------------------------------- the chaise cushion
	//
	// ONE CUSHION, AS LONG AS THE RETURN. That is the whole object: an uninterrupted 1100 mm run to
	// put your legs along, where a seat cushion is 570. Divided into seat-sized pieces with shadow
	// gaps between them it would be two more seats facing across the sofa, which is a corner unit.
	//
	// Its back edge lines up with every other seat cushion's, so the seam across the sofa reads as
	// one line rather than as a step.
	//
	// AND IT HAS A BACK CUSHION LIKE EVERY OTHER SEAT DOES. The first version of this reasoned that a
	// chaise has "nothing behind it", which confused the END of the return with its BACK: the back
	// PANEL is built arm to arm, across the chaise as well as the run, so the deck in front of that
	// panel runs the whole width too. Leaving it bare exposed a strip of upholstered deck exactly
	// BackRake + BackCushionThickness deep - 180 mm on this design - along the full 720 mm of the
	// chaise, and the render showed it as a flat tan band across the part of the sofa nearest the eye.
	// It is one cushion rather than a row of them for the same reason the seat below it is one.
	if (P.IsSectional())
	{
		// The seam between the run and the return, front to back in one line. Both cushions are inset
		// by a gap from ChaiseSeatX0, and the run's last cushion stops a gap short of it, so the joint
		// is two gaps wide where a cushion-to-cushion seam is one. That is right: this is the seam
		// between the two PIECES of a sectional, and it is wider on every one anybody sells.
		const double ChaiseX0 = P.ChaiseSeatX0() + P.CushionGap;
		const double ChaiseX1 = P.ChaiseSeatX1() - P.CushionGap;

		AppendSoft(Out.ChaiseCushion,
			FVector3d(ChaiseX0, P.ChaiseCushionFrontY(), DeckZ),
			FVector3d(ChaiseX1, SeatBackY, P.SeatHeight),
			SeatSoft, EHFSurfaceRole::Fabric);

		FHFMeshOps::ApplyWorldScaleUVs(Out.ChaiseCushion);

		// Set out from exactly the figures the run's back cushions use, so the tops line up across the
		// sofa and the lean matches. Only the X span differs, because the section it covers is wider.
		AppendSoft(Out.ChaiseBackCushion,
			FVector3d(ChaiseX0, BackCushionY0, P.SeatHeight),
			FVector3d(ChaiseX1, BackCushionY1, P.BackCushionTopZ()),
			BackSoft, EHFSurfaceRole::Fabric);

		FHFMeshOps::ApplyWorldScaleUVs(Out.ChaiseBackCushion);
	}

	// ------------------------------------------------------------------------------------ shell

	FHFMeshOps::AppendPreservingRoles(Out.Shell, Out.Legs);
	FHFMeshOps::AppendPreservingRoles(Out.Shell, Out.Skirt);
	FHFMeshOps::AppendPreservingRoles(Out.Shell, Out.Base);
	for (const FDynamicMesh3& Arm : Out.Arms)
	{
		FHFMeshOps::AppendPreservingRoles(Out.Shell, Arm);
	}
	FHFMeshOps::AppendPreservingRoles(Out.Shell, Out.Back);
	for (const FDynamicMesh3& Cushion : Out.SeatCushions)
	{
		FHFMeshOps::AppendPreservingRoles(Out.Shell, Cushion);
	}
	for (const FDynamicMesh3& Cushion : Out.BackCushions)
	{
		FHFMeshOps::AppendPreservingRoles(Out.Shell, Cushion);
	}
	FHFMeshOps::AppendPreservingRoles(Out.Shell, Out.ChaiseCushion);
	FHFMeshOps::AppendPreservingRoles(Out.Shell, Out.ChaiseBackCushion);

	Out.bValid = Out.Shell.TriangleCount() > 0;
	return Out;
}
