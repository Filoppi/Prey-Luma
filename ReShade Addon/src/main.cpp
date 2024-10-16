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
// Setting it to 0 causes the compiler to still assume it as defined and that thus we are in debug mode.
#ifndef NDEBUG
#define _DEBUG 1
#endif // !NDEBUG

#pragma comment(lib, "dxguid.lib")

#include <d3d11.h>
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

// ReShade dependencies
#include <deps/imgui/imgui.h>
#include <include/reshade.hpp>
#include <source/com_ptr.hpp>
#include <examples/utils/crc32_hash.hpp>
#if 0 // Not needed atm
#include <source/d3d11/d3d11_impl_type_convert.hpp>
#endif

#include "cbuffers.h"
#include "math.h"
#include "matrix.h"

#include "utils/descriptor.hpp"
#include "utils/format.hpp"
#include "utils/pipeline.hpp"
#include "utils/shader_compiler.hpp"

#define ICON_FK_CANCEL reinterpret_cast<const char*>(u8"\uf00d")
#define ICON_FK_OK reinterpret_cast<const char*>(u8"\uf00c")
#define ICON_FK_PLUS reinterpret_cast<const char*>(u8"\uf067")
#define ICON_FK_REFRESH reinterpret_cast<const char*>(u8"\uf021")
#define ICON_FK_UNDO reinterpret_cast<const char*>(u8"\uf0e2")
#define ICON_FK_SEARCH reinterpret_cast<const char*>(u8"\uf002")
#define ICON_FK_WARNING reinterpret_cast<const char*>(u8"\uf071")

#define ImTextureID ImU64

#define ENABLE_NGX 1
#define TEST_DLSS 0

#define DLSS_TARGET_TEXTURE_WAS_UAV 0
#define DLSS_KEEP_DLL_LOADED 1

#define OLD_GLOBAL_BUFFER_INTERCEPT_METHOD 0
#define DLSS_UPSCALE_LENS_EFFECTS 1
#define REPLACE_SAMPLERS_LIVE 1

#if ENABLE_NGX
#include "dlss/DLSS.h"
#endif

// NOLINTBEGIN(readability-identifier-naming)

extern "C" __declspec(dllexport) const char* NAME = "Prey Luma";
extern "C" __declspec(dllexport) const char* DESCRIPTION = "Prey (2017) Luma mod";
constexpr uint32_t VERSION = 1; // Internal version (not public facing)

// NOLINTEND(readability-identifier-naming)

#if DEVELOPMENT || _DEBUG || TEST
#define ASSERT_ONCE(x) { static bool asserted_once = false; \
if (!asserted_once && !(x)) { assert(x); asserted_once = true; } }
#else
#define ASSERT_ONCE(x)
#endif

//TODOFT: fix cpp file formatting in general (and make sure it's all thread safe, but it should be)
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
            MessageBoxA(NULL, "Loaded. You can now attach the debugger or continue execution.", NAME, NULL);
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
  // Oriignal pipeline
  reshade::api::pipeline pipeline;
  reshade::api::device* device;
  reshade::api::pipeline_layout layout;
  // Cloned subojects from the oriignal pipeline
  reshade::api::pipeline_subobject* subobjects_cache;
  uint32_t subobject_count;
  bool cloned = false;
  bool ready_for_binding = true;
  reshade::api::pipeline pipeline_clone;
  // Original shaders hash (there should only be one)
  std::vector<uint32_t> shader_hashes;
  // If true, this pipeline is currently being "tested"
  bool test = false;

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
  std::filesystem::path file_path;
  std::size_t preprocessed_hash = 0; // A value of 0 won't ever be generated by the hash algorithm
  std::string compilation_error;
};

// For "pipeline_cache_by_pipeline_handle", "pipeline_caches_by_shader_hash", "shader_cache", "pipelines_to_destroy"
std::recursive_mutex s_mutex_generic;
// For "shaders_to_dump", "dumped_shaders", "shader_cache"
std::recursive_mutex s_mutex_dumping;
// For "custom_shaders_cache", "pipelines_to_reload"
std::recursive_mutex s_mutex_loading;

std::thread thread_auto_dumping;
std::atomic<bool> thread_auto_dumping_running = false;
std::thread thread_auto_loading;
std::atomic<bool> thread_auto_loading_running = false;

//TODOFT: clean up all unused stuff
struct __declspec(uuid("3b70b2b2-51dc-4637-bd75-c1a71c4c322e")) DeviceData {
  std::shared_mutex mutex;

  reshade::api::device_api device_api;

  // <resource.handle, resource_view.handle>
  std::unordered_map<uint64_t, uint64_t> resource_views;
  // <resource.handle, vector<resource_view.handle>>
  std::unordered_map<uint64_t, std::vector<uint64_t>> resource_views_by_resource;
  std::unordered_map<uint64_t, std::string> resource_names;
  std::unordered_set<uint64_t> resources;

  //TODOFT3: are these pointers safe? they are not com pointers so theoretically DX could null them at any time, making them a stale pointer, though in reality we'd probably get a swapchain resize event first? (same stuff for other ptrs, like pipeline ptrs). It seems safe (as long it's made safe, at least for pipelines (which are shaders etc in DX11), see "register_destruction_callback_d3dx" in ReShade.
  std::unordered_set<reshade::api::swapchain*> swapchains;
  std::unordered_set<uint64_t> back_buffers;

  reshade::api::pipeline_layout settings_pipeline_layout;
  reshade::api::pipeline_layout shared_data_pipeline_layout;
  reshade::api::pipeline_layout ui_pipeline_layout;
};

struct __declspec(uuid("2ab7ec11-a21f-4184-a6b7-f321d198b9af")) SwapchainData {
  std::shared_mutex mutex;
  std::unordered_set<uint64_t> back_buffers;
};

// Pipelines by handle. Multiple pipelines can target the same shader, and even have multiple shaders within themselved.
// This only contains pipelines that we are replacing any shaders of.
std::unordered_map<uint64_t, CachedPipeline*> pipeline_cache_by_pipeline_handle;
// Same as "pipeline_cache_by_pipeline_handle" but for cloned pipelines.
std::unordered_map<uint64_t, CachedPipeline*> pipeline_cache_by_pipeline_clone_handle;
// All the pipelines linked to a shader. By shader hash.
std::unordered_map<uint32_t, std::unordered_set<CachedPipeline*>> pipeline_caches_by_shader_hash;
// All the shaders the game ever loaded (including the ones that have been unloaded). By shader hash.
std::unordered_map<uint32_t, CachedShader*> shader_cache;
// All the shaders the user has (and has had) as custom in the live folder. By shader hash.
std::unordered_map<uint32_t, CachedCustomShader*> custom_shaders_cache;

//TODOFT: make thread safe like maps above?
reshade::api::pipeline pipeline_state_original_compute_shader = reshade::api::pipeline(0);
reshade::api::pipeline pipeline_state_original_vertex_shader = reshade::api::pipeline(0);
reshade::api::pipeline pipeline_state_original_pixel_shader = reshade::api::pipeline(0);

#if REPLACE_SAMPLERS_LIVE
// Custom samplers mapped to original ones by texture LOD bias
std::recursive_mutex s_mutex_samplers;
std::unordered_map<uint64_t, std::unordered_map<float, com_ptr<ID3D11SamplerState>>> custom_sampler_by_original_sampler;
#endif

std::unordered_set<uint64_t> pipelines_to_reload;
static_assert(sizeof(reshade::api::pipeline::handle) == sizeof(uint64_t));
// Map of "reshade::api::pipeline::handle"
std::unordered_map<uint64_t, reshade::api::device*> pipelines_to_destroy;
// Newly loaded shaders that still need to be (auto) dumped
std::unordered_set<uint32_t> shaders_to_dump;
// All the shaders we have already dumped
std::unordered_set<uint32_t> dumped_shaders;

std::vector<uint32_t> trace_shader_hashes;
std::vector<uint64_t> trace_pipeline_handles;

const uint32_t HASH_CHARACTERS_LENGTH = 8;

const std::string NAME_ADVANCED_SETTINGS = std::string(NAME) + " Advanced";

constexpr uint32_t MAX_SHADER_DEFINES = 25; // Avoid setting this too big as it bloats the ReShade config whether used or not. Don't go beyond 99 (max array length 100) without changing core related to this.
constexpr uint32_t SHADER_DEFINES_MAX_NAME_LENGTH = 50 + 1; // Increase if necessary (+ 1 is for to null terminate the string)
constexpr uint32_t SHADER_DEFINES_MAX_VALUE_LENGTH = 1 + 1; // 1 character (+ 1 is for to null terminate the string)

struct ShaderDefine {
    ShaderDefine(const char* _name = "", char _value = '\0') {
        assert(std::strlen(_name) < SHADER_DEFINES_MAX_NAME_LENGTH);
        strncpy(&name[0], _name, std::max<size_t>(std::size(name), std::strlen(_name)));
        value[0] = _value;
        value[1] = '\0';
    }

    const char* GetName() const { return &name[0]; }
    const char* GetValue() const { return &value[0]; }

    char* GetName() { return &name[0]; }
    char* GetValue() { return &value[0]; }

    char name[SHADER_DEFINES_MAX_NAME_LENGTH];
    // Char and not int because defines are taken as text by the compiler, and we are fine with the 255 limit.
    // In fact, we only ideally want one positive numerical character,
    // so the range is 0-9 in char (the values don't match integers),
    // and also '\0' to simply define a define without a value, but these are adviced against.
    // Only the first character is used, the second is to null terminate the string and should never have any other value.
    char value[SHADER_DEFINES_MAX_VALUE_LENGTH];
};

static uint32_t defines_count = 0;
// Extra flag to specify we need a recompilation
static bool defines_need_recompilation = false;

struct ShaderDefineData {
    ShaderDefineData(const char* name = "", char value = '\0', bool _fixed_name = false, bool _fixed_value = false) :
        name_hint("Define " + std::to_string(defines_count) + " Name"),
        value_hint("Define " + std::to_string(defines_count) + " Value"),
        fixed_name(_fixed_name),
        fixed_value(_fixed_value),
        default_data(name, value) {
        defines_count++;
        editable_data = default_data;
    }

    // The default label/hint
    const std::string name_hint;
    const std::string value_hint;

    // Set to true if you want the name of this define to be fixed
    const bool fixed_name = false;
    const bool fixed_value = false;

    // The current (possibly editable) name and value
    ShaderDefine editable_data;
    // The default name and value
    const ShaderDefine default_data;
    // The last name and value that got compiled into shaders
    ShaderDefine compiled_data;

    bool IsNameEditable() const {
#if DEVELOPMENT
        return !fixed_name;
#elif !TEST
        return false;
#endif // DEVELOPMENT
        return !fixed_name && IsCustom();
    }
    bool IsValueEditable() const {
#if DEVELOPMENT || TEST
        return !fixed_value;
#endif // DEVELOPMENT
        return !fixed_value && !IsCustom();
    }

    // If true, this a "custom" shader define created at runtime
    bool IsCustom() const {
        return strcmp(default_data.GetName(), "") == 0;
    }
    // Whether it's fully null/empty
    bool IsEmpty() const {
        return strcmp(editable_data.GetName(), "") == 0 && (editable_data.value[0] == '\0');
    }
    // Whether is has the default name/value
    bool IsDefault() const {
        return strcmp(editable_data.GetName(), default_data.GetName()) == 0 && (editable_data.value[0] == default_data.value[0]);
    }
    // Whether it needs to be compiled for the values to apply (it's "dirty")
    bool NeedsCompilation() const {
        return strcmp(editable_data.GetName(), compiled_data.GetName()) != 0 || (editable_data.value[0] != compiled_data.value[0]);
    }

    void Reset() {
        strncpy(editable_data.GetName(), default_data.GetName(), SHADER_DEFINES_MAX_NAME_LENGTH);
        editable_data.value[0] = default_data.value[0];
    }
    void OnCompilation() {
        strncpy(compiled_data.GetName(), editable_data.GetName(), SHADER_DEFINES_MAX_NAME_LENGTH);
        compiled_data.value[0] = editable_data.value[0];
    }

    static void Reset(std::vector<ShaderDefineData>& shader_defines_data) {
        for (uint32_t i = 0; i < shader_defines_data.size(); i++) {
            shader_defines_data[i].Reset();
        }
    }
    static void OnCompilation(std::vector<ShaderDefineData>& shader_defines_data) {
        for (uint32_t i = 0; i < shader_defines_data.size(); i++) {
            shader_defines_data[i].OnCompilation();
        }
        defines_need_recompilation = false;
    }

    static void RemoveCustomData(std::vector<ShaderDefineData>& shader_defines_data, bool only_if_empty = false) {
#if 0
        for (auto it = shader_defines_data.begin(); it != shader_defines_data.end(); ) {
            if (it->IsCustom() && (!only_if_empty || it->IsEmpty())) {
                // We can't keep track of changes in the defines we removed so mark them as dirty before removing them
                defines_need_recompilation |= it->NeedsCompilation() || !it->IsEmpty();
                it = shader_defines_data.erase(it);
            }
            else {
                ++it;
            }
        }
#else // The above implementation fails to compile due to using "const" on member variables of the vector struct, due to some deleted construtor. We work around it by re-creating the vector from scratch
        const auto shader_defines_data_copy = shader_defines_data;
        shader_defines_data.clear();
        for (auto it = shader_defines_data_copy.begin(); it != shader_defines_data_copy.end(); ) {
            if (!it->IsCustom() || (only_if_empty && !it->IsEmpty()))
                shader_defines_data.emplace_back(*it);
            else // We can't keep track of changes in the defines we removed so mark them as dirty before removing them
                defines_need_recompilation |= it->NeedsCompilation() || !it->IsEmpty();
            ++it;
        }
#endif

        // Reset the global counter of how many defines had been created
        defines_count = shader_defines_data.size();
    }

    static void Load(std::vector<ShaderDefineData>& shader_defines_data, reshade::api::effect_runtime* runtime = nullptr) {
        char char_buffer[std::string_view("Define99Value ").size()]; // Hardcoded max length (see "MAX_SHADER_DEFINES")
#if DEVELOPMENT || TEST
        for (uint32_t i = 0; i <= MAX_SHADER_DEFINES; i++) {
#else
        for (uint32_t i = 0; i < shader_defines_data.size(); i++) {
#endif
            const bool is_new_define = i >= shader_defines_data.size();
            bool is_editable = is_new_define || shader_defines_data[i].IsNameEditable();
            bool should_load_value = true;
            sprintf(&char_buffer[0], i < 10 ? "Define#%iName" : "Define%iName", i);
            if (is_editable) {
                if (is_new_define) {
                    shader_defines_data.emplace_back();
                }
                size_t size = std::size(shader_defines_data[i].editable_data.name); // SHADER_DEFINES_MAX_NAME_LENGTH
                reshade::get_config_value(runtime, NAME_ADVANCED_SETTINGS.c_str(), &char_buffer[0], shader_defines_data[i].editable_data.GetName(), &size);
                if (!shader_defines_data[i].IsCustom() && shader_defines_data[i].editable_data.GetName() == "") {
                    shader_defines_data[i].Reset();
                }
            }
            else {
                // In this case, we should only load the define actual value if the define name serialized in the config matched with our default one
                char name[SHADER_DEFINES_MAX_NAME_LENGTH] = "";
                size_t size = std::size(name); // SHADER_DEFINES_MAX_NAME_LENGTH
                reshade::get_config_value(runtime, NAME_ADVANCED_SETTINGS.c_str(), &char_buffer[0], &name[0], &size);
                should_load_value = strcmp(shader_defines_data[i].editable_data.GetName(), &name[0]) == 0 && (&name[0] != ""); // See "ShaderDefineData::IsCustom()"
            }

            is_editable = is_new_define || shader_defines_data[i].IsValueEditable();
            if (is_editable && should_load_value) {
                assert(i < shader_defines_data.size());
                sprintf(&char_buffer[0], i < 10 ? "Define#%iValue" : "Define%iValue", i);
                size_t size = std::size(shader_defines_data[i].editable_data.value); // SHADER_DEFINES_MAX_VALUE_LENGTH
                reshade::get_config_value(runtime, NAME_ADVANCED_SETTINGS.c_str(), &char_buffer[0], shader_defines_data[i].editable_data.GetValue(), &size);
                if (!shader_defines_data[i].IsCustom() && shader_defines_data[i].editable_data.GetValue() == "") {
                    shader_defines_data[i].Reset();
                }
            }
        }

        // Clean up the ones that loaded up with empty values
        RemoveCustomData(shader_defines_data, true);
    }
    static void Save(std::vector<ShaderDefineData>& shader_defines_data, reshade::api::effect_runtime* runtime = nullptr) {
        char char_buffer[std::string_view("Define99Value ").size()]; // Hardcoded max length (see "MAX_SHADER_DEFINES")
        constexpr bool always_save_shader_defines = false;
        for (uint32_t i = 0; i < shader_defines_data.size(); i++) {
            sprintf(&char_buffer[0], i < 10 ? "Define#%iName" : "Define%iName", i);
            if (!shader_defines_data[i].IsDefault() || always_save_shader_defines) {
                reshade::set_config_value(runtime, NAME_ADVANCED_SETTINGS.c_str(), &char_buffer[0], shader_defines_data[i].editable_data.GetName(), std::size(shader_defines_data[i].editable_data.name) /*SHADER_DEFINES_MAX_NAME_LENGTH*/);
            }
            // Don't save default values, they would pollute the config file and cause issues with versioning.
            // If the shaders code change, we have other ways of detecting that we need to re-compile after launch, and if the addon code change, it would also automatically trigger a recompile.
            else {
                reshade::set_config_value(runtime, NAME_ADVANCED_SETTINGS.c_str(), &char_buffer[0], (const char*)nullptr);
            }
            sprintf(&char_buffer[0], i < 10 ? "Define#%iValue" : "Define%iValue", i);
            if (!shader_defines_data[i].IsDefault() || always_save_shader_defines) {
                reshade::set_config_value(runtime, NAME_ADVANCED_SETTINGS.c_str(), &char_buffer[0], shader_defines_data[i].editable_data.GetValue(), std::size(shader_defines_data[i].editable_data.value) /*SHADER_DEFINES_MAX_VALUE_LENGTH*/);
            }
            else {
                reshade::set_config_value(runtime, NAME_ADVANCED_SETTINGS.c_str(), &char_buffer[0], (const char*)nullptr);
            }
        }
#if DEVELOPMENT || TEST // These are never read or set outside of these configurations, so there's no need to clear them either for now
        // Clean the remaining non set defines if they were in the config (setting them to "null" will remove them)
        for (uint32_t i = shader_defines_data.size(); i <= MAX_SHADER_DEFINES; i++) {
            char char_buffer_2[SHADER_DEFINES_MAX_VALUE_LENGTH] = "";
            size_t size = SHADER_DEFINES_MAX_VALUE_LENGTH;
            sprintf(&char_buffer[0], i < 10 ? "Define#%iName" : "Define%iName", i);
            reshade::set_config_value(runtime, NAME_ADVANCED_SETTINGS.c_str(), &char_buffer[0], (const char*)nullptr);
            sprintf(&char_buffer[0], i < 10 ? "Define#%iValue" : "Define%iValue", i);
            reshade::set_config_value(runtime, NAME_ADVANCED_SETTINGS.c_str(), &char_buffer[0], (const char*)nullptr);
        }
#endif
    }
};

// Settings
#if DEVELOPMENT || TEST
bool auto_dump = true;
#else // !DEVELOPMENT && !TEST
bool auto_dump = false;
#endif // DEVELOPMENT || TEST
bool auto_load = true;
bool live_reload = false;
bool trace_list_unique_shaders_only = false;
bool trace_ignore_vertex_shaders = true;
const bool precompile_custom_shaders = true;
constexpr uint32_t shader_cbuffers_index = 2; // Default to 2 for Prey
bool dlss_sr = true;
bool prey_drs_detected = false;
float dlss_sr_render_resolution = 1.f;
bool prey_taa_enabled = false;
// Index 0 is one frame ago, index 1 is two frames ago
bool previous_prey_taa_enabled[2] = { false, false };
bool prey_taa_detected = false;
bool dlss_sr_supported = false;
bool tonemap_ui_background = true;
Matrix44A reprojection_matrix;
constexpr float tonemap_ui_background_amount = 0.25;
constexpr float srgb_white_level = 80;
constexpr float default_paper_white = 203;
constexpr float default_peak_white = 1000;
bool hdr_supported = false; //TODOFT: this requires more work to work properly
float default_user_peak_white = default_peak_white;
std::unordered_set<uint32_t> shader_hashes_TiledShadingTiledDeferredShading;
std::unordered_set<uint32_t> shader_hashes_MotionBlur;
std::unordered_set<uint32_t> shader_hashes_HDRPostProcessHDRFinalScene;
std::unordered_set<uint32_t> shader_hashes_PostAA;
std::unordered_set<uint32_t> shader_hashes_PostAAComposites;
uint32_t shader_hash_PostAAUpscaleImage;
std::unordered_set<uint32_t> shader_hashes_LensOptics;

//TODOFT3: clean up
constexpr bool dlss_mv_jittered = true; //TODOFT4: test this, in case it ever looked better, but the dynamic objects motion vectors are always half jittered and we can't remove these jitters properly (we tried...) so it's better to just run jittered all along (we got it to look perfect across all cases)
constexpr bool prey_taa_jittered = false; //TODOFT4: test with "FORCE_MOTION_VECTORS_JITTERED" and enable? Right now this looks bad because Prey's native TAA isn't made for it (seemengly)
int dlss_mode = dlss_mv_jittered ? 1 : 0; // We need jitters in DLSS because dynamic objects motion vectors are inherently calculated with at least the current frame's jitters acknowledged (because they are rendered with g-buffers, on projection matrices that have jitters)
int fix_prev_matrix_mode = dlss_mv_jittered ? 1 : 0; // NOTE: referenced in shader's code
int matrix_calculation_mode = 0;
int matrix_calculation_mode_2 = dlss_mv_jittered ? 1 : 4;
int samplers_upgrade_mode = 5;
int samplers_upgrade_mode_2 = 0;
#if REPLACE_SAMPLERS_LIVE
float texture_mip_lod_bias_offset = -1.0f;
bool custom_texture_mip_lod_bias_offset = false; // Live edit //TODOFT: "DEVELOPMENT" only
#else
constexpr float texture_mip_lod_bias_offset = -1.0f; // Value tweaked for DLSS (note that the game's native TAA already biases some samples by -1, so we'd do it twice)
#endif
float dlss_custom_exposure = 1.0;
float dlss_custom_pre_exposure = 1.0;

//TODOFT: put all of these in a better place (e.g. device data? frame data?). Probably not needed given it's all single threaded
// Per frame states:
bool has_drawn_main_post_processing = false;
// Useful to know if rendering was skipped in the previous frame (e.g. in case we were in a UI view)
bool has_drawn_main_post_processing_previous = false;
bool has_drawn_upscaling = false;
bool has_drawn_dlss_sr = false;
bool has_drawn_motion_blur_previous = false;
bool has_drawn_motion_blur = false;
bool has_drawn_tonemapping = false;
bool has_drawn_composed_gbuffers = false;
bool found_per_view_globals = false;
// Whether the rendering resolution was scaled in this frame (different from the ouput resolution)
bool prey_drs_active = false;
// Directly from cbuffer (so these are transposed)
Matrix44A projection_matrix;
Matrix44A nearest_projection_matrix;
Matrix44A previous_projection_matrix;
Matrix44A previous_nearest_projection_matrix;
float2 previous_projection_jitters = { 0, 0 };
float2 projection_jitters = { 0, 0 };
float2 render_resolution = { 1, 1 };
float2 previous_render_resolution = { 1, 1 };
float2 output_resolution = { 1, 1 };
// Pointer to the current DX buffer for the "global per view" cbuffer.
com_ptr<ID3D11Buffer> cb_per_view_global_buffer;
#if OLD_GLOBAL_BUFFER_INTERCEPT_METHOD
// Copy of the "cb_per_view_global_buffer" we occasionally use to read it from the CPU.
// We need to keep it alive over time to avoid crashes.
com_ptr<ID3D11Buffer> cb_per_view_global_buffer_copy;
#endif
void* cb_per_view_global_buffer_map_data = nullptr;
CBPerViewGlobal cb_per_view_global = { };
CBPerViewGlobal cb_per_view_global_previous = cb_per_view_global;
#if DEVELOPMENT //TODOFT3: delete once not needed anymore?
std::string last_drawn_shader = "";
std::vector<ID3D11Buffer*> cb_per_view_global_buffer_pending_verification;
std::vector<CBPerViewGlobal> cb_per_view_globals;
std::vector<std::string> cb_per_view_globals_last_drawn_shader;
std::vector<CBPerViewGlobal> cb_per_view_globals_previous;
std::vector<ID3D11Buffer*> cb_per_view_global_buffers;
#endif // DEVELOPMENT
LumaFrameSettings cb_luma_frame_settings = { };

constexpr uint32_t ui_cbuffer_index = 7;

// These default should ideally match shaders values, but it's not necessary because whathever the default values they have they will be overridden
std::vector<ShaderDefineData> shader_defines_data = {
  {"DEVELOPMENT", DEVELOPMENT ? '1' : '0', true, DEVELOPMENT ? false : true},
  {"POST_PROCESS_SPACE_TYPE", '1', true, false},
  {"ENABLE_GAMMA_CORRECTION", '1', false, false},
  {"ENABLE_LUT_EXTRAPOLATION", '1', false, false},
  {"TONEMAP_TYPE", '1', false, false},
  {"SUNSHAFTS_LOOK_TYPE", '2', false, false},
  {"ENABLE_LENS_OPTICS_HDR", '1', false, false},
  {"AUTO_HDR_VIDEOS", '1', false, false},
  {"SSAO_TYPE", '0', false, false},
  {"FORCE_NEUTRAL_COLOR_GRADING_LUT_TYPE", '0', false, false},
  {"DRAW_LUT", '0', false, (DEVELOPMENT || TEST) ? false : true},
  {"FORCE_MOTION_VECTORS_JITTERED", prey_taa_jittered ? '1' : '0', false, false},
  {"ENABLE_CAMERA_MOTION_BLUR", '0', false, false},
  {"ENABLE_POST_PROCESS", '1', false, false},
};
//TODOFT3: add more and update values, and polish them for shipping. Also maybe add a tooltip?
constexpr uint32_t development_define_index = 0;
constexpr uint32_t post_process_space_define_index = 1;

// Resources:
#if ENABLE_NGX
#if !DLSS_TARGET_TEXTURE_WAS_UAV
com_ptr<ID3D11Texture2D> dlss_output_color;
#endif // DLSS_TARGET_TEXTURE_WAS_UAV
com_ptr<ID3D11Texture2D> dlss_exposure;
com_ptr<ID3D11Texture2D> dlss_motion_vectors;
com_ptr<ID3D11RenderTargetView> dlss_motion_vectors_rtv;
#endif // ENABLE_NGX
com_ptr<ID3D11Texture2D> copy_texture;
com_ptr<ID3D11VertexShader> copy_vertex_shader;
com_ptr<ID3D11PixelShader> copy_pixel_shader;

static_assert(sizeof(Matrix44A) == sizeof(float4) * 4);

bool last_pressed_unload = false;
bool trace_scheduled = false;
bool trace_running = false;
bool needs_unload_shaders = false;
bool needs_load_shaders = false; // Load or reload shaders
bool needs_live_reload_update = live_reload;
bool trace_names = false;
std::atomic<bool> cloned_pipelines_changed = false;
uint32_t cloned_pipeline_count = 0;
uint32_t shader_cache_count = 0;
uint32_t resource_count = 0;
uint32_t resource_view_count = 0;
uint32_t trace_count = 0;

// Forward declares:
void ToggleLiveWatching();
void DumpShader(uint32_t shader_hash, bool auto_detect_type);
void AutoDumpShaders();
void AutoLoadShaders();

// Returns false if failed or if HDR is not enaged (but the white luminance can still be used).
bool GetHDRMaxLuminance(IDXGISwapChain3* swap_chain, float& max_luminance)
{
    max_luminance = srgb_white_level;
    
    IDXGIOutput* output = nullptr;
    if (FAILED(swap_chain->GetContainingOutput(&output))) {
        return false;
    }

    IDXGIOutput6* output6 = nullptr;
    if (FAILED(output->QueryInterface(&output6))) {
        return false;
    }

    DXGI_OUTPUT_DESC1 desc1;
    if (FAILED(output6->GetDesc1(&desc1))) {
        return false;
    }

    // Note: this might end up being outdated if a new display is added/removed,
    // or if HDR is toggled on them after swapchain creation (though it seems to be consistent between SDR and HDR).
    max_luminance = desc1.MaxLuminance;

    // HDR is not supported (this only works if HDR is enaged on the monitor that currently contains the swapchain)
    if (desc1.ColorSpace != DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020
        && desc1.ColorSpace != DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709)
    {
        return false;
    }

    return true;
}

inline void GetD3DName(ID3D11DeviceChild* obj, std::string& name) {
  if (obj == nullptr) {
    return;
  }

  // NOLINTNEXTLINE(modernize-avoid-c-arrays)
  char c_name[128] = {};
  UINT size = sizeof(name);
  if (obj->GetPrivateData(WKPDID_D3DDebugObjectName, &size, c_name) == S_OK) {
    name = c_name;
  }
}

inline void GetD3DName(ID3D12Resource* obj, std::string& name) {
  if (obj == nullptr) {
    return;
  }

  // NOLINTNEXTLINE(modernize-avoid-c-arrays)
  char c_name[128] = {};
  UINT size = sizeof(name);
  if (obj->GetPrivateData(WKPDID_D3DDebugObjectName, &size, c_name) == S_OK) {
    name = c_name;
  }
}

uint64_t GetResourceByViewHandle(DeviceData& data, uint64_t handle) {
  if (
      auto pair = data.resource_views.find(handle);
      pair != data.resource_views.end()) return pair->second;
  return 0;
}

std::string GetResourceNameByViewHandle(DeviceData& data, uint64_t handle) {
  if (!trace_names) return "?";
  auto resource_handle = GetResourceByViewHandle(data, handle);
  if (resource_handle == 0) return "?";
  if (!data.resources.contains(resource_handle)) return "?";

  if (
      auto pair = data.resource_names.find(resource_handle);
      pair != data.resource_names.end()) return pair->second;

  std::string name;
  if (data.device_api == reshade::api::device_api::d3d11) {
    auto* native_resource = reinterpret_cast<ID3D11DeviceChild*>(resource_handle);
    GetD3DName(native_resource, name);
  } else if (data.device_api == reshade::api::device_api::d3d12) {
    auto* native_resource = reinterpret_cast<ID3D12Resource*>(resource_handle);
    GetD3DName(native_resource, name);
  } else {
    name = "?";
  }
  if (!name.empty()) {
    data.resource_names[resource_handle] = name;
  }
  return name;
}

std::filesystem::path GetShaderPath() {
  // NOLINTNEXTLINE(modernize-avoid-c-arrays)
  wchar_t file_prefix[MAX_PATH] = L"";
  GetModuleFileNameW(nullptr, file_prefix, ARRAYSIZE(file_prefix));

  std::filesystem::path dump_path = file_prefix;
  dump_path = dump_path.parent_path();
  dump_path /= ".\\renodx-dev"; //TODOFT: rename shader folders
  return dump_path;
}

void DestroyPipelineSubojects(reshade::api::pipeline_subobject* subojects, uint32_t subobject_count) {
  for (uint32_t i = 0; i < subobject_count; ++i) {
    auto& suboject = subojects[i];

    switch (suboject.type) {
      case reshade::api::pipeline_subobject_type::vertex_shader:
        [[fallthrough]];
      case reshade::api::pipeline_subobject_type::compute_shader:
        [[fallthrough]];
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
  const std::lock_guard<std::recursive_mutex> lock(s_mutex_loading);
  auto custom_shader = custom_shaders_cache.find(shader_hash);
  if (custom_shader != custom_shaders_cache.end() && custom_shader->second != nullptr) {
    custom_shader->second->code.clear();
    custom_shader->second->is_hlsl = false;
    custom_shader->second->preprocessed_hash = 0;
    custom_shader->second->file_path.clear();
    custom_shader->second->compilation_error.clear();
  }
}

void UnloadCustomShaders(const std::unordered_set<uint64_t>& pipelines_filter = std::unordered_set<uint64_t>(), bool immediate = false, bool clean_custom_shader = true) {
  const std::lock_guard<std::recursive_mutex> lock(s_mutex_generic);
  for (auto& pair : pipeline_cache_by_pipeline_handle) {
    auto& cached_pipeline = pair.second;
    if (cached_pipeline == nullptr || (!pipelines_filter.empty() && !pipelines_filter.contains(cached_pipeline->pipeline.handle))) continue;

    // In case this is a full "unload" of all shaders
    if (pipelines_filter.empty()) {
      // Clear their compilation state, we might not have any other way of doing it.
      // Disable testing etc here, otherwise we might not always have a way to do it
      if (clean_custom_shader) {
        cached_pipeline->test = false;
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

void CompileCustomShaders(const std::unordered_set<uint64_t>& pipelines_filter = std::unordered_set<uint64_t>()) {
  auto directory = GetShaderPath();
  if (!std::filesystem::exists(directory)) {
    std::filesystem::create_directory(directory);
  }

  directory /= ".\\live";

  if (!std::filesystem::exists(directory)) {
    std::filesystem::create_directory(directory);
    return;
  }

  std::vector<std::string> shader_defines;
  {
      const std::lock_guard<std::recursive_mutex> lock(s_mutex_generic); //TODOFT5: thread safe? all around
      shader_defines.assign(shader_defines_data.size() * 2, "");
      for (uint32_t i = 0; i < shader_defines_data.size(); i++) {
          shader_defines[(i*2)] = shader_defines_data[i].compiled_data.name;
          shader_defines[(i*2)+1] = shader_defines_data[i].compiled_data.value;
      }
  }

  for (const auto& entry : std::filesystem::directory_iterator(directory)) {
    if (!entry.is_regular_file()) {
      reshade::log::message(reshade::log::level::warning, "loadCustomShaders(not a regular file)");
      continue;
    }
    const auto& entry_path = entry.path();
    const bool is_hlsl = entry_path.extension().compare(".hlsl") == 0;
    const bool is_cso = entry_path.extension().compare(".cso") == 0;
    if (!entry_path.has_extension() || !entry_path.has_stem() || (!is_hlsl && !is_cso)) {
      std::stringstream s;
      s << "loadCustomShaders(Missing extension or stem or unknown extension: ";
      s << entry_path.string();
      s << ")";
      reshade::log::message(reshade::log::level::warning, s.str().c_str());
      continue;
    }

    const auto filename_no_extension = entry_path.stem();
    const auto filename_no_extension_string = filename_no_extension.string();
    std::string hash_string;
    std::string shader_target;

    if (is_hlsl) {
      auto length = filename_no_extension_string.length();
      if (length < strlen("0x12345678.xx_x_x")) continue;
      shader_target = filename_no_extension_string.substr(length - strlen("xx_x_x"), strlen("xx_x_x"));
      if (shader_target[2] != '_') continue;
      if (shader_target[4] != '_') continue;
      hash_string = filename_no_extension_string.substr(length - strlen("12345678.xx_x_x"), HASH_CHARACTERS_LENGTH);
    } else if (is_cso) {
      // As long as cso starts from "0x12345678", it's good, they don't need the shader type specified
      if (filename_no_extension_string.size() < 10) {
        std::stringstream s;
        s << "loadCustomShaders(Invalid cso file format: ";
        s << filename_no_extension_string;
        s << ")";
        reshade::log::message(reshade::log::level::warning, s.str().c_str());
        continue;
      }
      hash_string = filename_no_extension_string.substr(2, HASH_CHARACTERS_LENGTH);

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

    uint32_t shader_hash;
    try {
      shader_hash = std::stoul(hash_string, nullptr, 16);
    } catch (const std::exception& e) {
      continue;
    }

    // Early out before compiling
    if (!pipelines_filter.empty()) {
      const std::lock_guard<std::recursive_mutex> lock(s_mutex_generic);
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

    char config_name[std::string_view("Shader#").size() + HASH_CHARACTERS_LENGTH + 1] = "";
    sprintf(&config_name[0], "Shader#%s", hash_string.c_str());

    const std::lock_guard<std::recursive_mutex> lock(s_mutex_loading);
    auto& custom_shader = custom_shaders_cache[shader_hash]; // Add default initialized shader
    const bool has_custom_shader = (custom_shaders_cache.find(shader_hash) != custom_shaders_cache.end()) && (custom_shader != nullptr);
    if (!has_custom_shader) {
      custom_shader = new CachedCustomShader();

      std::size_t preprocessed_hash = custom_shader->preprocessed_hash;
      const bool should_load_compiled_shader = is_hlsl; // If this shader doesn't have an hlsl, we should never read it or save it on disk, there's no need (we can still fall back on the original .cso if needed)
      // Note that if anybody manually changed the config hash, the data here could mismatch and end up recompiling when not needed or skipping recompilation even if needed (near impossible chance)
      if (should_load_compiled_shader && reshade::get_config_value(nullptr, NAME_ADVANCED_SETTINGS.c_str(), &config_name[0], preprocessed_hash)) {
          // This will load the matching cso
          if (renodx::utils::shader::compiler::LoadCompiledShaderFromFile(custom_shader->code, entry_path.c_str())) {
              // If both reading the pre-processor hash from config and the compiled shader from disk succeeded, then we are free to continue as if this shader was working
              custom_shader->file_path = entry_path;
              custom_shader->is_hlsl = is_hlsl;
              custom_shader->preprocessed_hash = preprocessed_hash;
              // Theoretically at this point, the shader pre-processor below should skip re-compiling this shader unless the hash changed
          }
      }
    }

    if (is_hlsl) {
        const auto previous_path = std::filesystem::current_path();
        // Set the current path to the shaders directory, it's needed by the DX compilers (specifically by the preprocess functions)
        //TODOFT5: this changes where Prey saves its log files: solution here https://github.com/clshortfuse/renodx/blob/4e368e50e612d57ca6ee13314fe931722f8e6403/src/utils/shader_compiler.hpp#L39 (and L341)
        std::filesystem::current_path(directory);

        //TODOFT5: keep the preprocessed blob produced from this and feed it to the compiler, otherwise it will do the preprocess step twice
        // Skip compiling the shader if it didn't change
        const bool needs_compilation = renodx::utils::shader::compiler::PreprocessShaderFromFile(entry_path.c_str(), entry_path.filename().c_str(), shader_target.c_str(), custom_shader->preprocessed_hash, shader_defines, &custom_shader->compilation_error);

        // Restore it to avoid unknown consequences
        std::filesystem::current_path(previous_path);

        if (!needs_compilation) {
          continue;
        }
    }

    // For extra safety, just clear everything that will be re-assigned below if this custom shader already existed
    if (has_custom_shader) {
      auto preprocessed_hash = custom_shader->preprocessed_hash;
      auto compilation_error = custom_shader->compilation_error;
      ClearCustomShader(shader_hash);
      // Keep the data we just filled up
      custom_shader->preprocessed_hash = preprocessed_hash;
      custom_shader->compilation_error = compilation_error;
    }
    custom_shader->file_path = entry_path;
    custom_shader->is_hlsl = is_hlsl;

    if (is_hlsl) {
      {
        std::stringstream s;
        s << "loadCustomShaders(Compiling file: ";
        s << entry_path.string();
        s << ", hash: " << PRINT_CRC32(shader_hash);
        s << ", target: " << shader_target;
        s << ")";
        reshade::log::message(reshade::log::level::debug, s.str().c_str());
      }

      custom_shader->code = renodx::utils::shader::compiler::CompileShaderFromFile(
          entry_path.c_str(),
          shader_target.c_str(),
          shader_defines,
          &custom_shader->compilation_error);

      if (custom_shader->code.empty()) {
        std::stringstream s;
        s << "loadCustomShaders(Compilation failed: ";
        s << entry_path.string();
        s << ")";
        reshade::log::message(reshade::log::level::warning, s.str().c_str());

        continue;
      }
      // Save the matching the pre-compiled shader hash in the config, so we can skip re-compilation on the next boot
      else {
        reshade::set_config_value(nullptr, NAME_ADVANCED_SETTINGS.c_str(), &config_name[0], custom_shader->preprocessed_hash);
      }

      {
        std::stringstream s;
        s << "loadCustomShaders(Shader built with size: " << custom_shader->code.size() << ")";
        reshade::log::message(reshade::log::level::debug, s.str().c_str());
      }
    }
    else if (is_cso) {
      std::ifstream file(entry_path, std::ios::binary);
      file.seekg(0, std::ios::end);
      custom_shader->code.resize(file.tellg());
      {
        std::stringstream s;
        s << "loadCustomShaders(Reading " << custom_shader->code.size() << " from " << filename_no_extension_string << ")";
        reshade::log::message(reshade::log::level::debug, s.str().c_str());
      }
      if (!custom_shader->code.empty()) {
        file.seekg(0, std::ios::beg);
        file.read(reinterpret_cast<char*>(custom_shader->code.data()), custom_shader->code.size());
      }
    }
  }

  // Only save after compiling, to make sure the config data aligns with the serialized compiled shaders data (blobs)
  {
      ShaderDefineData::Save(shader_defines_data);
  }
}

void LoadCustomShaders(const std::unordered_set<uint64_t>& pipelines_filter = std::unordered_set<uint64_t>(), bool recompile_shaders = true, bool immediate_load = true, bool immediate_unload = false) {
  reshade::log::message(reshade::log::level::debug, "loadCustomShaders()");

  if (recompile_shaders) {
    CompileCustomShaders(pipelines_filter);
  }

  // We can, and should, only lock this after compiling new shaders
  const std::lock_guard<std::recursive_mutex> lock(s_mutex_generic);

  // Clear all previously loaded custom shaders
  UnloadCustomShaders(pipelines_filter, immediate_unload, false);

  std::unordered_set<uint64_t> cloned_pipelines;

  const std::lock_guard<std::recursive_mutex> lock_loading(s_mutex_loading);
  for (const auto& custom_shader_pair : custom_shaders_cache) {
    uint32_t shader_hash = custom_shader_pair.first;
    const auto custom_shader = custom_shaders_cache[shader_hash];

    // Skip shaders that don't have code binaries at the moment
    if (custom_shader == nullptr || custom_shader->code.empty()) continue;

    auto pipelines_pair = pipeline_caches_by_shader_hash.find(shader_hash);
    if (pipelines_pair == pipeline_caches_by_shader_hash.end()) {
      std::stringstream s;
      s << "loadCustomShaders(Unknown hash: ";
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

      {
        std::stringstream s;
        s << "loadCustomShaders(Read ";
        s << custom_shader->code.size() << " bytes ";
        s << " from " << custom_shader->file_path.string();
        s << ")";
        reshade::log::message(reshade::log::level::debug, s.str().c_str());
      }

      // DX12 can use PSO objects that need to be cloned

      const uint32_t subobject_count = cached_pipeline->subobject_count;
      reshade::api::pipeline_subobject* subobjects = cached_pipeline->subobjects_cache;
      reshade::api::pipeline_subobject* new_subobjects = renodx::utils::pipeline::ClonePipelineSubObjects(subobject_count, subobjects);

      {
        std::stringstream s;
        s << "loadCustomShaders(Cloning pipeline ";
        s << reinterpret_cast<void*>(cached_pipeline->pipeline.handle);
        s << " with " << subobject_count << " object(s)";
        s << ")";
        reshade::log::message(reshade::log::level::debug, s.str().c_str());
      }
      reshade::log::message(reshade::log::level::debug, "Iterating pipeline...");

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
        // TODO(clshortfuse): Workaround leak
        std::memcpy(const_cast<void*>(new_desc->code), custom_shader->code.data(), custom_shader->code.size());

        const auto new_hash = compute_crc32(static_cast<const uint8_t*>(new_desc->code), new_desc->code_size);

        std::stringstream s;
        s << "loadCustomShaders(Injected pipeline data";
        s << " with " << PRINT_CRC32(new_hash);
        s << " (" << custom_shader->code.size() << " bytes)";
        s << ")";
        reshade::log::message(reshade::log::level::debug, s.str().c_str());
      }

      {
        std::stringstream s;
        s << "Creating pipeline clone (";
        s << "hash: " << PRINT_CRC32(shader_hash);
        s << ", layout: " << reinterpret_cast<void*>(cached_pipeline->layout.handle);
        s << ", subobject_count: " << subobject_count;
        s << ")";
        reshade::log::message(reshade::log::level::debug, s.str().c_str());
      }

      reshade::api::pipeline pipeline_clone;
      const bool built_pipeline_ok = cached_pipeline->device->create_pipeline(
          cached_pipeline->layout,
          subobject_count,
          new_subobjects,
          &pipeline_clone);
      std::stringstream s;
      s << "loadCustomShaders(cloned ";
      s << reinterpret_cast<void*>(cached_pipeline->pipeline.handle);
      s << " => " << reinterpret_cast<void*>(pipeline_clone.handle);
      s << ", layout: " << reinterpret_cast<void*>(cached_pipeline->layout.handle);
      s << ", size: " << subobject_count;
      s << ", " << (built_pipeline_ok ? "OK" : "FAILED!");
      s << ")";
      reshade::log::message(built_pipeline_ok ? reshade::log::level::info : reshade::log::level::error, s.str().c_str());

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

std::optional<std::string> ReadTextFile(const std::filesystem::path& path) {
  std::vector<uint8_t> data;
  std::optional<std::string> result;
  std::ifstream file(path, std::ios::binary);
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
  reshade::log::message(reshade::log::level::info, "Live callback.");
  // TODO: only re-load the shaders that were changed to improve performance, and also verify this is safe. Replacing shaders from another thread at a random time could break as we need to wait one frame or the pipeline binding could hang.
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

    directory /= ".\\live";

    if (!std::filesystem::exists(directory)) {
      std::filesystem::create_directory(directory);
    }

    reshade::log::message(reshade::log::level::info, "Watching live");

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
    {
      std::stringstream s;
      s << "ToggleLiveWatching(targetHandle: ";
      s << reinterpret_cast<void*>(m_target_dir_handle);
      reshade::log::message(reshade::log::level::info, s.str().c_str());
    }

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
      reshade::log::message(reshade::log::level::info, "ToggleLiveWatching(ReadDirectoryChangesExW: Listening.)");
    } else {
      std::stringstream s;
      s << "ToggleLiveWatching(ReadDirectoryChangesExW: Failed: ";
      s << GetLastError();
      s << ")";
      reshade::log::message(reshade::log::level::error, s.str().c_str());
    }

    LoadCustomShaders();
  } else {
    reshade::log::message(reshade::log::level::info, "Cancelling live");
    CancelIoEx(m_target_dir_handle, &overlapped);
  }
}

void OnInitDevice(reshade::api::device* device) {
  auto& device_data = device->create_private_data<DeviceData>();
  device_data.device_api = device->get_api();

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

#if ENABLE_NGX
  assert(!NGX::DLSS::HasInit()); // DLSS is only supported on one device at a time
  if (dlss_sr) {
      ID3D11Device* native_device = (ID3D11Device*)(device->get_native());
      com_ptr<IDXGIDevice> native_dxgi_device;
      HRESULT hr = native_device->QueryInterface(&native_dxgi_device);
      com_ptr<IDXGIAdapter> native_adapter;
      if (SUCCEEDED(hr))
      {
          hr = native_dxgi_device->GetAdapter(&native_adapter);
      }
      assert(SUCCEEDED(hr));

      dlss_sr_supported = NGX::DLSS::Init(native_device, native_adapter.get());
      if (!dlss_sr_supported) {
#if !DLSS_TARGET_TEXTURE_WAS_UAV
          dlss_output_color = nullptr;
#endif // DLSS_TARGET_TEXTURE_WAS_UAV
          dlss_exposure = nullptr;
          dlss_motion_vectors = nullptr;
          dlss_motion_vectors_rtv = nullptr;
          dlss_sr_render_resolution = 1.0;
          NGX::DLSS::Deinit(native_device); // No need to keep it initialized
          dlss_sr = false;
      }
  }
#endif // NGX
}

void OnDestroyDevice(reshade::api::device* device) {
  auto& device_data = device->get_private_data<DeviceData>(); // No need to lock the data mutex here, it could be concurrently used at this point

  device->destroy_pipeline_layout(device_data.ui_pipeline_layout);
  device->destroy_pipeline_layout(device_data.shared_data_pipeline_layout);
  device->destroy_pipeline_layout(device_data.settings_pipeline_layout);

  assert(cb_per_view_global_buffer_map_data == nullptr);
  cb_per_view_global_buffer = nullptr;
#if OLD_GLOBAL_BUFFER_INTERCEPT_METHOD
  cb_per_view_global_buffer_copy = nullptr;
#endif

#if REPLACE_SAMPLERS_LIVE
  {
      const std::lock_guard<std::recursive_mutex> lock_samplers(s_mutex_samplers);
      ASSERT_ONCE(custom_sampler_by_original_sampler.empty()); // Is this guaranteed in DX? Maybe not, but probably is!
      custom_sampler_by_original_sampler.clear();
  }
#endif

#if ENABLE_NGX
  ID3D11Device* native_device = (ID3D11Device*)(device->get_native());
  NGX::DLSS::Deinit(native_device); // NOTE: this could stutter the game on closure as it forces unloading the DLSS DLL, we could theoretically avoid it (not sure if we'd run into errors)
#if !DLSS_TARGET_TEXTURE_WAS_UAV
  dlss_output_color = nullptr;
#endif // DLSS_TARGET_TEXTURE_WAS_UAV
  dlss_exposure = nullptr;
  dlss_motion_vectors = nullptr;
  dlss_motion_vectors_rtv = nullptr;
#endif // NGX

  device->destroy_private_data<DeviceData>();
}

void OnInitSwapchain(reshade::api::swapchain* swapchain) {
  const size_t back_buffer_count = swapchain->get_back_buffer_count();
  auto& swapchain_data = swapchain->create_private_data<SwapchainData>();
  for (uint32_t index = 0; index < back_buffer_count; index++) {
    auto buffer = swapchain->get_back_buffer(index);
    swapchain_data.back_buffers.emplace(buffer.handle);
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
  if (SUCCEEDED(hr))
  {
      output_resolution.x = swapchain_desc.BufferDesc.Width;
      output_resolution.y = swapchain_desc.BufferDesc.Height;
      //ASSERT_ONCE(output_resolution.x != output_resolution.y); // Square resolutions are NOT supported on the swapchain by Luma, as we use that information to identify if some passes are shadow map projections (separate views) //TODOFT: delete? we aren't relying on this anymore
      render_resolution.x = output_resolution.x;
      render_resolution.y = output_resolution.y;
  }
  
  IDXGISwapChain3* native_swapchain3;
  hr = native_swapchain->QueryInterface(&native_swapchain3);
  ASSERT_ONCE(SUCCEEDED(hr));
  if (native_swapchain3 != nullptr)
  {
      hdr_supported = GetHDRMaxLuminance(native_swapchain3, default_user_peak_white);
      cb_luma_frame_settings.ScenePeakWhite = default_user_peak_white;
      native_swapchain3->Release();
  }
}

void OnDestroySwapchain(reshade::api::swapchain* swapchain) {
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
  const std::lock_guard<std::recursive_mutex> lock(s_mutex_generic);

  reshade::api::pipeline_subobject* subobjects_cache = renodx::utils::pipeline::ClonePipelineSubObjects(subobject_count, subobjects);

  auto* cached_pipeline = new CachedPipeline{
      pipeline,
      device,
      layout,
      subobjects_cache,
      subobject_count};

  bool found_replaceable_shader = false;
  bool found_custom_shader_file = false;

  for (uint32_t i = 0; i < subobject_count; ++i) {
    const auto& subobject = subobjects[i];
    for (uint32_t j = 0; j < subobject.count; ++j) {
      switch (subobject.type) {
        case reshade::api::pipeline_subobject_type::hull_shader:
        case reshade::api::pipeline_subobject_type::domain_shader:
        case reshade::api::pipeline_subobject_type::geometry_shader:
        case reshade::api::pipeline_subobject_type::blend_state:
          break;
        case reshade::api::pipeline_subobject_type::vertex_shader:
        case reshade::api::pipeline_subobject_type::compute_shader:
        case reshade::api::pipeline_subobject_type::pixel_shader: {
          auto* new_desc = static_cast<reshade::api::shader_desc*>(subobjects_cache[i].data);
          if (new_desc->code_size == 0) break;
          auto shader_hash = compute_crc32(static_cast<const uint8_t*>(new_desc->code), new_desc->code_size);

          {
            const std::lock_guard<std::recursive_mutex> lock_dumping(s_mutex_dumping);

            // Delete any previous shader with the same hash (unlikely to happen, but safer nonetheless)
            if (auto previous_shader_pair = shader_cache.find(shader_hash); previous_shader_pair != shader_cache.end() && previous_shader_pair->second != nullptr) {
              auto& previous_shader = previous_shader_pair->second;
              // Make sure that two shaders have the same hash, their code size also matches (theoretically we could check even more, but the chances hashes overlapping is extremely small)
              assert(previous_shader->size == new_desc->code_size);
              shader_cache_count--;
              delete previous_shader->data;
              delete previous_shader;
            }

            // Cache shader
            auto* cache = new CachedShader{
                malloc(new_desc->code_size),
                new_desc->code_size,
                subobject.type};
            std::memcpy(cache->data, new_desc->code, cache->size);
            shader_cache_count++;
            shader_cache[shader_hash] = cache;
            shaders_to_dump.emplace(shader_hash);
          }

          // Indexes
          assert(std::find(cached_pipeline->shader_hashes.begin(), cached_pipeline->shader_hashes.end(), shader_hash) == cached_pipeline->shader_hashes.end());
          cached_pipeline->shader_hashes.emplace_back(shader_hash);

          // Make sure we didn't already have a valid pipeline in there (this should never happen)
          auto pipelines_pair = pipeline_caches_by_shader_hash.find(shader_hash);
          if (pipelines_pair != pipeline_caches_by_shader_hash.end()) {
            pipelines_pair->second.emplace(cached_pipeline);
          } else {
            pipeline_caches_by_shader_hash[shader_hash] = { cached_pipeline };
          }
          found_replaceable_shader = true;
          {
            const std::lock_guard<std::recursive_mutex> lock(s_mutex_loading);
            found_custom_shader_file |= custom_shaders_cache.contains(shader_hash);
          }

#if DEVELOPMENT
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
        default:
          break;
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
    const std::lock_guard<std::recursive_mutex> lock_loading(s_mutex_loading);
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
  const std::lock_guard<std::recursive_mutex> lock(s_mutex_generic);

  {
    const std::lock_guard<std::recursive_mutex> lock_loading(s_mutex_loading);
    pipelines_to_reload.erase(pipeline.handle);
  }

  if (
      auto pipeline_cache_pair = pipeline_cache_by_pipeline_handle.find(pipeline.handle);
      pipeline_cache_pair != pipeline_cache_by_pipeline_handle.end()) {
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
  const std::lock_guard<std::recursive_mutex> lock(s_mutex_generic);

  if ((stages & reshade::api::pipeline_stage::compute_shader) != 0)
  {
      ASSERT_ONCE(stages == reshade::api::pipeline_stage::compute_shader || stages == reshade::api::pipeline_stage::all); // Make sure only one stage happens at a time
      pipeline_state_original_compute_shader = pipeline;
  }
  if ((stages & reshade::api::pipeline_stage::vertex_shader) != 0)
  {
      ASSERT_ONCE(stages == reshade::api::pipeline_stage::vertex_shader || stages == reshade::api::pipeline_stage::all); // Make sure only one stage happens at a time
      pipeline_state_original_vertex_shader = pipeline;
  }
  if ((stages & reshade::api::pipeline_stage::pixel_shader) != 0)
  {
      ASSERT_ONCE(stages == reshade::api::pipeline_stage::pixel_shader || stages == reshade::api::pipeline_stage::all); // Make sure only one stage happens at a time
      pipeline_state_original_pixel_shader = pipeline;
  }

  auto pair = pipeline_cache_by_pipeline_handle.find(pipeline.handle);
  if (pair == pipeline_cache_by_pipeline_handle.end() || pair->second == nullptr) return;

  auto* cached_pipeline = pair->second;

  if (cached_pipeline->test) {
    // This will make the shader output black, or skip drawing, so we can easily detect it. This might not be very safe but seems to work in DX11.
    // TODO: replace the pipeline with a shader that outputs all "SV_Target" as purple for more visiblity,
    // or return false in "reshade::addon_event::draw_or_dispatch_indirect" and similar draw calls to prevent them from being drawn.
    cmd_list->bind_pipeline(stages, reshade::api::pipeline{0});
  }
  else if (cached_pipeline->cloned && cached_pipeline->ready_for_binding) {
    cmd_list->bind_pipeline(stages, cached_pipeline->pipeline_clone);
  }

#if DEVELOPMENT
  if (!trace_running) return;

  bool add_pipeline_trace = true;
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

  // Pipelines are always "unique"
  if (add_pipeline_trace) {
    trace_pipeline_handles.push_back(cached_pipeline->pipeline.handle);
  }

  for (auto shader_hash : cached_pipeline->shader_hashes) {
    if (!trace_list_unique_shaders_only || std::find(trace_shader_hashes.begin(), trace_shader_hashes.end(), shader_hash) == trace_shader_hashes.end()) {
      trace_shader_hashes.push_back(shader_hash);
    }
  }
#endif // DEVELOPMENT
}

void OnPresent(
    reshade::api::command_queue* queue,
    reshade::api::swapchain* swapchain,
    const reshade::api::rect* source_rect,
    const reshade::api::rect* dest_rect,
    uint32_t dirty_rect_count,
    const reshade::api::rect* dirty_rects) {
  ASSERT_ONCE(!has_drawn_main_post_processing || found_per_view_globals); // We failed to find and assign global cbuffer 13 this frame //TODOFT4: this can trigger once when enabling/disabling dynamic resolution. Just ignore it for 1 frame or find a hook for it? (there's another identical TODO somewhere). It probably fails the threshold checks we have for the cbuffer 13 values (fixed?)
  ASSERT_ONCE(has_drawn_composed_gbuffers == has_drawn_main_post_processing); // Why is g-buffer composition drawing but post processing isn't?
  //TODOFT3: replace some instances of "has_drawn_main_post_processing" and "has_drawn_main_post_processing_previous" with "has_drawn_composed_gbuffers" (and its previous state)?
  if (has_drawn_main_post_processing)
  {
      previous_prey_taa_enabled[1] = previous_prey_taa_enabled[0];
      previous_prey_taa_enabled[0] = prey_taa_enabled;
  }
  else
  {
      previous_prey_taa_enabled[1] = false;
      previous_prey_taa_enabled[0] = false;
#if 0 // We leave this flag on as it's just for ImGUI stuff
      prey_taa_detected = false;
#endif
      // Theoretically we turn this flag off one frame late (or well, at the end of the frame),
      // but then again, if no scene rendered, this flag wouldn't have been used for anything.
      cb_luma_frame_settings.DLSS = 0;
  }
  has_drawn_composed_gbuffers = false;
  has_drawn_motion_blur_previous = has_drawn_motion_blur;
  has_drawn_motion_blur = false;
  has_drawn_tonemapping = false;
  has_drawn_main_post_processing_previous = has_drawn_main_post_processing;
  has_drawn_main_post_processing = false;
  has_drawn_upscaling = false;
  has_drawn_dlss_sr = false;
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
#endif // DEVELOPMENT

#if ENABLE_NGX
  auto* device = queue->get_device();
  ID3D11Device* native_device = (ID3D11Device*)(device->get_native());
  // We wouldn't really need to do anything other than clearing "dlss_output_color",
  // but to avoid wasting memory allocated by DLSS texture and other resources,
  // clear it up once disabled.
  if (dlss_sr != NGX::DLSS::HasInit()) {
      if (dlss_sr) {
          com_ptr<IDXGIDevice> native_dxgi_device;
          HRESULT hr = native_device->QueryInterface(&native_dxgi_device);
          com_ptr<IDXGIAdapter> native_adapter;
          if (SUCCEEDED(hr))
          {
              hr = native_dxgi_device->GetAdapter(&native_adapter);
          }
          assert(SUCCEEDED(hr));

          dlss_sr = NGX::DLSS::Init(native_device, native_adapter.get()); // No need to update "dlss_sr_supported"
      }
      else {
#if !DLSS_TARGET_TEXTURE_WAS_UAV
          dlss_output_color = nullptr;
#endif // DLSS_TARGET_TEXTURE_WAS_UAV
          dlss_exposure = nullptr;
          dlss_motion_vectors = nullptr;
          dlss_motion_vectors_rtv = nullptr;
          dlss_sr_render_resolution = 1.0; // Reset this to 0 when DLSS is toggled, even if "prey_drs_detected" is still true, we'll set it back to a low value if DRS is used again.
#if !DLSS_KEEP_DLL_LOADED // This will actually unload the DLSS DLL and all, making the game hitch, so it's better to just keep it in memory
          NGX::DLSS::Deinit(native_device);
#endif
      }
  }
#endif // NGX
}

enum LumaConstantBufferType {
    LumaSettings,
    LumaData,
    LumaLumaUIData
};

void SetPreyLumaConstantBuffers(reshade::api::command_list* cmd_list, reshade::api::shader_stage stages, reshade::api::pipeline_layout layout, LumaConstantBufferType type)
{
    switch (type)
    {
    case LumaConstantBufferType::LumaSettings:
    {
        cmd_list->push_constants(
            stages,
            layout,
            0,
            0,
            sizeof(LumaFrameSettings) / sizeof(uint32_t),
            &cb_luma_frame_settings);
        break;
    }
    case LumaConstantBufferType::LumaData:
    {
        LumaFrameData frame_data;
        frame_data.PostEarlyUpscaling = has_drawn_dlss_sr && !has_drawn_upscaling;
        frame_data.DummyPadding = 0;
        frame_data.CameraJitters = projection_jitters;
        frame_data.PreviousCameraJitters = previous_projection_jitters;
        frame_data.RenderResolutionScale.x = render_resolution.x / output_resolution.x;
        frame_data.RenderResolutionScale.y = render_resolution.y / output_resolution.y;
        // Always do this relative to the current output resolution
        frame_data.PreviousRenderResolutionScale.x = previous_render_resolution.x / output_resolution.x;
        frame_data.PreviousRenderResolutionScale.y = previous_render_resolution.y / output_resolution.y;
        frame_data.ViewProjectionMatrix = cb_per_view_global.CV_ViewProjMatr; //TODOFT3: delete? are we using these?
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
    case LumaConstantBufferType::LumaLumaUIData:
    {
        ASSERT_ONCE(false); // Not implemented (yet?)
        break;
    }
    }
}

bool HandlePreDraw(reshade::api::command_list* cmd_list, bool is_dispatch = false) {
  const auto* device = cmd_list->get_device();
  auto device_api = device->get_api();
  ID3D11Device* native_device = (ID3D11Device*)(device->get_native());
  ID3D11DeviceContext* native_device_context = (ID3D11DeviceContext*)(cmd_list->get_native());
  auto& device_data = device->get_private_data<DeviceData>();

  reshade::api::shader_stage stages = reshade::api::shader_stage::all_graphics | reshade::api::shader_stage::all_compute;

  std::unordered_set<uint64_t> back_buffers;

  reshade::api::pipeline_layout settings_pipeline_layout(0);
  reshade::api::pipeline_layout shared_data_pipeline_layout(0);
  reshade::api::pipeline_layout ui_pipeline_layout(0);

  // Lock for the shortest amount possible
  {
    const std::unique_lock lock(device_data.mutex);
    settings_pipeline_layout = device_data.settings_pipeline_layout;
    ui_pipeline_layout = device_data.ui_pipeline_layout;
    shared_data_pipeline_layout = device_data.shared_data_pipeline_layout;
    back_buffers = device_data.back_buffers;
  }

  bool is_custom_pass = false;

  std::vector<uint32_t> original_shader_hashes;

#if 1
#if DEVELOPMENT
  last_drawn_shader = "";
#endif //DEVELOPMENT
  if (is_dispatch) {
      if (pipeline_state_original_compute_shader.handle != 0)
      {
          const auto pipeline_pair = pipeline_cache_by_pipeline_handle.find(pipeline_state_original_compute_shader.handle);
          if (pipeline_pair != pipeline_cache_by_pipeline_handle.end() && pipeline_pair->second != nullptr) {
              original_shader_hashes = pipeline_pair->second->shader_hashes;
#if DEVELOPMENT
              last_drawn_shader = original_shader_hashes.empty() ? "" : std::format("{:x}", original_shader_hashes[0]);
#endif //DEVELOPMENT
              is_custom_pass = pipeline_pair->second->cloned;
              stages = reshade::api::shader_stage::compute;
          }
      }
  }
  else {
      if (pipeline_state_original_vertex_shader.handle != 0)
      {
          const auto pipeline_pair = pipeline_cache_by_pipeline_handle.find(pipeline_state_original_vertex_shader.handle);
          if (pipeline_pair != pipeline_cache_by_pipeline_handle.end() && pipeline_pair->second != nullptr) {
              original_shader_hashes = pipeline_pair->second->shader_hashes;
              is_custom_pass = pipeline_pair->second->cloned;
              stages = reshade::api::shader_stage::vertex;
          }
      }
      if (pipeline_state_original_pixel_shader.handle != 0)
      {
          const auto pipeline_pair = pipeline_cache_by_pipeline_handle.find(pipeline_state_original_pixel_shader.handle);
          if (pipeline_pair != pipeline_cache_by_pipeline_handle.end() && pipeline_pair->second != nullptr) {
              original_shader_hashes.insert(original_shader_hashes.end(), pipeline_pair->second->shader_hashes.begin(), pipeline_pair->second->shader_hashes.end());
#if DEVELOPMENT
              last_drawn_shader = original_shader_hashes.empty() ? "" : std::format("{:x}", original_shader_hashes[original_shader_hashes.size()-1]);
#endif //DEVELOPMENT
              is_custom_pass |= pipeline_pair->second->cloned;
              stages |= reshade::api::shader_stage::pixel;
          }
      }
  }
#else
  const auto pipeline_pair;
  if (is_dispatch) {
    com_ptr<ID3D11ComputeShader> cs;
    native_device_context->CSGetShader(&cs, nullptr, 0);
    is_custom_pass |= cs.get() ? false : pipeline_cache_by_pipeline_clone_handle.contains((uint64_t)cs.get());
    stages = reshade::api::shader_stage::compute;
    original_shader_hashes...
  }
  else {
    com_ptr<ID3D11VertexShader> vs;
    com_ptr<ID3D11PixelShader> ps;
    native_device_context->VSGetShader(&vs, nullptr, 0);
    native_device_context->PSGetShader(&ps, nullptr, 0);
    // Pipelines are actually just shaders pointers in DX11
    is_custom_pass |= (vs.get() ? false : pipeline_cache_by_pipeline_clone_handle.contains((uint64_t)vs.get())) || (ps.get() ? false : pipeline_cache_by_pipeline_clone_handle.contains((uint64_t)ps.get()));
    stages = reshade::api::shader_stage::vertex | reshade::api::shader_stage::pixel;
    pipeline_pair = pipeline_cache_by_pipeline_clone_handle.find((uint64_t)ps.get());
    original_shader_hashes...
  }
#endif

  bool is_drawing_lens_optics = false;
//TODOFT4: disable DLSS output scaling on lens effects passes that run in (arbitrary?) lower resolutions? Or fix them (is their render target large enough?)!
//If we can't manage to get that fixed, we can always start upscaling once we detect the "post AA composite" is about to run, and then replace the cbuffers (not really needed) and call it a day.
//lens effects would be lower quality but who cares? Actually currently the only problem with viewport is in "LensOptics_ImageSpaceShafts_shaftsOcc_0x4435D741.ps_5_0.hlsl",
//all other lens optics are problably fine! We seem to be able to detect that one just fine!
//The solution is probably to simply check the render target and set the viewport to the same resolution as it!?
//UPDATE: we did it, so nuke all DLSS_UPSCALE_LENS_EFFECTS code...
#if DLSS_UPSCALE_LENS_EFFECTS && 0 // We aren't doing anything with this atm
  for (auto shader_hash : shader_hashes_LensOptics) {
      if (std::find(original_shader_hashes.begin(), original_shader_hashes.end(), shader_hash) != original_shader_hashes.end()) {
          is_drawing_lens_optics = true;
          break;
      }
  }
#endif

//TODOFT4: it seems like this assert (or something like this) can happen when DRS changes res to quick or is toggled between frames.
//Somehow the wrong rend res can persist in our data from a global cbuffer 13 set that wasn't meant for the main scene rendering but some side rendering with a separate res.
//Could it be that we get a swapchain resize event too late and thus our output resolution isn't updated quick enough? (not it can't be because the output res is ... unchanged with DRS).
//We should probably find a moment where we absolutely stop taking in new cbuffer 13 values, one fixed point in the pipeline (e.g. blur?, AO?, scene composion? ...).
#if DEVELOPMENT && 0 // We are setting the viewport below now, no need to verify it was already right
  if (has_drawn_dlss_sr && !has_drawn_upscaling /*&& !is_drawing_lens_optics*/)
  {
      D3D11_VIEWPORT viewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
      UINT viewports_num = 1;
      native_device_context->RSGetViewports(&viewports_num, nullptr);
      ASSERT_ONCE(viewports_num == 1); // Possibly innocuous as long as it's > 0
      native_device_context->RSGetViewports(&viewports_num, &viewports[0]);
      ASSERT_ONCE(viewports[0].TopLeftX == 0 && viewports[0].TopLeftY == 0
          && ((viewports[0].Width == std::lrintf(render_resolution.x) && viewports[0].Height == std::lrintf(render_resolution.y))
              || (viewports[0].Width == std::lrintf(output_resolution.x) && viewports[0].Height == std::lrintf(output_resolution.y))));
  }
#endif // DEVELOPMENT

  if (!original_shader_hashes.empty()) {

      if (!has_drawn_composed_gbuffers)
      {
          for (auto shader_hash : shader_hashes_TiledShadingTiledDeferredShading) {
              if (std::find(original_shader_hashes.begin(), original_shader_hashes.end(), shader_hash) != original_shader_hashes.end()) {
                  has_drawn_composed_gbuffers = true;
                  break;
              }
          }
      }
      if (!has_drawn_tonemapping)
      {
          for (auto shader_hash : shader_hashes_HDRPostProcessHDRFinalScene) {
              if (std::find(original_shader_hashes.begin(), original_shader_hashes.end(), shader_hash) != original_shader_hashes.end()) {
                  has_drawn_tonemapping = true;
                  break;
              }
          }
      }
      // Note: this doesn't always run, it's based on a user setting!
      if (!has_drawn_motion_blur)
      {
          for (auto shader_hash : shader_hashes_MotionBlur) {
              if (std::find(original_shader_hashes.begin(), original_shader_hashes.end(), shader_hash) != original_shader_hashes.end()) {
                  has_drawn_motion_blur = true;
                  break;
              }
          }
      }
      if (!has_drawn_main_post_processing)
      {
          //TODO LUMA: optimize these shader searches by simply marking "CachedPipeline" with a tag on what they are (and whether they have a particular role) (also we can restrict the search to pixel shaders)
          // This is the last known pass that is guaranteed to run before UI draws in
          for (auto shader_hash : shader_hashes_PostAAComposites) {
              if (std::find(original_shader_hashes.begin(), original_shader_hashes.end(), shader_hash) != original_shader_hashes.end()) {
                  has_drawn_main_post_processing = true;
                  // If DRS is not currently running, upscaling won't happen
                  if (!prey_drs_active)
                  {
                      has_drawn_upscaling = true;
                  }
                  break;
              }
          }
      }
      if (!has_drawn_upscaling)
      {
           if (std::find(original_shader_hashes.begin(), original_shader_hashes.end(), shader_hash_PostAAUpscaleImage) != original_shader_hashes.end()) {
               has_drawn_upscaling = true;
               assert(has_drawn_main_post_processing && prey_drs_active);
           }

           //TODOFT: new (unified) implementation of "DLSS_UPSCALE_LENS_EFFECTS"
           // Between DLSS SR and upscaling, force the viewport to the full render target resolution at all times, because we upscaled early.
           // Usually this matches the swapchain output resolution, but some lens optics passes actually draw on textures with a different resolution (independently of the game render/output res).
           if (has_drawn_dlss_sr && !has_drawn_upscaling && prey_drs_active)
           {
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
               if (state.get())
               {
                   D3D11_RASTERIZER_DESC state_desc;
                   state->GetDesc(&state_desc);
                   if (state_desc.ScissorEnable)
                   {
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
               for (uint32_t i = 0; i < viewports_num; i++)
               {
                   viewports[i].Width = render_target_texture_2d_desc.Width;
                   viewports[i].Height = render_target_texture_2d_desc.Height;
               }
               native_device_context->RSSetViewports(viewports_num, &viewports[0]);
           }
      }

#if ENABLE_NGX
      if (is_custom_pass && dlss_sr) {
          //TODOFT (TODO LUMA): make sure DLSS lets scRGB colors pass through, or move this before tonemap/blur (after exposure)...
          //TODOFT: add sharpening if we did DLSS? It's already in! Do RCAS?
          //TODOFT: skip SMAA edge detection and edge AA passes, or just disable SMAA in menu
          //TODOFT: skip the texture copy into ps_shader_resources[1] after TAA if DLSS is running? It's not really necessary AFAIK (though it might be used by other things in the game, it's not clear, but likely not...)
          //TODOFT: Enable "DLSS_TARGET_TEXTURE_WAS_UAV" by changing the buffer to UAV with Luma DLL?
          //TODOFT1: force preset E even with DLAA? Nah, F looks better it seems? Right now preset F is used for DRS (or is it?)!! (try C instead of F in that case though!). Or simply expose it to users, or at least try it for dev settings.
          
          //TODO LUMA: move DLSS before tonemapping, depth of field, bloom and blur. It wouldn't be easy because exposure is calculated after blur in CryEngine,
          //but we could simply fall back on using DLSS Auto Exposure (even if that wouldn't match the actual value used by post processing...).
          //To achieve that, we need to add both DRS+DLSS scaling support to all shaders that run after DLSS, as DLSS would upscale the image before the final upscale pass.
          //Sun shafts and lens optics effects would (actually, could) draw in native resolution after upscaling then.
          //Overall that solution has no downsides other than the difficulty of running multiple passes at a different resolution (which really isn't hard as we already have a set up for it).
          
          // We do DLSS after some post processing (e.g. exposure, tonemap, color grading, bloom, blur, objects highlight, other possible AA forms, etc) because running it before post processing
          // would be harder (we'd need to collect more textures manually and manually skip all later AA steps), most importantly, that wouldn't work with the native dynamic resolution the game supports (without changing every single
          // texture sample coordinates in post processing). Even if it's after pp, it should still have enough quality.
          // We replace the "TAA"/"SMAA 2TX" pass, ignoring whatever it would have done (the secondary texture it allocated is kept alive, even if we don't use it, we couldn't really destroy it from ReShade),
          // after there's a "composition" pass (film grain, sharpening, ...) and then an optional upscale pass, both of these are too late for DLSS to run.
          for (auto shader_hash : shader_hashes_PostAA) {
              if (std::find(original_shader_hashes.begin(), original_shader_hashes.end(), shader_hash) != original_shader_hashes.end()) {
                  ASSERT_ONCE(prey_taa_detected); // Why did we get here without TAA enabled?
                  com_ptr<ID3D11ShaderResourceView> ps_shader_resources[17];
                  // 0 current color source
                  // 1 previous color source (post TAA)
                  // 2 depth (0-1 being near-far)
                  // 3 motion vectors (dynamic objects movement only, no camera movement (if not baked in the dynamic objects))
                  // 16 device depth (inverted depth, used by stencil)
                  native_device_context->PSGetShaderResources(0, ARRAYSIZE(ps_shader_resources), reinterpret_cast<ID3D11ShaderResourceView**>(ps_shader_resources));

                  com_ptr<ID3D11RenderTargetView> render_target_view;
                  com_ptr<ID3D11DepthStencilView> depth_stencil_view;
                  native_device_context->OMGetRenderTargets(1, &render_target_view, &depth_stencil_view);

                  const bool dlss_inputs_valid = ps_shader_resources[0].get() != nullptr && ps_shader_resources[16].get() != nullptr && ps_shader_resources[3].get() != nullptr && render_target_view != nullptr;
                  ASSERT_ONCE(dlss_inputs_valid);
                  if (dlss_inputs_valid) {
                      com_ptr<ID3D11Resource> output_colorTemp;
                      render_target_view->GetResource(&output_colorTemp);
                      com_ptr<ID3D11Texture2D> output_color;
                      HRESULT hr = output_colorTemp->QueryInterface(&output_color);
                      ASSERT_ONCE(SUCCEEDED(hr));

                      D3D11_TEXTURE2D_DESC output_texture_desc;
                      output_color->GetDesc(&output_texture_desc);

#if DLSS_UPSCALE_LENS_EFFECTS && 0
                      D3D11_VIEWPORT dlss_viewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
                      UINT dlss_viewports_num = 1;
                      native_device_context->RSGetViewports(&dlss_viewports_num, nullptr);
                      native_device_context->RSGetViewports(&dlss_viewports_num, &dlss_viewports[0]);
#endif
                      
                      ASSERT_ONCE(std::lrintf(output_resolution.x) == output_texture_desc.Width && std::lrintf(output_resolution.y) == output_texture_desc.Height);
                      std::array<uint32_t, 2> dlss_render_resolution = FindClosestIntegerResolutionForAspectRatio((double)output_texture_desc.Width * (double)dlss_sr_render_resolution, (double)output_texture_desc.Height * (double)dlss_sr_render_resolution, (double)output_texture_desc.Width / (double)output_texture_desc.Height);
                      // The "HDR" flag in DLSS SR actually means whether the color is in linear space or "sRGB gamma" (SDR) space
                      bool dlss_hdr = uint8_t(shader_defines_data[post_process_space_define_index].compiled_data.value[0] - '0') >= 1; // "POST_PROCESS_SPACE_TYPE" (we are assuming the value was always a number and not empty)

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
                      NGX::DLSS::UpdateSettings(native_device, native_device_context, output_texture_desc.Width, output_texture_desc.Height, dlss_render_resolution[0], dlss_render_resolution[1], dlss_hdr, dlss_mode);

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
                      ASSERT_ONCE(render_target_view == render_target_view_post && depth_stencil_view == depth_stencil_view_post);

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
#if DLSS_TARGET_TEXTURE_WAS_UAV
                      com_ptr<ID3D11Texture2D> dlss_output_color = output_color;
#else
                      output_texture_desc.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;

                      if (dlss_output_color.get())
                      {
                          D3D11_TEXTURE2D_DESC dlss_output_texture_desc;
                          dlss_output_color->GetDesc(&dlss_output_texture_desc);
                          dlss_output_changed = dlss_output_texture_desc.Width != output_texture_desc.Width || dlss_output_texture_desc.Height != output_texture_desc.Height || dlss_output_texture_desc.Format != output_texture_desc.Format;
                      }
                      if (!dlss_output_color.get() || dlss_output_changed)
                      {
                          dlss_output_color = nullptr; // Make sure we discard the previous one
                          hr = native_device->CreateTexture2D(&output_texture_desc, nullptr, &dlss_output_color);
                          ASSERT_ONCE(SUCCEEDED(hr));
                      }
                      if (!dlss_output_color.get()) {
                          skip_dlss = true;
                      }
#endif

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

                          //TODOFT: compatibility with "DELAY_HDR_TONEMAP" paper white? Try to set it to 0.5 or 0.18?
                          // Generate "fake" exposure texture
                          bool exposure_changed = false;
#if DEVELOPMENT
                          static float previous_dlss_custom_exposure = dlss_custom_exposure; //TODOFT: set this to 203/80 or 1/(203/80)?
                          exposure_changed = dlss_custom_exposure != previous_dlss_custom_exposure;
                          previous_dlss_custom_exposure = dlss_custom_exposure;
#endif // DEVELOPMENT
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
                              exposure_texture_data.pSysMem = &dlss_custom_exposure; // This needs to be "static" data in case the texture initialization was somehow delayed and read the data after the stack destroyed it
                              exposure_texture_data.SysMemPitch = 32;
                              exposure_texture_data.SysMemSlicePitch = 32;

                              dlss_exposure = nullptr; // Make sure we discard the previous one
                              hr = native_device->CreateTexture2D(&exposure_texture_desc, &exposure_texture_data, &dlss_exposure);
                              assert(SUCCEEDED(hr));
                          }

                          // Generate motion vectors from the objects velocity buffer and the camera movement.
                          // We take advantage of the state the game had set DX to, and simply swap the render target.
                          {
                              D3D11_TEXTURE2D_DESC object_velocity_texture_desc;
                              object_velocity_buffer->GetDesc(&object_velocity_texture_desc);
                              ASSERT_ONCE((object_velocity_texture_desc.BindFlags & D3D11_BIND_RENDER_TARGET) == D3D11_BIND_RENDER_TARGET);

#if DLSS_TARGET_TEXTURE_WAS_UAV
                              if (dlss_motion_vectors.get())
                              {
                                  D3D11_TEXTURE2D_DESC dlss_motion_vectors_desc;
                                  dlss_motion_vectors->GetDesc(&dlss_motion_vectors_desc);
                                  dlss_output_changed = dlss_motion_vectors_desc.Width != output_texture_desc.Width || dlss_motion_vectors_desc.Height != output_texture_desc.Height || dlss_motion_vectors_desc.Format != output_texture_desc.Format;
                              }
#endif
                              // We assume the conditions of this texture (and its render target view) changing are the same as "dlss_output_changed" in the "!DLSS_TARGET_TEXTURE_WAS_UAV" case
                              if (!dlss_motion_vectors.get() || dlss_output_changed)
                              {
                                  dlss_motion_vectors = nullptr; // Make sure we discard the previous one
                                  hr = native_device->CreateTexture2D(&object_velocity_texture_desc, nullptr, &dlss_motion_vectors);
                                  ASSERT_ONCE(SUCCEEDED(hr));

                                  D3D11_RENDER_TARGET_VIEW_DESC object_velocity_render_target_view_desc;
                                  render_target_view->GetDesc(&object_velocity_render_target_view_desc);
                                  object_velocity_render_target_view_desc.Format = object_velocity_texture_desc.Format;
                                  object_velocity_render_target_view_desc.ViewDimension = D3D11_RTV_DIMENSION::D3D11_RTV_DIMENSION_TEXTURE2D;
                                  object_velocity_render_target_view_desc.Texture2D.MipSlice = 0;

                                  dlss_motion_vectors_rtv = nullptr; // Make sure we discard the previous one
                                  native_device->CreateRenderTargetView(dlss_motion_vectors.get(), &object_velocity_render_target_view_desc, &dlss_motion_vectors_rtv);
                              }

#if 1
                              SetPreyLumaConstantBuffers(cmd_list, stages, settings_pipeline_layout, LumaConstantBufferType::LumaSettings);
                              SetPreyLumaConstantBuffers(cmd_list, stages, shared_data_pipeline_layout, LumaConstantBufferType::LumaData);

                              ID3D11RenderTargetView* const dlss_motion_vectors_rtv_const = dlss_motion_vectors_rtv.get();
                              native_device_context->OMSetRenderTargets(1, &dlss_motion_vectors_rtv_const, depth_stencil_view.get());

                              // This should be the same draw type that the shader would have used if we went through with it (SMAA 2TX/TAA).
                              native_device_context->Draw(3, 0);
#elif 0 // Alternative unfinished code to run a different computer shader
                              //SetPreyLumaConstantBuffers(cmd_list, reshade::api::shader_stage::pixel, settings_pipeline_layout, LumaConstantBufferType::LumaSettings); // Probably not needed
                              SetPreyLumaConstantBuffers(cmd_list, reshade::api::shader_stage::pixel, shared_data_pipeline_layout, LumaConstantBufferType::LumaData);
                              // This should be the same draw type that the shader would have used if we went through with it (SMAA 2TX/TAA).
                              native_device_context->Draw(3, 0);
#elif 0
                              //SetPreyLumaConstantBuffers(cmd_list, reshade::api::shader_stage::compute, settings_pipeline_layout, LumaConstantBufferType::LumaSettings); // Probably not needed
                              SetPreyLumaConstantBuffers(cmd_list, reshade::api::shader_stage::compute, shared_data_pipeline_layout, LumaConstantBufferType::LumaData);
                              native_device->CreateComputeShader();
                              native_device_context->CSSetSamplers();
                              native_device_context->CSSetShaderResources();
                              native_device_context->CSSetUnorderedAccessViews();
                              native_device_context->CSSetShader();
                              native_device_context->Dispatch();
#endif
                          }

                          // Reset the render target, just to make sure there's no conflicts with the same texture being used as RWTexture UAV or Shader Resources
                          ID3D11RenderTargetView* emptry_render_target_view[1] = { nullptr };
                          native_device_context->OMSetRenderTargets(0, emptry_render_target_view, depth_stencil_view.get());

                          // Reset DLSS history if we did not draw motion blur (and we previously did). Based on CryEngine source code, mb is skipped on the first frame after scene cuts, so we want to re-use that information.
                          // Reset DLSS history if for one frame we had stopped tonemapping. This might include some scene cuts, but also triggers when entering full screen UI menus or videos and then leaving them (it shouldn't be a problem).
                          // Reset DLSS history if the output resolution changed (just an extra safety mechanism, it might not actually be needed).
                          bool reset_dlss = dlss_output_changed || !has_drawn_main_post_processing_previous || (has_drawn_motion_blur_previous && !has_drawn_motion_blur);

                          uint32_t render_width_dlss = std::lrintf(render_resolution.x);
                          uint32_t render_height_dlss = std::lrintf(render_resolution.y);

                          const float dlss_pre_exposure = dlss_custom_pre_exposure != 1.f ? dlss_custom_pre_exposure : 0.f;

                          // There doesn't seem to be a need to restore the DX state to whatever we had before (e.g. render targets, cbuffers, samplers, UAVs, texture shader resources, viewport, scissor rect, ...), CryEngine always sets everything it needs again for every pass.
                          // It's unclear whether DLSS expects the target/output texture to also hold the history from the previous frames, but if "DLSS_TARGET_TEXTURE_WAS_UAV" is false, it does for us, while if "DLSS_TARGET_TEXTURE_WAS_UAV" was true,
                          // ps_shader_resources[1] would be the previous frame TAA output.
                          if (NGX::DLSS::Draw(native_device_context, dlss_output_color.get(), source_color.get(), dlss_motion_vectors.get(), depth_buffer.get(), dlss_exposure.get(), dlss_pre_exposure, projection_jitters.x, projection_jitters.y, reset_dlss, render_width_dlss, render_height_dlss)) {
#if !DLSS_TARGET_TEXTURE_WAS_UAV
                              // DX11 doesn't need barriers
                              native_device_context->CopyResource(output_color.get(), dlss_output_color.get());
#endif

#if DLSS_UPSCALE_LENS_EFFECTS && 0 // We already do this above now
                              // Immediately force the viewport to the output resolution in case CryEngine didn't set it again before the next call
                              // (this is because we've already upscaled earlier than the game expects, so from now on we work on the full resolution viewport, not the render res one).
                              for (uint32_t i = 0; i < dlss_viewports_num; i++)
                              {
                                  dlss_viewports[i].Width = output_resolution.x;
                                  dlss_viewports[i].Height = output_resolution.y;
                              }
                              native_device_context->RSSetViewports(dlss_viewports_num, &dlss_viewports[0]);
#endif // DLSS_UPSCALE_LENS_EFFECTS

                              has_drawn_dlss_sr = true;
                              return true; // "Cancel" the previously set draw call, DLSS will take care of it
                          }
                          // Restore the render target, hoping that if DLSS failed, it didn't already start changing cbuffers or texture resources bindings or viewport etc.
                          else
                          {
                              ASSERT_ONCE(false); // DLSS FAILED
                              ID3D11RenderTargetView* const render_target_view_const = render_target_view.get();
                              native_device_context->OMSetRenderTargets(1, &render_target_view_const, depth_stencil_view.get());
                          }
                      }
                  }
                  break;
              }
          }
      }
#endif // NGX
  }

  //TODOFT: avoid re-applying these if the data hasn't changed (within the same frame)?
  if (is_custom_pass)
  {
      SetPreyLumaConstantBuffers(cmd_list, stages, settings_pipeline_layout, LumaConstantBufferType::LumaSettings);
      //TODOFT: only ever send this after DLSS? We don't really need it before (we barely even need it after!)
      SetPreyLumaConstantBuffers(cmd_list, stages, shared_data_pipeline_layout, LumaConstantBufferType::LumaData);
  }

#if !DEVELOPMENT //TODOFT: re-enable once we are sure we replaced all the post tonemap shaders and we are done debugging the blend states (and remove "is_custom_pass" check from below)
  //TODOFT: disable UI draw cbuffer call from c++ if game is rendering to gamma
  if (!is_custom_pass) return false;
#else // We can't do any further checks in this case because some UI draws at the beginning of the frame (in world computers), and also sometimes the scene doesn't even draw!
  //if (!has_drawn_composed_gbuffers) return false;
#endif // !DEVELOPMENT

  LumaUIData ui_data;

  ui_data.background_tonemapping_amount = (tonemap_ui_background && has_drawn_main_post_processing) ? tonemap_ui_background_amount : 0.0;

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

bool OnDraw(
    reshade::api::command_list* cmd_list,
    uint32_t vertex_count,
    uint32_t instance_count,
    uint32_t first_vertex,
    uint32_t first_instance) {
  return HandlePreDraw(cmd_list);
}

bool OnDrawIndexed(
    reshade::api::command_list* cmd_list,
    uint32_t index_count,
    uint32_t instance_count,
    uint32_t first_index,
    int32_t vertex_offset,
    uint32_t first_instance) {
    return HandlePreDraw(cmd_list);
}

bool OnDispatch(reshade::api::command_list* cmd_list, uint32_t group_count_x, uint32_t group_count_y, uint32_t group_count_z) {
  return HandlePreDraw(cmd_list, true);
}

bool OnDrawOrDispatchIndirect(
    reshade::api::command_list* cmd_list,
    reshade::api::indirect_command type,
    reshade::api::resource buffer,
    uint64_t offset,
    uint32_t draw_count,
    uint32_t stride) {
  ASSERT_ONCE(false); // Not used by Prey
  // NOTE: according to ShortFuse, this can be "reshade::api::indirect_command::unknown" too, so we'd need to fall back on checking what shader is bound to know if this is a compute shader draw
  bool is_dispatch = type == reshade::api::indirect_command::dispatch || type == reshade::api::indirect_command::dispatch_mesh || type == reshade::api::indirect_command::dispatch_rays;
  return HandlePreDraw(cmd_list, is_dispatch);
}

//TODOFT: use the native ReShade sampler desc instead? Probably not
com_ptr<ID3D11SamplerState> CreateCustomSampler(reshade::api::device* device, D3D11_SAMPLER_DESC desc)
{
    if (samplers_upgrade_mode <= 0)
        return nullptr;

    ID3D11Device* native_device = (ID3D11Device*)(device->get_native());

#if !DEVELOPMENT
    if (desc.Filter == D3D11_FILTER_ANISOTROPIC || desc.Filter == D3D11_FILTER_COMPARISON_ANISOTROPIC)
    {
        desc.MaxAnisotropy = D3D11_REQ_MAXANISOTROPY;
        desc.MipLODBias = std::clamp(desc.MipLODBias + texture_mip_lod_bias_offset, D3D11_MIP_LOD_BIAS_MIN, D3D11_MIP_LOD_BIAS_MAX); // Setting this out of range (~ +/- 16) will make DX11 crash
    }
    else
    {
        return nullptr;
    }
#else
    // This could theoretically make some textures that have moire patters, or were purposely blurry, "worse", but the positives of upgrading still outweight the negatives.
    // Note that this might not fix all cases because there's still "ID3D11DeviceContext::SetResourceMinLOD()" and textures that are blurry for other reasons
    // because they use other types of samplers (unfortunately it seems like some decals use "D3D11_FILTER_MIN_MAG_MIP_LINEAR").
    // Note that the AF on different textures in the game seems is possibly linked with other graphics settings than just AF (maybe textures or objects quality).
    if (desc.Filter == D3D11_FILTER_ANISOTROPIC || desc.Filter == D3D11_FILTER_COMPARISON_ANISOTROPIC)
    {
        // Note: this doesn't seem to affect much
        if (samplers_upgrade_mode == 1)
        {
            desc.MaxAnisotropy = min(desc.MaxAnisotropy * 2, D3D11_REQ_MAXANISOTROPY);
        }
        else if (samplers_upgrade_mode == 2)
        {
            desc.MaxAnisotropy = min(desc.MaxAnisotropy * 4, D3D11_REQ_MAXANISOTROPY);
        }
        else if (samplers_upgrade_mode >= 3)
        {
            desc.MaxAnisotropy = D3D11_REQ_MAXANISOTROPY;
        }
        // Note: this is the main ingredient in making textures less blurry
        if (samplers_upgrade_mode == 4 && desc.MipLODBias <= 0.f)
        {
            desc.MipLODBias = std::clamp(desc.MipLODBias + texture_mip_lod_bias_offset, D3D11_MIP_LOD_BIAS_MIN, D3D11_MIP_LOD_BIAS_MAX);
        }
        else if (samplers_upgrade_mode >= 5)
        {
            desc.MipLODBias = std::clamp(texture_mip_lod_bias_offset, D3D11_MIP_LOD_BIAS_MIN, D3D11_MIP_LOD_BIAS_MAX);
        }
        // Note: this never seems to affect anything in Prey
        if (samplers_upgrade_mode >= 6)
        {
            desc.MinLOD = min(desc.MinLOD, 0.f);
        }
    }
    else if ((desc.Filter == D3D11_FILTER_MIN_MAG_MIP_LINEAR && samplers_upgrade_mode_2 >= 1) // This is the most common (main/only) format being used other than AF
        || (desc.Filter == D3D11_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR && samplers_upgrade_mode_2 >= 2)
        || (desc.Filter == D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT && samplers_upgrade_mode_2 >= 3)
        || (desc.Filter == D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT && samplers_upgrade_mode_2 >= 4)
        || (desc.Filter == D3D11_FILTER_MIN_MAG_MIP_POINT && samplers_upgrade_mode_2 >= 5)
        || (desc.Filter == D3D11_FILTER_COMPARISON_MIN_MAG_MIP_POINT && samplers_upgrade_mode_2 >= 6))
    {
#if 0 //TODOFT4: research. Forced this on to see how it behaves. Doesn't work, it doesn't really help any further with (e.g.) blurry decal textures
        desc.Filter == (desc.ComparisonFunc != D3D11_COMPARISON_NEVER) ? D3D11_FILTER_COMPARISON_ANISOTROPIC : D3D11_FILTER_ANISOTROPIC;
        desc.MaxAnisotropy = D3D11_REQ_MAXANISOTROPY;
#else
        // Note: this doesn't seem to do anything really, it doesn't help with the occasional blurry texture (probably because all samplers that needed anisotropic already had it set)
        if (samplers_upgrade_mode >= 7)
        {
            desc.Filter == (desc.ComparisonFunc != D3D11_COMPARISON_NEVER && samplers_upgrade_mode == 7) ? D3D11_FILTER_COMPARISON_ANISOTROPIC : D3D11_FILTER_ANISOTROPIC;
            desc.MaxAnisotropy = D3D11_REQ_MAXANISOTROPY;
        }
#endif
        // Note: changing the lod bias of non anisotropic filters makes reflections (cubemap samples?) a lot more specular (shiny) in Prey, so it's best avoided (it can look better is some screenshots, but it's likely not intended).
        // Even if we only fix up textures that didn't have a positive bias, we run into the same problem.
        if (samplers_upgrade_mode == 4 && desc.MipLODBias <= 0.f)
        {
            desc.MipLODBias = std::clamp(desc.MipLODBias + texture_mip_lod_bias_offset, D3D11_MIP_LOD_BIAS_MIN, D3D11_MIP_LOD_BIAS_MAX);
        }
        else if (samplers_upgrade_mode >= 5)
        {
            desc.MipLODBias = std::clamp(texture_mip_lod_bias_offset, D3D11_MIP_LOD_BIAS_MIN, D3D11_MIP_LOD_BIAS_MAX);
        }
        if (samplers_upgrade_mode >= 6)
        {
            desc.MinLOD = min(desc.MinLOD, 0.f);
        }
    }
#endif // !DEVELOPMENT

    com_ptr<ID3D11SamplerState> sampler;
    native_device->CreateSamplerState(&desc, &sampler);
    ASSERT_ONCE(sampler != nullptr);
    return sampler;
}

#if !REPLACE_SAMPLERS_LIVE //TODOFT4: enable "REPLACE_SAMPLERS_LIVE" and clean old branch up (or integrate with CreateCustomSampler())?
bool OnCreateSampler(reshade::api::device* device, reshade::api::sampler_desc& desc)
{
    // CryEngine only used:
    // D3D11_FILTER_ANISOTROPIC
    // D3D11_FILTER_COMPARISON_ANISOTROPIC
    // D3D11_FILTER_MIN_MAG_MIP_POINT
    // D3D11_FILTER_COMPARISON_MIN_MAG_MIP_POINT
    // D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT
    // D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT
    // D3D11_FILTER_MIN_MAG_MIP_LINEAR
    // D3D11_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR
    bool modified = false;
        
#if DEVELOPMENT && 0
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

    if (desc.filter == reshade::api::filter_mode::anisotropic || desc.filter == reshade::api::filter_mode::compare_anisotropic)
    {
        desc.max_anisotropy = D3D11_REQ_MAXANISOTROPY;
        desc.mip_lod_bias = std::clamp(desc.mip_lod_bias + texture_mip_lod_bias_offset, D3D11_MIP_LOD_BIAS_MIN, D3D11_MIP_LOD_BIAS_MAX);
        modified = true;
    }
    return modified;
}
#endif

#if REPLACE_SAMPLERS_LIVE
void OnInitSampler(reshade::api::device* device, const reshade::api::sampler_desc& desc, reshade::api::sampler sampler)
{
    if (sampler == 0)
        return;

    ID3D11SamplerState* native_sampler = reinterpret_cast<ID3D11SamplerState*>(sampler.handle);
    D3D11_SAMPLER_DESC native_desc;
    native_sampler->GetDesc(&native_desc);
    const std::lock_guard<std::recursive_mutex> lock_samplers(s_mutex_samplers);
    custom_sampler_by_original_sampler[sampler.handle][texture_mip_lod_bias_offset] = CreateCustomSampler(device, native_desc);
}

void OnDestroySampler(reshade::api::device* device, reshade::api::sampler sampler)
{
    //TODOFT: can this actually be called by a separate thread? probably
    // This only seems to happen when the game shuts down in Prey
    const std::lock_guard<std::recursive_mutex> lock_samplers(s_mutex_samplers);
    custom_sampler_by_original_sampler.erase(sampler.handle);
}
#endif // REPLACE_SAMPLERS_LIVE

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
#endif // DEVELOPMENT

//TODOFT: an alternative way of approaching this would be to cache all the address of buffers that are ever filled up through ::Map() calls,
//then story a copy of each of their instances, and when one of these buffers is set to a shader stage, re-set the same cbuffer with our
//modified and fixed up data. That is a bit slower but it would be more safe, as it would guarantee us 100% that the buffer we are changing is cbuffer 13.
// Call this after reading the global cbuffer (index 13) memory (from CPU or GPU memory).
// This will update the "cb_per_view_global" values if the ptr is found to be the right type of buffer (and return true in that case),
// correct some of its values, and cache information for other usage.
bool UpdateGlobalCBuffer(void* global_buffer_data_ptr)
{
    const CBPerViewGlobal& global_buffer_data = *((CBPerViewGlobal*)global_buffer_data_ptr);

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

    if (!is_valid_cbuffer)
    {
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

    if (is_custom_draw_version)
    {
        return false;
    }

    // Copy the temporary buffer ptr into our persistent data
    cb_per_view_global = global_buffer_data;

    // Re-use the current cbuffer as the previous one if we didn't draw the scene in the frame before
    const CBPerViewGlobal& cb_per_view_global_actual_previous = has_drawn_main_post_processing_previous ? cb_per_view_global_previous : cb_per_view_global;

    auto current_projection_matrix = cb_per_view_global.CV_PrevViewProjMatr;
    auto current_nearest_projection_matrix = cb_per_view_global.CV_PrevViewProjNearestMatr;

    // Fix up the "previous view projection matrices" as they had wrong data in Prey,
    // first of all, their name was "wrong", because it was meant to have the value of the previous projection matrix,
    // not the camera/view projection matrix, and second, it was actually always based on the current one,
    // so it would miss any changes in FOV and jitters (drastically lowering the quality of motion vectors).
    // After tonemapping, ignore fixing up these, because they'd be jitterless and we don't have a jitterless copy (they aren't used anyway!).
    // If in the previous frame we didn't render, we don't replace the matrix with the one from the last frame that was rendered,
    // because there's no guaranteed that it would match.
    // If AA is disabled, or if the current form of AA doesn't used jittered rendering, this doesn't really make a difference (but it's still better because it creates motion vectors based on the previous view matrix).
    if ((fix_prev_matrix_mode == 1 || fix_prev_matrix_mode == 2) && (prey_taa_jittered || dlss_sr) && !has_drawn_tonemapping && has_drawn_main_post_processing_previous)
    {
        //TODOFT4: investigate whether it's actually good that we are using the previous projection matrix FOV,
        //or should we use the current projection matrix with the previous frame's jitters?
        //Test this by seeing if zooming in and out of with the camera in game causes ghosting.
        //UPDATE: we've fixed it in shaders, like this "velocity /= LumaData.RenderResolutionScale"
        cb_per_view_global.CV_PrevViewProjMatr = previous_projection_matrix;
        cb_per_view_global.CV_PrevViewProjNearestMatr = previous_nearest_projection_matrix;
        if (fix_prev_matrix_mode == 2)
        {
            cb_per_view_global.CV_PrevViewProjMatr.m02 *= 0.5;
            cb_per_view_global.CV_PrevViewProjMatr.m12 *= 0.5;
            cb_per_view_global.CV_PrevViewProjNearestMatr.m02 *= 0.5;
            cb_per_view_global.CV_PrevViewProjNearestMatr.m12 *= 0.5;
        }
    }
#if DEVELOPMENT
    else if (fix_prev_matrix_mode == 3 && (prey_taa_jittered || dlss_sr) && !has_drawn_tonemapping && has_drawn_main_post_processing_previous)
    {
        // Use old jitters with new view matrix (this might match how they calculate MVs based on this matrix)
        cb_per_view_global.CV_PrevViewProjMatr.m02 = previous_projection_matrix.m02;
        cb_per_view_global.CV_PrevViewProjMatr.m12 = previous_projection_matrix.m12;
        cb_per_view_global.CV_PrevViewProjNearestMatr.m02 = previous_nearest_projection_matrix.m02;
        cb_per_view_global.CV_PrevViewProjNearestMatr.m12 = previous_nearest_projection_matrix.m12;
    }
    else if (fix_prev_matrix_mode == 4 && (prey_taa_jittered || dlss_sr) && !has_drawn_tonemapping && has_drawn_main_post_processing_previous)
    {
        // Remove jitters (test)
        cb_per_view_global.CV_PrevViewProjMatr.m02 = 0.f;
        cb_per_view_global.CV_PrevViewProjMatr.m12 = 0.f;
        cb_per_view_global.CV_PrevViewProjNearestMatr.m02 = 0.f;
        cb_per_view_global.CV_PrevViewProjNearestMatr.m12 = 0.f;
    }
    else if (fix_prev_matrix_mode == 5 && (prey_taa_jittered || dlss_sr) && !has_drawn_tonemapping && has_drawn_main_post_processing_previous)
    {
        // Flip jitters (test)
        cb_per_view_global.CV_PrevViewProjMatr.m02 = -cb_per_view_global.CV_PrevViewProjMatr.m02;
        cb_per_view_global.CV_PrevViewProjMatr.m12 = -cb_per_view_global.CV_PrevViewProjMatr.m12;
        cb_per_view_global.CV_PrevViewProjNearestMatr.m02 = -cb_per_view_global.CV_PrevViewProjNearestMatr.m02;
        cb_per_view_global.CV_PrevViewProjNearestMatr.m12 = -cb_per_view_global.CV_PrevViewProjNearestMatr.m12;
    }
    else if (fix_prev_matrix_mode == 6 && (prey_taa_jittered || dlss_sr) && !has_drawn_tonemapping && has_drawn_main_post_processing_previous)
    {
        // Remove half of the new jitters
        cb_per_view_global.CV_PrevViewProjMatr = previous_projection_matrix;
        cb_per_view_global.CV_PrevViewProjNearestMatr = previous_nearest_projection_matrix;
        cb_per_view_global.CV_PrevViewProjMatr.m02 -= current_projection_matrix.m02 * 0.5;
        cb_per_view_global.CV_PrevViewProjMatr.m12 -= current_projection_matrix.m12 * 0.5;
        cb_per_view_global.CV_PrevViewProjNearestMatr.m02 -= current_nearest_projection_matrix.m02 * 0.5;
        cb_per_view_global.CV_PrevViewProjNearestMatr.m12 -= current_nearest_projection_matrix.m12 * 0.5;
    }
    else if (fix_prev_matrix_mode == 7 && (prey_taa_jittered || dlss_sr) && !has_drawn_tonemapping && has_drawn_main_post_processing_previous)
    {
        // Remove new jitters based on (current???) render res (random, but might work...), because the problem with dynamic objects MVs only seem to be when we are using DRS
        cb_per_view_global.CV_PrevViewProjMatr = previous_projection_matrix;
        cb_per_view_global.CV_PrevViewProjNearestMatr = previous_nearest_projection_matrix;
        cb_per_view_global.CV_PrevViewProjMatr.m02 -= current_projection_matrix.m02 * (1.0 - cb_per_view_global.CV_HPosScale.x);
        cb_per_view_global.CV_PrevViewProjMatr.m12 -= current_projection_matrix.m12 * (1.0 - cb_per_view_global.CV_HPosScale.y);
        cb_per_view_global.CV_PrevViewProjNearestMatr.m02 -= current_nearest_projection_matrix.m02 * (1.0 - cb_per_view_global.CV_HPosScale.x);
        cb_per_view_global.CV_PrevViewProjNearestMatr.m12 -= current_nearest_projection_matrix.m12 * (1.0 - cb_per_view_global.CV_HPosScale.y);
    }
    else if (fix_prev_matrix_mode == 8 && (prey_taa_jittered || dlss_sr) && !has_drawn_tonemapping && has_drawn_main_post_processing_previous)
    {
        // ...
        cb_per_view_global.CV_PrevViewProjMatr = previous_projection_matrix;
        cb_per_view_global.CV_PrevViewProjNearestMatr = previous_nearest_projection_matrix;
        cb_per_view_global.CV_PrevViewProjMatr.m02 *= cb_per_view_global.CV_HPosScale.x;
        cb_per_view_global.CV_PrevViewProjMatr.m12 *= cb_per_view_global.CV_HPosScale.y;
        cb_per_view_global.CV_PrevViewProjNearestMatr.m02 *= cb_per_view_global.CV_HPosScale.x;
        cb_per_view_global.CV_PrevViewProjNearestMatr.m12 *= cb_per_view_global.CV_HPosScale.y;
    }
#endif // DEVELOPMENT

    // Fix up the rendering scale for all passes after DLSS SR, as we upscaled before the game expected,
    // there's only post processing passes after it anyway (and lens optics shaders don't really read cbuffer 13, but still, some of their passes use custom resolutions).
    if (has_drawn_dlss_sr && prey_drs_active && !has_drawn_upscaling)
    {
        cb_per_view_global.CV_ScreenSize.x = cb_output_resolution_x;
        cb_per_view_global.CV_ScreenSize.y = cb_output_resolution_y;

        cb_per_view_global.CV_HPosScale.x = 1.f;
        cb_per_view_global.CV_HPosScale.y = 1.f;
        // Upgrade the ones from the previous frame too, because at this rendering phase they'd also have been full resolution, and these aren't used anyway
        cb_per_view_global.CV_HPosScale.z = cb_per_view_global.CV_HPosScale.x;
        cb_per_view_global.CV_HPosScale.w = cb_per_view_global.CV_HPosScale.y;

        cb_per_view_global.CV_HPosClamp.x = 1.f - cb_per_view_global.CV_ScreenSize.z;
        cb_per_view_global.CV_HPosClamp.y = 1.f - cb_per_view_global.CV_ScreenSize.w;
        cb_per_view_global.CV_HPosClamp.z = cb_per_view_global.CV_HPosClamp.x;
        cb_per_view_global.CV_HPosClamp.w = cb_per_view_global.CV_HPosClamp.y;
    }

    // Update our cached data with information from the cbuffer
    {
        // After vanilla tonemapping (as soon as AA starts),
        // camera jitters are removed from the cbuffer projection matrices, and the render resolution is also set to 100% (after the upscaling pass),
        // so we want to ignore these cases. We stop at the gbuffer compositions draw, because that's the last know cbuffer 13 to have the perfect values we are looking for (that shader is always run, so it's reliable)!
        if (output_resolution_matches && (/*!found_per_view_globals ||*/ (!has_drawn_composed_gbuffers && !has_drawn_tonemapping && !has_drawn_main_post_processing)) /*&& !prey_drs_active*/)
        {
#if DEVELOPMENT
            static float2 previous_render_resolution;
            if (!found_per_view_globals)
            {
                previous_render_resolution.x = cb_per_view_global.CV_ScreenSize.x;
                previous_render_resolution.y = cb_per_view_global.CV_ScreenSize.y;
            }
#endif // DEVELOPMENT

            render_resolution.x = cb_per_view_global.CV_ScreenSize.x;
            render_resolution.y = cb_per_view_global.CV_ScreenSize.y;
            output_resolution.x = cb_output_resolution_x; // Round here already as it would always meant to be integer
            output_resolution.y = cb_output_resolution_y;

            auto previous_prey_drs_active = prey_drs_active;
            prey_drs_active = std::abs(render_resolution.x - output_resolution.x) >= 0.5f || std::abs(render_resolution.y - output_resolution.y) >= 0.5f;
            // Make sure this doesn't change within a frame (once we found DRS in a frame, we should never "lose" it again for that frame.
            // Ignore this when we have no shaders loaded as it would always break due to the "has_drawn_tonemapping" check failing.
            ASSERT_ONCE(cloned_pipeline_count == 0 || !found_per_view_globals || !previous_prey_drs_active || (previous_prey_drs_active == prey_drs_active));

            // Make sure that our rendering resolution doesn't chance randomly within the pipeline (it probably will!)
            assert(!found_per_view_globals || !prey_drs_detected || (AlmostEqual(render_resolution.x, previous_render_resolution.x, 0.25f) && AlmostEqual(render_resolution.y, previous_render_resolution.y, 0.25f)));

            // Once we detect the user enabled DRS, we can't ever know it's been disabled because the game only occasionally drops to lower rendering resolutions, so we couldn't know if it was ever disabled
            if (prey_drs_active)
            {
                prey_drs_detected = true;
                // Lower the DLSS quality mode (which might introduce a stutter, or a slight blurring of the image as it resets the history),
                // but this will make DLSS not use DLAA and instead fall back on a quality mode that allows for a dynamic range of resolutions.
                // This isn't the exact rend resolution DLSS will be forced to use, but the center of a range it's gonna expect.
                // See CryEngine "osm_fbMinScale" cvar, it should ideally be set to the same value.
                dlss_sr_render_resolution = 1.0 / 1.5f;
            }

            // NOTE: we could just save the first one we found, it should always be jittered and "correct".
            projection_matrix = current_projection_matrix;
            nearest_projection_matrix = current_nearest_projection_matrix;

            const auto projection_jitters_copy = projection_jitters;

            // These are called "m_vProjMatrixSubPixoffset" in CryEngine.
            // The matrix is transposed so we flip the matrix x and y indices.
            projection_jitters.x = current_projection_matrix(0, 2);
            projection_jitters.y = current_projection_matrix(1, 2);

            ASSERT_ONCE((projection_jitters_copy.x == 0 && projection_jitters_copy.y == 0) || (projection_jitters.x != 0 || projection_jitters.y != 0)); // Once we found jitters, we should never cache matrices that don't have jitters anymore

            bool prey_taa_enabled_copy = prey_taa_enabled;
            // This is a reliable check to tell whether TAA is enabled. Jitters are "never" zero if they are enabled:
            // they can be if we use the "srand" method, but it would happen one in a billion years;
            // they could also be zero with Halton if the frame index was reset to zero (it is every x frames), but that happens very rarely, and for one frame only.
            prey_taa_enabled = (std::abs(projection_jitters.x * render_resolution.x) >= 0.00075) || (std::abs(projection_jitters.y * render_resolution.y) >= 0.00075); //TODOFT: make calculations more accurate (the threshold)
            // Make sure that once we detect that TAA was active within a frame, then it should never be detected as off in the same frame (it would mean we are reading a bad cbuffer 13 that we should have discarded).
            // Ignore this when we have no shaders loaded as it would always break due to the "has_drawn_tonemapping" check failing.
            ASSERT_ONCE(cloned_pipeline_count == 0 || !found_per_view_globals || !prey_taa_enabled_copy || (prey_taa_enabled_copy == prey_taa_enabled));
            if (prey_taa_enabled_copy != prey_taa_enabled && has_drawn_main_post_processing_previous) // TAA changed
            {
                // Detect if TAA was ever detected as on/off/on or off/on/off over 3 frames, because if that was so, our jitter "length" detection method isn't solid enough and we should do more (or add more tolernace to it),
                // this might even happen every x hours once the randomization triggers specific enough values, though all TAA modes have a pretty short cycle with fixed jitters,
                // so it should either happen quickly or never.
                bool middle_value_different = (prey_taa_enabled == previous_prey_taa_enabled[0]) != (prey_taa_enabled == previous_prey_taa_enabled[1]);
                ASSERT_ONCE(!middle_value_different);
            }
            prey_taa_detected = prey_taa_enabled || previous_prey_taa_enabled[0]; // This one has a two frames tolerance. We let it persist even if the game stopped drawing the 3D scene.
            cb_luma_frame_settings.DLSS = (dlss_sr && prey_taa_detected) ? 1 : 0; // Have a two frames tolerance

#if REPLACE_SAMPLERS_LIVE //TODOFT4: re-create samplers (preferrably cache them in a map by rendering resolutions...), though to make sure, we'd need to make we only catch cbuffer13 calls when the rend res is right (the actual rendering one!)
            //TODOFT: note that by default the game has a lod bias of 0 on most samplers (it seems),
            //but when enabling TAA (the hidden setting), some samplers go to -1, but while using SMAA 2TX, even if it includes TAA, that's not set (at least not the first time it's used, maybe they persist after first ever using TAA),
            //so should we bias by -1 again by default when the game uses TAA? It remains to be seen how many samplers they change when enabling TAA, if it's most of them, then we should avoid re-biasing by -1
            //by checking whether any SMAA edge AA shaders are running (the ones before TAA).
            // We do this even when DLSS is not on, in case 
            if (!custom_texture_mip_lod_bias_offset)
            {
                if ((dlss_sr || prey_drs_active) && prey_taa_detected)
                {
                    texture_mip_lod_bias_offset = std::log2(render_resolution.y / output_resolution.y) - 1.f; // This results in -1 at output res
                }
                else
                {
                    texture_mip_lod_bias_offset = -1.f; // Reset to default value
                }
            }
            //TODOFT: re-create all samplers here once to avoid constant hitches when they are first used later on in the pipeline? It's probably fine
#endif

            if (!has_drawn_main_post_processing_previous)
            {
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
            const Matrix44_tpl<double> mScaleBias1 = Matrix44_tpl<double>(
                0.5, 0, 0, 0,
                0, -0.5, 0, 0,
                0, 0, 1, 0,
                0.5, 0.5, 0, 1);
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
            if (matrix_calculation_mode_2 == 1) // Flip jitters (somehow it works and fixes motion vectors generation, it's not 100% clear why)
            {
                projection_matrix_native.m20 = -projection_matrix_native.m20;
                projection_matrix_native.m21 = -projection_matrix_native.m21;
                previous_projection_matrix_native.m20 = -previous_projection_matrix_native.m20;
                previous_projection_matrix_native.m21 = -previous_projection_matrix_native.m21;
            }
            else if (matrix_calculation_mode_2 == 2)
            {
                projection_matrix_native.m20 = -projection_matrix_native.m20;
                previous_projection_matrix_native.m20 = -previous_projection_matrix_native.m20;
            }
            else if (matrix_calculation_mode_2 == 3)
            {
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

            found_per_view_globals = true; //TODOFT3: moved here, good yeah (seems so!)? (delete the copy below)
        }
    }

    //found_per_view_globals = true;

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
#if REPLACE_SAMPLERS_LIVE
    case reshade::api::descriptor_type::sampler: {
        reshade::api::descriptor_table_update custom_update = update;
        bool any_modified = false;
        const std::lock_guard<std::recursive_mutex> lock_samplers(s_mutex_samplers);
        for (uint32_t i = 0; i < update.count; i++) {
            const reshade::api::sampler& sampler = static_cast<const reshade::api::sampler*>(update.descriptors)[i];
            if (custom_sampler_by_original_sampler.contains(sampler.handle))
            {
                if (!custom_sampler_by_original_sampler[sampler.handle].contains(texture_mip_lod_bias_offset))
                {
                    ID3D11SamplerState* native_sampler = reinterpret_cast<ID3D11SamplerState*>(sampler.handle);
                    D3D11_SAMPLER_DESC native_desc;
                    native_sampler->GetDesc(&native_desc);
                    custom_sampler_by_original_sampler[sampler.handle][texture_mip_lod_bias_offset] = CreateCustomSampler(device, native_desc);
                }
                uint64_t custom_sampler_handle = (uint64_t)(custom_sampler_by_original_sampler[sampler.handle][texture_mip_lod_bias_offset].get());
                if (custom_sampler_handle != 0)
                {
                    reshade::api::sampler& custom_sampler = ((reshade::api::sampler*)(custom_update.descriptors))[i];
                    custom_sampler.handle = custom_sampler_handle;
                    any_modified |= true;
                }
            }
            else
            {
#if DEVELOPMENT
                //TODOFT4: why does this happen!? Is it one of our "cloned" samples recursively setting itself (indeed it seems to only trigger when we change the imGui settings for samplers)? (we seemengly confirmed it is, hopefully ReShade won't try to handle the lifetime of the native samplers we created locally!)
                bool recursive_or_null = sampler.handle == 0;
                for (const auto& a : custom_sampler_by_original_sampler) {
                    for (const auto& b : a.second) {
                        ID3D11SamplerState* native_sampler = reinterpret_cast<ID3D11SamplerState*>(sampler.handle);
                        recursive_or_null |= b.second.get() == native_sampler;
                    }
                }
                ASSERT_ONCE(recursive_or_null); // Shouldn't happen! (if we know the sampler set is "recursive", then we are good and don't need to replace this sampler again)
#if 0
                if (sampler.handle != 0)
                {
                    ID3D11SamplerState* native_sampler = reinterpret_cast<ID3D11SamplerState*>(sampler.handle);
                    D3D11_SAMPLER_DESC native_desc;
                    native_sampler->GetDesc(&native_desc);
                    custom_sampler_by_original_sampler[sampler.handle] = CreateCustomSampler(device, native_desc);
                }
#endif
#endif // DEVELOPMENT
            }
        }

        if (any_modified) {
#if 1
            cmd_list->push_descriptors(stages, layout, param_index, custom_update);
#else // Old "REPLACE_SAMPLERS_LIVE" (probably faster but a lot more annoying to write?
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
#endif
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
#if OLD_GLOBAL_BUFFER_INTERCEPT_METHOD //TODOFT: nuke?
            // There's between 1 and ~100 "copies" (instances) of this buffer in the game
            bool global_buffer_changed = cb_per_view_global_buffer != buffer;
            ASSERT_ONCE(!cb_per_view_global_buffer || !global_buffer_changed); // Test, just to know whether this is ever re-allocated (it is!)

            if (std::find(cb_per_view_global_buffers.begin(), cb_per_view_global_buffers.end(), buffer) == cb_per_view_global_buffers.end()) {
                cb_per_view_global_buffers.emplace_back(buffer); //TODOFT
            }

            if (!global_buffer_changed)
                continue;

            bool is_shadow_map_buffer = false; // NOTE: this is outdated by now, it's not that simple (there's multiple global cbuffer instances)
            D3D11_VIEWPORT viewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
            UINT viewports_num = 1;
            //native_device_context->RSGetViewports(&viewports_num, nullptr);
            native_device_context->RSGetViewports(&viewports_num, &viewports[0]);
            // Shadow projections use squared textures (and thus viewports).
            // The viewport always seems to be set before cbuffers are sent, so we can reliably check for this.
            is_shadow_map_buffer = viewports[0].Width == viewports[0].Height;
            //is_shadow_map_buffer = cb_per_view_global_buffer_shadow_map == nullptr; // cb_per_view_global.CV_ProjRatio.w

            // This is seemengly allocated once by the game (at startup) and never re-allocated again.
            // To "certify" the buffer being created before we are able to intercept its map/unmap calls,
            // we need to wait for it to be pushed to cbuffer 13 at least once.
            // That's not really a problem because it's already used in the menu (without really doing anything there).
            //cb_per_view_global_buffer.release();
            cb_per_view_global_buffer = nullptr;
            cb_per_view_global_buffer = buffer;

            D3D11_BUFFER_DESC buffer_desc;
            buffer->GetDesc(&buffer_desc);
            assert(buffer_range.size >= CBPerViewGlobal_buffer_size);
            assert(buffer_range.offset == 0); // Not sure what this would entail
            assert(buffer_desc.ByteWidth == CBPerViewGlobal_buffer_size);

            // For the very first frame, manually copy the cbuffer by creating a buffer and copying the resource (slow!).
            // In this rare case, we don't fix up the buffer's data because we'd need to re-upload it, which is unnecessary (this should only ever happen on menus, and potentially when restarting levels).
            {
                D3D11_BUFFER_DESC buffer_copy_desc = buffer_desc;
                buffer_copy_desc.Usage = D3D11_USAGE_STAGING; // Prey's buffers (at least n13) are "D3D11_USAGE_DEFAULT", so they are push only from the CPU
                buffer_copy_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
                buffer_copy_desc.BindFlags = 0;
                buffer_copy_desc.MiscFlags = 0;

                HRESULT hr;
                if (!cb_per_view_global_buffer_copy.get())
                {
                    hr = native_device->CreateBuffer(&buffer_copy_desc, NULL, &cb_per_view_global_buffer_copy);
                    ASSERT_ONCE(SUCCEEDED(hr));
                }

                native_device_context->CopyResource(cb_per_view_global_buffer_copy.get(), buffer);

                D3D11_MAPPED_SUBRESOURCE mapped;
                hr = native_device_context->Map(cb_per_view_global_buffer_copy.get(), 0, D3D11_MAP_READ, 0, &mapped);
                ASSERT_ONCE(SUCCEEDED(hr));

                if (SUCCEEDED(hr)) {
                    std::memcpy(&cb_per_view_global, mapped.pData, sizeof(CBPerViewGlobal));
                    native_device_context->Unmap(cb_per_view_global_buffer_copy.get(), 0);

                    UpdateGlobalCBuffer();
                }
            }
#endif

            break; // There can't be anything after this in DX11
            }
        }
    }
}

void OnMapBufferRegion(reshade::api::device* device, reshade::api::resource resource, uint64_t offset, uint64_t size, reshade::api::map_access access, void** data)
{
    ID3D11Device* native_device = (ID3D11Device*)(device->get_native());
    ID3D11Buffer* buffer = reinterpret_cast<ID3D11Buffer*>(resource.handle);
    // No need to convert to native DX11 flags
    if (access == reshade::api::map_access::write_only || access == reshade::api::map_access::write_discard)
    {
        D3D11_BUFFER_DESC buffer_desc;
        buffer->GetDesc(&buffer_desc);

        ASSERT_ONCE(buffer_desc.ByteWidth != sizeof(CBPerViewGlobal));
        // There seems to only ever be one buffer type of this size, but it's not guaranteed... //TODOFT: not true, there's more? Check the map data!
        if (buffer_desc.ByteWidth == CBPerViewGlobal_buffer_size)
        {
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

void OnUnmapBufferRegion(reshade::api::device* device, reshade::api::resource resource)
{
    ID3D11Device* native_device = (ID3D11Device*)(device->get_native());
    ID3D11Buffer* buffer = reinterpret_cast<ID3D11Buffer*>(resource.handle);
    bool is_global_cbuffer = cb_per_view_global_buffer != nullptr && cb_per_view_global_buffer == buffer;
    ASSERT_ONCE(!cb_per_view_global_buffer_map_data || is_global_cbuffer);
    if (is_global_cbuffer && cb_per_view_global_buffer_map_data != nullptr)
    {
        // The whole buffer size is theoretically "CBPerViewGlobal_buffer_size" but we actually don't have the data for the excessive (padding) bytes,
        // they are never read by shaders on the GPU anyway.
        char global_buffer_data[CBPerViewGlobal_buffer_size];
        std::memcpy(&global_buffer_data[0], cb_per_view_global_buffer_map_data, CBPerViewGlobal_buffer_size);
        if (UpdateGlobalCBuffer(&global_buffer_data[0]))
        {
            // Write back the cbuffer data after we have fixed it up (we always do!)
            std::memcpy(cb_per_view_global_buffer_map_data, &cb_per_view_global, sizeof(CBPerViewGlobal));
        }
        cb_per_view_global_buffer_map_data = nullptr;
        cb_per_view_global_buffer = nullptr; // No need to keep this cached
    }
}

bool OnCopyResource(reshade::api::command_list* cmd_list, reshade::api::resource source, reshade::api::resource dest)
{
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
                switch (format)
                {
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
                switch (format)
                {
                case DXGI_FORMAT_R16G16B16A16_TYPELESS:
                case DXGI_FORMAT_R16G16B16A16_FLOAT:
                    return true;
                }
                return false;
                };
            auto isFloat11 = [](DXGI_FORMAT format) {
                switch (format)
                {
                case DXGI_FORMAT_R11G11B10_FLOAT:
                    return true;
                }
                return false;
                };

            // If we detected incompatible formats that were likely caused by Luma upgrading texture formats (of render targets only...),
            // do the copy in shader
            if (((isUnorm8(target_desc.Format) || isFloat11(target_desc.Format)) && isFloat16(source_desc.Format))
                || ((isUnorm8(source_desc.Format) || isFloat11(source_desc.Format)) && isFloat16(target_desc.Format))) {
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
                if ((target_desc.BindFlags & D3D11_BIND_RENDER_TARGET) == 0) {
                    // Create the persisting texture copy if necessary (if anything changed from the last copy).
                    // Theoretically all these textures have the same resolution as the screen so having one persisten texture is ok.
                    //TODOFT: verify the above assumption
                    if (!copy_texture.get()) {
                        D3D11_TEXTURE2D_DESC proxy_target_desc;
                        copy_texture->GetDesc(&proxy_target_desc);
                        if (proxy_target_desc.Width != target_desc.Width || proxy_target_desc.Height != target_desc.Height || proxy_target_desc.Format != target_desc.Format) {
                            proxy_target_desc = target_desc;
                            proxy_target_desc.BindFlags |= D3D11_BIND_RENDER_TARGET;
                            copy_texture = nullptr;
                            hr = native_device->CreateTexture2D(&proxy_target_desc, nullptr, &copy_texture);
                        }
                    }
                    proxy_target_resource_texture = copy_texture;
                    assert(SUCCEEDED(hr));
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

                static const uint32_t copy_vertex_shader_hash = std::stoul("FFFFFFF0", nullptr, 16);
                static const uint32_t copy_pixel_shader_hash = std::stoul("FFFFFFF1", nullptr, 16);
                if (copy_vertex_shader == nullptr && copy_pixel_shader == nullptr) {
                    const std::lock_guard<std::recursive_mutex> lock(s_mutex_loading);
                    if (custom_shaders_cache.contains(copy_vertex_shader_hash) && custom_shaders_cache.contains(copy_pixel_shader_hash)) {
                        //TODOFT: these need to be loaded at all times, even when the user unloads the data?
                        //Also they should be re-compiled when we modify them in the IDE? Also should we create these separately (e.g. put them in a "Luma Custom Shaders" folder and compile them in a separate array?)?
                        CachedCustomShader* vertex_shader = custom_shaders_cache[copy_vertex_shader_hash];
                        CachedCustomShader* pixel_shader = custom_shaders_cache[copy_pixel_shader_hash];
                        hr = native_device->CreateVertexShader(vertex_shader->code.data(), vertex_shader->code.size(), nullptr, &copy_vertex_shader);
                        assert(SUCCEEDED(hr));
                        hr = native_device->CreatePixelShader(pixel_shader->code.data(), pixel_shader->code.size(), nullptr, &copy_pixel_shader);
                        assert(SUCCEEDED(hr));
                    }
                    else {
                        assert(false); // The copy shader failed to be found (maybe it has been unloaded?)
                    }
                }

                //
                // Cache aside the previous resources/states:
                //
                com_ptr<ID3D11VertexShader> prev_vs;
                com_ptr<ID3D11PixelShader> prev_ps;
                native_device_context->VSGetShader(&prev_vs, nullptr, 0);
                native_device_context->PSGetShader(&prev_ps, nullptr, 0);
                ID3D11RenderTargetView* prev_render_target_views[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT];
                ID3D11DepthStencilView* prev_depth_stencil_views[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT];
                native_device_context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, &prev_render_target_views[0], &prev_depth_stencil_views[0]);
                D3D11_PRIMITIVE_TOPOLOGY prev_primitive_topology;
                native_device_context->IAGetPrimitiveTopology(&prev_primitive_topology);
                com_ptr<ID3D11ShaderResourceView> prev_shader_resource_view;
                native_device_context->PSGetShaderResources(0, 1, &prev_shader_resource_view);
                D3D11_RECT prev_scissor_rects[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
                UINT prev_scissor_rects_num = 1;
                native_device_context->RSGetScissorRects(&prev_scissor_rects_num, nullptr); // This will get the number of scissor rects used
                native_device_context->RSGetScissorRects(&prev_scissor_rects_num, &prev_scissor_rects[0]);
                D3D11_VIEWPORT prev_viewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
                UINT prev_viewports_num = 1;
                native_device_context->RSGetViewports(&prev_viewports_num, nullptr); // This will get the number of viewports used
                native_device_context->RSGetViewports(&prev_viewports_num, &prev_viewports[0]);

                //
                // Set the new resources/states:
                //
                native_device_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY::D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP); //TODOFT: do we need to call IASetVertexBuffers()?
                D3D11_RECT scissor_rect; /*= prev_scissor_rects[0]*/
                scissor_rect.left = 0;
                scissor_rect.top = 0;
                scissor_rect.right = target_desc.Width;
                scissor_rect.bottom = target_desc.Height;
                native_device_context->RSSetScissorRects(1, &scissor_rect);
                D3D11_VIEWPORT viewport; /*= prev_viewports[0]*/
                viewport.TopLeftX = 0;
                viewport.TopLeftY = 0;
                viewport.Width = target_desc.Width;
                viewport.Height = target_desc.Height;
                viewport.MinDepth = 0;
                viewport.MaxDepth = 1;
                native_device_context->RSSetViewports(1, &viewport);
                ID3D11ShaderResourceView* const shader_resource_view_const = source_resource_texture_view.get();
                native_device_context->PSSetShaderResources(0, 1, &shader_resource_view_const);
                ID3D11RenderTargetView* const render_target_view_const = target_resource_texture_view.get();
                native_device_context->OMSetRenderTargets(1, &render_target_view_const, nullptr);
                native_device_context->VSSetShader(copy_vertex_shader.get(), nullptr, 0);
                native_device_context->PSSetShader(copy_pixel_shader.get(), nullptr, 0);

                //
                // Finally draw:
                //
                native_device_context->Draw(4, 0);

                //
                // Copy our render target target resource into the non render target target resource if necessary:
                //
                if ((target_desc.BindFlags & D3D11_BIND_RENDER_TARGET) == 0) {
                    native_device_context->CopyResource(target_resource_texture.get(), proxy_target_resource_texture.get());
                }

                //
                // Restore the previous resources/states:
                //
                native_device_context->IASetPrimitiveTopology(prev_primitive_topology);
                native_device_context->RSSetScissorRects(prev_scissor_rects_num, &prev_scissor_rects[0]);
                native_device_context->RSSetViewports(prev_viewports_num, &prev_viewports[0]);
                ID3D11ShaderResourceView* const prev_shader_resource_view_const = prev_shader_resource_view.get();
                native_device_context->PSSetShaderResources(0, 1, &prev_shader_resource_view_const);
                native_device_context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, &prev_render_target_views[0], prev_depth_stencil_views[0]);
                for (UINT i = 0; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; i++) {
                    if (prev_render_target_views[i] != nullptr) {
                        prev_render_target_views[i]->Release();
                        prev_render_target_views[i] = nullptr;
                    }
                    if (prev_depth_stencil_views[i] != nullptr) {
                        prev_depth_stencil_views[i]->Release();
                        prev_depth_stencil_views[i] = nullptr;
                    }
                }
                native_device_context->VSSetShader(prev_vs.get(), nullptr, 0);
                native_device_context->PSSetShader(prev_ps.get(), nullptr, 0);
                return true;
            }
        }
    }

    return false;
}

bool OnCopyTextureRegion(reshade::api::command_list* cmd_list, reshade::api::resource source, uint32_t source_subresource, const reshade::api::subresource_box* source_box, reshade::api::resource dest, uint32_t dest_subresource, const reshade::api::subresource_box* dest_box, reshade::api::filter_mode filter /*Unused in DX11*/)
{
    if (source_subresource == 0 && dest_subresource == 0 && (!source_box || (source_box->left == 0 && source_box->top == 0)) && (!dest_box || (dest_box->left == 0 && dest_box->top == 0)) && (!dest_box || !source_box || (source_box->width() == dest_box->width() && source_box->height() == dest_box->height()))) {
        return OnCopyResource(cmd_list, source, dest);
    }
    return false;
}

void OnBindViewports(reshade::api::command_list* cmd_list, uint32_t first, uint32_t count, const reshade::api::viewport* viewports)
{
    if (count <= 0) return;

#if DLSS_UPSCALE_LENS_EFFECTS // We moved the code below directly in the "before draw call" function
    return;
#endif

    // Only do this between DLSS SR (early upscaling) and the native upscaling, which runs later.
    // We need to force all passes after DLSS to run at full resolution in the viewport, otherwise we won't be able to run to the full area of their textures.
    if (!has_drawn_dlss_sr || has_drawn_upscaling || !prey_drs_active) return;

    assert(count <= D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE);

    const auto* device = cmd_list->get_device();
    ID3D11Device* native_device = (ID3D11Device*)(device->get_native());
    ID3D11DeviceContext* native_device_context = (ID3D11DeviceContext*)(cmd_list->get_native());
    const D3D11_VIEWPORT* native_viewports = reinterpret_cast<const D3D11_VIEWPORT*>(viewports);

    // Only the first viewport is used for post processing stuff in Prey, though viewports are set atomically in DX11,
    // thus we need to replace either all or none (and thus we are forced to copy them over).
    // DX11 just reads their value on the spot, it doesn't keep a reference to the pointer we pass in.

    D3D11_VIEWPORT native_viewports_copy[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]; // Equal to "D3D11_VIEWPORT_AND_SCISSORRECT_MAX_INDEX + 1"
    std::memcpy(&native_viewports_copy, native_viewports, count * sizeof(D3D11_VIEWPORT));

    bool matches_output_resolution = native_viewports_copy[0].Width == output_resolution.x && native_viewports_copy[0].Height == output_resolution.y;
    ASSERT_ONCE((native_viewports_copy[0].Width == render_resolution.x || native_viewports_copy[0].Height == render_resolution.y) || matches_output_resolution); // Theoretically they should always match the render res with the conditions above, even for lens optics
    if (!matches_output_resolution) return;

    native_viewports_copy[0].Width = output_resolution.x;
    native_viewports_copy[0].Height = output_resolution.y;

    native_device_context->RSSetViewports(count, &native_viewports_copy[0]);
}

void OnReshadePresent(reshade::api::effect_runtime* runtime) {
#if DEVELOPMENT
  if (trace_running) {
    reshade::log::message(reshade::log::level::info, "present()");
    reshade::log::message(reshade::log::level::info, "--- End Frame ---");
    trace_count = trace_pipeline_handles.size();
    trace_running = false;
  } else if (trace_scheduled) {
    trace_scheduled = false;
    trace_shader_hashes.clear();
    trace_pipeline_handles.clear();
    trace_running = true;
    reshade::log::message(reshade::log::level::info, "--- Frame ---");
  }
#endif // DEVELOPMENT

  // TODO: verify this delayed behaviour is actually ever needed and delete it if not
  {
    const std::lock_guard<std::recursive_mutex> lock(s_mutex_generic);
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
    const std::lock_guard<std::recursive_mutex> lock(s_mutex_generic);
    for (auto pair : pipelines_to_destroy) {
      pair.second->destroy_pipeline(reshade::api::pipeline{pair.first});
    }
    pipelines_to_destroy.clear();
  }

  if (needs_unload_shaders) {
    UnloadCustomShaders();
#if 1 // Optionally unload all custom shaders data
    const std::lock_guard<std::recursive_mutex> lock(s_mutex_loading);
    custom_shaders_cache.clear();
#endif
    needs_unload_shaders = false;
  }
  if (needs_load_shaders) {
    // Cache the defines at compilation time
    {
        ShaderDefineData::OnCompilation(shader_defines_data);
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

void DumpShader(uint32_t shader_hash, bool auto_detect_type = true) {
  auto dump_path = GetShaderPath();

  if (!std::filesystem::exists(dump_path)) {
    std::filesystem::create_directory(dump_path);
  }
  dump_path /= ".\\dump";
  if (!std::filesystem::exists(dump_path)) {
    std::filesystem::create_directory(dump_path);
  }

  wchar_t hash_string[11];
  swprintf_s(hash_string, L"0x%08X", shader_hash);

  dump_path /= hash_string;

  auto* cached_shader = shader_cache.find(shader_hash)->second;

  // Automatically find the shader type and append it to the name (a bit hacky). This can make dumping relevantly slower.
  if (auto_detect_type) {
    if (cached_shader->disasm.empty()) {
      auto disasm_code = renodx::utils::shader::compiler::DisassembleShader(cached_shader->data, cached_shader->size);
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
      static const std::string template_shader_full_name = "x_x";

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
      const auto type_index = cached_shader->disasm.find(template_shader_name);
      if (type_index != std::string::npos) {
        const std::string type = cached_shader->disasm.substr(type_index, template_shader_name.length() + template_shader_full_name.length());
        dump_path += ".";
        dump_path += type;
      }
    }
  }

  dump_path += L".cso";

  std::ofstream file(dump_path, std::ios::binary);

  file.write(static_cast<const char*>(cached_shader->data), cached_shader->size);

  if (!dumped_shaders.contains(shader_hash)) {
    dumped_shaders.emplace(shader_hash);
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
    if (!dumped_shaders.contains(shader_to_dump)) {
      DumpShader(shader_to_dump, true);
    }
  }
  thread_auto_dumping_running = false;
}

void AutoLoadShaders() {
  // Copy the "pipelines_to_reload_copy" so we don't have to lock "s_mutex_loading" all the times
  std::unordered_set<uint64_t> pipelines_to_reload_copy;
  {
    const std::lock_guard<std::recursive_mutex> lock_loading(s_mutex_loading);
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
    const std::lock_guard<std::recursive_mutex> lock(s_mutex_loading);
    pipelines_to_reload.clear();
  }
  ImGui::SameLine();
#endif // DEVELOPMENT || TEST

  bool needs_compilation = false;
  {
    needs_compilation = defines_need_recompilation;
    for (uint32_t i = 0; i < shader_defines_data.size() && !needs_compilation; i++) {
      needs_compilation |= shader_defines_data[i].NeedsCompilation();
    }
  }
#if !DEVELOPMENT && !TEST
  ImGui::BeginDisabled(!needs_compilation);
#endif
  if (ImGui::Button(cloned_pipeline_count ? (needs_compilation ? "Reload Shaders (*)" : "Reload Shaders") : "Load Shaders")) {
    needs_unload_shaders = false;
    last_pressed_unload = false;
    needs_load_shaders = true;
    const std::lock_guard<std::recursive_mutex> lock(s_mutex_loading);
    pipelines_to_reload.clear();
  }
#if !DEVELOPMENT
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
      ImGui::SetTooltip((cloned_pipeline_count && needs_compilation) ? "Shaders recompilation is needed for the changed settings to apply" : "Compiles shaders");
  }
#endif
#if !DEVELOPMENT && !TEST
  ImGui::EndDisabled();
#endif

#if DEVELOPMENT
  ImGui::SameLine();
  ImGui::PushID("##AutoLoadCheckBox");
  if (ImGui::Checkbox("Auto Load", &auto_load)) {
    if (!auto_load && thread_auto_loading.joinable()) {
      thread_auto_loading.join();
    }
    const std::lock_guard<std::recursive_mutex> lock(s_mutex_loading);
    pipelines_to_reload.clear();
  }
  ImGui::PopID();

  ImGui::SameLine();
  ImGui::PushID("##LiveReloadCheckBox");
  if (ImGui::Checkbox("Live Reload", &live_reload)) {
    needs_live_reload_update = true;
    const std::lock_guard<std::recursive_mutex> lock(s_mutex_loading);
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
            const std::lock_guard<std::recursive_mutex> lock(s_mutex_generic);
            for (auto index = 0; index < trace_count; index++) {
              auto pipeline_handle = trace_pipeline_handles.at(index);
              const bool is_selected = (selected_index == index);
              const auto pipeline_pair = pipeline_cache_by_pipeline_handle.find(pipeline_handle);
              const bool is_valid = pipeline_pair != pipeline_cache_by_pipeline_handle.end() && pipeline_pair->second != nullptr;
              std::stringstream name;
              auto text_color = IM_COL32(255, 255, 255, 255);

              if (is_valid) {
                const auto pipeline = pipeline_pair->second;

                name << std::setfill('0') << std::setw(3) << index << std::setw(0);
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

                const std::lock_guard<std::recursive_mutex> lock_loading(s_mutex_loading);
                const auto custom_shader = !pipeline->shader_hashes.empty() ? custom_shaders_cache[pipeline->shader_hashes[0]] : nullptr;

                // Find if the shader has been modified
                if (pipeline->cloned) {
                  // For now just force picking the first shader linked to the pipeline, there should always only be one (?)
                  if (custom_shader != nullptr && custom_shader->is_hlsl && !custom_shader->file_path.empty()) {
                    name << "* - ";

                    // TODO: add support for more name variations
                    static const std::string full_template_name = "0x12345678.xx_x_x.hlsl";
                    static const auto characters_to_remove_from_end = full_template_name.length();
                    auto filename_string = custom_shader->file_path.filename().string();
                    filename_string.erase(filename_string.length() - min(characters_to_remove_from_end, filename_string.length()));
                    if (filename_string.ends_with("_"))
                    {
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
                if (custom_shader != nullptr && !custom_shader->compilation_error.empty()) {
                  text_color = IM_COL32(255, 0, 0, 255);
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
            selected_index = max(selected_index, trace_count - 1);
          }
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
              const std::lock_guard<std::recursive_mutex> lock(s_mutex_generic);
              if (auto pipeline_pair = pipeline_cache_by_pipeline_handle.find(pipeline_handle); pipeline_pair != pipeline_cache_by_pipeline_handle.end() && pipeline_pair->second != nullptr) {
                const std::lock_guard<std::recursive_mutex> lock_dumping(s_mutex_dumping);
                auto* cache = (!pipeline_pair->second->shader_hashes.empty() && shader_cache.contains(pipeline_pair->second->shader_hashes[0])) ? shader_cache[pipeline_pair->second->shader_hashes[0]] : nullptr;
                if (cache && cache->disasm.empty()) {
                  auto disasm_code = renodx::utils::shader::compiler::DisassembleShader(cache->data, cache->size);
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
            if (selected_index >= 0 && trace_pipeline_handles.size() >= selected_index + 1 && (changed_selected || opened_live_tab_item != open_live_tab_item || refresh_cloned_pipelines)) {
              bool hlsl_set = false;
              auto pipeline_handle = trace_pipeline_handles.at(selected_index);

              const std::lock_guard<std::recursive_mutex> lock(s_mutex_generic);
              if (
                  auto pipeline_pair = pipeline_cache_by_pipeline_handle.find(pipeline_handle);
                  pipeline_pair != pipeline_cache_by_pipeline_handle.end() && pipeline_pair->second != nullptr) {

                const auto pipeline = pipeline_pair->second;
                const std::lock_guard<std::recursive_mutex> lock_loading(s_mutex_loading);
                const auto custom_shader = !pipeline->shader_hashes.empty() ? custom_shaders_cache[pipeline->shader_hashes[0]] : nullptr;
                // If the custom shader has a compilation error, print that, otherwise read the file text
                if (custom_shader != nullptr && !custom_shader->compilation_error.empty()) {
                  hlsl_string = custom_shader->compilation_error;
                  hlsl_error = true;
                  hlsl_set = true;
                } else if (custom_shader != nullptr && custom_shader->is_hlsl && !custom_shader->file_path.empty()) {
                  auto result = ReadTextFile(custom_shader->file_path);
                  if (result.has_value()) {
                    hlsl_string.assign(result.value());
                    hlsl_error = false;
                    hlsl_set = true;
                  } else {
                    hlsl_string.assign("FAILED TO READ FILE");
                    hlsl_error = true;
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
              ImGui::PushStyleColor(ImGuiCol_Text, hlsl_error ? IM_COL32(255, 0, 0, 255) : IM_COL32(255, 255, 255, 255));
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
              const std::lock_guard<std::recursive_mutex> lock(s_mutex_generic);
              if (auto pipeline_pair = pipeline_cache_by_pipeline_handle.find(pipeline_handle); pipeline_pair != pipeline_cache_by_pipeline_handle.end() && pipeline_pair->second != nullptr) {
                bool test_pipeline = pipeline_pair->second->test;
                if (ImGui::BeginChild("Settings")) {
                  if (!pipeline_pair->second->HasVertexShader())
                  {
                    ImGui::Checkbox("Test Shader (skips drawing, or draws black)", &test_pipeline);
                    if (pipeline_pair->second->cloned && ImGui::Button("Unload"))
                    {
                        UnloadCustomShaders({ pipeline_handle }, false, false);
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
        ImGui::BeginDisabled(!dlss_sr_supported);
        if (ImGui::Checkbox("DLSS Super Resolution", &dlss_sr)) {
            reshade::set_config_value(runtime, NAME, "DLSSSuperResolution", dlss_sr);
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("This replaces the game's native AA and dynamic resolution scaling implementations.\nSelect \"SMAA 2TX\" or \"TAA\" in the game's AA settings for DLSS/DLAA to engage (a tick will appear here when it's engaged).\n\nRequires compatible Nvidia GPUs.");
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
                ImGui::SmallButton(ICON_FK_OK); // Show that DLSS is engaged
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

        if (ImGui::Checkbox("Tonemap UI Background", &tonemap_ui_background)) {
            reshade::set_config_value(runtime, NAME, "TonemapUIBackground", tonemap_ui_background);
        }
        ImGui::SameLine();
        if (tonemap_ui_background != true) {
            ImGui::PushID("Tonemap UI Background");
            if (ImGui::SmallButton(ICON_FK_UNDO)) {
                tonemap_ui_background = true;
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

        int value = cb_luma_frame_settings.ForceSDR;
        if (ImGui::SliderInt("Force SDR", &value, 0, 1)) {
            reshade::set_config_value(runtime, NAME, "ForceSDR", value);
        }
        ImGui::SameLine();
        if (value != false) {
            ImGui::PushID("Force SDR");
            if (ImGui::SmallButton(ICON_FK_UNDO)) {
                value = false;
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
        cb_luma_frame_settings.ForceSDR = value;

        if (ImGui::SliderFloat("Scene Peak White", &cb_luma_frame_settings.ScenePeakWhite, 400.0, 10000)) {
            if (cb_luma_frame_settings.ScenePeakWhite == default_user_peak_white) {
                reshade::set_config_value(runtime, NAME, "ScenePeakWhite", 0.f);
            }
            else {
                reshade::set_config_value(runtime, NAME, "ScenePeakWhite", cb_luma_frame_settings.ScenePeakWhite);
            }
        }
        ImGui::SameLine();
        if (cb_luma_frame_settings.ScenePeakWhite != default_user_peak_white) {
            ImGui::PushID("Scene Peak White");
            if (ImGui::SmallButton(ICON_FK_UNDO)) {
                cb_luma_frame_settings.ScenePeakWhite = default_user_peak_white;
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
        if (ImGui::SliderFloat("Scene Paper White", &cb_luma_frame_settings.ScenePaperWhite, srgb_white_level, 500.f)) {
            reshade::set_config_value(runtime, NAME, "ScenePaperWhite", cb_luma_frame_settings.ScenePaperWhite);
        }
        ImGui::SameLine();
        if (cb_luma_frame_settings.ScenePaperWhite != default_paper_white) {
            ImGui::PushID("Scene Paper White");
            if (ImGui::SmallButton(ICON_FK_UNDO)) {
                cb_luma_frame_settings.ScenePaperWhite = default_paper_white;
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
        if (ImGui::SliderFloat("UI Paper White", &cb_luma_frame_settings.UIPaperWhite, srgb_white_level, 500.f)) {
            reshade::set_config_value(runtime, NAME, "UIPaperWhite", cb_luma_frame_settings.UIPaperWhite);
        }
        ImGui::SameLine();
        if (cb_luma_frame_settings.UIPaperWhite != default_paper_white) {
            ImGui::PushID("UI Paper White");
            if (ImGui::SmallButton(ICON_FK_UNDO)) {
                cb_luma_frame_settings.UIPaperWhite = default_paper_white;
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

#if DEVELOPMENT
        ImGui::NewLine();
        ImGui::SliderFloat("Developer Setting 1", &cb_luma_frame_settings.DevSetting1, 0.0, 1.0);
        ImGui::SliderFloat("Developer Setting 2", &cb_luma_frame_settings.DevSetting2, 0.0, 1.0);
        ImGui::SliderFloat("Developer Setting 3", &cb_luma_frame_settings.DevSetting3, 0.0, 1.0);
        ImGui::SliderFloat("Developer Setting 4", &cb_luma_frame_settings.DevSetting4, 0.0, 1.0);
        ImGui::SliderFloat("Developer Setting 5", &cb_luma_frame_settings.DevSetting5, 0.0, 1.0);
        ImGui::SliderFloat("Developer Setting 6", &cb_luma_frame_settings.DevSetting6, 0.0, 1.0);
        ImGui::SliderFloat("Developer Setting 7", &cb_luma_frame_settings.DevSetting7, 0.0, 1.0);

        ImGui::NewLine();
        ImGui::SliderFloat("DLSS Custom Exposure", &dlss_custom_exposure, 0.01, 10.0);
        ImGui::SliderFloat("DLSS Custom Pre-Exposure", &dlss_custom_pre_exposure, 0.01, 10.0);
        
        ImGui::NewLine();
        ImGui::SliderInt("DLSS Motion Vectors Jittered", &dlss_mode, 0, 1);
        ImGui::SliderInt("Fix Motion Vectors Generation Projection Matrix", &fix_prev_matrix_mode, 0, 8);
        //ImGui::SliderInt("matrix_calculation_mode", &matrix_calculation_mode, 0, 3); // Disabled
        ImGui::SliderInt("matrix_calculation_mode_2", &matrix_calculation_mode_2, 0, 4);

#if REPLACE_SAMPLERS_LIVE
        ImGui::NewLine();
        bool samplers_changed = ImGui::SliderInt("Texture Samplers Upgrade Mode", &samplers_upgrade_mode, 0, 7);
        samplers_changed |= ImGui::SliderInt("Texture Samplers Upgrade Mode - 2", &samplers_upgrade_mode_2, 0, 6);
        ImGui::Checkbox("Custom Texture Samplers Mip LOD Bias", &custom_texture_mip_lod_bias_offset);
        if (samplers_upgrade_mode > 0 && custom_texture_mip_lod_bias_offset)
        {
            samplers_changed |= ImGui::SliderFloat("Texture Samplers Mip LOD Bias", &texture_mip_lod_bias_offset, -8.f, +8.f);
        }
        if (samplers_changed)
        {
            //TODOFT3: move this somewhere "better"? It's probably fine here though :P
            const std::lock_guard<std::recursive_mutex> lock_samplers(s_mutex_samplers);
            for (auto& original_sampler_handle : custom_sampler_by_original_sampler)
            {
                ID3D11SamplerState* native_sampler = reinterpret_cast<ID3D11SamplerState*>(original_sampler_handle.first);
                D3D11_SAMPLER_DESC native_desc;
                native_sampler->GetDesc(&native_desc);
                original_sampler_handle.second[texture_mip_lod_bias_offset] = CreateCustomSampler(runtime->get_device(), native_desc);
            }
        }
#endif // REPLACE_SAMPLERS_LIVE
#endif // DEVELOPMENT

        ImGui::EndTabItem();
    }

    if (ImGui::BeginTabItem("Advanced Settings")) {
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
      {
          ImGui::SetTooltip("Shader Defines: reload shaders after changing these for the changes to apply (and save).\nSome settings are only editable in debug modes, and only apply if the \"DEVELOPMENT\" flag is turned on.\nDo not change unless you know what you are doing.");
      }

      // Show reset button
      {
          bool is_default = true;
          for (uint32_t i = 0; i < shader_defines_data.size() && is_default; i++) {
              is_default = shader_defines_data[i].IsDefault() && !shader_defines_data[i].IsCustom();
          }
          ImGui::PushID("Advanced Settings: Reset Defines");
          ImGui::BeginDisabled(is_default);
          if (ImGui::Button("Reset")) {
              // Remove all newly added settings
              ShaderDefineData::RemoveCustomData(shader_defines_data);

              // Reset the rest to default
              ShaderDefineData::Reset(shader_defines_data);
          }
          ImGui::EndDisabled();
          ImGui::PopID();
      }

#if DEVELOPMENT || TEST
      if (shader_defines_data.size() < MAX_SHADER_DEFINES) {
          ImGui::SameLine();
          ImGui::PushID("Advanced Settings: Add Define");
          if (ImGui::Button("Add")) {
              shader_defines_data.emplace_back();
          }
          ImGui::PopID();
      }
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
              if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
              {
                  ImGui::SetTooltip("Recompile shaders needed to apply the changed settings");
              }
              ImGui::EndDisabled();
              ImGui::PopID();
          }
      }
#endif

      for (uint32_t i = 0; i < shader_defines_data.size(); i++) {
#if !DEVELOPMENT && !TEST
        // Don't render empty text fields that couldn't be filled due to them not being editable
        if (!shader_defines_data[i].IsNameEditable() && !shader_defines_data[i].IsValueEditable() && shader_defines_data[i].IsCustom()) {
            continue;
        }
#endif

        ImGui::PushID(shader_defines_data[i].name_hint.data());
        ImGuiInputTextFlags flags = ImGuiInputTextFlags_CharsNoBlank;
        if (!shader_defines_data[i].IsNameEditable()) {
            flags |= ImGuiInputTextFlags_ReadOnly;
        }
        // ImGUI doesn't work with std::string data, it seems to need c style char arrays.
        ImGui::InputTextWithHint("", shader_defines_data[i].name_hint.data(), shader_defines_data[i].editable_data.GetName(), std::size(shader_defines_data[i].editable_data.name) /*SHADER_DEFINES_MAX_NAME_LENGTH*/, flags); //TODOFT5: spacing
        ImGui::PopID();

        ImGui::SameLine();
        ImGui::PushID(shader_defines_data[i].value_hint.data());
        flags = ImGuiInputTextFlags_CharsDecimal | ImGuiInputTextFlags_CharsNoBlank | ImGuiInputTextFlags_AlwaysOverwrite | ImGuiInputTextFlags_AutoSelectAll | ImGuiInputTextFlags_NoUndoRedo;
        if (!shader_defines_data[i].IsValueEditable()) {
            flags |= ImGuiInputTextFlags_ReadOnly;
        }
        ImGui::InputTextWithHint("", shader_defines_data[i].value_hint.data(), shader_defines_data[i].editable_data.GetValue(), std::size(shader_defines_data[i].editable_data.value) /*SHADER_DEFINES_MAX_VALUE_LENGTH*/, flags);
        // Avoid having empty values unless the default value also was empty
        if (shader_defines_data[i].editable_data.GetValue() == "") {
            shader_defines_data[i].editable_data.value[0] = shader_defines_data[i].default_data.value[0];
        }
        ImGui::PopID();
      }

      ImGui::EndTabItem();
    }

#if DEVELOPMENT || TEST
    if (ImGui::BeginTabItem("Info")) {
        ImGui::Text("Render Resolution: ", "");
        std::string text = std::to_string(render_resolution.x) + " " + std::to_string(render_resolution.y);
        ImGui::Text(text.c_str(), "");

        ImGui::NewLine();
        ImGui::Text("Output Resolution: ", "");
        text = std::to_string(output_resolution.x) + " " + std::to_string(output_resolution.y);
        ImGui::Text(text.c_str(), "");

        ImGui::NewLine();
        ImGui::Text("DLSS Resolution Scale: ", "");
        text = std::to_string(dlss_sr_render_resolution);
        ImGui::Text(text.c_str(), "");

        ImGui::NewLine();
        ImGui::Text("Camera Jitters: ", "");
        // In UV space
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

    ImGui::EndTabBar(); // TabBar
  }
}
} // namespace

void Init() {
#if DEVELOPMENT || _DEBUG
  LaunchDebugger();
#endif // DEVELOPMENT

  // Add all the shaders we have already dumped to the dumped list to avoid live re-dumping them
  dumped_shaders.clear();
  auto dump_path = GetShaderPath();
  if (std::filesystem::exists(dump_path)) {
    dump_path /= ".\\dump";
    if (std::filesystem::exists(dump_path)) {
      const std::lock_guard<std::recursive_mutex> lock_dumping(s_mutex_dumping);
      for (const auto& entry : std::filesystem::directory_iterator(dump_path)) {
        if (!entry.is_regular_file()) continue;
        const auto& entry_path = entry.path();
        if (entry_path.extension() != ".cso") continue;
        const auto& entry_path_string = entry_path.filename().string();
        if (entry_path_string.starts_with("0x") && entry_path_string.length() > 2 + HASH_CHARACTERS_LENGTH) {
          const std::string hash = entry_path_string.substr(2, HASH_CHARACTERS_LENGTH);
          try {
            dumped_shaders.emplace(std::stoul(hash, nullptr, 16));
          } catch (const std::exception& e) {
            continue;
          }
        }
      }
    }
  }

  // Define the pixel shader of some important passes we can use to determine where we are within the rendering pipeline:

  // TiledShading TiledDeferredShading (compute shaders)
  shader_hashes_TiledShadingTiledDeferredShading.emplace(std::stoul("1E676CD5", nullptr, 16));
  shader_hashes_TiledShadingTiledDeferredShading.emplace(std::stoul("80FF9313", nullptr, 16));
  shader_hashes_TiledShadingTiledDeferredShading.emplace(std::stoul("571D5EAE", nullptr, 16));
  shader_hashes_TiledShadingTiledDeferredShading.emplace(std::stoul("6710AFD5", nullptr, 16));
  shader_hashes_TiledShadingTiledDeferredShading.emplace(std::stoul("54147C78", nullptr, 16));
  shader_hashes_TiledShadingTiledDeferredShading.emplace(std::stoul("BCD5A089", nullptr, 16));
  shader_hashes_TiledShadingTiledDeferredShading.emplace(std::stoul("C2FC1948", nullptr, 16));
  shader_hashes_TiledShadingTiledDeferredShading.emplace(std::stoul("E3EF3C20", nullptr, 16));
  shader_hashes_TiledShadingTiledDeferredShading.emplace(std::stoul("F8633A07", nullptr, 16));
  // MotionBlur MotionBlur
  shader_hashes_MotionBlur.emplace(std::stoul("D0C2257A", nullptr, 16));
  shader_hashes_MotionBlur.emplace(std::stoul("76B51523", nullptr, 16));
  shader_hashes_MotionBlur.emplace(std::stoul("6DCC9E5D", nullptr, 16));
  // HDRPostProcess HDRFinalScene (vanilla HDR->SDR tonemapping)
  shader_hashes_HDRPostProcessHDRFinalScene.emplace(std::stoul("B5DC761A", nullptr, 16));
  shader_hashes_HDRPostProcessHDRFinalScene.emplace(std::stoul("17272B5B", nullptr, 16));
  shader_hashes_HDRPostProcessHDRFinalScene.emplace(std::stoul("F87B4963", nullptr, 16));
  shader_hashes_HDRPostProcessHDRFinalScene.emplace(std::stoul("81CE942F", nullptr, 16));
  shader_hashes_HDRPostProcessHDRFinalScene.emplace(std::stoul("83557B79", nullptr, 16));
  shader_hashes_HDRPostProcessHDRFinalScene.emplace(std::stoul("37ACE8EF", nullptr, 16));
  shader_hashes_HDRPostProcessHDRFinalScene.emplace(std::stoul("66FD11D0", nullptr, 16));
  // PostAA PostAA
#if 0 // These passes don't have any projection jitters (unless maybe "SMAA 1TX" could have them if we forced them through config), so we can't replace them with DLSS SR
  shader_hashes_PostAA.emplace(std::stoul("D8072D98", nullptr, 16)); // FXAA
  shader_hashes_PostAA.emplace(std::stoul("E9D92B11", nullptr, 16)); // SMAA 1TX
#endif
  shader_hashes_PostAA.emplace(std::stoul("BF813081", nullptr, 16)); // SMAA 2TX and TAA
  // PostAA PostAAComposites
  shader_hashes_PostAAComposites.emplace(std::stoul("83AE9250", nullptr, 16));
  shader_hashes_PostAAComposites.emplace(std::stoul("496492FE", nullptr, 16));
  shader_hashes_PostAAComposites.emplace(std::stoul("ED6287FE", nullptr, 16));
  shader_hashes_PostAAComposites.emplace(std::stoul("FAEE5EE9", nullptr, 16));
  shader_hash_PostAAUpscaleImage = std::stoul("C2F1D3F6", nullptr, 16); // Upscaling (post TAA, only when in frames where DRS engage)
  shader_hashes_LensOptics.emplace(std::stoul("4435D741", nullptr, 16));
  shader_hashes_LensOptics.emplace(std::stoul("C54F3986", nullptr, 16));
  shader_hashes_LensOptics.emplace(std::stoul("DAA20F29", nullptr, 16));
  shader_hashes_LensOptics.emplace(std::stoul("047AB485", nullptr, 16));
  shader_hashes_LensOptics.emplace(std::stoul("9D7A97B8", nullptr, 16));
  shader_hashes_LensOptics.emplace(std::stoul("9B2630A0", nullptr, 16));
  shader_hashes_LensOptics.emplace(std::stoul("51F2811A", nullptr, 16));
  shader_hashes_LensOptics.emplace(std::stoul("9391298E", nullptr, 16));
  shader_hashes_LensOptics.emplace(std::stoul("ED01E418", nullptr, 16));
  shader_hashes_LensOptics.emplace(std::stoul("53529823", nullptr, 16));
  shader_hashes_LensOptics.emplace(std::stoul("DDDE2220", nullptr, 16));
  //TODOFT: once we have collected 100% of the game shaders, update these hashes lists and make them const

  cb_luma_frame_settings.ScenePeakWhite = default_peak_white;
  cb_luma_frame_settings.ScenePaperWhite = default_paper_white;
  cb_luma_frame_settings.UIPaperWhite = default_paper_white;
  cb_luma_frame_settings.DLSS = 0; // We can't set this to 1 until we verified DLSS engaged correctly and is running

  // Load settings
  {
      reshade::api::effect_runtime* runtime = nullptr;
      uint32_t config_version = VERSION;
      reshade::get_config_value(runtime, NAME, "Version", config_version);
      if (config_version != VERSION) {
          if (config_version < VERSION) {
              // NOTE: put behaviour to load previous versions into new ones here (possibly even force recompile shaders)
          }
          else if (config_version > VERSION) {
              reshade::log::message(reshade::log::level::warning, "Prey Luma: trying to load a config from a newer version of the mod, loading might have unexpected results");
          }
          reshade::set_config_value(runtime, NAME, "Version", VERSION);
      }

      reshade::get_config_value(runtime, NAME, "DLSSSuperResolution", dlss_sr);
      reshade::get_config_value(runtime, NAME, "TonemapUIBackground", tonemap_ui_background);
      reshade::get_config_value(runtime, NAME, "ForceSDR", cb_luma_frame_settings.ForceSDR);
      if (reshade::get_config_value(runtime, NAME, "ScenePeakWhite", cb_luma_frame_settings.ScenePeakWhite) && cb_luma_frame_settings.ScenePeakWhite <= 0.f) {
          cb_luma_frame_settings.ScenePeakWhite = default_user_peak_white;
      }
      reshade::get_config_value(runtime, NAME, "ScenePaperWhite", cb_luma_frame_settings.ScenePaperWhite);
      reshade::get_config_value(runtime, NAME, "UIPaperWhite", cb_luma_frame_settings.UIPaperWhite);

      ShaderDefineData::Load(shader_defines_data, runtime);
  }

  ShaderDefineData::OnCompilation(shader_defines_data);

  // Pre-load all shaders to minimize the wait before replacing them after they are found in game ("auto_load"),
  // and to fill the list of shaders we customized, so we can know which ones we need replace on the spot.
  if (precompile_custom_shaders) {
    if (thread_auto_loading.joinable()) {
      thread_auto_loading.join();
    }
    thread_auto_loading_running = true;
    static std::binary_semaphore async_shader_compilation_semaphore{0};
    thread_auto_loading = std::thread([] {
      // We need to lock this mutex for the whole async shader loading, so that if the game starts loading shaders, we can already see if we have a custom version and live load it ("live_load"), otherwise the "custom_shaders_cache" list would be incomplete
      const std::lock_guard<std::recursive_mutex> lock(s_mutex_loading);
      // This is needed to make sure this thread locks "s_mutex_loading" before any other function could
      async_shader_compilation_semaphore.release();
      CompileCustomShaders();
      thread_auto_loading_running = false;
    });
    async_shader_compilation_semaphore.acquire();
  }
}

void Uninit() {
  if (thread_auto_dumping.joinable()) {
    thread_auto_dumping.join(); //TODOFT: safe?
  }
  if (thread_auto_loading.joinable()) {
    thread_auto_loading.join();
  }
}

extern "C" __declspec(dllexport) bool AddonInit(HMODULE addon_module, HMODULE reshade_module) {
  Init();
  return true;
}
extern "C" __declspec(dllexport) void AddonUninit(HMODULE addon_module, HMODULE reshade_module) {
  Uninit();
}

BOOL APIENTRY DllMain(HMODULE h_module, DWORD fdw_reason, LPVOID lpv_reserved) {
  switch (fdw_reason) {
    case DLL_PROCESS_ATTACH:
      if (!reshade::register_addon(h_module)) return FALSE;

#if DEVELOPMENT
      renodx::utils::descriptor::Use(fdw_reason);
#endif // DEVELOPMENT

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

#if !REPLACE_SAMPLERS_LIVE
      reshade::register_event<reshade::addon_event::create_sampler>(OnCreateSampler);
#endif
#if REPLACE_SAMPLERS_LIVE
      reshade::register_event<reshade::addon_event::init_sampler>(OnInitSampler);
      reshade::register_event<reshade::addon_event::destroy_sampler>(OnDestroySampler);
#endif

      reshade::register_event<reshade::addon_event::bind_viewports>(OnBindViewports);

      reshade::register_event<reshade::addon_event::present>(OnPresent);

      reshade::register_event<reshade::addon_event::reshade_present>(OnReshadePresent);

      reshade::register_overlay(NAME, OnRegisterOverlay);

      break;
    case DLL_PROCESS_DETACH:
#if DEVELOPMENT
      renodx::utils::descriptor::Use(fdw_reason);
#endif // DEVELOPMENT

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

#if !REPLACE_SAMPLERS_LIVE
      reshade::unregister_event<reshade::addon_event::create_sampler>(OnCreateSampler);
#endif
#if REPLACE_SAMPLERS_LIVE
      reshade::unregister_event<reshade::addon_event::init_sampler>(OnInitSampler);
      reshade::unregister_event<reshade::addon_event::destroy_sampler>(OnDestroySampler);
#endif

      reshade::unregister_event<reshade::addon_event::bind_viewports>(OnBindViewports);

      reshade::unregister_event<reshade::addon_event::present>(OnPresent);

      reshade::unregister_event<reshade::addon_event::reshade_present>(OnReshadePresent);

      reshade::unregister_overlay(NAME, OnRegisterOverlay);

      reshade::unregister_addon(h_module);

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

  return TRUE;
}
