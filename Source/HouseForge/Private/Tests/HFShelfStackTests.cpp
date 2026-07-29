// Copyright Siddartha G. All Rights Reserved.

#include "HouseForge.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Geometry/HFJoineryKit.h"
#include "Geometry/HFMeshOps.h"
#include "MeshQueries.h"
#include "Misc/AutomationTest.h"
#include "Model/HFTypes.h"

using namespace UE::Geometry;

#define HF_TEST_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
	/** A 750 wardrobe bay inside a 600 deep body: 563 clear, 2000 of hanging height. */
	FHFShelfStackParams MakeWardrobeBay()
	{
		FHFShelfStackParams Params;
		Params.Width = 75.0;
		Params.Depth = 56.3;
		Params.Height = 200.0;
		Params.ShelfCount = 4;
		Params.FrontSetback = 1.0;
		return Params;
	}

	double VolumeOf(const FDynamicMesh3& Mesh)
	{
		return TMeshQueries<FDynamicMesh3>::GetVolumeArea(Mesh).X;
	}

	/** The material a stack of the given layout is made of, cubic centimetres. */
	double ExpectedMaterial(int32 BayCount, double BayWidth, double PartitionThickness,
		int32 ShelfCount, double ShelfThickness, double ClearDepth, double Height)
	{
		return BayCount * ShelfCount * BayWidth * ClearDepth * ShelfThickness
			+ (BayCount - 1) * PartitionThickness * ClearDepth * Height;
	}

	/** Every triangle closed, tagged, and tagged with a role the caller expects to see. */
	void CheckSolidAndTagged(FAutomationTestBase& Test, const FDynamicMesh3& Mesh,
		const TArray<EHFSurfaceRole>& Allowed, const TCHAR* What)
	{
		Test.TestTrue(FString::Printf(TEXT("%s is watertight"), What), FHFMeshOps::IsClosed(Mesh));
		Test.TestTrue(FString::Printf(TEXT("%s is a solid, not an inside-out one"), What),
			VolumeOf(Mesh) > 0.0);

		for (const int32 Tid : Mesh.TriangleIndicesItr())
		{
			const int32 Group = Mesh.GetTriangleGroup(Tid);
			if (Group == 0)
			{
				// Untagged geometry cannot be re-materialled by the panel, so it is a real defect
				// rather than a cosmetic one - see .claude/rules/04-conventions.md.
				Test.AddError(FString::Printf(TEXT("%s emitted a triangle with no surface role."), What));
				return;
			}
			if (!Allowed.Contains(FHFMeshOps::RoleForGroup(Group)))
			{
				Test.AddError(FString::Printf(TEXT("%s emitted a triangle with an unexpected role."), What));
				return;
			}
		}
	}

	/** True when any triangle carries the role. */
	bool HasRole(const FDynamicMesh3& Mesh, EHFSurfaceRole Role)
	{
		for (const int32 Tid : Mesh.TriangleIndicesItr())
		{
			if (FHFMeshOps::RoleForGroup(Mesh.GetTriangleGroup(Tid)) == Role)
			{
				return true;
			}
		}
		return false;
	}

	/** Extent of the primary UV set, which at world scale is a real distance over the texel size. */
	FVector2d UVExtent(const FDynamicMesh3& Mesh)
	{
		if (!Mesh.HasAttributes() || Mesh.Attributes()->PrimaryUV() == nullptr)
		{
			return FVector2d::Zero();
		}

		const FDynamicMeshUVOverlay* UVs = Mesh.Attributes()->PrimaryUV();
		FVector2d Min(TNumericLimits<double>::Max(), TNumericLimits<double>::Max());
		FVector2d Max(-TNumericLimits<double>::Max(), -TNumericLimits<double>::Max());

		for (const int32 Eid : UVs->ElementIndicesItr())
		{
			const FVector2f UV = UVs->GetElement(Eid);
			Min = FVector2d(FMath::Min(Min.X, (double)UV.X), FMath::Min(Min.Y, (double)UV.Y));
			Max = FVector2d(FMath::Max(Max.X, (double)UV.X), FMath::Max(Max.Y, (double)UV.Y));
		}

		return (UVs->ElementCount() > 0) ? Max - Min : FVector2d::Zero();
	}
}

/**
 * A wardrobe bay of shelves, measured rather than counted.
 *
 * Every assertion here is a property the geometry has to have to survive being lit: it is a closed
 * solid, it holds the material the shelves are really made of, it sits exactly inside the clear
 * volume it was given, and its shelves divide that volume evenly. Triangle counts are deliberately
 * absent - they pass for a stack whose shelves are all in the wrong place.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFShelfStackTest, "HouseForge.Joinery.ShelfStack", HF_TEST_FLAGS)

bool FHFShelfStackTest::RunTest(const FString& Parameters)
{
	const FHFShelfStackParams Params = MakeWardrobeBay();
	const FDynamicMesh3 Mesh = FHFJoineryKit::GenerateShelfStack(Params);

	if (!TestTrue(TEXT("A wardrobe bay of shelves produces geometry"), Mesh.TriangleCount() > 0))
	{
		return false;
	}

	CheckSolidAndTagged(*this, Mesh, { EHFSurfaceRole::JoineryCarcass }, TEXT("A ply shelf stack"));

	// 18 ply, taken from the material rather than asked for.
	const FHFShelfStackParams Used = FHFJoineryKit::SanitiseShelfStack(Params);
	TestNearlyEqual(TEXT("Ply shelves are 18 mm board"), Used.ShelfThickness, 1.8, 0.0001);
	TestNearlyEqual(TEXT("Ply spans 900 mm before it needs support"), Used.MaxSpan, 90.0, 0.0001);
	TestEqual(TEXT("All four shelves fit"), Used.ShelfCount, 4);

	// The material four shelves are made of: 750 wide, 553 of usable depth, 18 thick. A stack that
	// double-counts an overlap or drops a shelf fails this and nothing else notices.
	const double ClearDepth = 56.3 - 1.0;
	const double Expected = ExpectedMaterial(1, 75.0, 1.8, 4, 1.8, ClearDepth, 200.0);
	TestNearlyEqual(TEXT("The stack holds the board four shelves are made of"),
		VolumeOf(Mesh), Expected, Expected * 1e-5);

	// The clear volume it was handed, exactly. Anything outside it fouls the shutter that closes
	// over the bay, and that is only visible once the shutter is animated.
	const FAxisAlignedBox3d Bounds = Mesh.GetBounds();
	TestNearlyEqual(TEXT("The shelves reach the left side"), Bounds.Min.X, 0.0, 0.001);
	TestNearlyEqual(TEXT("The shelves reach the right side"), Bounds.Max.X, 75.0, 0.001);
	TestNearlyEqual(TEXT("The shelf fronts sit at the declared setback"), Bounds.Min.Y, 1.0, 0.001);
	TestNearlyEqual(TEXT("The shelves reach the back panel"), Bounds.Max.Y, 56.3, 0.001);

	// Four shelves divide 2000 into five equal compartments of 385.6, which is squarely in the
	// 350-400 a wardrobe is set out to. The lowest shelf sits one compartment up and the highest one
	// compartment down from the top.
	const double Compartment = (200.0 - 4 * 1.8) / 5.0;
	TestTrue(TEXT("The compartments are a usable height for folded clothes"),
		Compartment >= 30.0 && Compartment <= 45.0);
	TestNearlyEqual(TEXT("The lowest shelf sits one compartment up"), Bounds.Min.Z, Compartment, 0.001);
	TestNearlyEqual(TEXT("The highest shelf leaves one compartment above it"),
		Bounds.Max.Z, 200.0 - Compartment, 0.001);

	// Real-world-scale UVs, or the material panel's tiling in millimetres means nothing. One tile is
	// a metre, so a 750 wide stack spans 0.75 of a tile across.
	const FVector2d UV = UVExtent(Mesh);
	TestNearlyEqual(TEXT("UVs are at world scale across the stack"), UV.X, 0.75, 0.001);
	TestTrue(TEXT("UVs are at world scale up the stack"), UV.Y > 0.0);

	return true;
}

/**
 * Every parameter has to move the geometry, or it is decoration on a details panel.
 *
 * Checked as measurable differences - material, bounds, spacing - rather than "the mesh changed",
 * because a mesh can change in ways that leave the thing looking identical.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFShelfStackParametersTest, "HouseForge.Joinery.ShelfStackParameters", HF_TEST_FLAGS)

bool FHFShelfStackParametersTest::RunTest(const FString& Parameters)
{
	const FHFShelfStackParams Base = MakeWardrobeBay();
	const double BaseVolume = VolumeOf(FHFJoineryKit::GenerateShelfStack(Base));
	const double ClearDepth = 56.3 - 1.0;

	// More shelves is proportionally more board, and tighter compartments.
	{
		FHFShelfStackParams P = Base;
		P.ShelfCount = 6;
		const FDynamicMesh3 Mesh = FHFJoineryKit::GenerateShelfStack(P);

		TestNearlyEqual(TEXT("Six shelves is half again the board of four"),
			VolumeOf(Mesh), BaseVolume * 6.0 / 4.0, BaseVolume * 1e-5);

		const double Compartment = (200.0 - 6 * 1.8) / 7.0;
		TestNearlyEqual(TEXT("Six shelves divide the height into seven"),
			Mesh.GetBounds().Min.Z, Compartment, 0.001);
	}

	// A wider bay is a wider shelf, up to the span limit.
	{
		FHFShelfStackParams P = Base;
		P.Width = 85.0;
		const FDynamicMesh3 Mesh = FHFJoineryKit::GenerateShelfStack(P);

		TestNearlyEqual(TEXT("A wider bay holds proportionally more board"),
			VolumeOf(Mesh), BaseVolume * 85.0 / 75.0, BaseVolume * 1e-5);
		TestNearlyEqual(TEXT("A wider bay produces wider shelves"), Mesh.GetBounds().Max.X, 85.0, 0.001);
	}

	// Glass is 8 mm, not 18, and it is tagged as glass so it refracts instead of reading as a thin
	// white board. It also spans far less, so this bay picks up a mid partition.
	{
		FHFShelfStackParams P = Base;
		P.ShelfMaterial = EHFShelfMaterial::Glass;

		const FHFShelfStackParams Used = FHFJoineryKit::SanitiseShelfStack(P);
		TestNearlyEqual(TEXT("A glass shelf is 8 mm toughened"), Used.ShelfThickness, 0.8, 0.0001);
		TestNearlyEqual(TEXT("Glass spans 600 mm before it stops looking safe"), Used.MaxSpan, 60.0, 0.0001);

		const FDynamicMesh3 Mesh = FHFJoineryKit::GenerateShelfStack(P);
		CheckSolidAndTagged(*this, Mesh, { EHFSurfaceRole::Glass, EHFSurfaceRole::JoineryCarcass },
			TEXT("A glass shelf stack"));
		TestTrue(TEXT("Glass shelves are tagged as glass"), HasRole(Mesh, EHFSurfaceRole::Glass));

		const double BayWidth = (75.0 - 1.8) / 2.0;
		const double Expected = ExpectedMaterial(2, BayWidth, 1.8, 4, 0.8, ClearDepth, 200.0);
		TestNearlyEqual(TEXT("Glass shelves are split over a partition and sized to it"),
			VolumeOf(Mesh), Expected, Expected * 1e-5);
	}

	// An explicit thickness beats the material default, which is what a caller matching an existing
	// unit needs.
	{
		FHFShelfStackParams P = Base;
		P.ShelfThickness = 2.5;
		const double Expected = ExpectedMaterial(1, 75.0, 1.8, 4, 2.5, ClearDepth, 200.0);
		TestNearlyEqual(TEXT("An explicit board thickness is used as given"),
			VolumeOf(FHFJoineryKit::GenerateShelfStack(P)), Expected, Expected * 1e-5);
	}

	// The setback is what keeps a shelf out of the path of the shutter closing over it.
	{
		FHFShelfStackParams P = Base;
		P.FrontSetback = 5.0;
		P.BackClearance = 2.0;
		const FDynamicMesh3 Mesh = FHFJoineryKit::GenerateShelfStack(P);

		TestNearlyEqual(TEXT("The setback moves the shelf fronts back"), Mesh.GetBounds().Min.Y, 5.0, 0.001);
		TestNearlyEqual(TEXT("The back clearance leaves a gap behind"), Mesh.GetBounds().Max.Y, 54.3, 0.001);
		TestTrue(TEXT("A shallower shelf is less board"), VolumeOf(Mesh) < BaseVolume);
	}

	// Nothing asked for is nothing built - a hanging-only section is a real answer, not a failure.
	{
		FHFShelfStackParams P = Base;
		P.ShelfCount = 0;
		TestEqual(TEXT("A bay with no shelves and no rail is empty"),
			FHFJoineryKit::GenerateShelfStack(P).TriangleCount(), 0);

		// Even one wide enough to need a partition: a partition exists to hold shelves up, and there
		// are none.
		P.Width = 300.0;
		TestEqual(TEXT("An empty bay gets no partitions either"),
			FHFJoineryKit::GenerateShelfStack(P).TriangleCount(), 0);
	}

	// Degenerate input has to come back empty rather than inverted or infinite.
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		FHFShelfStackParams P = Base;
		(Axis == 0 ? P.Width : Axis == 1 ? P.Depth : P.Height) = 0.0;
		TestEqual(TEXT("A bay with no volume produces no geometry"),
			FHFJoineryKit::GenerateShelfStack(P).TriangleCount(), 0);
	}

	{
		FHFShelfStackParams P = Base;
		P.FrontSetback = 100.0;
		TestEqual(TEXT("A setback deeper than the bay produces no geometry"),
			FHFJoineryKit::GenerateShelfStack(P).TriangleCount(), 0);
	}

	// More shelves than fit is capped at the number that do. The alternative is boards passing
	// through one another, which reads as a plausible volume and looks like a solid block.
	{
		FHFShelfStackParams P = Base;
		P.Height = 20.0;
		P.ShelfCount = 30;

		const FHFShelfStackParams Used = FHFJoineryKit::SanitiseShelfStack(P);
		TestTrue(TEXT("Only the shelves that fit are built"),
			Used.ShelfCount > 0 && Used.ShelfCount * Used.ShelfThickness < 20.0);

		const FDynamicMesh3 Mesh = FHFJoineryKit::GenerateShelfStack(P);
		CheckSolidAndTagged(*this, Mesh, { EHFSurfaceRole::JoineryCarcass }, TEXT("An over-filled stack"));
		TestTrue(TEXT("An over-filled stack still stays inside its bay"),
			Mesh.GetBounds().Min.Z >= -0.001 && Mesh.GetBounds().Max.Z <= 20.001);
	}

	return true;
}

/**
 * The hanging rail: the one thing in a wardrobe that is unmistakably a wardrobe.
 *
 * It does not move on its own, so it belongs in whatever mesh its bay belongs to - but it still has
 * to be a real tube of the declared diameter, hung where a hanger can lift off it, and inside the
 * volume the shutter closes over.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFShelfStackRailTest, "HouseForge.Joinery.ShelfStackRail", HF_TEST_FLAGS)

bool FHFShelfStackRailTest::RunTest(const FString& Parameters)
{
	// A rail on its own, so every measurement below is the rail's and nothing else's.
	FHFShelfStackParams RailOnly = MakeWardrobeBay();
	RailOnly.ShelfCount = 0;
	RailOnly.bHangingRail = true;

	const FHFShelfStackParams Used = FHFJoineryKit::SanitiseShelfStack(RailOnly);
	if (!TestTrue(TEXT("A wardrobe bay has room for a rail"), Used.bHangingRail))
	{
		return false;
	}

	const FDynamicMesh3 Rail = FHFJoineryKit::GenerateShelfStack(RailOnly);
	CheckSolidAndTagged(*this, Rail, { EHFSurfaceRole::MetalHardware }, TEXT("A hanging rail"));

	const FAxisAlignedBox3d Bounds = Rail.GetBounds();

	// Slung the declared drop below the top of the bay: 65 mm is what a hanger needs to lift off.
	TestNearlyEqual(TEXT("The rail hangs at its declared drop below the top"),
		Bounds.Center().Z, 200.0 - 6.5, 0.001);

	// It reaches both sides, because it is carried on a fixing at each one.
	TestNearlyEqual(TEXT("The rail is carried on the left side"), Bounds.Min.X, 0.0, 0.001);
	TestNearlyEqual(TEXT("The rail is carried on the right side"), Bounds.Max.X, 75.0, 0.001);

	// Centred in the depth, and it is the tube's declared diameter plus its fixings and no more.
	TestNearlyEqual(TEXT("The rail is centred in the depth"), Bounds.Center().Y, (1.0 + 56.3) * 0.5, 0.001);
	TestTrue(TEXT("The rail is at least its declared diameter deep"), Bounds.Depth() >= 2.5 - 0.001);
	TestTrue(TEXT("The rail is a tube, not a box"), Bounds.Height() <= 2.5 + 3.0);
	TestTrue(TEXT("The rail stays inside the bay"),
		Bounds.Min.Z >= 0.0 && Bounds.Max.Z <= 200.0 && Bounds.Min.Y >= 1.0 && Bounds.Max.Y <= 56.3);

	// A tube of the declared radius spanning the bay, give or take its end fixings. Asserted as a
	// range because the fixings are real material, not as a triangle count.
	const double RailVolume = VolumeOf(Rail);
	const double Cylinder = UE_DOUBLE_PI * FMath::Square(2.5 * 0.5) * 75.0;
	TestTrue(TEXT("The rail holds about the metal a 25 mm tube across the bay holds"),
		RailVolume > Cylinder * 0.85 && RailVolume < Cylinder * 1.4);

	// Diameter is honoured rather than decorative: twice across is four times the metal.
	{
		FHFShelfStackParams Thick = RailOnly;
		Thick.RailDiameter = 5.0;
		const double ThickVolume = VolumeOf(FHFJoineryKit::GenerateShelfStack(Thick));
		const double Ratio = ThickVolume / RailVolume;
		TestTrue(TEXT("Doubling the rail diameter roughly quadruples its metal"),
			Ratio > 3.0 && Ratio < 4.3);
	}

	// The drop is honoured too.
	{
		FHFShelfStackParams Low = RailOnly;
		Low.RailDrop = 20.0;
		TestNearlyEqual(TEXT("A deeper drop hangs the rail lower"),
			FHFJoineryKit::GenerateShelfStack(Low).GetBounds().Center().Z, 180.0, 0.001);
	}

	// With shelves under it, the rail is the same rail, in the compartment above the top shelf.
	{
		FHFShelfStackParams WithShelves = MakeWardrobeBay();
		WithShelves.bHangingRail = true;

		const FDynamicMesh3 Mesh = FHFJoineryKit::GenerateShelfStack(WithShelves);
		CheckSolidAndTagged(*this, Mesh,
			{ EHFSurfaceRole::JoineryCarcass, EHFSurfaceRole::MetalHardware }, TEXT("A shelved bay with a rail"));

		const double Shelved = VolumeOf(FHFJoineryKit::GenerateShelfStack(MakeWardrobeBay()));
		TestNearlyEqual(TEXT("Adding a rail adds exactly the rail"),
			VolumeOf(Mesh) - Shelved, RailVolume, RailVolume * 1e-5);

		const double Compartment = (200.0 - 4 * 1.8) / 5.0;
		const double TopShelfTop = 200.0 - Compartment;
		TestTrue(TEXT("The rail hangs in the compartment above the top shelf"),
			Mesh.GetBounds().Max.Z > TopShelfTop);
		TestTrue(TEXT("The rail stays inside the bay"), Mesh.GetBounds().Max.Z <= 200.0);
	}

	// A bay too shallow to hang anything in says so, rather than emitting a rail through its own
	// shutter. Reported through the sanitised parameters so a caller can tell "would not fit" from
	// "was not asked for".
	{
		FHFShelfStackParams Shallow = RailOnly;
		Shallow.Depth = 2.0;
		TestFalse(TEXT("A bay too shallow for a rail reports that it has none"),
			FHFJoineryKit::SanitiseShelfStack(Shallow).bHangingRail);
		TestEqual(TEXT("And builds nothing at all"),
			FHFJoineryKit::GenerateShelfStack(Shallow).TriangleCount(), 0);
	}

	{
		FHFShelfStackParams Narrow = RailOnly;
		Narrow.Width = 2.0;
		TestFalse(TEXT("A bay too narrow for a rail reports that it has none"),
			FHFJoineryKit::SanitiseShelfStack(Narrow).bHangingRail);
	}

	{
		FHFShelfStackParams Squat = RailOnly;
		Squat.Height = 2.0;
		TestFalse(TEXT("A compartment too short for a rail reports that it has none"),
			FHFJoineryKit::SanitiseShelfStack(Squat).bHangingRail);
	}

	return true;
}

/**
 * Span: the rule that keeps a shelf from sagging on camera.
 *
 * 18 ply carries 900 mm and 8 mm glass carries 600. Past that a shelf needs a partition under it,
 * and the reason this is tested rather than trusted is that a sagging shelf is invisible in a
 * wireframe and unmistakable in a lit render.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFShelfStackSpanTest, "HouseForge.Joinery.ShelfStackSpan", HF_TEST_FLAGS)

bool FHFShelfStackSpanTest::RunTest(const FString& Parameters)
{
	const double ClearDepth = 56.3 - 1.0;

	// A 1500 run of shelves in 18 ply: two bays of 741 over one partition.
	{
		FHFShelfStackParams P = MakeWardrobeBay();
		P.Width = 150.0;

		const FDynamicMesh3 Mesh = FHFJoineryKit::GenerateShelfStack(P);
		CheckSolidAndTagged(*this, Mesh, { EHFSurfaceRole::JoineryCarcass }, TEXT("A split shelf stack"));

		const double BayWidth = (150.0 - 1.8) / 2.0;
		TestTrue(TEXT("The split brings each shelf inside the span limit"), BayWidth <= 90.0);

		const double Expected = ExpectedMaterial(2, BayWidth, 1.8, 4, 1.8, ClearDepth, 200.0);
		TestNearlyEqual(TEXT("An over-wide bay is split and the partition is built"),
			VolumeOf(Mesh), Expected, Expected * 1e-5);

		// The partition runs the full height of the stack, which is the only way it carries anything.
		const FAxisAlignedBox3d Bounds = Mesh.GetBounds();
		TestNearlyEqual(TEXT("The stack still fills its bay"), Bounds.Max.X, 150.0, 0.001);
		TestNearlyEqual(TEXT("The partition reaches the bottom"), Bounds.Min.Z, 0.0, 0.001);
		TestNearlyEqual(TEXT("The partition reaches the top"), Bounds.Max.Z, 200.0, 0.001);
	}

	// Turned off, the same run is one unsupported shelf - which is the caller's business when the
	// carcass already supplies the partition.
	{
		FHFShelfStackParams P = MakeWardrobeBay();
		P.Width = 150.0;
		P.bMidPartitionWhenOverspan = false;

		const double Expected = ExpectedMaterial(1, 150.0, 1.8, 4, 1.8, ClearDepth, 200.0);
		TestNearlyEqual(TEXT("With splitting off the shelves run the full width"),
			VolumeOf(FHFJoineryKit::GenerateShelfStack(P)), Expected, Expected * 1e-5);
	}

	// Partitions eat width of their own, so a wide run needs the count solved rather than divided
	// once: 3000 over four bays, each inside the limit.
	{
		FHFShelfStackParams P = MakeWardrobeBay();
		P.Width = 300.0;

		const double BayWidth = (300.0 - 3 * 1.8) / 4.0;
		TestTrue(TEXT("Four bays bring a 3 m run inside the span limit"), BayWidth <= 90.0);

		const double Expected = ExpectedMaterial(4, BayWidth, 1.8, 4, 1.8, ClearDepth, 200.0);
		TestNearlyEqual(TEXT("A 3 m run is split over three partitions"),
			VolumeOf(FHFJoineryKit::GenerateShelfStack(P)), Expected, Expected * 1e-5);
	}

	// The limit is the material's. The same 750 bay is fine in ply and needs splitting in glass.
	{
		FHFShelfStackParams Ply = MakeWardrobeBay();
		const double PlyExpected = ExpectedMaterial(1, 75.0, 1.8, 4, 1.8, ClearDepth, 200.0);
		TestNearlyEqual(TEXT("750 is within the span of 18 ply, so it is not split"),
			VolumeOf(FHFJoineryKit::GenerateShelfStack(Ply)), PlyExpected, PlyExpected * 1e-5);

		FHFShelfStackParams Glass = Ply;
		Glass.ShelfMaterial = EHFShelfMaterial::Glass;
		const double GlassBay = (75.0 - 1.8) / 2.0;
		const double GlassExpected = ExpectedMaterial(2, GlassBay, 1.8, 4, 0.8, ClearDepth, 200.0);
		TestNearlyEqual(TEXT("The same bay in glass is split, because glass spans less"),
			VolumeOf(FHFJoineryKit::GenerateShelfStack(Glass)), GlassExpected, GlassExpected * 1e-5);
	}

	// A hanging rail sags over a long span exactly as a shelf does, so an over-wide hanging section
	// is split too - and each bay then gets its own rail, because one cannot pass through the
	// partition carrying it.
	{
		FHFShelfStackParams Split = MakeWardrobeBay();
		Split.ShelfCount = 0;
		Split.bHangingRail = true;
		Split.Width = 150.0;

		const FDynamicMesh3 Mesh = FHFJoineryKit::GenerateShelfStack(Split);
		CheckSolidAndTagged(*this, Mesh,
			{ EHFSurfaceRole::MetalHardware, EHFSurfaceRole::JoineryCarcass }, TEXT("A split bay of rails"));
		TestTrue(TEXT("A long hanging section is carried on a partition"),
			HasRole(Mesh, EHFSurfaceRole::JoineryCarcass));

		// One bay's worth of rail, twice, plus the partition between them - and nothing else.
		FHFShelfStackParams OneBay = Split;
		OneBay.Width = (150.0 - 1.8) / 2.0;
		const double OneRail = VolumeOf(FHFJoineryKit::GenerateShelfStack(OneBay));
		const double Partition = 1.8 * ClearDepth * 200.0;

		TestNearlyEqual(TEXT("Each bay gets its own rail rather than one through the partition"),
			VolumeOf(Mesh), 2.0 * OneRail + Partition, (2.0 * OneRail + Partition) * 1e-5);
	}

	// An absurd span limit must not spin the bay search or produce a bay of nothing.
	{
		FHFShelfStackParams P = MakeWardrobeBay();
		P.Width = 300.0;
		P.MaxSpan = 0.01;

		const FDynamicMesh3 Mesh = FHFJoineryKit::GenerateShelfStack(P);
		CheckSolidAndTagged(*this, Mesh, { EHFSurfaceRole::JoineryCarcass }, TEXT("An over-split stack"));
		TestNearlyEqual(TEXT("An over-split stack still fills its bay exactly"),
			Mesh.GetBounds().Max.X, 300.0, 0.001);
	}

	return true;
}

/**
 * Choosing a shelf count: the domain ladder, kept out of the generator.
 *
 * A wardrobe wants 375 mm compartments and a kitchen wall unit wants 320, and neither wants a
 * compartment nothing fits in. The generator does as it is told; this is what decides what to tell
 * it.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFShelfSpacingTest, "HouseForge.Joinery.ShelfSpacing", HF_TEST_FLAGS)

bool FHFShelfSpacingTest::RunTest(const FString& Parameters)
{
	// 2000 of wardrobe at the trade's 375: four shelves, 386 clear - inside the 350-400 band.
	{
		const int32 Count = FHFJoineryKit::ShelfCountForClearHeight(200.0);
		TestEqual(TEXT("A 2 m wardrobe bay takes four shelves"), Count, 4);

		const double Clear = (200.0 - Count * 1.8) / (Count + 1);
		TestTrue(TEXT("Its compartments are the 350-400 a wardrobe is set out to"),
			Clear >= 35.0 && Clear <= 40.0);
	}

	// 664 of clear wall unit at 320: one shelf, 323 clear - the 310-330 the trade builds.
	{
		const int32 Count = FHFJoineryKit::ShelfCountForClearHeight(66.4, 32.0);
		TestEqual(TEXT("A 700 wall unit takes one internal shelf"), Count, 1);

		const double Clear = (66.4 - Count * 1.8) / (Count + 1);
		TestTrue(TEXT("Its compartments are the 310-330 of a wall unit"),
			Clear >= 31.0 && Clear <= 33.0);
	}

	// Below one compartment there is nothing to divide.
	TestEqual(TEXT("A bay shorter than one compartment takes no shelves"),
		FHFJoineryKit::ShelfCountForClearHeight(20.0), 0);
	TestEqual(TEXT("A bay of no height takes no shelves"),
		FHFJoineryKit::ShelfCountForClearHeight(0.0), 0);
	TestEqual(TEXT("A bay of negative height takes no shelves"),
		FHFJoineryKit::ShelfCountForClearHeight(-50.0), 0);

	// The invariant that matters across the whole range: never a compartment too small to hold
	// anything a wardrobe is for, and never more shelves than the height can carry.
	for (double Height = 20.0; Height <= 300.0; Height += 2.5)
	{
		const int32 Count = FHFJoineryKit::ShelfCountForClearHeight(Height);
		if (Count <= 0)
		{
			continue;
		}

		const double Clear = (Height - Count * 1.8) / (Count + 1);
		if (Clear < FHFJoineryKit::MinUsefulCompartment - 0.001)
		{
			AddError(FString::Printf(
				TEXT("A %.1f cm bay was given %d shelves, leaving %.1f cm compartments."),
				Height, Count, Clear));
			break;
		}
	}

	// A glass shelf is thinner, so the same height carries its shelves at slightly wider spacing.
	{
		const int32 Count = FHFJoineryKit::ShelfCountForClearHeight(200.0, 37.5,
			FHFJoineryKit::DefaultShelfThickness(EHFShelfMaterial::Glass));
		TestTrue(TEXT("A glass stack of the same height takes a sensible count"), Count >= 4);
	}

	// And the count it hands back has to be one the generator will actually build, or the ladder is
	// advice the geometry ignores.
	{
		FHFShelfStackParams P = MakeWardrobeBay();
		P.ShelfCount = FHFJoineryKit::ShelfCountForClearHeight(P.Height);

		TestEqual(TEXT("The count it recommends is the count that gets built"),
			FHFJoineryKit::SanitiseShelfStack(P).ShelfCount, P.ShelfCount);
	}

	return true;
}

#undef HF_TEST_FLAGS

#endif // WITH_DEV_AUTOMATION_TESTS
