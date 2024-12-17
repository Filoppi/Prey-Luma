Luma is a Prey (2017) + Mooncrash DLC mod that re-writes the game's late rendering and post processing phases to improve the game's look without drifting from the artistic vision (believe me!).
The highlight feature is adding HDR support, DLSS and new Ambient Occlusion, making it akin to a smallish Remastered.
The mod works by hooking into the game's code and replacing shaders.
Luma was created by Pumbo (graphics) and Ersh (reverse engineering).

# List of features:
- Added HDR output (scRGB 16bit) (improved tonemapping, reworked all post processing effects)
- Increased buffers quality in SDR and HDR, reducing banding all around
- Improved the quality of dynamic shadow, especially from up close (they had broken filtering that causes them to be blocky) (the maximum shadow render distance is also increased through configs)
- Added a more modern Ambient Occlusion solution (GTAO) (the original AO is also improved in quality)
- Improved Screen Space Reflections (they are not cropped close to the camera anymore, they now get progressively more diffuse with distance, they blend in and out of view more nicely etc etc, their math in general has been refactored for much better looking and more "physically accurate" results)
- Added DLAA+DLSS Super Resolution (on Nvidia GPUs) (OptiScaler can be used to inject FSR 3) (this looks drastically better than the native TAA and has no noticeable ghosting)
- Added RCAS sharpening after TAA (replacing the original basic sharpening implementation, making it look a lot more natural)
- Added Perspective Correction (optional) (a modern type of "lens distortion" that makes the rendering look natural)
- Improved all of the native Anti Aliasing implementations (e.g. SMAA/TAA)
- Improved Anisotropic Filtering (it was not set to 16x on all textures that would benefit from it)
- Improved quality and look of the Sun, Sun Shaft effects and Lens "Optics" effects (e.g. lens flare)
- Improved Motion Blur quality and fixes multiple issues with its motion vectors
- Improved Bloom quality and fixes multiple issues with its generation (e.g. it was trailing at the edge of the screen)
- Improved Ultrawide Aspect Ratio support (Bloom, AO, SSR, Sun Shafts, Lens Optics, Lens Distortion etc did not scale properly with it, e.g. causing the sun to be huge in UW or causing bloom to be stretched, chromatic aberration was stretched in UW) (the game now also exposes the vertical FOV instead of the horizontal one, which was limited to 120 and not ultrawide friendly)
- Improved High Resolution support (the game was mostly developed for 1080p resolution, a multitude of effects did not scale properly to 4k, like the objects highlights overlay, or stars/sun sprites)
- Improved Dynamic Resolution Scaling support (Film Grain, Bloom, TAA, AO, SSR, Sun Shafts, Lens Optics and many other effects did not scale properly with it, causing visible changes in the image when the resolution changed, and its upscaling implementation just did not look very nice)
- Improved High Frame Rate support by unlocking the frame rate beyond 144 (you can change the limit in the menu now) (with DRS you can easily reach 240FPS now, thanks to tweaked settings that made it more stable)
- Improved Swapchain flip model (more responsive for VRR)
- More (e.g. added optional HDR post process filter on pre-rendered video, added settings to turn off Vignette or Camera Motion Blur)!

# How to install:
- Drop all the files into the game installation folder (including "autoexec.cfg" and "system.cfg") (the root folder, not the one with the executable). Override all files (you can make a backup, but Luma just changes a couple configs in the game packages, these changes simply increase the rendering quality and can persist without Luma).
- If you are on GOG, move the files in ".\Binaries\Danielle\x64\" to ".\Binaries\Danielle\x64-GOG\".
- Install the latest VC++ redist before using (https://aka.ms/vs/17/release/vc_redist.x64.exe).
- Install ReShade 6.3.3+ (with Addons support, for DX11, preferably as dxgi.dll) (you can disable the "Generic Depth" and "Effects Runtime Sync" Addons for performance gains).
- Unless you are on Linux/Proton, delete the "d3dcompiler_47.dll" from the main binary folder, it's an outdated shader compiler bundled with the game for "no reason" (Windows will fall back on the latest version of it this way, but Proton doesn't distribute the file so leave it in).

# Information:
- The performance cost on modern GPUs is negligeable, especially when using DLSS SR + Resolution Scaling (in fact, performance might drastically increase in that case).
- The mod is best used with all the graphics settings maxed out in the game, but any setting combination is supported too.
- Set you "game.cfg" to read only to avoid the game clearing most settings from it if changing settings within the game menu, so it's suggested to change your resolution and other settings directly from config before booting the game, or they will get overwritten every time.
- The game's HDR uses the HDR calibration data from Windows 11 and display's EDID.
- The in game brightness slider is best left at default value.
- Before updating the mod, make sure to delete all its previous files. To uninstall, delete all the files and restore the original version of the overwritten ones (or not, they simply change a couple of quality configs, they can stay without Luma).
- The game runs in HDR mode even when targeting SDR. Most ReShade shaders/effects still don't properly support HDR yet, so avoid using them.

# Issues and limitations:
- The Epic Games Store and Microsoft Store versions are not supported (the game data is across all game releases, so one could theoretically force use the Steam or GOG executables).
- The UI will look a bit different from Vanilla due to Luma using HDR/linear blending modes by default. Set "POST_PROCESS_SPACE_TYPE" to 0 or 2 in the advanced settings to make it behave like Vanilla.
- MSAA or Super-sampling are not supported with Luma.
- Changing the resolution after starting the game is not suggested, as some effects get initialized for the original resolution without being resized (vanilla issue).
- FXAA and no AA is not suggested as they lack object highlights and have other bugs (e.g. FXAA can break the game when close to an enemy and looking at the sun) (vanilla issue) (Luma hides FXAA from the settings).
- Sun shafts can disappear while they are still visible if the sun center gets occluded (this is a bug with the original game, it's slightly more noticeable with LUMA because sun shafts are stronger).
- Some objects in some levels disappear at certain camera angles (vanilla issue, lowering object details to high or below fixes it).
- Glass can flicker heavily, especially when there's multiple layers of it (vanilla issue).
- Mission/Items/Enemy indicator icons are misaligned when the game uses lens distortion (vanilla issue).
- Due to Windows limitations, the game cursor will follow the OS SDR White Level (SDR content brightness) instead of the game's UI paper white. Set the Windows SDR content brightness setting to 31 (out of 100) to make it match ~203 nits, as Luma is set to by default.

# Compatibility:
This mod should work with any other mod for Prey, just be careful of what you install, because some of the most popular mods change very random stuff with the game, or its graphics config (they will still be compatible with Luma).
Replace their files with the Luma version if necessary, none of the game's mods rely on config changes, so the Luma version of the configs will work with them too, and Luma only changes what's strictly necessary and with careful research behind it.

# Suggested mods:
- Real Lights plus Ultra Graphics Mod - https://www.nexusmods.com/prey2017/mods/22
  This mod is seemengly great overall, though it changes some arguable stuff, like light placements, fire sprites (swapping them to a different color, which breaks their gameplay design) and adds a strong darkening effect when in combat (which does not look good in HDR).
  Luma overrides the graphics menu changes from this mod, give that it exposes a lot of random and redundant stuff for user control.
  If you use it, do not apply the "autoexec.cfg" and "system.cfg" files from it, because they contain a myriad of random and unsafe changes (use the Luma version of the same files, which is compatible with this mod too).
- 2023 - PREY - Quality of Life Enhancement Mod - https://www.nexusmods.com/prey2017/mods/99
  This shares some features with the mod above, but generally improves on them.
- No-Intro (Skip Startup - Splash Videos) - https://www.nexusmods.com/prey2017/mods/115
- Chairloader - The Prey Modding Framework - https://www.nexusmods.com/prey2017/mods/103
  Not for general usage but it's great for messing around
- Sensitivity Sprint Scale - https://www.nexusmods.com/prey2017/mods/117

# References:
Join our discord: https://discord.gg/DNGfMZgH3f
Source Code: https://github.com/Filoppi/Prey-Luma

# Donations:
- https://www.buymeacoffee.com/realfiloppi (Pumbo)
- https://www.paypal.com/donate?hosted_button_id=BFT6XUJPRL6YC (Pumbo)
- https://ko-fi.com/ershin (Ersh)

# Thanks:
ShortFuse (support), Lilium (support), KoKlusz (testing), Musa (testing), crosire (support), FreshCloth (support), Regevitamins (support), MartysMods (support), Kaldaien (support), nd4spd (testing)

# Third party:
ReShade, ImGui, RenoDX, n3Dmigoto, DKUtil, Nvidia (DLSS), Fubaxiusz (Perfect Perspective), Oklab, Intel (Xe)GTAO, Darktable UCS, AMD RCAS, DICE (HDR tonemapper), Crytek (CryEngine) and Arkane (Prey)