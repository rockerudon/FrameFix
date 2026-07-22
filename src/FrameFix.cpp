#define WIN32_LEAN_AND_MEAN

#include "Ashita.h"

#include <cctype>
#include <intrin.h>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

#pragma comment(linker, "/EXPORT:expCreatePlugin=_expCreatePlugin@4")
#pragma comment(linker, "/EXPORT:expDestroyPlugin=_expDestroyPlugin@4")
#pragma comment(linker, "/EXPORT:expGetInterfaceVersion=_expGetInterfaceVersion@0")
#pragma comment(lib, "user32.lib")

namespace
{
    struct PatchSite
    {
        uintptr_t address = 0;
        uint8_t original[5]{};
        bool patched = false;
    };

    struct BytesPatchSite
    {
        uintptr_t address = 0;
        uint8_t original[16]{};
        uint32_t length = 0;
        bool patched = false;
    };

    uintptr_t g_statePointerLocation = 0;
    uintptr_t g_divisorCacheLocation = 0;
    uintptr_t g_divisorSetterAddress = 0;
    uintptr_t g_addonStyleSignatureAddress = 0;
    uintptr_t g_addonStylePointerLocation = 0;
    uintptr_t g_cameraPointerLocation = 0;
    uintptr_t g_gameMenuCurrentLocation = 0;
    uintptr_t g_nativeChatWinPtr1Location = 0;
    uintptr_t g_nativeChatWinPtr2Location = 0;
    float g_minScalar                = 1.0f;
    float g_maxScalar                = 4.0f;
    constexpr uint32_t kAnimationScalarHistoryCount = 4;
    float g_animationScalar          = 1.0f;
    float g_animationRawScalar       = 1.0f;
    float g_animationTargetScalar    = 1.0f;
    float g_animationScalarHistory[kAnimationScalarHistoryCount] = {1.0f, 1.0f, 1.0f, 1.0f};
    bool g_animationScalarInitialized = false;
    uint32_t g_animationScalarHistoryIndex = 0;
    bool g_uiTickHookEnabled         = false;
    uint32_t g_menuExtraMethodPasses = 0;
    uint32_t g_chatExtraUpdatePasses = 0;
    bool g_chatTickHookEnabled       = false;
    bool g_nativeChatWindowCatchupEnabled = false;
    uint32_t g_chatTickStep          = 1;
    uintptr_t g_chatRootUpdateOriginal = 0;
    uintptr_t g_nativeChatScrollReturn = 0;

    uintptr_t g_uiComponentUpdateOriginal = 0;
    bool g_uiComponentCatchupEnabled      = false;
    bool g_uiComponentUpdateInside        = false;
    uint32_t g_uiComponentExtraPasses     = 0;
    bool g_chatRootUpdateInside           = false;
    double g_uiComponentTickCarry         = 0.0;
    uintptr_t g_uiRootUpdateOriginal      = 0;
    bool g_uiRootCatchupEnabled           = false;
    bool g_uiRootUpdateInside             = false;
    uint32_t g_uiRootExtraPasses          = 0;
    double g_uiRootTickCarry              = 0.0;
    float g_uiRootSmoothFactor            = 0.555555582f;
    float g_uiRootMaxStep                 = 1.0f;
    float g_uiRootMinStep                 = -1.0f;
    uint32_t g_renderFrameSerial      = 0;
    double g_lastFrameDtMs            = 16.667;

    struct Vec3
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
    };

    using SetTransformFn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice8*, D3DTRANSFORMSTATETYPE, const D3DMATRIX*);

    SetTransformFn g_originalSetTransform = nullptr;
    void** g_setTransformSlot             = nullptr;
    bool g_cameraFixEnabled               = false;
    bool g_cameraInitialized              = false;
    uint32_t g_cameraLastFrameSerial      = 0xFFFFFFFF;
    uint32_t g_cameraAdjustedFrames       = 0;
    uint32_t g_cameraViewCalls            = 0;
    uint32_t g_cameraAcceptedViews        = 0;
    uint32_t g_cameraRejectedViews        = 0;
    uint32_t g_cameraHugeRejects          = 0;
    float g_cameraLastDistance            = 0.0f;
    float g_cameraMaxDistance             = 0.0f;
    float g_cameraMainMaxDistance         = 8.0f;
    float g_cameraWallJitterDistance      = 0.65f;
    uint32_t g_cameraStableFrames         = 0;
    Vec3 g_cameraFrameNearestEye{};
    uint32_t g_cameraFrameSerial          = 0xFFFFFFFF;
    float g_cameraFrameNearestDistance    = 9999.0f;
    bool g_cameraFrameHasNearest          = false;
    float g_cameraLastFrameNearestDistance = 0.0f;
    bool g_cameraHasLastFrameNearest       = false;
    uint32_t g_cameraClampMissFrames       = 0;
    bool g_cameraArmInitialized            = false;
    float g_cameraArmDistance              = 0.0f;
    Vec3 g_cameraArmDirection{};
    uint32_t g_cameraArmHoldFrames         = 0;
    Vec3 g_playerPosition{};
    bool g_hasPlayerPosition = false;
    float g_cameraPlayerDistance = 0.0f;
    float g_cameraMinPlayerDistance = 0.0f;
    float g_cameraMaxPlayerDistance = 35.0f;
    BytesPatchSite g_cameraCollisionStepSites[3]{};
    BytesPatchSite g_cameraJitterPointerSites[2]{};
    float g_cameraJitterValue = 1.0f;

    struct ViewCallerStats
    {
        uintptr_t returnAddress = 0;
        uint32_t frameSerial = 0xFFFFFFFF;
        uint32_t currentFrameCount = 0;
        uint32_t lastFrameCount = 0;
    };

    constexpr uint32_t kViewCallerCapacity = 16;
    ViewCallerStats g_viewCallers[kViewCallerCapacity]{};

    struct NativeChatDelayState
    {
        uintptr_t self = 0;
        double carry = 0.0;
        uint32_t lastFrame = 0xFFFFFFFF;
    };

    NativeChatDelayState g_nativeChatDelayStates[8]{};
    double g_nativeChatWindowCarry[2]{};
    uint32_t g_nativeChatWindowLastFrame[2] = {0xFFFFFFFF, 0xFFFFFFFF};

    template<typename T>
    bool safe_read(const uintptr_t address, T& value)
    {
        __try
        {
            value = *reinterpret_cast<const T*>(address);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    template<typename T>
    bool safe_write(const uintptr_t address, const T& value)
    {
        DWORD oldProtect = 0;
        if (!::VirtualProtect(reinterpret_cast<void*>(address), sizeof(T), PAGE_EXECUTE_READWRITE, &oldProtect))
            return false;

        __try
        {
            *reinterpret_cast<T*>(address) = value;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            DWORD unused = 0;
            ::VirtualProtect(reinterpret_cast<void*>(address), sizeof(T), oldProtect, &unused);
            return false;
        }

        DWORD unused = 0;
        ::VirtualProtect(reinterpret_cast<void*>(address), sizeof(T), oldProtect, &unused);
        return true;
    }

    bool matches(const uint8_t* data, const uint8_t* pattern, const char* mask, const size_t length)
    {
        __try
        {
            for (size_t i = 0; i < length; ++i)
            {
                if (mask[i] == 'x' && data[i] != pattern[i])
                    return false;
            }
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool get_module_range(const char* moduleName, uintptr_t& base, uint32_t& size)
    {
        const auto module = ::GetModuleHandleA(moduleName);
        if (module == nullptr)
            return false;

        IMAGE_DOS_HEADER dos{};
        if (!safe_read(reinterpret_cast<uintptr_t>(module), dos) || dos.e_magic != IMAGE_DOS_SIGNATURE)
            return false;

        IMAGE_NT_HEADERS32 nt{};
        if (!safe_read(reinterpret_cast<uintptr_t>(module) + static_cast<uintptr_t>(dos.e_lfanew), nt) ||
            nt.Signature != IMAGE_NT_SIGNATURE)
            return false;

        base = reinterpret_cast<uintptr_t>(module);
        size = nt.OptionalHeader.SizeOfImage;
        return size != 0;
    }

    bool find_fps_control(uintptr_t& pointerLocation, uintptr_t& cacheLocation, uintptr_t& setterAddress)
    {
        uintptr_t base = 0;
        uint32_t size  = 0;
        if (!get_module_range("FFXiMain.dll", base, size) && !get_module_range("ffximain.dll", base, size))
            return false;

        static const uint8_t pattern[] = {
            0x8B, 0x44, 0x24, 0x04, 0x8B, 0x0D, 0x00, 0x00, 0x00, 0x00,
            0x81, 0xEC, 0x00, 0x01, 0x00, 0x00, 0x3B, 0xC1, 0x74, 0x21, 0x8B, 0x0D,
            0x00, 0x00, 0x00, 0x00, 0x50, 0x8D, 0x54, 0x24, 0x04, 0x68,
        };
        static const char mask[] = "xxxxxx????xxxxxxxxxxxx????xxxxxx";

        for (uint32_t offset = 0; offset + sizeof(pattern) <= size; ++offset)
        {
            const auto current = reinterpret_cast<const uint8_t*>(base + offset);
            if (!matches(current, pattern, mask, sizeof(pattern)))
                continue;

            uintptr_t cacheImmediate = 0;
            uintptr_t pointerImmediate = 0;
            if (!safe_read(base + offset + 6, cacheImmediate) || !safe_read(base + offset + 22, pointerImmediate))
                return false;

            setterAddress = base + offset;
            cacheLocation = cacheImmediate;
            pointerLocation = pointerImmediate;
            return true;
        }

        return false;
    }

    bool find_fps_control()
    {
        return find_fps_control(g_statePointerLocation, g_divisorCacheLocation, g_divisorSetterAddress);
    }

    bool find_addon_style_fps_pointer(uintptr_t& signatureAddress, uintptr_t& pointerLocation)
    {
        uintptr_t base = 0;
        uint32_t size  = 0;
        if (!get_module_range("FFXiMain.dll", base, size) && !get_module_range("ffximain.dll", base, size))
            return false;

        static const uint8_t pattern[] = {
            0x81, 0xEC, 0x00, 0x01, 0x00, 0x00, 0x3B, 0xC1, 0x74, 0x21, 0x8B, 0x0D,
        };

        for (uint32_t offset = 0; offset + sizeof(pattern) + 4 <= size; ++offset)
        {
            const auto current = reinterpret_cast<const uint8_t*>(base + offset);
            if (!matches(current, pattern, "xxxxxxxxxxxx", sizeof(pattern)))
                continue;

            uintptr_t immediate = 0;
            if (!safe_read(base + offset + 12, immediate))
                return false;

            signatureAddress = base + offset;
            pointerLocation = immediate;
            return true;
        }

        return false;
    }

    bool find_game_menu_current_location(uintptr_t& currentLocation)
    {
        uintptr_t base = 0;
        uint32_t size  = 0;
        if (!get_module_range("FFXiMain.dll", base, size) && !get_module_range("ffximain.dll", base, size))
            return false;

        // Current menu object comparison used by the game menu update pass:
        // mov ecx,[eax+0Ch]; test ecx,ecx; ...; cmp eax,[current_menu_global]
        static const uint8_t pattern[] = {
            0x8B, 0x48, 0x0C, 0x85, 0xC9, 0x74, 0x00, 0x8B, 0x51, 0x08,
            0x85, 0xD2, 0x74, 0x00, 0x3B, 0x05, 0x00, 0x00, 0x00, 0x00,
        };
        static const char mask[] = "xxxxxx?xxxxxx?xx????";

        for (uint32_t offset = 0; offset + sizeof(pattern) <= size; ++offset)
        {
            const auto current = reinterpret_cast<const uint8_t*>(base + offset);
            if (!matches(current, pattern, mask, sizeof(pattern)))
                continue;

            uintptr_t immediate = 0;
            if (!safe_read(base + offset + 16, immediate))
                return false;

            currentLocation = immediate;
            return true;
        }

        return false;
    }

    bool find_native_chat_window_pointer_locations(uintptr_t& winPtr1Location, uintptr_t& winPtr2Location)
    {
        uintptr_t base = 0;
        uint32_t size  = 0;
        if (!get_module_range("FFXiMain.dll", base, size) && !get_module_range("ffximain.dll", base, size))
            return false;

        // Same signature used by FancyChat to locate the two native legacy chat windows.
        static const uint8_t pattern[] = {
            0xA1, 0x00, 0x00, 0x00, 0x00, 0xC6, 0x40, 0x59, 0x01, 0x8B, 0x0D,
            0x00, 0x00, 0x00, 0x00, 0xC6, 0x41, 0x59, 0x01, 0xC2, 0x08, 0x00,
        };
        static const char mask[] = "x????xxxxxx????xxxxxxx";

        for (uint32_t offset = 0; offset + sizeof(pattern) <= size; ++offset)
        {
            const auto current = reinterpret_cast<const uint8_t*>(base + offset);
            if (!matches(current, pattern, mask, sizeof(pattern)))
                continue;

            uintptr_t first = 0;
            uintptr_t second = 0;
            if (!safe_read(base + offset + 0x01, first) || !safe_read(base + offset + 0x0B, second))
                return false;

            winPtr1Location = first;
            winPtr2Location = second;
            return true;
        }

        return false;
    }

    bool find_camera_pointer_location(uintptr_t& pointerLocation)
    {
        uintptr_t base = 0;
        uint32_t size  = 0;
        if (!get_module_range("FFXiMain.dll", base, size) && !get_module_range("ffximain.dll", base, size))
            return false;

        static const uint8_t pattern[] = {
            0x83, 0xC4, 0x04, 0x85, 0xC9, 0x74, 0x11, 0x8B,
            0x11, 0x6A, 0x01, 0xFF, 0x52, 0x18, 0xC7, 0x05,
        };
        static const char mask[] = "xxxxxxxxxxxxxxxx";

        for (uint32_t offset = 0; offset + sizeof(pattern) + sizeof(uintptr_t) <= size; ++offset)
        {
            const auto current = reinterpret_cast<const uint8_t*>(base + offset);
            if (!matches(current, pattern, mask, sizeof(pattern)))
                continue;

            uintptr_t immediate = 0;
            if (!safe_read(base + offset + 0x10, immediate) || immediate == 0)
                return false;

            pointerLocation = immediate;
            return true;
        }

        return false;
    }

    bool looks_like_time_scalar_function(const uintptr_t address)
    {
        // GameManager::CheckTick-like helper:
        // mov ecx, [state_ptr]; fld [ecx+28h]; fcomp [one]; ...; ret
        static const uint8_t pattern[] = {
            0x8B, 0x0D, 0x00, 0x00, 0x00, 0x00, 0xD9, 0x41, 0x28, 0xD8, 0x1D, 0x00, 0x00,
            0x00, 0x00, 0xDF, 0xE0, 0xF6, 0xC4, 0x05, 0x7A, 0x07, 0xD9, 0x05, 0x00, 0x00,
            0x00, 0x00, 0xC3, 0xD9, 0x41, 0x28, 0xC3,
        };
        static const char mask[] = "xx????xxxxx????xxxxxxxxx????xxxxx";

        return matches(reinterpret_cast<const uint8_t*>(address), pattern, mask, sizeof(pattern));
    }

    uintptr_t relative_call_target(const uintptr_t callsite)
    {
        uint8_t opcode = 0;
        int32_t relative = 0;
        if (!safe_read(callsite, opcode) || opcode != 0xE8 || !safe_read(callsite + 1, relative))
            return 0;

        return callsite + 5 + static_cast<intptr_t>(relative);
    }

    uintptr_t find_animation_frame_tick_callsite()
    {
        uintptr_t base = 0;
        uint32_t size  = 0;
        if (!get_module_range("FFXiMain.dll", base, size) && !get_module_range("ffximain.dll", base, size))
            return 0;

        // AnimationTrack::UpdateFrame, found from XIClient:
        // sub esp,0Ch; push esi; mov esi,ecx; push edi; mov eax,[esi+34h]; lea edi,[esi+34h]
        static const uint8_t functionPattern[] = {
            0x83, 0xEC, 0x0C, 0x56, 0x8B, 0xF1, 0x57, 0x8B,
            0x46, 0x34, 0x8D, 0x7E, 0x34, 0x85, 0xC0,
        };
        static const char functionMask[] = "xxxxxxxxxxxxxxx";

        // call CheckTick; fmul [esp+10h]; fadd [esi+24h]; fstp [esi+24h]
        static const uint8_t callPattern[] = {
            0xE8, 0x00, 0x00, 0x00, 0x00,
            0xD8, 0x4C, 0x24, 0x10,
            0xD8, 0x46, 0x24,
            0xD9, 0x5E, 0x24,
        };
        static const char callMask[] = "x????xxxxxxxxxx";

        for (uint32_t offset = 0; offset + sizeof(functionPattern) <= size; ++offset)
        {
            const uintptr_t function = base + offset;
            const auto current = reinterpret_cast<const uint8_t*>(function);
            if (!matches(current, functionPattern, functionMask, sizeof(functionPattern)))
                continue;

            const uint32_t searchEnd = (offset + 0x260 < size) ? 0x260 : size - offset;
            for (uint32_t inner = 0x40; inner + sizeof(callPattern) <= searchEnd; ++inner)
            {
                const uintptr_t callsite = function + inner;
                const auto callBytes = reinterpret_cast<const uint8_t*>(callsite);
                if (!matches(callBytes, callPattern, callMask, sizeof(callPattern)))
                    continue;

                const uintptr_t target = relative_call_target(callsite);
                if (target < base || target >= base + size)
                    continue;

                if (!looks_like_time_scalar_function(target))
                    continue;

                return callsite;
            }
        }

        return 0;
    }

    uintptr_t find_animation_blend_tick_callsite()
    {
        uintptr_t base = 0;
        uint32_t size  = 0;
        if (!get_module_range("FFXiMain.dll", base, size) && !get_module_range("ffximain.dll", base, size))
            return 0;

        // AnimationTrack::UpdateBlendState, found from XIClient:
        // push ecx; push esi; mov esi,ecx; call CheckTick; fstp [esp+4]; ...
        static const uint8_t pattern[] = {
            0x51, 0x56, 0x8B, 0xF1,
            0xE8, 0x00, 0x00, 0x00, 0x00,
            0xD9, 0x5C, 0x24, 0x04,
            0x33, 0xC0,
            0xB9, 0x00, 0x00, 0x80, 0x3F,
            0x8A, 0x46, 0x08,
        };
        static const char mask[] = "xxxxx????xxxxxxxxxxxxxx";

        for (uint32_t offset = 0; offset + sizeof(pattern) <= size; ++offset)
        {
            const uintptr_t function = base + offset;
            const auto current = reinterpret_cast<const uint8_t*>(function);
            if (!matches(current, pattern, mask, sizeof(pattern)))
                continue;

            const uintptr_t callsite = function + 4;
            const uintptr_t target = relative_call_target(callsite);
            if (target < base || target >= base + size)
                continue;

            if (!looks_like_time_scalar_function(target))
                continue;

            return callsite;
        }

        return 0;
    }

    uintptr_t find_chat_root_update_callsite(uintptr_t& originalFunction)
    {
        originalFunction = 0;

        uintptr_t base = 0;
        uint32_t size  = 0;
        if (!get_module_range("FFXiMain.dll", base, size) && !get_module_range("ffximain.dll", base, size))
            return 0;

        // Main UI pass call:
        // mov ecx,[chat_root_global]; cmp ecx,ebx; je +5; call chat_root_update;
        // followed by a read of the FPS/control global.
        static const uint8_t pattern[] = {
            0x8B, 0x0D, 0x00, 0x00, 0x00, 0x00, 0x3B, 0xCB, 0x74, 0x05,
            0xE8, 0x00, 0x00, 0x00, 0x00, 0xA1, 0x00, 0x00, 0x00, 0x00,
            0x38, 0x58, 0x08,
        };
        static const char mask[] = "xx????xxxxx????x????xxx";

        for (uint32_t offset = 0; offset + sizeof(pattern) <= size; ++offset)
        {
            const auto current = reinterpret_cast<const uint8_t*>(base + offset);
            if (!matches(current, pattern, mask, sizeof(pattern)))
                continue;

            const uintptr_t callsite = base + offset + 10;
            int32_t relative = 0;
            if (!safe_read(callsite + 1, relative))
                continue;

            const uintptr_t target = callsite + 5 + static_cast<intptr_t>(relative);
            if (target < base || target >= base + size)
                continue;

            originalFunction = target;
            return callsite;
        }

        return 0;
    }

    uintptr_t find_native_chat_delay_function()
    {
        uintptr_t base = 0;
        uint32_t size  = 0;
        if (!get_module_range("FFXiMain.dll", base, size) && !get_module_range("ffximain.dll", base, size))
            return 0;

        // Native chat message animation helper.  Ashita's instantchat addon
        // patches inside this function; FrameFix keeps it intact and runs it
        // additional times when render frames are missed.
        static const uint8_t pattern[] = {
            0x8A, 0x44, 0x24, 0x04, 0x83, 0xEC, 0x08, 0x84, 0xC0, 0x53, 0x55, 0x56,
            0x57, 0x8B, 0xF1, 0x74, 0x00, 0x8B, 0x46, 0x34, 0x0F, 0xBF, 0x4E, 0x54,
            0x85, 0xC0, 0x7D, 0x00, 0x89, 0x4E, 0x34,
        };
        static const char mask[] = "xxxxxxxxxxxxxxxx?xxxxxxxxxx?xxx";

        for (uint32_t offset = 0; offset + sizeof(pattern) <= size; ++offset)
        {
            const auto current = reinterpret_cast<const uint8_t*>(base + offset);
            if (!matches(current, pattern, mask, sizeof(pattern)))
                continue;

            return base + offset;
        }

        return 0;
    }

    uintptr_t find_native_chat_scroll_patch_site()
    {
        const auto function = find_native_chat_delay_function();
        if (function == 0)
            return 0;

        // mov eax,[esi+3Ch]; mov edx,[esi+34h]; add eax,14h; mov [esi+3Ch],eax
        static const uint8_t expected[] = {
            0x8B, 0x46, 0x3C, 0x8B, 0x56, 0x34, 0x83, 0xC0, 0x14, 0x89, 0x46, 0x3C,
        };

        const auto address = function + 0x26;
        if (matches(reinterpret_cast<const uint8_t*>(address), expected, "xxxxxxxxxxxx", sizeof(expected)))
            return address;

        return 0;
    }

    bool write_call(PatchSite& site, void* destination)
    {
        DWORD oldProtect = 0;
        if (!::VirtualProtect(reinterpret_cast<void*>(site.address), 5, PAGE_EXECUTE_READWRITE, &oldProtect))
            return false;

        std::memcpy(site.original, reinterpret_cast<void*>(site.address), sizeof(site.original));

        const auto relative = reinterpret_cast<intptr_t>(destination) - static_cast<intptr_t>(site.address) - 5;
        auto* bytes         = reinterpret_cast<uint8_t*>(site.address);
        bytes[0]            = 0xE8;
        *reinterpret_cast<int32_t*>(bytes + 1) = static_cast<int32_t>(relative);

        ::FlushInstructionCache(::GetCurrentProcess(), reinterpret_cast<void*>(site.address), 5);
        DWORD unused = 0;
        ::VirtualProtect(reinterpret_cast<void*>(site.address), 5, oldProtect, &unused);
        site.patched = true;
        return true;
    }

    bool restore_site(PatchSite& site)
    {
        if (!site.patched)
            return true;

        DWORD oldProtect = 0;
        if (!::VirtualProtect(reinterpret_cast<void*>(site.address), 5, PAGE_EXECUTE_READWRITE, &oldProtect))
            return false;

        std::memcpy(reinterpret_cast<void*>(site.address), site.original, sizeof(site.original));
        ::FlushInstructionCache(::GetCurrentProcess(), reinterpret_cast<void*>(site.address), 5);

        DWORD unused = 0;
        ::VirtualProtect(reinterpret_cast<void*>(site.address), 5, oldProtect, &unused);
        site.patched = false;
        return true;
    }

    bool restore_bytes(BytesPatchSite& site)
    {
        if (!site.patched)
            return true;

        DWORD oldProtect = 0;
        if (!::VirtualProtect(reinterpret_cast<void*>(site.address), site.length, PAGE_EXECUTE_READWRITE, &oldProtect))
            return false;

        std::memcpy(reinterpret_cast<void*>(site.address), site.original, site.length);
        ::FlushInstructionCache(::GetCurrentProcess(), reinterpret_cast<void*>(site.address), site.length);

        DWORD unused = 0;
        ::VirtualProtect(reinterpret_cast<void*>(site.address), site.length, oldProtect, &unused);
        site.patched = false;
        return true;
    }

    bool write_bytes_patch(BytesPatchSite& site, const uintptr_t address, const uint8_t* patch, const uint32_t length)
    {
        if (site.patched)
            return true;

        if (length == 0 || length > sizeof(site.original))
            return false;

        DWORD oldProtect = 0;
        if (!::VirtualProtect(reinterpret_cast<void*>(address), length, PAGE_EXECUTE_READWRITE, &oldProtect))
            return false;

        site.address = address;
        site.length  = length;
        std::memcpy(site.original, reinterpret_cast<void*>(address), length);
        std::memcpy(reinterpret_cast<void*>(address), patch, length);

        ::FlushInstructionCache(::GetCurrentProcess(), reinterpret_cast<void*>(address), length);
        DWORD unused = 0;
        ::VirtualProtect(reinterpret_cast<void*>(address), length, oldProtect, &unused);
        site.patched = true;
        return true;
    }

    bool write_jump_with_nops(BytesPatchSite& site, const uintptr_t address, const uint32_t length, void* destination)
    {
        if (site.patched)
            return true;

        if (length < 5 || length > sizeof(site.original))
            return false;

        DWORD oldProtect = 0;
        if (!::VirtualProtect(reinterpret_cast<void*>(address), length, PAGE_EXECUTE_READWRITE, &oldProtect))
            return false;

        site.address = address;
        site.length = length;
        std::memcpy(site.original, reinterpret_cast<void*>(address), length);

        const auto relative = reinterpret_cast<intptr_t>(destination) - static_cast<intptr_t>(address) - 5;
        auto* bytes = reinterpret_cast<uint8_t*>(address);
        bytes[0] = 0xE9;
        *reinterpret_cast<int32_t*>(bytes + 1) = static_cast<int32_t>(relative);
        for (uint32_t index = 5; index < length; ++index)
            bytes[index] = 0x90;

        ::FlushInstructionCache(::GetCurrentProcess(), reinterpret_cast<void*>(address), length);
        DWORD unused = 0;
        ::VirtualProtect(reinterpret_cast<void*>(address), length, oldProtect, &unused);
        site.patched = true;
        return true;
    }

    uintptr_t find_camera_update_function()
    {
        uintptr_t base = 0;
        uint32_t size  = 0;
        if (!get_module_range("FFXiMain.dll", base, size) && !get_module_range("ffximain.dll", base, size))
            return 0;

        static const uint8_t pattern[] = {
            0x81, 0xEC, 0xE0, 0x01, 0x00, 0x00, 0xA0, 0x00, 0x00, 0x00, 0x00,
            0x56, 0x57, 0x8B, 0xF9, 0x84, 0xC0, 0x89, 0x7C, 0x24, 0x10,
        };
        static const char mask[] = "xxxxxxx????xxxxxxxxxx";

        for (uint32_t offset = 0; offset + sizeof(pattern) <= size; ++offset)
        {
            const auto current = reinterpret_cast<const uint8_t*>(base + offset);
            if (matches(current, pattern, mask, sizeof(pattern)))
                return base + offset;
        }

        return 0;
    }

    bool patch_camera_collision_substeps()
    {
        const uintptr_t cameraUpdate = find_camera_update_function();
        if (cameraUpdate == 0)
            return false;

        // These three sites are inside the camera collision resolver. The game
        // normally exposes every other collision substep at 60 FPS, which makes
        // wall contact look like a persistent flicker. Keeping two local
        // collision passes here matches the settled 30 FPS behavior without
        // touching global clocks, menus, cooldowns, or the final view matrix.
        static const uint32_t offsets[] = {0x0BD8, 0x101C, 0x12E8};
        static const uint8_t patch[]    = {0xB8, 0x02, 0x00, 0x00, 0x00, 0x90, 0x90, 0x90, 0x90, 0x90};

        bool ok = true;
        for (size_t index = 0; index < 3; ++index)
        {
            const uintptr_t address = cameraUpdate + offsets[index];
            const auto* bytes = reinterpret_cast<const uint8_t*>(address);
            if (!g_cameraCollisionStepSites[index].patched && (bytes[0] != 0xE8 || bytes[5] != 0xE8))
            {
                ok = false;
                continue;
            }

            ok = write_bytes_patch(g_cameraCollisionStepSites[index], address, patch, sizeof(patch)) && ok;
        }

        return ok;
    }

    bool restore_camera_collision_substeps()
    {
        bool ok = true;
        for (auto& site : g_cameraCollisionStepSites)
            ok = restore_bytes(site) && ok;
        return ok;
    }

    uintptr_t find_camera_jitter_signature()
    {
        uintptr_t base = 0;
        uint32_t size  = 0;
        if (!get_module_range("FFXiMain.dll", base, size) && !get_module_range("ffximain.dll", base, size))
            return 0;

        static const uint8_t pattern[] = {
            0x8D, 0x54, 0x24, 0x2C, 0x8D, 0x44, 0x24, 0x2C,
            0xD8, 0xC9, 0x52, 0x55, 0x50,
        };

        for (uint32_t offset = 0; offset + sizeof(pattern) + 0x20 <= size; ++offset)
        {
            const auto current = reinterpret_cast<const uint8_t*>(base + offset);
            if (std::memcmp(current, pattern, sizeof(pattern)) == 0)
                return base + offset;
        }

        return 0;
    }

    bool patch_camera_jitter_fix()
    {
        const uintptr_t signature = find_camera_jitter_signature();
        if (signature == 0)
            return false;

        uint8_t replacement[4]{};
        *reinterpret_cast<uint32_t*>(replacement) = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&g_cameraJitterValue));

        bool ok = true;
        ok = write_bytes_patch(g_cameraJitterPointerSites[0], signature + 0x0F, replacement, sizeof(replacement)) && ok;
        ok = write_bytes_patch(g_cameraJitterPointerSites[1], signature + 0x1F, replacement, sizeof(replacement)) && ok;
        return ok;
    }

    bool restore_camera_jitter_fix()
    {
        bool ok = true;
        for (auto& site : g_cameraJitterPointerSites)
            ok = restore_bytes(site) && ok;
        return ok;
    }

    Vec3 add_vec3(const Vec3& a, const Vec3& b)
    {
        return {a.x + b.x, a.y + b.y, a.z + b.z};
    }

    Vec3 sub_vec3(const Vec3& a, const Vec3& b)
    {
        return {a.x - b.x, a.y - b.y, a.z - b.z};
    }

    Vec3 mul_vec3(const Vec3& a, const float scalar)
    {
        return {a.x * scalar, a.y * scalar, a.z * scalar};
    }

    float dot_vec3(const Vec3& a, const Vec3& b)
    {
        return a.x * b.x + a.y * b.y + a.z * b.z;
    }

    float length_vec3(const Vec3& a)
    {
        return std::sqrt(dot_vec3(a, a));
    }

    bool finite_vec3(const Vec3& value)
    {
        return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
    }

    bool looks_like_identity_view(const D3DMATRIX& matrix)
    {
        const float axisDelta =
            std::fabs(matrix._11 - 1.0f) + std::fabs(matrix._22 - 1.0f) + std::fabs(matrix._33 - 1.0f) +
            std::fabs(matrix._12) + std::fabs(matrix._13) + std::fabs(matrix._21) + std::fabs(matrix._23) +
            std::fabs(matrix._31) + std::fabs(matrix._32);
        const float translation =
            std::fabs(matrix._41) + std::fabs(matrix._42) + std::fabs(matrix._43);
        return axisDelta < 0.001f && translation < 0.001f;
    }

    bool extract_eye_from_view(const D3DMATRIX& matrix, Vec3& eye, uint32_t& rejectReason)
    {
        rejectReason = 0;
        if (!std::isfinite(matrix._11) || !std::isfinite(matrix._22) || !std::isfinite(matrix._33) ||
            !std::isfinite(matrix._41) || !std::isfinite(matrix._42) || !std::isfinite(matrix._43))
        {
            rejectReason = 1;
            return false;
        }

        if (std::fabs(matrix._44 - 1.0f) > 0.05f)
        {
            rejectReason = 2;
            return false;
        }

        if (looks_like_identity_view(matrix))
        {
            rejectReason = 3;
            return false;
        }

        const Vec3 xAxis{matrix._11, matrix._21, matrix._31};
        const Vec3 yAxis{matrix._12, matrix._22, matrix._32};
        const Vec3 zAxis{matrix._13, matrix._23, matrix._33};

        const float xLen = length_vec3(xAxis);
        const float yLen = length_vec3(yAxis);
        const float zLen = length_vec3(zAxis);
        if (xLen < 0.75f || xLen > 1.25f || yLen < 0.75f || yLen > 1.25f || zLen < 0.75f || zLen > 1.25f)
        {
            rejectReason = 4;
            return false;
        }

        eye = add_vec3(add_vec3(mul_vec3(xAxis, -matrix._41), mul_vec3(yAxis, -matrix._42)), mul_vec3(zAxis, -matrix._43));
        if (!finite_vec3(eye))
        {
            rejectReason = 5;
            return false;
        }

        if (g_hasPlayerPosition)
        {
            g_cameraPlayerDistance = length_vec3(sub_vec3(eye, g_playerPosition));
            if (!std::isfinite(g_cameraPlayerDistance) || g_cameraPlayerDistance < g_cameraMinPlayerDistance ||
                g_cameraPlayerDistance > g_cameraMaxPlayerDistance)
            {
                rejectReason = 6;
                return false;
            }
        }

        return true;
    }

    bool extract_eye_from_view(const D3DMATRIX& matrix, Vec3& eye)
    {
        uint32_t rejectReason = 0;
        return extract_eye_from_view(matrix, eye, rejectReason);
    }

    void write_eye_to_view(D3DMATRIX& matrix, const Vec3& eye)
    {
        const Vec3 xAxis{matrix._11, matrix._21, matrix._31};
        const Vec3 yAxis{matrix._12, matrix._22, matrix._32};
        const Vec3 zAxis{matrix._13, matrix._23, matrix._33};

        matrix._41 = -dot_vec3(xAxis, eye);
        matrix._42 = -dot_vec3(yAxis, eye);
        matrix._43 = -dot_vec3(zAxis, eye);
    }

    uint32_t update_view_caller_stats(const uintptr_t returnAddress)
    {
        ViewCallerStats* freeSlot = nullptr;
        ViewCallerStats* slot = nullptr;

        for (auto& entry : g_viewCallers)
        {
            if (entry.returnAddress == returnAddress)
            {
                slot = &entry;
                break;
            }

            if (entry.returnAddress == 0 && freeSlot == nullptr)
                freeSlot = &entry;
        }

        if (slot == nullptr)
        {
            slot = freeSlot != nullptr ? freeSlot : &g_viewCallers[0];
            *slot = {};
            slot->returnAddress = returnAddress;
            slot->frameSerial = g_renderFrameSerial;
        }

        if (slot->frameSerial != g_renderFrameSerial)
        {
            slot->lastFrameCount = slot->currentFrameCount;
            slot->currentFrameCount = 0;
            slot->frameSerial = g_renderFrameSerial;
        }

        ++slot->currentFrameCount;
        return slot->lastFrameCount;
    }

    bool is_main_camera_candidate(const float playerDistance)
    {
        if (!g_hasPlayerPosition)
            return false;

        if (!std::isfinite(playerDistance) || playerDistance < 0.25f || playerDistance > g_cameraMainMaxDistance)
            return false;

        return true;
    }

    bool clamp_camera_distance(D3DMATRIX& matrix, const Vec3& eye, const float targetDistance)
    {
        if (!g_hasPlayerPosition || !std::isfinite(targetDistance) || targetDistance < 0.25f)
            return false;

        const Vec3 fromPlayer = sub_vec3(eye, g_playerPosition);
        const float currentDistance = length_vec3(fromPlayer);
        if (!std::isfinite(currentDistance) || currentDistance <= 0.001f)
            return false;

        const Vec3 adjustedEye = add_vec3(g_playerPosition, mul_vec3(fromPlayer, targetDistance / currentDistance));
        if (!finite_vec3(adjustedEye))
            return false;

        write_eye_to_view(matrix, adjustedEye);
        return true;
    }

    bool read_camera_struct(uintptr_t& base, Vec3& eye, Vec3& focus)
    {
        if (g_cameraPointerLocation == 0 && !find_camera_pointer_location(g_cameraPointerLocation))
            return false;

        base = 0;
        if (!safe_read(g_cameraPointerLocation, base) || base == 0)
            return false;

        if (!safe_read(base + 0x44, eye.x) || !safe_read(base + 0x48, eye.y) || !safe_read(base + 0x4C, eye.z) ||
            !safe_read(base + 0x50, focus.x) || !safe_read(base + 0x54, focus.y) || !safe_read(base + 0x58, focus.z))
            return false;

        return finite_vec3(eye) && finite_vec3(focus);
    }

    bool write_camera_eye(const uintptr_t base, const Vec3& eye)
    {
        return safe_write(base + 0x44, eye.x) && safe_write(base + 0x48, eye.y) && safe_write(base + 0x4C, eye.z);
    }

    void stabilize_camera_arm()
    {
        if (!g_cameraFixEnabled)
            return;

        uintptr_t base = 0;
        Vec3 eye{};
        Vec3 focus{};
        if (!read_camera_struct(base, eye, focus))
            return;

        const Vec3 arm = sub_vec3(eye, focus);
        const float distance = length_vec3(arm);
        if (!std::isfinite(distance) || distance < 0.25f || distance > 8.0f)
        {
            g_cameraArmInitialized = false;
            g_cameraArmHoldFrames = 0;
            return;
        }

        const Vec3 direction = mul_vec3(arm, 1.0f / distance);

        if (!g_cameraArmInitialized)
        {
            g_cameraArmDistance = distance;
            g_cameraArmDirection = direction;
            g_cameraArmInitialized = true;
            g_cameraArmHoldFrames = 0;
            return;
        }

        const float directionDot = dot_vec3(direction, g_cameraArmDirection);
        if (!std::isfinite(directionDot) || directionDot < 0.9992f)
        {
            // Let the game's collision solver own lateral pushes on uneven walls.
            g_cameraArmDistance = distance;
            g_cameraArmDirection = direction;
            g_cameraArmHoldFrames = 0;
            return;
        }

        const float delta = distance - g_cameraArmDistance;
        g_cameraLastDistance = std::fabs(delta);
        if (g_cameraLastDistance > g_cameraMaxDistance)
            g_cameraMaxDistance = g_cameraLastDistance;

        if (delta < -0.02f)
        {
            g_cameraArmDistance = distance;
            g_cameraArmDirection = direction;
            g_cameraArmHoldFrames = 0;
            return;
        }

        if (delta > 0.02f && delta <= g_cameraWallJitterDistance && g_cameraArmHoldFrames < 2)
        {
            const Vec3 adjustedEye = add_vec3(focus, mul_vec3(arm, g_cameraArmDistance / distance));
            if (finite_vec3(adjustedEye) && write_camera_eye(base, adjustedEye))
            {
                ++g_cameraAdjustedFrames;
                ++g_cameraStableFrames;
                ++g_cameraArmHoldFrames;
            }
            return;
        }

        g_cameraArmDistance = distance;
        g_cameraArmDirection = direction;
        g_cameraArmHoldFrames = 0;
    }

    bool stabilize_camera_view_matrix(D3DMATRIX& matrix, const uint32_t)
    {
        ++g_cameraViewCalls;

        Vec3 eye{};
        uint32_t rejectReason = 0;
        if (!extract_eye_from_view(matrix, eye, rejectReason))
        {
            ++g_cameraRejectedViews;
            return false;
        }

        ++g_cameraAcceptedViews;

        if (!is_main_camera_candidate(g_cameraPlayerDistance))
        {
            ++g_cameraRejectedViews;
            return false;
        }

        if (g_cameraFrameSerial != g_renderFrameSerial)
        {
            if (g_cameraFrameHasNearest && g_cameraFrameNearestDistance >= 0.25f &&
                g_cameraFrameNearestDistance <= g_cameraMainMaxDistance)
            {
                g_cameraLastFrameNearestDistance = g_cameraFrameNearestDistance;
                g_cameraHasLastFrameNearest = true;
                g_cameraClampMissFrames = 0;
            }
            else if (g_cameraHasLastFrameNearest)
            {
                ++g_cameraClampMissFrames;
                if (g_cameraClampMissFrames > 4)
                    g_cameraHasLastFrameNearest = false;
            }

            g_cameraFrameSerial = g_renderFrameSerial;
            g_cameraFrameNearestDistance = 9999.0f;
            g_cameraFrameHasNearest = false;
        }

        const float currentPlayerDistance = g_cameraPlayerDistance;
        if (!std::isfinite(currentPlayerDistance))
        {
            ++g_cameraRejectedViews;
            return false;
        }

        float targetDistance = 0.0f;
        if (g_cameraFrameHasNearest)
            targetDistance = g_cameraFrameNearestDistance;
        else if (g_cameraHasLastFrameNearest)
            targetDistance = g_cameraLastFrameNearestDistance;

        if (targetDistance >= 0.25f && targetDistance < currentPlayerDistance)
        {
            const float distanceDelta = currentPlayerDistance - targetDistance;
            g_cameraLastDistance = distanceDelta;
            if (distanceDelta > g_cameraMaxDistance)
                g_cameraMaxDistance = distanceDelta;

            if (g_cameraClampMissFrames <= 4 && distanceDelta > 0.02f && distanceDelta <= g_cameraWallJitterDistance &&
                clamp_camera_distance(matrix, eye, targetDistance))
            {
                ++g_cameraAdjustedFrames;
                ++g_cameraStableFrames;
                return true;
            }
        }

        if (!g_cameraFrameHasNearest || currentPlayerDistance < g_cameraFrameNearestDistance)
        {
            g_cameraFrameNearestEye = eye;
            g_cameraFrameNearestDistance = currentPlayerDistance;
            g_cameraFrameHasNearest = true;
            return false;
        }

        return false;
    }

    extern "C" HRESULT STDMETHODCALLTYPE set_transform_hook(
        IDirect3DDevice8* device,
        D3DTRANSFORMSTATETYPE state,
        const D3DMATRIX* matrix)
    {
        if (g_originalSetTransform == nullptr)
            return D3DERR_INVALIDCALL;

        const uintptr_t returnAddress = reinterpret_cast<uintptr_t>(_ReturnAddress());
        uint32_t previousFrameCallerCount = 0;
        if (state == D3DTS_VIEW && matrix != nullptr)
            previousFrameCallerCount = update_view_caller_stats(returnAddress);

        if (!g_cameraFixEnabled || state != D3DTS_VIEW || matrix == nullptr)
            return g_originalSetTransform(device, state, matrix);

        D3DMATRIX adjusted = *matrix;
        if (stabilize_camera_view_matrix(adjusted, previousFrameCallerCount))
            return g_originalSetTransform(device, state, &adjusted);

        return g_originalSetTransform(device, state, matrix);
    }

    bool install_set_transform_hook(IDirect3DDevice8* device)
    {
        if (device == nullptr)
            return false;

        auto*** const object = reinterpret_cast<void***>(device);
        void** const vtable = *object;
        if (vtable == nullptr)
            return false;

        constexpr uint32_t kSetTransformIndex = 37;
        void** const slot = &vtable[kSetTransformIndex];
        if (*slot == reinterpret_cast<void*>(&set_transform_hook))
            return true;

        DWORD oldProtect = 0;
        if (!::VirtualProtect(slot, sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect))
            return false;

        g_originalSetTransform = reinterpret_cast<SetTransformFn>(*slot);
        g_setTransformSlot = slot;
        *slot = reinterpret_cast<void*>(&set_transform_hook);
        ::FlushInstructionCache(::GetCurrentProcess(), slot, sizeof(void*));

        DWORD unused = 0;
        ::VirtualProtect(slot, sizeof(void*), oldProtect, &unused);
        return true;
    }

    bool restore_set_transform_hook()
    {
        if (g_setTransformSlot == nullptr || g_originalSetTransform == nullptr)
            return true;

        DWORD oldProtect = 0;
        if (!::VirtualProtect(g_setTransformSlot, sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect))
            return false;

        if (*g_setTransformSlot == reinterpret_cast<void*>(&set_transform_hook))
            *g_setTransformSlot = reinterpret_cast<void*>(g_originalSetTransform);

        ::FlushInstructionCache(::GetCurrentProcess(), g_setTransformSlot, sizeof(void*));

        DWORD unused = 0;
        ::VirtualProtect(g_setTransformSlot, sizeof(void*), oldProtect, &unused);
        g_setTransformSlot = nullptr;
        g_originalSetTransform = nullptr;
        g_cameraInitialized = false;
        return true;
    }

    float clamp_animation_scalar(float value)
    {
        if (!std::isfinite(value))
            return 1.0f;

        if (value < g_minScalar)
            value = g_minScalar;
        if (value > g_maxScalar)
            value = g_maxScalar;

        return value;
    }

    void reset_animation_scalar_state()
    {
        g_animationScalar = 1.0f;
        g_animationRawScalar = 1.0f;
        g_animationTargetScalar = 1.0f;
        for (auto& sample : g_animationScalarHistory)
            sample = 1.0f;
        g_animationScalarInitialized = false;
        g_animationScalarHistoryIndex = 0;
    }

    void update_animation_scalar_from_frame(const double dtMs)
    {
        if (dtMs <= 0.001 || dtMs > 250.0)
            return;

        // XIClient's GameManager smooths CheckTick with a tiny four-sample
        // history. Mirroring that here keeps animation timing faithful without
        // the long EMA tail that could leave motion accelerated after FPS
        // recovered from a dip.
        const float raw = clamp_animation_scalar(static_cast<float>(dtMs * 60.0 / 1000.0));
        g_animationRawScalar = raw;

        if (!g_animationScalarInitialized)
        {
            for (auto& sample : g_animationScalarHistory)
                sample = raw;
            g_animationScalarHistoryIndex = 0;
            g_animationTargetScalar = raw;
            g_animationScalar = std::fabs(raw - 1.0f) <= 0.04f ? 1.0f : raw;
            g_animationScalarInitialized = true;
            return;
        }

        g_animationScalarHistoryIndex = (g_animationScalarHistoryIndex + 1) % kAnimationScalarHistoryCount;
        g_animationScalarHistory[g_animationScalarHistoryIndex] = raw;

        float target = 0.0f;
        for (const auto sample : g_animationScalarHistory)
            target += sample;
        target = clamp_animation_scalar(target / static_cast<float>(kAnimationScalarHistoryCount));
        g_animationTargetScalar = target;

        if (std::fabs(target - 1.0f) <= 0.04f)
            target = 1.0f;

        g_animationScalar = target;
    }

    void update_ui_root_smoothing_from_frame(const double dtMs)
    {
        if (dtMs <= 0.001 || dtMs > 250.0)
            return;

        float scalar = static_cast<float>(dtMs * 60.0 / 1000.0);
        if (!std::isfinite(scalar) || scalar < 1.0f)
            scalar = 1.0f;
        if (scalar > 4.0f)
            scalar = 4.0f;

        g_uiRootSmoothFactor = 0.555555582f * scalar;
        g_uiRootMaxStep = scalar;
        g_uiRootMinStep = -scalar;
    }

    float animation_time_scalar_base()
    {
        uintptr_t state = 0;
        if (g_statePointerLocation == 0 || !safe_read(g_statePointerLocation, state) || state == 0)
            return 1.0f;

        int32_t divisor = 2;
        if (safe_read(state + 0x30, divisor) && divisor != 1)
            return 1.0f;

        const float value = g_animationScalarInitialized ? g_animationScalar : 1.0f;
        return clamp_animation_scalar(value);
    }

    extern "C" __declspec(noinline) float __cdecl animation_track_frame_time_scalar(const uintptr_t)
    {
        return animation_time_scalar_base();
    }

    extern "C" __declspec(noinline) float __cdecl animation_track_blend_time_scalar(const uintptr_t)
    {
        return animation_time_scalar_base();
    }

    extern "C" __declspec(naked) void animation_frame_time_scalar_hook()
    {
        __asm
        {
            push esi
            call animation_track_frame_time_scalar
            add esp, 4
            ret
        }
    }

    extern "C" __declspec(naked) void animation_blend_time_scalar_hook()
    {
        __asm
        {
            push esi
            call animation_track_blend_time_scalar
            add esp, 4
            ret
        }
    }

    extern "C" __declspec(noinline) void __fastcall ui_component_update_hook(void* self, void*, int scaled)
    {
        using UiComponentUpdateFn = void(__thiscall*)(void*, int);

        if (g_uiComponentUpdateOriginal == 0)
            return;

        auto* const original = reinterpret_cast<UiComponentUpdateFn>(g_uiComponentUpdateOriginal);
        original(self, scaled);

        if (!g_uiComponentCatchupEnabled || g_uiComponentUpdateInside)
            return;

        uint32_t extra = g_uiComponentExtraPasses;
        if (extra > 4)
            extra = 4;
        if (extra == 0)
            return;

        g_uiComponentUpdateInside = true;
        for (uint32_t pass = 0; pass < extra; ++pass)
            original(self, scaled);
        g_uiComponentUpdateInside = false;
    }

    extern "C" __declspec(noinline) void __fastcall ui_root_update_hook(void* self, void*)
    {
        using UiRootUpdateFn = void(__thiscall*)(void*);

        if (g_uiRootUpdateOriginal == 0)
            return;

        auto* const original = reinterpret_cast<UiRootUpdateFn>(g_uiRootUpdateOriginal);
        original(self);

        if (!g_uiRootCatchupEnabled || g_uiRootUpdateInside)
            return;

        uint32_t extra = g_uiRootExtraPasses;
        if (extra > 4)
            extra = 4;
        if (extra == 0)
            return;

        g_uiRootUpdateInside = true;
        for (uint32_t pass = 0; pass < extra; ++pass)
            original(self);
        g_uiRootUpdateInside = false;
    }

    void reset_native_chat_window_catchup()
    {
        g_nativeChatWindowCarry[0] = 0.0;
        g_nativeChatWindowCarry[1] = 0.0;
        g_nativeChatWindowLastFrame[0] = 0xFFFFFFFF;
        g_nativeChatWindowLastFrame[1] = 0xFFFFFFFF;
    }

    void apply_native_chat_window_scroll_catchup()
    {
        if (!g_nativeChatWindowCatchupEnabled || g_lastFrameDtMs <= 0.001 || g_lastFrameDtMs > 250.0)
            return;

        if (g_nativeChatWinPtr1Location == 0 || g_nativeChatWinPtr2Location == 0)
            find_native_chat_window_pointer_locations(g_nativeChatWinPtr1Location, g_nativeChatWinPtr2Location);

        uintptr_t windows[2]{};
        if (g_nativeChatWinPtr1Location != 0)
            safe_read(g_nativeChatWinPtr1Location, windows[0]);
        if (g_nativeChatWinPtr2Location != 0)
            safe_read(g_nativeChatWinPtr2Location, windows[1]);

        double scalar = g_lastFrameDtMs * 60.0 / 1000.0;
        if (!std::isfinite(scalar) || scalar <= 1.0)
        {
            reset_native_chat_window_catchup();
            return;
        }

        const double maxScalar = static_cast<double>(g_maxScalar > 1.0f ? g_maxScalar : 1.0f);
        if (scalar > maxScalar)
            scalar = maxScalar;

        for (uint32_t index = 0; index < 2; ++index)
        {
            if (windows[index] == 0 || g_nativeChatWindowLastFrame[index] == g_renderFrameSerial)
                continue;

            g_nativeChatWindowLastFrame[index] = g_renderFrameSerial;

            uint32_t countdown = 0;
            if (!safe_read(windows[index] + 0x3C, countdown) || countdown == 0 || countdown > 120)
            {
                g_nativeChatWindowCarry[index] = 0.0;
                continue;
            }

            g_nativeChatWindowCarry[index] += scalar - 1.0;
            uint32_t extra = static_cast<uint32_t>(std::floor(g_nativeChatWindowCarry[index] + 0.000001));
            if (extra == 0)
                continue;

            g_nativeChatWindowCarry[index] -= static_cast<double>(extra);
            if (g_nativeChatWindowCarry[index] < 0.0 || g_nativeChatWindowCarry[index] > maxScalar)
                g_nativeChatWindowCarry[index] = 0.0;

            if (extra > countdown)
                extra = countdown;

            safe_write(windows[index] + 0x3C, countdown - extra);
        }
    }

    extern "C" __declspec(noinline) void __fastcall chat_root_update_hook(void* self, void*)
    {
        using ChatRootUpdateFn = void(__thiscall*)(void*);

        if (g_chatRootUpdateOriginal == 0)
            return;

        auto* const original = reinterpret_cast<ChatRootUpdateFn>(g_chatRootUpdateOriginal);

        original(self);
        apply_native_chat_window_scroll_catchup();

        if (g_chatRootUpdateInside)
            return;

        uint32_t extra = g_chatExtraUpdatePasses;
        if (extra > 4)
            extra = 4;
        if (extra == 0)
            return;

        g_chatRootUpdateInside = true;
        for (uint32_t pass = 0; pass < extra; ++pass)
            original(self);
        g_chatRootUpdateInside = false;
    }

    NativeChatDelayState* get_native_chat_delay_state(void* self)
    {
        if (self == nullptr)
            return nullptr;

        const auto key = reinterpret_cast<uintptr_t>(self);
        NativeChatDelayState* empty = nullptr;

        for (auto& state : g_nativeChatDelayStates)
        {
            if (state.self == key)
                return &state;
            if (state.self == 0 && empty == nullptr)
                empty = &state;
        }

        if (empty == nullptr)
            empty = &g_nativeChatDelayStates[0];

        empty->self = key;
        empty->carry = 0.0;
        empty->lastFrame = 0xFFFFFFFF;
        return empty;
    }

    int32_t calculate_native_chat_scroll_step(void* self)
    {
        auto* const state = get_native_chat_delay_state(self);
        if (state == nullptr || g_lastFrameDtMs <= 0.001 || g_lastFrameDtMs > 250.0)
            return 0x14;

        double scalar = g_lastFrameDtMs * 60.0 / 1000.0;
        if (!std::isfinite(scalar) || scalar < 1.0)
        {
            state->carry = 0.0;
            return 0x14;
        }

        const double maxScalar = static_cast<double>(g_maxScalar > 1.0f ? g_maxScalar : 1.0f);
        if (scalar > maxScalar)
            scalar = maxScalar;

        state->carry += 20.0 * scalar;
        int32_t step = static_cast<int32_t>(std::floor(state->carry + 0.000001));
        if (step < 0x14)
            step = 0x14;

        state->carry -= static_cast<double>(step);
        if (state->carry < 0.0 || state->carry > 80.0)
            state->carry = 0.0;

        if (step > 0x50)
            step = 0x50;
        return step;
    }

    extern "C" __declspec(noinline) int32_t __stdcall native_chat_scroll_step(void* self)
    {
        return calculate_native_chat_scroll_step(self);
    }

    extern "C" __declspec(naked) void native_chat_scroll_hook()
    {
        __asm
        {
            push ecx
            push edx
            push esi
            call native_chat_scroll_step
            pop edx
            pop ecx
            mov edx, dword ptr [esi + 34h]
            add dword ptr [esi + 3Ch], eax
            mov eax, dword ptr [g_nativeChatScrollReturn]
            jmp eax
        }
    }
}

class FrameFix final : public IPlugin
{
public:
    const char* GetName(void) const override
    {
        return "FrameFix";
    }

    const char* GetAuthor(void) const override
    {
        return "rockerudon";
    }

    const char* GetDescription(void) const override
    {
        return "Sets FFXI to 60 FPS and fixes slow-motion frame pacing when rendering drops below target.";
    }

    const char* GetLink(void) const override
    {
        return "";
    }

    double GetVersion(void) const override
    {
        return 1.3;
    }

    uint32_t GetFlags(void) const override
    {
        return static_cast<uint32_t>(Ashita::PluginFlags::UseCommands) |
            static_cast<uint32_t>(Ashita::PluginFlags::UseDirect3D);
    }

    bool Initialize(IAshitaCore* core, ILogManager*, const uint32_t) override
    {
        m_core = core;
        locate();
        enable();
        return true;
    }

    void Release(void) override
    {
        disable();
        restore_set_transform_hook();
    }

    bool Direct3DInitialize(IDirect3DDevice8* device) override
    {
        m_device = device;
        // Do not hook the final view matrix in the normal build. The game's
        // camera collision solver needs to own lateral wall pushes.
        return true;
    }

    void Direct3DBeginScene(bool isRenderingBackBuffer) override
    {
        if (isRenderingBackBuffer)
            pulse_framefix_lock();
    }

    void Direct3DEndScene(bool isRenderingBackBuffer) override
    {
        if (isRenderingBackBuffer)
            pulse_framefix_lock();
    }

    void Direct3DPresent(const RECT*, const RECT*, HWND, const RGNDATA*) override
    {
        LARGE_INTEGER now{};
        ::QueryPerformanceCounter(&now);

        pulse_framefix_lock();

        if (m_frequency.QuadPart == 0)
        {
            ::QueryPerformanceFrequency(&m_frequency);
            m_lastCounter = now;
            return;
        }

        const double dtMs =
            static_cast<double>(now.QuadPart - m_lastCounter.QuadPart) * 1000.0 / static_cast<double>(m_frequency.QuadPart);
        m_lastCounter = now;

        const bool validDt = dtMs > 0.001 && dtMs <= 250.0;
        if (validDt)
        {
            g_lastFrameDtMs = dtMs;
            ++g_renderFrameSerial;
        }

        if (validDt)
        {
            update_animation_scalar_from_frame(dtMs);
            update_chat_catchup(dtMs);
        }

        // UI/menu clocks are deliberately left untouched to avoid cooldown/timer drift.
        if (validDt)
        {
            update_ui_component_catchup(dtMs);
            update_ui_root_catchup(dtMs);
        }
    }

    bool Direct3DSetRenderState(D3DRENDERSTATETYPE, DWORD*) override
    {
        return false;
    }

    bool Direct3DDrawPrimitive(D3DPRIMITIVETYPE, UINT, UINT) override
    {
        return false;
    }

    bool Direct3DDrawIndexedPrimitive(
        D3DPRIMITIVETYPE,
        UINT,
        UINT,
        UINT,
        UINT) override
    {
        return false;
    }

    bool Direct3DDrawPrimitiveUP(
        D3DPRIMITIVETYPE,
        UINT,
        CONST void*,
        UINT) override
    {
        return false;
    }

    bool Direct3DDrawIndexedPrimitiveUP(
        D3DPRIMITIVETYPE,
        UINT,
        UINT,
        UINT,
        CONST void*,
        D3DFORMAT,
        CONST void*,
        UINT) override
    {
        return false;
    }

    bool HandleCommand(int32_t, const char* command, bool) override
    {
        if (command == nullptr)
            return false;

        const char* args = nullptr;
        if (_strnicmp(command, "/framefix", 9) == 0)
            args = command + 9;
        else
            return false;

        while (*args == ' ')
            ++args;

        if (_strnicmp(args, "on", 2) == 0)
        {
            m_autoEnablePending = false;
            if (m_enabled)
            {
                chat("FrameFix is already enabled.");
            }
            else if (enable())
            {
                chat("FrameFix enabled.");
            }
            else
            {
                chat("FrameFix failed to enable. Check that you are on a supported FFXI client.");
            }
            return true;
        }

        if (_strnicmp(args, "off", 3) == 0)
        {
            m_autoEnablePending = false;
            m_lockFps = false;
            if (!m_enabled)
            {
                chat("FrameFix is already disabled.");
            }
            else if (disable())
            {
                chat("FrameFix disabled.");
            }
            else
            {
                chat("FrameFix disabled (some patches could not be reverted cleanly).");
            }
            return true;
        }

        chat("FrameFix usage: /framefix on | off");
        return true;

    }

private:
    bool locate()
    {
        if (!find_fps_control())
            return false;

        if (g_gameMenuCurrentLocation == 0)
            find_game_menu_current_location(g_gameMenuCurrentLocation);

        if (g_addonStylePointerLocation == 0)
            find_addon_style_fps_pointer(g_addonStyleSignatureAddress, g_addonStylePointerLocation);

        return true;
    }

    bool enable()
    {
        if (m_enabled)
            return true;

        if (!locate())
            return false;

        m_desiredDivisor = 1;
        m_lockFps = true;
        set_fps_divisor(1);
        m_forceFpsFrames = 600;
        g_cameraFixEnabled = false;
        g_cameraInitialized = false;
        g_cameraAdjustedFrames = 0;
        g_cameraViewCalls = 0;
        g_cameraAcceptedViews = 0;
        g_cameraRejectedViews = 0;
        g_cameraHugeRejects = 0;
        g_cameraLastDistance = 0.0f;
        g_cameraMaxDistance = 0.0f;
        g_cameraStableFrames = 0;
        g_cameraFrameSerial = 0xFFFFFFFF;
        g_cameraFrameNearestDistance = 9999.0f;
        g_cameraFrameHasNearest = false;
        g_cameraLastFrameNearestDistance = 0.0f;
        g_cameraHasLastFrameNearest = false;
        g_cameraClampMissFrames = 0;
        g_cameraArmInitialized = false;
        g_cameraArmDistance = 0.0f;
        g_cameraArmDirection = {};
        g_cameraArmHoldFrames = 0;
        restore_set_transform_hook();
        restore_camera_collision_substeps();

        reset_animation_scalar_state();

        if (!patch_animation_frame_tick())
            chat("FrameFix warning: motion frame hook was not found.");

        if (!patch_animation_blend_tick())
            chat("FrameFix warning: animation blend hook was not found.");

        if (!patch_ui_root_update())
            chat("FrameFix warning: UI root update hook was not found.");

        if (!patch_camera_collision_substeps())
            chat("FrameFix warning: camera collision substep patch was not found.");

        if (!patch_camera_jitter_fix())
            chat("FrameFix warning: camera jitter patch was not found.");

        m_chatCatchupEnabled = true;
        g_nativeChatWindowCatchupEnabled = true;
        reset_native_chat_window_catchup();
        if (g_nativeChatWinPtr1Location == 0 || g_nativeChatWinPtr2Location == 0)
            find_native_chat_window_pointer_locations(g_nativeChatWinPtr1Location, g_nativeChatWinPtr2Location);
        g_chatTickHookEnabled = false;
        g_chatTickStep = 1;
        m_chatCatchupCarry = 0.0;
        if (!patch_chat_root_update())
            chat("FrameFix warning: chat root update hook was not found.");

        m_enabled = true;
        return true;
    }

    bool disable()
    {
        bool ok = true;
        ok = restore_animation_frame_tick() && ok;
        ok = restore_animation_blend_tick() && ok;

        ok = restore_chat_root_update() && ok;
        ok = restore_native_chat_delay_catchup() && ok;
        ok = restore_ui_component_update() && ok;
        ok = restore_ui_root_update() && ok;
        ok = restore_ui_root_smoothing() && ok;
        g_uiTickHookEnabled = false;
        g_menuExtraMethodPasses = 0;
        g_chatExtraUpdatePasses = 0;
        m_chatCatchupEnabled = false;
        g_nativeChatWindowCatchupEnabled = false;
        reset_native_chat_window_catchup();
        g_chatTickHookEnabled = false;
        g_chatTickStep = 1;
        m_chatCatchupCarry = 0.0;
        g_cameraFixEnabled = false;
        g_cameraArmInitialized = false;
        g_cameraArmDirection = {};
        restore_set_transform_hook();
        ok = restore_camera_collision_substeps() && ok;
        ok = restore_camera_jitter_fix() && ok;
        m_lockFps = false;
        m_desiredDivisor = 2;
        set_fps_divisor(2);
        m_enabled = false;
        return ok;
    }

    bool patch_animation_frame_tick()
    {
        if (m_animationFrameTickCallSite.patched)
            return true;

        const uintptr_t callsite = find_animation_frame_tick_callsite();
        if (callsite == 0)
            return false;

        m_animationFrameTickCallSite.address = callsite;
        return write_call(m_animationFrameTickCallSite, reinterpret_cast<void*>(&animation_frame_time_scalar_hook));
    }

    bool restore_animation_frame_tick()
    {
        return restore_site(m_animationFrameTickCallSite);
    }

    bool patch_animation_blend_tick()
    {
        if (m_animationBlendTickCallSite.patched)
            return true;

        const uintptr_t callsite = find_animation_blend_tick_callsite();
        if (callsite == 0)
            return false;

        m_animationBlendTickCallSite.address = callsite;
        return write_call(m_animationBlendTickCallSite, reinterpret_cast<void*>(&animation_blend_time_scalar_hook));
    }

    bool restore_animation_blend_tick()
    {
        return restore_site(m_animationBlendTickCallSite);
    }

    void pulse_framefix_lock()
    {
        if (m_autoEnablePending && enable())
        {
            m_autoEnablePending = false;
            chat("FrameFix auto-enabled.");
        }

        enforce_fps_lock();

        if (m_chatCatchupEnabled)
            patch_chat_root_update();
    }

    void update_ui_component_catchup(const double dtMs)
    {
        if (!m_enabled || !g_uiComponentCatchupEnabled || dtMs <= 0.001 || dtMs > 250.0)
        {
            g_uiComponentExtraPasses = 0;
            return;
        }

        g_uiComponentTickCarry += (dtMs * 60.0) / 1000.0;
        auto ticks = static_cast<uint32_t>(std::floor(g_uiComponentTickCarry + 0.000001));
        if (ticks == 0)
        {
            g_uiComponentExtraPasses = 0;
            return;
        }

        g_uiComponentTickCarry -= static_cast<double>(ticks);
        if (g_uiComponentTickCarry < 0.0)
            g_uiComponentTickCarry = 0.0;

        uint32_t extra = ticks > 1 ? ticks - 1 : 0;
        if (extra > 4)
            extra = 4;

        g_uiComponentExtraPasses = extra;
    }

    void update_ui_root_catchup(const double dtMs)
    {
        if (!m_enabled || !g_uiRootCatchupEnabled || dtMs <= 0.001 || dtMs > 250.0)
        {
            g_uiRootExtraPasses = 0;
            return;
        }

        g_uiRootTickCarry += (dtMs * 60.0) / 1000.0;
        auto ticks = static_cast<uint32_t>(std::floor(g_uiRootTickCarry + 0.000001));
        if (ticks == 0)
        {
            g_uiRootExtraPasses = 0;
            return;
        }

        g_uiRootTickCarry -= static_cast<double>(ticks);
        if (g_uiRootTickCarry < 0.0)
            g_uiRootTickCarry = 0.0;

        uint32_t extra = ticks > 1 ? ticks - 1 : 0;
        if (extra > 4)
            extra = 4;

        g_uiRootExtraPasses = extra;
    }

    bool patch_ui_root_update()
    {
        if (m_uiRootUpdateSite.patched)
            return true;

        uintptr_t base = 0;
        uint32_t size = 0;
        if (!get_module_range("FFXiMain.dll", base, size) && !get_module_range("ffximain.dll", base, size))
            return false;

        constexpr uint32_t kUpdateRva = 0x117120;
        constexpr uint32_t kCallRva = 0x1173D9;
        if (kUpdateRva + 5 > size || kCallRva + 5 > size)
            return false;

        const uintptr_t original = base + kUpdateRva;
        const uintptr_t callsite = base + kCallRva;

        uint8_t opcode = 0;
        int32_t relative = 0;
        if (!safe_read(callsite, opcode) || opcode != 0xE8 || !safe_read(callsite + 1, relative))
            return false;

        const uintptr_t target = callsite + 5 + static_cast<intptr_t>(relative);
        if (target != original)
            return false;

        m_uiRootUpdateSite.address = callsite;
        if (!write_call(m_uiRootUpdateSite, reinterpret_cast<void*>(&ui_root_update_hook)))
            return false;

        g_uiRootUpdateOriginal = original;
        g_uiRootTickCarry = 0.0;
        g_uiRootExtraPasses = 0;
        g_uiRootCatchupEnabled = true;
        return true;
    }

    bool restore_ui_root_update()
    {
        const bool ok = restore_site(m_uiRootUpdateSite);
        g_uiRootCatchupEnabled = false;
        g_uiRootUpdateInside = false;
        g_uiRootExtraPasses = 0;
        g_uiRootTickCarry = 0.0;
        g_uiRootUpdateOriginal = 0;
        return ok;
    }

    bool patch_ui_component_update()
    {
        if (m_uiComponentUpdateSite.patched)
            return true;

        uintptr_t base = 0;
        uint32_t size = 0;
        if (!get_module_range("FFXiMain.dll", base, size) && !get_module_range("ffximain.dll", base, size))
            return false;

        constexpr uint32_t kUpdateRva = 0x119800;
        constexpr uint32_t kMainCallRva = 0x1171AD;
        if (kUpdateRva + 5 > size || kMainCallRva + 5 > size)
            return false;

        const uintptr_t original = base + kUpdateRva;
        const uintptr_t callsite = base + kMainCallRva;

        uint8_t opcode = 0;
        int32_t relative = 0;
        if (!safe_read(callsite, opcode) || opcode != 0xE8 || !safe_read(callsite + 1, relative))
            return false;

        const uintptr_t target = callsite + 5 + static_cast<intptr_t>(relative);
        if (target != original)
            return false;

        m_uiComponentUpdateSite.address = callsite;
        if (!write_call(m_uiComponentUpdateSite, reinterpret_cast<void*>(&ui_component_update_hook)))
            return false;

        g_uiComponentUpdateOriginal = original;
        g_uiComponentTickCarry = 0.0;
        g_uiComponentExtraPasses = 0;
        g_uiComponentCatchupEnabled = true;
        return true;
    }

    bool restore_ui_component_update()
    {
        const bool ok = restore_site(m_uiComponentUpdateSite);
        g_uiComponentCatchupEnabled = false;
        g_uiComponentUpdateInside = false;
        g_uiComponentExtraPasses = 0;
        g_uiComponentTickCarry = 0.0;
        g_uiComponentUpdateOriginal = 0;
        return ok;
    }

    bool patch_ui_root_smoothing()
    {
        uintptr_t base = 0;
        uint32_t size = 0;
        if (!get_module_range("FFXiMain.dll", base, size) && !get_module_range("ffximain.dll", base, size))
            return false;

        struct RootSmoothPatch
        {
            uint32_t rva;
            const float* value;
            uint8_t opcode;
        };

        static const RootSmoothPatch patches[] = {
            {0x11720B, &g_uiRootSmoothFactor, 0xD8},
            {0x117211, &g_uiRootMaxStep, 0xD8},
            {0x11721E, &g_uiRootMinStep, 0xD8},
            {0x117236, &g_uiRootMinStep, 0xD9},
            {0x11723E, &g_uiRootMaxStep, 0xD9},
            {0x11726A, &g_uiRootSmoothFactor, 0xD8},
            {0x117270, &g_uiRootMaxStep, 0xD8},
            {0x11727D, &g_uiRootMinStep, 0xD8},
            {0x117295, &g_uiRootMinStep, 0xD9},
            {0x11729D, &g_uiRootMaxStep, 0xD9},
        };

        bool ok = true;
        for (uint32_t index = 0; index < sizeof(patches) / sizeof(patches[0]); ++index)
        {
            if (m_uiRootSmoothSites[index].patched)
                continue;

            const auto& patch = patches[index];
            if (patch.rva + 6 > size)
            {
                ok = false;
                continue;
            }

            const uintptr_t address = base + patch.rva;
            uint8_t current[2] = {};
            if (!safe_read(address, current[0]) || !safe_read(address + 1, current[1]) || current[0] != patch.opcode ||
                current[1] != 0x0D)
            {
                ok = false;
                continue;
            }

            uint8_t bytes[6] = {patch.opcode, 0x0D, 0, 0, 0, 0};
            *reinterpret_cast<uintptr_t*>(bytes + 2) = reinterpret_cast<uintptr_t>(patch.value);
            ok = write_bytes_patch(m_uiRootSmoothSites[index], address, bytes, sizeof(bytes)) && ok;
        }

        return ok;
    }

    bool restore_ui_root_smoothing()
    {
        bool ok = true;
        for (auto& site : m_uiRootSmoothSites)
            ok = restore_bytes(site) && ok;

        g_uiRootSmoothFactor = 0.555555582f;
        g_uiRootMaxStep = 1.0f;
        g_uiRootMinStep = -1.0f;
        return ok;
    }

    bool patch_chat_root_update()
    {
        if (m_chatRootUpdateCallSite.patched)
            return true;

        uintptr_t original = 0;
        const auto callsite = find_chat_root_update_callsite(original);
        if (callsite == 0 || original == 0)
            return false;

        g_chatRootUpdateOriginal = original;
        m_chatRootUpdateCallSite.address = callsite;
        return write_call(m_chatRootUpdateCallSite, reinterpret_cast<void*>(&chat_root_update_hook));
    }

    bool restore_chat_root_update()
    {
        const bool ok = restore_site(m_chatRootUpdateCallSite);
        g_chatRootUpdateOriginal = 0;
        g_chatRootUpdateInside = false;
        return ok;
    }

    bool patch_native_chat_delay_catchup()
    {
        if (m_nativeChatScrollSite.patched)
            return true;

        const auto address = find_native_chat_scroll_patch_site();
        if (address == 0)
            return false;

        g_nativeChatScrollReturn = address + 12;
        for (auto& state : g_nativeChatDelayStates)
            state = {};

        return write_jump_with_nops(m_nativeChatScrollSite, address, 12, reinterpret_cast<void*>(&native_chat_scroll_hook));
    }

    bool restore_native_chat_delay_catchup()
    {
        const bool ok = restore_bytes(m_nativeChatScrollSite);
        g_nativeChatScrollReturn = 0;
        for (auto& state : g_nativeChatDelayStates)
            state = {};

        return ok;
    }

    void update_chat_catchup(const double dtMs)
    {
        if (!m_enabled || !m_chatCatchupEnabled || dtMs <= 0.001 || dtMs > 250.0)
        {
            g_chatExtraUpdatePasses = 0;
            g_chatTickStep = 1;
            return;
        }

        m_chatCatchupCarry += (dtMs * 60.0) / 1000.0;
        auto ticks = static_cast<uint32_t>(std::floor(m_chatCatchupCarry + 0.000001));
        if (ticks == 0)
        {
            g_chatExtraUpdatePasses = 0;
            g_chatTickStep = 1;
            return;
        }

        m_chatCatchupCarry -= static_cast<double>(ticks);
        if (m_chatCatchupCarry < 0.0)
            m_chatCatchupCarry = 0.0;

        if (ticks < 1)
            ticks = 1;
        if (ticks > 4)
            ticks = 4;

        uint32_t extra = ticks > 1 ? ticks - 1 : 0;
        if (extra > 4)
            extra = 4;

        g_chatTickStep = 1;
        g_chatExtraUpdatePasses = extra;
    }

    bool set_fps_divisor(const int32_t divisor)
    {
        bool wrote = false;
        bool verified = false;

        auto write_state_divisor = [&](const uintptr_t state) {
            if (state == 0)
                return;

            wrote = safe_write(state + 0x30, divisor) || wrote;
            int32_t current = 0;
            if (safe_read(state + 0x30, current) && current == divisor)
                verified = true;
        };

        write_state_divisor(get_fps_state_pointer());

        if (g_addonStylePointerLocation == 0)
            find_addon_style_fps_pointer(g_addonStyleSignatureAddress, g_addonStylePointerLocation);

        uintptr_t addonState = 0;
        if (g_addonStylePointerLocation != 0 && safe_read(g_addonStylePointerLocation, addonState))
            write_state_divisor(addonState);

        if (g_divisorCacheLocation != 0)
            wrote = safe_write(g_divisorCacheLocation, divisor) || wrote;

        return wrote && verified;
    }

    uintptr_t get_fps_state_pointer()
    {
        if (g_statePointerLocation == 0)
            locate();

        uintptr_t state = 0;
        if (g_statePointerLocation != 0 && safe_read(g_statePointerLocation, state) && state != 0)
            return state;

        uintptr_t signature = 0;
        uintptr_t pointerLocation = 0;
        if (find_addon_style_fps_pointer(signature, pointerLocation))
        {
            g_addonStyleSignatureAddress = signature;
            g_addonStylePointerLocation = pointerLocation;
            g_statePointerLocation = pointerLocation;
            state = 0;
            if (safe_read(pointerLocation, state) && state != 0)
                return state;
        }

        return 0;
    }

    void enforce_fps_lock()
    {
        if (!m_lockFps && m_forceFpsFrames == 0)
            return;

        set_fps_divisor(m_desiredDivisor);

        if (m_forceFpsFrames != 0)
            --m_forceFpsFrames;
    }

    void chat(const char* format, ...)
    {
        if (m_core == nullptr || m_core->GetChatManager() == nullptr)
            return;

        char buffer[1024] = {};
        va_list args;
        va_start(args, format);
        vsnprintf_s(buffer, sizeof(buffer), _TRUNCATE, format, args);
        va_end(args);

        m_core->GetChatManager()->Write(1, false, buffer);
    }

private:
    IAshitaCore* m_core = nullptr;
    IDirect3DDevice8* m_device = nullptr;
    PatchSite m_animationFrameTickCallSite;
    PatchSite m_animationBlendTickCallSite;
    PatchSite m_chatRootUpdateCallSite;
    BytesPatchSite m_nativeChatScrollSite;
    PatchSite m_uiComponentUpdateSite;
    PatchSite m_uiRootUpdateSite;
    BytesPatchSite m_uiRootSmoothSites[10];
    bool m_enabled = false;
    bool m_autoEnablePending = false;

    LARGE_INTEGER m_frequency{};
    LARGE_INTEGER m_lastCounter{};
    bool m_chatCatchupEnabled = false;
    double m_chatCatchupCarry = 0.0;
    bool m_lockFps = false;
    int32_t m_desiredDivisor = 2;
    uint32_t m_forceFpsFrames = 0;
};

extern "C" __declspec(dllexport) IPlugin* __stdcall expCreatePlugin(const char*)
{
    return new FrameFix();
}

extern "C" __declspec(dllexport) void __stdcall expDestroyPlugin(void* instance)
{
    delete static_cast<FrameFix*>(instance);
}

extern "C" __declspec(dllexport) double __stdcall expGetInterfaceVersion(void)
{
    return ASHITA_INTERFACE_VERSION;
}
