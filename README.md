# Custom Stray Framework
<img width="500" height="500" alt="preview" src="https://github.com/user-attachments/assets/dde8fd76-00ab-4c7a-9ee8-361b83747099" />

DLL mod for Mewgenics that acts as dependency framework for fully customizable stray cat mods, also adding customizable in-game stray spawning behavior!

The framework can also change how stray spawning behaves. Depending on the framework config, it can add extra cats after the game creates a normal stray event, replace each game-created stray candidate with a framework-controlled candidate, mix custom cats with native base-game strays, or spawn cats manually for testing/debugging purposes!

## Installation/Folder layout

A typical install of the framework and custom cat mods looks like this:

```text
mods/
  CustomStrayFramework/
    CustomStrayFramework.dll
    CustomStrayFramework.ini
    debug_spawns.ini

  FelixStrayExample/
    custom_strays.ini
    data/
      custom_cats.gon.append
      text/
        combined.csv.append
```

Custom Stray Framework scans every sibling folder beside it in the same parent `mods/` directory.

Any folder with a `custom_strays.ini` file can contribute cats to the shared custom stray pool. This means multiple separate stray mods can be installed at the same time, and each one can add its own `[Cat:...]` sections!

## Important note about disabling custom stray mods

If a custom stray mod is disabled in Mewtator, its cat definitions will still be picked up by Custom Stray Framework if its files are still present in the mods folder. To actually disable a custom stray definition, set the cat's `Enabled=0` in that mod's `custom_strays.ini`. Alternatively you can safely delete the mod folder!

Example:

```ini
[Cat:Example_Felix]
Enabled=0
```

## How it works

On startup, the framework:

1. Loads `CustomStrayFramework.ini`
2. Loads `debug_spawns.ini`
3. Scans for `custom_strays.ini` files
4. Reads every `[Cat:UniqueID]` section
5. Registers each valid cat into the framework's stray registry
6. Hooks House stray generation so the framework can add or replace strays

A cat definition is only registered if:

- `Enabled=1`
- `Weight` is greater than `0`
- `CustomCatName` is not blank

The framework then uses the registered custom stray pool when a native House stray event occurs or when the debug spawn hotkey is pressed.

## Native stray behavior modes

The main behavior is controlled in `CustomStrayFramework.ini`.

```ini
[Framework]
EnableNativeStrayHook=1
NativeStrayMode=ReplaceEach
```

### `EnableNativeStrayHook`

Controls whether the framework hooks automatic native House stray events.

```ini
EnableNativeStrayHook=1
```

- `1` enables automatic framework-controlled stray behavior.
- `0` leaves automatic native stray behavior alone. Manual/debug spawning can still be used.

### `NativeStrayMode=AppendExtra`

`AppendExtra` keeps the game's original stray event intact, then queues extra framework-controlled strays afterward.

Use this when you want custom strays to appear in addition to ordinary game strays.

```ini
NativeStrayMode=AppendExtra
```

Relevant spawn settings:

```ini
[Spawn]
RollsPerNativeStrayEvent=3
ChancePercent=100
MinExtraCatsPerNativeStrayEvent=1
MaxExtraCats=3
```

This means the framework will make up to 3 extra spawn attempts per native stray event, force at least 1 successful extra spawn if possible, and cap the event at 3 extra framework cats.

### `NativeStrayMode=ReplaceEach`

`ReplaceEach` lets every game-created stray candidate become a framework-controlled candidate. Depending on `CatPool`, the candidate may remain a native base-game stray or be converted into a custom stray.

Use this when you want custom stray mods to participate directly in the normal stray generation flow.

```ini
NativeStrayMode=ReplaceEach
```

In this mode, the framework applies the selected custom cat info to the native stray, writing configured metadata, stats, abilities, passives, age, gender, personality values, and breeding flags.

## Cat pool settings

`CatPool` controls what the framework is allowed to spawn.

```ini
[Spawn]
CatPool=Mixed
NativeExtraWeight=50
CustomExtraWeight=50
```

Supported values:

- `Mixed` - Uses both base-game strays and custom stray definitions.
- `CustomOnly` - Only uses cats from `custom_strays.ini` files.
- `NativeOnly` - Only uses base-game stray cats.

When `CatPool=Mixed`, `NativeExtraWeight` and `CustomExtraWeight` decide how often the framework chooses native versus custom cats.

Example:

```ini
CatPool=Mixed
NativeExtraWeight=25
CustomExtraWeight=75
```

This makes framework-controlled rolls favor custom cats over native strays.

## Custom stray definitions

Custom cats are defined in `custom_strays.ini`. Each cat gets its own section:

```ini
[Cat:Example_Felix]
Enabled=1
Weight=8
SpecialStray=1
SpecialSpawnChancePercent=10
CustomCatName=Felix
LocalizationKey=FELIX_NAME
InlineNameFallback=Felix
SafeMode=0
Gender=0
Age=2
NoBreed=1
```

The section name after `Cat:` is the framework ID. It should be unique enough to avoid conflicts with other mods.

Good examples:

```ini
[Cat:MyMod_Felix]
[Cat:AuthorName_CatName]
```

Avoid generic IDs like:

```ini
[Cat:Felix]
[Cat:Oscar]
[Cat:Jerry]
```

## Cat definition keys

### Basic keys

```ini
Enabled=1
Weight=10
CustomCatName=Felix
LocalizationKey=FELIX_NAME
InlineNameFallback=Felix
SafeMode=0
```

- `Enabled`: `1` loads this cat, `0` disables it.
- `Weight`: weighted random selection value. Higher means more common.
- `CustomCatName`: key from `data/custom_cats.gon.append`.
- `LocalizationKey`: key from `data/text/combined.csv.append`; use `none` to skip localization lookup.
- `InlineNameFallback`: fallback display name if localization fails or is disabled.
- `SafeMode`: if `1`, the framework applies the visual custom-cat template and fallback name but skips optional stats, abilities, passives, and personality writes. Use this for debugging.

### Special stray keys

```ini
SpecialStray=1
SpecialSpawnChancePercent=10
```

- `SpecialStray=1` marks the cat as a rare/special custom stray.
- `SpecialSpawnChancePercent` is a `0` to `100` chance gate used when that special stray is selected.

If a special stray fails its chance roll, the framework attempts to fall back to a non-special custom stray. If no valid fallback exists and native cats are allowed, the framework may fall back to a native random stray depending on the spawn path and pool settings.

### Metadata and breeding keys

```ini
Gender=0
Age=2
NoBreed=1
```

- `Gender`: `0` male, `1` female, `2` neutral.
- `Age`: apparent age in days. Omit to keep the default/template age behavior.
- `NoBreed`: `1` disables breeding, `0` allows breeding.

### Personality keys

```ini
Libido=0.30
Sexuality=0.75
Aggression=0.70
Fertility=0.85
Inbreeding=0.00
```

These are technically optional. If omitted, a game/template default is kept.

Suggested interpretation:

- `Libido`: below `0.30` low, `0.30` to `0.70` average, above `0.70` high.
- `Sexuality`: below `0.10` straight, `0.10` to `0.90` bisexual, above `0.90` gay.
- `Aggression`: personality aggression value.
- `Fertility`: twin-chance coefficient.
- `Inbreeding`: below `0.10` not inbred, `0.10` to `0.25` slight, `0.25` to `0.50` moderate, `0.50` to `0.80` high, above `0.80` extreme.

### Stat keys

```ini
Strength=3
Dexterity=5
Constitution=6
Intelligence=8
Speed=4
Charisma=7
Luck=9
```

These set the cat's base inheritable stats.

### Level-up delta keys

```ini
StrengthDelta=0
DexterityDelta=0
ConstitutionDelta=0
IntelligenceDelta=0
SpeedDelta=0
CharismaDelta=0
LuckDelta=0
```

These write level-up delta stat modifiers on top of the base stats.

### Ability keys

```ini
Basic0=DefaultMove
Basic1=BasicMelee

Accessible0=PoisonNail
Accessible1=
Accessible2=
Accessible3=
```

- `Basic0` and `Basic1` are the two basic active-skill slots.
- `Accessible0` through `Accessible3` are learned/accessible active-skill slots.
- Leave a slot blank to keep it empty.

### Passive keys

```ini
Passive0=FirstImpression
Passive0Level=1
Passive1=
Passive1Level=0
```

- `Passive0` and `Passive1` are passive ability IDs.
- `Passive0Level` and `Passive1Level` set the matching passive's upgrade level.
- Leave the passive name blank for no passive in that slot.

## Tutorial: making your own custom stray mod

This tutorial creates a standalone cat-pack mod that depends on Custom Stray Framework.

### 1. Create your mod folder

Create a new folder beside `CustomStrayFramework`:

```text
mods/
  CustomStrayFramework/
  MyCustomStrays/
```

Inside `MyCustomStrays`, create this structure:

```text
MyCustomStrays/
  custom_strays.ini
  description.json
  data/
    custom_cats.gon.append
    text/
      combined.csv.append
```

### 2. Add the custom cat appearance

In `data/custom_cats.gon.append`, add a custom cat template. The key you choose here is what `CustomCatName` must point to in `custom_strays.ini`.

Example:

```gon
Felix {
    default_frame 1002

    texture 192
    claws 1
    palette 11

    body 20
    head 58
    tail 218
    leg1 21
    leg2 21
    arm1 21
    arm2 21
    lefteye 194
    righteye 194
    lefteyebrow 114
    righteyebrow 114
    leftear 90
    rightear 160
    mouth 216

    voice female9
    pitch .6
}
```

The important part is the template name:

```gon
Felix {
```

That means the matching stray definition should use:

```ini
CustomCatName=Felix
```

### 3. Add the localized name

In `data/text/combined.csv.append`, add a localization row:

```csv
FELIX_NAME,"Felix"
```

Then use that key in your cat definition:

```ini
LocalizationKey=FELIX_NAME
```

If you do not want to use localization, set:

```ini
LocalizationKey=none
InlineNameFallback=Felix
```

### 4. Define the custom stray

In `custom_strays.ini`, add your cat section:

```ini
; =================
; Custom Stray Cats
; =================

[Cat:MyCustomStrays_Felix]
Enabled=1
Weight=8
SpecialStray=0
SpecialSpawnChancePercent=100
CustomCatName=Felix
LocalizationKey=FELIX_NAME
InlineNameFallback=Felix
SafeMode=0
Gender=0
Age=2
NoBreed=1

; Personality
Libido=0.30
Sexuality=0.75
Aggression=0.70
Fertility=0.85
Inbreeding=0.00

; Base stats
Strength=3
Dexterity=5
Constitution=6
Intelligence=8
Speed=4
Charisma=7
Luck=9

; Level-up deltas
StrengthDelta=0
DexterityDelta=0
ConstitutionDelta=0
IntelligenceDelta=0
SpeedDelta=0
CharismaDelta=0
LuckDelta=0

; Basic abilities
Basic0=DefaultMove
Basic1=BasicMelee

; Accessible abilities
Accessible0=PoisonNail
Accessible1=
Accessible2=
Accessible3=

; Passives
Passive0=FirstImpression
Passive0Level=1
Passive1=
Passive1Level=0
```

### 5. Install and test

Your final mod folder should look like this:

```text
mods/
  CustomStrayFramework/
    CustomStrayFramework.dll
    CustomStrayFramework.ini
    debug_spawns.ini

  MyCustomStrays/
    custom_strays.ini
    description.json
    data/
      custom_cats.gon.append
      text/
        combined.csv.append
```

Launch the game with Custom Stray Framework installed. The framework should scan `MyCustomStrays/custom_strays.ini` automatically because it is a sibling folder of `CustomStrayFramework`.

## Testing with the debug spawn hotkey

The framework includes `debug_spawns.ini` for testing your stray cat. By default, the debug spawn hotkey is configured in `CustomStrayFramework.ini`:

```ini
[Framework]
HotkeyVk=118
ReloadHotkeyVk=120
```

Defaults:

- `HotkeyVk=118` is F7.
- `ReloadHotkeyVk=120` is F9.

Example `debug_spawns.ini`:

```ini
[Debug]
Enabled=1
UseNativeStraySpawn=0
Mode=ExplicitList
Count=1

[ExplicitList]
Cat0=MyCustomStrays_Felix
```

Press F7 to reload custom stray definitions and spawn the configured debug cat. Press F9 to force reload `CustomStrayFramework.ini`, `debug_spawns.ini`, and all `custom_strays.ini` files.

### Debug modes

```ini
Mode=WeightedRandom
```

Spawns from loaded custom cats using their `Weight` values.

```ini
Mode=ExplicitList
```

Spawns the exact IDs listed under `[ExplicitList]` as `Cat0`, `Cat1`, `Cat2`, and so on.

```ini
Mode=All
```

Attempts to spawn every loaded custom cat up to `Count`.

## Common configurations/settings

### Make custom cats more common

```ini
[Spawn]
CatPool=Mixed
NativeExtraWeight=25
CustomExtraWeight=75
```

### Only spawn custom cats through the framework

```ini
[Spawn]
CatPool=CustomOnly
```

### Add custom cats without replacing normal strays

```ini
[Framework]
EnableNativeStrayHook=1
NativeStrayMode=AppendExtra
```

### Disable a custom cat

```ini
[Cat:MyCustomStrays_Felix]
Enabled=0
```

Minimum matching localization file:

```csv
FELIX_NAME,"Felix"
```
