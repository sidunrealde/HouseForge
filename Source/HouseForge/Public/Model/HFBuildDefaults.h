// Copyright Siddartha G. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Geometry/HFFanKit.h"
#include "Geometry/HFJoineryKit.h"
#include "Geometry/HFOpeningParams.h"
#include "Model/HFSpecValidator.h"
#include "HFBuildDefaults.generated.h"

/**
 * The project's construction figures, as a plain value.
 *
 * This is the type that stands between UHFSettings and the generators, and the reason it exists is
 * .claude/rules/04-conventions.md: every generator is a pure function, and a pure function may not
 * reach for a settings singleton. Global state behind a generator would make its output depend on
 * ini contents and on which test ran first, and would end the ability to test any of it headlessly.
 *
 * So the flow is one-directional and has exactly one place where the settings object is touched:
 *
 *     UHFSettings                 the user-editable object in Project Settings
 *       -> Resolve()              read ONCE, in the composing layer
 *       -> FHFBuildDefaults       this - plain, copyable, no UObject anywhere in it
 *       -> ApplyTo(params)        stamps the figures onto a parameter struct
 *       -> generator(params)      unchanged, still pure, still headlessly testable
 *
 * Default-constructed, it carries exactly the figures that were compiled in before any of this was
 * overridable. That is what makes the whole change a no-op until somebody edits something, and it
 * is asserted by HouseForge.Settings.DefaultsMatchTheConstantsTheyReplaced.
 *
 * A test that wants particular figures builds one of these by hand. No settings object need exist,
 * and nothing here will go looking for one.
 */
USTRUCT(BlueprintType)
struct HOUSEFORGE_API FHFJoineryDefaults
{
	GENERATED_BODY()

	// ------------------------------------------------------------------- board & panel thickness

	/** Carcass side, top, bottom and partition board. 18 mm faced ply. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	double CarcassBoardThickness = 1.8;

	/** Finished shutter leaf: 18 mm ply plus a laminate on each face. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	double ShutterLeafThickness = 1.9;

	/** Drawer front, built the same way as a shutter leaf. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	double DrawerFrontThickness = 1.9;

	/** Drawer box side: 12 mm ply, or the 13 mm side of a metal box. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	double DrawerBoxSideThickness = 1.2;

	/** Drawer box bottom, grooved in above the bottom of the sides. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	double DrawerBoxBottomThickness = 0.6;

	// ------------------------------------------------------- reveals, shadow gaps and clearances

	/**
	 * Total gap between one shutter and the next, taken half from each side.
	 *
	 * The most load-bearing figure in the kit. Without it a run of shutters renders as one unbroken
	 * slab, which is the clearest tell there is that joinery was generated rather than built.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	double ShutterRevealGap = 0.3;

	/** The same shadow line between one drawer front and the next. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	double DrawerRevealGap = 0.3;

	/** Gap a hinge leaves between a closed leaf's back and the carcass front edges. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	double ShutterBackClearance = 0.1;

	/** The same behind a drawer front. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	double DrawerBackClearance = 0.1;

	/** How far a shelf's front edge sits behind the carcass front plane, clear of a closing shutter. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	double ShelfFrontSetback = 1.0;

	// ------------------------------------------------------------------------- plinth / toe kick

	/** Height the carcass stands off the floor on. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	double PlinthHeight = 10.0;

	/** Toe kick: how far the plinth front sits behind the shutter face. What makes a run float. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	double PlinthFrontRecess = 5.0;

	/** The same setback at an end on show. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	double PlinthEndRecess = 5.0;

	/** Plinth board. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	double PlinthPanelThickness = 1.8;

	// ------------------------------------------------------------------------ cornice / moulding

	/** Front-to-back depth of the moulding. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	double CorniceDepth = 4.5;

	/** Height of the moulding. 6 caps a kitchen wall unit; 7.5 tops a wardrobe. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	double CorniceHeight = 6.0;

	/** How far the front face stands proud of the shutter face - the shadow line that reads as a cap. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	double CorniceProjection = 2.5;

	/** Size of the front-underside feature: the splay leg, the cove radius, the step. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	double CorniceProfileSize = 2.0;

	/** Chamfer on the exposed arrises. A sharp edge reads as CG under any lighting. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	double CorniceEdgeBevel = 0.2;

	// ------------------------------------------------------------------ shelving & hanging rails

	/** Compartment height the shelf ladder aims for. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	double TargetShelfSpacing = 37.5;

	/** Below this a compartment holds nothing a wardrobe is for. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	double MinUsefulCompartment = 30.0;

	/** 18 mm faced ply. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	double ShelfThicknessPly = 1.8;

	/** 8 mm toughened. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	double ShelfThicknessGlass = 0.8;

	/** How far 18 ply spans before it sags on camera. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	double MaxShelfSpanPly = 90.0;

	/** The same for 8 mm toughened, which sags sooner and more visibly. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	double MaxShelfSpanGlass = 60.0;

	/** Hanging rail tube diameter. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	double HangingRailDiameter = 2.5;

	/** Rail centre below the top of its compartment - the clearance a hanger needs to lift off. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	double HangingRailDrop = 6.5;

	/**
	 * Clear height under a rail below which a rail is not fitted.
	 *
	 * The setting the user asked for by name. 90 is a short-hang bay, and the composed wardrobe's
	 * hanging bay measures 90.8 - correct, but 8 mm of margin. A project that wants full-length
	 * garments to hang needs 150 here, and until this was a setting there was no way to say so.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	double MinHangingClearance = 90.0;

	// -------------------------------------------------------------------------------- shutters

	/** Bay width one shutter closes. 60 is where a hinged leaf starts to sag. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	double ShutterModuleWidth = 45.0;

	/** Swing at open amount 1. Concealed hinges open 100-110, not 90. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	double ShutterOpenAngleDegrees = 100.0;

	/** Width of the frame members around a pane in a glazed leaf. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	double GlazedShutterStileWidth = 6.0;

	/** Pane thickness in a glazed leaf. A real solid, never a plane. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	double ShutterGlassThickness = 0.5;

	// --------------------------------------------------------------------------------- applying
	//
	// One overload per parameter struct. Each stamps ONLY the construction figures and never the
	// dimensions - a plinth's Width and Depth, a shelf stack's Height, are what the composing layer
	// is working out and would be destroyed by a blanket copy. Sentinel fields that mean "use the
	// material's own" are left alone for the same reason.

	void ApplyTo(FHFPlinthParams& Params) const;
	void ApplyTo(FHFCarcassParams& Params) const;
	void ApplyTo(FHFShutterParams& Params) const;
	void ApplyTo(FHFCorniceParams& Params) const;
	void ApplyTo(FHFShelfStackParams& Params) const;
	void ApplyTo(FHFDrawerParams& Params) const;

	/**
	 * The shelf material figures, for the two calls that resolve a stack's zero sentinel.
	 *
	 * The exception to "ApplyTo stamps everything". A shelf's thickness and span follow its MATERIAL,
	 * and the material is not known until the composing layer has decided what the bay is - so these
	 * four cannot be written onto the params in advance without choosing ply or glass too early.
	 * They travel separately and are resolved at the point of use instead:
	 *
	 *     FHFJoineryKit::GenerateShelfStack(Params, Defaults.ShelfFigures())
	 *
	 * Pass this to SanitiseShelfStack or GenerateShelfStack and the project's ShelfThicknessPly,
	 * ShelfThicknessGlass, MaxShelfSpanPly and MaxShelfSpanGlass reach the geometry. Omit it and the
	 * kit's own constants stand, which is what every test that does not care gets.
	 */
	FHFShelfMaterialFigures ShelfFigures() const;

	/**
	 * How many shelves this project wants in a given clear height.
	 *
	 * The other pair that ApplyTo cannot carry, and for a blunter reason than the material figures:
	 * FHFShelfStackParams has no field for either of them. TargetShelfSpacing and MinUsefulCompartment
	 * are not properties of a stack, they are the rule for choosing how many shelves a stack gets, and
	 * that rule is applied before there is a stack to write them on.
	 *
	 * So they were copied out of the settings into this struct and then read by nobody at all. The
	 * only consumer, FHFJoineryKit::ShelfCountForClearHeight, resolves both from its own compiled-in
	 * constants when a caller passes zero, and every caller passed zero. Turning the dial on the
	 * settings page moved nothing - not the geometry, and not even the kit.
	 *
	 * This is the one call that carries them. The composing layer asks the project how many shelves,
	 * rather than asking the kit and getting the kit's opinion back.
	 *
	 * @param ShelfThickness Zero to take the project's ply thickness.
	 */
	int32 ShelfCountFor(double ClearHeight, double ShelfThickness = 0.0) const;

	/** A parameter struct with the project's figures already on it, ready for dimensions. */
	template <typename ParamsType>
	ParamsType Make() const
	{
		ParamsType Params;
		ApplyTo(Params);
		return Params;
	}
};

/**
 * How this project builds its fans.
 *
 * Small, because a fan is small, and separate from the joinery figures because it shares none of
 * them. The two kinds keep their own speeds: 300 rpm is a ceiling fan on speed 5 and 1350 an
 * extract, and a single figure covering both would be wrong for one of them by a factor of four.
 *
 * A SPEED IS A REAL SETTING even though it moves no geometry in a still. It is the figure that turns
 * elapsed time into revolutions for a Sequencer track or a walkthrough pawn, so a render of a fan on
 * a long exposure - or any shot where the blades are meant to blur - depends on it entirely.
 */
USTRUCT(BlueprintType)
struct HOUSEFORGE_API FHFFanDefaults
{
	GENERATED_BODY()

	/** Ceiling fan speed, in revolutions per minute. Signed: the sign is the direction. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	double CeilingFanRpm = 300.0;

	/** Extract speed, in revolutions per minute. An order faster than a ceiling fan, and audible. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	double ExhaustFanRpm = 1350.0;

	/** Blades on a ceiling fan. Three everywhere in India; four is a hotel fitting. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	int32 CeilingFanBladeCount = 3;

	/** Blades on an extract. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	int32 ExhaustFanBladeCount = 5;

	/**
	 * Angle a ceiling fan's blade is set at. Never zero - a flat blade reads as a paper cut-out.
	 *
	 * SPLIT FROM THE EXTRACT'S, the same way the speeds and the blade counts are, and for the same
	 * reason: the two are different objects and one figure covering both is wrong for one of them. A
	 * single BladePitchDegrees stamped 12 over FHFFanKit::DefaultsFor(Exhaust)'s own 22, which made
	 * the kit's figure unreachable from anything the house built and left every extract in the flat
	 * with blades too shallow to read as an impeller - flat spokes in a case.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	double CeilingFanBladePitchDegrees = 12.0;

	/**
	 * Angle an extract's blade is set at. Steeper than a ceiling fan's, because it is one.
	 *
	 * An extract moves a small volume fast through a duct rather than a large one slowly across a
	 * room, so its impeller is deeper-set. 22 is the kit's own figure for the object.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	double ExhaustFanBladePitchDegrees = 22.0;

	/**
	 * Canopy to the top of the motor on a ceiling fan.
	 *
	 * The figure that decides how far the blades hang below the slab, which matters in every room of
	 * this flat that has a false ceiling in it: the rod has to clear the soffit the fan passes
	 * through, and a short one leaves the blades inside the plasterboard.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	double CeilingFanDropLength = 30.0;

	/** Stamps the figures onto a fan's parameters, leaving its dimensions alone. */
	void ApplyTo(FHFFanParams& Params) const;
};

/**
 * Everything the project says about how its house is built.
 *
 * Held by value, copied freely, and the only thing the composing layer needs in hand before it can
 * build parameter structs. Contains no UObject and reads no global state.
 */
USTRUCT(BlueprintType)
struct HOUSEFORGE_API FHFBuildDefaults
{
	GENERATED_BODY()

	/** Doors, sliding windows, ventilators and fixed glazing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	FHFOpeningBuildParams Opening;

	/** The joinery kit's construction figures. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	FHFJoineryDefaults Joinery;

	/** How fast the fans turn, and how they are built. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	FHFFanDefaults Fan;

	/** What the validator judges a spec against. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	FHFValidationLimits Validation;

	/**
	 * The project's settings, resolved.
	 *
	 * THE ONE PLACE the settings object is read. Everything downstream takes the result by value.
	 * Defined in HFSettings.cpp, because this header must stay usable - and this struct must stay
	 * constructible - with no settings object anywhere in the picture.
	 *
	 * Safe before the settings CDO exists and safe in a headless commandlet: falls back to the
	 * compiled-in defaults rather than failing, so a test that never mentions settings gets exactly
	 * the figures it always got.
	 */
	static FHFBuildDefaults FromProjectSettings();
};
