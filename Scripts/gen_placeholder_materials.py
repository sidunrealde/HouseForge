"""Authors the placeholder material set HouseForge dresses generated geometry with.

Run once, in the editor, to (re)create the assets under /HouseForge/Materials. They are then
committed and loaded at runtime by FHFMaterialLibrary; nothing calls this at generation time.

    UnrealEditor-Cmd.exe HouseBuilder.uproject ^
        -run=pythonscript -script="Plugins/HouseForge/Scripts/gen_placeholder_materials.py" ^
        -unattended -nopause -nosplash -stdout

Why a script rather than assets authored by hand: the set has to stay in step with
EHFSurfaceRole, and the only way to see at a glance that it does is to have the roles, their
colours and their finishes written down in one readable list. Re-running is idempotent - each
asset is overwritten in place, so material instances that a level already references keep their
identity and no level needs re-saving.

This is milestone 10's placeholder, not milestone 10. There are no texture maps, no tiling
controls and no per-role parameter editing here on purpose; what this produces is a default look
that reads as a room, and that a real material library replaces wholesale later.
"""

import unreal

FOLDER = "/HouseForge/Materials"

OPAQUE_PARENT = "M_HF_Surface"
GLAZED_PARENT = "M_HF_SurfaceGlazed"

# Roles, in EHFSurfaceRole order. The name must match the enumerator exactly: FHFMaterialLibrary
# builds the asset path from StaticEnum's name string, so a mismatch shows up as a role that
# renders in the default checkerboard.
#
# Colours are sRGB, the space they were picked in, and converted to linear below. Entering them
# straight as linear is the classic way to end up with a flat that is uniformly too dark and too
# saturated while every individual number looks reasonable in the diff.
#
# Chosen as a plausible mid-range Indian flat rather than as a colour key: white POP ceiling, warm
# cream walls, a glossy vitrified floor, dark granite skirting and counters, pre-laminated ply
# carcasses. A room lit like a colour test chart is harder to judge than one lit like a room.
ROLES = [
    # name,              sRGB base colour,        rough, metal, spec, opacity
    ("WallPaint",        (0.86, 0.83, 0.76),      0.88,  0.0,   0.30, None),
    ("FloorFinish",      (0.68, 0.65, 0.60),      0.18,  0.0,   0.65, None),
    ("CeilingSoffit",    (0.96, 0.96, 0.96),      0.90,  0.0,   0.25, None),
    # Warmer and deeper than the soffit it is cut into, so a cove reads as a cove from below
    # instead of vanishing into the ceiling it sits in.
    ("CoveInterior",     (0.80, 0.71, 0.58),      0.85,  0.0,   0.25, None),
    ("Skirting",         (0.28, 0.26, 0.25),      0.30,  0.0,   0.55, None),
    ("JoineryCarcass",   (0.74, 0.63, 0.48),      0.75,  0.0,   0.35, None),
    ("ShutterLaminate",  (0.34, 0.42, 0.41),      0.38,  0.0,   0.50, None),
    ("CounterStone",     (0.15, 0.15, 0.16),      0.15,  0.0,   0.70, None),
    # The one translucent role. A window drawn as an opaque pane reads as a boarded-up hole.
    ("Glass",            (0.78, 0.86, 0.88),      0.03,  0.0,   1.00, 0.14),
    ("MetalHardware",    (0.58, 0.59, 0.60),      0.28,  1.0,   0.50, None),
    ("DoorLeaf",         (0.43, 0.27, 0.17),      0.45,  0.0,   0.45, None),
    ("WindowFrame",      (0.23, 0.23, 0.24),      0.35,  1.0,   0.50, None),
    ("Sanitary",         (0.96, 0.96, 0.95),      0.06,  0.0,   0.85, None),
    ("Fabric",           (0.47, 0.41, 0.36),      0.95,  0.0,   0.20, None),
    ("Appliance",        (0.66, 0.67, 0.68),      0.26,  1.0,   0.50, None),
    ("Structure",        (0.56, 0.55, 0.53),      0.88,  0.0,   0.30, None),
]


def srgb_to_linear(c):
    """The sRGB transfer function. Unreal's vector parameters are linear."""
    if c <= 0.04045:
        return c / 12.92
    return ((c + 0.055) / 1.055) ** 2.4


def linear_colour(srgb, alpha=1.0):
    r, g, b = srgb
    return unreal.LinearColor(
        srgb_to_linear(r), srgb_to_linear(g), srgb_to_linear(b), alpha
    )


def replace_asset(name, asset_class, factory):
    """Creates an asset, deleting any existing one so a re-run is a clean re-author."""
    path = "{}/{}".format(FOLDER, name)
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        unreal.EditorAssetLibrary.delete_asset(path)
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    return tools.create_asset(name, FOLDER, asset_class, factory)


def scalar_param(material, param_name, default, x, y):
    node = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionScalarParameter, x, y
    )
    node.set_editor_property("parameter_name", param_name)
    node.set_editor_property("default_value", default)
    return node


def vector_param(material, param_name, default, x, y):
    node = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionVectorParameter, x, y
    )
    node.set_editor_property("parameter_name", param_name)
    node.set_editor_property("default_value", default)
    return node


def build_parent(name, translucent):
    """A single parameterised surface. Every role is an instance of one of these two."""
    material = replace_asset(name, unreal.Material, unreal.MaterialFactoryNew())

    if translucent:
        material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
        # Per-pixel surface lighting, not the default volumetric mode: glass with no specular
        # response is a grey film, and the highlight is most of what says "pane" rather than "gap".
        material.set_editor_property(
            "translucency_lighting_mode",
            unreal.TranslucencyLightingMode.TLM_SURFACE_PER_PIXEL_LIGHTING,
        )
        material.set_editor_property("two_sided", True)

    base = vector_param(material, "BaseColor", unreal.LinearColor(0.5, 0.5, 0.5, 1.0), -400, 0)
    rough = scalar_param(material, "Roughness", 0.5, -400, 200)
    metal = scalar_param(material, "Metallic", 0.0, -400, 320)
    spec = scalar_param(material, "Specular", 0.5, -400, 440)

    connect = unreal.MaterialEditingLibrary.connect_material_property
    connect(base, "", unreal.MaterialProperty.MP_BASE_COLOR)
    connect(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    connect(metal, "", unreal.MaterialProperty.MP_METALLIC)
    connect(spec, "", unreal.MaterialProperty.MP_SPECULAR)

    if translucent:
        opacity = scalar_param(material, "Opacity", 0.15, -400, 560)
        connect(opacity, "", unreal.MaterialProperty.MP_OPACITY)

    unreal.MaterialEditingLibrary.recompile_material(material)
    unreal.EditorAssetLibrary.save_loaded_asset(material)
    return material


def build_instance(role, colour, rough, metal, spec, opacity, opaque, glazed):
    name = "MI_HF_{}".format(role)
    instance = replace_asset(
        name, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew()
    )

    parent = glazed if opacity is not None else opaque
    unreal.MaterialEditingLibrary.set_material_instance_parent(instance, parent)

    unreal.MaterialEditingLibrary.set_material_instance_vector_parameter_value(
        instance, "BaseColor", linear_colour(colour)
    )
    for param, value in (("Roughness", rough), ("Metallic", metal), ("Specular", spec)):
        unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(
            instance, param, value
        )
    if opacity is not None:
        unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(
            instance, "Opacity", opacity
        )

    unreal.MaterialEditingLibrary.update_material_instance(instance)
    unreal.EditorAssetLibrary.save_loaded_asset(instance)
    return instance


def main():
    if not unreal.EditorAssetLibrary.does_directory_exist(FOLDER):
        unreal.EditorAssetLibrary.make_directory(FOLDER)

    opaque = build_parent(OPAQUE_PARENT, translucent=False)
    glazed = build_parent(GLAZED_PARENT, translucent=True)

    for role, colour, rough, metal, spec, opacity in ROLES:
        build_instance(role, colour, rough, metal, spec, opacity, opaque, glazed)
        unreal.log("HouseForge: authored MI_HF_{}".format(role))

    unreal.log(
        "HouseForge: {} placeholder materials written to {}".format(len(ROLES), FOLDER)
    )


main()
