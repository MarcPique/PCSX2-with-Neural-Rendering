// Load the real DLL through the public ReShade ABI and exercise its registered callback.
#include <reshade.hpp>
#include <cstdio>
#include <cstring>

static void* registered_callback = nullptr;
static bool registered = false;
static int logs = 0;
extern "C" __declspec(dllexport) bool ReShadeRegisterAddon(void*, uint32_t version)
{
	return registered = version == RESHADE_API_VERSION;
}
extern "C" __declspec(dllexport) void ReShadeUnregisterAddon(void*) { registered = false; }
extern "C" __declspec(dllexport) void ReShadeRegisterEvent(reshade::addon_event event, void* callback)
{
	if (event == reshade::addon_event::reshade_open_overlay)
		registered_callback = callback;
}
extern "C" __declspec(dllexport) void ReShadeUnregisterEvent(reshade::addon_event event, void*)
{
	if (event == reshade::addon_event::reshade_open_overlay)
		registered_callback = nullptr;
}
extern "C" __declspec(dllexport) void ReShadeLogMessage(void*, int, const char*) { ++logs; }
int main(int argc, char** argv)
{
	if (argc != 2)
		return 1;
	HMODULE guard = LoadLibraryA(argv[1]);
	if (!guard || !registered || !registered_callback)
		return 2;
	auto callback = reinterpret_cast<bool (*)(reshade::api::effect_runtime*, bool, reshade::api::input_source)>(registered_callback);
	for (int source = 0; source <= static_cast<int>(reshade::api::input_source::clipboard); ++source)
		if (!callback(nullptr, true, static_cast<reshade::api::input_source>(source)) ||
			callback(nullptr, false, static_cast<reshade::api::input_source>(source)))
			return 3;
	if (logs != 2)
		return 4; // Loaded + first blocked attempt, no per-frame log spam.
	FreeLibrary(guard);
	if (registered || registered_callback)
		return 5;
	std::puts("PASS: actual addon loads, vetoes opens from every input source, permits closing, unloads cleanly.");
}
