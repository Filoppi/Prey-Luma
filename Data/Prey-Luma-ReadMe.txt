Luma (Prey (2017) + Mooncrash) aims to rewrite the game post processing phase to improve the look of the game without drifting from the original artistic vision (believe me).
The highlight feature is adding HDR support, though benefits are not restricted to HDR users, there's a lot more, and it's akin to a small Remastered.
The mod works by hooking into the game's code and replacing shaders.

Luma was created by Pumbo (graphics) and Ersh (reverse engineering).
Join our discord here: https://discord.gg/DNGfMZgH3f

List of features:
-Added HDR output (scRGB 16bit) (improved tonemapping)
-Increased buffers quality in SDR and HDR, reducing banding all around
-Upgraded quality and look of sun shaft effects and lens "optic" effects (e.g. lens flare)
-Improved the quality of dynamic shadow, especially from up close (they had broken filtering that causes them to be blocky)
-Improved SSAO quality
-Added DLSS Super Resolution (on Nvidia GPUs) (OptiScaler can be used to inject FSR 3)
-Improved all of the native Anti Aliasing implementations
-Improved Motion Blur quality
-Improved Bloom quality
-Improved Ultrawide aspect ratio support (sun shafts and sun lens effects did not scale properly causing the sun to be huge in UW, bloom was stretched in UW, chromatic aberration was stretched in UW, ...)
-Improved Dynamic Resolution Scaling support (film grain, bloom, TAA, lens optics, ... did not scale properly with dynamic rendering resolutions, and it generally just did not look very nice)
-Improved Anisotropic Filtering (it was not set to 16x on all textures)
-Improved swapchain flip model
-More (e.g. the sun got progressively smaller at higher resolutions in some scenes, objects highlights didn't look right at higher resolutions, added optional HDR post process filter on video cutscenes)!

How to use:
Drop all the files into the game installation folder (including "autoexec.cfg" and "system.cfg"), except the "game.cfg" file that goes into the user data folder (this is optional, but suggested).
Install the latest VC++ redist before using (https://aka.ms/vs/17/release/vc_redist.x64.exe).
Before updating the mod, make sure to delete all its previous files. To uninstall, clear all the files (they are unique to the mod).
Install ReShade 6.3.3+ to use this mod (for DX11, preferably as dxgi.dll).
Unless you are on Linux/Proton, delete the "d3dcompiler_47.dll" from the main binary folder, it's an outdated shader compiler bundled with the game for "no reason" (Windows will fall back on the latest version of it this way, but Proton doesn't distribute the file so leave it in).
Preferably, keep your ".\renodx-dev\dump" folder and send them to the developers after long play sessions, so they can catch all the shaders and make them Luma compatible.
Performance cost on modern GPUs is negligeable, especially when using DLSS SR + Dynamic Resolution Scaling.
Set you "game.cfg" to read only to avoid the game clearing most settings from it if changing settings within the game menu, so it's suggested to change your resolution directly from config before booting the game.
The game's HDR uses the HDR calibration data from Windows 11.

Issues and limitations:
-This is currently only compatible with the Steam version of the game (the game data is across all game releases, so one could theoretically force use the Steam executable even on GOG or other game releases).
-The SDR mode is currently contained in an HDR output, so if looked at from an SDR display, it will have a gamma 2.2/sRGB mismatch.
-Prey Luma settings aren't saved so they need to be changed on every boot.
-The UI will look a bit different from Vanilla due to Luma using HDR/linear blending modes by default. Set "POST_PROCESS_SPACE_TYPE" to 0 or 2 in the advanced settings to make it behave like Vanilla.
-Anti Aliasing might show as "None" in the game settings menu even if it internally is engaged to TAA.
-Super-sampling is not supported with Luma.
-Changing the resolution after starting the game is not suggested, as some effects get initialized for the original resolution without being resized (vanilla issue).
-FXAA and no AA is not suggested as they lack object highlights and have other bugs (e.g. FXAA can break the game when close to an enemy and looking at the sun) (vanilla issue).
-Sun shafts can disappear while they are still visible if the sun center gets occluded (this is a bug with the original game, it's slightly more noticeable with LUMA because sun shafts are stronger).
-Some objects in some levels disappear at certain camera angles (vanilla issue, lowering object details to high or below fixes it).
-Glass can flicker heavily when there's multiple layers of it (vanilla issue).
-Due to Windows limitations, the game cursor will follow the OS SDR White Level (SDR content brightness) instead of the game's UI paper white. Set the Windows SDR content brightness setting to 31 (out of 100) to make it match ~203 nits, as Luma is set to by default.

Compatibility:
This mod should work with any other mod for Prey, just be careful of what you install, because some of the most popular mods change very random stuff with the game, or its graphics config (they will still be compatible with Luma).
Replace their files with the Luma version if necessary, none of the game's mods rely on config changes, so the Luma version of the configs will work with them too, and Luma only changes what's strictly necessary and with careful research behind it.
If you want to load the mod with an asi loader instead of through ReShade Addons automatic loading, you can name the asi loader with the following names: bink2w64.dll, dinput8.dll, version.dll, winhttp.dll, winmm.dll (untested), wininet.dll.

Donations:
https://www.buymeacoffee.com/realfiloppi (Pumbo)
https://www.paypal.com/donate?hosted_button_id=BFT6XUJPRL6YC (Pumbo)
https://ko-fi.com/ershin (Ersh)

Thanks:
ShortFuse (support), Lilium (support), KoKlusz (testing), Musa (testing), crosire (support), FreshCloth (support), Regevitamins (support), MartysMods (support), Kaldaien (support), nd4spd (testing)

Third party:
ReShade, ImGui, RenoDX, DKUtil, Nvidia (DLSS), Fubaxiusz (Perfect Perspective), Oklab, Intel (Xe)GTAO, Darktable UCS, AMD RCAS, DICE (HDR tonemapper), Crytek (CryEngine) and Arkane (Prey)