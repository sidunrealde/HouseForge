"""Authors DA_HF_MaterialLibrary, and writes its finishes onto the role material instances.

Run in the editor after gen_materials.py, which builds the graphs this fills in:

    UnrealEditor-Cmd.exe HouseBuilder.uproject ^
        -run=pythonscript -script="Plugins/HouseForge/Scripts/gen_material_library.py" ^
        -unattended -nopause -nosplash -stdout

WHAT THIS SCRIPT DOES NOT CONTAIN
=================================

Numbers. Not one. The finish table - what every surface role IS, with its colour, roughness,
metallic, coat, tiling and detail - lives in C++, in HFMaterialLibrary.cpp, and a freshly
constructed UHFMaterialLibrary already carries it. So this script creates one, saves it, and asks it
to push itself onto the MI_HF_* instances.

That is the whole point of the milestone this belongs to. Before it, gen_materials.py held a table
of eighteen finishes and the plugin held none, so the values a level rendered with existed only
inside .uasset binaries and nothing in Source could read, test or change them. The table is now in
one place, one piece of code writes it into a material instance, and
HouseForge.Materials.LibraryAndItsInstancesAgree measures the result.

WHY THE PUSH GOES THROUGH C++ RATHER THAN unreal.MaterialEditingLibrary
======================================================================

UHFMaterialLibrary::PushFinish is the same code path the material panel will use when a user drags a
slider. Authoring the shipped assets through a second mechanism would mean a parameter could be
correct at author time and wrong at edit time - or the reverse - with nothing to catch it. It also
means every rule about which parameters exist on which master, and which static switches follow
which texture slot, is stated once.

IDEMPOTENT. Re-running overwrites the asset in place and rewrites the same values, so a level
already referencing these instances keeps its references and nothing needs re-saving.
"""

import unreal

FOLDER = "/HouseForge/Materials"
LIBRARY_NAME = "DA_HF_MaterialLibrary"


def main():
    # THE REGISTRY HAS TO HAVE SEEN THIS FOLDER FIRST. Run as a commandlet the asset registry never
    # scans the plugin's own content, so load_asset fails for a .uasset sitting right there on disk
    # and create_asset then refuses with "already exists in package". See gen_materials.py, which
    # was bitten by exactly this.
    unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous(
        [FOLDER], force_rescan=True)

    if not unreal.EditorAssetLibrary.does_directory_exist(FOLDER):
        unreal.EditorAssetLibrary.make_directory(FOLDER)

    path = "{}/{}".format(FOLDER, LIBRARY_NAME)
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        unreal.EditorAssetLibrary.delete_asset(path)

    factory = unreal.DataAssetFactory()
    factory.set_editor_property("data_asset_class", unreal.HFMaterialLibrary)

    library = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        LIBRARY_NAME, FOLDER, unreal.HFMaterialLibrary, factory)

    if library is None:
        raise RuntimeError("HouseForge: could not create {}".format(path))

    finishes = library.get_editor_property("finishes")
    unreal.log("HouseForge: {} carries {} finishes".format(LIBRARY_NAME, len(finishes)))

    if len(finishes) == 0:
        # A library with no rows would be a silently empty asset that resolves every role to the
        # compiled-in fallback - working, and wrong about where its values came from.
        raise RuntimeError("HouseForge: {} was created empty".format(LIBRARY_NAME))

    written = library.push_all_finishes(unreal.HFMaterialPush.COMMIT)
    unreal.log("HouseForge: pushed {} finishes onto their material instances".format(written))

    if written != len(finishes):
        raise RuntimeError(
            "HouseForge: {} finishes but only {} instances to write to - run gen_materials.py first"
            .format(len(finishes), written))

    # The library, and every instance the push dirtied. By directory rather than by name: the push
    # decides which instances it touched, and reconstructing eighteen asset names from enumerator
    # names here would be a second place to get the naming rule wrong.
    unreal.EditorAssetLibrary.save_loaded_asset(library)
    unreal.EditorAssetLibrary.save_directory(FOLDER, only_if_is_dirty=True, recursive=False)

    unreal.log("HouseForge: {} written to {}".format(LIBRARY_NAME, FOLDER))


main()
