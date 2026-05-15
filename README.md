# MewFurnitureFramework
<img width="800" height="500" alt="mewfurnframework" src="https://github.com/user-attachments/assets/aab54df6-3350-4258-ae73-f7da520a3fbf" />

A DLL mod for Mewgenics that acts as a framework for the creation and loading of new custom furniture mods!

This is designed to work alongside [Mewjector](https://github.com/githubuser508/mewjector) and is a requirement.
This allows modders to add new furniture data without replacing the game's original `furniture_info.data` file.

Instead of asking every mod to ship a full edited copy of `furniture_info.data`, the framework scans installed framework mods for small additive furniture data files, merges the new rows into the loaded furniture database in memory, and leaves the base game file untouched!

It also includes an improved in-game furniture editor, making it easier to build, test, and package new furniture for release. The framework additionally includes Jack's shop testing tools, letting modders force new modded furniture into the shop to confirm it appears, can be bought, and uses the expected price.

## Features

- **Additive furniture loading**  
  Ship only your new relevant furniture data instead of a full replacement `furniture_info.data`.

- **Runtime merge**  
  The framework patches the loaded furniture data in memory when the game loads `furniture_info.data`.

- **Duplicate protection**  
  Furniture names already present in the base data, another loaded append file, or the optional vanilla name list are skipped instead of duplicated.

- **Mod-folder scanning**  
  The framework searches common mod and output folders for furniture append files.

- **In-editor capture workflow**  
  Capture the currently selected furniture from the furniture editor directly into an append file.

- **Improved furniture debug overlay**  
  The debug overlay for the game's furniture editor now includes various helpful logs, improved explanations, auto-shows upon furniture editor entry, and has formatting/text improvements as well.

- **Vanilla name dumping**  
  Dump the game's loaded vanilla furniture names to a text file for reference and duplicate protection.

- **Jack shop furniture testing**  
  Force a configured furniture item into Jack's shop or reroll Jack's visible shop furniture so modders can quickly confirm that their furniture can appear, be bought, and has the expected price.

## How it works

On startup, MewFurnitureFramework hooks the game's packed-file loading path. When `furniture_info.data` is loaded, the framework validates the base file, scans for furniture append files, appends new rows, updates the row count, and swaps the merged data into memory.

The original game file is not modified.

Furniture rows are matched by name. If a row name already exists, that row is ignored so that mods do not accidentally overwrite each other or the base game.

## Installation

Install the framework like you do any other mod, using your normal Mewjector/Mewtator setup!

Layout should look like this:

```text
  mods/
    MewFurnitureFramework/
      MewFurnitureFramework.dll
    MyFurnitureMod/
      data/
        furniture_info.data.append
```

The framework can find and load furniture append files for each furniture mod in the game's `mods` folder.

## Using furniture mods

1. Install `MewFurnitureFramework`.
2. Place furniture mods inside the game's `mods` folder.
3. Make sure each furniture mod includes one of the supported furniture append files.
4. Launch the game.

When the game loads `furniture_info.data`, the framework merges any valid append files it finds.

### Furniture pool behavior

Once the framework merges a modded furniture row, the game treats that row as part of the loaded furniture database. This means modded furniture is automatically eligible for Jack's shop furniture pool, not only that, but
modded furniture may also be available through other furniture pools or furniture-selection systems. 

## Setting up a furniture mod

Before using the furniture editor capture tools, create a separate mod folder for your furniture mod. The framework expects each furniture mod to live alongside `MewFurnitureFramework` in the game's `mods` folder.

For a complete working example, see the example furniture mod repository:

[Example Furniture Mod](https://github.com/Pseudonym-Tim/mewgenics-super-meat-boy-arcade-machine-furniture)

Your furniture mod should use this layout:

```text
mods/
  MewFurnitureFramework/
    MewFurnitureFramework.dll
  MyFurnitureMod/
    data/
      furniture_info.data.append
    MewFurnitureUninstaller.exe
```

Make sure your furniture mod is installed in the game's `mods` folder so MewFurnitureFramework can find it while the game is running.

A basic furniture mod setup process looks like this:

1. Create a new mod folder for your furniture mod.
2. Create a `data` folder inside that mod folder.
3. Add or copy your `furniture_info.data.append` file (generated with the improved in-game furniture editor) into the `data` folder.
4. Make sure to include the standalone `MewFurnitureUninstaller.exe` in the root of the furniture mod folder before release. You can find the uninstaller attached to this repository's releases, or in the root folder of the example furniture mod.
5. Make sure you have both `MewFurnitureFramework` and your furniture mod through your normal Mewjector/Mewtator setup.
6. Launch the game and confirm that the framework loads your append file.

When using the in-game furniture editor capture workflow, captured furniture data is first written to:

```text
MewFurnitureFramework/output/furniture_info.data.append
```

For release, copy/cut that generated append file into your furniture mod's data folder:

```text
MyFurnitureMod/data/furniture_info.data.append
```

### Removing furniture mods from saves

Furniture mods can affect save files! If a save contains furniture from a mod, removing that mod through just your modloader or through deleting the folder will leave the save referencing furniture data that no longer exists!

To solve this problem, each furniture mod should come packaged with its own standalone `MewFurnitureUninstaller.exe`. You can find the uninstaller attached to this repository's releases, or in the root folder of the example furniture mod. Use the uninstaller found in the root directory of the specific furniture mod you want to remove after normal mod folder deletion or mod-loader disable. It will uninstall that mod's furniture from your saves so you can keep playing those saves without that furniture mod installed!

## Using the in-game furniture editor

For setup instructions, see [Setting up a furniture mod](#setting-up-a-furniture-mod).

To use the in-game furniture editor, please follow these steps:

1. Install and open Mewtator.
2. Go to Mewtator's settings.
3. Enable the dev mode and enable the debug console
4. Make sure your furniture mod is already set up and installed.
5. Launch the game through Mewtator.
6. Open the game's furniture editor from the dev/debug tools.

## Creating a furniture append file

Before capturing furniture data, make sure your furniture mod is already set up. See [Setting up a furniture mod](#setting-up-a-furniture-mod) for the full setup process.

1. Launch the game through Mewtator with dev mode enabled and MewFurnitureFramework installed.
2. Open the furniture editor.
3. Select the furniture entry you want to capture.
4. Use one of the capture hotkeys:

| Hotkey | Action |
| --- | --- |
| `Ctrl + F8` | Capture or update the selected furniture row in the append file. |
| `Ctrl + Shift + F8` | Replace the append file with only the selected furniture row. |

Captured furniture data is written to:

```text
MewFurnitureFramework/output/furniture_info.data.append
```
For release, copy/cut that generated append file into your furniture mod's data folder.

Use `Ctrl + F8` when building a multi-furniture mod pack for multiple custom furniture items. Use `Ctrl + Shift + F8` when you just want a clean one-furniture append file for the currently selected furniture item.

## Testing your furniture in Jack's shop

MewFurnitureFramework includes a small Jack shop test helper, so modders can quickly confirm that their furniture can appear in the shop, be bought, and display the expected cost. 

Configure your test furniture in:

```text
MewFurnitureFramework/jack_shop_test.txt
```

The most important options are:

```text
@furniture=YourFurnitureInternalName
@slot=0
@rare=0
@refresh_before_replace=1
```

`@furniture` should match the internal furniture name from your append file. `@slot` chooses which visible Jack shop slot gets replaced, starting at `0`. `@rare` controls whether the item uses the normal or rare furniture pricing path. `@refresh_before_replace=1` rerolls Jack's shop before placing your configured test furniture into the chosen slot.

Use these hotkeys while Jack's shop is open:

| Hotkey | Action |
| --- | --- |
| `Ctrl + F10` | Reroll Jack's current shop furniture. |
| `Ctrl + Shift + F10` | Reroll Jack's shop, then inject the configured test furniture into the configured slot. |

## Furniture editor improvements

MewFurnitureFramework improves the existing in-game furniture editor in a number of ways.

New functionality has been added:

| Hotkey | Action |
| --- | --- |
| `F7` | Toggle the furniture debug overlay. |
| `Ctrl + F8` | Capture/update the selected furniture row. |
| `Ctrl + Shift + F8` | Overwrite the append file with the selected row. |
| `Ctrl + F9` | Dump vanilla furniture names. |

Additionally, the debug overlay for the furniture mode has also been improved: Various helpful logs, improved explanations, auto-shows upon furniture editor entry, and formatting/text improvements as well.
