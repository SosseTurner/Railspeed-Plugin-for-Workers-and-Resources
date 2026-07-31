# railspeed

A [TesmioLoader](https://github.com/MaxLegend/TesmioLoader) plugin for **Workers & Resources: Soviet Republic** that raises the game's hardcoded rail speed limits.

Concrete track is capped at 150 km/h, and no game file exposes that number. Give a locomotive a higher `$ENGINE_SPEED` and it still refuses to pass 150. This plugin lifts that ceiling, along with the bridge and tunnel tiers, to whatever you put in a config file.

---

## What it changes

Every infrastructure speed limit in the game comes out of a single lookup function. This plugin repoints six of its return values:

| Stock limit | What uses it | Config key |
|---|---|---|
| 150 km/h | concrete rail | `speed_concrete` |
| 120 km/h | tunnels, and one bridge type | `speed_120` |
| 121 km/h | bridge tier | `speed_121` |
| 135 km/h | bridge tier | `speed_135` |
| 140 km/h | bridge tier | `speed_140` |
| 70 km/h | wood rail | `speed_wood` |

By default the single `speed` key drives everything **except wood rail**, which is left at its stock 70 — the two track tiers exist so the cheap one is slower, and raising it removes any reason to build concrete. Set `speed_wood` explicitly if you disagree.

---

## Requirements

- **Workers & Resources: Soviet Republic** — build **1.1.1.7** (`SOVIET64.exe`, 10,308,096 bytes)
- **TesmioLoader**, installed and working

Every patch site is verified against the exact bytes 1.1.1.7 ships, *before* anything is written. On any other build the mismatched site is skipped and named in `tesmioloader.log`; if all six are skipped the plugin unloads itself. It will not write into code it does not recognise.

---

## Install

1. Copy plugin folder contents into TesmioLoader's `plugins\` folder.
2. Open `railspeed.ini` and set the speed you want.
3. Launch through the TesmioLoader launcher as usual.

Check `tesmioloader.log` to confirm. You are looking for lines like:

```
railspeed  concrete rail   (stock 150) -> 230 km/h
railspeed  5 of 6 limits patched
```

To uninstall, delete the two files. Nothing is written to your save — the game stores track pieces, not the speeds they imply, so a save made with this plugin loads fine without it and returns to stock limits.

---

## Configuration

```ini
[railspeed]
enabled = 1

; concrete rail + every bridge and tunnel tier, in km/h
speed = 230

; optional per-tier overrides; a key set here always wins over `speed`
speed_concrete =
speed_120 =
speed_121 =
speed_135 =
speed_140 =
speed_wood =
```

Any value up to 400 works — the plugin allocates its own constant rather than reusing one of the game's, so you are not restricted to round numbers.

Save the file as **UTF-8 without a BOM**. PowerShell's `-Encoding UTF8` writes a BOM, which stops the section header matching and makes every setting silently fall back to its default.

---

## Things worth knowing

**A locomotive still obeys its own top speed.** The cap a train actually runs at is `min(locomotive, track)`, so raising the track limit to 230 does nothing until the locomotive's `$ENGINE_SPEED` clears it too. Wagons were tested and do not appear to constrain the train — only the locomotive needs raising.

**Trains brake harder from higher speeds.** That is the expected consequence, not a fault: they still slow and stop normally, and no junction speed limit was observed in testing. Pushing the figure far past 230 is worth watching at a busy junction once, but nothing here is known to break.

---

## How it works

Every limit is returned by a rip-relative load of a pooled float constant:

```
F3 0F 10 05 <disp32>        movss xmm0, [rip+disp32]
```

Those constants cannot simply be edited. MSVC pools identical float literals, so the single `.rdata` slot holding `150.0f` is shared with roughly 190 unrelated call sites — gear ratios, camera zoom, UI geometry. Writing `230` over it would corrupt all of them.

So the plugin never touches the constants. It allocates its own floats near the executable and rewrites only the **4-byte displacement** of each load, so the same instruction reads a different number. Nothing else in the process can observe the change. In TesmioLoader's terms this is technique 3 — a data reference rewrite, no trampoline, no stolen bytes, nothing to unwind.

One quirk worth recording: the 120 km/h site loads into `xmm2`, not `xmm0` like the other five (ModRM byte `15` rather than `05`). The compiler picked a different register at that one spot. Only the displacement is rewritten, so the destination register does not matter to the patch — but the guard has to expect the right byte or it will refuse to patch a site that is perfectly fine.

---

## Building

You need **MSVC**. Install Visual Studio (Community is fine) or the standalone Build Tools, and check the **"Desktop development with C++"** workload — that provides `cl.exe`, the Windows SDK, and `vcvars64.bat`.

1. Clone TesmioLoader.

2. Drop this plugin into its `plugins\` folder so the layout is:

   ```
   TesmioLoader\
     build.bat
     src\
       tesmio_api.h
       tesmio_plugin.h
     plugins\
       railspeed\
         railspeed.cpp
         railspeed.ini
   ```

   `build.bat` discovers plugin folders automatically — a folder named `<name>` containing `<name>.cpp`. Nothing needs registering.

3. Open `build.bat` and point `VCVARS` at your actual install. The path in the repo will almost certainly not match yours:

   ```bat
   set VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat
   ```

   Common locations:

   ```
   C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat
   C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat
   ```

4. Run `build.bat` from a `cmd.exe` window **you opened yourself** — double-clicking closes the window on error before you can read it. It calls `vcvars64.bat` itself, so a Developer Command Prompt is not required.

5. Output lands in `build\plugins\railspeed.dll`, with `railspeed.ini` copied alongside it.

The plugin is built `/MT`, like the loader and every other plugin — each has its own CRT and nothing may be freed across the API boundary.

---

## License

GPLv3, the same as TesmioLoader, which this plugin includes headers from and is therefore a derivative work of. See [`LICENSE`](LICENSE) for the full text.

No game code or assets are redistributed here. Addresses and byte patterns documented in the source are factual observations about a binary you must already own.

---

## Credits

Built on [TesmioLoader](https://github.com/MaxLegend/TesmioLoader), which does all the hard work of getting code into the process in the first place. The addresses were found by reverse-engineering `SOVIET64.exe` 1.1.1.7 with Ghidra and Cheat Engine.
