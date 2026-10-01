// SPDX-License-Identifier: GPL-2.0-only
#include <reshade.hpp>
#include <atomic>

extern "C" __declspec(dllexport) const char* NAME = "PCSX2 Settings Only";
extern "C" __declspec(dllexport) const char* DESCRIPTION = "Configure effects in PCSX2 settings. In-game menus are disabled.";

static bool on_overlay(reshade::api::effect_runtime*, bool open, reshade::api::input_source)
{
	// Covers keyboard, gamepad and API requests. Closing is always allowed.
	static std::atomic_bool logged = false;
	if (open && !logged.exchange(true))
		reshade::log::message(reshade::log::level::info, "PCSX2: blocked in-game ReShade menu; use PCSX2 settings.");
	return open;
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
	if (reason == DLL_PROCESS_ATTACH)
	{
		if (!reshade::register_addon(module))
			return FALSE;
		reshade::register_event<reshade::addon_event::reshade_open_overlay>(on_overlay);
		reshade::log::message(reshade::log::level::info, "PCSX2: settings-only overlay guard active (keyboard/gamepad/API).");
	}
	else if (reason == DLL_PROCESS_DETACH)
	{
		reshade::unregister_event<reshade::addon_event::reshade_open_overlay>(on_overlay);
		reshade::unregister_addon(module);
	}
	return TRUE;
}
