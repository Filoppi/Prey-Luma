/*
 * Copyright (C) 2024 Carlos Lopez and Filippo Tarpini
 * SPDX-License-Identifier: MIT
 */

// Enable when you are developing shaders or code (not debugging, there's "NDEBUG" for that)
#define DEVELOPMENT 0
// Enable when you are testing shaders or code (e.g. to dump the shaders etc etc)
// This is not mutually exclusive with "DEVELOPMENT", but it should be a sub-set of it
// If neither of these are true, then we are in "shipping" mode, with code meant to be used by the final user
#define TEST 0

// "_DEBUG" might already be defined in debug?
// Setting it to 0 causes the compiler to still assume it as defined and that thus we are in debug mode (don't change this manually).
#ifndef NDEBUG
#define _DEBUG 1
#endif // !NDEBUG

#define LOG_VERBOSE ((DEVELOPMENT || TEST) && 0)

// Disables loading the ReShade Addon code (useful to test the mod without any ReShade dependencies)
#define DISABLE_RESHADE 0

#pragma comment(lib, "dxguid.lib")

#include <d3d11.h>
#include <dxgi.h>
#include <dxgi1_6.h>
#include <Windows.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <set>
#include <vector>
#include <semaphore>
#include <utility>

// ReShade dependencies
#include <deps/imgui/imgui.h>
#include <include/reshade.hpp>
#include <source/com_ptr.hpp>
#include <examples/utils/crc32_hash.hpp>
#if 0 // Not needed atm
#include <source/d3d11/d3d11_impl_type_convert.hpp>
#endif

#include "includes/globals.h"
#include "includes/cbuffers.h"
#include "includes/math.h"
#include "includes/matrix.h"
#include "includes/recursive_shared_mutex.h"

#include "utils/format.hpp"
#include "utils/pipeline.hpp"
#include "utils/shader_compiler.hpp"
#include "utils/display.hpp"

#include "native plugin/NativePlugin.h"

#include "dlss/DLSS.h" // see "ENABLE_NGX" inside

#define ICON_FK_CANCEL reinterpret_cast<const char*>(u8"\uf00d")
#define ICON_FK_OK reinterpret_cast<const char*>(u8"\uf00c")
#define ICON_FK_PLUS reinterpret_cast<const char*>(u8"\uf067")
#define ICON_FK_MINUS reinterpret_cast<const char*>(u8"\uf068")
#define ICON_FK_REFRESH reinterpret_cast<const char*>(u8"\uf021")
#define ICON_FK_UNDO reinterpret_cast<const char*>(u8"\uf0e2")
#define ICON_FK_SEARCH reinterpret_cast<const char*>(u8"\uf002")
#define ICON_FK_WARNING reinterpret_cast<const char*>(u8"\uf071")
#define ICON_FK_FILE_CODE reinterpret_cast<const char*>(u8"\uf1c9")

#define ImTextureID ImU64

// Depends on "DEVELOPMENT"
#define TEST_DLSS (DEVELOPMENT && 0)

#define DLSS_KEEP_DLL_LOADED 1

#define FORCE_KEEP_CUSTOM_SHADERS_LOADED 1

// This might not disable all shaders dumping related code, but it disables enough to remove any performance cost
#define ALLOW_SHADERS_DUMPING (DEVELOPMENT || TEST)

// NOLINTBEGIN(readability-identifier-naming)

// These are needed by ReShade
extern "C" __declspec(dllexport) const char* NAME = Globals::NAME;
extern "C" __declspec(dllexport) const char* DESCRIPTION = Globals::DESCRIPTION;
extern "C" __declspec(dllexport) const char* WEBSITE = "https://github.com/Filoppi/Prey-Luma"; //TODOFT: set to nexus when it's up, maybe add an "ISSUES" definition too

// NOLINTEND(readability-identifier-naming)

#if DEVELOPMENT || _DEBUG || TEST
#define ASSERT_ONCE(x) { static bool asserted_once = false; \
if (!asserted_once && !(x)) { assert(x); asserted_once = true; } }
#else
#define ASSERT_ONCE(x)
#endif

namespace {
#if DEVELOPMENT || _DEBUG
    bool LaunchDebugger()
    {
#if 0 // Non stopping optional debugger
        // Get System directory, typically c:\windows\system32
        std::wstring systemDir(MAX_PATH + 1, '\0');
        UINT nChars = GetSystemDirectoryW(&systemDir[0], systemDir.length());
        if (nChars == 0) return false; // failed to get system directory
        systemDir.resize(nChars);

        // Get process ID and create the command line
        DWORD pid = GetCurrentProcessId();
        std::wostringstream s;
        s << systemDir << L"\\vsjitdebugger.exe -p " << pid;
        std::wstring cmdLine = s.str();

        // Start debugger process
        STARTUPINFOW si;
        ZeroMemory(&si, sizeof(si));
        si.cb = sizeof(si);

        PROCESS_INFORMATION pi;
        ZeroMemory(&pi, sizeof(pi));

        if (!CreateProcessW(NULL, &cmdLine[0], NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) return false;

        // Close debugger process handles to eliminate resource leak
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
#else // Stop execution until the debugger is attached or skipped

#if 1
        if (!IsDebuggerPresent()) {
            auto ret = MessageBoxA(NULL, "Loaded. You can now attach the debugger or continue execution.\nPress cancel to close the application.", NAME, MB_SETFOREGROUND | MB_OKCANCEL);
            if (ret == IDABORT || ret == IDCANCEL) {
                exit(0);
            }
        }
#else
        // Wait for the debugger to attach
        while (!IsDebuggerPresent()) Sleep(100);
#endif

#endif

#if 0
        // Stop execution so the debugger can take over
        DebugBreak();
#endif

        return true;
    }
#endif // DEVELOPMENT || _DEBUG

    bool IsMemoryAllZero(const char* begin, std::size_t bytes)
    {
        return std::all_of(begin, begin + bytes, [](char byte) { return byte == 0; });
    }

struct CachedPipeline {
  // Orignal pipeline
  reshade::api::pipeline pipeline;
  // Cached device (makes it easier to access, even if there's only a global one in Prey)
  reshade::api::device* device;
  reshade::api::pipeline_layout layout;
  // Cloned subojects from the orignal pipeline
  reshade::api::pipeline_subobject* subobjects_cache;
  uint32_t subobject_count;
  bool cloned = false;
  bool ready_for_binding = true;
  reshade::api::pipeline pipeline_clone;
  // Original shaders hash (there should only be one)
  std::vector<uint32_t> shader_hashes;
#if DEVELOPMENT
  // If true, this pipeline is currently being "tested"
  bool test = false;
#endif

  bool HasPixelShader() const {
    for (uint32_t i = 0; i < subobject_count; i++) {
      if (subobjects_cache[i].type == reshade::api::pipeline_subobject_type::pixel_shader) return true;
    }
    return false;
  }
  bool HasComputeShader() const {
    for (uint32_t i = 0; i < subobject_count; i++) {
      if (subobjects_cache[i].type == reshade::api::pipeline_subobject_type::compute_shader) return true;
    }
    return false;
  }
  bool HasVertexShader() const {
    for (uint32_t i = 0; i < subobject_count; i++) {
      if (subobjects_cache[i].type == reshade::api::pipeline_subobject_type::vertex_shader) return true;
    }
    return false;
  }
};

struct CachedShader {
  void* data = nullptr;
  size_t size = 0;
  reshade::api::pipeline_subobject_type type;
  int32_t index = -1;
  std::string disasm;
};

struct CachedCustomShader {
  std::vector<uint8_t> code;
  bool is_hlsl = false;
  std::filesystem::path file_path; // This should point to the source hlsl wherever possible (or a cso blob as fallback)
  std::size_t preprocessed_hash = 0; // A value of 0 won't ever be generated by the hash algorithm
  std::string compilation_errors; // Compilation errors and warnings log
#if DEVELOPMENT || TEST
  bool compilation_error;
#endif
};

// For "pipeline_cache_by_pipeline_handle", "pipeline_cache_by_pipeline_clone_handle", "pipeline_caches_by_shader_hash", "pipelines_to_destroy"
recursive_shared_mutex s_mutex_generic;
// For "shaders_to_dump", "dumped_shaders", "shader_cache". In general for dumping shaders to disk
std::recursive_mutex s_mutex_dumping;
// For "custom_shaders_cache", "pipelines_to_reload". In general for loading shaders from disk and compiling them
recursive_shared_mutex s_mutex_loading;
// Mutex for created shader DX objects
std::shared_mutex s_mutex_shader_objects;
// Mutex for shader defines ("shader_defines_data", "code_shaders_defines")
std::shared_mutex s_mutex_shader_defines;
// Mutex to deal with data shader with ReShade, like ini/config saving and loading
std::shared_mutex s_mutex_reshade;
// For "custom_sampler_by_original_sampler"
std::shared_mutex s_mutex_samplers;
// For "native_swapchain3" and "global_native_device"
std::shared_mutex s_mutex_device;
#if DEVELOPMENT
// for "trace_shader_hashes", "trace_pipeline_handles", "trace_pipeline_draws" and "trace_threads" (writing only, reading is already safe)
std::shared_mutex s_mutex_trace;
#endif

bool asi_loaded = true;
std::thread thread_auto_dumping;
std::atomic<bool> thread_auto_dumping_running = false;
std::thread thread_auto_loading;
std::atomic<bool> thread_auto_loading_running = false;

struct __declspec(uuid("cfebf6d4-d184-4e1a-ac14-09d088e560ca")) DeviceData {
  std::shared_mutex mutex;

#if DEVELOPMENT
  //TODOFT: clean up this unused stuff?
  std::unordered_map<uint64_t, uint64_t> resource_views; // <resource.handle, resource_view.handle>
  std::unordered_map<uint64_t, std::string> resource_names;
  std::unordered_set<uint64_t> resources;
#endif

  std::unordered_set<reshade::api::swapchain*> swapchains;
  std::unordered_set<uint64_t> back_buffers;

  reshade::api::pipeline_layout settings_pipeline_layout;
  reshade::api::pipeline_layout shared_data_pipeline_layout;
  reshade::api::pipeline_layout ui_pipeline_layout;
};

struct __declspec(uuid("c5805458-2c02-4ebf-b139-38b85118d971")) SwapchainData {
  std::shared_mutex mutex;

  std::unordered_set<uint64_t> back_buffers;
};

struct __declspec(uuid("90d9d05b-fdf5-44ee-8650-3bfd0810667a")) CommandListData {
    reshade::api::pipeline pipeline_state_original_compute_shader = reshade::api::pipeline(0);
    reshade::api::pipeline pipeline_state_original_vertex_shader = reshade::api::pipeline(0);
    reshade::api::pipeline pipeline_state_original_pixel_shader = reshade::api::pipeline(0);
};

// Pipelines by handle. Multiple pipelines can target the same shader, and even have multiple shaders within themselved.
// This only contains pipelines that we are replacing any shaders of.
std::unordered_map<uint64_t, CachedPipeline*> pipeline_cache_by_pipeline_handle;
// Same as "pipeline_cache_by_pipeline_handle" but for cloned pipelines.
std::unordered_map<uint64_t, CachedPipeline*> pipeline_cache_by_pipeline_clone_handle;
// All the pipelines linked to a shader. By shader hash.
std::unordered_map<uint32_t, std::unordered_set<CachedPipeline*>> pipeline_caches_by_shader_hash;
// All the shaders the game ever loaded (including the ones that have been unloaded). Only used by shader dumping or to see their binary code in the ImGUI view. By shader hash.
std::unordered_map<uint32_t, CachedShader*> shader_cache;
// All the shaders the user has (and has had) as custom in the live folder. By shader hash.
std::unordered_map<uint32_t, CachedCustomShader*> custom_shaders_cache;

// Custom samplers mapped to original ones by texture LOD bias
std::unordered_map<uint64_t, std::unordered_map<float, com_ptr<ID3D11SamplerState>>> custom_sampler_by_original_sampler;

std::unordered_set<uint64_t> pipelines_to_reload;
static_assert(sizeof(reshade::api::pipeline::handle) == sizeof(uint64_t));
// Map of "reshade::api::pipeline::handle"
std::unordered_map<uint64_t, reshade::api::device*> pipelines_to_destroy;
// Newly loaded shaders that still need to be (auto) dumped
std::unordered_set<uint32_t> shaders_to_dump;
// All the shaders we have already dumped
std::unordered_set<uint32_t> dumped_shaders;

#if DEVELOPMENT
std::vector<uint64_t> trace_pipeline_handles; // The actual list of pipelines that run within the traced frame
std::vector<uint32_t> trace_pipeline_draws; // How many times the last bound pipeline drew
std::vector<std::thread::id> trace_threads; // The thread that drew (set) each pipeline
std::vector<uint32_t> trace_shader_hashes; // Just used to filter out what shaders we've already listed
#endif

const uint32_t HASH_CHARACTERS_LENGTH = 8;
const std::string NAME_ADVANCED_SETTINGS = std::string(NAME) + " Advanced";

// Needs to be here to compile properly
#include "includes/shader_define.h"

struct ShaderHashesList {
    std::unordered_set<uint32_t> pixel_shaders;
    std::unordered_set<uint32_t> vertex_shaders;
    std::unordered_set<uint32_t> compute_shaders;

    bool Contains(uint32_t shader_hash, reshade::api::shader_stage shader_stage) {
        // NOTE: we could probably check if the value matches a specific shader stage (e.g. a switch?), but I'm not 100% sure other flags are ever set
        if ((shader_stage & reshade::api::shader_stage::pixel) != 0) {
            if (pixel_shaders.contains(shader_hash)) return true;
        }
        if ((shader_stage & reshade::api::shader_stage::vertex) != 0) {
            if (vertex_shaders.contains(shader_hash)) return true;
        }
        if ((shader_stage & reshade::api::shader_stage::compute) != 0) {
            return compute_shaders.contains(shader_hash);
        }
        return false;
    }
    bool Contains(const ShaderHashesList& other) {
        for (const uint32_t shader_hash : other.pixel_shaders) {
            if (pixel_shaders.contains(shader_hash)) {
                return true;
            }
        }
        for (const uint32_t shader_hash : other.vertex_shaders) {
            if (vertex_shaders.contains(shader_hash)) {
                return true;
            }
        }
        for (const uint32_t shader_hash : other.compute_shaders) {
            if (compute_shaders.contains(shader_hash)) {
                return true;
            }
        }
        return false;
    }
    bool Empty() {
        return pixel_shaders.empty() && vertex_shaders.empty() && compute_shaders.empty();
    }
};

// Settings
bool auto_dump = (bool)ALLOW_SHADERS_DUMPING;
bool auto_load = true;
bool live_reload = false;
#if DEVELOPMENT
bool trace_list_unique_shaders_only = false;
bool trace_ignore_vertex_shaders = true;
#endif
const bool precompile_custom_shaders = true;
constexpr uint32_t shader_cbuffers_index = 2; // Default to 2 for Prey
bool dlss_sr = true; // If true DLSS is enabled by the user and supported+initialized correctly
bool dlss_sr_supported = false;
bool tonemap_ui_background = true;
constexpr float tonemap_ui_background_amount = 0.25;
constexpr float srgb_white_level = 80;
constexpr float default_paper_white = 203; // ITU White Level
constexpr float default_peak_white = 1000;
bool hdr_enabled_display = false;
bool hdr_supported_display = false;
float default_user_peak_white = default_peak_white;
ShaderHashesList shader_hashes_TiledShadingTiledDeferredShading;
uint32_t shader_hash_DeferredShadingSSRRaytrace;
uint32_t shader_hash_DeferredShadingSSReflectionComp;
uint32_t shader_hash_PostEffectsGaussBlurBilinear;
uint32_t shader_hash_PostEffectsTextureToTextureResampled;
ShaderHashesList shader_hashes_MotionBlur;
ShaderHashesList shader_hashes_HDRPostProcessHDRFinalScene;
ShaderHashesList shader_hashes_HDRPostProcessHDRFinalScene_Sunshafts;
ShaderHashesList shader_hashes_SMAA_EdgeDetection;
ShaderHashesList shader_hashes_PostAA;
ShaderHashesList shader_hashes_PostAAComposites;
uint32_t shader_hash_PostAAUpscaleImage;
ShaderHashesList shader_hashes_LensOptics;
ShaderHashesList shader_hashes_DirOccPass;
ShaderHashesList shader_hashes_SSDO_Blur;
const uint32_t shader_hash_copy_vertex = std::stoul("FFFFFFF0", nullptr, 16);
const uint32_t shader_hash_copy_pixel = std::stoul("FFFFFFF1", nullptr, 16);
const uint32_t shader_hash_transform_function_copy_pixel = std::stoul("FFFFFFF2", nullptr, 16);
const uint32_t shader_hash_draw_exposure = std::stoul("FFFFFFF3", nullptr, 16);
constexpr bool prevent_shader_cache_loading = false;
bool prevent_shader_cache_saving = false;

//TODOFT3: clean up
constexpr bool force_motion_vectors_jittered = true;
int fix_prev_matrix_mode = 1; // NOTE: referenced in shader's code //TODOFT: delete!
int matrix_calculation_mode = 0;
int matrix_calculation_mode_2 = 1;
float texture_mip_lod_bias_offset = 0.f;
#if DEVELOPMENT
int samplers_upgrade_mode = 5;
int samplers_upgrade_mode_2 = 0;
bool custom_texture_mip_lod_bias_offset = false; // Live edit
float dlss_custom_exposure = 0.f; // Ignored at 0
float dlss_custom_pre_exposure = 0.f; // Ignored at 0
#endif
float dlss_scene_exposure = 1.f;
float dlss_scene_pre_exposure = 1.f;

#if DEVELOPMENT
bool disable_taa_jitters = false;
int force_taa_jitter_phases = 0; // Ignored if 0 (automatic mode), set to 1 to basically disable jitters
int frame_sleep_ms = 0;
#endif

////////////////////////////////////////////////////////////////////
// Per frame states
// These are atomic for extra safety as there's multiple command lists in the game
////////////////////////////////////////////////////////////////////

std::atomic<bool> has_drawn_composed_gbuffers = false;
std::atomic<bool> has_drawn_main_post_processing = false;
// Useful to know if rendering was skipped in the previous frame (e.g. in case we were in a UI view)
bool has_drawn_main_post_processing_previous = false;
std::atomic<bool> has_drawn_upscaling = false;
std::atomic<bool> has_drawn_dlss_sr = false;
std::atomic<bool> has_drawn_motion_blur = false;
bool has_drawn_motion_blur_previous = false;
std::atomic<bool> has_drawn_tonemapping = false;
std::atomic<bool> has_drawn_ssr = false;
std::atomic<ID3D11DeviceContext*> ssr_command_list = nullptr;
std::atomic<bool> has_drawn_ssr_blend = false;
std::atomic<bool> has_drawn_ssao = false;
std::atomic<bool> has_drawn_ssao_denoise = false;
std::atomic<bool> found_per_view_globals = false;
// Whether the rendering resolution was scaled in this frame (different from the ouput resolution)
std::atomic<bool> prey_drs_active = false;
std::atomic<bool> force_reset_dlss = false;
std::atomic<bool> prey_drs_detected = false;
std::atomic<float> dlss_render_resolution_scale = 1.f;
std::atomic<bool> dlss_suppressed = false;
std::atomic<bool> prey_taa_enabled = false;
// Index 0 is one frame ago, index 1 is two frames ago
bool previous_prey_taa_enabled[2] = { false, false };
std::atomic<bool> prey_taa_detected = false;
uint32_t frame_index = 0;
// Directly from cbuffer (so these are transposed)
Matrix44A projection_matrix;
Matrix44A nearest_projection_matrix;
Matrix44A previous_projection_matrix;
Matrix44A previous_nearest_projection_matrix;
Matrix44A reprojection_matrix;
float2 previous_projection_jitters = { 0, 0 };
float2 projection_jitters = { 0, 0 };
float2 render_resolution = { 1, 1 };
float2 previous_render_resolution = { 1, 1 };
float2 output_resolution = { 1, 1 };
// Pointer to the current DX buffer for the "global per view" cbuffer.
com_ptr<ID3D11Buffer> cb_per_view_global_buffer;
void* cb_per_view_global_buffer_map_data = nullptr;
CBPerViewGlobal cb_per_view_global = { };
CBPerViewGlobal cb_per_view_global_previous = cb_per_view_global;
#if DEVELOPMENT //TODOFT3: delete once not needed anymore?
std::string last_drawn_shader = ""; //TODOFT: move to CommandListData or make thread_local?
std::vector<std::string> cb_per_view_globals_last_drawn_shader;
std::vector<ID3D11Buffer*> cb_per_view_global_buffer_pending_verification;
std::vector<CBPerViewGlobal> cb_per_view_globals;
std::vector<CBPerViewGlobal> cb_per_view_globals_previous;

enum class DebugDrawTextureOptionsMask : uint32_t {
    None = 0,
    Fullscreen = 1 << 0,
    RenderResolutionScale = 1 << 1,
    ShowAlpha = 1 << 2,
    PreMultiplyAlpha = 1 << 3,
    InvertColors = 1 << 4,
    LinearToGamma = 1 << 5,
    GammaToLinear = 1 << 6
};
com_ptr<ID3D11Texture2D> debug_draw_texture;
DXGI_FORMAT debug_draw_texture_format = DXGI_FORMAT_UNKNOWN;
uint32_t debug_draw_shader_hash = 0;
char debug_draw_shader_hash_string[HASH_CHARACTERS_LENGTH + 1];
uint64_t debug_draw_pipeline = 0;
std::atomic<int32_t> debug_draw_pipeline_instance = 0; // Theoretically should be within "CommandListData" but this should work for most cases
int32_t debug_draw_pipeline_target_instance = -1;
// If true we are drawing the render target texture, otherwise the shader resource texture
bool debug_draw_render_target_view = true;
int32_t debug_draw_view_index = 0;
uint32_t debug_draw_options = (uint32_t)DebugDrawTextureOptionsMask::Fullscreen | (uint32_t)DebugDrawTextureOptionsMask::RenderResolutionScale;
bool debug_draw_auto_clear_texture = false;
#endif // DEVELOPMENT
LumaFrameSettings cb_luma_frame_settings = { };
#if DEVELOPMENT
LumaFrameDevSettings cb_luma_frame_dev_settings_default_value(0.f);
LumaFrameDevSettings cb_luma_frame_dev_settings_min_value(0.f);
LumaFrameDevSettings cb_luma_frame_dev_settings_max_value(1.f);
std::array<std::string, LumaFrameDevSettings::SettingsNum> cb_luma_frame_dev_settings_names;
#endif

constexpr uint32_t ui_cbuffer_index = 7;

std::string shaders_compilation_errors; // errors and warning log

// List of define values read by our settings shaders
std::unordered_map<std::string, uint8_t> code_shaders_defines;
// These default should ideally match shaders values, but it's not necessary because whathever the default values they have they will be overridden
// TODO: add grey out conditions (another define, by name, whether its value is > 0), and also add min/max values range (to limit the user insertable values), and "category"
std::vector<ShaderDefineData> shader_defines_data = {
  {"DEVELOPMENT", DEVELOPMENT ? '1' : '0', true, DEVELOPMENT ? false : true, "Enables some development/debug features that are otherwise not allowed (get a TEST or DEVELOPMENT build if you want to use this)"},
  {"POST_PROCESS_SPACE_TYPE", '1', true, false, "0 - Gamma space\n1 - Linear space\n2 - Linear space until UI (then gamma space)\n\nSelect \"2\" if you want the UI to look exactly like it did in Vanilla\nSelect \"1\" for the highest possible quality (e.g. color accuracy, banding, DLSS)"},
  {"GAMMA_CORRECTION_TYPE", '1', true, false, "(HDR only) Emulates a specific SDR gamma\nThis is best left to \"1\" (Gamma 2.2) unless you have crushed blacks or overly saturated colors\n0 - sRGB\n1 - Gamma 2.2\n2 - sRGB (color hues) with gamma 2.2 luminance"},
  {"TONEMAP_TYPE", '1', false, false, "0 - Vanilla SDR\n1 - Luma HDR (Vanilla+)\n2 - Raw HDR (Untonemapped)\nThe HDR tonemapper works for SDR too\nThis games uses a filmic tonemapper, which slightly crushes blacks"},
  {"SUNSHAFTS_LOOK_TYPE", '2', false, false, "0 - Raw Vanilla\n1 - Vanilla+\n2 - Luma HDR (Suggested)\nThis influences both HDR and SDR, all options work in both"},
  {"ENABLE_LENS_OPTICS_HDR", '1', false, false, "Makes the lens effects (e.g. lens flare) slightly HDR"},
  {"AUTO_HDR_VIDEOS", '1', false, false, "(HDR only) Generates some HDR highlights from SDR videos, for consistency\nThis is pretty lightweight so it won't really affect the artistic intent"},
  {"ENABLE_LUT_EXTRAPOLATION", '1', false, false, "LUT Extrapolation should be the best looking and most accurate SDR to HDR LUT adaptation mode,\nbut you can always turn it off for the its simpler fallback"},
#if DEVELOPMENT || TEST
  {"DLSS_RELATIVE_PRE_EXPOSURE", '1', true, false },
  {"ENABLE_LINEAR_COLOR_GRADING_LUT", '1', false, false, "Whether (SDR) LUTs are stored in linear or gamma space"},
  {"FORCE_NEUTRAL_COLOR_GRADING_LUT_TYPE", '0', false, false, "Can force a neutral LUT in different ways (color grading is still applied)"},
  {"DRAW_LUT", '0', false, (DEVELOPMENT || TEST) ? false : true},
#endif
  {"SSAO_TYPE", '1', false, false, "Screen Space Ambient Occlusion\n0 - Vanilla\n1 - Luma GTAO\nIn case GTAO is too performance intensive, lower the \"SSAO_QUALITY\" or go into the official game graphics settings and set \"Screen Space Directional Occlusion\" to half resolution\nDLSS is suggested to help with denoising AO"},
  {"SSAO_QUALITY", '1', false, false, "0 - Vanilla\n1 - High\n2 - Extreme (slow)"},
#if DEVELOPMENT || TEST // For now we don't want to give users this customization, the default value should be good for most users and most cases
  {"SSAO_RADIUS", '1', false, false, "0 - Small, 1 - Vanilla/Standard (suggested)\n2 - Large\nSmaller radiuses can look more stable but don't do as much\nLarger radiuses can look more realistic, but also over darkening and bring out screen space limitations more often (e.g. stuff de-occluding around the edges when turning the camera)\nOnly applies to GTAO"},
#endif
  {"ENABLE_SSAO_TEMPORAL", '1', false, false, "Disable if you don't use TAA to avoid seeing noise in Ambient Occlusion (though it won't have the same quality)\nYou can disable it for you use TAA too but it's not suggested"},
  {"BLOOM_QUALITY", '1', false, false, "0 - Vanilla\n1 - High"},
  {"SSR_QUALITY", '1', false, false, "Screen Space Reflections\n0 - Vanilla\n1 - High\n2 - Ultra\nThis can be fairly expensive so lower it if you are having performance issues"},
#if DEVELOPMENT || TEST
  {"FORCE_MOTION_VECTORS_JITTERED", force_motion_vectors_jittered ? '0' : '0', false, false, "Forces Motion Vectors generation to include the jitters from the previous frame too, as DLSS needs\nEnabling this forces the native TAA to work as when we have DLSS enabled, making it look a little bit better (less shimmery)"},
#endif
  {"ENABLE_POST_PROCESS", '1', false, false, "Allows you to disable all post processing (at once)"},
  {"ENABLE_CAMERA_MOTION_BLUR", '0', false, false, "Camera Motion Blur can look pretty botched in Prey, and can mess with DLSS/TAA, it's turned off by default in Luma"},
  {"ENABLE_COLOR_GRADING_LUT", '1', false, false, "Allows you to disable color grading\nDon't disable it unless you know what you are doing"},
  {"POST_TAA_SHARPENING_TYPE", '2', false, false, "0 - None (disabled)\n1 - Vanilla (basic sharpening)\n2 - RCAS (AMD improved sharpening)"},
  {"ENABLE_VIGNETTE", '1', false, false, "Allows you to disable vignette\nIt's not that prominent in Prey, it's only used in certain cases to convey gameplay information,\nso don't disable it unless you know what you are doing"},
#if DEVELOPMENT || TEST // Disabled these final users because these require the "DEVELOPMENT" flag to be used and we don't want users to mess around with them (it's not what the mod wants to achieve)
  {"ENABLE_SHARPENING", '1', false, false, "Allows you to disable sharpening globally\nDisabling it is not suggested, especially if you use TAA (you can use \"POST_TAA_SHARPENING_TYPE\" for that anyway)"},
  {"ENABLE_FILM_GRAIN", '1', false, false, "Allows you to disable color grading\nIt's not that prominent in Prey, it's only used in certain cases to convey gameplay information,\nso don't disable it unless you know what you are doing"},
#endif
  {"ENABLE_DITHERING", '0', false, false, "Temporal dithering control\nIt doesn't seem to be needed in this game so Luma disabled it by default"},
  {"DITHERING_BIT_DEPTH", '9', false, false, "Dithering quantization (values between 7 and 9 should be best)"},
};
// uint8_t is enough for MAX_SHADER_DEFINES
std::unordered_map<uint32_t, uint8_t> shader_defines_data_index;

static constexpr unsigned int crc_table[256] = {
    0x00000000, 0x77073096, 0xee0e612c, 0x990951ba, 0x076dc419, 0x706af48f,
    0xe963a535, 0x9e6495a3,    0x0edb8832, 0x79dcb8a4, 0xe0d5e91e, 0x97d2d988,
    0x09b64c2b, 0x7eb17cbd, 0xe7b82d07, 0x90bf1d91, 0x1db71064, 0x6ab020f2,
    0xf3b97148, 0x84be41de, 0x1adad47d, 0x6ddde4eb, 0xf4d4b551, 0x83d385c7,
    0x136c9856, 0x646ba8c0, 0xfd62f97a, 0x8a65c9ec, 0x14015c4f, 0x63066cd9,
    0xfa0f3d63, 0x8d080df5, 0x3b6e20c8, 0x4c69105e, 0xd56041e4, 0xa2677172,
    0x3c03e4d1, 0x4b04d447, 0xd20d85fd, 0xa50ab56b, 0x35b5a8fa, 0x42b2986c,
    0xdbbbc9d6, 0xacbcf940, 0x32d86ce3, 0x45df5c75, 0xdcd60dcf, 0xabd13d59,
    0x26d930ac, 0x51de003a, 0xc8d75180, 0xbfd06116, 0x21b4f4b5, 0x56b3c423,
    0xcfba9599, 0xb8bda50f, 0x2802b89e, 0x5f058808, 0xc60cd9b2, 0xb10be924,
    0x2f6f7c87, 0x58684c11, 0xc1611dab, 0xb6662d3d, 0x76dc4190, 0x01db7106,
    0x98d220bc, 0xefd5102a, 0x71b18589, 0x06b6b51f, 0x9fbfe4a5, 0xe8b8d433,
    0x7807c9a2, 0x0f00f934, 0x9609a88e, 0xe10e9818, 0x7f6a0dbb, 0x086d3d2d,
    0x91646c97, 0xe6635c01, 0x6b6b51f4, 0x1c6c6162, 0x856530d8, 0xf262004e,
    0x6c0695ed, 0x1b01a57b, 0x8208f4c1, 0xf50fc457, 0x65b0d9c6, 0x12b7e950,
    0x8bbeb8ea, 0xfcb9887c, 0x62dd1ddf, 0x15da2d49, 0x8cd37cf3, 0xfbd44c65,
    0x4db26158, 0x3ab551ce, 0xa3bc0074, 0xd4bb30e2, 0x4adfa541, 0x3dd895d7,
    0xa4d1c46d, 0xd3d6f4fb, 0x4369e96a, 0x346ed9fc, 0xad678846, 0xda60b8d0,
    0x44042d73, 0x33031de5, 0xaa0a4c5f, 0xdd0d7cc9, 0x5005713c, 0x270241aa,
    0xbe0b1010, 0xc90c2086, 0x5768b525, 0x206f85b3, 0xb966d409, 0xce61e49f,
    0x5edef90e, 0x29d9c998, 0xb0d09822, 0xc7d7a8b4, 0x59b33d17, 0x2eb40d81,
    0xb7bd5c3b, 0xc0ba6cad, 0xedb88320, 0x9abfb3b6, 0x03b6e20c, 0x74b1d29a,
    0xead54739, 0x9dd277af, 0x04db2615, 0x73dc1683, 0xe3630b12, 0x94643b84,
    0x0d6d6a3e, 0x7a6a5aa8, 0xe40ecf0b, 0x9309ff9d, 0x0a00ae27, 0x7d079eb1,
    0xf00f9344, 0x8708a3d2, 0x1e01f268, 0x6906c2fe, 0xf762575d, 0x806567cb,
    0x196c3671, 0x6e6b06e7, 0xfed41b76, 0x89d32be0, 0x10da7a5a, 0x67dd4acc,
    0xf9b9df6f, 0x8ebeeff9, 0x17b7be43, 0x60b08ed5, 0xd6d6a3e8, 0xa1d1937e,
    0x38d8c2c4, 0x4fdff252, 0xd1bb67f1, 0xa6bc5767, 0x3fb506dd, 0x48b2364b,
    0xd80d2bda, 0xaf0a1b4c, 0x36034af6, 0x41047a60, 0xdf60efc3, 0xa867df55,
    0x316e8eef, 0x4669be79, 0xcb61b38c, 0xbc66831a, 0x256fd2a0, 0x5268e236,
    0xcc0c7795, 0xbb0b4703, 0x220216b9, 0x5505262f, 0xc5ba3bbe, 0xb2bd0b28,
    0x2bb45a92, 0x5cb36a04, 0xc2d7ffa7, 0xb5d0cf31, 0x2cd99e8b, 0x5bdeae1d,
    0x9b64c2b0, 0xec63f226, 0x756aa39c, 0x026d930a, 0x9c0906a9, 0xeb0e363f,
    0x72076785, 0x05005713, 0x95bf4a82, 0xe2b87a14, 0x7bb12bae, 0x0cb61b38,
    0x92d28e9b, 0xe5d5be0d, 0x7cdcefb7, 0x0bdbdf21, 0x86d3d2d4, 0xf1d4e242,
    0x68ddb3f8, 0x1fda836e, 0x81be16cd, 0xf6b9265b, 0x6fb077e1, 0x18b74777,
    0x88085ae6, 0xff0f6a70, 0x66063bca, 0x11010b5c, 0x8f659eff, 0xf862ae69,
    0x616bffd3, 0x166ccf45, 0xa00ae278, 0xd70dd2ee, 0x4e048354, 0x3903b3c2,
    0xa7672661, 0xd06016f7, 0x4969474d, 0x3e6e77db, 0xaed16a4a, 0xd9d65adc,
    0x40df0b66, 0x37d83bf0, 0xa9bcae53, 0xdebb9ec5, 0x47b2cf7f, 0x30b5ffe9,
    0xbdbdf21c, 0xcabac28a, 0x53b39330, 0x24b4a3a6, 0xbad03605, 0xcdd70693,
    0x54de5729, 0x23d967bf, 0xb3667a2e, 0xc4614ab8, 0x5d681b02, 0x2a6f2b94,
    0xb40bbe37, 0xc30c8ea1, 0x5a05df1b, 0x2d02ef8d
};

constexpr uint32_t string_view_crc32(std::string_view str) {
#if 0 //TODOFT: delete
    if (!std::is_constant_evaluated())
        assert(false);
#endif
    uint32_t crc = 0xffffffff;
    for (char c : str)
        crc = (crc >> 8) ^ crc_table[(crc ^ c) & 0xff];
    return crc ^ 0xffffffff;
}
constexpr uint32_t char_ptr_crc32(const char* char_ptr) {
    uint32_t crc = 0xffffffff;
    size_t i = 0;
    while (char_ptr[i] != '\0') {
        crc = (crc >> 8) ^ crc_table[(crc ^ char_ptr[i]) & 0xff];
        i++;
    }
    return crc ^ 0xffffffff;
}

// TODO: if at runtime we can't edit "shader_defines_data" (e.g. in non dev modes), then we could directly set these to the index value of their respective "shader_defines_data" and skip the map?
constexpr uint32_t DEVELOPMENT_HASH = char_ptr_crc32("DEVELOPMENT");
constexpr uint32_t POST_PROCESS_SPACE_TYPE_HASH = char_ptr_crc32("POST_PROCESS_SPACE_TYPE");
constexpr uint32_t GAMMA_CORRECTION_TYPE_HASH = char_ptr_crc32("GAMMA_CORRECTION_TYPE");
constexpr uint32_t AUTO_HDR_VIDEOS_HASH = char_ptr_crc32("AUTO_HDR_VIDEOS");
constexpr uint32_t SSAO_TYPE_HASH = char_ptr_crc32("SSAO_TYPE");
constexpr uint32_t DLSS_RELATIVE_PRE_EXPOSURE_HASH = char_ptr_crc32("DLSS_RELATIVE_PRE_EXPOSURE"); // "DEVELOPMENT" only
constexpr uint32_t FORCE_MOTION_VECTORS_JITTERED_HASH = char_ptr_crc32("FORCE_MOTION_VECTORS_JITTERED"); // "DEVELOPMENT" only

// Resources:

#if ENABLE_NGX
// DLSS
com_ptr<ID3D11Texture2D> dlss_output_color;
com_ptr<ID3D11Texture2D> dlss_exposure;
com_ptr<ID3D11Texture2D> dlss_motion_vectors;
com_ptr<ID3D11RenderTargetView> dlss_motion_vectors_rtv;
#endif // ENABLE_NGX

// Custom shaders
com_ptr<ID3D11Texture2D> copy_texture;
com_ptr<ID3D11Texture2D> transfer_function_copy_texture;
com_ptr<ID3D11ShaderResourceView> transfer_function_copy_shader_resource_view;
com_ptr<ID3D11VertexShader> copy_vertex_shader;
com_ptr<ID3D11PixelShader> copy_pixel_shader;
com_ptr<ID3D11PixelShader> transfer_function_copy_pixel_shader;
com_ptr<ID3D11PixelShader> draw_exposure_pixel_shader; // DLSS
com_ptr<ID3D11Buffer> exposure_buffer_gpu; // DLSS
com_ptr<ID3D11Buffer> exposure_buffer_cpu; // DLSS
com_ptr<ID3D11RenderTargetView> exposure_buffer_rtv; // DLSS

// GTAO
com_ptr<ID3D11Texture2D> gtao_edges_texture;
UINT gtao_edges_texture_width = 0;
UINT gtao_edges_texture_height = 0;
com_ptr<ID3D11RenderTargetView> gtao_edges_rtv;
com_ptr<ID3D11ShaderResourceView> gtao_edges_srv;

// SSR
com_ptr<ID3D11Texture2D> ssr_texture;
com_ptr<ID3D11Texture2D> ssr_diffuse_texture;
UINT ssr_diffuse_texture_width = 0;
UINT ssr_diffuse_texture_height = 0;
DXGI_FORMAT ssr_texture_format = DXGI_FORMAT_UNKNOWN;
com_ptr<ID3D11RenderTargetView> ssr_diffuse_rtv;
com_ptr<ID3D11ShaderResourceView> ssr_srv;
com_ptr<ID3D11ShaderResourceView> ssr_diffuse_srv;

com_ptr<ID3D11BlendState> default_blend_state;

// There's only one swapchain in Prey, but the game chances its configuration from different threads
IDXGISwapChain3* native_swapchain3 = nullptr;
ID3D11Device* global_native_device = nullptr;

HWND game_window = 0;

static_assert(sizeof(Matrix44A) == sizeof(float4) * 4);

// Caches all the states we might need to modify to draw a simple pixel shader.
// First call "Cache()" (once) and then call "Restore()" (once).
struct DrawStateStack {
    // This is the max according to PSSetShader() documentation
    static constexpr UINT max_shader_class_instances = 256;

    // Not used by Prey's CryEngine
    #define ENABLE_SHADER_CLASS_INSTANCES 0

    DrawStateStack() {
#if ENABLE_SHADER_CLASS_INSTANCES
        std::memset(&vs_instances, 0, sizeof(void*) * max_shader_class_instances);
        std::memset(&ps_instances, 0, sizeof(void*) * max_shader_class_instances);
#endif
    }

    // Cache aside the previous resources/states:
    void Cache(ID3D11DeviceContext* device_context) {
        device_context->OMGetBlendState(&blend_state, blend_factor, &blend_sample_mask); //TODOFT: blend_factor!? I guess it's passed in as a ptr already
        device_context->IAGetPrimitiveTopology(&primitive_topology);
        device_context->RSGetScissorRects(&scissor_rects_num, nullptr); // This will get the number of scissor rects used
        device_context->RSGetScissorRects(&scissor_rects_num, &scissor_rects[0]);
        device_context->RSGetViewports(&viewports_num, nullptr); // This will get the number of viewports used
        device_context->RSGetViewports(&viewports_num, &viewports[0]);
        device_context->PSGetShaderResources(0, 1, &shader_resource_view); // Only cache the first one
        device_context->PSGetConstantBuffers(shader_cbuffers_index, 1, &constant_buffer); // Hardcoded to our "shader_cbuffers_index"
        device_context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, &render_target_views[0], &depth_stencil_view);
#if ENABLE_SHADER_CLASS_INSTANCES
        device_context->VSGetShader(&vs, vs_instances, &vs_instances_count);
        device_context->PSGetShader(&ps, ps_instances, &ps_instances_count);
        ASSERT_ONCE(vs_instances_count == 0 && ps_instances_count == 0);
#else
        device_context->VSGetShader(&vs, nullptr, 0);
        device_context->PSGetShader(&ps, nullptr, 0);
#endif

#if 0 // These are not needed until proven otherwise, we don't change, nor rely on these states
        ID3D11RasterizerState* RS;
        UINT StencilRef;
        ID3D11DepthStencilState* DepthStencilState;
        ID3D11SamplerState* PSSampler;
        ID3D11Buffer* IndexBuffer;
        ID3D11Buffer* VertexBuffer;
        ID3D11Buffer* VSConstantBuffer;
        UINT IndexBufferOffset, VertexBufferStride, VertexBufferOffset;
        DXGI_FORMAT IndexBufferFormat;
        ID3D11InputLayout* InputLayout;

        device_context->RSGetState(&RS);
        device_context->OMGetDepthStencilState(&DepthStencilState, &StencilRef);
        device_context->PSGetSamplers(0, 1, &PSSampler);
        device_context->VSGetConstantBuffers(0, 1, &VSConstantBuffer);
        device_context->IAGetIndexBuffer(&IndexBuffer, &IndexBufferFormat, &IndexBufferOffset);
        device_context->IAGetVertexBuffers(0, 1, &VertexBuffer, &VertexBufferStride, &VertexBufferOffset);
        device_context->IAGetInputLayout(&InputLayout);
#endif
    }

    // Restore the previous resources/states:
    void Restore(ID3D11DeviceContext* device_context) {
        device_context->OMSetBlendState(blend_state.get(), blend_factor, blend_sample_mask);
        device_context->IASetPrimitiveTopology(primitive_topology);
        device_context->RSSetScissorRects(scissor_rects_num, &scissor_rects[0]);
        device_context->RSSetViewports(viewports_num, &viewports[0]);
        ID3D11ShaderResourceView* const shader_resource_view_const = shader_resource_view.get();
        device_context->PSSetShaderResources(0, 1, &shader_resource_view_const);
        ID3D11Buffer* const constant_buffer_const = constant_buffer.get();
        device_context->PSSetConstantBuffers(shader_cbuffers_index, 1, &constant_buffer_const);
        device_context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, &render_target_views[0], depth_stencil_view.get());
        for (UINT i = 0; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; i++) {
            if (render_target_views[i] != nullptr) {
                render_target_views[i]->Release();
                render_target_views[i] = nullptr;
            }
        }
#if ENABLE_SHADER_CLASS_INSTANCES
        device_context->VSSetShader(vs.get(), vs_instances, vs_instances_count);
        device_context->PSSetShader(ps.get(), ps_instances, ps_instances_count);
        for (UINT i = 0; i < max_shader_class_instances; i++) {
            if (vs_instances[i] != nullptr) {
                vs_instances[i]->Release();
                vs_instances[i] = nullptr;
            }
            if (ps_instances[i] != nullptr) {
                ps_instances[i]->Release();
                ps_instances[i] = nullptr;
            }
        }
#else
        device_context->VSSetShader(vs.get(), nullptr, 0);
        device_context->PSSetShader(ps.get(), nullptr, 0);
#endif
    }

    com_ptr<ID3D11BlendState> blend_state;
    FLOAT blend_factor[4] = { 1.f, 1.f, 1.f, 1.f };
    UINT blend_sample_mask;
    com_ptr<ID3D11VertexShader> vs;
    com_ptr<ID3D11PixelShader> ps;
#if ENABLE_SHADER_CLASS_INSTANCES
    UINT vs_instances_count = max_shader_class_instances;
    UINT ps_instances_count = max_shader_class_instances;
    ID3D11ClassInstance* vs_instances[max_shader_class_instances];
    ID3D11ClassInstance* ps_instances[max_shader_class_instances];
#endif
    D3D11_PRIMITIVE_TOPOLOGY primitive_topology;
    ID3D11RenderTargetView* render_target_views[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT];

    com_ptr<ID3D11DepthStencilView> depth_stencil_view;
    com_ptr<ID3D11ShaderResourceView> shader_resource_view;
    com_ptr<ID3D11Buffer> constant_buffer;
    D3D11_RECT scissor_rects[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
    UINT scissor_rects_num = 1;
    D3D11_VIEWPORT viewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
    UINT viewports_num = 1;

    #undef ENABLE_SHADER_CLASS_INSTANCES
};

bool last_pressed_unload = false;
bool needs_unload_shaders = false;
bool needs_load_shaders = false; // Load or reload shaders
bool needs_live_reload_update = live_reload;
std::atomic<bool> cloned_pipelines_changed = false;
uint32_t cloned_pipeline_count = 0; // How many pipelines (shaders/passes) we replaced with custom ones (if zero, we can assume the mod isn't doing much)
#if DEVELOPMENT
bool trace_scheduled = false;
bool trace_running = false;
uint32_t shader_cache_count = 0;
uint32_t trace_count = 0;
#endif

// Forward declares:
void ToggleLiveWatching();
void DumpShader(uint32_t shader_hash, bool auto_detect_type);
void AutoDumpShaders();
void AutoLoadShaders();

// Quick and unsafe. Passing in the hash instead of the string is the only way make sure strings hashes are calculate them at compile time.
__forceinline ShaderDefineData& GetShaderDefineData(uint32_t hash)
{
#if 0 // We don't lock "s_mutex_shader_defines" here as it wouldn't be particularly relevant (it won't lead to crashes, as generaly they are not edited in random threads, though having it enabled could lead to deadlocks if there's nested locks!).
    const std::shared_lock lock(s_mutex_shader_defines);
#endif
    return shader_defines_data[shader_defines_data_index[hash]];
}
__forceinline uint8_t GetShaderDefineCompiledNumericalValue(uint32_t hash)
{
    return GetShaderDefineData(hash).GetCompiledNumericalValue();
}
#if DEVELOPMENT
std::optional<std::string> GetD3DName(ID3D11DeviceChild* obj) {
  if (obj == nullptr) return std::nullopt;

  byte data[128] = {};
  UINT size = sizeof(data);
  if (obj->GetPrivateData(WKPDID_D3DDebugObjectName, &size, data) == S_OK) {
    if (size > 0) return std::string{data, data + size};
  }
  return std::nullopt;
}

std::optional<std::string> GetD3DNameW(ID3D11DeviceChild* obj) {
  if (obj == nullptr) return std::nullopt;

  byte data[128] = {};
  UINT size = sizeof(data);
  if (obj->GetPrivateData(WKPDID_D3DDebugObjectNameW, &size, data) == S_OK) {
    if (size > 0) {
      char c_name[128] = {};
      size_t out_size;
      // wide-character-string-to-multibyte-string_safe
      auto ret = wcstombs_s(&out_size, c_name, sizeof(c_name), reinterpret_cast<wchar_t*>(data), size);
      if (ret == 0 && out_size > 0) {
        return std::string(c_name, c_name + out_size);
      }
    }
  }
  return GetD3DName(obj);
}

uint64_t GetResourceByViewHandle(DeviceData& data, uint64_t handle) {
  if (auto pair = data.resource_views.find(handle); pair != data.resource_views.end())
    return pair->second;
  return 0;
}

std::string GetResourceNameByViewHandle(DeviceData& data, uint64_t handle) {
  auto resource_handle = GetResourceByViewHandle(data, handle);
  if (resource_handle == 0) return "?";
  if (!data.resources.contains(resource_handle)) return "?";

  if (auto pair = data.resource_names.find(resource_handle); pair != data.resource_names.end())
    return pair->second;

  auto* native_resource = reinterpret_cast<ID3D11DeviceChild*>(resource_handle);
  std::optional<std::string> name = GetD3DNameW(native_resource);
  if (name.has_value()) {
    data.resource_names[resource_handle] = name.value();
  }
  return "";
}
#endif

std::filesystem::path GetShaderPath() {
  // NOLINTNEXTLINE(modernize-avoid-c-arrays)
  wchar_t file_path[MAX_PATH] = L"";
  // We don't pass in any module handle, thus this will return the path of the executable that loaded our dll
  GetModuleFileNameW(nullptr, file_path, ARRAYSIZE(file_path));

  std::filesystem::path shaders_path = file_path;
  shaders_path = shaders_path.parent_path();
  std::string name_no_spaces = NAME;
  std::replace(name_no_spaces.begin(), name_no_spaces.end(), ' ', '-');
  shaders_path /= name_no_spaces;
  return shaders_path;
}

void DestroyPipelineSubojects(reshade::api::pipeline_subobject* subojects, uint32_t subobject_count) {
  for (uint32_t i = 0; i < subobject_count; ++i) {
    auto& suboject = subojects[i];

    switch (suboject.type) {
      case reshade::api::pipeline_subobject_type::vertex_shader:
      case reshade::api::pipeline_subobject_type::compute_shader:
      case reshade::api::pipeline_subobject_type::pixel_shader: {
        auto* desc = static_cast<reshade::api::shader_desc*>(suboject.data);
        delete desc->code;
        desc->code = nullptr;
        break;
      }
      default:
        break;
    }

    delete suboject.data;
    suboject.data = nullptr;
  }
  delete[] subojects;  // NOLINT
}

void ClearCustomShader(uint32_t shader_hash) {
  const std::unique_lock lock(s_mutex_loading);
  auto custom_shader = custom_shaders_cache.find(shader_hash);
  if (custom_shader != custom_shaders_cache.end() && custom_shader->second != nullptr) {
    custom_shader->second->code.clear();
    custom_shader->second->is_hlsl = false;
    custom_shader->second->preprocessed_hash = 0;
    custom_shader->second->file_path.clear();
    custom_shader->second->compilation_errors.clear();
#if DEVELOPMENT || TEST
    custom_shader->second->compilation_error = false;
#endif
  }
}

void UnloadCustomShaders(const std::unordered_set<uint64_t>& pipelines_filter = std::unordered_set<uint64_t>(), bool immediate = false, bool clean_custom_shader = true) {
  const std::unique_lock lock(s_mutex_generic);
  for (auto& pair : pipeline_cache_by_pipeline_handle) {
    auto& cached_pipeline = pair.second;
    if (cached_pipeline == nullptr || (!pipelines_filter.empty() && !pipelines_filter.contains(cached_pipeline->pipeline.handle))) continue;

    // In case this is a full "unload" of all shaders
    if (pipelines_filter.empty()) {
      // Clear their compilation state, we might not have any other way of doing it.
      // Disable testing etc here, otherwise we might not always have a way to do it
      if (clean_custom_shader) {
#if DEVELOPMENT
        cached_pipeline->test = false;
#endif
        for (auto shader_hash : cached_pipeline->shader_hashes) {
          ClearCustomShader(shader_hash);
        }
      }
    }

    if (!cached_pipeline->cloned) continue;
    cached_pipeline->cloned = false;  // This stops the cloned pipeline from being used in the next frame, allowing us to destroy it
    cloned_pipeline_count--;
    cloned_pipelines_changed = true;

    if (immediate) {
      cached_pipeline->device->destroy_pipeline(reshade::api::pipeline{cached_pipeline->pipeline_clone.handle});
    } else {
      pipelines_to_destroy[cached_pipeline->pipeline_clone.handle] = cached_pipeline->device;
    }
    cached_pipeline->pipeline_clone = {0};
    pipeline_cache_by_pipeline_clone_handle.erase(cached_pipeline->pipeline_clone.handle);
  }
}

// Expects "s_mutex_loading" to make sure we don't try to compile/load any other files we are currently deleting
void CleanShadersCache() {
    const auto directory = GetShaderPath();
    if (!std::filesystem::exists(directory)) {
        return;
    }

    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        const auto& entry_path = entry.path();
        const bool is_cso = entry_path.extension().compare(".cso") == 0;
        if (!entry_path.has_extension() || !entry_path.has_stem() || !is_cso) {
            continue;
        }

        const auto filename_no_extension_string = entry_path.stem().string();

#if 1 // Optionally leave any "raw" cso that was likely copied from the dumped shaders folder (these were not compiled from a custom hlsl shader by the same hash)
        if (filename_no_extension_string.length() >= strlen("0x12345678") && filename_no_extension_string[0] == '0' && filename_no_extension_string[1] == 'x') {
            continue;
        }
#endif

        std::filesystem::remove(entry_path);
    }
}

// Compiles all the "custom" shaders we have in our shaders folder
void CompileCustomShaders(const std::unordered_set<uint64_t>& pipelines_filter = std::unordered_set<uint64_t>()) {
  std::vector<std::string> shader_defines;
  // Cache them for consistency and to avoid threads from halting
  {
      const std::shared_lock lock(s_mutex_shader_defines);
      shader_defines.assign(shader_defines_data.size() * 2, "");
      for (uint32_t i = 0; i < shader_defines_data.size(); i++) {
          shader_defines[(i * 2)] = shader_defines_data[i].compiled_data.name;
          shader_defines[(i * 2) + 1] = shader_defines_data[i].compiled_data.value;
      }
  }

  // We need to clear this every time "CompileCustomShaders()" is called as we can't clear previous logs from it. We do this even if we have some "pipelines_filter"
  {
      const std::unique_lock lock(s_mutex_loading);
      shaders_compilation_errors.clear();
  }

  const auto directory = GetShaderPath();
  if (!std::filesystem::exists(directory)) {
      if (!std::filesystem::create_directory(directory)) {
          const std::unique_lock lock(s_mutex_loading);
          shaders_compilation_errors = "Cannot find nor create shaders directory";
          return;
      }
  }
  else if (!std::filesystem::is_directory(directory)) {
      const std::unique_lock lock(s_mutex_loading);
      shaders_compilation_errors = "The shaders path is already taken by a file";
      return;
  }

  if (pipelines_filter.empty()) {
      const std::unique_lock lock_shader_defines(s_mutex_shader_defines);

      code_shaders_defines.clear();
#if DEVELOPMENT
      const auto prev_cb_luma_frame_dev_settings_default_value = cb_luma_frame_dev_settings_default_value;
      cb_luma_frame_dev_settings_default_value = LumaFrameDevSettings(0.f);
      cb_luma_frame_dev_settings_min_value = LumaFrameDevSettings(0.f);
      cb_luma_frame_dev_settings_max_value = LumaFrameDevSettings(1.f);
      cb_luma_frame_dev_settings_names = {};
#endif

      auto settings_directory = directory;
      settings_directory /= "include";
      settings_directory /= "Settings.hlsl";
      if (std::filesystem::is_regular_file(settings_directory)) {
          try {
              std::ifstream file;
              file.exceptions(std::ifstream::failbit | std::ifstream::badbit);
              file.open(settings_directory.c_str()); // Open file
              std::stringstream str_stream;
              str_stream << file.rdbuf(); // Read the file
              std::string str = str_stream.str(); // str holds the content of the file
              size_t i = -1;
              int settings_count = 0;
              while (true) {
                  // Iterate the string line (break) by line (break),
                  // and check for defines values.

                  size_t i0 = i + 1;
                  i = str.find('\n', i0);
                  bool finished = false;
                  if (i0 == i) continue;
                  if (i == std::string::npos) {
                      i = str.length();
                      finished = true;
                  }

                  // TODO: make this more flexible, allowing spaces around "#" and "define" etc,
                  // and defines values that are not numerical (from 0 to 9)
                  std::string_view str_view(&str[i0], i - i0);
                  if (str_view.rfind("#define ", 0) == 0) {
                      str_view = str_view.substr(strlen("#define "));
                      size_t space_index = str_view.find(' ');
                      if (space_index != std::string::npos) {
                          std::string_view define_name = str_view.substr(0, space_index);
                          size_t second_space_index = str_view.find(' ', space_index);
                          if (second_space_index != std::string::npos) {
                              std::string_view define_value = str_view.substr(space_index + 1, second_space_index);
                              uint8_t define_int_value = define_value[0] - '0';
                              if (define_int_value <= 9) {
                                  code_shaders_defines.emplace(define_name, define_int_value);
                              }
                          }
                      }
                  }
#if DEVELOPMENT
                  // Reflections on dev settings.
                  // They can have a comment like "// Default, Min, Max, Name" next to them (e.g. "// 0.5, 0, 1.3, Custom Name").
                  if (str_view.find("float DevSetting") != std::string::npos) {
                      if (settings_count >= LumaFrameDevSettings::SettingsNum) continue;
                      settings_count++;
                      const auto meta_data_pos = str_view.find("//");
                      if (meta_data_pos == std::string::npos) continue;
                      i0 += meta_data_pos + 2;
                      std::string str_line(&str[i0], i - i0);
                      std::stringstream ss(str_line);
                      if (!ss.good()) continue;

                      int settings_float_count = 0;
                      float str_float;
                      bool reached_end = false;
                      while (ss.peek() == ' ') {
                          ss.ignore();
                          if (!ss.good()) { reached_end = true; break; }
                      }
                      // The float read would seemengly advance some state in the stream buffer even if it failed finding it, so skip it in case the next value is not a number (ignore ".3f" like definitions...).
                      // Float heading spaces are automatically ignored.
                      while (!reached_end && ss.peek() >= '0' && ss.peek() <= '9' && ss >> str_float) {
                          if (settings_float_count == 0) cb_luma_frame_dev_settings_default_value[settings_count-1] = str_float;
                          else if (settings_float_count == 1) cb_luma_frame_dev_settings_min_value[settings_count-1] = str_float;
                          else if (settings_float_count == 2) cb_luma_frame_dev_settings_max_value[settings_count-1] = str_float;
                          settings_float_count++;
                          if (!ss.good()) { reached_end = true; break; };
                          // Remove known (supported) characters to ignore (spaces are already ignored above anyway)
                          while (ss.peek() == ',' || ss.peek() == ' ') {
                              ss.ignore();
                              if (!ss.good()) { reached_end = true; break; }
                          }
                      }

                      std::string str;
                      auto ss_pos = ss.tellg();
                      // If we found a string, read the whole remaining stream buffer, otherwise the "str" string would end at the first space
                      if (!reached_end && ss >> str) {
                          cb_luma_frame_dev_settings_names[settings_count - 1] = ss.str();
                          cb_luma_frame_dev_settings_names[settings_count - 1] = cb_luma_frame_dev_settings_names[settings_count - 1].substr(ss_pos, cb_luma_frame_dev_settings_names[settings_count - 1].length() - ss_pos);
                      }
                  }
#endif

                  if (finished) break;
              }
          }
          catch (const std::exception& e) {
          }
#if DEVELOPMENT
          // Re-apply the default settings if they changed
          if (memcmp(&cb_luma_frame_dev_settings_default_value, &prev_cb_luma_frame_dev_settings_default_value, sizeof(cb_luma_frame_dev_settings_default_value)) != 0) {
              const std::unique_lock lock_reshade(s_mutex_reshade);
              cb_luma_frame_settings.DevSettings = cb_luma_frame_dev_settings_default_value;
          }
#endif
      }
      else {
          assert(false); // Missing shader
      }
  }

  std::unordered_set<uint32_t> changed_shaders_hashes;

  for (const auto& entry : std::filesystem::directory_iterator(directory)) {
      if (!entry.is_regular_file()) {
          reshade::log::message(reshade::log::level::warning, "LoadCustomShaders(not a regular file)");
          continue;
      }
      const auto& entry_path = entry.path();
      const bool is_hlsl = entry_path.extension().compare(".hlsl") == 0;
      const bool is_cso = entry_path.extension().compare(".cso") == 0;
      if (!entry_path.has_extension() || !entry_path.has_stem() || (!is_hlsl && !is_cso)) {
          std::stringstream s;
          s << "LoadCustomShaders(Missing extension or stem or unknown extension: ";
          s << entry_path.string();
          s << ")";
          reshade::log::message(reshade::log::level::warning, s.str().c_str());
          continue;
      }

      const auto filename_no_extension_string = entry_path.stem().string();
      std::vector<std::string> hash_strings;
      std::string shader_target;

      if (is_hlsl) {
          auto length = filename_no_extension_string.length();
          if (length < strlen("0x12345678.xx_x_x")) continue;
          ASSERT_ONCE(length > strlen("0x12345678.xx_x_x")); // HLSL files are expected to have a name in front of the hash. They can still be loaded, but they won't be distinguishable from raw cso files
          shader_target = filename_no_extension_string.substr(length - strlen("xx_x_x"), strlen("xx_x_x"));
          if (shader_target[2] != '_') continue;
          if (shader_target[4] != '_') continue;
          size_t next_hash_pos = filename_no_extension_string.find("0x");
          if (next_hash_pos == std::string::npos) continue;
          do {
              hash_strings.push_back(filename_no_extension_string.substr(next_hash_pos + 2 /*0x*/, HASH_CHARACTERS_LENGTH));
              next_hash_pos = filename_no_extension_string.find("0x", next_hash_pos + 1);
          } while (next_hash_pos != std::string::npos);
      }
      else if (is_cso) {
          // As long as cso starts from "0x12345678", it's good, they don't need the shader type specified
          if (filename_no_extension_string.size() < 10) {
              std::stringstream s;
              s << "LoadCustomShaders(Invalid cso file format: ";
              s << filename_no_extension_string;
              s << ")";
              reshade::log::message(reshade::log::level::warning, s.str().c_str());
              continue;
          }
          hash_strings.push_back(filename_no_extension_string.substr(2, HASH_CHARACTERS_LENGTH));

          // Only directly load the cso if no hlsl by the same name exists,
          // which implies that we either did not ship the hlsl and shipped the pre-compiled cso(s),
          // or that this is a vanilla cso dumped from the game.
          // If the hlsl also exists, we load the cso though the hlsl code loading below (by redirecting it).
          // 
          // Note that if we have two shaders with the same hash but different overall name (e.g. the description part of the shader),
          // whichever is iterated last will load on top of the previous one (whether they are both cso, or cso and hlsl etc).
          // We don't care about "fixing" that because it's not a real world case.
          const auto filename_hlsl_string = filename_no_extension_string + ".hlsl";
          if (!std::filesystem::exists(filename_hlsl_string)) continue;
      }
      // Any other case (non hlsl non cso) is already earlied out above

      for (const auto& hash_string : hash_strings) {
          uint32_t shader_hash;
          try {
              shader_hash = std::stoul(hash_string, nullptr, 16);
          }
          catch (const std::exception& e) {
              continue;
          }

          // Early out before compiling
          if (!pipelines_filter.empty()) {
              const std::shared_lock lock(s_mutex_generic);
              bool pipeline_found = false;
              for (const auto& pipeline_pair : pipeline_cache_by_pipeline_handle) {
                  if (std::find(pipeline_pair.second->shader_hashes.begin(), pipeline_pair.second->shader_hashes.end(), shader_hash) == pipeline_pair.second->shader_hashes.end()) continue;
                  if (pipelines_filter.contains(pipeline_pair.first)) {
                      pipeline_found = true;
                  }
                  break;
              }
              if (!pipeline_found) {
                  continue;
              }
          }

          // Add defines to specify the current "target" hash we are building the shader with (some shaders can share multiple permutations (hashes) within the same hlsl)
          std::vector<std::string> local_shader_defines = shader_defines;
          local_shader_defines.push_back("_" + hash_string);
          local_shader_defines.push_back("1");

          char config_name[std::string_view("Shader#").size() + HASH_CHARACTERS_LENGTH + 1] = "";
          sprintf(&config_name[0], "Shader#%s", hash_string.c_str());

          const std::unique_lock lock(s_mutex_loading); // Don't lock until now as we didn't access any shared data
          auto& custom_shader = custom_shaders_cache[shader_hash]; // Add default initialized shader
          const bool has_custom_shader = (custom_shaders_cache.find(shader_hash) != custom_shaders_cache.end()) && (custom_shader != nullptr);
          std::wstring original_file_path_cso; // Only valid for hlsl files
          std::wstring trimmed_file_path_cso; // Only valid for hlsl files

          if (is_hlsl) {
              std::wstring file_path_cso = entry_path.c_str();
              std::wstring hash_wstring = std::wstring(hash_string.begin(), hash_string.end());
              if (file_path_cso.ends_with(L".hlsl")) {
                  file_path_cso = file_path_cso.substr(0, file_path_cso.size() - 5);
                  file_path_cso += L".cso";
              }
              else if (!file_path_cso.ends_with(L".cso")) {
                  file_path_cso += L".cso";
              }
              original_file_path_cso = file_path_cso;

              size_t first_hash_pos = file_path_cso.find(L"0x");
              if (first_hash_pos != std::string::npos) {
                  // Remove all the non first shader hashes in the file (and anything in between them)
                  size_t prev_hash_pos = first_hash_pos;
                  size_t next_hash_pos = file_path_cso.find(L"0x", prev_hash_pos + 1);
                  while (next_hash_pos != std::string::npos && (file_path_cso.length() - next_hash_pos) >= 10) {
                      file_path_cso = file_path_cso.substr(0, prev_hash_pos + 10) + file_path_cso.substr(next_hash_pos + 10);
                      next_hash_pos = file_path_cso.find(L"0x", next_hash_pos + 1);
                      prev_hash_pos = next_hash_pos;
                  }
                  file_path_cso.replace(first_hash_pos + 2 /*0x*/, HASH_CHARACTERS_LENGTH, hash_wstring.c_str());
              }
              trimmed_file_path_cso = file_path_cso;
          }

          if (!has_custom_shader) {
              custom_shader = new CachedCustomShader();

              std::size_t preprocessed_hash = custom_shader->preprocessed_hash;
              // Note that if anybody manually changed the config hash, the data here could mismatch and end up recompiling when not needed or skipping recompilation even if needed (near impossible chance)
              const bool should_load_compiled_shader = is_hlsl && !prevent_shader_cache_loading; // If this shader doesn't have an hlsl, we should never read it or save it on disk, there's no need (we can still fall back on the original .cso if needed)
              if (should_load_compiled_shader && reshade::get_config_value(nullptr, NAME_ADVANCED_SETTINGS.c_str(), &config_name[0], preprocessed_hash)) {
                  // This will load the matching cso
                  // TODO: move these to a sub folder called "cache"? It'd make everything cleaner (and the "CompileCustomShaders()" could simply nuke a directory then, and we could remove the restriction where hlsl files need to have a name in front of the hash),
                  // but it would make it harder to manually remove a single specific shader cso we wanted to nuke for test reasons (especially if we exclusively put the hash in their cso name).
                  // Also it would be a problem due to the custom "native" shaders we have (e.g. "copy") that don't have a target hash they are replacing.
                  if (utils::shader::compiler::LoadCompiledShaderFromFile(custom_shader->code, trimmed_file_path_cso.c_str())) {
                      // If both reading the pre-processor hash from config and the compiled shader from disk succeeded, then we are free to continue as if this shader was working
                      custom_shader->file_path = entry_path;
                      custom_shader->is_hlsl = is_hlsl;
                      custom_shader->preprocessed_hash = preprocessed_hash;
                      changed_shaders_hashes.emplace(shader_hash);
                      // Theoretically at this point, the shader pre-processor below should skip re-compiling this shader unless the hash changed
                  }
              }
          }

          CComPtr<ID3DBlob> uncompiled_code_blob;

          if (is_hlsl) {
              constexpr bool compile_from_current_path = false; // Set this to true to include headers from the current directory instead of the file root folder

              const auto previous_path = std::filesystem::current_path();
              if (compile_from_current_path) {
                  // Set the current path to the shaders directory, it can be needed by the DX compilers (specifically by the preprocess functions)
                  std::filesystem::current_path(directory);
              }

              std::string compilation_errors;

              // Skip compiling the shader if it didn't change
              // Note that this won't replace "custom_shader->compilation_error" unless there was any new error/warning, and that's kind of what we want
              // Note that this will not try to build the shader again if the last compilation failed and its files haven't changed
              bool error = false;
              const bool needs_compilation = utils::shader::compiler::PreprocessShaderFromFile(entry_path.c_str(), compile_from_current_path ? entry_path.filename().c_str() : entry_path.c_str(), shader_target.c_str(), custom_shader->preprocessed_hash, uncompiled_code_blob, local_shader_defines, error, &compilation_errors);

              // Only overwrite the previous compilation error if we have any preprocessor errors
              if (!compilation_errors.empty() || error) {
                  custom_shader->compilation_errors = compilation_errors;
#if DEVELOPMENT || TEST
                  custom_shader->compilation_error = error;
#endif
#if !DEVELOPMENT && !TEST // Ignore warnings for public builds
                  if (error)
#endif
                  {
                      shaders_compilation_errors.append(filename_no_extension_string);
                      shaders_compilation_errors.append(": ");
                      shaders_compilation_errors.append(compilation_errors);
                  }
              }
              // Print out the same (last) compilation errors again if the shader still needs to be compiled but hasn't changed.
              // We might want to ignore this case for public builds (we can't know whether this was an error or a warning atm),
              // but it seems like this can only trigger after a shader had previous failed to build, so these should be guaranteed to be errors,
              // and thus we should be able to print them to all users (we don't want warnings in public builds).
              else if (!needs_compilation && custom_shader->code.size() == 0 && !custom_shader->compilation_errors.empty()) {
                  shaders_compilation_errors.append(filename_no_extension_string);
                  shaders_compilation_errors.append(": ");
                  shaders_compilation_errors.append(custom_shader->compilation_errors);
              }

              if (compile_from_current_path) {
                  // Restore it to avoid unknown consequences
                  std::filesystem::current_path(previous_path);
              }

              if (!needs_compilation) {
                  continue;
              }
          }

          // If we reached this place, we can consider this shader as "changed" even if it will fail compiling.
          // We don't care to avoid adding duplicate elements to this list.
          changed_shaders_hashes.emplace(shader_hash);

          // For extra safety, just clear everything that will be re-assigned below if this custom shader already existed
          if (has_custom_shader) {
              auto preprocessed_hash = custom_shader->preprocessed_hash;
              ClearCustomShader(shader_hash);
              // Keep the data we just filled up
              custom_shader->preprocessed_hash = preprocessed_hash;
          }
          custom_shader->file_path = entry_path;
          custom_shader->is_hlsl = is_hlsl;
          // Clear these in case the compiler didn't overwrite them
          custom_shader->code.clear();
          custom_shader->compilation_errors.clear();
#if DEVELOPMENT || TEST
          custom_shader->compilation_error = false;
#endif

          if (is_hlsl) {
#if _DEBUG && LOG_VERBOSE
              {
                  std::stringstream s;
                  s << "LoadCustomShaders(Compiling file: ";
                  s << entry_path.string();
                  s << ", hash: " << PRINT_CRC32(shader_hash);
                  s << ", target: " << shader_target;
                  s << ")";
                  reshade::log::message(reshade::log::level::debug, s.str().c_str());
              }
#endif

              bool error = false;
              // TODO: specify the name of the function to compile (e.g. "main" or HDRTonemapPS) so we could unify more shaders into a single file with multiple techniques?
              utils::shader::compiler::CompileShaderFromFile(
                  custom_shader->code,
                  uncompiled_code_blob,
                  entry_path.c_str(),
                  shader_target.c_str(),
                  local_shader_defines,
                  !prevent_shader_cache_saving,
                  error,
                  &custom_shader->compilation_errors,
                  trimmed_file_path_cso.c_str());
              ASSERT_ONCE(!trimmed_file_path_cso.empty()); // If we got here, this string should always be valid, as it means the shader read from disk was an hlsl

              // Ugly workaround to avoid providing the shader compiler a custom name for CSO files, given we trim their name from multiple hashes that the HLSL original path might have
              if (!prevent_shader_cache_saving && !original_file_path_cso.empty() && original_file_path_cso != trimmed_file_path_cso) {
                  if (std::filesystem::is_regular_file(original_file_path_cso)) {
                      ASSERT_ONCE(false); // This shouldn't happen anymore unless the shader was manually created or named
                      std::filesystem::remove(trimmed_file_path_cso);
                      std::filesystem::rename(original_file_path_cso, trimmed_file_path_cso);
                  }
              }

              if (!custom_shader->compilation_errors.empty()) {
#if DEVELOPMENT || TEST
                  custom_shader->compilation_error = error;
#endif
#if !DEVELOPMENT && !TEST // Ignore warnings for public builds
                  if (error)
#endif
                  {
                      shaders_compilation_errors.append(filename_no_extension_string);
                      shaders_compilation_errors.append(": ");
                      shaders_compilation_errors.append(custom_shader->compilation_errors);
                  }
              }

              if (custom_shader->code.empty()) {
                  std::stringstream s;
                  s << "LoadCustomShaders(Compilation failed: ";
                  s << entry_path.string();
                  s << ")";
                  reshade::log::message(reshade::log::level::warning, s.str().c_str());

                  continue;
              }
              // Save the matching the pre-compiled shader hash in the config, so we can skip re-compilation on the next boot
              else if (!prevent_shader_cache_saving) {
                  reshade::set_config_value(nullptr, NAME_ADVANCED_SETTINGS.c_str(), &config_name[0], custom_shader->preprocessed_hash);
              }

#if _DEBUG && LOG_VERBOSE
              {
                  std::stringstream s;
                  s << "LoadCustomShaders(Shader built with size: " << custom_shader->code.size() << ")";
                  reshade::log::message(reshade::log::level::debug, s.str().c_str());
              }
#endif
          }
          else if (is_cso) {
              try {
                  std::ifstream file;
                  file.exceptions(std::ifstream::failbit | std::ifstream::badbit);
                  file.open(entry_path, std::ios::binary);
                  file.seekg(0, std::ios::end);
                  custom_shader->code.resize(file.tellg());
#if _DEBUG && LOG_VERBOSE
                  {
                      std::stringstream s;
                      s << "LoadCustomShaders(Reading " << custom_shader->code.size() << " from " << filename_no_extension_string << ")";
                      reshade::log::message(reshade::log::level::debug, s.str().c_str());
                  }
#endif
                  if (!custom_shader->code.empty()) {
                      file.seekg(0, std::ios::beg);
                      file.read(reinterpret_cast<char*>(custom_shader->code.data()), custom_shader->code.size());
                  }
              }
              catch (const std::exception& e) {
              }
          }
      }
  }

  // TODO: theoretically if "prevent_shader_cache_saving" is true, we should clean all the shader hashes and defines from the config, though hopefully it's fine without
  if (pipelines_filter.empty() && !prevent_shader_cache_saving) {
      const std::shared_lock lock(s_mutex_shader_defines);
      // Only save after compiling, to make sure the config data aligns with the serialized compiled shaders data (blobs)
      ShaderDefineData::Save(shader_defines_data);
  }

  auto CreateShaderObject = [&]<typename T>(uint32_t shader_hash, com_ptr<T>& shader_object, bool force_delete_previous = true) {
      if (changed_shaders_hashes.contains(shader_hash)) {
          if (force_delete_previous) {
              // The shader changed, so we should clear its previous version resource anyway (to avoid keeping an outdated version)
              shader_object = nullptr;
          }
          if (custom_shaders_cache.contains(shader_hash)) {
              // Delay the deletition
              if (!force_delete_previous) {
                  shader_object = nullptr;
              }

              const CachedCustomShader* custom_shader_cache = custom_shaders_cache[shader_hash];

              const std::shared_lock lock(s_mutex_device);

              if constexpr (typeid(T) == typeid(ID3D11VertexShader)) {
                  HRESULT hr = global_native_device->CreateVertexShader(custom_shader_cache->code.data(), custom_shader_cache->code.size(), nullptr, &shader_object);
                  assert(SUCCEEDED(hr));
              }
              else if constexpr (typeid(T) == typeid(ID3D11PixelShader)) {
                  HRESULT hr = global_native_device->CreatePixelShader(custom_shader_cache->code.data(), custom_shader_cache->code.size(), nullptr, &shader_object);
                  assert(SUCCEEDED(hr));
              }
              else if constexpr (typeid(T) == typeid(ID3D11ComputeShader)) {
                  HRESULT hr = global_native_device->CreateComputeShader(custom_shader_cache->code.data(), custom_shader_cache->code.size(), nullptr, &shader_object);
                  assert(SUCCEEDED(hr));
              }
              else {
                  static_assert(false);
              }
          }
      }
      };

  // Refresh the persistent custom shaders we have.
  // Note that the hash can be "fake" on custom shaders, as we decide it trough the file names.
  {
      const std::unique_lock lock(s_mutex_shader_objects);
      CreateShaderObject(shader_hash_copy_vertex, copy_vertex_shader, !(bool)FORCE_KEEP_CUSTOM_SHADERS_LOADED);
      CreateShaderObject(shader_hash_copy_pixel, copy_pixel_shader, !(bool)FORCE_KEEP_CUSTOM_SHADERS_LOADED);
      CreateShaderObject(shader_hash_transform_function_copy_pixel, transfer_function_copy_pixel_shader, !(bool)FORCE_KEEP_CUSTOM_SHADERS_LOADED);
      CreateShaderObject(shader_hash_draw_exposure, draw_exposure_pixel_shader, !(bool)FORCE_KEEP_CUSTOM_SHADERS_LOADED);
  }
}

// Optionally compiles all the shaders we have in our data folder and links them with the game rendering pipelines
void LoadCustomShaders(const std::unordered_set<uint64_t>& pipelines_filter = std::unordered_set<uint64_t>(), bool recompile_shaders = true, bool immediate_load = true, bool immediate_unload = false) {
#if _DEBUG && LOG_VERBOSE
  reshade::log::message(reshade::log::level::info, "LoadCustomShaders()");
#endif

  if (recompile_shaders) {
    CompileCustomShaders(pipelines_filter);
  }

  // We can, and should, only lock this after compiling new shaders (above)
  const std::unique_lock lock(s_mutex_generic);

  // Clear all previously loaded custom shaders
  UnloadCustomShaders(pipelines_filter, immediate_unload, false);

  std::unordered_set<uint64_t> cloned_pipelines;

  const std::unique_lock lock_loading(s_mutex_loading);
  for (const auto& custom_shader_pair : custom_shaders_cache) {
    uint32_t shader_hash = custom_shader_pair.first;
    const auto custom_shader = custom_shaders_cache[shader_hash];

    // Skip shaders that don't have code binaries at the moment
    if (custom_shader == nullptr || custom_shader->code.empty()) continue;

    auto pipelines_pair = pipeline_caches_by_shader_hash.find(shader_hash);
    if (pipelines_pair == pipeline_caches_by_shader_hash.end()) {
      std::stringstream s;
      s << "LoadCustomShaders(Unknown hash: ";
      s << PRINT_CRC32(shader_hash);
      s << ")";
      reshade::log::message(reshade::log::level::warning, s.str().c_str());
      continue;
    }

    // Re-clone all the pipelines that used this shader hash (except the ones that are filtered out)
    for (CachedPipeline* cached_pipeline : pipelines_pair->second) {
      if (cached_pipeline == nullptr) continue;
      if (!pipelines_filter.empty() && !pipelines_filter.contains(cached_pipeline->pipeline.handle)) continue;
      if (cloned_pipelines.contains(cached_pipeline->pipeline.handle)) { assert(false); continue; }
      cloned_pipelines.emplace(cached_pipeline->pipeline.handle);
      // Force destroy this pipeline in case it was already cloned
      UnloadCustomShaders({cached_pipeline->pipeline.handle}, immediate_unload, false);

#if _DEBUG && LOG_VERBOSE
      {
        std::stringstream s;
        s << "LoadCustomShaders(Read ";
        s << custom_shader->code.size() << " bytes ";
        s << " from " << custom_shader->file_path.string();
        s << ")";
        reshade::log::message(reshade::log::level::debug, s.str().c_str());
      }
#endif

      // DX12 can use PSO objects that need to be cloned
      const uint32_t subobject_count = cached_pipeline->subobject_count;
      reshade::api::pipeline_subobject* subobjects = cached_pipeline->subobjects_cache;
      reshade::api::pipeline_subobject* new_subobjects = utils::pipeline::ClonePipelineSubObjects(subobject_count, subobjects);

#if _DEBUG && LOG_VERBOSE
      {
        std::stringstream s;
        s << "LoadCustomShaders(Cloning pipeline ";
        s << reinterpret_cast<void*>(cached_pipeline->pipeline.handle);
        s << " with " << subobject_count << " object(s)";
        s << ")";
        reshade::log::message(reshade::log::level::debug, s.str().c_str());
      }
      reshade::log::message(reshade::log::level::debug, "Iterating pipeline...");
#endif

      for (uint32_t i = 0; i < subobject_count; ++i) {
        const auto& subobject = subobjects[i];
        switch (subobject.type) {
          case reshade::api::pipeline_subobject_type::vertex_shader:
            [[fallthrough]];
          case reshade::api::pipeline_subobject_type::compute_shader:
            [[fallthrough]];
          case reshade::api::pipeline_subobject_type::pixel_shader:
            break;
          default:
            continue;
        }

        auto& clone_subject = new_subobjects[i];

        auto* new_desc = static_cast<reshade::api::shader_desc*>(clone_subject.data);

        new_desc->code_size = custom_shader->code.size();
        new_desc->code = malloc(custom_shader->code.size());
        std::memcpy(const_cast<void*>(new_desc->code), custom_shader->code.data(), custom_shader->code.size());

        const auto new_hash = compute_crc32(static_cast<const uint8_t*>(new_desc->code), new_desc->code_size);

#if _DEBUG && LOG_VERBOSE
        {
            std::stringstream s;
            s << "LoadCustomShaders(Injected pipeline data";
            s << " with " << PRINT_CRC32(new_hash);
            s << " (" << custom_shader->code.size() << " bytes)";
            s << ")";
            reshade::log::message(reshade::log::level::debug, s.str().c_str());
        }
#endif
      }

#if _DEBUG && LOG_VERBOSE
      {
        std::stringstream s;
        s << "Creating pipeline clone (";
        s << "hash: " << PRINT_CRC32(shader_hash);
        s << ", layout: " << reinterpret_cast<void*>(cached_pipeline->layout.handle);
        s << ", subobject_count: " << subobject_count;
        s << ")";
        reshade::log::message(reshade::log::level::debug, s.str().c_str());
      }
#endif

      reshade::api::pipeline pipeline_clone;
      const bool built_pipeline_ok = cached_pipeline->device->create_pipeline(
          cached_pipeline->layout,
          subobject_count,
          new_subobjects,
          &pipeline_clone);
#if !_DEBUG || !LOG_VERBOSE
      if (!built_pipeline_ok)
#endif
      {
          std::stringstream s;
          s << "LoadCustomShaders(cloned ";
          s << reinterpret_cast<void*>(cached_pipeline->pipeline.handle);
          s << " => " << reinterpret_cast<void*>(pipeline_clone.handle);
          s << ", layout: " << reinterpret_cast<void*>(cached_pipeline->layout.handle);
          s << ", size: " << subobject_count;
          s << ", " << (built_pipeline_ok ? "OK" : "FAILED!");
          s << ")";
          reshade::log::message(built_pipeline_ok ? reshade::log::level::info : reshade::log::level::error, s.str().c_str());
      }

      if (built_pipeline_ok) {
        assert(!cached_pipeline->cloned && cached_pipeline->pipeline_clone.handle == 0);
        cached_pipeline->cloned = true;
        cached_pipeline->ready_for_binding = immediate_load;
        cached_pipeline->pipeline_clone = pipeline_clone;
        pipeline_cache_by_pipeline_clone_handle[pipeline_clone.handle] = cached_pipeline;
        cloned_pipeline_count++;
        cloned_pipelines_changed = true;
      }
      // Clean up unused cloned subobjects
      else {
        DestroyPipelineSubojects(new_subobjects, subobject_count);
        new_subobjects = nullptr;
      }
    }
  }
}

// TODO: optimize
std::optional<std::string> ReadTextFile(const std::filesystem::path& path) {
  std::vector<uint8_t> data;
  std::optional<std::string> result;
  std::ifstream file(path, std::ios::binary);
  if (!file) return result;
  file.seekg(0, std::ios::end);
  const size_t file_size = file.tellg();
  if (file_size == 0) return result;

  data.resize(file_size);
  file.seekg(0, std::ios::beg).read(reinterpret_cast<char*>(data.data()), file_size);
  result = std::string(reinterpret_cast<const char*>(data.data()), file_size);
  return result;
}

OVERLAPPED overlapped;
HANDLE m_target_dir_handle = INVALID_HANDLE_VALUE;

bool needs_watcher_init = true;

std::aligned_storage_t<1U << 18, std::max<size_t>(alignof(FILE_NOTIFY_EXTENDED_INFORMATION), alignof(FILE_NOTIFY_INFORMATION))> watch_buffer;

void CALLBACK HandleEventCallback(DWORD error_code, DWORD bytes_transferred, LPOVERLAPPED overlapped) {
#if _DEBUG && LOG_VERBOSE
  reshade::log::message(reshade::log::level::info, "Live editing callback");
#endif
  // TODO: verify this is safe. Replacing shaders from another thread at a random time could break as we need to wait one frame or the pipeline binding could hang.
  LoadCustomShaders();
  // Trigger the watch again as the event is only triggered once
  ToggleLiveWatching();
}

void CheckForLiveUpdate() {
  if (live_reload && !last_pressed_unload) {
    WaitForSingleObjectEx(overlapped.hEvent, 0, TRUE);
  }
}

void ToggleLiveWatching() {
  if (live_reload && !last_pressed_unload) {
    auto directory = GetShaderPath();
    if (!std::filesystem::exists(directory)) {
      std::filesystem::create_directory(directory);
    }
    else if (!std::filesystem::is_directory(directory))
    {
        reshade::log::message(reshade::log::level::error, "ToggleLiveWatching: the target path is already taken by a file");
        CancelIoEx(m_target_dir_handle, &overlapped);
        return;
    }

#if _DEBUG && LOG_VERBOSE
    reshade::log::message(reshade::log::level::info, "Watching live");
#endif

#if 0
    // Clean up any previous handle for safety
    if (m_target_dir_handle != INVALID_HANDLE_VALUE) {
      CancelIoEx(m_target_dir_handle, &overlapped);
    }
#endif

    m_target_dir_handle = CreateFileW(
        directory.c_str(),
        FILE_LIST_DIRECTORY,
        (FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE),
        NULL,  // NOLINT
        OPEN_EXISTING,
        (FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED),
        NULL  // NOLINT
    );
    if (m_target_dir_handle == INVALID_HANDLE_VALUE) {
      reshade::log::message(reshade::log::level::error, "ToggleLiveWatching(targetHandle: invalid)");
      return;
    }
#if _DEBUG && LOG_VERBOSE
    {
      std::stringstream s;
      s << "ToggleLiveWatching(targetHandle: ";
      s << reinterpret_cast<void*>(m_target_dir_handle);
      reshade::log::message(reshade::log::level::info, s.str().c_str());
    }
#endif

    memset(&watch_buffer, 0, sizeof(watch_buffer));
    overlapped = {0};
    overlapped.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);  // NOLINT

    const BOOL success = ReadDirectoryChangesExW(
        m_target_dir_handle,
        &watch_buffer,
        sizeof(watch_buffer),
        TRUE,
        FILE_NOTIFY_CHANGE_FILE_NAME
            | FILE_NOTIFY_CHANGE_DIR_NAME
            | FILE_NOTIFY_CHANGE_ATTRIBUTES
            | FILE_NOTIFY_CHANGE_SIZE
            | FILE_NOTIFY_CHANGE_CREATION
            | FILE_NOTIFY_CHANGE_LAST_WRITE,
        NULL,  // NOLINT
        &overlapped,
        &HandleEventCallback,
        ReadDirectoryNotifyExtendedInformation);

    if (success == S_OK) {
#if _DEBUG && LOG_VERBOSE
      reshade::log::message(reshade::log::level::info, "ToggleLiveWatching(ReadDirectoryChangesExW: Listening.)");
#endif
    } else {
      std::stringstream s;
      s << "ToggleLiveWatching(ReadDirectoryChangesExW: Failed: ";
      s << GetLastError();
      s << ")";
      reshade::log::message(reshade::log::level::error, s.str().c_str());
    }

    LoadCustomShaders();
  } else {
#if _DEBUG && LOG_VERBOSE
    reshade::log::message(reshade::log::level::info, "Cancelling live");
#endif
    CancelIoEx(m_target_dir_handle, &overlapped);
  }
}

void OnDisplayModeChanged()
{
  // s_mutex_reshade should already be locked here, it's not necessary anyway
  GetShaderDefineData(GAMMA_CORRECTION_TYPE_HASH).editable = cb_luma_frame_settings.DisplayMode != 0;
  GetShaderDefineData(AUTO_HDR_VIDEOS_HASH).editable = cb_luma_frame_settings.DisplayMode != 0;
}

void OnInitDevice(reshade::api::device* device) {
  auto& device_data = device->create_private_data<DeviceData>();
  ID3D11Device* native_device = (ID3D11Device*)(device->get_native());
  {
      const std::unique_lock lock(s_mutex_device);
      assert(!global_native_device);
      global_native_device = native_device;
  }

  {
      const std::shared_lock lock(device_data.mutex); // Not much need to lock this on its own creation, but let's do it anyway...
      reshade::api::pipeline_layout_param pipeline_layout_param = reshade::api::pipeline_layout_param();
      pipeline_layout_param.type = reshade::api::pipeline_layout_param_type::push_constants;  // We could be using "reshade::api::pipeline_layout_param_type::push_descriptors" in cbuffer mode too but this is simpler
      pipeline_layout_param.push_constants.count = 1;
      pipeline_layout_param.push_constants.dx_register_index = shader_cbuffers_index;
      pipeline_layout_param.push_constants.visibility = reshade::api::shader_stage::vertex | reshade::api::shader_stage::pixel | reshade::api::shader_stage::compute;
      auto result = device->create_pipeline_layout(1, &pipeline_layout_param, &device_data.settings_pipeline_layout);

      pipeline_layout_param.push_constants.count = 1;
      pipeline_layout_param.push_constants.dx_register_index = ui_cbuffer_index + 1;
      result = device->create_pipeline_layout(1, &pipeline_layout_param, &device_data.shared_data_pipeline_layout);
      pipeline_layout_param.push_constants.dx_register_index = ui_cbuffer_index;
      result = device->create_pipeline_layout(1, &pipeline_layout_param, &device_data.ui_pipeline_layout);
  }

  D3D11_BLEND_DESC blend_state_desc;
  blend_state_desc.AlphaToCoverageEnable = FALSE;
  blend_state_desc.IndependentBlendEnable = FALSE;
  // We only need RT 0
  blend_state_desc.RenderTarget[0].BlendEnable = FALSE;
  blend_state_desc.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
  blend_state_desc.RenderTarget[0].DestBlend = D3D11_BLEND_ZERO;
  blend_state_desc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
  blend_state_desc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
  blend_state_desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
  blend_state_desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
  blend_state_desc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
  native_device->CreateBlendState(&blend_state_desc, &default_blend_state);

#if ENABLE_NGX
  assert(!NGX::DLSS::HasInit()); // DLSS is only supported on one device at a time
  com_ptr<IDXGIDevice> native_dxgi_device;
  HRESULT hr = native_device->QueryInterface(&native_dxgi_device);
  com_ptr<IDXGIAdapter> native_adapter;
  if (SUCCEEDED(hr)) {
      hr = native_dxgi_device->GetAdapter(&native_adapter);
  }
  assert(SUCCEEDED(hr));

  dlss_sr_supported = NGX::DLSS::Init(native_device, native_adapter.get());
  if (!dlss_sr_supported) {
      dlss_output_color = nullptr;
      dlss_exposure = nullptr;
      dlss_motion_vectors = nullptr;
      dlss_motion_vectors_rtv = nullptr;
      dlss_render_resolution_scale = 1.f;
      dlss_scene_exposure = 1.f;
      dlss_scene_pre_exposure = 1.f;
      exposure_buffer_gpu = nullptr;
      exposure_buffer_cpu = nullptr;
      exposure_buffer_rtv = nullptr;

      NGX::DLSS::Deinit(native_device); // No need to keep it initialized if it's not supported
      dlss_sr = false; // No need to serialize this to config really
  }
// Optionally unload dlss if it's supported but currently not enabled
#if !DLSS_KEEP_DLL_LOADED
  else if (!dlss_sr) {
      NGX::DLSS::Deinit(native_device);
  }
#endif
#endif // NGX
}

void OnDestroyDevice(reshade::api::device* device) {
  auto& device_data = device->get_private_data<DeviceData>(); // No need to lock the data mutex here, it could be concurrently used at this point

  {
      const std::shared_lock lock(device_data.mutex);
      device->destroy_pipeline_layout(device_data.ui_pipeline_layout);
      device->destroy_pipeline_layout(device_data.shared_data_pipeline_layout);
      device->destroy_pipeline_layout(device_data.settings_pipeline_layout);
  }

  assert(cb_per_view_global_buffer_map_data == nullptr);
  cb_per_view_global_buffer = nullptr;

  {
      const std::unique_lock lock_samplers(s_mutex_samplers);
      ASSERT_ONCE(custom_sampler_by_original_sampler.empty()); // Is this guaranteed in DX? Maybe not, but probably is!
      custom_sampler_by_original_sampler.clear();
  }

#if ENABLE_NGX
  ID3D11Device* native_device = (ID3D11Device*)(device->get_native());
  NGX::DLSS::Deinit(native_device); // NOTE: this could stutter the game on closure as it forces unloading the DLSS DLL, we could theoretically avoid it (not sure if we'd run into errors)
  dlss_output_color = nullptr;
  dlss_exposure = nullptr;
  dlss_motion_vectors = nullptr;
  dlss_motion_vectors_rtv = nullptr;
  exposure_buffer_gpu = nullptr;
  exposure_buffer_cpu = nullptr; // Hopefully it's fine to let go of this buffer com ptr and destroy the device without unmapping the resource first
  exposure_buffer_rtv = nullptr;
#endif // NGX

  copy_texture = nullptr;
  transfer_function_copy_texture = nullptr;
  transfer_function_copy_shader_resource_view = nullptr;
  {
      const std::unique_lock lock(s_mutex_shader_objects);
      copy_vertex_shader = nullptr;
      copy_pixel_shader = nullptr;
      transfer_function_copy_pixel_shader = nullptr;
      draw_exposure_pixel_shader = nullptr;
  }

  default_blend_state = nullptr;

  {
      const std::unique_lock lock(s_mutex_device);
      assert(!native_swapchain3);
      assert(global_native_device);
      global_native_device = nullptr;
  }

  device->destroy_private_data<DeviceData>();
}

void OnInitSwapchain(reshade::api::swapchain* swapchain) {
  const size_t back_buffer_count = swapchain->get_back_buffer_count();
  auto& swapchain_data = swapchain->create_private_data<SwapchainData>();
  {
    const std::unique_lock lock(swapchain_data.mutex); // Not much need to lock this on its own creation, but let's do it anyway...
    for (uint32_t index = 0; index < back_buffer_count; index++) {
      auto buffer = swapchain->get_back_buffer(index);
      swapchain_data.back_buffers.emplace(buffer.handle);
    }
  }
  auto* device = swapchain->get_device();
  if (device != nullptr) {
    auto& device_data = device->get_private_data<DeviceData>();
    const std::unique_lock lock(device_data.mutex);
    device_data.swapchains.emplace(swapchain);

    for (uint32_t index = 0; index < back_buffer_count; index++) {
      auto buffer = swapchain->get_back_buffer(index);
      device_data.back_buffers.emplace(buffer.handle);
    }
  }

  IDXGISwapChain* native_swapchain = (IDXGISwapChain*)(swapchain->get_native());
  DXGI_SWAP_CHAIN_DESC swapchain_desc;
  HRESULT hr = native_swapchain->GetDesc(&swapchain_desc);
  ASSERT_ONCE(SUCCEEDED(hr));
  if (SUCCEEDED(hr)) {
      output_resolution.x = swapchain_desc.BufferDesc.Width;
      output_resolution.y = swapchain_desc.BufferDesc.Height;
      render_resolution.x = output_resolution.x;
      render_resolution.y = output_resolution.y;
  }

  IDXGISwapChain3* local_native_swapchain3;
  {
      const std::unique_lock lock(s_mutex_device);
      ASSERT_ONCE(native_swapchain3 == nullptr);
      native_swapchain3 = nullptr;
      // The cast pointer is actually the same, we are just making sure the type is right.
      hr = native_swapchain->QueryInterface(&native_swapchain3);
      ASSERT_ONCE(SUCCEEDED(hr));
      local_native_swapchain3 = native_swapchain3;
  }
  // This is basically where we verify and update the user display settings
  if (local_native_swapchain3 != nullptr) {
      const std::unique_lock lock_reshade(s_mutex_reshade);
      GetHDRMaxLuminance(local_native_swapchain3, default_user_peak_white, srgb_white_level);
      IsHDRSupportedAndEnabled(swapchain_desc.OutputWindow, hdr_supported_display, hdr_enabled_display, local_native_swapchain3);
      game_window = swapchain_desc.OutputWindow; // This shouldn't really need any thread safety protection

      if (!hdr_enabled_display) {
          // Force the display mode to SDR if HDR is not engaged
          cb_luma_frame_settings.DisplayMode = 0;
          OnDisplayModeChanged();
          cb_luma_frame_settings.ScenePeakWhite = srgb_white_level;
          cb_luma_frame_settings.ScenePaperWhite = srgb_white_level;
          cb_luma_frame_settings.UIPaperWhite = srgb_white_level;
      } else if (cb_luma_frame_settings.DisplayMode < 2) {
          cb_luma_frame_settings.ScenePeakWhite = default_user_peak_white;
      }

      // We release the resource because the swapchain lifespan is, and should be, controlled by the game.
      // We already have "OnDestroySwapchain()" to handle its destruction.
      local_native_swapchain3->Release();
  }
}

void OnDestroySwapchain(reshade::api::swapchain* swapchain) {
  {
      const std::unique_lock lock(s_mutex_device);
      assert(native_swapchain3 != nullptr);
      if ((IDXGISwapChain*)(swapchain->get_native()) == native_swapchain3) {
          native_swapchain3 = nullptr;
      }
      else {
          assert(false); // There's seemengly more than one Swapchain?
      }
  }

  auto* device = swapchain->get_device();
  if (device != nullptr) {
    auto& device_data = device->get_private_data<DeviceData>();
    const std::unique_lock lock(device_data.mutex);
    device_data.swapchains.erase(swapchain);
    auto& swapchain_data = swapchain->get_private_data<SwapchainData>();
    for (const uint64_t handle : swapchain_data.back_buffers) {
      device_data.back_buffers.erase(handle);
    }
  }
}

void OnInitCommandList(reshade::api::command_list* cmd_list) {
    auto& cmd_list_data = cmd_list->create_private_data<CommandListData>();
}

#if DEVELOPMENT
void OnInitPipelineLayout(
    reshade::api::device* device,
    const uint32_t param_count,
    const reshade::api::pipeline_layout_param* params,
    reshade::api::pipeline_layout layout) {
  uint32_t cbv_index = 0;
  uint32_t pc_count = 0;

  for (uint32_t param_index = 0; param_index < param_count; ++param_index) {
    auto param = params[param_index];
    if (param.type == reshade::api::pipeline_layout_param_type::descriptor_table) {
      for (uint32_t range_index = 0; range_index < param.descriptor_table.count; ++range_index) {
        auto range = param.descriptor_table.ranges[range_index];
        if (range.type == reshade::api::descriptor_type::constant_buffer) {
          if (cbv_index < range.dx_register_index + range.count) {
            cbv_index = range.dx_register_index + range.count;
          }
        }
      }
    } else if (param.type == reshade::api::pipeline_layout_param_type::push_constants) {
      pc_count++;
      if (cbv_index < param.push_constants.dx_register_index + param.push_constants.count) {
        cbv_index = param.push_constants.dx_register_index + param.push_constants.count;
      }
    } else if (param.type == reshade::api::pipeline_layout_param_type::push_descriptors) {
      if (param.push_descriptors.type == reshade::api::descriptor_type::constant_buffer) {
        if (cbv_index < param.push_descriptors.dx_register_index + param.push_descriptors.count) {
          cbv_index = param.push_descriptors.dx_register_index + param.push_descriptors.count;
        }
      }
    }
  }
}
#endif // DEVELOPMENT

void OnInitPipeline(
    reshade::api::device* device,
    reshade::api::pipeline_layout layout,
    uint32_t subobject_count,
    const reshade::api::pipeline_subobject* subobjects,
    reshade::api::pipeline pipeline) {
    // In DX11 each pipeline should only have one subobject (e.g. a shader)
    for (uint32_t i = 0; i < subobject_count; ++i) {
        const auto& subobject = subobjects[i];
        for (uint32_t j = 0; j < subobject.count; ++j) {
            switch (subobject.type) {
            case reshade::api::pipeline_subobject_type::vertex_shader:
            case reshade::api::pipeline_subobject_type::compute_shader:
            case reshade::api::pipeline_subobject_type::pixel_shader: {
                break;
            default:
                return; // Nothing to do here, we don't want to clone the pipeline
            }
            }
        }
    }
  reshade::api::pipeline_subobject* subobjects_cache = utils::pipeline::ClonePipelineSubObjects(subobject_count, subobjects);

  auto* cached_pipeline = new CachedPipeline{
      pipeline,
      device,
      layout,
      subobjects_cache,
      subobject_count};

  bool found_replaceable_shader = false;
  bool found_custom_shader_file = false;

  const std::unique_lock lock(s_mutex_generic);
  for (uint32_t i = 0; i < subobject_count; ++i) {
    const auto& subobject = subobjects[i];
    for (uint32_t j = 0; j < subobject.count; ++j) {
      switch (subobject.type) {
        case reshade::api::pipeline_subobject_type::vertex_shader:
        case reshade::api::pipeline_subobject_type::compute_shader:
        case reshade::api::pipeline_subobject_type::pixel_shader: {
          auto* new_desc = static_cast<reshade::api::shader_desc*>(subobjects_cache[i].data);
          if (new_desc->code_size == 0) break;
          found_replaceable_shader = true;
          auto shader_hash = compute_crc32(static_cast<const uint8_t*>(new_desc->code), new_desc->code_size);

#if ALLOW_SHADERS_DUMPING
          {
            const std::unique_lock lock_dumping(s_mutex_dumping);

            // Delete any previous shader with the same hash (unlikely to happen, but safer nonetheless)
            if (auto previous_shader_pair = shader_cache.find(shader_hash); previous_shader_pair != shader_cache.end() && previous_shader_pair->second != nullptr) {
              auto& previous_shader = previous_shader_pair->second;
              // Make sure that two shaders have the same hash, their code size also matches (theoretically we could check even more, but the chances hashes overlapping is extremely small)
              assert(previous_shader->size == new_desc->code_size);
#if DEVELOPMENT
              shader_cache_count--;
#endif
              delete previous_shader->data;
              delete previous_shader;
            }

            // Cache shader
            auto* cache = new CachedShader{
                malloc(new_desc->code_size),
                new_desc->code_size,
                subobject.type};
            std::memcpy(cache->data, new_desc->code, cache->size);
#if DEVELOPMENT
            shader_cache_count++;
#endif
            shader_cache[shader_hash] = cache;
            shaders_to_dump.emplace(shader_hash);
          }
#endif // ALLOW_SHADERS_DUMPING

          // Indexes
          assert(std::find(cached_pipeline->shader_hashes.begin(), cached_pipeline->shader_hashes.end(), shader_hash) == cached_pipeline->shader_hashes.end());
          cached_pipeline->shader_hashes.emplace_back(shader_hash);
          ASSERT_ONCE(cached_pipeline->shader_hashes.size() == 1); // Just to make sure if this actually happens

          // Make sure we didn't already have a valid pipeline in there (this should never happen)
          auto pipelines_pair = pipeline_caches_by_shader_hash.find(shader_hash);
          if (pipelines_pair != pipeline_caches_by_shader_hash.end()) {
            pipelines_pair->second.emplace(cached_pipeline);
          } else {
            pipeline_caches_by_shader_hash[shader_hash] = { cached_pipeline };
          }
          {
            const std::shared_lock lock(s_mutex_loading);
            found_custom_shader_file |= custom_shaders_cache.contains(shader_hash);
          }

#if _DEBUG && LOG_VERBOSE
          // Metrics
          {
            std::stringstream s2;
            s2 << "caching shader(";
            s2 << "hash: " << PRINT_CRC32(shader_hash);
            s2 << ", type: " << subobject.type;
            s2 << ", pipeline: " << reinterpret_cast<void*>(pipeline.handle);
            s2 << ")";
            reshade::log::message(reshade::log::level::info, s2.str().c_str());
          }
#endif // DEVELOPMENT
          break;
        }
      }
    }
  }
  if (!found_replaceable_shader) {
    delete cached_pipeline;
    cached_pipeline = nullptr;
    DestroyPipelineSubojects(subobjects_cache, subobject_count);
    subobjects_cache = nullptr;
    return;
  }
  pipeline_cache_by_pipeline_handle[pipeline.handle] = cached_pipeline;

  // Automatically load any custom shaders that might have been bound to this pipeline.
  // To avoid this slowing down everything, we only do it if we detect the user already had a matching shader in its custom shaders folder.
  if (auto_load && !last_pressed_unload && found_custom_shader_file) {
    const std::unique_lock lock_loading(s_mutex_loading);
    // Immediately cloning and replacing the pipeline might be unsafe, we need to delay it to the next frame.
    pipelines_to_reload.emplace(pipeline.handle);
    if (precompile_custom_shaders) {
      // If done with the "immediate" flag, this is possibly unsafe on some games or hardware configurations, thus it can hang the game (even if it seems like it should be safe given it doesn't do anything other than create a cloned pipeline without binding it yet) (this works absolute fine in Prey so we can do it).
      // If done without the "immediate" flag, this will cause a hitch due to shader compilation (unless precompile_custom_shaders is true), and still start drawing one frame after, so it's better to rely on the "AutoLoadShaders()" function.
      const bool immediate = true;
      LoadCustomShaders(pipelines_to_reload, !precompile_custom_shaders, immediate);
      pipelines_to_reload.clear();
    }
  }
}

void OnDestroyPipeline(
    reshade::api::device* device,
    reshade::api::pipeline pipeline) {
  {
    const std::unique_lock lock_loading(s_mutex_loading);
    pipelines_to_reload.erase(pipeline.handle);
  }

  const std::unique_lock lock(s_mutex_generic);
  if (auto pipeline_cache_pair = pipeline_cache_by_pipeline_handle.find(pipeline.handle); pipeline_cache_pair != pipeline_cache_by_pipeline_handle.end()) {
    auto& cached_pipeline = pipeline_cache_pair->second;

    if (cached_pipeline != nullptr) {
      // Clean other references to the pipeline
      for (auto& pipelines_cache_pair : pipeline_caches_by_shader_hash) {
        auto& cached_pipelines = pipelines_cache_pair.second;
        cached_pipelines.erase(cached_pipeline);
      }

      // Destroy our cloned subojects
      DestroyPipelineSubojects(cached_pipeline->subobjects_cache, cached_pipeline->subobject_count);
      cached_pipeline->subobjects_cache = nullptr;

      // Destroy our cloned version of the pipeline (and leave the original intact)
      if (cached_pipeline->cloned) {
        cached_pipeline->cloned = false;
        cached_pipeline->device->destroy_pipeline(cached_pipeline->pipeline_clone);
        pipeline_cache_by_pipeline_clone_handle.erase(cached_pipeline->pipeline_clone.handle);
        cloned_pipeline_count--;
        cloned_pipelines_changed = true;
      }
      free(cached_pipeline);
      cached_pipeline = nullptr;
    }

    pipeline_cache_by_pipeline_handle.erase(pipeline.handle);
  }
}

void OnBindPipeline(
    reshade::api::command_list* cmd_list,
    reshade::api::pipeline_stage stages,
    reshade::api::pipeline pipeline) {
  auto& cmd_list_data = cmd_list->get_private_data<CommandListData>();

  if ((stages & reshade::api::pipeline_stage::compute_shader) != 0) {
      ASSERT_ONCE(stages == reshade::api::pipeline_stage::compute_shader || stages == reshade::api::pipeline_stage::all); // Make sure only one stage happens at a time (it does in DX11)
      cmd_list_data.pipeline_state_original_compute_shader = pipeline;
  }
  if ((stages & reshade::api::pipeline_stage::vertex_shader) != 0) {
      ASSERT_ONCE(stages == reshade::api::pipeline_stage::vertex_shader || stages == reshade::api::pipeline_stage::all); // Make sure only one stage happens at a time (it does in DX11)
      cmd_list_data.pipeline_state_original_vertex_shader = pipeline;
  }
  if ((stages & reshade::api::pipeline_stage::pixel_shader) != 0) {
      ASSERT_ONCE(stages == reshade::api::pipeline_stage::pixel_shader || stages == reshade::api::pipeline_stage::all); // Make sure only one stage happens at a time (it does in DX11)
      cmd_list_data.pipeline_state_original_pixel_shader = pipeline;
  }

  const std::shared_lock lock(s_mutex_generic);
  auto pair = pipeline_cache_by_pipeline_handle.find(pipeline.handle);
  if (pair == pipeline_cache_by_pipeline_handle.end() || pair->second == nullptr) return;

  auto* cached_pipeline = pair->second;

#if DEVELOPMENT
  if (cached_pipeline->test) {
    // This will make the shader output black, or skip drawing, so we can easily detect it. This might not be very safe but seems to work in DX11.
    // TODO: replace the pipeline with a shader that outputs all "SV_Target" as purple for more visiblity,
    // or return false in "reshade::addon_event::draw_or_dispatch_indirect" and similar draw calls to prevent them from being drawn.
    cmd_list->bind_pipeline(stages, reshade::api::pipeline{0});
  }
  else
#endif
  if (cached_pipeline->cloned && cached_pipeline->ready_for_binding) {
    cmd_list->bind_pipeline(stages, cached_pipeline->pipeline_clone);
  }

#if DEVELOPMENT
  if (!trace_running) return;

  const std::unique_lock lock_trace(s_mutex_trace);

  bool add_pipeline_trace = true;
  // Not a particularly useful feature anymore, given that it hides away passes (we might wanna add some g-buffer geometry draws from the list but not the post processing stuff for example)
  if (trace_list_unique_shaders_only) {
    auto trace_count = trace_shader_hashes.size();
    for (auto index = 0; index < trace_count; index++) {
      auto hash = trace_shader_hashes.at(index);
      if (std::find(cached_pipeline->shader_hashes.begin(), cached_pipeline->shader_hashes.end(), hash) != cached_pipeline->shader_hashes.end()) {
        trace_shader_hashes.erase(trace_shader_hashes.begin() + index);
        add_pipeline_trace = false;
        break;
      }
    }
  }

  if (trace_ignore_vertex_shaders && (stages == reshade::api::pipeline_stage::vertex_shader || stages == reshade::api::pipeline_stage::input_assembler)) {
    add_pipeline_trace = false;
  }

  // Pipelines are always "unique" (in DX11 they are simply shader state set calls)
  // TODO: move this to actual draw calls, not pipelines bindings...
  if (add_pipeline_trace) {
    trace_pipeline_handles.push_back(cached_pipeline->pipeline.handle);
    trace_pipeline_draws.push_back(0);
    trace_threads.push_back(std::this_thread::get_id());
  }

  for (auto shader_hash : cached_pipeline->shader_hashes) {
    if (!trace_list_unique_shaders_only || std::find(trace_shader_hashes.begin(), trace_shader_hashes.end(), shader_hash) == trace_shader_hashes.end()) {
      trace_shader_hashes.push_back(shader_hash);
    }
  }
#endif // DEVELOPMENT
}

enum LumaConstantBufferType {
    LumaSettings,
    LumaData,
    LumaLumaUIData
};

void SetPreyLumaConstantBuffers(reshade::api::command_list* cmd_list, reshade::api::shader_stage stages, reshade::api::pipeline_layout layout, LumaConstantBufferType type, uint32_t custom_data = 0) {
    switch (type) {
    case LumaConstantBufferType::LumaSettings: {
        const std::shared_lock lock_reshade(s_mutex_reshade);
        cmd_list->push_constants(
            stages,
            layout,
            0,
            0,
            sizeof(LumaFrameSettings) / sizeof(uint32_t),
            &cb_luma_frame_settings);
        break;
    }
    case LumaConstantBufferType::LumaData: {
        LumaFrameData frame_data;
        frame_data.PostEarlyUpscaling = has_drawn_dlss_sr && !has_drawn_upscaling;
        frame_data.CustomData = custom_data;
        frame_data.Padding = 0;
        frame_data.FrameIndex = frame_index;
        frame_data.CameraJitters = projection_jitters; //TODOFT: pre-multiply these by float2(0.5, -0.5) (NDC to UV space) given that they are always used like that in shaders
        frame_data.PreviousCameraJitters = previous_projection_jitters;
        frame_data.RenderResolutionScale.x = render_resolution.x / output_resolution.x;
        frame_data.RenderResolutionScale.y = render_resolution.y / output_resolution.y;
        // Always do this relative to the current output resolution
        frame_data.PreviousRenderResolutionScale.x = previous_render_resolution.x / output_resolution.x;
        frame_data.PreviousRenderResolutionScale.y = previous_render_resolution.y / output_resolution.y;
        frame_data.ViewProjectionMatrix = cb_per_view_global.CV_ViewProjMatr; //TODOFT3: delete? are we using these? Make it thread safe
        frame_data.PreviousViewProjectionMatrix = cb_per_view_global_previous.CV_ViewProjMatr;
        frame_data.ReprojectionMatrix = reprojection_matrix;
        cmd_list->push_constants(
            stages,
            layout,
            0,
            0,
            sizeof(LumaFrameData) / sizeof(uint32_t),
            &frame_data);
        break;
    }
    case LumaConstantBufferType::LumaLumaUIData: {
        ASSERT_ONCE(false); // Not implemented (yet?)
        break;
    }
    }
}

void DrawCustomPixelShader(ID3D11DeviceContext* device_context, ID3D11BlendState* blend_state, ID3D11VertexShader* vs, ID3D11PixelShader* ps, ID3D11ShaderResourceView* source_resource_texture_view, ID3D11RenderTargetView* target_resource_texture_view, UINT width, UINT height, bool alpha = true) {
    // Set the new resources/states:
    constexpr FLOAT blend_factor_alpha[4] = { 1.f, 1.f, 1.f, 1.f };
    constexpr FLOAT blend_factor[4] = { 1.f, 1.f, 1.f, 0.f };
    device_context->OMSetBlendState(blend_state, alpha ? blend_factor_alpha : blend_factor, 0xFFFFFFFF);
    // Note: we don't seem to need to call (and cache+restore) IASetVertexBuffers().
    // That's either because Prey always has vertices buffers set in there already, or because DX is tolerant enough (we are not seeing any etc errors in the DX log).
    device_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY::D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    D3D11_RECT scissor_rect;
    scissor_rect.left = 0;
    scissor_rect.top = 0;
    scissor_rect.right = width;
    scissor_rect.bottom = height;
    device_context->RSSetScissorRects(1, &scissor_rect);
    D3D11_VIEWPORT viewport;
    viewport.TopLeftX = 0;
    viewport.TopLeftY = 0;
    viewport.Width = width;
    viewport.Height = height;
    viewport.MinDepth = 0;
    viewport.MaxDepth = 1;
    device_context->RSSetViewports(1, &viewport);
    device_context->PSSetShaderResources(0, 1, &source_resource_texture_view);
    device_context->OMSetRenderTargets(1, &target_resource_texture_view, nullptr);
    device_context->VSSetShader(vs, nullptr, 0);
    device_context->PSSetShader(ps, nullptr, 0);

    // Finally draw:
    device_context->Draw(4, 0);
}

void OnPresent(
    reshade::api::command_queue* queue,
    reshade::api::swapchain* swapchain,
    const reshade::api::rect* source_rect,
    const reshade::api::rect* dest_rect,
    uint32_t dirty_rect_count,
    const reshade::api::rect* dirty_rects) {
    ID3D11Device* native_device = (ID3D11Device*)(queue->get_device()->get_native());
    ID3D11DeviceContext* native_device_context = (ID3D11DeviceContext*)(queue->get_immediate_command_list()->get_native());

#if DEVELOPMENT // Allow to tank performance to test auto rendering resolution scaling
    if (frame_sleep_ms > 0)
        Sleep(frame_sleep_ms);
#endif

    // "POST_PROCESS_SPACE_TYPE" 0 and 2 mean that the final image was stored textures in gamma space,
    // so we need to linearize it for scRGB HDR (linear) output.
    // "GAMMA_CORRECTION_TYPE" 2 is always re-corrected in the final shader.
    // 
    // If there are no shaders being currently replaced in the game (cloned_pipeline_count),
    // we can assume that we either missed replacing some shaders, or that we have unloaded all of our shaders.
    // Both cases need linearization at the end, as the game would be drawing in gamma space (during post process).
    bool shader_defines_need_linearization = GetShaderDefineCompiledNumericalValue(POST_PROCESS_SPACE_TYPE_HASH) != 1;
    bool shader_defines_need_gamma_correction = GetShaderDefineCompiledNumericalValue(POST_PROCESS_SPACE_TYPE_HASH) == 1 && GetShaderDefineCompiledNumericalValue(GAMMA_CORRECTION_TYPE_HASH) >= 2;
    bool display_mode_needs_gamma_correction = cb_luma_frame_settings.DisplayMode == 0; // SDR on SDR Display on scRGB HDR Swapchain needs Gamma 2.2/sRGB mismatch correction
#if DEVELOPMENT
    bool needs_draw_debug_texture = debug_draw_texture.get() != nullptr;
#else
    constexpr bool needs_draw_debug_texture = false;
#endif
    if (shader_defines_need_linearization || shader_defines_need_gamma_correction || display_mode_needs_gamma_correction || cloned_pipeline_count == 0 || needs_draw_debug_texture) {
        const std::shared_lock lock_shader_objects(s_mutex_shader_objects);
        if (copy_vertex_shader && transfer_function_copy_pixel_shader) {
            IDXGISwapChain* native_swapchain = (IDXGISwapChain*)(swapchain->get_native());
            com_ptr<ID3D11Texture2D> back_buffer;
            native_swapchain->GetBuffer(0, IID_PPV_ARGS(&back_buffer)); // DX11 only ever has 1 buffer for games
            assert(back_buffer != nullptr);
        
            D3D11_TEXTURE2D_DESC target_desc;
            back_buffer->GetDesc(&target_desc);
            ASSERT_ONCE((target_desc.BindFlags & D3D11_BIND_RENDER_TARGET) != 0);
            // For now we only support this format, nothing else wouldn't realy make sense
            ASSERT_ONCE(target_desc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT);

            uint32_t custom_const_buffer_data = 0;

#if DEVELOPMENT
            if (debug_draw_texture.get()) {
                D3D11_SHADER_RESOURCE_VIEW_DESC debug_srv_desc;
                debug_srv_desc.Format = debug_draw_texture_format;
                debug_srv_desc.ViewDimension = D3D11_SRV_DIMENSION::D3D11_SRV_DIMENSION_TEXTURE2D;
                debug_srv_desc.Texture2D.MipLevels = 1;
                debug_srv_desc.Texture2D.MostDetailedMip = 0;
                com_ptr<ID3D11ShaderResourceView> debug_srv;
                // We recreate this every frame, it doesn't really matter (and this is allowed to fail in case of quirky formats)
                HRESULT hr = native_device->CreateShaderResourceView(debug_draw_texture.get(), &debug_srv_desc, &debug_srv);
                ASSERT_ONCE(SUCCEEDED(hr));

                ID3D11ShaderResourceView* const debug_srv_const = debug_srv.get();
                native_device_context->PSSetShaderResources(1, 1, &debug_srv_const); // Use index 1 (0 is already used)

                custom_const_buffer_data = debug_draw_options;
            }
            // Empty the shader resource so the shader can tell there isn't one
            else {
                ID3D11ShaderResourceView* const debug_srv_const = nullptr;
                native_device_context->PSSetShaderResources(1, 1, &debug_srv_const);
            }
#endif

            D3D11_TEXTURE2D_DESC proxy_target_desc;
            if (transfer_function_copy_texture.get() != nullptr) {
                transfer_function_copy_texture->GetDesc(&proxy_target_desc);
            }
            if (transfer_function_copy_texture.get() == nullptr || proxy_target_desc.Width != target_desc.Width || proxy_target_desc.Height != target_desc.Height /*|| proxy_target_desc.Format != target_desc.Format*/) {
                proxy_target_desc = target_desc;
                proxy_target_desc.BindFlags |= D3D11_BIND_SHADER_RESOURCE;
                proxy_target_desc.BindFlags &= ~D3D11_BIND_RENDER_TARGET;
                proxy_target_desc.BindFlags &= ~D3D11_BIND_UNORDERED_ACCESS;
                proxy_target_desc.CPUAccessFlags = 0;
                proxy_target_desc.Usage = D3D11_USAGE_DEFAULT;
                transfer_function_copy_texture = nullptr;
                HRESULT hr = native_device->CreateTexture2D(&proxy_target_desc, nullptr, &transfer_function_copy_texture);
                assert(SUCCEEDED(hr));

                D3D11_SHADER_RESOURCE_VIEW_DESC source_srv_desc;
                source_srv_desc.Format = target_desc.Format;
                source_srv_desc.ViewDimension = D3D11_SRV_DIMENSION::D3D11_SRV_DIMENSION_TEXTURE2D;
                source_srv_desc.Texture2D.MipLevels = 1;
                source_srv_desc.Texture2D.MostDetailedMip = 0;
                transfer_function_copy_shader_resource_view = nullptr;
                hr = native_device->CreateShaderResourceView(transfer_function_copy_texture.get(), &source_srv_desc, &transfer_function_copy_shader_resource_view);
                assert(SUCCEEDED(hr));
            }

            // We need to copy the texture to read back from it, even if we only exclusively write to the same pixel we read and thus there couldn't be any race condition. Unfortunately DX works like that.
            native_device_context->CopyResource(transfer_function_copy_texture.get(), back_buffer.get());

            DrawStateStack draw_state_stack;
            draw_state_stack.Cache(native_device_context);

            com_ptr<ID3D11RenderTargetView> target_resource_texture_view;
            // If we already had a render target, we can assume it was already set to the swapchain,
            // but it's good to make sure of it nonetheless.
            if (draw_state_stack.render_target_views[0] != nullptr) {
#if DEVELOPMENT || TEST
                com_ptr<ID3D11Resource> render_target_resource;
                draw_state_stack.render_target_views[0]->GetResource(&render_target_resource);
                assert(render_target_resource.get() == back_buffer.get());
#endif
                target_resource_texture_view = draw_state_stack.render_target_views[0];
            }
            else { // This case doesn't seem to happen (ever?) so we don't bother caching the "ID3D11RenderTargetView"
                D3D11_RENDER_TARGET_VIEW_DESC target_rtv_desc;
                target_rtv_desc.Format = target_desc.Format;
                target_rtv_desc.ViewDimension = D3D11_RTV_DIMENSION::D3D11_RTV_DIMENSION_TEXTURE2D;
                target_rtv_desc.Texture2D.MipSlice = 0;
                HRESULT hr = native_device->CreateRenderTargetView(back_buffer.get(), &target_rtv_desc, &target_resource_texture_view);
                assert(SUCCEEDED(hr));
            }

            // Push our settings cbuffer in case where no other custom shader run this frame
            {
                auto& device_data = queue->get_device()->get_private_data<DeviceData>();
                const std::shared_lock lock(device_data.mutex);
                const auto cb_luma_frame_settings_copy = cb_luma_frame_settings;
                // Force a custom display mode in case we have no game custom shaders loaded, so the custom linearization shader can linearize anyway, independently of "POST_PROCESS_SPACE_TYPE"
                bool force_linearize = cloned_pipeline_count == 0;
                if (force_linearize) {
                    // No need for "s_mutex_reshade" here, given that they are generally only also changed by the user manually changing the settings in ImGUI, which runs at the very end of the frame
                    cb_luma_frame_settings.DisplayMode = -1;
                }
                SetPreyLumaConstantBuffers(queue->get_immediate_command_list(), reshade::api::shader_stage::pixel, device_data.settings_pipeline_layout, LumaConstantBufferType::LumaSettings);
                if (force_linearize) {
                    cb_luma_frame_settings.DisplayMode = cb_luma_frame_settings_copy.DisplayMode;
                }
                if (cloned_pipeline_count == 0 || custom_const_buffer_data != 0) {
                    SetPreyLumaConstantBuffers(queue->get_immediate_command_list(), reshade::api::shader_stage::pixel, device_data.shared_data_pipeline_layout, LumaConstantBufferType::LumaData, custom_const_buffer_data);
                }
            }

            // Note: we don't need to re-apply our custom cbuffers as in Prey, they are on indexes that are never used by the game's code
            DrawCustomPixelShader(native_device_context, default_blend_state.get(), copy_vertex_shader.get(), transfer_function_copy_pixel_shader.get(), transfer_function_copy_shader_resource_view.get(), target_resource_texture_view.get(), target_desc.Width, target_desc.Height, false);

            draw_state_stack.Restore(native_device_context);
        }
        else {
            ASSERT_ONCE(false); // The custom shaders failed to be found (they have either been unloaded or failed to compile, or simply missing in the files)
        }
    }
    else {
        transfer_function_copy_texture = nullptr;
        transfer_function_copy_shader_resource_view = nullptr;
    }

  // Update all variables as this is on the only thing guaranteed to run once per frame:
  ASSERT_ONCE(!has_drawn_main_post_processing || found_per_view_globals); // We failed to find and assign global cbuffer 13 this frame (could it be that the scene is empty if this triggers?)
  ASSERT_ONCE(has_drawn_composed_gbuffers == has_drawn_main_post_processing); // Why is g-buffer composition drawing but post processing isn't?
  //TODOFT3: replace some instances of "has_drawn_main_post_processing" and "has_drawn_main_post_processing_previous" with "has_drawn_composed_gbuffers" (and its previous state)?
  if (has_drawn_main_post_processing) {
      previous_prey_taa_enabled[1] = previous_prey_taa_enabled[0];
      previous_prey_taa_enabled[0] = prey_taa_enabled;
  }
  else {
      previous_prey_taa_enabled[1] = false;
      previous_prey_taa_enabled[0] = false;
      prey_taa_detected = false;
      // Theoretically we turn this flag off one frame late (or well, at the end of the frame),
      // but then again, if no scene rendered, this flag wouldn't have been used for anything.
      cb_luma_frame_settings.DLSS = 0; // No need for "s_mutex_reshade" here, given that they are generally only also changed by the user manually changing the settings in ImGUI, which runs at the very end of the frame
      dlss_suppressed = false;
      // Reset DRS related values if there's a scene cut or loading screen or a menu, we have no way of telling if it's actually still enabled in the user settings.
      // Note that this could cause a micro stutter the next frame we use DLSS as it's gonna have to recreate its internal textures, but we have no way to tell if DRS is still active from the user, so we have to reset the DLSS mode at some point to allow DLAA to run again.
      if (!prey_drs_active) {
        dlss_render_resolution_scale = 1.f;
        prey_drs_detected = false;
      }
      dlss_scene_exposure = 1.f;
      dlss_scene_pre_exposure = 1.f;
  }
  has_drawn_ssao = false;
  has_drawn_ssao_denoise = false;
  has_drawn_ssr = false;
  has_drawn_ssr_blend = false;
  has_drawn_composed_gbuffers = false;
  has_drawn_motion_blur_previous = has_drawn_motion_blur;
  has_drawn_motion_blur = false;
  has_drawn_tonemapping = false;
  has_drawn_main_post_processing_previous = has_drawn_main_post_processing;
  has_drawn_main_post_processing = false;
  has_drawn_upscaling = false;
#if 0 // Moved to "OnReShadePresent()"
  has_drawn_dlss_sr = false;
#endif
#if 1 // Not much need to reset this, but let's do it anyway (e.g. in case the game scene isn't currently rendering)
  prey_drs_active = false;
#endif
  found_per_view_globals = false;
  previous_render_resolution = render_resolution;
  previous_projection_matrix = projection_matrix;
  previous_nearest_projection_matrix = nearest_projection_matrix;
  previous_projection_jitters = projection_jitters;
  cb_per_view_global_previous = cb_per_view_global;
#if DEVELOPMENT
  cb_per_view_globals_last_drawn_shader.emplace_back(last_drawn_shader);
  cb_per_view_globals_last_drawn_shader.clear();
  cb_per_view_globals_previous = cb_per_view_globals;
  cb_per_view_globals.clear();

  if (debug_draw_auto_clear_texture) {
    debug_draw_texture = nullptr;
  }
  debug_draw_pipeline_instance = 0;
#endif // DEVELOPMENT

#if ENABLE_NGX
  // We wouldn't really need to do anything other than clearing "dlss_output_color",
  // but to avoid wasting memory allocated by DLSS texture and other resources,
  // clear it up once disabled.
  if (dlss_sr != NGX::DLSS::HasInit()) {
      if (dlss_sr) {
          com_ptr<IDXGIDevice> native_dxgi_device;
          HRESULT hr = native_device->QueryInterface(&native_dxgi_device);
          com_ptr<IDXGIAdapter> native_adapter;
          if (SUCCEEDED(hr)) {
              hr = native_dxgi_device->GetAdapter(&native_adapter);
          }
          assert(SUCCEEDED(hr));

          dlss_sr = NGX::DLSS::Init(native_device, native_adapter.get()); // No need to update "dlss_sr_supported"
      }
      else {
          dlss_output_color = nullptr;
          dlss_exposure = nullptr;
          dlss_motion_vectors = nullptr;
          dlss_motion_vectors_rtv = nullptr;
          dlss_render_resolution_scale = 1.f; // Reset this to 0 when DLSS is toggled, even if "prey_drs_detected" is still true, we'll set it back to a low value if DRS is used again.
          dlss_scene_exposure = 1.f;
          dlss_scene_pre_exposure = 1.f;
          exposure_buffer_gpu = nullptr;
          if (exposure_buffer_cpu.get()) {
              native_device_context->Unmap(exposure_buffer_cpu.get(), 0);
              exposure_buffer_cpu = nullptr;
          }
          exposure_buffer_rtv = nullptr;
#if !DLSS_KEEP_DLL_LOADED // This will actually unload the DLSS DLL and all, making the game hitch, so it's better to just keep it in memory
          NGX::DLSS::Deinit(native_device);
#endif
      }
  }

  // Update halton sequence with the latest rendering resolution.
  // Theoretically we should do that at the beginning of the rendering pass, after picking the current frame resolution (in DRS, res can change almost every frame),
  // but in reality there's probably little difference. Also, our implementation rounds it to the closest power of 2.
  // This won't do anything (these values are ignored by the game) unless "TAA" or "SMAA 2TX" are active.
#if DEVELOPMENT
  if (force_taa_jitter_phases > 0) {
      NativePlugin::SetHaltonSequencePhases(force_taa_jitter_phases);
  }
  else
#endif
  if (dlss_sr && !dlss_suppressed && prey_taa_detected && cloned_pipeline_count != 0) {
      NativePlugin::SetHaltonSequencePhases(render_resolution.y, output_resolution.y);
  }
  // Restore the default value for the game's native TAA, though instead of going to "16" as "r_AntialiasingTAAPattern" "10" would do, we set the phase to 8, which is actually the game's default for TAA/SMAA 2TX, and more appropriate for its short history (4 works too and looks about the same, maybe better, as it's what SMAA defaulted to in CryEngine)
  else {
      NativePlugin::SetHaltonSequencePhases(8);
  }
#else
  NativePlugin::SetHaltonSequencePhases(8); // We could do this once only on boot but whatever
#endif // NGX

  frame_index++;
}

//TODOFT5: "_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR" as define at the top?
//TODOFT5: fix cpp file formatting in general (remove clang.tidy files?)
//TODOFT5: Add "UpdateSubresource" to check whether they map buffers with that (it's not optimized so probably it's unused by CryEngine)? Also make sure that our CopyTexture() func works!
//TODOFT5: merge all the shader permutations that use the same code (and then move shader binaries to bin folder?)
//TODOFT5: move project files out of the "build" folder? and the "ReShade Addon" folder? Add shader files to VS project?
//TODOFT5: the game broke the exposure state in debug?
//TODOFT (TODO): make sure DLSS lets scRGB colors pass through...
//TODOFT: do one last test on all jitters with DLSS to see if motion vectors are right in motion with DRS (it seems to be?)
//TODOFT: add a new RT to draw UI on top (pre-multiplied alpha everywhere), so we could compose it smartly, possibly in the final linearization pass.

// Return false to prevent the original draw call from running (e.g. if you replaced it or just want to skip it)
// Prey always seemengly draws in direct mode (?), but it uses different command lists on different threads (e.g. seemengly for the shadow projection maps, as they are separate, and stuff like that), though all the primary passes are done on the same thread.
// There's a few compute shaders but most passes are classic pixel shaders.
// If we ever wanted to still run the game's original draw call (first) and then ours (second), we'd need to pass more arguments in this function (to replicate the draw call identically).
bool HandlePreDraw(reshade::api::command_list* cmd_list, bool is_dispatch /*= false*/, ShaderHashesList& original_shader_hashes) {
  const auto* device = cmd_list->get_device();
  auto device_api = device->get_api();
  ID3D11Device* native_device = (ID3D11Device*)(device->get_native());
  ID3D11DeviceContext* native_device_context = (ID3D11DeviceContext*)(cmd_list->get_native());
  auto& device_data = device->get_private_data<DeviceData>();

#if DEVELOPMENT
  if (trace_running) {
      const std::unique_lock lock_trace(s_mutex_trace);
      // Hack to find the last matching index of "trace_pipeline_draws" given that there's different command lists doing draw calls in multiple threads cuncurrently
      for (int i = trace_threads.size() - 1; i >= 0; i--) {
          if (trace_threads[i] == std::this_thread::get_id()) {
              trace_pipeline_draws[i]++;
              break;
          }
      }
  }
#endif

  reshade::api::shader_stage stages = reshade::api::shader_stage::all_graphics | reshade::api::shader_stage::all_compute;

  std::unordered_set<uint64_t> back_buffers;

  reshade::api::pipeline_layout settings_pipeline_layout(0);
  reshade::api::pipeline_layout shared_data_pipeline_layout(0);
  reshade::api::pipeline_layout ui_pipeline_layout(0);

  // Lock for the shortest amount possible
  {
    const std::shared_lock lock(device_data.mutex);
    settings_pipeline_layout = device_data.settings_pipeline_layout;
    ui_pipeline_layout = device_data.ui_pipeline_layout;
    shared_data_pipeline_layout = device_data.shared_data_pipeline_layout;
    back_buffers = device_data.back_buffers;
  }

  bool is_custom_pass = false;

  auto& cmd_list_data = cmd_list->get_private_data<CommandListData>();

#if DEVELOPMENT
  last_drawn_shader = "";
#endif //DEVELOPMENT
  // We check the last shader pointers ("pipeline_state_original_compute_shader") we had cached in the pipeline set state functions.
  // Alternatively we could check "PSGetShader()" against "pipeline_cache_by_pipeline_clone_handle" but that'd probably have uglier and slower code.
  if (is_dispatch) {
      if (cmd_list_data.pipeline_state_original_compute_shader.handle != 0)
      {
          const auto pipeline_pair = pipeline_cache_by_pipeline_handle.find(cmd_list_data.pipeline_state_original_compute_shader.handle);
          if (pipeline_pair != pipeline_cache_by_pipeline_handle.end() && pipeline_pair->second != nullptr) {
              original_shader_hashes.compute_shaders = std::unordered_set<uint32_t>(pipeline_pair->second->shader_hashes.begin(), pipeline_pair->second->shader_hashes.end());
#if DEVELOPMENT
              last_drawn_shader = original_shader_hashes.compute_shaders.empty() ? "" : std::format("{:x}", *original_shader_hashes.compute_shaders.begin()); // String hash to int
#endif //DEVELOPMENT
              is_custom_pass = pipeline_pair->second->cloned;
              stages = reshade::api::shader_stage::compute;
          }
      }
  }
  else {
      if (cmd_list_data.pipeline_state_original_vertex_shader.handle != 0) {
          const auto pipeline_pair = pipeline_cache_by_pipeline_handle.find(cmd_list_data.pipeline_state_original_vertex_shader.handle);
          if (pipeline_pair != pipeline_cache_by_pipeline_handle.end() && pipeline_pair->second != nullptr) {
              original_shader_hashes.vertex_shaders = std::unordered_set<uint32_t>(pipeline_pair->second->shader_hashes.begin(), pipeline_pair->second->shader_hashes.end());
              is_custom_pass = pipeline_pair->second->cloned;
              stages = reshade::api::shader_stage::vertex;
          }
      }

      if (cmd_list_data.pipeline_state_original_pixel_shader.handle != 0) {
          const auto pipeline_pair = pipeline_cache_by_pipeline_handle.find(cmd_list_data.pipeline_state_original_pixel_shader.handle);
          if (pipeline_pair != pipeline_cache_by_pipeline_handle.end() && pipeline_pair->second != nullptr) {
              original_shader_hashes.pixel_shaders = std::unordered_set<uint32_t>(pipeline_pair->second->shader_hashes.begin(), pipeline_pair->second->shader_hashes.end());
#if DEVELOPMENT
              last_drawn_shader = original_shader_hashes.pixel_shaders.empty() ? "" : std::format("{:x}", *original_shader_hashes.pixel_shaders.begin()); // String hash to int
#endif //DEVELOPMENT
              is_custom_pass |= pipeline_pair->second->cloned;
              stages |= reshade::api::shader_stage::pixel;
          }
      }
  }

//TODOFT4: it seems like this assert (or something like this) can happen when DRS changes res to quick or is toggled between frames.
//Somehow the wrong rend res can persist in our data from a global cbuffer 13 set that wasn't meant for the main scene rendering but some side rendering with a separate res.
//Could it be that we get a swapchain resize event too late and thus our output resolution isn't updated quick enough? (not it can't be because the output res is ... unchanged with DRS).
//We should probably find a moment where we absolutely stop taking in new cbuffer 13 values, one fixed point in the pipeline (e.g. blur?, AO?, scene composion? ...).
#if DEVELOPMENT && 0 // We are setting the viewport below now, no need to verify it was already right
  // We exclude a couple of shaders from "shader_hashes_LensOptics" from this check as we know they run at 1/4 resolution
  if (has_drawn_dlss_sr && !has_drawn_upscaling && !original_shader_hashes.Contains(std::stoul("4435D741", nullptr, 16), reshade::api::shader_stage::pixel) && !original_shader_hashes.Contains(std::stoul("C54F3986", nullptr, 16), reshade::api::shader_stage::pixel)) {
      D3D11_VIEWPORT viewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
      UINT viewports_num = 1;
      native_device_context->RSGetViewports(&viewports_num, nullptr);
      ASSERT_ONCE(viewports_num == 1); // Possibly innocuous as long as it's > 0
      native_device_context->RSGetViewports(&viewports_num, &viewports[0]);
      ASSERT_ONCE(viewports[0].TopLeftX == 0 && viewports[0].TopLeftY == 0
          && ((std::lrintf(viewports[0].Width) == std::lrintf(render_resolution.x) && std::lrintf(viewports[0].Height) == std::lrintf(render_resolution.y))
              || (std::lrintf(viewports[0].Width) == std::lrintf(output_resolution.x) && std::lrintf(viewports[0].Height) == std::lrintf(output_resolution.y))));
  }
#endif // DEVELOPMENT

  if (!original_shader_hashes.Empty()) {
      //TODOFT5: optimize these shader searches by simply marking "CachedPipeline" with a tag on what they are (and whether they have a particular role) (also we can restrict the search to pixel shaders) upfront
      if (!has_drawn_composed_gbuffers && original_shader_hashes.Contains(shader_hashes_TiledShadingTiledDeferredShading)) {
          has_drawn_composed_gbuffers = true;
      }
      if (!has_drawn_ssr && original_shader_hashes.Contains(shader_hash_DeferredShadingSSRRaytrace, reshade::api::shader_stage::pixel)) {
          has_drawn_ssr = true;
          // There's no need to ever skip this added render target, the performance cost is tiny
          if (is_custom_pass) {
              uint2 ssr_diffuse_target_resolution = { (UINT)output_resolution.x, (UINT)output_resolution.y };

              ID3D11RenderTargetView* rtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT];
              ID3D11DepthStencilView* dsvs;
              native_device_context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, &rtvs[0], &dsvs);

              DXGI_FORMAT new_ssr_texture_format = DXGI_FORMAT_UNKNOWN;

              // See the same code for SSDO (GTAO), the render target resolution is handled in a similar way, based on "r_arkssr" and "r_SSReflHalfRes", but this one can actually draw to a lower (halved) resolution render target (when selecting the half res SSR quality from the menu)
              if (rtvs[0]) {
                  com_ptr<ID3D11Resource> render_target_resource;
                  rtvs[0]->GetResource(&render_target_resource);
                  if (render_target_resource) {
                      ssr_texture = nullptr;
                      render_target_resource->QueryInterface(&ssr_texture);
                      if (ssr_texture) {
                          D3D11_TEXTURE2D_DESC render_target_texture_2d_desc;
                          ssr_texture->GetDesc(&render_target_texture_2d_desc);
                          ssr_diffuse_target_resolution.x = render_target_texture_2d_desc.Width;
                          ssr_diffuse_target_resolution.y = render_target_texture_2d_desc.Height;
                          new_ssr_texture_format = render_target_texture_2d_desc.Format;
                      }
                  }
              }
              if (!ssr_diffuse_texture.get() || ssr_diffuse_texture_width != ssr_diffuse_target_resolution.x || ssr_diffuse_texture_height != ssr_diffuse_target_resolution.y || ssr_texture_format != new_ssr_texture_format) {
                  ssr_diffuse_texture_width = ssr_diffuse_target_resolution.x;
                  ssr_diffuse_texture_height = ssr_diffuse_target_resolution.y;

                  D3D11_TEXTURE2D_DESC texture_desc;
                  texture_desc.Width = ssr_diffuse_texture_width;
                  texture_desc.Height = ssr_diffuse_texture_height;
                  texture_desc.MipLevels = 1;
                  texture_desc.ArraySize = 1;
                  texture_desc.Format = DXGI_FORMAT::DXGI_FORMAT_R8_UNORM;
                  texture_desc.SampleDesc.Count = 1;
                  texture_desc.SampleDesc.Quality = 0;
                  texture_desc.Usage = D3D11_USAGE_DEFAULT;
                  texture_desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
                  texture_desc.CPUAccessFlags = 0;
                  texture_desc.MiscFlags = 0;

                  ssr_diffuse_texture = nullptr;
                  HRESULT hr = native_device->CreateTexture2D(&texture_desc, nullptr, &ssr_diffuse_texture);
                  assert(SUCCEEDED(hr));

                  D3D11_RENDER_TARGET_VIEW_DESC rtv_desc;
                  rtv_desc.Format = texture_desc.Format;
                  rtv_desc.ViewDimension = D3D11_RTV_DIMENSION::D3D11_RTV_DIMENSION_TEXTURE2D;
                  rtv_desc.Texture2D.MipSlice = 0;

                  ssr_diffuse_rtv = nullptr;
                  hr = native_device->CreateRenderTargetView(ssr_diffuse_texture.get(), &rtv_desc, &ssr_diffuse_rtv);
                  assert(SUCCEEDED(hr));

                  D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc;
                  srv_desc.Format = texture_desc.Format;
                  srv_desc.ViewDimension = D3D11_SRV_DIMENSION::D3D11_SRV_DIMENSION_TEXTURE2D;
                  srv_desc.Texture2D.MipLevels = 1;
                  srv_desc.Texture2D.MostDetailedMip = 0;

                  ssr_diffuse_srv = nullptr;
                  hr = native_device->CreateShaderResourceView(ssr_diffuse_texture.get(), &srv_desc, &ssr_diffuse_srv);
                  assert(SUCCEEDED(hr));

                  if (ssr_texture) {
                      srv_desc.Format = new_ssr_texture_format;

                      ssr_texture_format = new_ssr_texture_format;
                      ssr_srv = nullptr;
                      // Cache the main (first) SSR texture for later retrieval in the SSR blend shader, given it only had access to mip mapped versions of it
                      hr = native_device->CreateShaderResourceView(ssr_texture.get(), &srv_desc, &ssr_srv);
                      assert(SUCCEEDED(hr));
                  }
              }

              // Add a second render target to store how "diffuse" reflections need to be, based on the ray travel distance from the relfection point (and the specularity etc).
              // We need to cache and restore all the RTs as the game uses a push and pop mechanism that tracks them closely, so any changes in state can break them.
              ID3D11RenderTargetView* rtv1 = rtvs[1];
              rtvs[1] = ssr_diffuse_rtv.get();
              native_device_context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, &rtvs[0], dsvs);

#if DEVELOPMENT // Currently we'd only ever need these in development modes to make tweaks, or for in development code paths that are still disabled
              SetPreyLumaConstantBuffers(cmd_list, stages, settings_pipeline_layout, LumaConstantBufferType::LumaSettings);
              SetPreyLumaConstantBuffers(cmd_list, stages, shared_data_pipeline_layout, LumaConstantBufferType::LumaData);
#endif

              native_device_context->Draw(3, 0);

              rtvs[1] = rtv1;
              native_device_context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, &rtvs[0], dsvs);
              for (UINT i = 0; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; i++) {
                  if (rtvs[i] != nullptr) {
                      rtvs[i]->Release();
                      rtvs[i] = nullptr;
                  }
              }

              ASSERT_ONCE(ssr_command_list == nullptr);
              ssr_command_list = native_device_context; // To make sure we only fix mip map draw calls from the same command list (more can run at the same time in different threads)

              return true;
          }
          else {
              ssr_texture = nullptr;
              ssr_srv = nullptr;
              ssr_diffuse_texture = nullptr; // We can leave "ssr_diffuse_texture_width" and "ssr_diffuse_texture_height" as they were
              ssr_diffuse_rtv = nullptr;
              ssr_diffuse_srv = nullptr;
          }
      }
      if (has_drawn_ssr && !has_drawn_ssr_blend && native_device_context == ssr_command_list && is_custom_pass && (original_shader_hashes.Contains(shader_hash_PostEffectsGaussBlurBilinear, reshade::api::shader_stage::pixel) || original_shader_hashes.Contains(shader_hash_PostEffectsTextureToTextureResampled, reshade::api::shader_stage::pixel))) {
          uint32_t custom_data = 1; // This value will make the SSR mip map generation and blurring shaders take choices specifically designed for SSR
          SetPreyLumaConstantBuffers(cmd_list, stages, shared_data_pipeline_layout, LumaConstantBufferType::LumaData, custom_data);
          return false;
      }
      if (has_drawn_ssr && !has_drawn_ssr_blend && original_shader_hashes.Contains(shader_hash_DeferredShadingSSReflectionComp, reshade::api::shader_stage::pixel)) {
          has_drawn_ssr_blend = true;
          ssr_command_list = nullptr;
          if (ssr_srv.get() || ssr_diffuse_srv.get()) {
              ID3D11ShaderResourceView* const shader_resource_views_const[2] = { ssr_srv.get(), ssr_diffuse_srv.get() };
              native_device_context->PSSetShaderResources(5, 2, &shader_resource_views_const[0]);
          }
          return false; // Return as we don't need any of Luma's cbuffers
      }
      if (!has_drawn_tonemapping && original_shader_hashes.Contains(shader_hashes_HDRPostProcessHDRFinalScene)) {
          has_drawn_tonemapping = true;

          // Update the DLSS pre-exposure to take the opposite value of our exposure (basically our brightness) to avoid DLSS causing additional lag when the exposure changes.
          // This way, DLSS will divide the linear buffer by this value, which would have previously been multiplied in given that TAA runs after the scene exposure is factored in (even in HDR, and it shouldn't! But moving it is too hard).
          // For this particular case, we don't use the native DLSS exposure texture, but we rely on pre-exposure itself, as it has a different temporal behaviour,
          // if we changed the DLSS exposure texture every frame to follow the scene exposure, DLSS would act weird (mostly likely just ignore it, as it uses that as a hint of the exposure the tonemapper would use after TAA),
          // while with pre-exposure it works as expected (except it kinda lags behind a bit, because it doesn't store a pre-exposure value attached to every frame, and simply uses the last provided one).
          if (dlss_sr && !dlss_suppressed && prey_taa_detected && cloned_pipeline_count != 0 && draw_exposure_pixel_shader) {
              static D3D11_MAPPED_SUBRESOURCE mapped_exposure;

              // Create pre-exposure buffers once
              if (!exposure_buffer_gpu.get()) {
                  D3D11_BUFFER_DESC exposure_buffer_desc;
                  exposure_buffer_desc.ByteWidth = 4; // 1x float32
                  exposure_buffer_desc.Usage = D3D11_USAGE_DEFAULT;
                  exposure_buffer_desc.BindFlags = D3D11_BIND_RENDER_TARGET;
                  exposure_buffer_desc.CPUAccessFlags = 0;
                  exposure_buffer_desc.MiscFlags = 0;
                  exposure_buffer_desc.StructureByteStride = sizeof(float);

                  D3D11_SUBRESOURCE_DATA exposure_buffer_data;
                  exposure_buffer_data.pSysMem = &dlss_scene_pre_exposure; // This needs to be "static" data in case the texture initialization was somehow delayed and read the data after the stack destroyed it
                  exposure_buffer_data.SysMemPitch = 0;
                  exposure_buffer_data.SysMemSlicePitch = 0;

                  exposure_buffer_gpu = nullptr;
                  HRESULT hr = native_device->CreateBuffer(&exposure_buffer_desc, &exposure_buffer_data, &exposure_buffer_gpu);
                  ASSERT_ONCE(SUCCEEDED(hr));

                  exposure_buffer_desc.Usage = D3D11_USAGE_STAGING;
                  exposure_buffer_desc.BindFlags = 0;
                  exposure_buffer_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

                  if (exposure_buffer_cpu.get()) {
                      native_device_context->Unmap(exposure_buffer_cpu.get(), 0);
                      exposure_buffer_cpu = nullptr;
                  }
                  hr = native_device->CreateBuffer(&exposure_buffer_desc, &exposure_buffer_data, &exposure_buffer_cpu);
                  ASSERT_ONCE(SUCCEEDED(hr));

                  // Keep this mapped permanently to avoid butchering the frame rate (shader memory writes will directly go into our mapped data)
                  mapped_exposure.pData = nullptr;
                  hr = native_device_context->Map(exposure_buffer_cpu.get(), 0, D3D11_MAP_READ, 0, &mapped_exposure);
                  ASSERT_ONCE(SUCCEEDED(hr));

                  D3D11_RENDER_TARGET_VIEW_DESC exposure_buffer_rtv_desc;
                  exposure_buffer_rtv_desc.Format = DXGI_FORMAT::DXGI_FORMAT_R32_FLOAT;
                  exposure_buffer_rtv_desc.ViewDimension = D3D11_RTV_DIMENSION::D3D11_RTV_DIMENSION_BUFFER;
                  exposure_buffer_rtv_desc.Buffer.FirstElement = 0;
                  exposure_buffer_rtv_desc.Buffer.NumElements = 1;

                  exposure_buffer_rtv = nullptr;
                  hr = native_device->CreateRenderTargetView(exposure_buffer_gpu.get(), &exposure_buffer_rtv_desc, &exposure_buffer_rtv);
                  ASSERT_ONCE(SUCCEEDED(hr));
              }

              // Cache original state
              com_ptr<ID3D11RenderTargetView> rtv;
              native_device_context->OMGetRenderTargets(1, &rtv, nullptr);
              com_ptr<ID3D11PixelShader> ps;
              native_device_context->PSGetShader(&ps, nullptr, 0);

              bool has_sunshafts = original_shader_hashes.Contains(shader_hashes_HDRPostProcessHDRFinalScene_Sunshafts); // These shaders use a different cbuffer layout
              SetPreyLumaConstantBuffers(cmd_list, stages, shared_data_pipeline_layout, LumaConstantBufferType::LumaData, has_sunshafts);

              // Draw the exposure
              native_device_context->PSSetShader(draw_exposure_pixel_shader.get(), nullptr, 0);
              ID3D11RenderTargetView* render_target_view_const = exposure_buffer_rtv.get();
              native_device_context->OMSetRenderTargets(1, &render_target_view_const, nullptr);
              native_device_context->Draw(3, 0);

              // Copy it back as CPU buffer and read+store it
              native_device_context->CopyResource(exposure_buffer_cpu.get(), exposure_buffer_gpu.get());
              float scene_exposure = 1.f;
              // Note: this is possibly some frames behind, but has no performance hit and it's fine as it is for the use we make of it
              if (mapped_exposure.pData != nullptr) {
                  // Depending on "DLSS_RELATIVE_PRE_EXPOSURE" this is either the relative exposure (compared to the average expected exposure value) or raw final exposure
                  scene_exposure = *((float*)mapped_exposure.pData);
                  if (std::isinf(scene_exposure) || std::isnan(scene_exposure) || scene_exposure <= 0.f) {
                      scene_exposure = 1.f;
                  }
              }
              dlss_scene_pre_exposure = scene_exposure;
#if DEVELOPMENT || TEST
              bool dlss_relative_pre_exposure = GetShaderDefineCompiledNumericalValue(DLSS_RELATIVE_PRE_EXPOSURE_HASH) >= 1;
#else
              bool dlss_relative_pre_exposure;
              {
                  const std::shared_lock lock(s_mutex_shader_defines);
                  dlss_relative_pre_exposure = code_shaders_defines.contains("DLSS_RELATIVE_PRE_EXPOSURE") && code_shaders_defines["DLSS_RELATIVE_PRE_EXPOSURE"] >= 1;
              }
#endif
              if (dlss_relative_pre_exposure) {
                  // With this design, the pre-exposure is set to the relative exposure and the exposure texture is set to 1 (see the shader for more).
                  dlss_scene_exposure = 1.f;
              }
              else {
                  // With this design, we set the DLSS pre-exposure and exposure to the same value, so, given that the exposure was already multiplied in despite it shouldn't have it been so,
                  // DLSS will divide out the exposure through the pre-exposure parameter and then re-acknowledge it through the exposure texture, basically making DLSS act as if it was done before exposure/tonemapping.
                  dlss_scene_exposure = scene_exposure;
              }

              // Restore original state
              native_device_context->PSSetShader(ps.get(), nullptr, 0);
              render_target_view_const = rtv.get();
              native_device_context->OMSetRenderTargets(1, &render_target_view_const, nullptr);
          }
      }
      // Note: this doesn't always run, it's based on a user setting!
      if (!has_drawn_motion_blur && original_shader_hashes.Contains(shader_hashes_MotionBlur)) {
          has_drawn_motion_blur = true;
      }
      if (!has_drawn_ssao && original_shader_hashes.Contains(shader_hashes_DirOccPass)) {
            has_drawn_ssao = true;
            if (is_custom_pass && GetShaderDefineCompiledNumericalValue(SSAO_TYPE_HASH) >= 1) { // If using GTAO
                uint2 gtao_edges_target_resolution = { (UINT)output_resolution.x, (UINT)output_resolution.y };

                ID3D11RenderTargetView* rtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT];
                ID3D11DepthStencilView* dsvs;
                native_device_context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, &rtvs[0], &dsvs);

// This is an optional extra check we can make to properly determine the resolution of our edges texture.
// Unless "r_ssdoHalfRes" has a value of 3, then the RT would always have the same resolution as the final swapchain output.
// Unfortunately this texture is sampled with a bilinear sampler by 0-1 UV coordinates, and also because DX11 doesn't allow two render targets to have a different resolution,
// we'd either need to make this a UAV RW texture or to make sure it matches the original RT in resolution.
// Given that that cvar isn't exposed to the official game settings and doesn't seem to be enableable even through config, this is disabled to save performance.
#if DEVELOPMENT
                if (rtvs[0]) {
                    com_ptr<ID3D11Resource> render_target_resource;
                    rtvs[0]->GetResource(&render_target_resource);
                    if (render_target_resource) {
                        com_ptr<ID3D11Texture2D> render_target_texture_2d;
                        render_target_resource->QueryInterface(&render_target_texture_2d);
                        if (render_target_texture_2d) {
                            D3D11_TEXTURE2D_DESC render_target_texture_2d_desc;
                            render_target_texture_2d->GetDesc(&render_target_texture_2d_desc);
                            ASSERT_ONCE(gtao_edges_target_resolution.x == render_target_texture_2d_desc.Width && gtao_edges_target_resolution.y == render_target_texture_2d_desc.Height);
                            gtao_edges_target_resolution.x = render_target_texture_2d_desc.Width;
                            gtao_edges_target_resolution.y = render_target_texture_2d_desc.Height;
                        }
                    }
                }
#endif
                if (!gtao_edges_texture.get() || gtao_edges_texture_width != gtao_edges_target_resolution.x || gtao_edges_texture_height != gtao_edges_target_resolution.y) {
                    gtao_edges_texture_width = gtao_edges_target_resolution.x;
                    gtao_edges_texture_height = gtao_edges_target_resolution.y;

                    D3D11_TEXTURE2D_DESC texture_desc;
                    texture_desc.Width = gtao_edges_texture_width;
                    texture_desc.Height = gtao_edges_texture_height;
                    texture_desc.MipLevels = 1;
                    texture_desc.ArraySize = 1;
                    texture_desc.Format = DXGI_FORMAT::DXGI_FORMAT_R8_UNORM; // The texture is encoded to this format
                    texture_desc.SampleDesc.Count = 1;
                    texture_desc.SampleDesc.Quality = 0;
                    texture_desc.Usage = D3D11_USAGE_DEFAULT;
                    texture_desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
                    texture_desc.CPUAccessFlags = 0;
                    texture_desc.MiscFlags = 0;

                    gtao_edges_texture = nullptr;
                    HRESULT hr = native_device->CreateTexture2D(&texture_desc, nullptr, &gtao_edges_texture);
                    assert(SUCCEEDED(hr));

                    D3D11_RENDER_TARGET_VIEW_DESC rtv_desc;
                    rtv_desc.Format = texture_desc.Format;
                    rtv_desc.ViewDimension = D3D11_RTV_DIMENSION::D3D11_RTV_DIMENSION_TEXTURE2D;
                    rtv_desc.Texture2D.MipSlice = 0;

                    gtao_edges_rtv = nullptr;
                    hr = native_device->CreateRenderTargetView(gtao_edges_texture.get(), &rtv_desc, &gtao_edges_rtv);
                    assert(SUCCEEDED(hr));

                    D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc;
                    srv_desc.Format = texture_desc.Format;
                    srv_desc.ViewDimension = D3D11_SRV_DIMENSION::D3D11_SRV_DIMENSION_TEXTURE2D;
                    srv_desc.Texture2D.MipLevels = 1;
                    srv_desc.Texture2D.MostDetailedMip = 0;

                    gtao_edges_srv = nullptr;
                    hr = native_device->CreateShaderResourceView(gtao_edges_texture.get(), &srv_desc, &gtao_edges_srv);
                    assert(SUCCEEDED(hr));
                }

                // Add a second render target (the depth edges) as it's needed by GTAO.
                // We need to cache and restore all the RTs as the game uses a push and pop mechanism that tracks them closely, so any changes in state can break them.
                ID3D11RenderTargetView* rtv1 = rtvs[1];
                rtvs[1] = gtao_edges_rtv.get();
                native_device_context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, &rtvs[0], dsvs);

                SetPreyLumaConstantBuffers(cmd_list, stages, settings_pipeline_layout, LumaConstantBufferType::LumaSettings);
                SetPreyLumaConstantBuffers(cmd_list, stages, shared_data_pipeline_layout, LumaConstantBufferType::LumaData);
                native_device_context->Draw(3, 0);

                rtvs[1] = rtv1;
                native_device_context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, &rtvs[0], dsvs);
                for (UINT i = 0; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; i++) {
                    if (rtvs[i] != nullptr) {
                        rtvs[i]->Release();
                        rtvs[i] = nullptr;
                    }
                }
                return true;
            }
            else {
                gtao_edges_texture = nullptr; // We can leave "gtao_edges_texture_width" and "gtao_edges_texture_height" as they were
                gtao_edges_rtv = nullptr;
                gtao_edges_srv = nullptr;
            }
      }
      if (has_drawn_ssao && !has_drawn_ssao_denoise && original_shader_hashes.Contains(shader_hashes_SSDO_Blur)) {
          has_drawn_ssao_denoise = true;
          if (gtao_edges_srv.get()) {
              ID3D11ShaderResourceView* const shader_resource_view_const = gtao_edges_srv.get();
              native_device_context->PSSetShaderResources(3, 1, &shader_resource_view_const);
          }
      }
      if (!has_drawn_main_post_processing && original_shader_hashes.Contains(shader_hashes_PostAAComposites)) {
          // This is the last known pass that is guaranteed to run before UI draws in
          has_drawn_main_post_processing = true;
          // If DRS is not currently running, upscaling won't happen, pretend it did
          if (!prey_drs_active) {
              has_drawn_upscaling = true;
          }
      }
      // If DLSS is guaranteed to be running instead of SMAA 2TX, we can skip the edge detection passes of SMAA 2TX (these also run other SMAA modes but then DLSS wouldn't run with these).
      // This check might engage one frame late after DLSS engages but it doesn't matter.
      // This is particularly useful because on every boot the game rejects the TAA user config setting (seemengly due to "r_AntialiasingMode" being clamped to 3 (SMAA 2TX)), so we'd waste performance if we didn't skip the passes (we still do).
      if (!has_drawn_main_post_processing && original_shader_hashes.Contains(shader_hashes_SMAA_EdgeDetection) && dlss_sr && !dlss_suppressed && prey_taa_detected && cloned_pipeline_count != 0) {
          return true;
      }
      if (!has_drawn_upscaling) {
           if (original_shader_hashes.Contains(shader_hash_PostAAUpscaleImage, reshade::api::shader_stage::pixel)) {
               has_drawn_upscaling = true;
               assert(has_drawn_main_post_processing && prey_drs_active);
           }

           // Between DLSS SR and upscaling, force the viewport to the full render target resolution at all times, because we upscaled early.
           // Usually this matches the swapchain output resolution, but some lens optics passes actually draw on textures with a different resolution (independently of the game render/output res).
           if (has_drawn_dlss_sr && !has_drawn_upscaling && prey_drs_active) {
               com_ptr<ID3D11RenderTargetView> render_target_view;
               native_device_context->OMGetRenderTargets(1, &render_target_view, nullptr);

#if DEVELOPMENT
               D3D11_RENDER_TARGET_VIEW_DESC render_target_view_desc;
               render_target_view->GetDesc(&render_target_view_desc);
               ASSERT_ONCE(render_target_view_desc.ViewDimension == D3D11_RTV_DIMENSION::D3D11_RTV_DIMENSION_TEXTURE2D); // This should always be the case
#endif // DEVELOPMENT

               com_ptr<ID3D11Resource> render_target_resource;
               render_target_view->GetResource(&render_target_resource);
               com_ptr<ID3D11Texture2D> render_target_texture_2d;
               HRESULT hr = render_target_resource->QueryInterface(&render_target_texture_2d);
               ASSERT_ONCE(SUCCEEDED(hr));
               D3D11_TEXTURE2D_DESC render_target_texture_2d_desc;
               render_target_texture_2d->GetDesc(&render_target_texture_2d_desc);

#if DEVELOPMENT
               // Scissors are often set after viewports in CryEngine, so check them separately.
               // We need to make sure that all the draw calls after DLSS upscaling run at full resolution and not rendering resolution.
               com_ptr<ID3D11RasterizerState> state;
               native_device_context->RSGetState(&state);
               if (state.get()) {
                   D3D11_RASTERIZER_DESC state_desc;
                   state->GetDesc(&state_desc);
                   if (state_desc.ScissorEnable) {
                       D3D11_RECT scissor_rects[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
                       UINT scissor_rects_num = 1;
                       // This will get the number of scissor rects used
                       native_device_context->RSGetScissorRects(&scissor_rects_num, nullptr);
                       ASSERT_ONCE(scissor_rects_num == 1); // Possibly innocuous as long as it's > 0, but we should only ever have one viewport and one RT!
                       native_device_context->RSGetScissorRects(&scissor_rects_num, &scissor_rects[0]);

                       // If this ever triggered, we'd need to replace scissors too after DLSS (and make them full resolution).
                       ASSERT_ONCE(scissor_rects[0].left == 0 && scissor_rects[0].top == 0 && scissor_rects[0].right == render_target_texture_2d_desc.Width && scissor_rects[0].bottom == render_target_texture_2d_desc.Height);
                   }
               }
#endif // DEVELOPMENT

               D3D11_VIEWPORT viewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
               UINT viewports_num = 1;
               native_device_context->RSGetViewports(&viewports_num, nullptr);
               ASSERT_ONCE(viewports_num == 1); // Possibly innocuous as long as it's > 0, but we should only ever have one viewport and one RT!
               native_device_context->RSGetViewports(&viewports_num, &viewports[0]);
               for (uint32_t i = 0; i < viewports_num; i++) {
                   viewports[i].Width = render_target_texture_2d_desc.Width;
                   viewports[i].Height = render_target_texture_2d_desc.Height;
               }
               native_device_context->RSSetViewports(viewports_num, &viewports[0]);
           }
      }

#if ENABLE_NGX
      // Don't even try to run DLSS if we have no custom shaders loaded, we need them for DLSS to work properly (it might somewhat work even without them, but it's untested and unneeded)
      if (is_custom_pass && dlss_sr && !dlss_suppressed && cloned_pipeline_count != 0) {
          // TODO: add DLSS transparency mask (e.g. glass, decals, emissive) by caching the g-buffers before and after transparent stuff draws near the end?
          // TODO: add DLSS bias mask (to ignore animated textures) by marking up some shaders(materials)/textures hashes with it?
          // TODO: move DLSS before tonemapping, depth of field, bloom and blur. It wouldn't be easy because exposure is calculated after blur in CryEngine,
          // but we could simply fall back on using DLSS Auto Exposure (even if that wouldn't match the actual value used by post processing...).
          // To achieve that, we need to add both DRS+DLSS scaling support to all shaders that run after DLSS, as DLSS would upscale the image before the final upscale pass (and native TAA would be skipped).
          // Sun shafts and lens optics effects would (actually, could) draw in native resolution after upscaling then.
          // Overall that solution has no downsides other than the difficulty of running multiple passes at a different resolution (which really isn't hard as we already have a set up for it).
          // TODO: increase the number of Halton sequence phases when there's no camera rotation happening, in movement it can benefit from being lower, but when steady (or rotating the camera only, which conserves most of the TAA history),
          // a higher phase count can drastically improve the quality.
          
          // We do DLSS after some post processing (e.g. exposure, tonemap, color grading, bloom, blur, objects highlight, sun shafts, other possible AA forms, etc) because running it before post processing
          // would be harder (we'd need to collect more textures manually and manually skip all later AA steps), most importantly, that wouldn't work with the native dynamic resolution the game supports (without changing every single
          // texture sample coordinates in post processing). Even if it's after pp, it should still have enough quality.
          // We replace the "TAA"/"SMAA 2TX" pass (whichever of the ones in our supported passes list is run), ignoring whatever it would have done (the secondary texture it allocated is kept alive, even if we don't use it, we couldn't really destroy it from ReShade),
          // after there's a "composition" pass (film grain, sharpening, ...) and then an optional upscale pass, both of these are too late for DLSS to run.
          if (original_shader_hashes.Contains(shader_hashes_PostAA)) {
                  ASSERT_ONCE(prey_taa_detected); // Why did we get here without TAA enabled?
                  com_ptr<ID3D11ShaderResourceView> ps_shader_resources[17];
                  // 0 current color source
                  // 1 previous color source (post TAA)
                  // 2 depth (0-1 being camera origin - far)
                  // 3 motion vectors (dynamic objects movement only, no camera movement (if not baked in the dynamic objects))
                  // 16 device depth (inverted depth, used by stencil)
                  native_device_context->PSGetShaderResources(0, ARRAYSIZE(ps_shader_resources), reinterpret_cast<ID3D11ShaderResourceView**>(ps_shader_resources));

                  ID3D11RenderTargetView* render_target_views[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT];
                  com_ptr<ID3D11DepthStencilView> depth_stencil_view;
                  native_device_context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, &render_target_views[0], &depth_stencil_view);

                  const bool dlss_inputs_valid = ps_shader_resources[0].get() != nullptr && ps_shader_resources[16].get() != nullptr && ps_shader_resources[3].get() != nullptr && render_target_views[0] != nullptr;
                  ASSERT_ONCE(dlss_inputs_valid);
                  if (dlss_inputs_valid) {
                      com_ptr<ID3D11Resource> output_colorTemp;
                      render_target_views[0]->GetResource(&output_colorTemp);
                      com_ptr<ID3D11Texture2D> output_color;
                      HRESULT hr = output_colorTemp->QueryInterface(&output_color);
                      ASSERT_ONCE(SUCCEEDED(hr));

                      D3D11_TEXTURE2D_DESC output_texture_desc;
                      output_color->GetDesc(&output_texture_desc);
                                            
                      ASSERT_ONCE(std::lrintf(output_resolution.x) == output_texture_desc.Width && std::lrintf(output_resolution.y) == output_texture_desc.Height);
                      std::array<uint32_t, 2> dlss_render_resolution = FindClosestIntegerResolutionForAspectRatio((double)output_texture_desc.Width * (double)dlss_render_resolution_scale, (double)output_texture_desc.Height * (double)dlss_render_resolution_scale, (double)output_texture_desc.Width / (double)output_texture_desc.Height);
                      // The "HDR" flag in DLSS SR actually means whether the color is in linear space or "sRGB gamma" (apparently not 2.2) (SDR) space, colors beyond 0-1 don't seem to be clipped either way
                      bool dlss_hdr = GetShaderDefineCompiledNumericalValue(POST_PROCESS_SPACE_TYPE_HASH) >= 1; // we are assuming the value was always a number and not empty

#if (DEVELOPMENT || TEST) && TEST_DLSS
                      com_ptr<ID3D11VertexShader> vs;
                      com_ptr<ID3D11PixelShader> ps;
                      native_device_context->VSGetShader(&vs, nullptr, 0);
                      native_device_context->PSGetShader(&ps, nullptr, 0);
#endif //  (DEVELOPMENT || TEST) && TEST_DLSS

                      //TODOFT: we could do this async from the beginning of rendering (when we can detect res changes), to here, with a mutex, to avoid potential stutters when DRS first engages (same with creating DLSS textures?)
                      // Our DLSS implementation picks a quality mode based on a fixed rendering resolution, but we scale it back in case we detected the game is running DRS, otherwise we run DLAA.
                      // At lower quality modes (non DLAA), DLSS actually seems to allow for a wider input resolution range that it actually claims when queried for it, but if we declare a resolution scale below 50% here, we can get an assert,
                      // still, DLSS will keep working at any input resolution (or at least with a pretty big tolerance range).
                      // This function doesn't alter the pipeline state (e.g. shaders, cbuffers, RTs, ...), if not, we need to move it to the "Present()" function
                      NGX::DLSS::UpdateSettings(native_device, native_device_context, output_texture_desc.Width, output_texture_desc.Height, dlss_render_resolution[0], dlss_render_resolution[1], dlss_hdr, prey_drs_detected);

#if (DEVELOPMENT || TEST) && TEST_DLSS // Verify that DLSS never alters the pipeline state
                      com_ptr<ID3D11ShaderResourceView> ps_shader_resources_post[ARRAYSIZE(ps_shader_resources)];
                      native_device_context->PSGetShaderResources(0, ARRAYSIZE(ps_shader_resources_post), reinterpret_cast<ID3D11ShaderResourceView**>(ps_shader_resources_post));
                      for (uint32_t i = 0; i < ARRAYSIZE(ps_shader_resources); i++)
                      {
                          ASSERT_ONCE(ps_shader_resources[i] == ps_shader_resources_post[i]);
                      }

                      com_ptr<ID3D11RenderTargetView> render_target_view_post;
                      com_ptr<ID3D11DepthStencilView> depth_stencil_view_post;
                      native_device_context->OMGetRenderTargets(1, &render_target_view_post, &depth_stencil_view_post);
                      ASSERT_ONCE(render_target_views[0] == render_target_view_post && depth_stencil_view == depth_stencil_view_post);

                      com_ptr<ID3D11VertexShader> vs_post;
                      com_ptr<ID3D11PixelShader> ps_post;
                      native_device_context->VSGetShader(&vs_post, nullptr, 0);
                      native_device_context->PSGetShader(&ps_post, nullptr, 0);
                      ASSERT_ONCE(vs == vs_post && ps == ps_post);
                      vs = nullptr;
                      ps = nullptr;
                      vs_post = nullptr;
                      ps_post = nullptr;
#endif // (DEVELOPMENT || TEST) && TEST_DLSS

                      bool skip_dlss = output_texture_desc.Width < 32 || output_texture_desc.Height < 32; // DLSS doesn't support output below 32x32
                      bool dlss_output_changed = false;
                      constexpr bool dlss_use_native_uav = true;
                      bool dlss_output_supports_uav = dlss_use_native_uav && (output_texture_desc.BindFlags & D3D11_BIND_UNORDERED_ACCESS) != 0;
                      if (!dlss_output_supports_uav) {
                          output_texture_desc.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;

                          if (dlss_output_color.get()) {
                              D3D11_TEXTURE2D_DESC dlss_output_texture_desc;
                              dlss_output_color->GetDesc(&dlss_output_texture_desc);
                              dlss_output_changed = dlss_output_texture_desc.Width != output_texture_desc.Width || dlss_output_texture_desc.Height != output_texture_desc.Height || dlss_output_texture_desc.Format != output_texture_desc.Format;
                          }
                          if (!dlss_output_color.get() || dlss_output_changed) {
                              dlss_output_color = nullptr; // Make sure we discard the previous one
                              hr = native_device->CreateTexture2D(&output_texture_desc, nullptr, &dlss_output_color);
                              ASSERT_ONCE(SUCCEEDED(hr));
                          }
                          if (!dlss_output_color.get()) {
                              skip_dlss = true;
                          }
                      }
                      else {
                          dlss_output_color = output_color;
                      }

                      if (!skip_dlss) {
                          com_ptr<ID3D11Resource> source_color;
                          ps_shader_resources[0]->GetResource(&source_color);
                          com_ptr<ID3D11Resource> depth_buffer;
                          ps_shader_resources[16]->GetResource(&depth_buffer);
                          com_ptr<ID3D11Resource> object_velocity_buffer_temp;
                          ps_shader_resources[3]->GetResource(&object_velocity_buffer_temp);
                          com_ptr<ID3D11Texture2D> object_velocity_buffer;
                          hr = object_velocity_buffer_temp->QueryInterface(&object_velocity_buffer);
                          ASSERT_ONCE(SUCCEEDED(hr));

                          // Generate "fake" exposure texture
                          bool exposure_changed = false;
                          float dlss_exposure_val = dlss_scene_exposure;
#if DEVELOPMENT
                          if (dlss_custom_exposure > 0.f) {
                              dlss_exposure_val = dlss_custom_exposure;
                          }
#endif // DEVELOPMENT
                          static float previous_dlss_exposure_val = dlss_exposure_val;
                          exposure_changed = dlss_exposure_val != previous_dlss_exposure_val;
                          previous_dlss_exposure_val = dlss_exposure_val;
                          // TODO: optimize this for the "DLSS_RELATIVE_PRE_EXPOSURE" false case! Avoid re-creating the texture every frame the exposure changes and instead make it dynamic and re-write it from the CPU? Or simply make our exposure calculation shader write to a texture directly
                          // (though in that case it wouldn't have the same delay as the CPU side pre-exposure buffer readback)
                          if (!dlss_exposure.get() || exposure_changed) {
                              D3D11_TEXTURE2D_DESC exposure_texture_desc; // DLSS fails if we pass in a 1D texture so we have to make a 2D one
                              exposure_texture_desc.Width = 1;
                              exposure_texture_desc.Height = 1;
                              exposure_texture_desc.MipLevels = 1;
                              exposure_texture_desc.ArraySize = 1;
                              exposure_texture_desc.Format = DXGI_FORMAT::DXGI_FORMAT_R32_FLOAT; // FP32 just so it's easier to initialize data for it
                              exposure_texture_desc.SampleDesc.Count = 1;
                              exposure_texture_desc.SampleDesc.Quality = 0;
                              exposure_texture_desc.Usage = D3D11_USAGE_IMMUTABLE;
                              exposure_texture_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
                              exposure_texture_desc.CPUAccessFlags = 0;
                              exposure_texture_desc.MiscFlags = 0;

                              // It's best to force an exposure of 1 given that DLSS runs after the auto exposure is applied (in tonemapping).
                              // Theoretically knowing the average exposure of the frame would still be beneficial to it (somehow) so maybe we could simply let the auto exposure in,
                              D3D11_SUBRESOURCE_DATA exposure_texture_data;
                              exposure_texture_data.pSysMem = &dlss_exposure_val; // This needs to be "static" data in case the texture initialization was somehow delayed and read the data after the stack destroyed it
                              exposure_texture_data.SysMemPitch = 32;
                              exposure_texture_data.SysMemSlicePitch = 32;

                              dlss_exposure = nullptr; // Make sure we discard the previous one
                              hr = native_device->CreateTexture2D(&exposure_texture_desc, &exposure_texture_data, &dlss_exposure);
                              assert(SUCCEEDED(hr));
                          }

                          // Generate motion vectors from the objects velocity buffer and the camera movement.
                          // For the most past, these look great, especially with rotational camera movement. When there's location camera movement, thin lines do break a bit,
                          // and that might be a precision issue with high resolution and jittered matrices not having high enough precision.
                          // We take advantage of the state the game had set DX to, and simply swap the render target.
                          {
                              D3D11_TEXTURE2D_DESC object_velocity_texture_desc;
                              object_velocity_buffer->GetDesc(&object_velocity_texture_desc);
                              ASSERT_ONCE((object_velocity_texture_desc.BindFlags & D3D11_BIND_RENDER_TARGET) == D3D11_BIND_RENDER_TARGET);

                              // Update the "dlss_output_changed" flag if we hadn't already (we wouldn't have had a previous copy to compare against above)
                              if (dlss_output_supports_uav) {
                                  if (dlss_motion_vectors.get()) {
                                      D3D11_TEXTURE2D_DESC dlss_motion_vectors_desc;
                                      dlss_motion_vectors->GetDesc(&dlss_motion_vectors_desc);
                                      dlss_output_changed = dlss_motion_vectors_desc.Width != output_texture_desc.Width || dlss_motion_vectors_desc.Height != output_texture_desc.Height;
                                  }
                              }
                              // We assume the conditions of this texture (and its render target view) changing are the same as "dlss_output_changed"
                              if (!dlss_motion_vectors.get() || dlss_output_changed) {
                                  dlss_motion_vectors = nullptr; // Make sure we discard the previous one
                                  hr = native_device->CreateTexture2D(&object_velocity_texture_desc, nullptr, &dlss_motion_vectors);
                                  ASSERT_ONCE(SUCCEEDED(hr));

                                  D3D11_RENDER_TARGET_VIEW_DESC object_velocity_render_target_view_desc;
                                  render_target_views[0]->GetDesc(&object_velocity_render_target_view_desc);
                                  object_velocity_render_target_view_desc.Format = object_velocity_texture_desc.Format;
                                  object_velocity_render_target_view_desc.ViewDimension = D3D11_RTV_DIMENSION::D3D11_RTV_DIMENSION_TEXTURE2D;
                                  object_velocity_render_target_view_desc.Texture2D.MipSlice = 0;

                                  dlss_motion_vectors_rtv = nullptr; // Make sure we discard the previous one
                                  native_device->CreateRenderTargetView(dlss_motion_vectors.get(), &object_velocity_render_target_view_desc, &dlss_motion_vectors_rtv);
                              }

                              SetPreyLumaConstantBuffers(cmd_list, stages, settings_pipeline_layout, LumaConstantBufferType::LumaSettings);
                              SetPreyLumaConstantBuffers(cmd_list, stages, shared_data_pipeline_layout, LumaConstantBufferType::LumaData);

                              ID3D11RenderTargetView* const dlss_motion_vectors_rtv_const = dlss_motion_vectors_rtv.get();
                              native_device_context->OMSetRenderTargets(1, &dlss_motion_vectors_rtv_const, depth_stencil_view.get());

                              // This should be the same draw type that the shader would have used if we went through with it (SMAA 2TX/TAA).
                              native_device_context->Draw(3, 0);
                          }

                          // Reset the render target, just to make sure there's no conflicts with the same texture being used as RWTexture UAV or Shader Resources
                          ID3D11RenderTargetView* emptry_render_target_view[1] = { nullptr };
                          native_device_context->OMSetRenderTargets(0, emptry_render_target_view, depth_stencil_view.get());

                          // Reset DLSS history if we did not draw motion blur (and we previously did). Based on CryEngine source code, mb is skipped on the first frame after scene cuts, so we want to re-use that information.
                          // Reset DLSS history if for one frame we had stopped tonemapping. This might include some scene cuts, but also triggers when entering full screen UI menus or videos and then leaving them (it shouldn't be a problem).
                          // Reset DLSS history if the output resolution or format changed (just an extra safety mechanism, it might not actually be needed).
                          bool reset_dlss = force_reset_dlss || dlss_output_changed || !has_drawn_main_post_processing_previous || (has_drawn_motion_blur_previous && !has_drawn_motion_blur);
                          force_reset_dlss = false;

                          uint32_t render_width_dlss = std::lrintf(render_resolution.x);
                          uint32_t render_height_dlss = std::lrintf(render_resolution.y);

                          // These configurations store the image already multiplied by paper white from the beginning of tonemapping, including at the time DLSS runs.
                          // The other configurations run DLSS in "SDR" Gamma Space so we couldn't safely change the exposure.
                          const bool dlss_use_paper_white_pre_exposure = GetShaderDefineCompiledNumericalValue(POST_PROCESS_SPACE_TYPE_HASH) >= 1;

                          float dlss_pre_exposure = 0.f; // 0 means it's ignored
                          if (dlss_use_paper_white_pre_exposure) {
#if 1 // Alternative that considers a value of 1 in the DLSS color textures to match the SDR output nits range (whatever that is)
                              dlss_pre_exposure = cb_luma_frame_settings.ScenePaperWhite / default_paper_white;
#else // Alternative that considers a value of 1 in the DLSS color textures to match 203 nits
                              dlss_pre_exposure = cb_luma_frame_settings.ScenePaperWhite / srgb_white_level;
#endif
                              dlss_pre_exposure *= dlss_scene_pre_exposure;
                          }
#if DEVELOPMENT
                          if (dlss_custom_pre_exposure > 0.f)
                            dlss_pre_exposure = dlss_custom_pre_exposure;
#endif

                          // There doesn't seem to be a need to restore the DX state to whatever we had before (e.g. render targets, cbuffers, samplers, UAVs, texture shader resources, viewport, scissor rect, ...), CryEngine always sets everything it needs again for every pass.
                          // DLSS internally keeps its own frames history, we don't need to do that ourselves (by feeding in an output buffer that was the previous frame's output, though we do have that if needed, it should be in ps_shader_resources[1]).
                          if (NGX::DLSS::Draw(native_device_context, dlss_output_color.get(), source_color.get(), dlss_motion_vectors.get(), depth_buffer.get(), dlss_exposure.get(), dlss_pre_exposure, projection_jitters.x, projection_jitters.y, reset_dlss, render_width_dlss, render_height_dlss)) {
                              has_drawn_dlss_sr = true;
                          }

                          // Fully reset the state of the RTs given that CryEngine is very delicate with it and uses some push and pop technique (simply resetting caching and resetting the first RT seemed fine for DLSS in case optimization is needed).
                          // The fact that it could changes cbuffers or texture resources bindings or viewport seems fines.
                          native_device_context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, &render_target_views[0], depth_stencil_view.get());
                          for (UINT i = 0; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; i++) {
                              if (render_target_views[i] != nullptr) {
                                  render_target_views[i]->Release();
                                  render_target_views[i] = nullptr;
                              }
                          }

                          if (has_drawn_dlss_sr) {
                              if (!dlss_output_supports_uav) {
                                  native_device_context->CopyResource(output_color.get(), dlss_output_color.get()); // DX11 doesn't need barriers
                              }

                              has_drawn_dlss_sr = true;
                              return true; // "Cancel" the previously set draw call, DLSS will take care of it
                          }
                          // DLSS Failed, suppress it for this frame and fall back on SMAA/TAA, hoping that anything before would have been rendered correctly for it already (otherwise it will start being correct in the next frame, given we suppress it (until manually toggled again, given that it'd likely keep failing))
                          else {
                              ASSERT_ONCE(false);
                              cb_luma_frame_settings.DLSS = 0;
                              dlss_suppressed = true;
                              force_reset_dlss = true; // We missed frames so it's good to do this, it might also help prevent further errors
                          }
                      }
                      // In this case it's not our buisness to keep alive this "external" texture
                      if (dlss_output_supports_uav) {
                          dlss_output_color = nullptr;
                      }
                  }
              }
      }
#endif // NGX
  }

  //TODOFT: avoid re-applying these if the data hasn't changed (within the same frame)? Add a flag by shader for who needs them?
  if (is_custom_pass) {
      SetPreyLumaConstantBuffers(cmd_list, stages, settings_pipeline_layout, LumaConstantBufferType::LumaSettings);
      //TODOFT: only ever send this after DLSS? We don't really need it before (we barely even need it after!)
      SetPreyLumaConstantBuffers(cmd_list, stages, shared_data_pipeline_layout, LumaConstantBufferType::LumaData);
  }

#if !DEVELOPMENT //TODOFT: re-enable once we are sure we replaced all the post tonemap shaders and we are done debugging the blend states (and remove "is_custom_pass" check from below)
  //TODOFT: disable UI draw cbuffer call from c++ if game is rendering to gamma?
  if (!is_custom_pass) return false;
#else // We can't do any further checks in this case because some UI draws at the beginning of the frame (in world computers), and also sometimes the scene doesn't even draw!
  //if (!has_drawn_composed_gbuffers) return false;
#endif // !DEVELOPMENT

  LumaUIData ui_data;

  // No need to lock "s_mutex_reshade" for "cb_luma_frame_settings" here, it's not relevant
  ui_data.background_tonemapping_amount = (cb_luma_frame_settings.DisplayMode == 1 && tonemap_ui_background && has_drawn_main_post_processing) ? tonemap_ui_background_amount : 0.0;

  com_ptr<ID3D11RenderTargetView> render_target_view;
  native_device_context->OMGetRenderTargets(1, &render_target_view, nullptr);
  //native_device_context->OMGetRenderTargetsAndUnorderedAccessViews(1, &render_target_view, nullptr);
  if (render_target_view) {
    com_ptr<ID3D11Resource> render_target_resource;
    render_target_view->GetResource(&render_target_resource);
    if (render_target_resource != nullptr) {
      // We check across all the swap chain back buffers, not just the one that will be presented this frame,
      // because at least for Prey, there's only one, and anyway even if there were more, they wouldn't be used for anything else.
      // Note that in Prey in world screens are rendered with Scaleform too, but they'd never draw on the swapchain.
      if (back_buffers.contains((uint64_t)render_target_resource.get())) {
        ui_data.drawing_on_swapchain = 1;
      }
      render_target_resource = nullptr;
    }
    render_target_view = nullptr;
#if 0
    if (ui_data.drawing_on_swapchain) {
        native_device_context->OMSetRenderTargets(0, nullptr, nullptr);
        ID3D11UnorderedAccessView* swapchain_texture_uav;
        render_target_resource->Release();
        native_device->CreateUnorderedAccessView(render_target_resource, nullptr, &swapchain_texture_uav);
        native_device_context->OMSetRenderTargetsAndUnorderedAccessViews(0, nullptr, nullptr, 0, 1, &swapchain_texture_uav, nullptr);
    }
#endif
  }

  //TODOFT: check all the scaleform hashes for new unknown blend types, we need to set the cbuffers even for UI passes that render at the beginning of the frame, because they will draw in world UI (e.g. computers)
  com_ptr<ID3D11BlendState> blend_state;
  native_device_context->OMGetBlendState(&blend_state, nullptr, nullptr);
  if (blend_state) {
    D3D11_BLEND_DESC blend_desc;
    blend_state->GetDesc(&blend_desc);
    // We don't care for the alpha blend operation (source alpha * dest alpha) as alpha is never read back from destination
    if (blend_desc.RenderTarget[0].BlendEnable
        && blend_desc.RenderTarget[0].BlendOp == D3D11_BLEND_OP::D3D11_BLEND_OP_ADD) {
      // Do both the "straight alpha" and "pre-multiplied alpha" cases
      if ((blend_desc.RenderTarget[0].SrcBlend == D3D11_BLEND::D3D11_BLEND_SRC_ALPHA || blend_desc.RenderTarget[0].SrcBlend == D3D11_BLEND::D3D11_BLEND_ONE)
          && (blend_desc.RenderTarget[0].DestBlend == D3D11_BLEND::D3D11_BLEND_INV_SRC_ALPHA || blend_desc.RenderTarget[0].DestBlend == D3D11_BLEND::D3D11_BLEND_ONE)) {
        if (blend_desc.RenderTarget[0].SrcBlend == D3D11_BLEND::D3D11_BLEND_ONE && blend_desc.RenderTarget[0].DestBlend == D3D11_BLEND::D3D11_BLEND_ONE) {
          ui_data.blend_mode = 4;
        } else if (blend_desc.RenderTarget[0].SrcBlend == D3D11_BLEND::D3D11_BLEND_SRC_ALPHA && blend_desc.RenderTarget[0].DestBlend == D3D11_BLEND::D3D11_BLEND_ONE) {
          ui_data.blend_mode = 3;
        } else if (blend_desc.RenderTarget[0].SrcBlend == D3D11_BLEND::D3D11_BLEND_ONE && blend_desc.RenderTarget[0].DestBlend == D3D11_BLEND::D3D11_BLEND_INV_SRC_ALPHA) {
          ui_data.blend_mode = 2;
        } else /*if (blend_desc.RenderTarget[0].SrcBlend == D3D11_BLEND::D3D11_BLEND_SRC_ALPHA && blend_desc.RenderTarget[0].DestBlend == D3D11_BLEND::D3D11_BLEND_INV_SRC_ALPHA)*/ {
          ui_data.blend_mode = 1;
          assert(!has_drawn_main_post_processing || !ui_data.drawing_on_swapchain || (blend_desc.RenderTarget[0].SrcBlend == D3D11_BLEND::D3D11_BLEND_SRC_ALPHA && blend_desc.RenderTarget[0].DestBlend == D3D11_BLEND::D3D11_BLEND_INV_SRC_ALPHA));
        }
        //if (ui_data.blend_mode == 1 || ui_data.blend_mode == 3)
        {
          // In "blend_mode == 1", Prey seems to erroneously use "D3D11_BLEND::D3D11_BLEND_SRC_ALPHA" as source blend alpha, thus it multiplies alpha by itself when using pre-multiplied alpha passes,
          // which doesn't seem to make much sense, at least not for the first write on a separate new texture (it means that the next blend with the final target background could end up going beyond 1 because the background darkening intensity is lower than it should be).
          ASSERT_ONCE( !has_drawn_main_post_processing || (ui_data.drawing_on_swapchain ?
          // Make sure we never read back from the swap chain texture (which means we can ignore all the alpha blend ops on previous to it)
          (blend_desc.RenderTarget[0].SrcBlend != D3D11_BLEND::D3D11_BLEND_DEST_ALPHA
          && blend_desc.RenderTarget[0].DestBlend != D3D11_BLEND::D3D11_BLEND_DEST_ALPHA
          && blend_desc.RenderTarget[0].SrcBlend != D3D11_BLEND::D3D11_BLEND_DEST_COLOR
          && blend_desc.RenderTarget[0].DestBlend != D3D11_BLEND::D3D11_BLEND_DEST_COLOR)
          // Make sure that writes to separate textures always use known alpha blends modes, because we'll be reading back that alpha for later (possibly)
          : (blend_desc.RenderTarget[0].BlendOpAlpha == D3D11_BLEND_OP::D3D11_BLEND_OP_ADD
          && (blend_desc.RenderTarget[0].SrcBlendAlpha == D3D11_BLEND::D3D11_BLEND_SRC_ALPHA
              || blend_desc.RenderTarget[0].SrcBlendAlpha == D3D11_BLEND::D3D11_BLEND_ONE))));
         }
      }
      else {
        ASSERT_ONCE(!has_drawn_main_post_processing || !ui_data.drawing_on_swapchain);
      }
    }
    assert(!has_drawn_main_post_processing || !ui_data.drawing_on_swapchain || !blend_desc.RenderTarget[0].BlendEnable || blend_desc.RenderTarget[0].BlendOp == D3D11_BLEND_OP::D3D11_BLEND_OP_ADD);
    blend_state = nullptr;
  }

  if (is_custom_pass) {
    cmd_list->push_constants(
        stages,
        ui_pipeline_layout,
        0,
        0,
        sizeof(LumaUIData) / sizeof(uint32_t),
        &ui_data);
  }

  return false; // Return true to cancel this draw call
}

#if DEVELOPMENT
void CopyDebugDrawTexture(reshade::api::command_list* cmd_list) {
    ID3D11Device* native_device = (ID3D11Device*)(cmd_list->get_device()->get_native());
    ID3D11DeviceContext* native_device_context = (ID3D11DeviceContext*)(cmd_list->get_native());

    com_ptr<ID3D11Resource> texture_resource;
    // TODO: add support for render target indexes beyond 0
    if (debug_draw_render_target_view) {
        com_ptr<ID3D11RenderTargetView> render_target_view;

        ID3D11RenderTargetView* rtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT];
        native_device_context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, &rtvs[0], nullptr);
        render_target_view = rtvs[debug_draw_view_index];
        for (UINT i = 0; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; i++) {
            if (rtvs[i] != nullptr) {
                rtvs[i]->Release();
                rtvs[i] = nullptr;
            }
        }

        if (render_target_view) {
            render_target_view->GetResource(&texture_resource);
            D3D11_RENDER_TARGET_VIEW_DESC rtv_desc;
            render_target_view->GetDesc(&rtv_desc);
            debug_draw_texture_format = rtv_desc.Format; // Note: this isn't synchronized with the conditions that update "debug_draw_texture" below but it should work anyway
        }
    }
    else {
        com_ptr<ID3D11ShaderResourceView> shader_resource_view;
        native_device_context->PSGetShaderResources(debug_draw_view_index, 1, &shader_resource_view);
        if (shader_resource_view) {
            shader_resource_view->GetResource(&texture_resource);
            D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc;
            shader_resource_view->GetDesc(&srv_desc);
            debug_draw_texture_format = srv_desc.Format;
        }
    }
    if (texture_resource) {
        com_ptr<ID3D11Texture2D> texture;
        texture_resource->QueryInterface(&texture);
        // For now we re-create it every frame as we don't care for performance
        if (texture) {
            D3D11_TEXTURE2D_DESC texture_desc;
            texture->GetDesc(&texture_desc);
            texture_desc.Usage = D3D11_USAGE_DEFAULT;
            texture_desc.CPUAccessFlags = 0;
            texture_desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS; // Just do all of them
            debug_draw_texture = nullptr;
            HRESULT hr = native_device->CreateTexture2D(&texture_desc, nullptr, &debug_draw_texture); //TODOFT: figure out error, happens sometimes. And make thread safe!
            ASSERT_ONCE(SUCCEEDED(hr));

            // Back it up as it gets immediately overwritten or re-used later
            if (debug_draw_texture) {
                native_device_context->CopyResource(debug_draw_texture.get(), texture.get());
            }
        }
    }
}
#endif

bool OnDraw(
    reshade::api::command_list* cmd_list,
    uint32_t vertex_count,
    uint32_t instance_count,
    uint32_t first_vertex,
    uint32_t first_instance) {
    ShaderHashesList original_shader_hashes;
    bool cancelled_or_replaced = HandlePreDraw(cmd_list, false, original_shader_hashes);
#if DEVELOPMENT
    // TODO: add support for cancelled passes here (and below), given that we can't retrieve the render target texture anymore. Also add support for compute shaders?
    // First run the draw call (don't delegate it to ReShade) and then copy its output
    auto& cmd_list_data = cmd_list->get_private_data<CommandListData>();
    bool wants_debug_draw = debug_draw_shader_hash != 0 || debug_draw_pipeline != 0;
    if (wants_debug_draw && (debug_draw_shader_hash == 0 || original_shader_hashes.Contains(debug_draw_shader_hash, reshade::api::shader_stage::pixel)) && (debug_draw_pipeline == 0 || debug_draw_pipeline == cmd_list_data.pipeline_state_original_pixel_shader.handle)) {
        auto local_debug_draw_pipeline_instance = debug_draw_pipeline_instance.fetch_add(1);
        if (debug_draw_pipeline_target_instance == -1 || local_debug_draw_pipeline_instance - 1 == debug_draw_pipeline_target_instance) {
            if (!cancelled_or_replaced) {
                ID3D11DeviceContext* native_device_context = (ID3D11DeviceContext*)(cmd_list->get_native());
                if (instance_count > 1) {
                    native_device_context->DrawInstanced(vertex_count, instance_count, first_vertex, first_instance);
                }
                else {
                    ASSERT_ONCE(first_instance == 0);
                    native_device_context->Draw(vertex_count, first_vertex);
                }
                cancelled_or_replaced = true;
            }

            CopyDebugDrawTexture(cmd_list);
        }
    }
#endif
    return cancelled_or_replaced;
}

bool OnDrawIndexed(
    reshade::api::command_list* cmd_list,
    uint32_t index_count,
    uint32_t instance_count,
    uint32_t first_index,
    int32_t vertex_offset,
    uint32_t first_instance) {
    ShaderHashesList original_shader_hashes;
    bool cancelled_or_replaced = HandlePreDraw(cmd_list, false, original_shader_hashes);
#if DEVELOPMENT
    // First run the draw call (don't delegate it to ReShade) and then copy its output
    auto& cmd_list_data = cmd_list->get_private_data<CommandListData>();
    bool wants_debug_draw = debug_draw_shader_hash != 0 || debug_draw_pipeline != 0;
    if (wants_debug_draw && (debug_draw_shader_hash == 0 || original_shader_hashes.Contains(debug_draw_shader_hash, reshade::api::shader_stage::pixel)) && (debug_draw_pipeline == 0 || debug_draw_pipeline == cmd_list_data.pipeline_state_original_pixel_shader.handle)) {
        auto local_debug_draw_pipeline_instance = debug_draw_pipeline_instance.fetch_add(1);
        if (debug_draw_pipeline_target_instance == -1 || local_debug_draw_pipeline_instance - 1 == debug_draw_pipeline_target_instance) {
            if (!cancelled_or_replaced) {
                ID3D11DeviceContext* native_device_context = (ID3D11DeviceContext*)(cmd_list->get_native());
                if (instance_count > 1) {
                    native_device_context->DrawIndexedInstanced(index_count, instance_count, first_index, vertex_offset, first_instance);
                }
                else {
                    ASSERT_ONCE(first_instance == 0);
                    native_device_context->DrawIndexed(index_count, first_index, vertex_offset);
                }
                cancelled_or_replaced = true;
            }

            CopyDebugDrawTexture(cmd_list);
        }
    }
#endif
    return cancelled_or_replaced;
}

bool OnDispatch(reshade::api::command_list* cmd_list, uint32_t group_count_x, uint32_t group_count_y, uint32_t group_count_z) {
    ShaderHashesList original_shader_hashes;
    return HandlePreDraw(cmd_list, true, original_shader_hashes);
}

bool OnDrawOrDispatchIndirect(
    reshade::api::command_list* cmd_list,
    reshade::api::indirect_command type,
    reshade::api::resource buffer,
    uint64_t offset,
    uint32_t draw_count,
    uint32_t stride) {
    ASSERT_ONCE(false); // Not used by Prey (DrawIndexedInstancedIndirect() and DrawInstancedIndirect() weren't used in CryEngine)
    // NOTE: according to ShortFuse, this can be "reshade::api::indirect_command::unknown" too, so we'd need to fall back on checking what shader is bound to know if this is a compute shader draw
    bool is_dispatch = type == reshade::api::indirect_command::dispatch || type == reshade::api::indirect_command::dispatch_mesh || type == reshade::api::indirect_command::dispatch_rays;
    ShaderHashesList original_shader_hashes;
    return HandlePreDraw(cmd_list, is_dispatch, original_shader_hashes);
}

// TODO: use the native ReShade sampler desc instead? It's not really necessary
com_ptr<ID3D11SamplerState> CreateCustomSampler(ID3D11Device* device, D3D11_SAMPLER_DESC desc) {
#if !DEVELOPMENT
    if (desc.Filter == D3D11_FILTER_ANISOTROPIC || desc.Filter == D3D11_FILTER_COMPARISON_ANISOTROPIC) {
        desc.MaxAnisotropy = D3D11_REQ_MAXANISOTROPY;
        desc.MipLODBias = std::clamp(desc.MipLODBias + texture_mip_lod_bias_offset, D3D11_MIP_LOD_BIAS_MIN, D3D11_MIP_LOD_BIAS_MAX); // Setting this out of range (~ +/- 16) will make DX11 crash
    }
    else {
        return nullptr;
    }
#else
    if (samplers_upgrade_mode <= 0)
        return nullptr;

    // Prey's CryEngine only uses:
    // D3D11_FILTER_ANISOTROPIC
    // D3D11_FILTER_COMPARISON_ANISOTROPIC
    // D3D11_FILTER_MIN_MAG_MIP_POINT
    // D3D11_FILTER_COMPARISON_MIN_MAG_MIP_POINT
    // D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT
    // D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT
    // D3D11_FILTER_MIN_MAG_MIP_LINEAR
    // D3D11_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR
    
    // This could theoretically make some textures that have moire patters, or were purposely blurry, "worse", but the positives of upgrading still outweight the negatives.
    // Note that this might not fix all cases because there's still "ID3D11DeviceContext::SetResourceMinLOD()" and textures that are blurry for other reasons
    // because they use other types of samplers (unfortunately it seems like some decals use "D3D11_FILTER_MIN_MAG_MIP_LINEAR").
    // Note that the AF on different textures in the game seems is possibly linked with other graphics settings than just AF (maybe textures or objects quality).
    if (desc.Filter == D3D11_FILTER_ANISOTROPIC || desc.Filter == D3D11_FILTER_COMPARISON_ANISOTROPIC) {
        // Note: this doesn't seem to affect much
        if (samplers_upgrade_mode == 1) {
            desc.MaxAnisotropy = min(desc.MaxAnisotropy * 2, D3D11_REQ_MAXANISOTROPY);
        }
        else if (samplers_upgrade_mode == 2) {
            desc.MaxAnisotropy = min(desc.MaxAnisotropy * 4, D3D11_REQ_MAXANISOTROPY);
        }
        else if (samplers_upgrade_mode >= 3) {
            desc.MaxAnisotropy = D3D11_REQ_MAXANISOTROPY;
        }
        // Note: this is the main ingredient in making textures less blurry
        if (samplers_upgrade_mode == 4 && desc.MipLODBias <= 0.f) {
            desc.MipLODBias = std::clamp(desc.MipLODBias + texture_mip_lod_bias_offset, D3D11_MIP_LOD_BIAS_MIN, D3D11_MIP_LOD_BIAS_MAX);
        }
        else if (samplers_upgrade_mode >= 5) {
            desc.MipLODBias = std::clamp(texture_mip_lod_bias_offset, D3D11_MIP_LOD_BIAS_MIN, D3D11_MIP_LOD_BIAS_MAX);
        }
        // Note: this never seems to affect anything in Prey
        if (samplers_upgrade_mode >= 6) {
            desc.MinLOD = min(desc.MinLOD, 0.f);
        }
    }
    else if ((desc.Filter == D3D11_FILTER_MIN_MAG_MIP_LINEAR && samplers_upgrade_mode_2 >= 1) // This is the most common (main/only) format being used other than AF
        || (desc.Filter == D3D11_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR && samplers_upgrade_mode_2 >= 2)
        || (desc.Filter == D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT && samplers_upgrade_mode_2 >= 3)
        || (desc.Filter == D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT && samplers_upgrade_mode_2 >= 4)
        || (desc.Filter == D3D11_FILTER_MIN_MAG_MIP_POINT && samplers_upgrade_mode_2 >= 5)
        || (desc.Filter == D3D11_FILTER_COMPARISON_MIN_MAG_MIP_POINT && samplers_upgrade_mode_2 >= 6)) {
#if 0 //TODOFT4: research. Forced this on to see how it behaves. Doesn't work, it doesn't really help any further with (e.g.) blurry decal textures
        desc.Filter == (desc.ComparisonFunc != D3D11_COMPARISON_NEVER) ? D3D11_FILTER_COMPARISON_ANISOTROPIC : D3D11_FILTER_ANISOTROPIC;
        desc.MaxAnisotropy = D3D11_REQ_MAXANISOTROPY;
#else
        // Note: this doesn't seem to do anything really, it doesn't help with the occasional blurry texture (probably because all samplers that needed anisotropic already had it set)
        if (samplers_upgrade_mode >= 7) {
            desc.Filter == (desc.ComparisonFunc != D3D11_COMPARISON_NEVER && samplers_upgrade_mode == 7) ? D3D11_FILTER_COMPARISON_ANISOTROPIC : D3D11_FILTER_ANISOTROPIC;
            desc.MaxAnisotropy = D3D11_REQ_MAXANISOTROPY;
        }
#endif
        // Note: changing the lod bias of non anisotropic filters makes reflections (cubemap samples?) a lot more specular (shiny) in Prey, so it's best avoided (it can look better is some screenshots, but it's likely not intended).
        // Even if we only fix up textures that didn't have a positive bias, we run into the same problem.
        if (samplers_upgrade_mode == 4 && desc.MipLODBias <= 0.f) {
            desc.MipLODBias = std::clamp(desc.MipLODBias + texture_mip_lod_bias_offset, D3D11_MIP_LOD_BIAS_MIN, D3D11_MIP_LOD_BIAS_MAX);
        }
        else if (samplers_upgrade_mode >= 5) {
            desc.MipLODBias = std::clamp(texture_mip_lod_bias_offset, D3D11_MIP_LOD_BIAS_MIN, D3D11_MIP_LOD_BIAS_MAX);
        }
        if (samplers_upgrade_mode >= 6) {
            desc.MinLOD = min(desc.MinLOD, 0.f);
        }
    }
#endif // !DEVELOPMENT

    com_ptr<ID3D11SamplerState> sampler;
    device->CreateSamplerState(&desc, &sampler);
    ASSERT_ONCE(sampler != nullptr);
    return sampler;
}

void OnInitSampler(reshade::api::device* device, const reshade::api::sampler_desc& desc, reshade::api::sampler sampler) {
    if (sampler == 0)
        return;

#if DEVELOPMENT && 0 // Assert in case we got unexpected samplers
    if (desc.filter == reshade::api::filter_mode::anisotropic || desc.filter == reshade::api::filter_mode::compare_anisotropic)
    {
        assert(desc.max_anisotropy >= 2); // Doesn't seem to happen
        assert(desc.min_lod == 0); // Doesn't seem to happen
        assert(desc.mip_lod_bias == 0.f); // This seems to happen when enabling TAA (but not with SMAA 2TX), some new samplers are created with bias -1 and then persist, it's unclear if they are used though.
    }
    else
    {
        assert(desc.max_anisotropy <= 1); // This can happen (like once) in Prey. AF is probably ignored for these anyway so it's innocuous
    }
    assert(desc.filter != reshade::api::filter_mode::min_mag_anisotropic_mip_point && desc.filter != reshade::api::filter_mode::compare_min_mag_anisotropic_mip_point); // Doesn't seem to happen

    ASSERT_ONCE(desc.filter == reshade::api::filter_mode::anisotropic
        || desc.filter == reshade::api::filter_mode::compare_anisotropic
        || desc.filter == reshade::api::filter_mode::min_mag_mip_linear
        || desc.filter == reshade::api::filter_mode::compare_min_mag_mip_linear
        || desc.filter == reshade::api::filter_mode::min_mag_linear_mip_point
        || desc.filter == reshade::api::filter_mode::compare_min_mag_linear_mip_point); // Doesn't seem to happen
#endif // DEVELOPMENT

    ID3D11SamplerState* native_sampler = reinterpret_cast<ID3D11SamplerState*>(sampler.handle);

    std::shared_lock shared_lock_samplers(s_mutex_samplers);

    // Custom samplers lifetime should never be tracked by ReShade, otherwise we'd recursively create custom samplers out of custom samplers
    // (it's unclear if CryEngine somehow does anything with these samplers or if ReShade captures our own samplers creation events (it probably does as we create them directly through the DX native funcs))
    for (const auto& samplers_handle : custom_sampler_by_original_sampler) {
        for (const auto& custom_sampler_handle : samplers_handle.second) {
            ID3D11SamplerState* native_sampler = reinterpret_cast<ID3D11SamplerState*>(sampler.handle);
            if (custom_sampler_handle.second.get() == native_sampler) {
                return;
            }
        }
    }

    D3D11_SAMPLER_DESC native_desc;
    native_sampler->GetDesc(&native_desc);
    shared_lock_samplers.unlock(); // This is fine!
    std::unique_lock unique_lock_samplers(s_mutex_samplers);
    custom_sampler_by_original_sampler[sampler.handle][texture_mip_lod_bias_offset] = CreateCustomSampler((ID3D11Device*)device->get_native(), native_desc);
}

void OnDestroySampler(reshade::api::device* device, reshade::api::sampler sampler) {
    //TODOFT: can this actually be called by a separate thread? probably
    // This only seems to happen when the game shuts down in Prey
    const std::unique_lock lock_samplers(s_mutex_samplers);

#if DEVELOPMENT //TODOFT: delete, already in "OnInitSampler()", so this shouldn't be able to ever happen
    ID3D11SamplerState* native_sampler = reinterpret_cast<ID3D11SamplerState*>(sampler.handle);
    // Custom samplers lifetime should never be tracked by ReShade (is this innoucuous? remove it from the list in case it happened)
    for (const auto& samplers_handle : custom_sampler_by_original_sampler) {
        for (const auto& custom_sampler_handle : samplers_handle.second) {
            ASSERT_ONCE(custom_sampler_handle.second.get() != native_sampler);
        }
    }
#endif

    custom_sampler_by_original_sampler.erase(sampler.handle);
}

#if DEVELOPMENT
void OnInitResource(
    reshade::api::device* device,
    const reshade::api::resource_desc& desc,
    const reshade::api::subresource_data* initial_data,
    reshade::api::resource_usage initial_state,
    reshade::api::resource resource) {
  auto& data = device->get_private_data<DeviceData>();
  const std::unique_lock lock(data.mutex);
  data.resources.emplace(resource.handle);
}

void OnDestroyResource(reshade::api::device* device, reshade::api::resource resource) {
  auto& data = device->get_private_data<DeviceData>();
  const std::unique_lock lock(data.mutex);
  data.resources.erase(resource.handle);
}

void OnInitResourceView(
    reshade::api::device* device,
    reshade::api::resource resource,
    reshade::api::resource_usage usage_type,
    const reshade::api::resource_view_desc& desc,
    reshade::api::resource_view view) {
  auto& data = device->get_private_data<DeviceData>();
  const std::unique_lock lock(data.mutex);
  if (data.resource_views.contains(view.handle)) {
    if (resource.handle == 0) {
      data.resource_views.erase(view.handle);
      return;
    }
  }
  if (resource.handle != 0) {
    data.resource_views.emplace(view.handle, resource.handle);
  }
}

void OnDestroyResourceView(reshade::api::device* device, reshade::api::resource_view view) {
  auto& data = device->get_private_data<DeviceData>();
  const std::unique_lock lock(data.mutex);
  data.resource_views.erase(view.handle);
}

std::thread::id global_cbuffer_thread_id;
#endif // DEVELOPMENT

//TODOFT: an alternative way of approaching this would be to cache all the address of buffers that are ever filled up through ::Map() calls,
//then story a copy of each of their instances, and when one of these buffers is set to a shader stage, re-set the same cbuffer with our
//modified and fixed up data. That is a bit slower but it would be more safe, as it would guarantee us 100% that the buffer we are changing is cbuffer 13.
// Call this after reading the global cbuffer (index 13) memory (from CPU or GPU memory). This seemengly only happens in one thread.
// This will update the "cb_per_view_global" values if the ptr is found to be the right type of buffer (and return true in that case),
// correct some of its values, and cache information for other usage.
bool UpdateGlobalCBuffer(const void* global_buffer_data_ptr)
{
    const CBPerViewGlobal& global_buffer_data = *((const CBPerViewGlobal*)global_buffer_data_ptr);

    //TODOFT: optimize?
    // Is this the cbuffer we are looking for?
    // Note that even if it was, in the menu a lot of these parameters are uninitialized (usually zeroed around, with matrices being identity).
    // This check overall is a bit crazy, but there's ~0% chance that it will fail and accidentally use a buffer that isn't the global one (cb13)
    bool is_valid_cbuffer = true
        && global_buffer_data.CV_AnimGenParams.x >= 0.f && global_buffer_data.CV_AnimGenParams.y >= 0.f && global_buffer_data.CV_AnimGenParams.z >= 0.f && global_buffer_data.CV_AnimGenParams.w >= 0.f // These are either all 4 0 or all 4 > 0
        && global_buffer_data.CV_CameraRightVector.w == 0.f
        && global_buffer_data.CV_CameraFrontVector.w == 0.f
        && global_buffer_data.CV_CameraUpVector.w == 0.f
        && global_buffer_data.CV_ScreenSize.x > 0.f && global_buffer_data.CV_ScreenSize.y > 0.f && global_buffer_data.CV_ScreenSize.z > 0.f && global_buffer_data.CV_ScreenSize.w > 0.f
        && AlmostEqual(global_buffer_data.CV_ScreenSize.x, global_buffer_data.CV_HPosScale.x * (0.5f / global_buffer_data.CV_ScreenSize.z), 0.5f) && AlmostEqual(global_buffer_data.CV_ScreenSize.y, global_buffer_data.CV_HPosScale.y * (0.5f / global_buffer_data.CV_ScreenSize.w), 0.5f)
        && global_buffer_data.CV_HPosScale.x > 0.f && global_buffer_data.CV_HPosScale.y > 0.f && global_buffer_data.CV_HPosScale.z > 0.f && global_buffer_data.CV_HPosScale.w > 0.f
        && global_buffer_data.CV_HPosScale.x <= 1.f && global_buffer_data.CV_HPosScale.y <= 1.f && global_buffer_data.CV_HPosScale.z <= 1.f && global_buffer_data.CV_HPosScale.w <= 1.f
        && global_buffer_data.CV_HPosClamp.x > 0.f && global_buffer_data.CV_HPosClamp.y > 0.f && global_buffer_data.CV_HPosClamp.z > 0.f && global_buffer_data.CV_HPosClamp.w > 0.f
        && global_buffer_data.CV_HPosClamp.x <= 1.f && global_buffer_data.CV_HPosClamp.y <= 1.f && global_buffer_data.CV_HPosClamp.z <= 1.f && global_buffer_data.CV_HPosClamp.w <= 1.f
        //&& mathMatrixAlmostEqual(global_buffer_data.CV_InvViewProj.GetTransposed(), global_buffer_data.CV_ViewProjMatr.GetTransposed().GetInverted(), 0.001f) // These checks fail, they need more investigation
        //&& mathMatrixAlmostEqual(global_buffer_data.CV_InvViewMatr.GetTransposed(), global_buffer_data.CV_ViewMatr.GetTransposed().GetInverted(), 0.001f)
        && (mathMatrixIsProjection(global_buffer_data.CV_PrevViewProjMatr.GetTransposed()) || mathMatrixIsIdentity(global_buffer_data.CV_PrevViewProjMatr)) // For shadow projection "CV_PrevViewProjMatr" is actually what its names says it is, instead of being the current projection matrix as in other passes
        && (mathMatrixIsProjection(global_buffer_data.CV_PrevViewProjNearestMatr.GetTransposed()) || mathMatrixIsIdentity(global_buffer_data.CV_PrevViewProjNearestMatr))
        && global_buffer_data.CV_SunLightDir.w == 1.f
        //&& global_buffer_data.CV_SunColor.w == 1.f // This is only approximately 1 (maybe not guaranteed, sometimes it's 0)
        && global_buffer_data.CV_SkyColor.w == 1.f
        && global_buffer_data.CV_DecalZFightingRemedy.w == 0.f
        && global_buffer_data.CV_PADDING0 == 0.f && global_buffer_data.CV_PADDING1 == 0.f
        ;

#if DEVELOPMENT
    cb_per_view_globals.emplace_back(global_buffer_data);
    cb_per_view_globals_last_drawn_shader.emplace_back(last_drawn_shader); // The shader hash could we unspecified if we didn't replace the shader
#endif // DEVELOPMENT

    if (!is_valid_cbuffer) {
        return false;
    }

    //if (is_valid_cbuffer)
    {
#if 0 // This happens, but it's not a problem
        char* global_buffer_data_ptr_cast = (char*)global_buffer_data_ptr;
        // Make sure that all extra memory is zero, as an extra check. This could easily be uninitialized memory though.
        ASSERT_ONCE(IsMemoryAllZero(&global_buffer_data_ptr_cast[sizeof(CBPerViewGlobal) - 1], CBPerViewGlobal_buffer_size - sizeof(CBPerViewGlobal)));
#endif

        ASSERT_ONCE((global_buffer_data.CV_DecalZFightingRemedy.x >= 0.9f && global_buffer_data.CV_DecalZFightingRemedy.x <= 1.f) || global_buffer_data.CV_DecalZFightingRemedy.x == 0.f);
    }

    float cb_output_resolution_x = std::round(0.5f / global_buffer_data.CV_ScreenSize.z); // Round here already as it would always meant to be integer
    float cb_output_resolution_y = std::round(0.5f / global_buffer_data.CV_ScreenSize.w);

    //TODOFT4: improve these checks here and below, they aren't reliable? They seem fine now! But we are having some problems with wrong resolutions passing through (or right resolutions being blocked?) (fixed?)
    bool output_resolution_matches = AlmostEqual(output_resolution.x, cb_output_resolution_x, 0.5f) && AlmostEqual(output_resolution.y, cb_output_resolution_y, 0.5f);
    // Shadow maps and other things temporarily change the values in the global cbuffer,
    // like not use inverse depth (which affects the projection matrix, and thus many other matrices?),
    // use different render and output resolutions, etc etc.
    // We could also base our check on "CV_ProjRatio" (x and y) and "CV_FrustumPlaneEquation" and "CV_DecalZFightingRemedy" as these are also different for alternative views.
    // "CV_PrevViewProjMatr" is not a raw projection matrix when rendering shadow maps, so we can easily detect that.
    // Note: we can check if the matrix is identity to detect whether we are currently in a menu (the main menu?)
    bool is_custom_draw_version = /*!output_resolution_matches ||*/ !mathMatrixIsProjection(global_buffer_data.CV_PrevViewProjMatr.GetTransposed());

    if (is_custom_draw_version) {
        return false;
    }

#if DEVELOPMENT
    std::thread::id new_global_cbuffer_thread_id = std::this_thread::get_id();
    // Make sure this cbuffer is always updated in the same thread (forever)
    if (global_cbuffer_thread_id != std::thread::id()) {
        ASSERT_ONCE(global_cbuffer_thread_id == new_global_cbuffer_thread_id);
    }
    global_cbuffer_thread_id = new_global_cbuffer_thread_id;
#endif

    // Copy the temporary buffer ptr into our persistent data
    cb_per_view_global = global_buffer_data;

    // Re-use the current cbuffer as the previous one if we didn't draw the scene in the frame before
    const CBPerViewGlobal& cb_per_view_global_actual_previous = has_drawn_main_post_processing_previous ? cb_per_view_global_previous : cb_per_view_global;

    auto current_projection_matrix = cb_per_view_global.CV_PrevViewProjMatr;
    auto current_nearest_projection_matrix = cb_per_view_global.CV_PrevViewProjNearestMatr;

    // Note that "prey_taa_detected" would be one frame late here, but to avoid unexpectedly replacing proj matrices, we check it anyway  (the game always starts with a fade to black, so it's fine)
    bool replace_prev_projection_matrix = cloned_pipeline_count != 0 && ((dlss_sr && !dlss_suppressed && prey_taa_detected)
#if DEVELOPMENT || TEST
        || GetShaderDefineCompiledNumericalValue(FORCE_MOTION_VECTORS_JITTERED_HASH) >= 1
#else
        || force_motion_vectors_jittered
#endif
    );

    // Fix up the "previous view projection matrices" as they had wrong data in Prey,
    // first of all, their name was "wrong", because it was meant to have the value of the previous projection matrix,
    // not the camera/view projection matrix, and second, it was actually always based on the current one,
    // so it would miss any changes in FOV and jitters (drastically lowering the quality of motion vectors).
    // After tonemapping, ignore fixing up these, because they'd be jitterless and we don't have a jitterless copy (they aren't used anyway!).
    // If in the previous frame we didn't render, we don't replace the matrix with the one from the last frame that was rendered,
    // because there's no guaranteed that it would match.
    // If AA is disabled, or if the current form of AA doesn't used jittered rendering, this doesn't really make a difference (but it's still better because it creates motion vectors based on the previous view matrix).
    if ((fix_prev_matrix_mode >= 1 && fix_prev_matrix_mode <= 4) && replace_prev_projection_matrix && !has_drawn_tonemapping && has_drawn_main_post_processing_previous) {
        //TODOFT4: investigate whether it's actually good that we are using the previous projection matrix FOV,
        //or should we use the current projection matrix with the previous frame's jitters?
        //Test this by seeing if zooming in and out of with the camera in game causes ghosting.
        //UPDATE: we've fixed it in shaders, like this "velocity /= LumaData.RenderResolutionScale"
        cb_per_view_global.CV_PrevViewProjMatr = previous_projection_matrix;
        cb_per_view_global.CV_PrevViewProjNearestMatr = previous_nearest_projection_matrix;
        if (fix_prev_matrix_mode >= 2) {
            cb_per_view_global.CV_PrevViewProjMatr.m02 *= 0.5;
            cb_per_view_global.CV_PrevViewProjMatr.m12 *= 0.5;
            cb_per_view_global.CV_PrevViewProjNearestMatr.m02 *= 0.5;
            cb_per_view_global.CV_PrevViewProjNearestMatr.m12 *= 0.5;
        }
#if DEVELOPMENT
        // We've already fixes this in shaders (in better ways?)
        if (fix_prev_matrix_mode == 3) {
            cb_per_view_global.CV_PrevViewProjMatr.m02 *= cb_per_view_global.CV_HPosScale.x;
            cb_per_view_global.CV_PrevViewProjMatr.m12 *= cb_per_view_global.CV_HPosScale.y;
            cb_per_view_global.CV_PrevViewProjNearestMatr.m02 *= cb_per_view_global.CV_HPosScale.x;
            cb_per_view_global.CV_PrevViewProjNearestMatr.m12 *= cb_per_view_global.CV_HPosScale.y;
        }
        else if (fix_prev_matrix_mode == 4) {
            cb_per_view_global.CV_PrevViewProjMatr.m02 /= cb_per_view_global.CV_HPosScale.x;
            cb_per_view_global.CV_PrevViewProjMatr.m12 /= cb_per_view_global.CV_HPosScale.y;
            cb_per_view_global.CV_PrevViewProjNearestMatr.m02 /= cb_per_view_global.CV_HPosScale.x;
            cb_per_view_global.CV_PrevViewProjNearestMatr.m12 /= cb_per_view_global.CV_HPosScale.y;
        }
#endif // DEVELOPMENT
    }
#if DEVELOPMENT
    else if (fix_prev_matrix_mode >= 5 && replace_prev_projection_matrix && !has_drawn_tonemapping && has_drawn_main_post_processing_previous) {
        // Use old jitters with new view matrix (this might match how they calculate MVs based on this matrix)
        cb_per_view_global.CV_PrevViewProjMatr.m02 = previous_projection_matrix.m02;
        cb_per_view_global.CV_PrevViewProjMatr.m12 = previous_projection_matrix.m12;
        cb_per_view_global.CV_PrevViewProjNearestMatr.m02 = previous_nearest_projection_matrix.m02;
        cb_per_view_global.CV_PrevViewProjNearestMatr.m12 = previous_nearest_projection_matrix.m12;
    }

    // Just for test.
    if (disable_taa_jitters) {
        current_projection_matrix.m02 = 0;
        current_projection_matrix.m12 = 0;
        current_nearest_projection_matrix.m02 = 0;
        current_nearest_projection_matrix.m12 = 0;
        cb_per_view_global.CV_PrevViewProjMatr.m02 = 0;
        cb_per_view_global.CV_PrevViewProjMatr.m12 = 0;
        cb_per_view_global.CV_PrevViewProjNearestMatr.m02 = 0;
        cb_per_view_global.CV_PrevViewProjNearestMatr.m12 = 0;
    }
#endif // DEVELOPMENT

    // Fix up the rendering scale for all passes after DLSS SR, as we upscaled before the game expected,
    // there's only post processing passes after it anyway (and lens optics shaders don't really read cbuffer 13 (we made sure of that), but still, some of their passes use custom resolutions).
    if (has_drawn_dlss_sr && prey_drs_active && !has_drawn_upscaling) {
        cb_per_view_global.CV_ScreenSize.x = cb_output_resolution_x;
        cb_per_view_global.CV_ScreenSize.y = cb_output_resolution_y;

        cb_per_view_global.CV_HPosScale.x = 1.f;
        cb_per_view_global.CV_HPosScale.y = 1.f;
        // Upgrade the ones from the previous frame too, because at this rendering phase they'd also have been full resolution, and these aren't used anyway
        cb_per_view_global.CV_HPosScale.z = cb_per_view_global.CV_HPosScale.x;
        cb_per_view_global.CV_HPosScale.w = cb_per_view_global.CV_HPosScale.y;

        // Clamp at the last texel center (half pixel offset) at the bottom right of the rendering (which is now equal to output) resolution area
        cb_per_view_global.CV_HPosClamp.x = 1.f - cb_per_view_global.CV_ScreenSize.z;
        cb_per_view_global.CV_HPosClamp.y = 1.f - cb_per_view_global.CV_ScreenSize.w;
        cb_per_view_global.CV_HPosClamp.z = cb_per_view_global.CV_HPosClamp.x;
        cb_per_view_global.CV_HPosClamp.w = cb_per_view_global.CV_HPosClamp.y;
    }

    // Update our cached data with information from the cbuffer.
    // After vanilla tonemapping (as soon as AA starts),
    // camera jitters are removed from the cbuffer projection matrices, and the render resolution is also set to 100% (after the upscaling pass),
    // so we want to ignore these cases. We stop at the gbuffer compositions draw, because that's the last know cbuffer 13 to have the perfect values we are looking for (that shader is always run, so it's reliable)!
    // A lot of passes are drawn on scaled down render targets and the cbuffer values would have been updated to reflect that (e.g. "CV_ScreenSize"), so ignore these cases.
    if (output_resolution_matches && (/*!found_per_view_globals ||*/ (!has_drawn_composed_gbuffers && !has_drawn_tonemapping && !has_drawn_main_post_processing))) {
#if DEVELOPMENT
        static float2 local_previous_render_resolution;
        if (!found_per_view_globals) {
            local_previous_render_resolution.x = cb_per_view_global.CV_ScreenSize.x;
            local_previous_render_resolution.y = cb_per_view_global.CV_ScreenSize.y;
        }
#endif // DEVELOPMENT

        //TODOFT: these have read/writes that are possibly not thread safe but they should never cause issues in actual usages of Prey
        render_resolution.x = cb_per_view_global.CV_ScreenSize.x;
        render_resolution.y = cb_per_view_global.CV_ScreenSize.y;
#if 0 // They should already match and the one we have would be more accurate anyway
        output_resolution.x = cb_output_resolution_x; // Round here already as it would always meant to be integer
        output_resolution.y = cb_output_resolution_y;
#endif

        auto previous_prey_drs_active = prey_drs_active.load();
        prey_drs_active = std::abs(render_resolution.x - output_resolution.x) >= 0.5f || std::abs(render_resolution.y - output_resolution.y) >= 0.5f;
        // Make sure this doesn't change within a frame (once we found DRS in a frame, we should never "lose" it again for that frame.
        // Ignore this when we have no shaders loaded as it would always break due to the "has_drawn_tonemapping" check failing.
        ASSERT_ONCE(cloned_pipeline_count == 0 || !found_per_view_globals || !previous_prey_drs_active || (previous_prey_drs_active == prey_drs_active));

#if DEVELOPMENT
        // Make sure that our rendering resolution doesn't change randomly within the pipeline (it probably will!)
        assert(!found_per_view_globals || !prey_drs_detected || (AlmostEqual(render_resolution.x, local_previous_render_resolution.x, 0.25f) && AlmostEqual(render_resolution.y, local_previous_render_resolution.y, 0.25f)));
#endif // DEVELOPMENT

        // Once we detect the user enabled DRS, we can't ever know it's been disabled because the game only occasionally drops to lower rendering resolutions, so we couldn't know if it was ever disabled
        if (prey_drs_active) {
            prey_drs_detected = true;

            float resolution_scale = render_resolution.y / output_resolution.y;
            // Lower the DLSS quality mode (which might introduce a stutter, or a slight blurring of the image as it resets the history),
            // but this will make DLSS not use DLAA and instead fall back on a quality mode that allows for a dynamic range of resolutions.
            // This isn't the exact rend resolution DLSS will be forced to use, but the center of a range it's gonna expect.
            // Unfortunately DLSS has a limited range of accepted resolutions per quality mode, and if you go beyond it, it fails to render (until in range again),
            // thus, we need to make sure the automatic DRS range of Prey is within the same range!
            // We couldn't change this resolution scale every frame as it's make DLSS stutter massively.
            // See CryEngine "osm_fbMinScale" cvar (config), that drives the min rend res scale, the DLSS rend scale should ideally be set to the same value, but it's fine if it's above it, given it's the target "average" dynamic resolution.
            // If CryEngine ever went below 50% render scale, we force DLSS into ultra performance mode (33%), as the range allowed by quality mode (67%) can't go below 50%. There will be a stutter (and history reset?) every time we swap back and forth, but at least it works...
            if (resolution_scale < 0.5f - FLT_EPSILON) {
#if 1 // Unfortunately no quality mode with a res scale below 0.5 supports dynamic resolution scaling, so we are forced to change the quality mode every frame or so (or at least, every time Prey changes DRS value, which might further slow down the DRS detection mechanism...)
                dlss_render_resolution_scale = resolution_scale;
#else // If we do this, DLSS would fail if any resolution that didn't exactly match 33% render scale was used by the game
                dlss_render_resolution_scale = 1.f / 3.f;
#endif
            }
            else {
                // This should pick quality or balanced mode, with a range from 100% to 50% resolution scale
                dlss_render_resolution_scale = 1.f / 1.5f;
            }
        }
        // Reset to DLAA and try again (once), given that we can't go from a 1/3 to a 1 rend scale (e.g. in case DRS was disabled in the menu)
        else if (dlss_suppressed && dlss_render_resolution_scale != 1.f) {
            dlss_render_resolution_scale = 1.f;
            dlss_suppressed = false;
        }

        // NOTE: we could just save the first one we found, it should always be jittered and "correct".
        projection_matrix = current_projection_matrix;
        nearest_projection_matrix = current_nearest_projection_matrix;

        const auto projection_jitters_copy = projection_jitters;

        // These are called "m_vProjMatrixSubPixoffset" in CryEngine.
        // The matrix is transposed so we flip the matrix x and y indices.
        projection_jitters.x = current_projection_matrix(0, 2);
        projection_jitters.y = current_projection_matrix(1, 2);

#if DEVELOPMENT
        ASSERT_ONCE(disable_taa_jitters || (projection_jitters_copy.x == 0 && projection_jitters_copy.y == 0) || (projection_jitters.x != 0 || projection_jitters.y != 0)); // Once we found jitters, we should never cache matrices that don't have jitters anymore
#endif

        bool prey_taa_enabled_copy = prey_taa_enabled;
        // This is a reliable check to tell whether TAA is enabled. Jitters are "never" zero if they are enabled:
        // they can be if we use the "srand" method, but it would happen one in a billion years;
        // they could also be zero with Halton if the frame index was reset to zero (it is every x frames), but that happens very rarely, and for one frame only.
        prey_taa_enabled = (std::abs(projection_jitters.x * render_resolution.x) >= 0.00075) || (std::abs(projection_jitters.y * render_resolution.y) >= 0.00075); //TODOFT: make calculations more accurate (the threshold)
#if DEVELOPMENT
        prey_taa_enabled = prey_taa_enabled || disable_taa_jitters;
#endif // DEVELOPMENT
        // Make sure that once we detect that TAA was active within a frame, then it should never be detected as off in the same frame (it would mean we are reading a bad cbuffer 13 that we should have discarded).
        // Ignore this when we have no shaders loaded as it would always break due to the "has_drawn_tonemapping" check failing.
        ASSERT_ONCE(cloned_pipeline_count == 0 || !found_per_view_globals || !prey_taa_enabled_copy || (prey_taa_enabled_copy == prey_taa_enabled));
        if (prey_taa_enabled_copy != prey_taa_enabled && has_drawn_main_post_processing_previous) { // TAA changed
            // Detect if TAA was ever detected as on/off/on or off/on/off over 3 frames, because if that was so, our jitter "length" detection method isn't solid enough and we should do more (or add more tolernace to it),
            // this might even happen every x hours once the randomization triggers specific enough values, though all TAA modes have a pretty short cycle with fixed jitters,
            // so it should either happen quickly or never.
            bool middle_value_different = (prey_taa_enabled == previous_prey_taa_enabled[0]) != (prey_taa_enabled == previous_prey_taa_enabled[1]);
            ASSERT_ONCE(!middle_value_different);
        }
        bool drew_dlss = cb_luma_frame_settings.DLSS; // If this was true, DLSS would have been enabled and probably drew
        prey_taa_detected = prey_taa_enabled || previous_prey_taa_enabled[0]; // This one has a two frames tolerance. We let it persist even if the game stopped drawing the 3D scene.
        cb_luma_frame_settings.DLSS = (dlss_sr && !dlss_suppressed && prey_taa_detected) ? 1 : 0; // No need for "s_mutex_reshade" here, given that they are generally only also changed by the user manually changing the settings in ImGUI, which runs at the very end of the frame
        if (cb_luma_frame_settings.DLSS && !drew_dlss) {
            // Reset DLSS history when we toggle DLSS on and off manually, or when the user in the game changes the AA mode,
            // otherwise the history from the last time DLSS was active will be kept (DLSS doesn't know time passes since it was last used).
            // We could also clear DLSS resources here when we know it's unused for a while, but it would possibly lead to stutters.
            force_reset_dlss = true;
        }

        //TODOFT: note that by default the game has a lod bias of 0 on most samplers (it seems),
        //but when enabling TAA (the "hidden" setting), some samplers go to -1, but while using SMAA 2TX, even if it includes TAA, that's not set (at least not the first time it's used, maybe they persist after first ever using TAA),
        //so should we bias by -1 again by default when the game uses TAA? It remains to be seen how many samplers they change when enabling TAA, if it's most of them, then we should avoid re-biasing by -1
        //by checking whether any SMAA edge AA shaders are running (the ones before TAA).
#if DEVELOPMENT
        if (!custom_texture_mip_lod_bias_offset)
#endif
        {
            std::shared_lock shared_lock_samplers(s_mutex_samplers);

            const auto prev_texture_mip_lod_bias_offset = texture_mip_lod_bias_offset;
            if (dlss_sr && !dlss_suppressed && prey_taa_detected && cloned_pipeline_count != 0) {
                texture_mip_lod_bias_offset = std::log2(render_resolution.y / output_resolution.y) - 1.f; // This results in -1 at output res
            }
            else {
                texture_mip_lod_bias_offset = prey_taa_detected ? -1.f : 0.f; // Reset to default value (it's determined by "r_AntialiasingTSAAMipBias" for TAA (doesn't apply to SMAA), which Prey defaults to 0 but Luma's configs set to -1)
            }
            const auto new_texture_mip_lod_bias_offset = texture_mip_lod_bias_offset;

            bool texture_mip_lod_bias_offset_changed = prev_texture_mip_lod_bias_offset != new_texture_mip_lod_bias_offset;
            //TODOFT: verify that this doesn't happen a billion times per frame with random resolutions that might be set by non primary render paths (e.g. shadow maps), if so, safely move this to compute once at the end of the frame (tested, it seems fine?)
            // Re-create all samplers immediately here instead of doing it at the end of the frame.
            // This allows us to avoid possible (but very unlikely) hitches that could happen if we re-created a new sampler for a new resolution later on when samplers descriptors are set.
            // It also allows us to use the right samplers for this frame's resolution.
            if (texture_mip_lod_bias_offset_changed) {
                for (auto& samplers_handle : custom_sampler_by_original_sampler) {
                    if (samplers_handle.second.contains(new_texture_mip_lod_bias_offset)) continue; // Skip "resolutions" that already got their custom samplers created
                    ID3D11SamplerState* native_sampler = reinterpret_cast<ID3D11SamplerState*>(samplers_handle.first);
                    D3D11_SAMPLER_DESC native_desc;
                    native_sampler->GetDesc(&native_desc);
                    shared_lock_samplers.unlock(); // This is fine!
                    {
                        std::unique_lock unique_lock_samplers(s_mutex_samplers);
                        samplers_handle.second[new_texture_mip_lod_bias_offset] = CreateCustomSampler(global_native_device, native_desc);
                    }
                    shared_lock_samplers.lock();
                }
            }
        }

        if (!has_drawn_main_post_processing_previous) {
            previous_render_resolution = render_resolution;
            previous_projection_jitters = projection_jitters;

            previous_projection_matrix = projection_matrix;
            previous_nearest_projection_matrix = nearest_projection_matrix;

            previous_prey_taa_enabled[0] = prey_taa_enabled;
            previous_prey_taa_enabled[1] = prey_taa_enabled;

            // Make sure that after a scene reset our resolution matches the swapchain one.
            // This also helps us catch edge cases where the first time the game uses this cbuffer it's not for the actual scene but for a side render,
            // like shadow projection maps, mirrors, etc etc.
            //ASSERT_ONCE(output_resolution_matches); // NOTE: this can't happen anymore as we wouldn't get here in that case
        }

        Matrix44_tpl<double> projection_matrix_native = current_projection_matrix.GetTransposed();

#if DEVELOPMENT && 0
        // We cast to double to caclulate in higher accuracy (given we don't have access to the origin projection matrix).
        // "(A * B).Transpose()" is equal to "B.Transpose() * A.Transpose()".
        Matrix44_tpl<double> projection_matrix_recalculated = (Matrix44_tpl<double>(cb_per_view_global.CV_ViewProjMatr) * Matrix44_tpl<double>(cb_per_view_global.CV_InvViewMatr)).GetTransposed();

        ASSERT_ONCE(mathMatrixAlmostEqual(projection_matrix_native, projection_matrix_recalculated, 0.001)); // NOTE: this happens, even if the results roughly seem identical (only 3 2 or so are different?)
        ASSERT_ONCE(AlmostEqual(projection_matrix_native(0, 1), 0.0, 0.0001)
            && AlmostEqual(projection_matrix_native(0, 2), 0.0, 0.0001)
            && AlmostEqual(projection_matrix_native(0, 3), 0.0, 0.0001)
            && AlmostEqual(projection_matrix_native(1, 0), 0.0, 0.0001)
            && AlmostEqual(projection_matrix_native(1, 2), 0.0, 0.0001)
            && AlmostEqual(projection_matrix_native(1, 3), 0.0, 0.0001)
            && AlmostEqual(projection_matrix_native(3, 0), 0.0, 0.0001)
            && AlmostEqual(projection_matrix_native(3, 1), 0.0, 0.0001)
            && AlmostEqual(projection_matrix_native(3, 3), 0.0, 0.0001)
        );

        // Clean up imprecisions
        projection_matrix_recalculated(0, 1) = 0;
        projection_matrix_recalculated(0, 2) = 0;
        projection_matrix_recalculated(0, 3) = 0;
        projection_matrix_recalculated(1, 0) = 0;
        projection_matrix_recalculated(1, 2) = 0;
        projection_matrix_recalculated(1, 3) = 0;
        projection_matrix_recalculated(3, 0) = 0;
        projection_matrix_recalculated(3, 1) = 0;
        projection_matrix_recalculated(3, 3) = 0;
#endif // DEVELOPMENT

        //Matrix44_tpl<double> projection_matrix_prev = (Matrix44_tpl<double>(current_projection_matrix) * Matrix44_tpl<double>(cb_per_view_global.CV_InvViewMatr)).GetTransposed();
        //Matrix44_tpl<double> projection_matrix_prev_real = (Matrix44_tpl<double>(cb_per_view_global_actual_previous.CV_ViewProjMatr) * Matrix44_tpl<double>(cb_per_view_global_actual_previous.CV_InvViewMatr)).GetTransposed();
        //Matrix44_tpl<double> projection_matrix_prev_real_fake = (Matrix44_tpl<double>(current_projection_matrix) * Matrix44_tpl<double>(cb_per_view_global_actual_previous.CV_InvViewMatr)).GetTransposed();

#if 0 // RANDOM TEST (matrix_calculation_mode == 3 looks good)
        // Supposedly from NDC to UV space
        Matrix44_tpl<double> mScaleBias1 = Matrix44_tpl<double>(
            0.5, 0, 0, 0,
            0, (matrix_calculation_mode == 1 || matrix_calculation_mode == 2) ? 0.5 : -0.5, 0, 0,
            0, 0, 1, 0,
            (matrix_calculation_mode == 2 || matrix_calculation_mode == 3) ? 1.0 : 0.5, (matrix_calculation_mode == 2 || matrix_calculation_mode == 3) ? 1.0 : 0.5, 0, 1);
        Matrix44_tpl<double> mScaleBias2 = Matrix44_tpl<double>(
            2.0, 0, 0, 0,
            0, (matrix_calculation_mode == 1 || matrix_calculation_mode == 2) ? 2.0 : -2.0, 0, 0,
            0, 0, 1, 0,
            (matrix_calculation_mode == 1 || matrix_calculation_mode == 2) ? 1.0 : -1.0, 1.0, 0, 1);
#else
        // NDC to UV space (y is flipped)
        const Matrix44_tpl<double> mScaleBias1 = Matrix44_tpl<double>(
            0.5, 0, 0, 0,
            0, -0.5, 0, 0,
            0, 0, 1, 0,
            0.5, 0.5, 0, 1);
        // UV to NDC space (y is flipped)
        const Matrix44_tpl<double> mScaleBias2 = Matrix44_tpl<double>(
            2.0, 0, 0, 0,
            0, -2.0, 0, 0,
            0, 0, 1, 0,
            -1.0, 1.0, 0, 1);
#endif

#if DEVELOPMENT && 0 // Not needed anymore, but here in case
        const Matrix44A mViewProjPrev = Matrix44_tpl<double>(cb_per_view_global_actual_previous.CV_ViewMatr.GetTransposed()) * projection_matrix_native * Matrix44_tpl<double>(mScaleBias1);
#endif // DEVELOPMENT

        Matrix44_tpl<double> previous_projection_matrix_native = Matrix44_tpl<double>(previous_projection_matrix.GetTransposed());
        if (matrix_calculation_mode_2 == 1) { // Flip jitters (somehow it works and fixes motion vectors generation, it's not 100% clear why)
            projection_matrix_native.m20 = -projection_matrix_native.m20;
            projection_matrix_native.m21 = -projection_matrix_native.m21;
            previous_projection_matrix_native.m20 = -previous_projection_matrix_native.m20;
            previous_projection_matrix_native.m21 = -previous_projection_matrix_native.m21;
        }
        else if (matrix_calculation_mode_2 == 2) {
            projection_matrix_native.m20 = -projection_matrix_native.m20;
            previous_projection_matrix_native.m20 = -previous_projection_matrix_native.m20;
        }
        else if (matrix_calculation_mode_2 == 3) {
            projection_matrix_native.m21 = -projection_matrix_native.m21;
            previous_projection_matrix_native.m21 = -previous_projection_matrix_native.m21;
        }
        Matrix44_tpl<double> mViewInv;
        mathMatrixLookAtInverse(mViewInv, Matrix44_tpl<double>(cb_per_view_global.CV_ViewMatr.GetTransposed()));
        Matrix44_tpl<double> mProjInv;
        mathMatrixPerspectiveFovInverse(mProjInv, previous_projection_matrix_native);
        Matrix44_tpl<double> mReprojection64 = mProjInv * mViewInv * Matrix44_tpl<double>(cb_per_view_global_actual_previous.CV_ViewMatr.GetTransposed()) * projection_matrix_native;
        // Not sure exactly what these do (NDC space?, depth buffer scaling?) but they work (anything else doesn't work, I've tried).
        mReprojection64 = mScaleBias2 * mReprojection64 * mScaleBias1;
        reprojection_matrix = mReprojection64.GetTransposed(); // Transpose it here so it's easier to read on the GPU (and consistent with the other matrices)

        found_per_view_globals = true;
    }

    return true;
}

void OnPushDescriptors(
    reshade::api::command_list* cmd_list,
    reshade::api::shader_stage stages,
    reshade::api::pipeline_layout layout,
    uint32_t param_index,
    const reshade::api::descriptor_table_update& update) {

    // OLD_GLOBAL_BUFFER_INTERCEPT_METHOD
    if (update.type == reshade::api::descriptor_type::constant_buffer
        && ((stages & (reshade::api::shader_stage::vertex | reshade::api::shader_stage::pixel | reshade::api::shader_stage::compute)) == 0 // CBuffer 13 is set to vertex and pixel and compute stages
            || update.binding != 13) // CBuffer 13 is the one we are looking for in Prey
        ) {
        return;
    }

    auto* device = cmd_list->get_device();
    ID3D11Device* native_device = (ID3D11Device*)(device->get_native());
    ID3D11DeviceContext* native_device_context = (ID3D11DeviceContext*)(cmd_list->get_native());

    switch (update.type) {
    default:
    case reshade::api::descriptor_type::shader_resource_view:
    case reshade::api::descriptor_type::buffer_unordered_access_view:
    case reshade::api::descriptor_type::unordered_access_view:
        break;
    case reshade::api::descriptor_type::sampler: {
        reshade::api::descriptor_table_update custom_update = update;
        bool any_modified = false;
        std::shared_lock shared_lock_samplers(s_mutex_samplers);
        for (uint32_t i = 0; i < update.count; i++) {
            const reshade::api::sampler& sampler = static_cast<const reshade::api::sampler*>(update.descriptors)[i];
            if (custom_sampler_by_original_sampler.contains(sampler.handle)) {
                ID3D11SamplerState* native_sampler = reinterpret_cast<ID3D11SamplerState*>(sampler.handle);
                // Create the version of this sampler to match the current mip lod bias
                if (!custom_sampler_by_original_sampler[sampler.handle].contains(texture_mip_lod_bias_offset)) {
                    D3D11_SAMPLER_DESC native_desc;
                    native_sampler->GetDesc(&native_desc);
                    const auto last_texture_mip_lod_bias_offset = texture_mip_lod_bias_offset;
                    shared_lock_samplers.unlock();
                    {
                        std::unique_lock unique_lock_samplers(s_mutex_samplers); // Only lock for reading if necessary. It doesn't matter if we released the shared lock above for a tiny amount of time, it's safe anyway
                        custom_sampler_by_original_sampler[sampler.handle][last_texture_mip_lod_bias_offset] = CreateCustomSampler((ID3D11Device*)device->get_native(), native_desc);
                    }
                    shared_lock_samplers.lock();
                }
                // Update the customized descriptor data
                uint64_t custom_sampler_handle = (uint64_t)(custom_sampler_by_original_sampler[sampler.handle][texture_mip_lod_bias_offset].get());
                if (custom_sampler_handle != 0) {
                    reshade::api::sampler& custom_sampler = ((reshade::api::sampler*)(custom_update.descriptors))[i];
                    custom_sampler.handle = custom_sampler_handle;
                    any_modified |= true;
                }
            }
            else {
#if DEVELOPMENT
                // If recursive (already cloned) sampler ptrs are set, it's because the game somehow got the pointers and is re-using them (?),
                // this seems to happen when we change the ImGui settings for samplers a lot and quickly.
                bool recursive_or_null = sampler.handle == 0;
                ID3D11SamplerState* native_sampler = reinterpret_cast<ID3D11SamplerState*>(sampler.handle);
                for (const auto& samplers_handle : custom_sampler_by_original_sampler) {
                    for (const auto& custom_sampler_handle : samplers_handle.second) {
                        recursive_or_null |= custom_sampler_handle.second.get() == native_sampler;
                    }
                }
                ASSERT_ONCE(recursive_or_null); // Shouldn't happen! (if we know the sampler set is "recursive", then we are good and don't need to replace this sampler again)
#if 0 //TODOFT: delete or restore in case the "recursive_or_null" assert above ever triggered
                if (sampler.handle != 0) {
                    ID3D11SamplerState* native_sampler = reinterpret_cast<ID3D11SamplerState*>(sampler.handle);
                    D3D11_SAMPLER_DESC native_desc;
                    native_sampler->GetDesc(&native_desc);
                    custom_sampler_by_original_sampler[sampler.handle] = CreateCustomSampler((ID3D11Device*)device->get_native(), native_desc);
                }
#endif
#endif // DEVELOPMENT
            }
        }

        if (any_modified) {
#if 1
            cmd_list->push_descriptors(stages, layout, param_index, custom_update);
#else // Old implementation to live replace samples (probably faster but a lot more annoying to write?
            // Note: we could call "cmd_list->push_descriptors()" but this is simpler as it doesn't require us to create a pipeline layout
            if ((stages & reshade::api::shader_stage::vertex) == reshade::api::shader_stage::vertex)
                native_device_context->VSSetSamplers(update.binding, update.count, sampler_ptrs);
            if ((stages & reshade::api::shader_stage::hull) == reshade::api::shader_stage::hull)
                native_device_context->HSSetSamplers(update.binding, update.count, sampler_ptrs);
            if ((stages & reshade::api::shader_stage::domain) == reshade::api::shader_stage::domain)
                native_device_context->DSSetSamplers(update.binding, update.count, sampler_ptrs);
            if ((stages & reshade::api::shader_stage::geometry) == reshade::api::shader_stage::geometry)
                native_device_context->GSSetSamplers(update.binding, update.count, sampler_ptrs);
            if ((stages & reshade::api::shader_stage::pixel) == reshade::api::shader_stage::pixel)
                native_device_context->PSSetSamplers(update.binding, update.count, sampler_ptrs);
            if ((stages & reshade::api::shader_stage::compute) == reshade::api::shader_stage::compute)
                native_device_context->CSSetSamplers(update.binding, update.count, sampler_ptrs);
#endif
        }
        break;
        }
    case reshade::api::descriptor_type::constant_buffer: {
        for (uint32_t i = 0; i < update.count; i++) {
            const reshade::api::buffer_range& buffer_range = static_cast<const reshade::api::buffer_range*>(update.descriptors)[i];
            if (buffer_range.buffer.handle == 0) {
                continue;
            }
            ID3D11Buffer* buffer = reinterpret_cast<ID3D11Buffer*>(buffer_range.buffer.handle);

#if DEVELOPMENT
            auto it = std::find(cb_per_view_global_buffer_pending_verification.begin(), cb_per_view_global_buffer_pending_verification.end(), buffer);
            if (it != cb_per_view_global_buffer_pending_verification.end()) {
                //std::erase(cb_per_view_global_buffer_pending_verification, buffer);
                //cb_per_view_global_buffer_pending_verification.pop_back();
                //cb_per_view_global_buffer_pending_verification.erase(it);
            }
#endif // DEVELOPMENT

            break; // There can't be anything after this in DX11
            }
        }
    }
}

void OnMapBufferRegion(reshade::api::device* device, reshade::api::resource resource, uint64_t offset, uint64_t size, reshade::api::map_access access, void** data) {
    ID3D11Device* native_device = (ID3D11Device*)(device->get_native());
    ID3D11Buffer* buffer = reinterpret_cast<ID3D11Buffer*>(resource.handle);
    // No need to convert to native DX11 flags
    if (access == reshade::api::map_access::write_only || access == reshade::api::map_access::write_discard) {
        D3D11_BUFFER_DESC buffer_desc;
        buffer->GetDesc(&buffer_desc);

        ASSERT_ONCE(buffer_desc.ByteWidth != sizeof(CBPerViewGlobal));
        // There seems to only ever be one buffer type of this size, but it's not guaranteed... //TODOFT: not true, there's more? Check the map data!
        // They seemengly all happen on the same thread.
        if (buffer_desc.ByteWidth == CBPerViewGlobal_buffer_size) {
            cb_per_view_global_buffer = buffer;
#if DEVELOPMENT
            if (std::find(cb_per_view_global_buffer_pending_verification.begin(), cb_per_view_global_buffer_pending_verification.end(), buffer) == cb_per_view_global_buffer_pending_verification.end()) {
                //cb_per_view_global_buffer_pending_verification.push_back(buffer);
            }
            // These are the classic "features" of cbuffer 13 (the one we are looking for), in case any of these were different, it could possibly mean we are looking at the wrong buffer here.
            ASSERT_ONCE(buffer_desc.Usage == D3D11_USAGE_DYNAMIC && buffer_desc.BindFlags == D3D11_BIND_CONSTANT_BUFFER && buffer_desc.CPUAccessFlags == D3D11_CPU_ACCESS_WRITE && buffer_desc.MiscFlags == 0 && buffer_desc.StructureByteStride == 0);
#endif // DEVELOPMENT
            ASSERT_ONCE(!cb_per_view_global_buffer_map_data);
            cb_per_view_global_buffer_map_data = *data;
        }
    }
}

void OnUnmapBufferRegion(reshade::api::device* device, reshade::api::resource resource) {
    ID3D11Device* native_device = (ID3D11Device*)(device->get_native());
    ID3D11Buffer* buffer = reinterpret_cast<ID3D11Buffer*>(resource.handle);
    bool is_global_cbuffer = cb_per_view_global_buffer != nullptr && cb_per_view_global_buffer == buffer;
    ASSERT_ONCE(!cb_per_view_global_buffer_map_data || is_global_cbuffer);
    if (is_global_cbuffer && cb_per_view_global_buffer_map_data != nullptr) {
        // The whole buffer size is theoretically "CBPerViewGlobal_buffer_size" but we actually don't have the data for the excessive (padding) bytes,
        // they are never read by shaders on the GPU anyway.
        char global_buffer_data[CBPerViewGlobal_buffer_size];
        std::memcpy(&global_buffer_data[0], cb_per_view_global_buffer_map_data, CBPerViewGlobal_buffer_size);
        if (UpdateGlobalCBuffer(&global_buffer_data[0])) {
            // Write back the cbuffer data after we have fixed it up (we always do!)
            std::memcpy(cb_per_view_global_buffer_map_data, &cb_per_view_global, sizeof(CBPerViewGlobal));
        }
        cb_per_view_global_buffer_map_data = nullptr;
        cb_per_view_global_buffer = nullptr; // No need to keep this cached
    }
}

bool OnCopyResource(reshade::api::command_list* cmd_list, reshade::api::resource source, reshade::api::resource dest) {
    ID3D11Resource* source_resource = reinterpret_cast<ID3D11Resource*>(source.handle);
    com_ptr<ID3D11Texture2D> source_resource_texture;
    HRESULT hr = source_resource->QueryInterface(&source_resource_texture);
    if (SUCCEEDED(hr)) {
        ID3D11Resource* target_resource = reinterpret_cast<ID3D11Resource*>(dest.handle);
        com_ptr<ID3D11Texture2D> target_resource_texture;
        hr = target_resource->QueryInterface(&target_resource_texture);
        if (SUCCEEDED(hr)) {
            D3D11_TEXTURE2D_DESC source_desc;
            D3D11_TEXTURE2D_DESC target_desc;
            source_resource_texture->GetDesc(&source_desc);
            target_resource_texture->GetDesc(&target_desc);

            if (source_desc.Width != target_desc.Width || source_desc.Height != target_desc.Height)
                return false;

            auto isUnorm8 = [](DXGI_FORMAT format) {
                switch (format) {
                case DXGI_FORMAT_R8G8B8A8_TYPELESS:
                case DXGI_FORMAT_R8G8B8A8_UNORM:
                case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
                case DXGI_FORMAT_B8G8R8A8_UNORM:
                case DXGI_FORMAT_B8G8R8X8_UNORM:
                case DXGI_FORMAT_B8G8R8A8_TYPELESS:
                case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
                case DXGI_FORMAT_B8G8R8X8_TYPELESS:
                case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
                    return true;
                }
                return false;
                };
            auto isFloat16 = [](DXGI_FORMAT format) {
                switch (format) {
                case DXGI_FORMAT_R16G16B16A16_TYPELESS:
                case DXGI_FORMAT_R16G16B16A16_FLOAT:
                    return true;
                }
                return false;
                };
            auto isFloat11 = [](DXGI_FORMAT format) {
                switch (format) {
                case DXGI_FORMAT_R11G11B10_FLOAT:
                    return true;
                }
                return false;
                };

            // If we detected incompatible formats that were likely caused by Luma upgrading texture formats (of render targets only...),
            // do the copy in shader
            if (((isUnorm8(target_desc.Format) || isFloat11(target_desc.Format)) && isFloat16(source_desc.Format))
                || ((isUnorm8(source_desc.Format) || isFloat11(source_desc.Format)) && isFloat16(target_desc.Format))) {
                const std::shared_lock lock(s_mutex_shader_objects);
                if (copy_vertex_shader == nullptr || copy_pixel_shader == nullptr) {
                    ASSERT_ONCE(false); // The custom shaders failed to be found (they have either been unloaded or failed to compile, or simply missing in the files)
                    // We can't continue, drawing with emtpy shaders would crash or skip the call
                    return false;
                }

                const auto* device = cmd_list->get_device();
                ID3D11Device* native_device = (ID3D11Device*)(device->get_native());
                ID3D11DeviceContext* native_device_context = (ID3D11DeviceContext*)(cmd_list->get_native());

                //
                // Prepare resources:
                //
                assert((source_desc.BindFlags & D3D11_BIND_SHADER_RESOURCE) != 0);
                com_ptr<ID3D11ShaderResourceView> source_resource_texture_view;
                D3D11_SHADER_RESOURCE_VIEW_DESC source_srv_desc;
                source_srv_desc.Format = source_desc.Format;
                // Redirect typeless and sRGB formats to classic UNORM, the "copy resource" functions wouldn't distinguish between these, as they copy by byte.
                switch (source_srv_desc.Format)
                {
                case DXGI_FORMAT_R8G8B8A8_TYPELESS:
                case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
                    source_srv_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                    break;
                case DXGI_FORMAT_B8G8R8A8_TYPELESS:
                case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
                    source_srv_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
                    break;
                case DXGI_FORMAT_B8G8R8X8_TYPELESS:
                case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
                    source_srv_desc.Format = DXGI_FORMAT_B8G8R8X8_UNORM;
                    break;
                case DXGI_FORMAT_R16G16B16A16_TYPELESS:
                    source_srv_desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
                    break;
                }
                source_srv_desc.ViewDimension = D3D11_SRV_DIMENSION::D3D11_SRV_DIMENSION_TEXTURE2D;
                source_srv_desc.Texture2D.MipLevels = 1;
                source_srv_desc.Texture2D.MostDetailedMip = 0;
                hr = native_device->CreateShaderResourceView(source_resource_texture.get(), &source_srv_desc, &source_resource_texture_view);
                assert(SUCCEEDED(hr));

                com_ptr<ID3D11Texture2D> proxy_target_resource_texture;
                // We need to make a double copy if the target texture isn't a render target, unfortunately (we could intercept its creation and add the flag, or replace any further usage in this frame by redirecting all pointers
                // to the new copy we made, but for now this works)
                // TODO: we could also check if the target texture supports UAV writes (unlikely) and fall back on a Copy Compute Shader instead of a Pixel Shader, to avoid two further texture copies.
                if ((target_desc.BindFlags & D3D11_BIND_RENDER_TARGET) == 0) {
                    // Create the persisting texture copy if necessary (if anything changed from the last copy).
                    // Theoretically all these textures have the same resolution as the screen so having one persisten texture should be ok.
                    // TODO: create more than one texture (one per format and one per resolution?) if ever needed
                    //TODOFT3: verify the above assumption, testing whether this texture is actually constantly re-created
                    D3D11_TEXTURE2D_DESC proxy_target_desc;
                    if (copy_texture.get() != nullptr) {
                        copy_texture->GetDesc(&proxy_target_desc);
                    }
                    if (copy_texture.get() == nullptr || proxy_target_desc.Width != target_desc.Width || proxy_target_desc.Height != target_desc.Height || proxy_target_desc.Format != target_desc.Format) {
                        proxy_target_desc = target_desc;
                        proxy_target_desc.BindFlags |= D3D11_BIND_RENDER_TARGET;
                        proxy_target_desc.BindFlags &= ~D3D11_BIND_SHADER_RESOURCE;
                        proxy_target_desc.BindFlags &= ~D3D11_BIND_UNORDERED_ACCESS;
                        proxy_target_desc.CPUAccessFlags = 0;
                        proxy_target_desc.Usage = D3D11_USAGE_DEFAULT;
                        copy_texture = nullptr;
                        hr = native_device->CreateTexture2D(&proxy_target_desc, nullptr, &copy_texture);
                        assert(SUCCEEDED(hr));
                    }
                    proxy_target_resource_texture = copy_texture;
                }
                else {
                    proxy_target_resource_texture = target_resource_texture;
                }

                com_ptr<ID3D11RenderTargetView> target_resource_texture_view;
                D3D11_RENDER_TARGET_VIEW_DESC target_rtv_desc;
                target_rtv_desc.Format = target_desc.Format;
                switch (target_rtv_desc.Format)
                {
                case DXGI_FORMAT_R8G8B8A8_TYPELESS:
                case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
                    target_rtv_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                    break;
                case DXGI_FORMAT_B8G8R8A8_TYPELESS:
                case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
                    target_rtv_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
                    break;
                case DXGI_FORMAT_B8G8R8X8_TYPELESS:
                case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
                    target_rtv_desc.Format = DXGI_FORMAT_B8G8R8X8_UNORM;
                    break;
                case DXGI_FORMAT_R16G16B16A16_TYPELESS:
                    target_rtv_desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
                    break;
                }
                target_rtv_desc.ViewDimension = D3D11_RTV_DIMENSION::D3D11_RTV_DIMENSION_TEXTURE2D;
                target_rtv_desc.Texture2D.MipSlice = 0;
                hr = native_device->CreateRenderTargetView(proxy_target_resource_texture.get(), &target_rtv_desc, &target_resource_texture_view);
                assert(SUCCEEDED(hr));

                DrawStateStack draw_state_stack;
                draw_state_stack.Cache(native_device_context);

                DrawCustomPixelShader(native_device_context, default_blend_state.get(), copy_vertex_shader.get(), copy_pixel_shader.get(), source_resource_texture_view.get(), target_resource_texture_view.get(), target_desc.Width, target_desc.Height, true);

                //
                // Copy our render target target resource into the non render target target resource if necessary:
                //
                if ((target_desc.BindFlags & D3D11_BIND_RENDER_TARGET) == 0) {
                    native_device_context->CopyResource(target_resource_texture.get(), proxy_target_resource_texture.get());
                }

                draw_state_stack.Restore(native_device_context);
                return true;
            }
        }
    }

    return false;
}

bool OnCopyTextureRegion(reshade::api::command_list* cmd_list, reshade::api::resource source, uint32_t source_subresource, const reshade::api::subresource_box* source_box, reshade::api::resource dest, uint32_t dest_subresource, const reshade::api::subresource_box* dest_box, reshade::api::filter_mode filter /*Unused in DX11*/) {
    if (source_subresource == 0 && dest_subresource == 0 && (!source_box || (source_box->left == 0 && source_box->top == 0)) && (!dest_box || (dest_box->left == 0 && dest_box->top == 0)) && (!dest_box || !source_box || (source_box->width() == dest_box->width() && source_box->height() == dest_box->height()))) {
        return OnCopyResource(cmd_list, source, dest);
    }
    return false;
}

void OnReshadePresent(reshade::api::effect_runtime* runtime) {
#if DEVELOPMENT
  if (trace_running) {
#if _DEBUG && LOG_VERBOSE
    reshade::log::message(reshade::log::level::info, "present()");
    reshade::log::message(reshade::log::level::info, "--- End Frame ---");
#endif
    const std::unique_lock lock_trace(s_mutex_trace);
    trace_count = trace_pipeline_handles.size();
    trace_running = false;
  } else if (trace_scheduled) {
    trace_scheduled = false;
    {
        const std::unique_lock lock_trace(s_mutex_trace);
        trace_pipeline_handles.clear();
        trace_pipeline_draws.clear();
        trace_threads.clear();
        trace_shader_hashes.clear();
    }
    trace_running = true;
#if _DEBUG && LOG_VERBOSE
    reshade::log::message(reshade::log::level::info, "--- Frame ---");
#endif
  }
#endif // DEVELOPMENT

  // Moved here so we can read it from ImGUI
  has_drawn_dlss_sr = false;

  //TODOFT: verify this delayed behaviour is actually ever needed and delete it if not (it might be useless!)
  {
    const std::unique_lock lock(s_mutex_generic);
    for (auto& pipeline_pair : pipeline_cache_by_pipeline_handle) {
      // Force waiting a frame as replacing the pipeline the first frame it was created could cause hangs
      pipeline_pair.second->ready_for_binding = true;
    }
  }

  // Dump new shaders (checking the "shaders_to_dump" count is theoretically not thread safe but it should work nonetheless as this is run every frame)
  if (auto_dump && !thread_auto_dumping_running && !shaders_to_dump.empty()) {
    if (thread_auto_dumping.joinable()) {
      thread_auto_dumping.join();
    }
    thread_auto_dumping_running = true;
    thread_auto_dumping = std::thread(AutoDumpShaders);
  }

  // Load new shaders (checking the "pipelines_to_reload" count is theoretically not thread safe but it should work nonetheless as this is run every frame)
  if (auto_load && !last_pressed_unload && !thread_auto_loading_running && !pipelines_to_reload.empty()) {
    if (thread_auto_loading.joinable()) {
      thread_auto_loading.join();
    }
    thread_auto_loading_running = true;
    thread_auto_loading = std::thread(AutoLoadShaders);
  }

  // Destroy the cloned pipelines in the following frame to avoid crashes
  {
    const std::unique_lock lock(s_mutex_generic);
    for (auto pair : pipelines_to_destroy) {
      pair.second->destroy_pipeline(reshade::api::pipeline{pair.first});
    }
    pipelines_to_destroy.clear();
  }

  if (needs_unload_shaders) {
    {
        const std::unique_lock lock(s_mutex_loading);
        shaders_compilation_errors.clear();
    }
    UnloadCustomShaders();
#if 1 // Optionally unload all custom shaders data
    {
        const std::unique_lock lock(s_mutex_loading);
        custom_shaders_cache.clear();
    }
#endif
    needs_unload_shaders = false;

#if !FORCE_KEEP_CUSTOM_SHADERS_LOADED
    // Unload customly created shader objects (from the shader code/binaries above),
    // to make sure they will re-create
    {
        const std::unique_lock lock(s_mutex_shader_objects);
        copy_vertex_shader = nullptr;
        copy_pixel_shader = nullptr;
        transfer_function_copy_pixel_shader = nullptr;
        draw_exposure_pixel_shader = nullptr;
    }
#endif
  }
  if (needs_load_shaders) {
    // Cache the defines at compilation time
    {
        const std::unique_lock lock(s_mutex_shader_defines);
        ShaderDefineData::OnCompilation(shader_defines_data);
        for (int i = 0; i < shader_defines_data.size(); i++) {
            shader_defines_data_index[string_view_crc32(std::string_view(shader_defines_data[i].compiled_data.GetName()))] = i;
        }
    }
    LoadCustomShaders();
    needs_load_shaders = false;
  }

  if (needs_live_reload_update) {
    ToggleLiveWatching();
    needs_live_reload_update = false;
  }
  CheckForLiveUpdate();
}

// Expects "s_mutex_dumping"
void DumpShader(uint32_t shader_hash, bool auto_detect_type = true) {
  auto dump_path = GetShaderPath();
  if (!std::filesystem::exists(dump_path)) {
    std::filesystem::create_directory(dump_path);
  }
  dump_path /= "dump";
  if (!std::filesystem::exists(dump_path)) {
    std::filesystem::create_directory(dump_path);
  }
  else if (!std::filesystem::is_directory(dump_path))
  {
    ASSERT_ONCE(false); // The target path is already taken by a file
    return;
  }

  wchar_t hash_string[11];
  swprintf_s(hash_string, L"0x%08X", shader_hash); // Note: "std::format("{:x}", shader_hash)" would be better and is already used elsewhere

  dump_path /= hash_string;

  auto* cached_shader = shader_cache.find(shader_hash)->second;

  // Automatically find the shader type and append it to the name (a bit hacky). This can make dumping relevantly slower.
  if (auto_detect_type) {
    if (cached_shader->disasm.empty()) {
      auto disasm_code = utils::shader::compiler::DisassembleShader(cached_shader->data, cached_shader->size);
      if (disasm_code.has_value()) {
        cached_shader->disasm.assign(disasm_code.value());
      } else {
        cached_shader->disasm.assign("DECOMPILATION FAILED");
      }
    }

    if (cached_shader->type == reshade::api::pipeline_subobject_type::vertex_shader
        || cached_shader->type == reshade::api::pipeline_subobject_type::pixel_shader
        || cached_shader->type == reshade::api::pipeline_subobject_type::compute_shader) {
      static const std::string template_vertex_shader_name = "vs_";
      static const std::string template_pixel_shader_name = "ps_";
      static const std::string template_compute_shader_name = "cs_";
      static const std::string template_shader_model_version_name = "x_x";

      std::string_view template_shader_name;
      switch (cached_shader->type) {
        case reshade::api::pipeline_subobject_type::vertex_shader: {
          template_shader_name = template_vertex_shader_name;
          break;
        }
        default:
        case reshade::api::pipeline_subobject_type::pixel_shader: {
          template_shader_name = template_pixel_shader_name;
          break;
        }
        case reshade::api::pipeline_subobject_type::compute_shader: {
          template_shader_name = template_compute_shader_name;
          break;
        }
      }
      for (char i = '0'; i <= '9'; i++) {
          std::string type_wildcard = std::string(template_shader_name) + i + '_';
          const auto type_index = cached_shader->disasm.find(type_wildcard);
          if (type_index != std::string::npos) {
              const std::string type = cached_shader->disasm.substr(type_index, template_shader_name.length() + template_shader_model_version_name.length());
              dump_path += ".";
              dump_path += type;
              break;
          }
      }
    }
  }

  dump_path += L".cso";

  try {
      std::ofstream file(dump_path, std::ios::binary);

      file.write(static_cast<const char*>(cached_shader->data), cached_shader->size);

      if (!dumped_shaders.contains(shader_hash)) {
        dumped_shaders.emplace(shader_hash);
      }
  }
  catch (const std::exception& e) {
  }
}

void AutoDumpShaders() {
  // Copy the "shaders_to_dump" so we don't have to lock "s_mutex_dumping" all the times
  std::unordered_set<uint32_t> shaders_to_dump_copy;
  {
    const std::lock_guard<std::recursive_mutex> lock_dumping(s_mutex_dumping);
    if (shaders_to_dump.empty()) {
      thread_auto_dumping_running = false;
      return;
    }
    shaders_to_dump_copy = shaders_to_dump;
    shaders_to_dump.clear();
  }
  for (auto shader_to_dump : shaders_to_dump_copy) {
    const std::lock_guard<std::recursive_mutex> lock_dumping(s_mutex_dumping);
    // Set this to true in case your old dumped shaders have bad naming (e.g. missing the "ps_5_0" appendix) and you want to replace them (on the next boot, the duplicate shaders with the shorter name will be deleted)
    constexpr bool force_redump_shaders = false;
    if (force_redump_shaders || !dumped_shaders.contains(shader_to_dump)) {
      DumpShader(shader_to_dump, true);
    }
  }
  thread_auto_dumping_running = false;
}

void AutoLoadShaders() {
  // Copy the "pipelines_to_reload_copy" so we don't have to lock "s_mutex_loading" all the times
  std::unordered_set<uint64_t> pipelines_to_reload_copy;
  {
    const std::unique_lock lock_loading(s_mutex_loading);
    if (pipelines_to_reload.empty()) {
      thread_auto_loading_running = false;
      return;
    }
    pipelines_to_reload_copy = pipelines_to_reload;
    pipelines_to_reload.clear();
  }
  LoadCustomShaders(pipelines_to_reload_copy, !precompile_custom_shaders);
  thread_auto_loading_running = false;
}

// @see https://pthom.github.io/imgui_manual_online/manual/imgui_manual.html
// This runs within the swapchain "Present()" function, and thus it's thread safe
void OnRegisterOverlay(reshade::api::effect_runtime* runtime) {
#if DEVELOPMENT
  const bool refresh_cloned_pipelines = cloned_pipelines_changed.exchange(false);

  if (ImGui::Button("Trace")) {
    trace_scheduled = true;
  }
  ImGui::SameLine();
  ImGui::Checkbox("List Unique Shaders Only", &trace_list_unique_shaders_only);

  ImGui::SameLine();
  ImGui::Checkbox("Ignore Vertex Shaders", &trace_ignore_vertex_shaders);

  ImGui::SameLine();
  ImGui::PushID("##DumpShaders");
  if (ImGui::Button(std::format("Dump Shaders ({})", shader_cache_count).c_str())) {
    const std::lock_guard<std::recursive_mutex> lock_dumping(s_mutex_dumping);
    // Force dump everything here
    for (auto shader : shader_cache) {
      DumpShader(shader.first, true);
    }
    shaders_to_dump.clear();
  }
  ImGui::PopID();

  ImGui::SameLine();
  ImGui::PushID("##AutoDumpCheckBox");
  if (ImGui::Checkbox("Auto Dump", &auto_dump)) {
    if (!auto_dump && thread_auto_dumping.joinable()) {
      thread_auto_dumping.join();
    }
  }
  ImGui::PopID();
#endif // DEVELOPMENT

#if DEVELOPMENT || TEST
  if (ImGui::Button(std::format("Unload Shaders ({})", cloned_pipeline_count).c_str())) {
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
      ImGui::SetTooltip("Unload all compiled and replaced shaders. The numbers shows how many shaders are being replaced at this moment in the game, from the custom loaded/compiled ones.");
    }
    needs_unload_shaders = true;
    last_pressed_unload = true;
#if 0  // Not necessary anymore with "last_pressed_unload"
    // For consistency, disable live reload and auto load, it makes no sense for them to be on if we have unloaded shaders
    if (live_reload) {
      live_reload = false;
      needs_live_reload_update = true;
    }
    if (auto_load) {
      auto_load = false;
      if (thread_auto_loading.joinable()) {
        thread_auto_loading.join();
      }
    }
#endif
    const std::unique_lock lock(s_mutex_loading);
    pipelines_to_reload.clear();
  }
  ImGui::SameLine();
#endif // DEVELOPMENT || TEST

  bool needs_compilation = false;
  {
    const std::shared_lock lock(s_mutex_shader_defines);
    needs_compilation = defines_need_recompilation;
    for (uint32_t i = 0; i < shader_defines_data.size() && !needs_compilation; i++) {
      needs_compilation |= shader_defines_data[i].NeedsCompilation();
    }
  }
#if !DEVELOPMENT && !TEST
  ImGui::BeginDisabled(!needs_compilation);
#endif
  static const std::string reload_shaders_button_title_error = std::string("Reload Shaders ") + std::string(ICON_FK_WARNING);
  static const std::string reload_shaders_button_title_outdated = std::string("Reload Shaders ") + std::string(ICON_FK_REFRESH);
  // We skip locking "s_mutex_loading" just to read the size of "shaders_compilation_errors".
  // We could maybe check "last_pressed_unload" instead of "cloned_pipeline_count", but that wouldn't work in case unloading shaders somehow failed.
  if (ImGui::Button(shaders_compilation_errors.empty() ? (cloned_pipeline_count ? (needs_compilation ? reload_shaders_button_title_outdated.c_str() : "Reload Shaders") : "Load Shaders") : reload_shaders_button_title_error.c_str())) {
    needs_unload_shaders = false;
    last_pressed_unload = false;
    needs_load_shaders = true;
    const std::unique_lock lock(s_mutex_loading);
    pipelines_to_reload.clear();
  }
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
      const std::shared_lock lock(s_mutex_loading);
#if !DEVELOPMENT
      if (shaders_compilation_errors.empty()) {
          ImGui::SetTooltip((cloned_pipeline_count && needs_compilation) ? "Shaders recompilation is needed for the changed settings to apply" : "(Re)Compiles shaders");
      }
      else
#endif
      if (!shaders_compilation_errors.empty()) {
          ImGui::SetTooltip(shaders_compilation_errors.c_str());
      }
  }
#if !DEVELOPMENT && !TEST
  ImGui::EndDisabled();
#endif
#if DEVELOPMENT || TEST
  ImGui::SameLine();
  if (ImGui::Button("Clean Shaders Cache")) {
      const std::unique_lock lock_loading(s_mutex_loading);
      CleanShadersCache();
      // Force recompile all shaders the next time
      for (const auto& custom_shader_pair : custom_shaders_cache) {
          if (custom_shader_pair.second) {
              custom_shader_pair.second->preprocessed_hash = 0;
          }
      }
  }
#endif

#if DEVELOPMENT
  ImGui::SameLine();
  ImGui::PushID("##AutoLoadCheckBox");
  if (ImGui::Checkbox("Auto Load", &auto_load)) {
    if (!auto_load && thread_auto_loading.joinable()) {
      thread_auto_loading.join();
    }
    const std::unique_lock lock(s_mutex_loading);
    pipelines_to_reload.clear();
  }
  ImGui::PopID();

  ImGui::SameLine();
  ImGui::PushID("##LiveReloadCheckBox");
  if (ImGui::Checkbox("Live Reload", &live_reload)) {
    needs_live_reload_update = true;
    const std::unique_lock lock(s_mutex_loading);
    pipelines_to_reload.clear();
  }
  ImGui::PopID();
#endif // DEVELOPMENT

  if (ImGui::BeginTabBar("##TabBar", ImGuiTabBarFlags_None)) {
#if DEVELOPMENT
    static int32_t selected_index = -1;
    bool changed_selected = false;
    ImGui::PushID("##ShadersTab");
    auto handle_shader_tab = ImGui::BeginTabItem(std::format("Traced Shaders ({})", trace_count).c_str());
    ImGui::PopID();
    if (handle_shader_tab) {
      if (ImGui::BeginChild("HashList", ImVec2(100, -FLT_MIN), ImGuiChildFlags_ResizeX)) {
        if (ImGui::BeginListBox("##HashesListbox", ImVec2(-FLT_MIN, -FLT_MIN))) {
          if (!trace_running) {
            const std::shared_lock lock(s_mutex_generic);
            for (auto index = 0; index < trace_count; index++) {
              auto pipeline_handle = trace_pipeline_handles.at(index);
              auto thread_id =  trace_threads.at(index)._Get_underlying_id(); // Possibly compiler dependent but whatever, cast to int alternatively
              auto draw_calls = trace_pipeline_draws.at(index);
              const bool is_selected = selected_index == index;
              // Note that the pipelines can be run more than once so this will return the first one matching (there's only one actually, we don't have separate settings for their running instance, as that's runtime stuff)
              const auto pipeline_pair = pipeline_cache_by_pipeline_handle.find(pipeline_handle);
              const bool is_valid = pipeline_pair != pipeline_cache_by_pipeline_handle.end() && pipeline_pair->second != nullptr;
              std::stringstream name;
              auto text_color = IM_COL32(255, 255, 255, 255);

              if (is_valid) {
                const auto pipeline = pipeline_pair->second;

                // Index - Thread ID - Draw Calls count - Shader Hash(es) - Shader Name
                name << std::setfill('0') << std::setw(3) << index << std::setw(0); // Fill up 3 slots for the index so the text is aligned
                name << " - " << thread_id;
                name << " - " << draw_calls << "x";
                for (auto shader_hash : pipeline->shader_hashes) {
                  name << " - " << PRINT_CRC32(shader_hash);
                }

                // Pick the default color by shader type
                if (pipeline->HasVertexShader()) {
                  text_color = IM_COL32(255, 255, 0, 255); // Yellow
                }
                else if (pipeline->HasComputeShader()) {
                  text_color = IM_COL32(128, 0, 128, 255); // Purple
                }

                const std::shared_lock lock_loading(s_mutex_loading);
                const auto custom_shader = !pipeline->shader_hashes.empty() ? custom_shaders_cache[pipeline->shader_hashes[0]] : nullptr;

                // Find if the shader has been modified
                if (pipeline->cloned) {
                  // For now just force picking the first shader linked to the pipeline, there should always only be one (?)
                  if (custom_shader != nullptr && custom_shader->is_hlsl && !custom_shader->file_path.empty()) {
                    name << "* - ";

                    auto filename_string = custom_shader->file_path.filename().string();
                    if (const auto hash_begin_index = filename_string.find("0x"); hash_begin_index != std::string::npos) {
                        filename_string.erase(hash_begin_index); // Start deleting from where the shader hash(es) begin (e.g. "0x12345678.xx_x_x.hlsl")
                    }
                    if (filename_string.ends_with("_") || filename_string.ends_with(".")) {
                      filename_string.erase(filename_string.length() - 1);
                    }

                    name << filename_string;
                  }
                  else {
                    name << "*";
                  }

                  text_color = IM_COL32(0, 255, 0, 255);
                }
                // Highlight loading error
                if (custom_shader != nullptr && !custom_shader->compilation_errors.empty()) {
                  text_color = custom_shader->compilation_error ? IM_COL32(255, 0, 0, 255) : IM_COL32(255, 165, 0, 255); // Red for Error, Orange for Warning
                }
              } else {
                text_color = IM_COL32(255, 0, 0, 255);
                name << " - ERROR: CANNOT FIND PIPELINE";
              }

              ImGui::PushStyleColor(ImGuiCol_Text, text_color);
              if (ImGui::Selectable(name.str().c_str(), is_selected)) {
                selected_index = index;
                changed_selected = true;
              }
              ImGui::PopStyleColor();

              if (is_selected) {
                ImGui::SetItemDefaultFocus();
              }
            }
          } else {
            selected_index = -1;
          }
          selected_index = min(selected_index, trace_count - 1); // Extra safety
          ImGui::EndListBox();
        }
      }
      ImGui::EndChild(); // HashList

      ImGui::SameLine();
      if (ImGui::BeginChild("##ShaderDetails", ImVec2(0, 0))) {
        ImGui::BeginDisabled(selected_index == -1);
        if (ImGui::BeginTabBar("##ShadersCodeTab", ImGuiTabBarFlags_None)) {
          const bool open_disassembly_tab_item = ImGui::BeginTabItem("Disassembly");
          static bool opened_disassembly_tab_item = false;
          if (open_disassembly_tab_item) {
            static std::string disasm_string;
            if (selected_index >= 0 && trace_pipeline_handles.size() >= selected_index + 1 && (changed_selected || opened_disassembly_tab_item != open_disassembly_tab_item)) {
              const auto pipeline_handle = trace_pipeline_handles.at(selected_index);
              const std::unique_lock lock(s_mutex_generic);
              if (auto pipeline_pair = pipeline_cache_by_pipeline_handle.find(pipeline_handle); pipeline_pair != pipeline_cache_by_pipeline_handle.end() && pipeline_pair->second != nullptr) {
                const std::lock_guard<std::recursive_mutex> lock_dumping(s_mutex_dumping);
                auto* cache = (!pipeline_pair->second->shader_hashes.empty() && shader_cache.contains(pipeline_pair->second->shader_hashes[0])) ? shader_cache[pipeline_pair->second->shader_hashes[0]] : nullptr;
                if (cache && cache->disasm.empty()) {
                  auto disasm_code = utils::shader::compiler::DisassembleShader(cache->data, cache->size);
                  if (disasm_code.has_value()) {
                    cache->disasm.assign(disasm_code.value());
                  } else {
                    cache->disasm.assign("DECOMPILATION FAILED");
                  }
                }
                disasm_string.assign(cache ? cache->disasm : "");
              }
            }

            if (ImGui::BeginChild("DisassemblyCode")) {
              ImGui::InputTextMultiline(
                  "##disassemblyCode",
                  const_cast<char*>(disasm_string.c_str()),
                  disasm_string.length(),
                  ImVec2(-FLT_MIN, -FLT_MIN),
                  ImGuiInputTextFlags_ReadOnly);
            }
            ImGui::EndChild();  // DisassemblyCode
            ImGui::EndTabItem();  // Disassembly
          }
          opened_disassembly_tab_item = open_disassembly_tab_item;

          ImGui::PushID("##LiveTabItem");
          const bool open_live_tab_item = ImGui::BeginTabItem("Live");
          ImGui::PopID();
          static bool opened_live_tab_item = false;
          if (open_live_tab_item) {
            static std::string hlsl_string;
            static bool hlsl_error = false;
            static bool hlsl_warning = false;
            if (selected_index >= 0 && trace_pipeline_handles.size() >= selected_index + 1 && (changed_selected || opened_live_tab_item != open_live_tab_item || refresh_cloned_pipelines)) {
              bool hlsl_set = false;
              auto pipeline_handle = trace_pipeline_handles.at(selected_index);

              const std::shared_lock lock(s_mutex_generic);
              if (auto pipeline_pair = pipeline_cache_by_pipeline_handle.find(pipeline_handle);
                  pipeline_pair != pipeline_cache_by_pipeline_handle.end() && pipeline_pair->second != nullptr) {

                const auto pipeline = pipeline_pair->second;
                const std::shared_lock lock_loading(s_mutex_loading);
                const auto custom_shader = !pipeline->shader_hashes.empty() ? custom_shaders_cache[pipeline->shader_hashes[0]] : nullptr;
                // If the custom shader has a compilation error, print that, otherwise read the file text
                if (custom_shader != nullptr && !custom_shader->compilation_errors.empty()) {
                  hlsl_string = custom_shader->compilation_errors;
                  hlsl_error = custom_shader->compilation_error;
                  hlsl_warning = !custom_shader->compilation_error;
                  hlsl_set = true;
                } else if (custom_shader != nullptr && custom_shader->is_hlsl && !custom_shader->file_path.empty()) {
                  auto result = ReadTextFile(custom_shader->file_path);
                  if (result.has_value()) {
                    hlsl_string.assign(result.value());
                    hlsl_error = false;
                    hlsl_warning = false;
                    hlsl_set = true;
                  } else {
                    hlsl_string.assign("FAILED TO READ FILE");
                    hlsl_error = true;
                    hlsl_warning = false;
                    hlsl_set = true;
                  }
                }
              }

              if (!hlsl_set) {
                hlsl_string.clear();
              }
            }
            opened_live_tab_item = open_live_tab_item;

            if (ImGui::BeginChild("LiveCode")) {
              ImGui::PushStyleColor(ImGuiCol_Text, hlsl_error ? IM_COL32(255, 0, 0, 255) : (hlsl_warning ? IM_COL32(255, 165, 0, 255) : IM_COL32(255, 255, 255, 255))); // Red for Error, Orange for Warning, White for the rest
              ImGui::InputTextMultiline(
                  "##liveCode",
                  const_cast<char*>(hlsl_string.c_str()),
                  hlsl_string.length(),
                  ImVec2(-FLT_MIN, -FLT_MIN));
              ImGui::PopStyleColor();
            }
            ImGui::EndChild();  // LiveCode
            ImGui::EndTabItem();  // Live
          }
          
          ImGui::PushID("##SettingsTabItem");
          const bool open_settings_tab_item = ImGui::BeginTabItem("Settings");
          ImGui::PopID();
          if (open_settings_tab_item) {
            if (selected_index >= 0 && trace_pipeline_handles.size() >= selected_index + 1) {
              auto pipeline_handle = trace_pipeline_handles.at(selected_index);
              const std::unique_lock lock(s_mutex_generic);
              if (auto pipeline_pair = pipeline_cache_by_pipeline_handle.find(pipeline_handle); pipeline_pair != pipeline_cache_by_pipeline_handle.end() && pipeline_pair->second != nullptr) {
                bool test_pipeline = pipeline_pair->second->test;
                if (ImGui::BeginChild("Settings")) {
                  if (!pipeline_pair->second->HasVertexShader()) {
                    ImGui::Checkbox("Test Shader (skips drawing, or draws black)", &test_pipeline);
                    if (pipeline_pair->second->cloned && ImGui::Button("Unload")) {
                        UnloadCustomShaders({ pipeline_handle }, false, false);
                    }
                    if (!pipeline_pair->second->cloned && ImGui::Button("Load")) {
                        LoadCustomShaders({ pipeline_handle }, false, true);
                    }
                    // NOTE: we could also have a "Compile" (single shader) button here but it's not necessary
                  }
                  if (pipeline_pair->second->HasPixelShader()) {
                      if (ImGui::Button("Debug Draw Shader")) {
                          debug_draw_pipeline = pipeline_pair->first; // Note: this is probably completely useless at the moment as we don't store the index of the pipeline instance the user had selected (e.g. "debug_draw_pipeline_target_instance")
                          debug_draw_shader_hash = pipeline_pair->second->shader_hashes[0];
                          std::string new_debug_draw_shader_hash_string = std::format("{:x}", debug_draw_shader_hash);
                          if (new_debug_draw_shader_hash_string.size() <= HASH_CHARACTERS_LENGTH)
                              strcpy(&debug_draw_shader_hash_string[0], new_debug_draw_shader_hash_string.c_str());
                          else
                              debug_draw_shader_hash_string[0] = 0;
                          debug_draw_texture = nullptr;
#if 0 // Let the user settings persist for now, it seems more intuitive
                          debug_draw_pipeline_target_instance = -1;
                          debug_draw_render_target_view = true;
#endif
                      }
                  }
                }
                ImGui::EndChild(); // Settings
                pipeline_pair->second->test = test_pipeline;
              }
            }

            ImGui::EndTabItem();  // Settings
          }

          ImGui::EndTabBar();  // ShadersCodeTab
        }
        ImGui::EndDisabled();
      }
      ImGui::EndChild();  // ShaderDetails
      ImGui::EndTabItem();  // Traced Shaders
    }
#endif // DEVELOPMENT

    if (ImGui::BeginTabItem("Settings")) {
        const std::unique_lock lock_reshade(s_mutex_reshade); // Lock the entire scope for extra safety, though we are mainly only interested in keeping "cb_luma_frame_settings" safe

        ImGui::BeginDisabled(!dlss_sr_supported);
        if (ImGui::Checkbox("DLSS Super Resolution", &dlss_sr)) {
            if (dlss_sr) dlss_suppressed = false;
            reshade::set_config_value(runtime, NAME, "DLSSSuperResolution", dlss_sr);
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("This replaces the game's native AA and dynamic resolution scaling implementations.\nSelect \"SMAA 2TX\" or \"TAA\" in the game's AA settings for DLSS/DLAA to engage (a tick will appear here when it's engaged and a warning if it failed).\n\nRequires compatible Nvidia GPUs.");
        }
        ImGui::SameLine();
        if (dlss_sr != true && dlss_sr_supported) {
            ImGui::PushID("DLSS Super Resolution Enabled");
            if (ImGui::SmallButton(ICON_FK_UNDO)) {
                dlss_sr = true;
            }
            ImGui::PopID();
        }
        else {
            if (dlss_sr && prey_taa_detected && cloned_pipeline_count != 0) {
                ImGui::PushID("DLSS Super Resolution Active");
                ImGui::BeginDisabled();
                ImGui::SmallButton((has_drawn_dlss_sr && !dlss_suppressed) ? ICON_FK_OK : ICON_FK_WARNING); // Show that DLSS is engaged (or failed) (this will reset if we open a menu etc)
                ImGui::EndDisabled();
                ImGui::PopID();
            }
            else {
                const auto& style = ImGui::GetStyle();
                ImVec2 size = ImGui::CalcTextSize(ICON_FK_UNDO);
                size.x += style.FramePadding.x;
                size.y += style.FramePadding.y;
                ImGui::InvisibleButton("", ImVec2(size.x, size.y));
            }
        }
        ImGui::EndDisabled();

        auto ChangeDisplayMode = [&](int display_mode, bool enable_hdr_on_display = true, IDXGISwapChain3* swapchain = nullptr) {
            reshade::set_config_value(runtime, NAME, "DisplayMode", display_mode);
            cb_luma_frame_settings.DisplayMode = display_mode;
            OnDisplayModeChanged();
            if (display_mode >= 1) {
                if (enable_hdr_on_display) {
                    SetHDREnabled(game_window);
                    bool dummy_bool;
                    IsHDRSupportedAndEnabled(game_window, dummy_bool, hdr_enabled_display, swapchain); // This should always succeed, so we don't fallback to SDR in case it didn't
                }
                if (reshade::get_config_value(runtime, NAME, "ScenePeakWhite", cb_luma_frame_settings.ScenePeakWhite) && cb_luma_frame_settings.ScenePeakWhite <= 0.f) {
                    cb_luma_frame_settings.ScenePeakWhite = default_user_peak_white;
                }
                reshade::get_config_value(runtime, NAME, "ScenePaperWhite", cb_luma_frame_settings.ScenePaperWhite);
                reshade::get_config_value(runtime, NAME, "UIPaperWhite", cb_luma_frame_settings.UIPaperWhite);
                // Align all the parameters for the SDR on HDR mode (the game paper white can still be changed)
                if (display_mode >= 2) {
                    // For now we don't default to 203 nits game paper white when changing to this mode
                    cb_luma_frame_settings.UIPaperWhite = cb_luma_frame_settings.ScenePaperWhite;
                    cb_luma_frame_settings.ScenePeakWhite = cb_luma_frame_settings.ScenePaperWhite; // No, we don't want "default_peak_white" here
                }
            }
            else {
                cb_luma_frame_settings.ScenePeakWhite = display_mode == 0 ? srgb_white_level : default_paper_white;
                cb_luma_frame_settings.ScenePaperWhite = display_mode == 0 ? srgb_white_level : default_paper_white;
                cb_luma_frame_settings.UIPaperWhite = display_mode == 0 ? srgb_white_level : default_paper_white;
            }
            };

        auto DrawScenePaperWhite = [&]() {
            if (ImGui::SliderFloat("Scene Paper White", &cb_luma_frame_settings.ScenePaperWhite, srgb_white_level, 500.f, "%.f")) {
                reshade::set_config_value(runtime, NAME, "ScenePaperWhite", cb_luma_frame_settings.ScenePaperWhite);
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                ImGui::SetTooltip("The \"average\" brightness of the game scene.\nHigher does not mean better, change this to your liking (especially if you struggle to read UI text), and don't get too close to the peak white.\nThe in game settings brightness is best left at default.");
            }
            ImGui::SameLine();
            if (cb_luma_frame_settings.ScenePaperWhite != default_paper_white) {
                ImGui::PushID("Scene Paper White");
                if (ImGui::SmallButton(ICON_FK_UNDO)) {
                    cb_luma_frame_settings.ScenePaperWhite = default_paper_white;
                    reshade::set_config_value(runtime, NAME, "ScenePaperWhite", cb_luma_frame_settings.ScenePaperWhite);
                }
                ImGui::PopID();
            }
            else {
                const auto& style = ImGui::GetStyle();
                ImVec2 size = ImGui::CalcTextSize(ICON_FK_UNDO);
                size.x += style.FramePadding.x;
                size.y += style.FramePadding.y;
                ImGui::InvisibleButton("", ImVec2(size.x, size.y));
            }
            };

        {
            const std::shared_lock lock(s_mutex_device);
            // Note: this is fast enough that we can check it every frame.
            // There's probably no need for thread safety checks on "native_swapchain3", but we have a mutex anyway.
            IsHDRSupportedAndEnabled(game_window, hdr_supported_display, hdr_enabled_display, native_swapchain3);
        }

        int display_mode = cb_luma_frame_settings.DisplayMode;
        int display_mode_max = 1;
        if (hdr_supported_display) {
#if DEVELOPMENT || TEST
            display_mode_max++; // Add "SDR in HDR for HDR" mode
#endif
        }
        const char* preset_strings[3] = {
            "SDR", // SDR (80 nits) on scRGB HDR for SDR (gamma sRGB, because Windows interprets scRGB as sRGB)
            "HDR",
            "SDR on HDR", // (Fake) SDR (baseline to 203 nits) on scRGB HDR for HDR (gamma 2.2) - Dev only, for quick comparisons
        };
        ImGui::BeginDisabled(!hdr_supported_display);
        if (ImGui::SliderInt("Display Mode", &display_mode, 0, display_mode_max, preset_strings[display_mode], ImGuiSliderFlags_NoInput)) {
            const std::shared_lock lock(s_mutex_device);
            ChangeDisplayMode(display_mode, true, native_swapchain3);
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("Display Mode. Greyed out if HDR is not supported.\nThe HDR display calibration (peak white brightness) is retrieved from the OS (Windows 11 HDR user calibration or display EDID),\nonly adjust it if necessary.\nIt's suggested to only play the game in SDR while the display is in SDR mode (with gamma 2.2, not sRGB) (avoid SDR mode in HDR).");
        }
        ImGui::SameLine();
        // Show a reset button to enable HDR in the game if we are playing SDR in HDR
        if ((display_mode == 0 && hdr_enabled_display) || (display_mode >= 1 && !hdr_enabled_display)) {
            ImGui::PushID("Display Mode");
            if (ImGui::SmallButton(ICON_FK_UNDO)) {
                display_mode = hdr_enabled_display ? 1 : 0;
                const std::shared_lock lock(s_mutex_device);
                ChangeDisplayMode(display_mode, false, native_swapchain3);
            }
            ImGui::PopID();
        }
        else {
            const auto& style = ImGui::GetStyle();
            ImVec2 size = ImGui::CalcTextSize(ICON_FK_UNDO);
            size.x += style.FramePadding.x;
            size.y += style.FramePadding.y;
            ImGui::InvisibleButton("", ImVec2(size.x, size.y));
        }

        if (display_mode == 1) {
            if (ImGui::SliderFloat("Scene Peak White", &cb_luma_frame_settings.ScenePeakWhite, 400.0, 10000.f, "%.f")) {
                if (cb_luma_frame_settings.ScenePeakWhite == default_user_peak_white) {
                    reshade::set_config_value(runtime, NAME, "ScenePeakWhite", 0.f);
                }
                else {
                    reshade::set_config_value(runtime, NAME, "ScenePeakWhite", cb_luma_frame_settings.ScenePeakWhite);
                }
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                ImGui::SetTooltip("Set this to the brightest nits value your display (TV/Monitor) can emit.\nDirectly calibrating in Windows is suggested.");
            }
            ImGui::SameLine();
            if (cb_luma_frame_settings.ScenePeakWhite != default_user_peak_white) {
                ImGui::PushID("Scene Peak White");
                if (ImGui::SmallButton(ICON_FK_UNDO)) {
                    cb_luma_frame_settings.ScenePeakWhite = default_user_peak_white;
                    reshade::set_config_value(runtime, NAME, "ScenePeakWhite", 0.f);
                }
                ImGui::PopID();
            }
            else {
                const auto& style = ImGui::GetStyle();
                ImVec2 size = ImGui::CalcTextSize(ICON_FK_UNDO);
                size.x += style.FramePadding.x;
                size.y += style.FramePadding.y;
                ImGui::InvisibleButton("", ImVec2(size.x, size.y));
            }
            DrawScenePaperWhite();
            constexpr bool supports_custom_ui_paper_white_scaling = true; // Currently all "post_process_space_define_index" modes support it (modify the tooltip otherwise)
            ImGui::BeginDisabled(!supports_custom_ui_paper_white_scaling);
            if (ImGui::SliderFloat("UI Paper White", supports_custom_ui_paper_white_scaling ? &cb_luma_frame_settings.UIPaperWhite : &cb_luma_frame_settings.ScenePaperWhite, srgb_white_level, 500.f, "%.f")) {
                reshade::set_config_value(runtime, NAME, "UIPaperWhite", cb_luma_frame_settings.UIPaperWhite);

// This is not safe to do, so let's rely on users manually setting this instead.
// Also note that this is a test implementation, it doesn't react to all places that change "cb_luma_frame_settings.UIPaperWhite", and does not restore the user original value on exit.
#if 0
                // This makes the game cursor have the same brightness as the game's UI
                SetSDRWhiteLevel(game_window, std::clamp(cb_luma_frame_settings.UIPaperWhite, 80.f, 480.f));
#endif
            }
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                ImGui::SetTooltip("The peak brightness of the User Interface (with the exception of the 2D cursor, which is driven by the Windows SDR White Level).\nHigher does not mean better, change this to your liking.");
            }
            ImGui::SameLine();
            if (cb_luma_frame_settings.UIPaperWhite != default_paper_white) {
                ImGui::PushID("UI Paper White");
                if (ImGui::SmallButton(ICON_FK_UNDO)) {
                    cb_luma_frame_settings.UIPaperWhite = default_paper_white;
                    reshade::set_config_value(runtime, NAME, "UIPaperWhite", cb_luma_frame_settings.UIPaperWhite);
                }
                ImGui::PopID();
            }
            else {
                const auto& style = ImGui::GetStyle();
                ImVec2 size = ImGui::CalcTextSize(ICON_FK_UNDO);
                size.x += style.FramePadding.x;
                size.y += style.FramePadding.y;
                ImGui::InvisibleButton("", ImVec2(size.x, size.y));
            }

            if (ImGui::Checkbox("Tonemap UI Background", &tonemap_ui_background)) {
                reshade::set_config_value(runtime, NAME, "TonemapUIBackground", tonemap_ui_background);
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                ImGui::SetTooltip("This can help to keep the UI readable when there's bright backgrounds behind it.");
            }
            ImGui::SameLine();
            if (tonemap_ui_background != true) {
                ImGui::PushID("Tonemap UI Background");
                if (ImGui::SmallButton(ICON_FK_UNDO)) {
                    tonemap_ui_background = true;
                    reshade::set_config_value(runtime, NAME, "TonemapUIBackground", tonemap_ui_background);
                }
                ImGui::PopID();
            }
            else {
                const auto& style = ImGui::GetStyle();
                ImVec2 size = ImGui::CalcTextSize(ICON_FK_UNDO);
                size.x += style.FramePadding.x;
                size.y += style.FramePadding.y;
                ImGui::InvisibleButton("", ImVec2(size.x, size.y));
            }
        }
        else if (display_mode >= 2) {
            DrawScenePaperWhite();
            cb_luma_frame_settings.UIPaperWhite = cb_luma_frame_settings.ScenePaperWhite;
            cb_luma_frame_settings.ScenePeakWhite = cb_luma_frame_settings.ScenePaperWhite;
        }

#if DEVELOPMENT
        ImGui::NewLine();
        ImGui::Text("Developer Settings: ", "");

        ImGui::NewLine();
        static std::string DevSettingsNames[LumaFrameDevSettings::SettingsNum];
        for (size_t i = 0; i < LumaFrameDevSettings::SettingsNum; i++) {
            // These strings need to persist
            if (DevSettingsNames[i].empty()) {
                DevSettingsNames[i] = "Developer Setting " + std::to_string(i + 1);
            }
            float& value = cb_luma_frame_settings.DevSettings[i];
            float& min_value = cb_luma_frame_dev_settings_min_value[i];
            float& max_value = cb_luma_frame_dev_settings_max_value[i];
            float& default_value = cb_luma_frame_dev_settings_default_value[i];
            // Note: this will "fail" if we named two devs settings with the same name!
            ImGui::SliderFloat(cb_luma_frame_dev_settings_names[i].empty() ? DevSettingsNames[i].c_str() : cb_luma_frame_dev_settings_names[i].c_str(), &value, min_value, max_value);
            ImGui::SameLine();
            if (value != default_value) {
                ImGui::PushID(DevSettingsNames[i].c_str());
                if (ImGui::SmallButton(ICON_FK_UNDO)) {
                    value = default_value;
                }
                ImGui::PopID();
            }
            else {
                const auto& style = ImGui::GetStyle();
                ImVec2 size = ImGui::CalcTextSize(ICON_FK_UNDO);
                size.x += style.FramePadding.x;
                size.y += style.FramePadding.y;
                ImGui::InvisibleButton("", ImVec2(size.x, size.y));
            }
        }

        ImGui::NewLine();
        ImGui::SliderInt("Tank Performance (Per Frame Sleep MS)", &frame_sleep_ms, 0, 100);

        ImGui::NewLine();
        ImGuiInputTextFlags text_flags = ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_CharsNoBlank | ImGuiInputTextFlags_AlwaysOverwrite | ImGuiInputTextFlags_NoUndoRedo;
        if (ImGui::InputTextWithHint("Debug Draw Shader Hash", "12345678", debug_draw_shader_hash_string, HASH_CHARACTERS_LENGTH + 1, text_flags)) {
            try {
                if (strlen(debug_draw_shader_hash_string) != HASH_CHARACTERS_LENGTH) {
                    throw std::invalid_argument("Shader Hash has invalid length");
                }
                debug_draw_shader_hash = std::stoul(&debug_draw_shader_hash_string[0], nullptr, 16);
            }
            catch (const std::exception& e) {
                debug_draw_shader_hash = 0;
            }
            // Keep the pipeline ptr if we are simply clearing the hash
            if (debug_draw_shader_hash != 0) {
                debug_draw_pipeline = 0;
            }
            debug_draw_texture = nullptr;
        }
        ImGui::SameLine();
        if (debug_draw_shader_hash != 0 || debug_draw_pipeline != 0) {
            ImGui::PushID("Debug Draw");
            if (ImGui::SmallButton(ICON_FK_UNDO)) {
                debug_draw_shader_hash_string[0] = 0;
                debug_draw_shader_hash = 0;
                debug_draw_pipeline = 0;
                debug_draw_texture = nullptr;
            }
            ImGui::PopID();
        }
        else {
            const auto& style = ImGui::GetStyle();
            ImVec2 size = ImGui::CalcTextSize(ICON_FK_UNDO);
            size.x += style.FramePadding.x;
            size.y += style.FramePadding.y;
            ImGui::InvisibleButton("", ImVec2(size.x, size.y));
        }
        if (debug_draw_shader_hash != 0 || debug_draw_pipeline != 0) {
            const char* debug_draw_mode_strings[2] = {
                "Render Target",
                "Shader Resource",
            };
            static int debug_draw_mode;
            if (ImGui::SliderInt("Debug Draw Mode", &debug_draw_mode, 0, 1, debug_draw_mode_strings[debug_draw_mode], ImGuiSliderFlags_NoInput)) {
                debug_draw_render_target_view = debug_draw_mode <= 0;
                debug_draw_view_index = 0;
            }
            if (debug_draw_render_target_view) {
                ImGui::SliderInt("Debug Draw: Render Target Index", &debug_draw_view_index, 0, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT - 1);
            }
            else {
                ImGui::SliderInt("Debug Draw: Pixel Shader Resource Index", &debug_draw_view_index, 0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT - 1);
            }
            ImGui::SliderInt("Debug Draw: Pipeline Instance", &debug_draw_pipeline_target_instance, -1, 100); // In case the same pipeline was run more than once by the game, we can pick one to print
            bool debug_draw_fullscreen = (debug_draw_options & (uint32_t)DebugDrawTextureOptionsMask::Fullscreen) != 0;
            bool debug_draw_rend_res_scale = (debug_draw_options & (uint32_t)DebugDrawTextureOptionsMask::RenderResolutionScale) != 0;
            bool debug_draw_show_alpha = (debug_draw_options & (uint32_t)DebugDrawTextureOptionsMask::ShowAlpha) != 0;
            bool debug_draw_premultiply_alpha = (debug_draw_options & (uint32_t)DebugDrawTextureOptionsMask::PreMultiplyAlpha) != 0;
            bool debug_draw_invert_colors = (debug_draw_options & (uint32_t)DebugDrawTextureOptionsMask::InvertColors) != 0;
            bool debug_draw_linear_to_gamma = (debug_draw_options & (uint32_t)DebugDrawTextureOptionsMask::LinearToGamma) != 0;
            bool debug_draw_gamma_to_linear = (debug_draw_options & (uint32_t)DebugDrawTextureOptionsMask::GammaToLinear) != 0;
            if (ImGui::Checkbox("Debug Draw Options: Fullscreen", &debug_draw_fullscreen)) {
                if (debug_draw_fullscreen) {
                    debug_draw_options |= (uint32_t)DebugDrawTextureOptionsMask::Fullscreen;
                }
                else {
                    debug_draw_options &= ~(uint32_t)DebugDrawTextureOptionsMask::Fullscreen;
                }
            }
            if (ImGui::Checkbox("Debug Draw Options: Render Resolution Scale", &debug_draw_rend_res_scale)) {
                if (debug_draw_rend_res_scale) {
                    debug_draw_options |= (uint32_t)DebugDrawTextureOptionsMask::RenderResolutionScale;
                }
                else {
                    debug_draw_options &= ~(uint32_t)DebugDrawTextureOptionsMask::RenderResolutionScale;
                }
            }
            if (ImGui::Checkbox("Debug Draw Options: Show Alpha", &debug_draw_show_alpha)) {
                if (debug_draw_show_alpha) {
                    debug_draw_options |= (uint32_t)DebugDrawTextureOptionsMask::ShowAlpha;
                }
                else {
                    debug_draw_options &= ~(uint32_t)DebugDrawTextureOptionsMask::ShowAlpha;
                }
            }
            if (ImGui::Checkbox("Debug Draw Options: Premultiply Alpha", &debug_draw_premultiply_alpha)) {
                if (debug_draw_premultiply_alpha) {
                    debug_draw_options |= (uint32_t)DebugDrawTextureOptionsMask::PreMultiplyAlpha;
                }
                else {
                    debug_draw_options &= ~(uint32_t)DebugDrawTextureOptionsMask::PreMultiplyAlpha;
                }
            }
            if (ImGui::Checkbox("Debug Draw Options: Invert Colors", &debug_draw_invert_colors)) {
                if (debug_draw_invert_colors) {
                    debug_draw_options |= (uint32_t)DebugDrawTextureOptionsMask::InvertColors;
                }
                else {
                    debug_draw_options &= ~(uint32_t)DebugDrawTextureOptionsMask::InvertColors;
                }
            }
            if (ImGui::Checkbox("Debug Draw Options: Linear to Gamma", &debug_draw_linear_to_gamma)) {
                if (debug_draw_linear_to_gamma) {
                    debug_draw_options |= (uint32_t)DebugDrawTextureOptionsMask::LinearToGamma;
                }
                else {
                    debug_draw_options &= ~(uint32_t)DebugDrawTextureOptionsMask::LinearToGamma;
                }
            }
            if (ImGui::Checkbox("Debug Draw Options: Gamma to Linear", &debug_draw_gamma_to_linear)) {
                if (debug_draw_gamma_to_linear) {
                    debug_draw_options |= (uint32_t)DebugDrawTextureOptionsMask::GammaToLinear;
                }
                else {
                    debug_draw_options &= ~(uint32_t)DebugDrawTextureOptionsMask::GammaToLinear;
                }
            }
            ImGui::Checkbox("Debug Draw: Auto Clear Texture", &debug_draw_auto_clear_texture); // Is it persistent or not (in case the target texture stopped being found on newer frames). We could also "freeze" it and stop updating it, but we don't need that for now.
        }

        ImGui::NewLine();
        ImGui::SliderFloat("DLSS Custom Exposure", &dlss_custom_exposure, 0.0, 10.0);
        ImGui::SliderFloat("DLSS Custom Pre-Exposure", &dlss_custom_pre_exposure, 0.0, 10.0);
        ImGui::SliderInt("DLSS Halton Jitter Phases", &force_taa_jitter_phases, 0, 64);
        
        ImGui::NewLine();
        ImGui::SliderInt("Fix Motion Vectors Generation Projection Matrix", &fix_prev_matrix_mode, 0, 5);
        //ImGui::SliderInt("matrix_calculation_mode", &matrix_calculation_mode, 0, 3); // Disabled
        ImGui::SliderInt("matrix_calculation_mode_2", &matrix_calculation_mode_2, 0, 4);
        if (ImGui::Checkbox("Disable Camera Jitters", &disable_taa_jitters)) {
            if (!disable_taa_jitters && force_taa_jitter_phases == 1) {
                force_taa_jitter_phases = 0;
            }
        }
        if (disable_taa_jitters) {
            force_taa_jitter_phases = 1; // Having 1 phase means there's no jitters (or well, they might not be centered in the pixel, but they are fixed over time)
        }

        ImGui::NewLine();
        bool samplers_changed = ImGui::SliderInt("Texture Samplers Upgrade Mode", &samplers_upgrade_mode, 0, 7);
        samplers_changed |= ImGui::SliderInt("Texture Samplers Upgrade Mode - 2", &samplers_upgrade_mode_2, 0, 6);
        ImGui::Checkbox("Custom Texture Samplers Mip LOD Bias", &custom_texture_mip_lod_bias_offset);
        if (samplers_upgrade_mode > 0 && custom_texture_mip_lod_bias_offset) {
            const std::unique_lock lock_samplers(s_mutex_samplers);
            samplers_changed |= ImGui::SliderFloat("Texture Samplers Mip LOD Bias", &texture_mip_lod_bias_offset, -8.f, +8.f);
        }
        if (samplers_changed) {
            const std::unique_lock lock_samplers(s_mutex_samplers);
            for (auto& samplers_handle : custom_sampler_by_original_sampler) {
                ID3D11SamplerState* native_sampler = reinterpret_cast<ID3D11SamplerState*>(samplers_handle.first);
                D3D11_SAMPLER_DESC native_desc;
                native_sampler->GetDesc(&native_desc);
                samplers_handle.second[texture_mip_lod_bias_offset] = CreateCustomSampler((ID3D11Device*)runtime->get_device()->get_native(), native_desc);
            }
        }
#endif // DEVELOPMENT

        ImGui::EndTabItem();
    }

    if (ImGui::BeginTabItem("Advanced Settings")) {
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
          ImGui::SetTooltip("Shader Defines: reload shaders after changing these for the changes to apply (and save).\nSome settings are only editable in debug modes, and only apply if the \"DEVELOPMENT\" flag is turned on.\nDo not change unless you know what you are doing.");
      }

      const std::unique_lock lock_shader_defines(s_mutex_shader_defines);

      // Show reset button
      {
          bool is_default = true;
          for (uint32_t i = 0; i < shader_defines_data.size() && is_default; i++) {
              is_default = shader_defines_data[i].IsDefault() && !shader_defines_data[i].IsCustom();
          }
          ImGui::BeginDisabled(is_default);
          ImGui::PushID("Advanced Settings: Reset Defines");
          static const std::string reset_button_title = std::string(ICON_FK_UNDO) + std::string(" Reset");
          if (ImGui::Button(reset_button_title.c_str())) {
              // Remove all newly added settings
              ShaderDefineData::RemoveCustomData(shader_defines_data);

              // Reset the rest to default
              ShaderDefineData::Reset(shader_defines_data);
          }
          if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
              ImGui::SetTooltip("Resets the defines to their default value");
          }
          ImGui::PopID();
          ImGui::EndDisabled();
      }
      // Show restore button (basically "undo")
      {
          bool needs_compilation = defines_need_recompilation;
          for (uint32_t i = 0; i < shader_defines_data.size() && !needs_compilation; i++) {
              needs_compilation |= shader_defines_data[i].NeedsCompilation();
          }
          ImGui::BeginDisabled(!needs_compilation);
          ImGui::SameLine();
          ImGui::PushID("Advanced Settings: Restore Defines");
          static const std::string restore_button_title = std::string(ICON_FK_UNDO) + std::string(" Restore");
          if (ImGui::Button(restore_button_title.c_str())) {
              ShaderDefineData::Restore(shader_defines_data);
          }
          if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
              ImGui::SetTooltip("Restores the defines to the last compiled values, undoing any changes that haven't been applied");
          }
          ImGui::PopID();
          ImGui::EndDisabled();
      }

#if DEVELOPMENT || TEST
      ImGui::BeginDisabled(shader_defines_data.empty() || !shader_defines_data[shader_defines_data.size() - 1].IsCustom());
      ImGui::SameLine();
      ImGui::PushID("Advanced Settings: Remove Define");
      static const std::string remove_button_title = std::string(ICON_FK_MINUS) + std::string(" Remove");
      if (ImGui::Button(remove_button_title.c_str())) {
          shader_defines_data.pop_back();
          defines_count--;
      }
      ImGui::PopID();
      ImGui::EndDisabled();

      ImGui::BeginDisabled(shader_defines_data.size() >= MAX_SHADER_DEFINES);
      ImGui::SameLine();
      ImGui::PushID("Advanced Settings: Add Define");
      static const std::string add_button_title = std::string(ICON_FK_PLUS) + std::string(" Add");
      if (ImGui::Button(add_button_title.c_str())) {
          // We don't default the value to 0 here, we leave it blank
          shader_defines_data.emplace_back();
      }
      ImGui::PopID();
      ImGui::EndDisabled();
#endif

#if 0 // We simply add a "*" next to the reload shaders button now instead
      // Show when the defines are "dirty" (shaders need recompile)
      {
          bool needs_compilation = defines_need_recompilation;
          for (uint32_t i = 0; i < shader_defines_data.size() && !needs_compilation; i++) {
              needs_compilation |= shader_defines_data[i].NeedsCompilation();
          }
          if (needs_compilation) {
              ImGui::SameLine();
              ImGui::PushID("Advanced Settings: Defines Dirty");
              ImGui::BeginDisabled();
              ImGui::SmallButton(ICON_FK_REFRESH); // Note: we don't want to modify "needs_load_shaders" here, there's another button for that
              if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                  ImGui::SetTooltip("Recompile shaders needed to apply the changed settings");
              }
              ImGui::EndDisabled();
              ImGui::PopID();
          }
      }
#endif

      uint8_t longest_shader_define_name_length = 0;
#if 1 // Enables automatic sizing
      for (uint32_t i = 0; i < shader_defines_data.size(); i++) {
          longest_shader_define_name_length = max(longest_shader_define_name_length, strlen(shader_defines_data[i].editable_data.GetName()));
      }
      longest_shader_define_name_length += 1; // Add an extra space to avoid it looking too crammed and lagging by one frame
#else
      uint8_t longest_shader_define_name_length = SHADER_DEFINES_MAX_NAME_LENGTH - 1; // Remove the null termination
#endif
      for (uint32_t i = 0; i < shader_defines_data.size(); i++) {
        // Don't render empty text fields that couldn't be filled due to them not being editable
        bool disabled = false;
        if (!shader_defines_data[i].IsNameEditable() && !shader_defines_data[i].IsValueEditable()) {
#if !DEVELOPMENT && !TEST
            if (shader_defines_data[i].IsCustom()) {
                continue;
            }
#endif
            disabled = true;
            ImGui::BeginDisabled();
        }

        bool show_tooltip = false;

        ImGui::PushID(shader_defines_data[i].name_hint.data());
        ImGuiInputTextFlags flags = ImGuiInputTextFlags_CharsNoBlank;
        if (!shader_defines_data[i].IsNameEditable()) {
            flags |= ImGuiInputTextFlags_ReadOnly;
        }
        // All characters should (roughly) have the same length
        ImGui::SetNextItemWidth(ImGui::CalcTextSize("0").x * longest_shader_define_name_length);
        // ImGUI doesn't work with std::string data, it seems to need c style char arrays.
        bool name_edited = ImGui::InputTextWithHint("", shader_defines_data[i].name_hint.data(), shader_defines_data[i].editable_data.GetName(), std::size(shader_defines_data[i].editable_data.name) /*SHADER_DEFINES_MAX_NAME_LENGTH*/, flags);
        show_tooltip |= ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
        ImGui::PopID();

        // TODO: fix this, it doesn't seem to work
        auto ModulateValueText = [](ImGuiInputTextCallbackData* data) -> int {
#if 0
            if (data->EventFlag == ImGuiInputTextFlags_CallbackEdit) {
                if (data->Buf[0] == '\0') {
                    // SHADER_DEFINES_MAX_VALUE_LENGTH
#if 0 // Better implementation (actually resets to default when the text was cleaned (invalid value)) (space and - can also be currently written to in the value text field)
                    data->Buf[0] = shader_defines_data[i].default_data.value[0];
                    data->Buf[1] = shader_defines_data[i].default_data.value[1];
#else
                    data->Buf[0] == '0';
                    data->Buf[1] == '\0';
#endif
                    data->BufDirty = true;
                };
            };
#endif
            return 0;
            };

        ImGui::SameLine();
        ImGui::PushID(shader_defines_data[i].value_hint.data());
        flags = ImGuiInputTextFlags_CharsDecimal | ImGuiInputTextFlags_CharsNoBlank | ImGuiInputTextFlags_AlwaysOverwrite | ImGuiInputTextFlags_AutoSelectAll | ImGuiInputTextFlags_NoUndoRedo | ImGuiInputTextFlags_CallbackEdit;
        if (!shader_defines_data[i].IsValueEditable()) {
            flags |= ImGuiInputTextFlags_ReadOnly;
        }
        ImGui::SetNextItemWidth(ImGui::CalcTextSize("00").x);
        bool value_edited = ImGui::InputTextWithHint("", shader_defines_data[i].value_hint.data(), shader_defines_data[i].editable_data.GetValue(), std::size(shader_defines_data[i].editable_data.value) /*SHADER_DEFINES_MAX_VALUE_LENGTH*/, flags, ModulateValueText);
        show_tooltip |= ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
        // Avoid having empty values unless the default value also was empty. This is a worse implementation of the "ImGuiInputTextFlags_CallbackEdit" above, which we can't get to work.
        // If the value was empty to begin with, we leave it, to avoid confusion.
        if (value_edited && shader_defines_data[i].IsValueEmpty()) {
            // SHADER_DEFINES_MAX_VALUE_LENGTH
            shader_defines_data[i].editable_data.value[0] = shader_defines_data[i].default_data.value[0];
            shader_defines_data[i].editable_data.value[1] = shader_defines_data[i].default_data.value[1];
#if 0 // This would only appear for 1 frame at the moment
            if (show_tooltip) {
                ImGui::SetTooltip(shader_defines_data[i].value_hint.c_str());
                show_tooltip = false;
            }
#endif
        }
#if 0 // Disabled for now as this is not very user friendly and could accidentally happen if two defines start with the same name.
        // Reset the define name if it matches another one
        if (name_edited && ShaderDefineData::ContainsName(shader_defines_data, shader_defines_data[i].editable_data.GetName(), i)) {
            shader_defines_data[i].Clear();
        }
#endif
        ImGui::PopID();

        if (disabled) {
            ImGui::EndDisabled();
        }

        if (show_tooltip && shader_defines_data[i].IsNameDefault() && shader_defines_data[i].HasTooltip()) {
            ImGui::SetTooltip(shader_defines_data[i].GetTooltip());
        }
      }

      ImGui::EndTabItem();
    }

#if DEVELOPMENT || TEST
    if (ImGui::BeginTabItem("Info")) {
        ImGui::Text("Render Resolution: ", "");
        std::string text = std::to_string((int)render_resolution.x) + " " + std::to_string((int)render_resolution.y);
        ImGui::Text(text.c_str(), "");

        ImGui::NewLine();
        ImGui::Text("Output Resolution: ", "");
        text = std::to_string((int)output_resolution.x) + " " + std::to_string((int)output_resolution.y);
        ImGui::Text(text.c_str(), "");

        if (dlss_sr) {
            ImGui::NewLine();
            ImGui::Text("DLSS Target Resolution Scale: ", "");
            text = std::to_string(dlss_render_resolution_scale);
            ImGui::Text(text.c_str(), "");
        }

        if (dlss_sr && cloned_pipeline_count != 0) {
            ImGui::NewLine();
            bool dlss_relative_pre_exposure = GetShaderDefineCompiledNumericalValue(DLSS_RELATIVE_PRE_EXPOSURE_HASH) >= 1;
            if (dlss_relative_pre_exposure)
                ImGui::Text("DLSS Relative Scene Exposure: ", "");
            else
                ImGui::Text("DLSS Scene Exposure: ", "");
            text = std::to_string(dlss_scene_pre_exposure);
            ImGui::Text(text.c_str(), "");
        }

        ImGui::NewLine();
        ImGui::Text("Camera Jitters: ", "");
        // In NCD space
        // Add padding to make it draw consistently even with a "-" in front of the numbers.
        text = (projection_jitters.x >= 0 ? " " : "") + std::to_string(projection_jitters.x) + " " + (projection_jitters.y >= 0 ? " " : "") + std::to_string(projection_jitters.y);
        ImGui::Text(text.c_str(), "");
        // In absolute space
        // These values should be between -1 and 1 (note that X might be flipped)
        text = (projection_jitters.x >= 0 ? " " : "") + std::to_string(projection_jitters.x * render_resolution.x) + " " + (projection_jitters.y >= 0 ? " " : "") + std::to_string(projection_jitters.y * render_resolution.y);
        ImGui::Text(text.c_str(), "");

        ImGui::NewLine();
        ImGui::Text("Texture Mip LOD Bias: ", "");
        text = std::to_string(texture_mip_lod_bias_offset);
        ImGui::Text(text.c_str(), "");

        ImGui::EndTabItem(); // Info
    }
#endif // DEVELOPMENT || TEST

    if (ImGui::BeginTabItem("About")) {
        ImGui::Text("Luma is developed by Pumbo and Ersh and is open source and free.\nIf you enjoy it, consider donating.", "");

        const auto button_color = ImGui::GetStyleColorVec4(ImGuiCol_Button);
        const auto button_hovered_color = ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered);
        const auto button_active_color = ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive);
        ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(70, 134, 0, 255));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(70 + 9, 134 + 9, 0, 255));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(70 + 18, 134 + 18, 0, 255));
        static const std::string donation_link_pumbo = std::string("Buy Pumbo a Coffee ") + std::string(ICON_FK_OK);
        if (ImGui::Button(donation_link_pumbo.c_str())) {
            system("start https://buymeacoffee.com/realfiloppi");
        }
        ImGui::SameLine();
        static const std::string donation_link_ersh = std::string("Buy Ersh a Coffee ") + std::string(ICON_FK_OK);
        if (ImGui::Button(donation_link_ersh.c_str())) {
            system("start https://ko-fi.com/ershin");
        }
        ImGui::PopStyleColor(3);

        ImGui::NewLine();
        // Restore the previous color, otherwise the state we set would persist even if we popped it
        ImGui::PushStyleColor(ImGuiCol_Button, button_color);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, button_hovered_color);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, button_active_color);
#if 0 //TODOFT: update
        if (ImGui::Button("Nexus Mods")) {
            system("start https://www.nexusmods.com/starfield/mods/4821");
        }
#endif
        static const std::string social_link = std::string("Join our \"HDR Den\" Discord ") + std::string(ICON_FK_SEARCH);
        if (ImGui::Button(social_link.c_str())) {
            // Unique link for Prey Luma (to track the origin of people joining), do not share for other purposes
            static const std::string obfuscated_link = std::string("start https://discord.gg/J9fM") + std::string("3EVuEZ");
            system(obfuscated_link.c_str());
        }
        static const std::string contributing_link = std::string("Contribute on Github ") + std::string(ICON_FK_FILE_CODE);
        if (ImGui::Button(contributing_link.c_str())) {
            system("start https://github.com/Filoppi/Prey-Luma");
        }
        ImGui::PopStyleColor(3);

        ImGui::NewLine();
        ImGui::Text("Credits:"
            "\n\nMain:"
            "\nPumbo (Graphics)"
            "\nErsh (Reverse engineering)"

            "\n\nThird Party:"
            "\nReShade"
            "\nImGui"
            "\nRenoDX"
            "\n3Dmigoto"
            "\nDKUtil"
            "\nNvidia (DLSS)"
            "\nOklab"
            "\nFubaxiusz (Perfect Perspective)"
            "\nIntel (Xe)GTAO"
            "\nDarktable UCS"
            "\nAMD RCAS"
            "\nDICE (HDR tonemapper)"
            "\nCrytek (CryEngine)"
            "\nArkane (Prey)"

            "\n\nThanks:"
            "\nShortFuse (support)"
            "\nLilium (support)"
            "\nKoKlusz (testing)"
            "\nMusa (testing)"
            "\ncrosire (support)"
            "\nFreshCloth (support)"
            "\nRegevitamins (support)"
            "\nMartysMods (support)"
            "\nKaldaien (support)"
            "\nnd4spd (testing)"
            , "");

        ImGui::NewLine();
        static const std::string version = "Version: " + std::to_string(Globals::VERSION);
        ImGui::Text(version.c_str());

        ImGui::EndTabItem(); // About
    }

    ImGui::EndTabBar(); // TabBar
  }
}
} // namespace

void Init(bool async) {
  // Add all the shaders we have already dumped to the dumped list to avoid live re-dumping them
  dumped_shaders.clear();
  std::set<std::filesystem::path> dumped_shaders_paths;
  auto dump_path = GetShaderPath();
  if (std::filesystem::exists(dump_path)) {
    dump_path /= "dump";
    // No need to create the directory here if it didn't already exist
    if (std::filesystem::is_directory(dump_path)) {
      const std::lock_guard<std::recursive_mutex> lock_dumping(s_mutex_dumping);
      for (const auto& entry : std::filesystem::directory_iterator(dump_path)) {
        if (!entry.is_regular_file()) continue;
        const auto& entry_path = entry.path();
        if (entry_path.extension() != ".cso") continue;
        const auto& entry_strem_string = entry_path.stem().string();
        if (entry_strem_string.starts_with("0x") && entry_strem_string.length() >= 2 + HASH_CHARACTERS_LENGTH) {
          const std::string shader_hash_string = entry_strem_string.substr(2, HASH_CHARACTERS_LENGTH);
          try {
            uint32_t shader_hash = std::stoul(shader_hash_string, nullptr, 16);
            bool duplicate = dumped_shaders.contains(shader_hash);
#if DEVELOPMENT
            ASSERT_ONCE(!duplicate); // We have a duplicate shader dumped, cancel here to avoid deleting it
#endif
            if (duplicate) {
                for (const auto& prev_entry_path : dumped_shaders_paths) {
                    if (prev_entry_path.string().contains(shader_hash_string)) {
                        // Delete the old version if it's shorter in name (e.g. it might have missed the "ps_5_0" appendix, or simply missing a name we manually appended to it)
                        if (prev_entry_path.string().length() < entry_path.string().length()) {
                            if (std::filesystem::remove(prev_entry_path)) {
                                duplicate = false;
                                break;
                            }
                        }
                    }
                }
            }
            if (!duplicate) {
                dumped_shaders.emplace(shader_hash);
                dumped_shaders_paths.emplace(entry_path);
            }
          } catch (const std::exception& e) {
            continue;
          }
        }
      }
    }
  }

  // Define the pixel shader of some important passes we can use to determine where we are within the rendering pipeline:

  // TiledShading TiledDeferredShading
  shader_hashes_TiledShadingTiledDeferredShading.compute_shaders = { std::stoul("1E676CD5", nullptr, 16), std::stoul("80FF9313", nullptr, 16), std::stoul("571D5EAE", nullptr, 16), std::stoul("6710AFD5", nullptr, 16), std::stoul("54147C78", nullptr, 16), std::stoul("BCD5A089", nullptr, 16), std::stoul("C2FC1948", nullptr, 16), std::stoul("E3EF3C20", nullptr, 16), std::stoul("F8633A07", nullptr, 16) };
  // DeferredShading SSR_Raytrace 
  shader_hash_DeferredShadingSSRRaytrace = std::stoul("AED014D7", nullptr, 16);
  // DeferredShading - SSReflection_Comp
  shader_hash_DeferredShadingSSReflectionComp = std::stoul("F355426A", nullptr, 16);
  // PostEffects GaussBlurBilinear
  shader_hash_PostEffectsGaussBlurBilinear = std::stoul("8B135192", nullptr, 16);
  // PostEffects TextureToTextureResampled
  shader_hash_PostEffectsTextureToTextureResampled = std::stoul("B969DC27", nullptr, 16); // One of the many
  // MotionBlur MotionBlur
  shader_hashes_MotionBlur.pixel_shaders = { std::stoul("D0C2257A", nullptr, 16), std::stoul("76B51523", nullptr, 16), std::stoul("6DCC9E5D", nullptr, 16) };
  // HDRPostProcess HDRFinalScene (vanilla HDR->SDR tonemapping)
  shader_hashes_HDRPostProcessHDRFinalScene.pixel_shaders = { std::stoul("B5DC761A", nullptr, 16), std::stoul("17272B5B", nullptr, 16), std::stoul("F87B4963", nullptr, 16), std::stoul("81CE942F", nullptr, 16), std::stoul("83557B79", nullptr, 16), std::stoul("37ACE8EF", nullptr, 16), std::stoul("66FD11D0", nullptr, 16) };
  // Same as "shader_hashes_HDRPostProcessHDRFinalScene" but it includes ones with sunshafts only
  shader_hashes_HDRPostProcessHDRFinalScene_Sunshafts.pixel_shaders = { std::stoul("81CE942F", nullptr, 16), std::stoul("37ACE8EF", nullptr, 16), std::stoul("66FD11D0", nullptr, 16) };
  // PostAA PostAA
// These passes don't have any projection jitters (unless maybe "SMAA 1TX" could have them if we forced them through config), so we can't replace them with DLSS SR.
// SMAA (without TX) is completely missing from here as it doesn't have a composition pass we could replace (well, maybe NeighborhoodBlendingSMAA).
#if 0
  shader_hashes_PostAA.pixel_shaders.emplace(std::stoul("D8072D98", nullptr, 16)); // FXAA
  shader_hashes_PostAA.pixel_shaders.emplace(std::stoul("E9D92B11", nullptr, 16)); // SMAA 1TX
#endif
  shader_hashes_PostAA.pixel_shaders.emplace(std::stoul("BF813081", nullptr, 16)); // SMAA 2TX and TAA
  // PostAA lendWeightSMAA + PostAA LumaEdgeDetectionSMAA
  shader_hashes_SMAA_EdgeDetection.pixel_shaders = { std::stoul("5636A813", nullptr, 16), std::stoul("47B723BD", nullptr, 16) };

  // PostAA PostAAComposites
  shader_hashes_PostAAComposites.pixel_shaders.emplace(std::stoul("83AE9250", nullptr, 16));
  shader_hashes_PostAAComposites.pixel_shaders.emplace(std::stoul("496492FE", nullptr, 16));
  shader_hashes_PostAAComposites.pixel_shaders.emplace(std::stoul("ED6287FE", nullptr, 16));
  shader_hashes_PostAAComposites.pixel_shaders.emplace(std::stoul("FAEE5EE9", nullptr, 16));
  shader_hash_PostAAUpscaleImage = std::stoul("C2F1D3F6", nullptr, 16); // Upscaling pixel shader (post TAA, only when in frames where DRS engage)
  shader_hashes_LensOptics.pixel_shaders = { std::stoul("4435D741", nullptr, 16), std::stoul("C54F3986", nullptr, 16), std::stoul("DAA20F29", nullptr, 16), std::stoul("047AB485", nullptr, 16), std::stoul("9D7A97B8", nullptr, 16), std::stoul("9B2630A0", nullptr, 16), std::stoul("51F2811A", nullptr, 16), std::stoul("9391298E", nullptr, 16), std::stoul("ED01E418", nullptr, 16), std::stoul("53529823", nullptr, 16), std::stoul("DDDE2220", nullptr, 16) };
  // DeferredShading DirOccPass
  shader_hashes_DirOccPass.pixel_shaders = { std::stoul("944B65F0", nullptr, 16), std::stoul("DB98D83F", nullptr, 16) };
  // ShadowBlur - SSDO Blur
  shader_hashes_SSDO_Blur.pixel_shaders.emplace(std::stoul("1023CD1B", nullptr, 16));
  //TODOFT: once we have collected 100% of the game shaders, update these hashes lists, and make global functions to convert hashes between string and int

  cb_luma_frame_settings.ScenePeakWhite = default_peak_white;
  cb_luma_frame_settings.ScenePaperWhite = default_paper_white;
  cb_luma_frame_settings.UIPaperWhite = default_paper_white;
  cb_luma_frame_settings.DLSS = 0; // We can't set this to 1 until we verified DLSS engaged correctly and is running

  // Load settings
  {
      const std::unique_lock lock_reshade(s_mutex_reshade);

      reshade::api::effect_runtime* runtime = nullptr;
      uint32_t config_version = Globals::VERSION;
      reshade::get_config_value(runtime, NAME, "Version", config_version);
      if (config_version != Globals::VERSION) {
          if (config_version < Globals::VERSION) {
              const std::unique_lock lock_loading(s_mutex_loading);
              // NOTE: put behaviour to load previous versions into new ones here
              CleanShadersCache(); // Force recompile shaders, just for extra safety (theoretically changes are auto detected through the preprocessor, but we can't be certain)
          }
          else if (config_version > Globals::VERSION) {
              reshade::log::message(reshade::log::level::warning, "Prey Luma: trying to load a config from a newer version of the mod, loading might have unexpected results");
          }
          reshade::set_config_value(runtime, NAME, "Version", Globals::VERSION);
      }

      reshade::get_config_value(runtime, NAME, "DLSSSuperResolution", dlss_sr);
      reshade::get_config_value(runtime, NAME, "TonemapUIBackground", tonemap_ui_background);
      reshade::get_config_value(runtime, NAME, "DisplayMode", cb_luma_frame_settings.DisplayMode);
#if !DEVELOPMENT && !TEST // Don't allow "SDR in HDR for HDR" mode (there's no strong reason not to, but it avoids permutations exposed to users)
      if (cb_luma_frame_settings.DisplayMode >= 2) {
          cb_luma_frame_settings.DisplayMode = 0;
      }
#endif
      OnDisplayModeChanged();

      if (reshade::get_config_value(runtime, NAME, "ScenePeakWhite", cb_luma_frame_settings.ScenePeakWhite) && cb_luma_frame_settings.ScenePeakWhite <= 0.f) {
          cb_luma_frame_settings.ScenePeakWhite = default_user_peak_white;
      }
      reshade::get_config_value(runtime, NAME, "ScenePaperWhite", cb_luma_frame_settings.ScenePaperWhite);
      reshade::get_config_value(runtime, NAME, "UIPaperWhite", cb_luma_frame_settings.UIPaperWhite);
      if (cb_luma_frame_settings.DisplayMode == 0) {
          cb_luma_frame_settings.ScenePeakWhite = srgb_white_level;
          cb_luma_frame_settings.ScenePaperWhite = srgb_white_level;
          cb_luma_frame_settings.UIPaperWhite = srgb_white_level;
      }
      else if (cb_luma_frame_settings.DisplayMode >= 2) {
          cb_luma_frame_settings.UIPaperWhite = cb_luma_frame_settings.ScenePaperWhite;
          cb_luma_frame_settings.ScenePeakWhite = cb_luma_frame_settings.ScenePaperWhite;
      }

      const std::unique_lock lock_shader_defines(s_mutex_shader_defines);
      ShaderDefineData::Load(shader_defines_data, runtime);
  }

  {
      const std::unique_lock lock_shader_defines(s_mutex_shader_defines);
      ShaderDefineData::OnCompilation(shader_defines_data);
      for (int i = 0; i < shader_defines_data.size(); i++) {
          shader_defines_data_index[string_view_crc32(std::string_view(shader_defines_data[i].compiled_data.GetName()))] = i;
      }
  }

  // Pre-load all shaders to minimize the wait before replacing them after they are found in game ("auto_load"),
  // and to fill the list of shaders we customized, so we can know which ones we need replace on the spot.
  if (async && precompile_custom_shaders) {
    if (thread_auto_loading.joinable()) {
      thread_auto_loading.join();
    }
    thread_auto_loading_running = true;
    static std::binary_semaphore async_shader_compilation_semaphore{0};
    thread_auto_loading = std::thread([] {
      // We need to lock this mutex for the whole async shader loading, so that if the game starts loading shaders (from another thread), we can already see if we have a custom version and live load it ("live_load"), otherwise the "custom_shaders_cache" list would be incomplete
      const std::unique_lock lock(s_mutex_loading);
      // This is needed to make sure this thread locks "s_mutex_loading" before any other function could
      async_shader_compilation_semaphore.release();
      CompileCustomShaders();
      thread_auto_loading_running = false;
    });
    async_shader_compilation_semaphore.acquire();
  }
}

// This can't be called on "DLL_PROCESS_DETACH" as it needs a multi threaded enviroment
void Uninit() {
  if (thread_auto_dumping.joinable()) {
    thread_auto_dumping.join();
  }
  if (thread_auto_loading.joinable()) {
    thread_auto_loading.join();
  }
}

// This is called immediately after "DllMain" if this dll/addon is loaded directly by ReShade
extern "C" __declspec(dllexport) bool AddonInit(HMODULE addon_module, HMODULE reshade_module) {
  Init(true);
  return true;
}
extern "C" __declspec(dllexport) void AddonUninit(HMODULE addon_module, HMODULE reshade_module) {
  Uninit();
}

BOOL APIENTRY DllMain(HMODULE h_module, DWORD fdw_reason, LPVOID lpv_reserved) {
  switch (fdw_reason) {
    // Note: this dll doesn't support being loaded more than once (or unloaded in the middle of execution)
    // as it doesn't fully restore the original state on uninit (there's no need to really).
    // ReShade loads addons when the game creates a DirectX device, and this seems to only ever happen once in Prey's case.
    case DLL_PROCESS_ATTACH:
    {
#if DEVELOPMENT || _DEBUG
      LaunchDebugger();
#endif // DEVELOPMENT

      // Hardcoding "Globals::NAME" here:
      if (GetModuleHandle(TEXT("PreyDll.dll")) == NULL) {
        MessageBoxA(game_window, "You are trying to use \"Prey Luma\" on a game that is not \"Prey (2017)\".\nThe mod will still run but probably crash.", NAME, MB_SETFOREGROUND);
      }

      wchar_t file_path_char[MAX_PATH] = L"";
      GetModuleFileNameW(h_module, file_path_char, ARRAYSIZE(file_path_char));
      std::filesystem::path file_path = file_path_char;
      if (file_path.extension() == ".addon" || file_path.extension() == ".addon64") {
          asi_loaded = false;
      }
      else {
        // Just to make sure, if we got loaded then it's probably fine either way
        assert(file_path.extension() == ".dll" || file_path.extension() == ".asi");
      }

      // Make sure the user deleted the d3dcompiler_47 dll
      std::filesystem::path shader_compiler_path = file_path.parent_path();
      shader_compiler_path.append("d3dcompiler_47.dll");
      if (std::filesystem::is_regular_file(shader_compiler_path)) {
          bool old_version = true;
          DWORD verHandle = 0;
          DWORD verSize = GetFileVersionInfoSize(shader_compiler_path.c_str(), &verHandle);
          if (verSize != NULL) {
              LPSTR verData = new char[verSize];
              if (GetFileVersionInfo(shader_compiler_path.c_str(), verHandle, verSize, verData)) {
                  LPBYTE lpBuffer = NULL;
                  UINT size = 0;
                  if (VerQueryValue(verData, L"\\", (VOID FAR * FAR*) & lpBuffer, &size)) {
                      if (size) {
                          VS_FIXEDFILEINFO* verInfo = (VS_FIXEDFILEINFO*)lpBuffer;
                          if (verInfo->dwSignature == 0xfeef04bd) {
                              // The version would be v1.v2.v3.v4
                              const auto v1 = (verInfo->dwFileVersionMS >> 16) & 0xffff;
                              const auto v2 = (verInfo->dwFileVersionMS >> 0) & 0xffff;
                              const auto v3 = (verInfo->dwFileVersionLS >> 16) & 0xffff;
                              const auto v4 = (verInfo->dwFileVersionLS >> 0) & 0xffff;
                              old_version = v1 <= 6 && v2 <= 3 && v3 <= 9600 && v3 <= 16384;
                          }
                      }
                  }
              }
              delete[] verData;
          }
          if (old_version) {
              MessageBoxA(game_window, "Please delete \"d3dcompiler_47.dll\" from the game executable directory;\nthe game came bundled with an old version that is worse in all aspects.\nIf you are on Proton, manually update it to the latest version.", NAME, MB_SETFOREGROUND);
              prevent_shader_cache_saving = true;
          }
      }

#if DISABLE_RESHADE
      if (!asi_loaded) return FALSE;
#else
      // Register the ReShade addon.
      // We simply cancel everything else if reshade is not present or failed to register,
      // we could still load the native plugin,
      const bool reshade_addon_register_succeeded = reshade::register_addon(h_module);
      if (!reshade_addon_register_succeeded) return FALSE;
#endif // DISABLE_RESHADE

      // Initialize the "native plugin" (our code hooks/patches)
      NativePlugin::Init(NAME, Globals::VERSION);

#if DISABLE_RESHADE
      if (asi_loaded) return TRUE;
#endif // DISABLE_RESHADE

      reshade::register_event<reshade::addon_event::init_command_list>(OnInitCommandList);

      reshade::register_event<reshade::addon_event::init_device>(OnInitDevice);
      reshade::register_event<reshade::addon_event::destroy_device>(OnDestroyDevice);
      reshade::register_event<reshade::addon_event::init_swapchain>(OnInitSwapchain);
      reshade::register_event<reshade::addon_event::destroy_swapchain>(OnDestroySwapchain);

#if DEVELOPMENT
      reshade::register_event<reshade::addon_event::init_pipeline_layout>(OnInitPipelineLayout);
#endif // DEVELOPMENT

      reshade::register_event<reshade::addon_event::init_pipeline>(OnInitPipeline);
      reshade::register_event<reshade::addon_event::destroy_pipeline>(OnDestroyPipeline);

      reshade::register_event<reshade::addon_event::bind_pipeline>(OnBindPipeline);

#if DEVELOPMENT
      reshade::register_event<reshade::addon_event::init_resource>(OnInitResource);
      reshade::register_event<reshade::addon_event::destroy_resource>(OnDestroyResource);
      reshade::register_event<reshade::addon_event::init_resource_view>(OnInitResourceView);
      reshade::register_event<reshade::addon_event::destroy_resource_view>(OnDestroyResourceView);
#endif // DEVELOPMENT

      reshade::register_event<reshade::addon_event::push_descriptors>(OnPushDescriptors);
      reshade::register_event<reshade::addon_event::map_buffer_region>(OnMapBufferRegion);
      reshade::register_event<reshade::addon_event::unmap_buffer_region>(OnUnmapBufferRegion);
      reshade::register_event<reshade::addon_event::copy_resource>(OnCopyResource);
      reshade::register_event<reshade::addon_event::copy_texture_region>(OnCopyTextureRegion);

      reshade::register_event<reshade::addon_event::draw>(OnDraw);
      reshade::register_event<reshade::addon_event::dispatch>(OnDispatch);
      reshade::register_event<reshade::addon_event::draw_indexed>(OnDrawIndexed);
      reshade::register_event<reshade::addon_event::draw_or_dispatch_indirect>(OnDrawOrDispatchIndirect);

      reshade::register_event<reshade::addon_event::init_sampler>(OnInitSampler);
      reshade::register_event<reshade::addon_event::destroy_sampler>(OnDestroySampler);

      reshade::register_event<reshade::addon_event::present>(OnPresent);

      reshade::register_event<reshade::addon_event::reshade_present>(OnReshadePresent);

      reshade::register_overlay(NAME, OnRegisterOverlay);

      break;
    }
    case DLL_PROCESS_DETACH:
    {
      reshade::unregister_event<reshade::addon_event::init_command_list>(OnInitCommandList);

      reshade::unregister_event<reshade::addon_event::init_device>(OnInitDevice);
      reshade::unregister_event<reshade::addon_event::destroy_device>(OnDestroyDevice);
      reshade::unregister_event<reshade::addon_event::init_swapchain>(OnInitSwapchain);
      reshade::unregister_event<reshade::addon_event::destroy_swapchain>(OnDestroySwapchain);

#if DEVELOPMENT
      reshade::unregister_event<reshade::addon_event::init_pipeline_layout>(OnInitPipelineLayout);
#endif // DEVELOPMENT

      reshade::unregister_event<reshade::addon_event::init_pipeline>(OnInitPipeline);
      reshade::unregister_event<reshade::addon_event::destroy_pipeline>(OnDestroyPipeline);

      reshade::unregister_event<reshade::addon_event::bind_pipeline>(OnBindPipeline);

#if DEVELOPMENT
      reshade::unregister_event<reshade::addon_event::init_resource>(OnInitResource);
      reshade::unregister_event<reshade::addon_event::destroy_resource>(OnDestroyResource);
      reshade::unregister_event<reshade::addon_event::init_resource_view>(OnInitResourceView);
      reshade::unregister_event<reshade::addon_event::destroy_resource_view>(OnDestroyResourceView);
#endif // DEVELOPMENT

      reshade::unregister_event<reshade::addon_event::push_descriptors>(OnPushDescriptors);
      reshade::unregister_event<reshade::addon_event::map_buffer_region>(OnMapBufferRegion);
      reshade::unregister_event<reshade::addon_event::unmap_buffer_region>(OnUnmapBufferRegion);
      reshade::unregister_event<reshade::addon_event::copy_resource>(OnCopyResource);
      reshade::unregister_event<reshade::addon_event::copy_texture_region>(OnCopyTextureRegion);

      reshade::unregister_event<reshade::addon_event::draw>(OnDraw);
      reshade::unregister_event<reshade::addon_event::dispatch>(OnDispatch);
      reshade::unregister_event<reshade::addon_event::draw_indexed>(OnDrawIndexed);
      reshade::unregister_event<reshade::addon_event::draw_or_dispatch_indirect>(OnDrawOrDispatchIndirect);

      reshade::unregister_event<reshade::addon_event::init_sampler>(OnInitSampler);
      reshade::unregister_event<reshade::addon_event::destroy_sampler>(OnDestroySampler);

      reshade::unregister_event<reshade::addon_event::present>(OnPresent);

      reshade::unregister_event<reshade::addon_event::reshade_present>(OnReshadePresent);

      reshade::unregister_overlay(NAME, OnRegisterOverlay);

      reshade::unregister_addon(h_module);

      // In case our threads are still not joined, detach them and safely do a busy loop
      // until they finished running, so we don't risk them reading/writing to stale memory.
      // This could cause a bit of wait, especially if we just booted the game and shaders are still compiling,
      // but there's no nice and clear alternatively really.
      // This is needed because DLL loading/unloading is completely single threaded and isn't
      // able to join threads (though "thread.detach()" somehow seems to work).
      // Note that there's no need to call "Uninit()" here, independently on whether we are asi or ReShade loaded.
      if (thread_auto_dumping.joinable()) {
        thread_auto_dumping.detach();
        while (thread_auto_dumping_running) {
        }
      }
      if (thread_auto_loading.joinable()) {
        thread_auto_loading.detach();
        while (thread_auto_loading_running) {
        }
      }

      break;
      }
  }

  return TRUE;
}
