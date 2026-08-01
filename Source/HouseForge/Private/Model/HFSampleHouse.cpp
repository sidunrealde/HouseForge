// Copyright Siddartha G. All Rights Reserved.

#include "Model/HFSampleHouse.h"

#include "HouseForge.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "Model/HFSpecSerializer.h"

namespace
{
	// ------------------------------------------------------------------- plan grid, millimetres
	// Internal faces. Walls are placed on these lines, so a room's clear dimension is the grid
	// span minus half a wall thickness at each end - close enough for a reference layout, and it
	// keeps the numbers legible on the drawings.

	constexpr double X0 = 0.0;			// west external
	constexpr double X1 = 1800.0;		// foyer | common bath
	constexpr double X2 = 4200.0;		// common bath | corridor, and kitchen | master bedroom
	constexpr double X3 = 6600.0;		// living | bedroom 2
	constexpr double X4 = 8100.0;		// corridor | master bath
	constexpr double X5 = 10800.0;		// east external

	constexpr double XE = 12300.0;		// east balcony (wash area) parapet

	constexpr double YB = -1500.0;		// south balcony parapet
	constexpr double YN = 9900.0;		// north balcony parapet
	constexpr double Y0 = 0.0;			// south external
	constexpr double Y1 = 3600.0;		// living/bedroom2 | service band
	constexpr double Y2 = 5400.0;		// service band | kitchen/master bedroom
	constexpr double Y3 = 8400.0;		// north external

	// The utility, carved out of the kitchen's north-east corner. Both lines land on the 300 module
	// the rest of the plan is set out on.
	constexpr double XU = 3000.0;		// kitchen | utility
	constexpr double YU = 6600.0;		// kitchen | utility

	// -------------------------------------------------------------------- why the band is cut here
	// The service band (Y 3600..5400) used to read foyer | corridor | common bath | master bath |
	// utility, and it could not work. Both bedrooms front this band, and with the corridor only
	// 1800..4200 neither bedroom had a corridor wall to hang its door on: D_Bed2 opened bedroom 2
	// into the master bathroom and D_MBed opened the master bedroom into the common one. The
	// bathroom fittings standing in those doorways were where a bathroom's fittings belong.
	//
	// Bedroom 2 starts at X 6600 and COL_M1's east face is at 6825, so a 900 door needs the corridor
	// to reach about 8100. The corridor cannot start east of 4200 without sealing the foyer off. That
	// is a 3900 corridor, which leaves 5100 of band for what used to be three wet rooms - enough for
	// two. So one room leaves the band, and it is the utility, because a utility belongs off the
	// kitchen anyway. The band is now foyer | common bath | corridor | master bath, every door opens
	// into the room it serves, and nothing swings into the corridor at all.

	constexpr double ExternalThickness = 230.0;
	constexpr double InternalThickness = 115.0;
	constexpr double ParapetThickness  = 115.0;

	constexpr double WallHeight    = 3000.0;
	constexpr double ParapetHeight = 1100.0;

	constexpr double DoorWidth     = 900.0;
	constexpr double DoorHeight    = 2100.0;
	constexpr double MainDoorWidth = 1050.0;

	// 1200 on a 900 sill puts every window head on 2100, which is the door head, and that is how
	// Indian practice sets a flat out - one line round the whole plan that the pelmets, the wall
	// units and the false ceiling all measure from. At 1350 the four habitable-room windows sat 150
	// above the door heads for no reason anybody could point at, and only the kitchen window - which
	// had to be worked out from its worktop - was on the line.
	constexpr double WindowHeight = 1200.0;
	constexpr double WindowSill   = 900.0;
	constexpr double HeadHeight   = 2100.0;	// door head, and every window head with it

	static_assert(DoorHeight == HeadHeight, "Door heads define the head line; keep them on it.");
	static_assert(WindowSill + WindowHeight == HeadHeight,
		"Window heads line up with the door heads. Change the sill and the height together.");

	struct FSampleBuilder
	{
		FHFHouseSpec Spec;

		void AddWall(const FName& Id, const FVector2D& Start, const FVector2D& End, bool bExternal, double Height = WallHeight)
		{
			FHFWall Wall;
			Wall.Id = Id;
			Wall.Start = Start;
			Wall.End = End;
			Wall.Thickness = bExternal ? ExternalThickness : InternalThickness;
			Wall.Height = Height;
			Wall.bIsExternal = bExternal;
			Spec.Walls.Add(Wall);
		}

		void AddParapet(const FName& Id, const FVector2D& Start, const FVector2D& End)
		{
			FHFWall Wall;
			Wall.Id = Id;
			Wall.Start = Start;
			Wall.End = End;
			Wall.Thickness = ParapetThickness;
			Wall.Height = ParapetHeight;
			Wall.bIsExternal = true;
			Spec.Walls.Add(Wall);
		}

		void AddOpening(const FName& Id, const FName& WallId, double Offset, double Width, double Height,
			double Sill, EHFOpeningKind Kind, EHFSwing Swing = EHFSwing::None)
		{
			FHFOpening Opening;
			Opening.Id = Id;
			Opening.WallId = WallId;
			Opening.OffsetAlongWall = Offset;
			Opening.Width = Width;
			Opening.Height = Height;
			Opening.SillHeight = Sill;
			Opening.Kind = Kind;
			Opening.Swing = Swing;
			Spec.Openings.Add(Opening);
		}

		void AddDoor(const FName& Id, const FName& WallId, double Offset, EHFSwing Swing, double Width = DoorWidth)
		{
			AddOpening(Id, WallId, Offset, Width, DoorHeight, 0.0, EHFOpeningKind::Door, Swing);
		}

		/**
		 * A window in an external wall.
		 *
		 * Sliding by default, because that is what every habitable-room window in a flat of this
		 * class is: a two-track aluminium unit with one fixed sash and one that runs. Casements and
		 * fixed lights do appear in Indian flats, but not in this layout - there is no picture window
		 * and no top-hung casement in it - so nothing here is fixed just to have one of each.
		 */
		void AddWindow(const FName& Id, const FName& WallId, double Offset, double Width,
			EHFOpeningKind Kind = EHFOpeningKind::SlidingWindow,
			double Height = WindowHeight, double Sill = WindowSill)
		{
			AddOpening(Id, WallId, Offset, Width, Height, Sill, Kind);
		}

		/**
		 * A room from its boundary, counter-clockwise. The closing edge is implicit.
		 *
		 * Every room here is a rectangle except the kitchen, which lost its north-east corner to the
		 * utility and is now an L.
		 */
		FHFRoom& AddRoomPoly(const FName& Id, const FString& Name, EHFRoomType Type,
			const TArray<FVector2D>& Boundary, double SkirtingHeight = 100.0)
		{
			FHFRoom Room;
			Room.Id = Id;
			Room.Name = Name;
			Room.Type = Type;
			Room.Boundary = Boundary;
			Room.CeilingHeight = WallHeight;
			Room.SkirtingHeight = SkirtingHeight;
			return Spec.Rooms.Add_GetRef(Room);
		}

		/** A rectangular room, from a corner pair. */
		FHFRoom& AddRoom(const FName& Id, const FString& Name, EHFRoomType Type,
			double MinX, double MinY, double MaxX, double MaxY,
			double SkirtingHeight = 100.0)
		{
			return AddRoomPoly(Id, Name, Type, {
				FVector2D(MinX, MinY),
				FVector2D(MaxX, MinY),
				FVector2D(MaxX, MaxY),
				FVector2D(MinX, MaxY)
			}, SkirtingHeight);
		}

		void AddCeiling(const FName& Id, const FName& RoomId, EHFCeilingStyle Style,
			double Drop, double BandWidth, const TArray<FVector2D>& Lights = {})
		{
			FHFFalseCeiling Ceiling;
			Ceiling.Id = Id;
			Ceiling.RoomId = RoomId;
			Ceiling.Style = Style;
			Ceiling.Drop = Drop;
			Ceiling.BandWidth = BandWidth;
			Ceiling.Cove.ChannelWidth = 80.0;
			Ceiling.Cove.LipHeight = 50.0;
			Ceiling.Cove.Setback = 20.0;
			Ceiling.Cove.bHasLedStrip = (Style == EHFCeilingStyle::Cove);
			Ceiling.LightPositions = Lights;
			Spec.FalseCeilings.Add(Ceiling);
		}

		void AddBeam(const FName& Id, const FVector2D& Start, const FVector2D& End, double Depth = 450.0)
		{
			FHFBeam Beam;
			Beam.Id = Id;
			Beam.Start = Start;
			Beam.End = End;
			Beam.Width = 230.0;
			Beam.Depth = Depth;
			Beam.SoffitZ = WallHeight;
			Spec.Beams.Add(Beam);
		}

		void AddColumn(const FName& Id, const FVector2D& Position, double Rotation = 0.0)
		{
			FHFColumn Column;
			Column.Id = Id;
			Column.Position = Position;
			Column.Size = FVector2D(450.0, 230.0);
			Column.RotationDegrees = Rotation;
			Column.Height = WallHeight;
			Spec.Columns.Add(Column);
		}

		FHFFixture& AddFixture(const FName& Id, const FName& RoomId, EHFFixtureType Type, const FString& Label,
			const FVector2D& Position, const FVector2D& Footprint, double Height,
			double Rotation = 0.0, double BaseZ = 0.0)
		{
			FHFFixture Fixture;
			Fixture.Id = Id;
			Fixture.RoomId = RoomId;
			Fixture.Type = Type;
			Fixture.Label = Label;
			Fixture.Position = Position;
			Fixture.Footprint = Footprint;
			Fixture.Height = Height;
			Fixture.RotationDegrees = Rotation;
			Fixture.BaseZ = BaseZ;
			return Spec.Fixtures.Add_GetRef(Fixture);
		}
	};
}

FHFHouseSpec FHFSampleHouse::Make2BHK()
{
	FSampleBuilder B;

	B.Spec.SchemaVersion = 1;
	B.Spec.Name = TEXT("Sample 2BHK");
	B.Spec.SourceDrawing = TEXT("Reference/Drawings/Sample2BHK/01-blank-layout.png");
	B.Spec.Units = EHFUnits::Millimeters;
	B.Spec.UnitsSource = TEXT("Sheet header: ALL DIMENSIONS IN MILLIMETERS");
	B.Spec.DefaultWallThickness = InternalThickness;
	B.Spec.DefaultWallHeight = WallHeight;

	// --------------------------------------------------------------------------------- walls
	// External shell.
	B.AddWall(TEXT("W_South"), FVector2D(X0, Y0), FVector2D(X5, Y0), true);
	B.AddWall(TEXT("W_East"),  FVector2D(X5, Y0), FVector2D(X5, Y3), true);
	B.AddWall(TEXT("W_North"), FVector2D(X5, Y3), FVector2D(X0, Y3), true);
	B.AddWall(TEXT("W_West"),  FVector2D(X0, Y3), FVector2D(X0, Y0), true);

	// Horizontal internal partitions.
	B.AddWall(TEXT("W_Mid_Lower"), FVector2D(X0, Y1), FVector2D(X5, Y1), false);
	B.AddWall(TEXT("W_Mid_Upper"), FVector2D(X0, Y2), FVector2D(X5, Y2), false);

	// Vertical internal partitions, south band.
	B.AddWall(TEXT("W_Living_Bed2"), FVector2D(X3, Y0), FVector2D(X3, Y1), false);

	// Vertical internal partitions, service band: foyer | common bath | corridor | master bath.
	B.AddWall(TEXT("W_Foyer_CBath"), FVector2D(X1, Y1), FVector2D(X1, Y2), false);
	B.AddWall(TEXT("W_CBath_Corr"),  FVector2D(X2, Y1), FVector2D(X2, Y2), false);
	B.AddWall(TEXT("W_Corr_MBath"),  FVector2D(X4, Y1), FVector2D(X4, Y2), false);

	// Vertical internal partition, north band.
	B.AddWall(TEXT("W_Kitchen_MBed"), FVector2D(X2, Y2), FVector2D(X2, Y3), false);

	// The utility, boxed out of the kitchen's north-east corner. Two partitions, no beam: neither
	// line is load bearing, and BM_Kitchen_MBed already runs down X2 beside it.
	B.AddWall(TEXT("W_Kitchen_Util"),   FVector2D(XU, YU), FVector2D(XU, Y3), false);
	B.AddWall(TEXT("W_Kitchen_Util_S"), FVector2D(XU, YU), FVector2D(X2, YU), false);

	// Balcony parapets. Three balconies: living (south), master bedroom (north) and a service
	// balcony off the master bathroom (east).
	B.AddParapet(TEXT("W_Balc_South"), FVector2D(X0, YB), FVector2D(X2, YB));
	B.AddParapet(TEXT("W_Balc_West"),  FVector2D(X0, YB), FVector2D(X0, Y0));
	B.AddParapet(TEXT("W_Balc_East"),  FVector2D(X2, YB), FVector2D(X2, Y0));

	B.AddParapet(TEXT("W_BalcN_North"), FVector2D(X3, YN), FVector2D(X5, YN));
	B.AddParapet(TEXT("W_BalcN_West"),  FVector2D(X3, Y3), FVector2D(X3, YN));
	B.AddParapet(TEXT("W_BalcN_East"),  FVector2D(X5, Y3), FVector2D(X5, YN));

	B.AddParapet(TEXT("W_BalcE_East"),  FVector2D(XE, Y1), FVector2D(XE, Y2));
	B.AddParapet(TEXT("W_BalcE_South"), FVector2D(X5, Y1), FVector2D(XE, Y1));
	B.AddParapet(TEXT("W_BalcE_North"), FVector2D(X5, Y2), FVector2D(XE, Y2));

	// ----------------------------------------------------------------- structure
	// Every downstand beam follows a wall line, and the grid is the wall grid. Nothing here crosses
	// open floor.
	//
	// BM_Living_Cross used to. It ran X 0..6600 at Y 1800 - the exact centre of the living room, on
	// no wall line and between no columns - and it was the only beam in the flat written with a
	// literal coordinate instead of a grid constant, and the only one 400 deep instead of 450. Both
	// of those were the tell. Its stated reason was that the living room's 6600 span was too wide to
	// go unbeamed, and that reads the room the wrong way round: the slab in this bay is framed on
	// all four sides - BM_West at X0, BM_Living_Bed2 at X3, BM_South at Y0, BM_Mid_Lower at Y1 - so
	// it spans 3600 across the short direction, not 6600 along the long one. 3600 is an ordinary
	// span for a 125 slab and it is two-way at that aspect anyway. A beam down the middle of it
	// relieves nothing, because nothing was spanning 6600 in the first place; it merely hangs its
	// own reaction on the mid-span of the two beams it lands on and makes both of them work harder.
	//
	// It was also the one thing in the flat somebody actually saw. R_Living's ceiling is a Cove, so
	// the centre of the room stays at slab height and there is no soffit for a 400 downstand to hide
	// behind. It read in a render as a 400 mm rib crossing the living room at 2600.
	//
	// The validator's BeamNotSupported rule now refuses one, so a beam cannot come back here without
	// a wall under it or a column at each end.
	B.AddBeam(TEXT("BM_South"),      FVector2D(X0, Y0), FVector2D(X5, Y0));
	B.AddBeam(TEXT("BM_North"),      FVector2D(X0, Y3), FVector2D(X5, Y3));
	B.AddBeam(TEXT("BM_West"),       FVector2D(X0, Y0), FVector2D(X0, Y3));
	B.AddBeam(TEXT("BM_East"),       FVector2D(X5, Y0), FVector2D(X5, Y3));
	B.AddBeam(TEXT("BM_Mid_Lower"),  FVector2D(X0, Y1), FVector2D(X5, Y1));
	B.AddBeam(TEXT("BM_Mid_Upper"),  FVector2D(X0, Y2), FVector2D(X5, Y2));
	B.AddBeam(TEXT("BM_Living_Bed2"), FVector2D(X3, Y0), FVector2D(X3, Y1));
	B.AddBeam(TEXT("BM_Kitchen_MBed"), FVector2D(X2, Y2), FVector2D(X2, Y3));

	// Columns at the shell corners and the main wall junctions.
	B.AddColumn(TEXT("COL_SW"), FVector2D(X0, Y0));
	B.AddColumn(TEXT("COL_SE"), FVector2D(X5, Y0));
	B.AddColumn(TEXT("COL_NE"), FVector2D(X5, Y3));
	B.AddColumn(TEXT("COL_NW"), FVector2D(X0, Y3));
	B.AddColumn(TEXT("COL_W1"), FVector2D(X0, Y1), 90.0);
	B.AddColumn(TEXT("COL_W2"), FVector2D(X0, Y2), 90.0);
	B.AddColumn(TEXT("COL_E1"), FVector2D(X5, Y1), 90.0);
	B.AddColumn(TEXT("COL_E2"), FVector2D(X5, Y2), 90.0);
	B.AddColumn(TEXT("COL_S1"), FVector2D(X3, Y0));
	B.AddColumn(TEXT("COL_M1"), FVector2D(X3, Y1));
	B.AddColumn(TEXT("COL_N1"), FVector2D(X2, Y3));

	// ------------------------------------------------------------------------------ openings
	// Main entrance, in the west wall at the foyer. W_West runs north to south from (X0,Y3), so
	// offsets are measured down from the north-west corner.
	//
	// It used to be the only opening the foyer had: nothing led out of it, so no room in the flat
	// was reachable from the front door at all. D_Foyer, below, is the way out of it.
	B.AddOpening(TEXT("D_Main"), TEXT("W_West"), Y3 - 4500.0, MainDoorWidth, DoorHeight, 0.0,
		EHFOpeningKind::Door, EHFSwing::InwardRight);

	// Living to balcony: full-height sliding unit in the south wall.
	B.AddOpening(TEXT("D_Balcony"), TEXT("W_South"), 2100.0, 1800.0, 2100.0, 0.0,
		EHFOpeningKind::SlidingDoor);

	// Windows in the external shell. All five are two-track sliding units, which is what a flat of
	// this class is built with; the kitchen's is the same unit at a smaller size.
	B.AddWindow(TEXT("Win_Living"),  TEXT("W_South"), 5400.0, 1500.0);
	B.AddWindow(TEXT("Win_Bed2_S"),  TEXT("W_South"), 8700.0, 1500.0);
	// 900 wide at 810 along the wall, not 1200 at 1800. At 1800 it spanned Y 1200..2400 and the
	// bedroom's wardrobe stands at Y 1500..3300 against the same wall, so three quarters of the
	// window was behind a 2400-tall wardrobe. Nothing said so while a window was fixed glazing
	// nobody had to reach; a sliding sash has a catch on its meeting stile, and that catch was
	// inside a cupboard. The wall is clear from the corner column at Y 115 to the wardrobe at
	// 1500, which takes a 900 unit - the smaller of the two common bedroom sizes - with a pier of
	// about 240 either side.
	B.AddWindow(TEXT("Win_Bed2_E"),  TEXT("W_East"),  810.0, 900.0);
	// W_North runs east to west from (X5,Y3), so offsets count back from the north-east corner.
	// The master bedroom window opens onto the north balcony, alongside its sliding door.
	//
	// 1500 rather than 1800: at 1800 its western jamb landed on the centreline of the balcony
	// parapet at X3, so the parapet stood 57 mm in front of the glazing and the sash overlapped it.
	// Invisible while windows were fixed geometry nothing swept; the moment they became sashes the
	// house sweep found it. 1500 is also the size a master bedroom window is.
	B.AddWindow(TEXT("Win_MBed_N"),  TEXT("W_North"), X5 - 7500.0, 1500.0);
	// The kitchen window is the one window in the flat that cannot take the standard sill. A
	// kitchen window sits over the worktop, so it starts above the counter and its backsplash
	// rather than at the 900 the habitable rooms use: at 900 its bottom 90 mm was behind the
	// granite's upstand, and the wall units above the counter ran straight across its middle.
	//
	// 1200 x 900 on a 1200 sill is what a kitchen window is, and it puts the head on 2100 - the
	// same line as every door head in the flat, which is the convention Indian practice follows.
	// The wall units are split around it below.
	//
	// Centred on X 1800 rather than 2100: the utility now takes the kitchen's north-east corner, so
	// the kitchen's run of north wall ends at X 3000 instead of 4200 and the window moves with the
	// worktop under it.
	B.AddWindow(TEXT("Win_Kitchen"), TEXT("W_North"), X5 - 1800.0, 1200.0,
		EHFOpeningKind::SlidingWindow, /*Height*/ 900.0, /*Sill*/ 1200.0);

	// The utility's own window, in the same north wall east of the kitchen's. A utility with a
	// washing machine in it and no window is a room nobody would draw; the old one had none because
	// it was landlocked in the service band, and moving it here is what pays for the corridor.
	B.AddWindow(TEXT("Win_Utility"), TEXT("W_North"), X5 - 3400.0, 600.0,
		EHFOpeningKind::SlidingWindow, /*Height*/ 900.0, /*Sill*/ 1200.0);

	// Balcony access. Master bedroom to the north balcony; master bathroom to the east service
	// balcony, which the master bath now backs onto since the utility left the service band.
	B.AddOpening(TEXT("D_BalcN"), TEXT("W_North"), X5 - 9300.0, 1800.0, 2100.0, 0.0,
		EHFOpeningKind::SlidingDoor);
	B.AddDoor(TEXT("D_BalcE"), TEXT("W_East"), 4500.0, EHFSwing::OutwardLeft);

	// The common bathroom is the only room in the flat with no external wall, so it keeps its
	// ventilator: a top-hung pivot sash rather than a fixed louvre, because a bathroom with no
	// window and no exhaust running needs one somebody can actually open. It sits directly on the
	// head of its own door, which is where a ventilator goes when there is nowhere else for it.
	//
	// The master bathroom's is gone. It has D_BalcE onto the wash area now, and a ventilator into a
	// corridor is a poor second to a door onto outside air.
	// 600 x 350, not 600 x 450. It sits directly over D_CBath, so its sill cannot come down off the
	// door head at 2100 - the only dimension free to move is its height. Both rooms it serves now
	// finish at 2500, and a 450 ventilator would head out at 2550: 50 of it stranded in the plenum
	// above the ceiling, open to a void at one end and to nothing at the other.
	B.AddOpening(TEXT("Vent_CBath"), TEXT("W_CBath_Corr"), 900.0, 600.0, 350.0, 2100.0, EHFOpeningKind::Ventilator);

	// ----------------------------------------------------------------------------- internal doors
	// W_Mid_Lower and W_Mid_Upper both run west to east from X0, so on both of them a swing declared
	// Inward opens north and Outward opens south, and Left hangs the leaf on the western jamb.
	//
	// Every one of these opens into the room it serves and none of them opens into the corridor. The
	// two bedroom doors are hung on the jamb nearest their own side wall, so an open leaf lies flat
	// against that wall instead of standing in the middle of the room.
	B.AddDoor(TEXT("D_Foyer"),   TEXT("W_Mid_Lower"), 900.0,  EHFSwing::OutwardLeft, MainDoorWidth);
	B.AddDoor(TEXT("D_Living"),  TEXT("W_Mid_Lower"), 5400.0, EHFSwing::OutwardLeft);
	// 7400, not 7200. COL_M1 stands on the junction at (6600, 3600) where W_Living_Bed2 and
	// W_Mid_Lower meet and BM_Living_Bed2 lands on BM_Mid_Lower, and its east face is at 6825; at
	// 7200 the clear opening started at 6750 and the door leaf was built 75 mm inside the column.
	// The column is not free to move - sliding it leaves a 3600 beam unsupported and puts an RCC
	// member mid-span in the living room - and the doorway has 4200 of bedroom wall to sit anywhere
	// in, so the doorway moved. 125 clear to the column, 192 to the corridor's east wall.
	B.AddDoor(TEXT("D_Bed2"),    TEXT("W_Mid_Lower"), 7400.0, EHFSwing::OutwardLeft);
	B.AddDoor(TEXT("D_Kitchen"), TEXT("W_Mid_Upper"), 1200.0, EHFSwing::InwardRight);
	B.AddDoor(TEXT("D_MBed"),    TEXT("W_Mid_Upper"), 4850.0, EHFSwing::InwardLeft);
	// 9575, not 8700. The master bathroom has TWO doors - this one from the bedroom in its north
	// wall, and D_BalcE onto the service balcony in its east wall - and at 8700 they fought over the
	// same 1685 of room depth. A 900 shower has to stand somewhere, and with the bedroom door at the
	// west end the only corner left for it was the east one, directly in front of D_BalcE; the last
	// pass moved the shower there to clear this door and walled the balcony door in doing it.
	//
	// Moving this door east instead settles both at once: it frees the whole west end of the bath for
	// the shower and leaves the east end clear as the approach to the balcony. 9200..9950 is as far
	// east as it can go - F_MBed_Wardrobe stands at X 10085..10685 on the bedroom side, so anything
	// beyond this puts a 2400 wardrobe across the doorway from the other room.
	B.AddDoor(TEXT("D_MBath"),   TEXT("W_Mid_Upper"), 9575.0, EHFSwing::OutwardLeft, 750.0);
	B.AddDoor(TEXT("D_CBath"),   TEXT("W_CBath_Corr"), 900.0, EHFSwing::InwardLeft, 750.0);
	B.AddDoor(TEXT("D_Utility"), TEXT("W_Kitchen_Util_S"), 600.0, EHFSwing::InwardLeft, 750.0);

	// --------------------------------------------------------------------------------- rooms
	// The order matters beyond readability: the drawing generator numbers its elevation sheets from
	// it, so the eleven committed sheet filenames follow this list. Re-order it and the set renames.
	B.AddRoom(TEXT("R_Living"),   TEXT("Living / Dining"),  EHFRoomType::Living,        X0, Y0, X3, Y1);
	B.AddRoom(TEXT("R_Bed2"),     TEXT("Bedroom 2"),        EHFRoomType::Bedroom,       X3, Y0, X5, Y1);
	B.AddRoom(TEXT("R_Foyer"),    TEXT("Foyer"),            EHFRoomType::Foyer,         X0, Y1, X1, Y2);
	B.AddRoom(TEXT("R_Corridor"), TEXT("Corridor"),         EHFRoomType::Corridor,      X2, Y1, X4, Y2);
	B.AddRoom(TEXT("R_CBath"),    TEXT("Common Bathroom"),  EHFRoomType::Bathroom,      X1, Y1, X2, Y2, 0.0);
	B.AddRoom(TEXT("R_MBath"),    TEXT("Master Bathroom"),  EHFRoomType::Bathroom,      X4, Y1, X5, Y2, 0.0);
	B.AddRoom(TEXT("R_Utility"),  TEXT("Utility"),          EHFRoomType::Utility,       XU, YU, X2, Y3, 0.0);
	// The one room in the plan that is not a rectangle: the utility is boxed out of its north-east
	// corner, so it wraps round it as an L. Listed counter-clockwise like every other boundary.
	B.AddRoomPoly(TEXT("R_Kitchen"), TEXT("Kitchen"), EHFRoomType::Kitchen, {
		FVector2D(X0, Y2), FVector2D(X2, Y2), FVector2D(X2, YU),
		FVector2D(XU, YU), FVector2D(XU, Y3), FVector2D(X0, Y3)
	});
	B.AddRoom(TEXT("R_MBed"),     TEXT("Master Bedroom"),   EHFRoomType::MasterBedroom, X2, Y2, X5, Y3);
	B.AddRoom(TEXT("R_Balcony"),   TEXT("Balcony"),           EHFRoomType::Balcony,      X0, YB, X2, Y0, 0.0);
	B.AddRoom(TEXT("R_BalconyN"),  TEXT("Balcony 2"),         EHFRoomType::Balcony,      X3, Y3, X5, YN, 0.0);
	// Not "Wash Area Balcony" any more. The wash area is the utility, which is where the machine is;
	// this one is reached only through the master bathroom, so it is that bathroom's service balcony
	// and nothing else can honestly be put on it.
	B.AddRoom(TEXT("R_BalconyE"),  TEXT("Service Balcony"),   EHFRoomType::Balcony,      X5, Y1, XE, Y2, 0.0);

	// ------------------------------------------------------------------------ false ceilings
	// Living gets the full cove treatment; bedrooms a peripheral band; wet areas a full drop to
	// conceal plumbing; the corridor a bulkhead over its length.
	//
	// EVERY DROP HERE IS 500, AND NOT ONE OF THEM IS A DESIGN CHOICE. The beams are 450 deep hung
	// from a 3000 slab, so their soffits are at 2550, and six of the eight are 230 wide over 115
	// partitions - they stand 57.5 proud of the plaster on both faces and read as a ledge round the
	// top of every room they border. A false ceiling exists to bury that. These were 200, 300 and
	// 400: every one of them sat ABOVE the beam soffit, so the beam pierced the finished ceiling and
	// hung below it, which is what "a ragged dark line along the top of every wall" was.
	//
	// 500 puts the soffit at 2500, 50 clear below the beams, and a full drop's 20 panel top at 2520
	// still 30 clear - short of touching, because a face landing exactly on the beam soffit is two
	// coplanar faces and the flashing starts again. Clear height 2500 is a normal finished ceiling
	// in a flat of these proportions, and the deep perimeter box a 450 beam forces is exactly how
	// this is detailed on site.
	B.AddCeiling(TEXT("FC_Living"), TEXT("R_Living"), EHFCeilingStyle::Cove, 500.0, 600.0,
		{ FVector2D(1200.0, 1200.0), FVector2D(1200.0, 2400.0), FVector2D(5400.0, 1200.0), FVector2D(5400.0, 2400.0) });

	B.AddCeiling(TEXT("FC_MBed"), TEXT("R_MBed"), EHFCeilingStyle::Peripheral, 500.0, 600.0,
		{ FVector2D(5100.0, 6300.0), FVector2D(8100.0, 6300.0) });

	B.AddCeiling(TEXT("FC_Bed2"), TEXT("R_Bed2"), EHFCeilingStyle::Peripheral, 500.0, 500.0,
		{ FVector2D(7500.0, 1200.0), FVector2D(9600.0, 1200.0) });

	B.AddCeiling(TEXT("FC_Kitchen"), TEXT("R_Kitchen"), EHFCeilingStyle::FullDrop, 500.0, 0.0,
		{ FVector2D(900.0, 6300.0), FVector2D(2700.0, 6300.0), FVector2D(900.0, 7800.0), FVector2D(2700.0, 7800.0) });

	B.AddCeiling(TEXT("FC_CBath"), TEXT("R_CBath"), EHFCeilingStyle::FullDrop, 500.0, 0.0,
		{ FVector2D(3000.0, 4500.0) });

	B.AddCeiling(TEXT("FC_MBath"), TEXT("R_MBath"), EHFCeilingStyle::FullDrop, 500.0, 0.0,
		{ FVector2D(9450.0, 4500.0) });

	// FC_Living_Beam went with BM_Living_Cross. It existed only to box that beam in, and a 450 deep
	// bulkhead crossing the middle of the living room for no reason is worse than the beam was - the
	// beam at least stopped at 2600, and the bulkhead reached 2550 over the whole width of the room.
	// Nothing else in R_Living needs a localised drop: its cove is a perimeter band and its four
	// downlights sit in that band.

	{
		// A bulkhead follows its own polygon rather than the room, so it needs one explicitly.
		FHFFalseCeiling Bulkhead;
		Bulkhead.Id = TEXT("FC_Corridor");
		Bulkhead.RoomId = TEXT("R_Corridor");
		Bulkhead.Style = EHFCeilingStyle::Bulkhead;

		// 500 for the same reason every other ceiling in this flat is: BM_Mid_Lower and
		// BM_Mid_Upper are the corridor's two long walls, and at 300 this bulkhead stopped 250
		// short of burying either of them.
		Bulkhead.Drop = 500.0;
		Bulkhead.BandWidth = 0.0;
		Bulkhead.ExplicitPolygon = {
			FVector2D(X2, Y1), FVector2D(X4, Y1), FVector2D(X4, Y2), FVector2D(X2, Y2)
		};
		Bulkhead.LightPositions = { FVector2D(5400.0, 4500.0), FVector2D(6900.0, 4500.0) };
		B.Spec.FalseCeilings.Add(Bulkhead);
	}

	// ------------------------------------------------------------------------------ fixtures
	// Living / dining
	{
		// The seating faces the south wall, clear of both doorways: D_Foyer's leaf sweeps X 375..1425
		// and D_Living's X 4950..5850, and the sofa sits between them.
		FHFFixture& Sofa = B.AddFixture(TEXT("F_Sofa"), TEXT("R_Living"), EHFFixtureType::Sofa,
			TEXT("3-seater sofa"), FVector2D(3800.0, 2900.0), FVector2D(2100.0, 900.0), 800.0, 180.0);
		Sofa.AnchorWallId = TEXT("W_Mid_Lower");

		B.AddFixture(TEXT("F_CoffeeTable"), TEXT("R_Living"), EHFFixtureType::CoffeeTable,
			TEXT("Coffee table"), FVector2D(3800.0, 1800.0), FVector2D(1100.0, 600.0), 400.0);

		// The TV run, built around the balcony door rather than beside it.
		//
		// One 1800 unit centred on X 2100 stood across the whole of D_Balcony - not partly, all of
		// it - because the door and the joinery were drawn on different layers and read as separate
		// things. A wall unit on the wall an opening is in is split around the opening: a tall
		// storage column in the pier west of the door, and the console carrying the television in
		// the 1650 pier between the door's east jamb at 3000 and Win_Living's west jamb at 4650.
		// That is how the kitchen's wall units already deal with the window over the sink, and it is
		// what the joinery in front of a balcony door actually is.
		FHFFixture& TallUnit = B.AddFixture(TEXT("F_TVUnit_W"), TEXT("R_Living"), EHFFixtureType::TVUnit,
			TEXT("TV wall unit, tall storage west of the balcony door"),
			FVector2D(690.0, 400.0), FVector2D(900.0, 450.0), 1800.0);
		TallUnit.AnchorWallId = TEXT("W_South");
		TallUnit.Params.ShutterCount = 2;
		TallUnit.Params.ShelfCount = 4;
		TallUnit.Params.HandleStyle = EHFHandleStyle::HandlelessGroove;
		TallUnit.Params.PlinthHeight = 80.0;

		FHFFixture& TVUnit = B.AddFixture(TEXT("F_TVUnit_E"), TEXT("R_Living"), EHFFixtureType::TVUnit,
			TEXT("TV console with drawers, east of the balcony door"),
			FVector2D(3800.0, 400.0), FVector2D(1200.0, 450.0), 600.0);
		TVUnit.AnchorWallId = TEXT("W_South");
		TVUnit.Params.DrawerCount = 3;
		TVUnit.Params.HandleStyle = EHFHandleStyle::HandlelessGroove;
		TVUnit.Params.PlinthHeight = 80.0;

		B.AddFixture(TEXT("F_DiningTable"), TEXT("R_Living"), EHFFixtureType::DiningTable,
			TEXT("4-seater dining"), FVector2D(5100.0, 1800.0), FVector2D(1400.0, 800.0), 750.0);

		FHFFixture& Fan = B.AddFixture(TEXT("F_Fan_Living"), TEXT("R_Living"), EHFFixtureType::CeilingFan,
			TEXT("Ceiling fan"), FVector2D(3300.0, 1800.0), FVector2D(1200.0, 1200.0), 300.0);
		Fan.Params.Diameter = 1200.0;
	}

	// Kitchen: an L-shaped modular run along the west and north walls.
	{
		// footprint.x is always the run length and .y the depth; rotation orients it. A 90 degree
		// yaw therefore puts a 2400 long, 600 deep run against the west wall.
		FHFFixture& BaseWest = B.AddFixture(TEXT("F_Kitchen_BaseW"), TEXT("R_Kitchen"), EHFFixtureType::KitchenBaseCabinet,
			TEXT("Base units, west run"), FVector2D(415.0, 6900.0), FVector2D(2400.0, 600.0), 850.0, 90.0);
		BaseWest.AnchorWallId = TEXT("W_West");
		BaseWest.Params.ShutterCount = 2;
		BaseWest.Params.DrawerCount = 3;
		BaseWest.Params.PlinthHeight = 100.0;
		BaseWest.Params.HandleStyle = EHFHandleStyle::JProfile;

		// 2300, not 3000: the north run now dies into W_Kitchen_Util, whose west face is at 2942.5.
		FHFFixture& BaseNorth = B.AddFixture(TEXT("F_Kitchen_BaseN"), TEXT("R_Kitchen"), EHFFixtureType::KitchenBaseCabinet,
			TEXT("Base units, north run"), FVector2D(1750.0, 7985.0), FVector2D(2300.0, 600.0), 850.0);
		BaseNorth.AnchorWallId = TEXT("W_North");
		BaseNorth.Params.ShutterCount = 3;
		BaseNorth.Params.DrawerCount = 2;
		BaseNorth.Params.PlinthHeight = 100.0;
		BaseNorth.Params.HandleStyle = EHFHandleStyle::JProfile;

		// Counters run along their walls, so they anchor to them like the cabinets beneath.
		FHFFixture& CounterW = B.AddFixture(TEXT("F_Kitchen_CounterW"), TEXT("R_Kitchen"), EHFFixtureType::CounterTop,
			TEXT("Granite counter, west"), FVector2D(415.0, 6900.0), FVector2D(2400.0, 600.0), 40.0, 90.0, 850.0);
		CounterW.AnchorWallId = TEXT("W_West");
		CounterW.Params.UpstandHeight = 100.0;

		FHFFixture& CounterN = B.AddFixture(TEXT("F_Kitchen_CounterN"), TEXT("R_Kitchen"), EHFFixtureType::CounterTop,
			TEXT("Granite counter, north"), FVector2D(1750.0, 7985.0), FVector2D(2300.0, 600.0), 40.0, 0.0, 850.0);
		CounterN.AnchorWallId = TEXT("W_North");
		CounterN.Params.UpstandHeight = 100.0;

		// Two runs, not one, because the window is between them.
		//
		// A single 3000 run centred on the same X as the window put 700 mm of cabinet across the
		// whole width of it. A real kitchen splits the wall units around the window over the sink,
		// which is exactly where this window is. Re-cut now the window has moved with the shortened
		// north wall: X 600..1200 and 2400..2900, with the window's 1200..2400 clear between them.
		// One shutter each at these widths - two would be 300 wide leaves, which is a cupboard
		// nobody builds.
		auto AddWallUnits = [&B](const FName& Id, const FString& Label, double CentreX, double Length)
		{
			FHFFixture& Units = B.AddFixture(Id, TEXT("R_Kitchen"), EHFFixtureType::KitchenWallCabinet,
				Label, FVector2D(CentreX, 8135.0), FVector2D(Length, 300.0), 700.0, 0.0, 1400.0);
			Units.AnchorWallId = TEXT("W_North");
			Units.Params.ShutterCount = 1;
			Units.Params.ShelfCount = 2;
			Units.Params.CorniceHeight = 60.0;
			Units.Params.bHasGlassInsert = true;
			Units.Params.HandleStyle = EHFHandleStyle::Bar;
		};

		AddWallUnits(TEXT("F_Kitchen_WallW"), TEXT("Wall units, north run west of the window"), 900.0, 600.0);
		AddWallUnits(TEXT("F_Kitchen_WallE"), TEXT("Wall units, north run east of the window"), 2650.0, 500.0);

		B.AddFixture(TEXT("F_Kitchen_Sink"), TEXT("R_Kitchen"), EHFFixtureType::Sink,
			TEXT("Double-bowl sink"), FVector2D(1800.0, 7985.0), FVector2D(800.0, 450.0), 200.0, 0.0, 690.0);

		// Set into the west counter, so they turn with it.
		B.AddFixture(TEXT("F_Kitchen_Hob"), TEXT("R_Kitchen"), EHFFixtureType::Hob,
			TEXT("4-burner hob"), FVector2D(415.0, 6300.0), FVector2D(580.0, 500.0), 60.0, 90.0, 850.0);

		FHFFixture& Chimney = B.AddFixture(TEXT("F_Kitchen_Chimney"), TEXT("R_Kitchen"), EHFFixtureType::Chimney,
			TEXT("Chimney"), FVector2D(415.0, 6300.0), FVector2D(600.0, 500.0), 700.0, 90.0, 1500.0);
		Chimney.AnchorWallId = TEXT("W_West");

		// On the kitchen's south wall, not in the corner by the utility.
		//
		// The redraw boxed the utility out of the kitchen's north-east corner and put D_Utility in the
		// new partition at Y 6600, X 3225..3975. The refrigerator was left where it had always been,
		// at (3800, 6000) against W_Kitchen_MBed - which is now 192 mm in front of that doorway,
		// across 525 of its 750 width, for the whole 1800 of its height. It left a 225 mm slot into
		// the room the redraw had just created: the utility could not be walked into.
		//
		// It cannot be nudged clear. The doorway has 750 of wall to sit in and the fridge is 700
		// deep, so anywhere on W_Kitchen_MBed north of the notch is behind the partition and anywhere
		// south of it is still square in front of the door. The fridge has to leave that corner.
		//
		// X 1750..2450 on W_Mid_Upper is clear of D_Kitchen's leaf (which sweeps X 750..1650) by 100,
		// clear of the west run of base units by a metre, and leaves the whole notch free as the
		// approach to the utility. It also makes a better kitchen: sink north, hob west, fridge
		// south is the work triangle those three want to be in.
		FHFFixture& Fridge = B.AddFixture(TEXT("F_Kitchen_Fridge"), TEXT("R_Kitchen"), EHFFixtureType::Refrigerator,
			TEXT("Refrigerator"), FVector2D(2100.0, 5810.0), FVector2D(700.0, 700.0), 1800.0);
		Fridge.AnchorWallId = TEXT("W_Mid_Upper");
	}

	// Master bedroom
	{
		FHFFixture& Bed = B.AddFixture(TEXT("F_MBed_Bed"), TEXT("R_MBed"), EHFFixtureType::Bed,
			TEXT("King bed"), FVector2D(6300.0, 7200.0), FVector2D(1800.0, 2000.0), 600.0, 180.0);
		Bed.AnchorWallId = TEXT("W_North");

		// Clear of the bed's 5400..7200 span so they sit beside it, not clipping into it.
		B.AddFixture(TEXT("F_MBed_Night1"), TEXT("R_MBed"), EHFFixtureType::Nightstand,
			TEXT("Nightstand"), FVector2D(5100.0, 8000.0), FVector2D(450.0, 400.0), 550.0);
		B.AddFixture(TEXT("F_MBed_Night2"), TEXT("R_MBed"), EHFFixtureType::Nightstand,
			TEXT("Nightstand"), FVector2D(7500.0, 8000.0), FVector2D(450.0, 400.0), 550.0);

		// 2400 long run, 600 deep, turned to stand against the east wall at X=10800.
		FHFFixture& Wardrobe = B.AddFixture(TEXT("F_MBed_Wardrobe"), TEXT("R_MBed"), EHFFixtureType::Wardrobe,
			TEXT("4-bay sliding wardrobe with a top-hung loft"), FVector2D(10385.0, 6900.0),
			FVector2D(2400.0, 600.0), 2400.0, 90.0);
		Wardrobe.AnchorWallId = TEXT("W_East");
		Wardrobe.Params.ShutterCount = 4;
		Wardrobe.Params.ShelfCount = 5;
		Wardrobe.Params.bHasLoft = true;
		Wardrobe.Params.LoftHeight = 500.0;
		Wardrobe.Params.bHasHangingRail = true;
		Wardrobe.Params.PlinthHeight = 100.0;
		Wardrobe.Params.HandleStyle = EHFHandleStyle::JProfile;

		// SLIDING, and the room is why rather than fashion. This is 2400 of wardrobe in a bedroom
		// 3000 deep with a bed in it: a hinged leaf on this run swings 591 mm out into the floor it
		// stands on, measured off the built actor's own bounds, and the walkway between the bed and
		// the wardrobe is not that wide. A sliding run is what a flat of this class actually gets
		// here, and it is drawn differently - no swing arcs, leaves that lap instead of a reveal at
		// every bay - so it is something the plan says rather than something anybody assumed.
		//
		// Bedroom 2's wardrobe stays side-hung deliberately. It is 1800 in a room with the space for
		// it, and it keeps a hinged production instance beside the sliding one.
		Wardrobe.Params.ShutterMotion = EHFShutterMotion::Sliding;

		// The loft is a FLAP, not a slider, and Sanitise would refuse it as a slider anyway: a
		// sliding run's gear is a track at the head of the body, and there is nothing above it for a
		// loft leaf to run on. Top-hung is the other thing actually built over a slider - it lifts
		// out and up on stays instead of swinging into the room, which is the same reason the body
		// slides.
		Wardrobe.Params.LoftShutterMotion = EHFShutterMotion::TopHung;

		FHFFixture& Fan = B.AddFixture(TEXT("F_Fan_MBed"), TEXT("R_MBed"), EHFFixtureType::CeilingFan,
			TEXT("Ceiling fan"), FVector2D(6300.0, 6900.0), FVector2D(1200.0, 1200.0), 300.0);
		Fan.Params.Diameter = 1200.0;

		// Beside D_MBed's handle jamb at X 5300, which is where a hand reaches for it on the way in.
		FHFFixture& Switches = B.AddFixture(TEXT("F_Sw_MBed"), TEXT("R_MBed"), EHFFixtureType::SwitchPlate,
			TEXT("6-gang switch plate"), FVector2D(5500.0, 5500.0), FVector2D(220.0, 20.0), 150.0, 0.0, 1200.0);
		Switches.AnchorWallId = TEXT("W_Mid_Upper");
		Switches.Params.GangCount = 6;
	}

	// Bedroom 2
	{
		FHFFixture& Bed = B.AddFixture(TEXT("F_Bed2_Bed"), TEXT("R_Bed2"), EHFFixtureType::Bed,
			TEXT("Queen bed"), FVector2D(8300.0, 1300.0), FVector2D(1500.0, 2000.0), 600.0);
		Bed.AnchorWallId = TEXT("W_South");

		FHFFixture& Wardrobe = B.AddFixture(TEXT("F_Bed2_Wardrobe"), TEXT("R_Bed2"), EHFFixtureType::Wardrobe,
			TEXT("3-door wardrobe"), FVector2D(10385.0, 2400.0), FVector2D(1800.0, 600.0), 2400.0, 90.0);
		Wardrobe.AnchorWallId = TEXT("W_East");
		Wardrobe.Params.ShutterCount = 3;
		Wardrobe.Params.ShelfCount = 4;
		Wardrobe.Params.bHasLoft = true;

		// Stated, in the millimetres this spec is written in, and it has to be: FHFFixtureParams
		// defaults LoftHeight to 60, which is a CENTIMETRE figure on a struct whose lengths are
		// converted from whatever the spec declares. Left unsaid it came through as a 60 mm loft -
		// 24 mm of clear height once the boards are off it - and AHFWardrobeActor correctly refused
		// to build a loft nothing could be put in, so this wardrobe had a loft on the drawing and
		// none in the level. Nothing caught it while no fixture read the figure.
		Wardrobe.Params.LoftHeight = 450.0;

		Wardrobe.Params.bHasHangingRail = true;
		Wardrobe.Params.PlinthHeight = 100.0;
		Wardrobe.Params.HandleStyle = EHFHandleStyle::Bar;

		// Turned onto the room's west wall. It stood against W_Mid_Lower at (7000, 3300), which is
		// now the corridor wall carrying D_Bed2, and the desk was across a third of the doorway.
		// W_Living_Bed2 is 3600 of blank wall with nothing else on it, which is where a study table
		// wants to be anyway - out of the circulation and lit from the side.
		FHFFixture& Study = B.AddFixture(TEXT("F_Bed2_Study"), TEXT("R_Bed2"), EHFFixtureType::StudyTable,
			TEXT("Study table"), FVector2D(6950.0, 1200.0), FVector2D(1200.0, 550.0), 750.0, 90.0);
		Study.AnchorWallId = TEXT("W_Living_Bed2");
		Study.Params.DrawerCount = 2;

		FHFFixture& Fan = B.AddFixture(TEXT("F_Fan_Bed2"), TEXT("R_Bed2"), EHFFixtureType::CeilingFan,
			TEXT("Ceiling fan"), FVector2D(8700.0, 1800.0), FVector2D(1200.0, 1200.0), 300.0);
		Fan.Params.Diameter = 1200.0;
	}

	// Bathrooms
	{
		// Both bathrooms are laid out around their doors rather than beside them, which is what the
		// last pass got wrong in both rooms. It moved each room's fittings with the room and checked
		// them against the walls; it never checked them against the door leaf that has to sweep past
		// them or the floor somebody has to stand on to reach the handle.
		//
		// Common bath, X 1800..4200 clear 1857.5..4142.5, door in the EAST wall at Y 4125..4875
		// hinged on its south jamb and opening in. So the whole quadrant X 3450..4200, Y 4125..4875
		// is leaf, and nothing may stand in it.
		//
		// F_CBath_WC did. At X 3710..4090 it reached 75 mm past the hinge jamb into that quadrant -
		// only 75, but a swept arc is not a doorway and a leaf does not care how little of it is in
		// the way. It fouled the pan at 56 degrees open and lay across it at 90: the door could not
		// be opened. It reads as an obvious mistake now and did not before, because the room was
		// mirrored about its own door when the band was re-cut - the WC that used to sit at the far
		// end from the doorway ended up beside the hinge without moving relative to the room.
		//
		// 2900 puts it 360 clear of the arc, with the basin 235 west of it and the shower north.
		B.AddFixture(TEXT("F_CBath_WC"), TEXT("R_CBath"), EHFFixtureType::WC,
			TEXT("Wall-hung WC"), FVector2D(2900.0, 3960.0), FVector2D(380.0, 600.0), 400.0);
		B.AddFixture(TEXT("F_CBath_Basin"), TEXT("R_CBath"), EHFFixtureType::Basin,
			TEXT("Counter basin"), FVector2D(2200.0, 3900.0), FVector2D(550.0, 450.0), 200.0, 0.0, 800.0);
		// The service band runs Y 3600..5400, so a 900-deep shower must centre at 4900 to keep
		// its far edge at 5350 rather than pushing through the partition. 2900 rather than the
		// room's centre at 3000 leaves 100 clear of D_CBath's leaf tip at 3450.
		B.AddFixture(TEXT("F_CBath_Shower"), TEXT("R_CBath"), EHFFixtureType::Shower,
			TEXT("Shower area"), FVector2D(2900.0, 4900.0), FVector2D(900.0, 900.0), 2100.0);

		// Master bath, X 8100..10800 clear 8157.5..10685, Y 3600..5400 clear 3657.5..5342.5. Two
		// doors: D_MBath in the north wall at X 9200..9950, and D_BalcE in the east wall at
		// Y 4050..4950 onto the service balcony.
		//
		// The east end belongs to D_BalcE and nothing else. Keeping the strip X 9935..10685 clear
		// across the door's full width is the whole of the fix here: last pass the shower stood at
		// X 9450..10350 - 335 mm in front of that door, 500 of its 900 blocked, 2100 tall - and the
		// WC took another 150, so the only way to the balcony door was a 250 mm slot between them.
		// Nobody passes through 250.
		//
		// So the shower goes to the WEST end, which moving D_MBath east has now freed, and the two
		// doors face each other across an open floor instead of fighting over one corner.
		B.AddFixture(TEXT("F_MBath_Shower"), TEXT("R_MBath"), EHFFixtureType::Shower,
			TEXT("Shower area"), FVector2D(8610.0, 4890.0), FVector2D(900.0, 900.0), 2100.0);

		FHFFixture& Vanity = B.AddFixture(TEXT("F_MBath_Vanity"), TEXT("R_MBath"), EHFFixtureType::Vanity,
			TEXT("Vanity unit"), FVector2D(8700.0, 3910.0), FVector2D(900.0, 500.0), 800.0);
		Vanity.AnchorWallId = TEXT("W_Mid_Lower");
		Vanity.Params.ShutterCount = 2;
		Vanity.Params.DrawerCount = 1;
		Vanity.Params.HandleStyle = EHFHandleStyle::Knob;

		B.AddFixture(TEXT("F_MBath_Basin"), TEXT("R_MBath"), EHFFixtureType::Basin,
			TEXT("Counter basin"), FVector2D(8700.0, 3910.0), FVector2D(500.0, 400.0), 180.0, 0.0, 800.0);

		// On the south wall between the vanity and the balcony door's approach, 350 clear of the one
		// and 55 clear of the other.
		B.AddFixture(TEXT("F_MBath_WC"), TEXT("R_MBath"), EHFFixtureType::WC,
			TEXT("Wall-hung WC"), FVector2D(9690.0, 3960.0), FVector2D(380.0, 600.0), 400.0);
	}

	// Utility, off the kitchen, with the machine under its own window and against the outside wall
	// the drain runs down.
	{
		FHFFixture& Washer = B.AddFixture(TEXT("F_Util_Washer"), TEXT("R_Utility"), EHFFixtureType::WashingMachine,
			TEXT("Washing machine"), FVector2D(3370.0, 7985.0), FVector2D(600.0, 600.0), 850.0);
		Washer.AnchorWallId = TEXT("W_North");

		// The wash sink belongs beside the machine, and this is where it has come from.
		//
		// It used to be F_Wash_Sink, out on the east balcony, and that was already true before the
		// redraw for a different reason: the balcony was the wash area and the utility was next to it
		// in the service band. The redraw moved the utility to the kitchen's far corner and left the
		// sink where it was, which made the trip from the washing machine to the sink run utility ->
		// kitchen -> foyer -> living -> corridor -> master bedroom -> master bathroom -> balcony,
		// the last leg of it through somebody's en-suite. Wet laundry does not go that way.
		//
		// North of Y 7420 so it stays out of the 750 approach to D_Utility, and turned onto the east
		// wall so the machine keeps the window.
		FHFFixture& Sink = B.AddFixture(TEXT("F_Util_Sink"), TEXT("R_Utility"), EHFFixtureType::Sink,
			TEXT("Utility sink"), FVector2D(3917.5, 7720.0), FVector2D(600.0, 450.0), 250.0, 90.0, 600.0);
		Sink.AnchorWallId = TEXT("W_Kitchen_MBed");
	}

	// Foyer
	{
		// Beside D_Foyer's handle jamb at X 1425, not at X 1000 where the doorway now is.
		FHFFixture& Switches = B.AddFixture(TEXT("F_Sw_Foyer"), TEXT("R_Foyer"), EHFFixtureType::SwitchPlate,
			TEXT("4-gang switch plate"), FVector2D(1565.0, 3700.0), FVector2D(160.0, 20.0), 120.0, 0.0, 1200.0);
		Switches.AnchorWallId = TEXT("W_Mid_Lower");
		Switches.Params.GangCount = 4;

		FHFFixture& DB = B.AddFixture(TEXT("F_DB"), TEXT("R_Foyer"), EHFFixtureType::DistributionBoard,
			TEXT("Distribution board"), FVector2D(1600.0, 5150.0), FVector2D(300.0, 60.0), 350.0, 90.0, 1800.0);
		DB.AnchorWallId = TEXT("W_Foyer_CBath");

		// Turned onto the foyer's east wall. The foyer's north wall is now the kitchen doorway, and
		// it was against that; the east wall is 1800 of blank partition with the DB high up on it.
		FHFFixture& Shoes = B.AddFixture(TEXT("F_ShoeRack"), TEXT("R_Foyer"), EHFFixtureType::ShoeRack,
			TEXT("Shoe rack"), FVector2D(1600.0, 4300.0), FVector2D(1200.0, 350.0), 900.0, 90.0);
		Shoes.AnchorWallId = TEXT("W_Foyer_CBath");
		Shoes.Params.ShutterCount = 2;
		Shoes.Params.ShelfCount = 3;
		Shoes.Params.PlinthHeight = 80.0;
		Shoes.Params.HandleStyle = EHFHandleStyle::HandlelessGroove;
	}

	// ------------------------------------------------------------- electrical services
	// Sockets, switch plates, AC points and the wet-area services. Drawings carry these on their
	// own layer, and they are what makes a generated flat usable rather than merely furnished.
	{
		auto AddSocket = [&B](const FName& Id, const FName& RoomId, const FVector2D& Position,
			const FName& AnchorWall, double Rotation = 0.0)
		{
			FHFFixture& Socket = B.AddFixture(Id, RoomId, EHFFixtureType::PowerSocket,
				TEXT("Power socket"), Position, FVector2D(160.0, 20.0), 120.0, Rotation, 300.0);
			Socket.AnchorWallId = AnchorWall;
			Socket.Params.GangCount = 2;
			return &Socket;
		};

		auto AddSwitchPlate = [&B](const FName& Id, const FName& RoomId, const FVector2D& Position,
			const FName& AnchorWall, int32 Gangs, double Rotation = 0.0)
		{
			FHFFixture& Plate = B.AddFixture(Id, RoomId, EHFFixtureType::SwitchPlate,
				FString::Printf(TEXT("%d-gang switch plate"), Gangs),
				Position, FVector2D(40.0 * Gangs, 20.0), 150.0, Rotation, 1200.0);
			Plate.AnchorWallId = AnchorWall;
			Plate.Params.GangCount = Gangs;
			return &Plate;
		};

		auto AddSplitAC = [&B](const FName& Id, const FName& RoomId, const FVector2D& Position,
			const FName& AnchorWall, double Rotation = 0.0)
		{
			FHFFixture& Unit = B.AddFixture(Id, RoomId, EHFFixtureType::ACIndoorUnit,
				TEXT("Split AC indoor unit"), Position, FVector2D(900.0, 220.0), 300.0, Rotation, 2200.0);
			Unit.AnchorWallId = AnchorWall;
			return &Unit;
		};

		// Living / dining. The switch plate sits beside D_Living's handle jamb at X 5850, and the
		// TV point follows the console it feeds.
		AddSocket(TEXT("F_Soc_Living_TV"), TEXT("R_Living"), FVector2D(3800.0, 120.0), TEXT("W_South"));
		AddSocket(TEXT("F_Soc_Living_1"),  TEXT("R_Living"), FVector2D(5900.0, 1800.0), TEXT("W_Living_Bed2"), 90.0);
		AddSwitchPlate(TEXT("F_Sw_Living"), TEXT("R_Living"), FVector2D(6060.0, 3480.0), TEXT("W_Mid_Lower"), 8);
		AddSplitAC(TEXT("F_AC_Living"), TEXT("R_Living"), FVector2D(3300.0, 3480.0), TEXT("W_Mid_Lower"));

		// Master bedroom
		AddSocket(TEXT("F_Soc_MBed_1"), TEXT("R_MBed"), FVector2D(5100.0, 8280.0), TEXT("W_North"));
		AddSocket(TEXT("F_Soc_MBed_2"), TEXT("R_MBed"), FVector2D(7500.0, 8280.0), TEXT("W_North"));
		AddSplitAC(TEXT("F_AC_MBed"), TEXT("R_MBed"), FVector2D(6300.0, 5520.0), TEXT("W_Mid_Upper"));

		// Bedroom 2
		AddSocket(TEXT("F_Soc_Bed2_1"), TEXT("R_Bed2"), FVector2D(7000.0, 120.0), TEXT("W_South"));
		// Beside D_Bed2's handle jamb at X 7850.
		AddSwitchPlate(TEXT("F_Sw_Bed2"), TEXT("R_Bed2"), FVector2D(8050.0, 3480.0), TEXT("W_Mid_Lower"), 6);
		AddSplitAC(TEXT("F_AC_Bed2"), TEXT("R_Bed2"), FVector2D(8700.0, 3480.0), TEXT("W_Mid_Lower"));

		// Kitchen: counter-height sockets each side of the window, and the switch plate beside
		// D_Kitchen's handle jamb at X 750 rather than in the far corner of the room.
		AddSocket(TEXT("F_Soc_Kit_1"), TEXT("R_Kitchen"), FVector2D(700.0, 8280.0), TEXT("W_North"))->BaseZ = 1100.0;
		AddSocket(TEXT("F_Soc_Kit_2"), TEXT("R_Kitchen"), FVector2D(2700.0, 8280.0), TEXT("W_North"))->BaseZ = 1100.0;
		AddSwitchPlate(TEXT("F_Sw_Kitchen"), TEXT("R_Kitchen"), FVector2D(550.0, 5520.0), TEXT("W_Mid_Upper"), 6);

		// The extract goes with the utility. The kitchen has a chimney over its hob, which is what
		// actually clears a kitchen; a utility with a washing machine in it has nothing else.
		//
		// Above the window rather than beside it. At X 3950 this fan was cored 125 x 45 straight
		// through COL_N1, whose west face is at 3975 - and it could never have fitted there, because
		// the free strip between Win_Utility's east jamb at 3700 and that column face is 275 mm and
		// the fan is 300 wide. It was moved east with the room and nobody looked at what it landed
		// in; a column is 3000 of concrete and a duct does not get cored through one.
		//
		// The wall's free width is spent, so the fan takes the height instead: the window head is at
		// 2100 and the fan sits at 2200..2500, directly over it and clear of the column entirely.
		// That is where an extract goes in a room this size anyway - high, and on the outside wall.
		FHFFixture& UtilityExhaust = B.AddFixture(TEXT("F_Exh_Utility"), TEXT("R_Utility"),
			EHFFixtureType::ExhaustFan, TEXT("Exhaust fan"),
			FVector2D(3400.0, 8280.0), FVector2D(300.0, 100.0), 300.0, 0.0, 2200.0);
		UtilityExhaust.AnchorWallId = TEXT("W_North");

		// Bathrooms: geyser, mirror and towel rail in each. The geyser goes over the shower and the
		// mirror over the basin, so both follow the fitting they belong to rather than the room's
		// centre line - at a shared CentreX the common bath's mirror hung 300 off its basin and,
		// once the WC moved clear of the door leaf, over the WC instead.
		//
		// The extract is NOT in here. It was, on NorthWall, and that is what put both of them in the
		// wrong wall: see below.
		auto FitOutBathroom = [&B](const FName& Prefix, const FName& RoomId,
			double ShowerX, double BasinX, double TowelX,
			const FName& NorthWall, const FName& SouthWall)
		{
			FHFFixture& Geyser = B.AddFixture(FName(*(Prefix.ToString() + TEXT("_Geyser"))), RoomId,
				EHFFixtureType::Geyser, TEXT("Storage water heater"),
				FVector2D(ShowerX, 5250.0), FVector2D(450.0, 400.0), 450.0, 0.0, 2100.0);
			Geyser.AnchorWallId = NorthWall;

			FHFFixture& Mirror = B.AddFixture(FName(*(Prefix.ToString() + TEXT("_Mirror"))), RoomId,
				EHFFixtureType::Mirror, TEXT("Mirror"),
				FVector2D(BasinX, 3720.0), FVector2D(600.0, 30.0), 800.0, 0.0, 1000.0);
			Mirror.AnchorWallId = SouthWall;

			FHFFixture& Towel = B.AddFixture(FName(*(Prefix.ToString() + TEXT("_Towel"))), RoomId,
				EHFFixtureType::TowelRail, TEXT("Towel rail"),
				FVector2D(TowelX, 3700.0), FVector2D(500.0, 40.0), 60.0, 0.0, 1200.0);
			Towel.AnchorWallId = SouthWall;
		};

		FitOutBathroom(TEXT("F_CBath"), TEXT("R_CBath"), 2900.0, 2200.0, 3500.0,
			TEXT("W_Mid_Upper"), TEXT("W_Mid_Lower"));
		FitOutBathroom(TEXT("F_MBath"), TEXT("R_MBath"), 8610.0, 8700.0, 9950.0,
			TEXT("W_Mid_Upper"), TEXT("W_Mid_Lower"));

		// ------------------------------------------------------ where a bathroom fan actually blows
		// Both of these hung on W_Mid_Upper, because the fit-out took one "north wall" argument and
		// used it for the geyser, which only has to be screwed to something, and for the extract,
		// which has to blow somewhere. W_Mid_Upper is an internal partition along the whole plan, so
		// F_CBath_Exhaust discharged into the KITCHEN and F_MBath_Exhaust into the MASTER BEDROOM.
		//
		// The master bath has an external wall it was not using. The fan goes in it, over D_BalcE,
		// at 2300..2550 - under the 2600 soffit of FC_MBath and clear of BM_East's 2550 soffit.
		FHFFixture& MBathExhaust = B.AddFixture(TEXT("F_MBath_Exhaust"), TEXT("R_MBath"),
			EHFFixtureType::ExhaustFan, TEXT("Exhaust fan"),
			FVector2D(10635.0, 4500.0), FVector2D(250.0, 100.0), 250.0, 90.0, 2300.0);
		MBathExhaust.AnchorWallId = TEXT("W_East");

		// The common bath has no external wall at all, and this shell cannot give it one. Its band
		// runs foyer | common bath | corridor | master bath between two external faces, the foyer
		// needs the west one for the front door and the master bath has the east one, so the common
		// bath is landlocked whichever way the band is cut. What it actually wants is a vertical
		// shaft, and there is nowhere in this footprint to put one.
		//
		// So the fan moves to the corridor wall, alongside Vent_CBath, which already discharges
		// there. That is not ventilation to outside air and it is not pretending to be; it is the
		// difference between a WC extract blowing into circulation space and one blowing into the
		// room the food is cooked in. The shaft is an outstanding item against this plan, not a
		// solved one - see Docs for the note.
		FHFFixture& CBathExhaust = B.AddFixture(TEXT("F_CBath_Exhaust"), TEXT("R_CBath"),
			EHFFixtureType::ExhaustFan, TEXT("Exhaust fan"),
			FVector2D(4100.0, 5100.0), FVector2D(250.0, 100.0), 250.0, 90.0, 2300.0);
		CBathExhaust.AnchorWallId = TEXT("W_CBath_Corr");

		// Utility: the machine point, at machine height on the wall behind it. X 3800, not 3950,
		// which put it 55 into COL_N1's west face once it was set flush with the wall like its
		// siblings; and Y 8280 so it is on the wall rather than floating 25 mm in front of it.
		AddSocket(TEXT("F_Soc_Util"), TEXT("R_Utility"), FVector2D(3800.0, 8280.0), TEXT("W_North"))->BaseZ = 1000.0;
	}

	// ------------------------------------------------------------------------- balconies
	{
		auto AddRailing = [&B](const FName& Id, const FName& RoomId, const FVector2D& Position,
			const FVector2D& Footprint, double Rotation, const FName& AnchorWall)
		{
			FHFFixture& Rail = B.AddFixture(Id, RoomId, EHFFixtureType::Railing,
				TEXT("MS railing"), Position, Footprint, 800.0, Rotation, ParapetHeight);
			Rail.AnchorWallId = AnchorWall;
		};

		AddRailing(TEXT("F_Rail_Balcony"),  TEXT("R_Balcony"),  FVector2D(2100.0, -1440.0), FVector2D(4200.0, 60.0), 0.0, TEXT("W_Balc_South"));
		AddRailing(TEXT("F_Rail_BalconyN"), TEXT("R_BalconyN"), FVector2D(8700.0, 9840.0),  FVector2D(4200.0, 60.0), 0.0, TEXT("W_BalcN_North"));
		AddRailing(TEXT("F_Rail_BalconyE"), TEXT("R_BalconyE"), FVector2D(12240.0, 4500.0), FVector2D(1800.0, 60.0), 90.0, TEXT("W_BalcE_East"));

		// Condensing units live on the balconies, where they belong.
		FHFFixture& OutdoorLiving = B.AddFixture(TEXT("F_ACOut_Living"), TEXT("R_Balcony"),
			EHFFixtureType::ACOutdoorUnit, TEXT("AC outdoor unit"),
			FVector2D(3700.0, -1000.0), FVector2D(800.0, 350.0), 600.0, 90.0);
		OutdoorLiving.AnchorWallId = TEXT("W_Balc_East");

		FHFFixture& OutdoorMBed = B.AddFixture(TEXT("F_ACOut_MBed"), TEXT("R_BalconyN"),
			EHFFixtureType::ACOutdoorUnit, TEXT("AC outdoor unit"),
			FVector2D(10200.0, 9400.0), FVector2D(800.0, 350.0), 600.0, 90.0);
		OutdoorMBed.AnchorWallId = TEXT("W_BalcN_East");

		// F_Wash_Sink has gone to the utility, and with it the last thing that made this a wash area.
		//
		// This balcony opens off the master bathroom and nothing else can reach it, so a sink here
		// was one the washing machine's owner could only get to through somebody's en-suite. It is
		// now what its position makes it - the master bathroom's own service balcony - and it is
		// deliberately left with nothing on it but its railing. Anything put here is a thing that
		// has to be serviced through a private bathroom, which is the mistake the sink was.
	}

	// ------------------------------------------------------- pelmets over the main windows
	{
		auto AddPelmet = [&B](const FName& Id, const FName& RoomId, const FVector2D& Position,
			const FVector2D& Footprint, double Rotation, const FName& AnchorWall)
		{
			FHFFixture& Pelmet = B.AddFixture(Id, RoomId, EHFFixtureType::Pelmet,
				TEXT("Curtain pelmet"), Position, Footprint, 200.0, Rotation, 2350.0);
			Pelmet.AnchorWallId = AnchorWall;
		};

		AddPelmet(TEXT("F_Pelmet_Living"), TEXT("R_Living"), FVector2D(5400.0, 180.0), FVector2D(1900.0, 180.0), 0.0, TEXT("W_South"));
		AddPelmet(TEXT("F_Pelmet_MBed"),   TEXT("R_MBed"),   FVector2D(7500.0, 8220.0), FVector2D(2200.0, 180.0), 0.0, TEXT("W_North"));
		AddPelmet(TEXT("F_Pelmet_Bed2"),   TEXT("R_Bed2"),   FVector2D(8700.0, 180.0), FVector2D(1900.0, 180.0), 0.0, TEXT("W_South"));
	}

	return B.Spec;
}

FString FHFSampleHouse::GetCommittedSpecPath()
{
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("HouseForge"));
	if (!Plugin.IsValid())
	{
		return FString();
	}

	return FPaths::ConvertRelativePathToFull(
		FPaths::Combine(Plugin->GetBaseDir(), TEXT("Reference"), TEXT("Specs"), TEXT("Sample2BHK.json")));
}

bool FHFSampleHouse::ExportCommittedSpec(FString& OutError)
{
	const FString Path = GetCommittedSpecPath();
	if (Path.IsEmpty())
	{
		OutError = TEXT("Could not locate the HouseForge plugin directory.");
		return false;
	}

	return FHFSpecSerializer::SaveToFile(Make2BHK(), Path, OutError);
}

static FAutoConsoleCommand GExportSampleSpecCommand(
	TEXT("HouseForge.ExportSampleSpec"),
	TEXT("Regenerates Reference/Specs/Sample2BHK.json from FHFSampleHouse::Make2BHK()."),
	FConsoleCommandDelegate::CreateStatic([]()
	{
		FString Error;
		if (FHFSampleHouse::ExportCommittedSpec(Error))
		{
			UE_LOG(LogHouseForge, Display, TEXT("Exported sample spec to %s"), *FHFSampleHouse::GetCommittedSpecPath());
		}
		else
		{
			UE_LOG(LogHouseForge, Error, TEXT("Failed to export sample spec: %s"), *Error);
		}
	}));
