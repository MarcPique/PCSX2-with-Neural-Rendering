// SPDX-License-Identifier: GPL-3.0-or-later
// Real Vulkan WSI, hidden window and no audio. Exercises OFF/ON/OFF/ON/OFF
// in one process. Synthetic frames only: not a game compatibility test.
#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>
#include "neural_rendering_config.h"
#include <QCoreApplication>
#include <QFile>
#include <QTextStream>
#include <vector>
#include <thread>
#include <chrono>
#include <stdexcept>

static void require(VkResult result, const char* operation)
{
	if (result != VK_SUCCESS)
		throw std::runtime_error(std::string(operation) + " status=" + std::to_string(result));
}
static LRESULT CALLBACK WindowProc(HWND w, UINT m, WPARAM a, LPARAM b) { return DefWindowProcW(w, m, a, b); }
static void cycle(HWND window, bool enabled, int ordinal)
{
	QString error;
	QFile::remove(neural_rendering::root_path() + "/ReShade.log");
	if (enabled)
	{
		auto config = neural_rendering::default_config();
		neural_rendering::set_ini_value(config, "RenoDX.DLSS5", "NRIntensity", ordinal == 1 ? "0.6" : "1");
		neural_rendering::set_ini_value(config, "RenoDX.DLSS5", "NRStyle", ordinal == 1 ? "1" : "2");
		if (!neural_rendering::write_text(neural_rendering::config_path(), config, &error))
			throw std::runtime_error(error.toStdString());
	}
	if (!neural_rendering::save_enabled(enabled, &error))
		throw std::runtime_error(error.toStdString());
	QTextStream(stdout) << "Cycle " << ordinal << " enabled=" << enabled << " " << neural_rendering::initialize() << Qt::endl;
	if (!neural_rendering::prepared())
		throw std::runtime_error("environment not prepared");
	VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
	app.pApplicationName = "PCSX2 Neural cycle test";
	app.apiVersion = VK_API_VERSION_1_3;
	const char* instance_extensions[] = {"VK_KHR_surface", "VK_KHR_win32_surface"};
	VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
	ci.pApplicationInfo = &app;
	ci.enabledExtensionCount = 2;
	ci.ppEnabledExtensionNames = instance_extensions;
	VkInstance instance{};
	require(vkCreateInstance(&ci, nullptr, &instance), "instance");
	const bool guard_loaded = GetModuleHandleW(L"pcsx2-settings-only.addon64") != nullptr;
	if (guard_loaded != enabled)
		throw std::runtime_error("guard module state does not match master switch");
	VkWin32SurfaceCreateInfoKHR sci{VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR};
	sci.hinstance = GetModuleHandleW(nullptr);
	sci.hwnd = window;
	VkSurfaceKHR surface{};
	require(vkCreateWin32SurfaceKHR(instance, &sci, nullptr, &surface), "surface");
	uint32_t count = 0;
	require(vkEnumeratePhysicalDevices(instance, &count, nullptr), "GPU count");
	std::vector<VkPhysicalDevice> devices(count);
	require(vkEnumeratePhysicalDevices(instance, &count, devices.data()), "GPUs");
	VkPhysicalDevice gpu{};
	for (auto d : devices)
	{
		VkPhysicalDeviceProperties p{};
		vkGetPhysicalDeviceProperties(d, &p);
		if (p.vendorID == 0x10de)
			gpu = d;
	}
	if (!gpu)
		throw std::runtime_error("NVIDIA GPU not found");
	vkGetPhysicalDeviceQueueFamilyProperties(gpu, &count, nullptr);
	std::vector<VkQueueFamilyProperties> families(count);
	vkGetPhysicalDeviceQueueFamilyProperties(gpu, &count, families.data());
	uint32_t family = UINT32_MAX;
	for (uint32_t i = 0; i < count; ++i)
	{
		VkBool32 supported{};
		require(vkGetPhysicalDeviceSurfaceSupportKHR(gpu, i, surface, &supported), "present support");
		if (supported && (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT))
		{
			family = i;
			break;
		}
	}
	if (family == UINT32_MAX)
		throw std::runtime_error("no graphics/present queue");
	float priority = 1;
	VkDeviceQueueCreateInfo qi{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
	qi.queueFamilyIndex = family;
	qi.queueCount = 1;
	qi.pQueuePriorities = &priority;
	const char* extensions[] = {"VK_KHR_swapchain", "VK_KHR_external_memory_win32", "VK_KHR_external_semaphore_win32", "VK_KHR_timeline_semaphore"};
	VkPhysicalDeviceTimelineSemaphoreFeatures timeline{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES};
	timeline.timelineSemaphore = VK_TRUE;
	VkDeviceCreateInfo di{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
	di.pNext = &timeline;
	di.queueCreateInfoCount = 1;
	di.pQueueCreateInfos = &qi;
	di.enabledExtensionCount = 4;
	di.ppEnabledExtensionNames = extensions;
	VkDevice device{};
	require(vkCreateDevice(gpu, &di, nullptr, &device), "device");
	VkQueue queue{};
	vkGetDeviceQueue(device, family, 0, &queue);
	VkSurfaceCapabilitiesKHR caps{};
	require(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(gpu, surface, &caps), "surface capabilities");
	require(vkGetPhysicalDeviceSurfaceFormatsKHR(gpu, surface, &count, nullptr), "format count");
	std::vector<VkSurfaceFormatKHR> formats(count);
	require(vkGetPhysicalDeviceSurfaceFormatsKHR(gpu, surface, &count, formats.data()), "formats");
	auto format = formats.front();
	for (auto f : formats)
		if (f.format == VK_FORMAT_B8G8R8A8_UNORM)
		{
			format = f;
			break;
		}
	VkSwapchainCreateInfoKHR si{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
	si.surface = surface;
	si.minImageCount = caps.minImageCount + 1;
	if (caps.maxImageCount && si.minImageCount > caps.maxImageCount)
		si.minImageCount = caps.maxImageCount;
	si.imageFormat = format.format;
	si.imageColorSpace = format.colorSpace;
	si.imageExtent = caps.currentExtent;
	si.imageArrayLayers = 1;
	si.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	si.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
	si.preTransform = caps.currentTransform;
	si.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	si.presentMode = VK_PRESENT_MODE_FIFO_KHR;
	si.clipped = VK_TRUE;
	VkSwapchainKHR swapchain{};
	require(vkCreateSwapchainKHR(device, &si, nullptr, &swapchain), "swapchain");
	require(vkGetSwapchainImagesKHR(device, swapchain, &count, nullptr), "image count");
	std::vector<VkImage> images(count);
	require(vkGetSwapchainImagesKHR(device, swapchain, &count, images.data()), "images");
	VkCommandPoolCreateInfo pi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
	pi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	pi.queueFamilyIndex = family;
	VkCommandPool pool{};
	require(vkCreateCommandPool(device, &pi, nullptr, &pool), "command pool");
	VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
	ai.commandPool = pool;
	ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	ai.commandBufferCount = 1;
	VkCommandBuffer cmd{};
	require(vkAllocateCommandBuffers(device, &ai, &cmd), "command buffer");
	VkSemaphoreCreateInfo semi{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
	VkSemaphore acquired{}, rendered{};
	require(vkCreateSemaphore(device, &semi, nullptr, &acquired), "acquired semaphore");
	require(vkCreateSemaphore(device, &semi, nullptr, &rendered), "rendered semaphore");
	for (int frame = 0; frame < (enabled ? 240 : 30); ++frame)
	{
		MSG msg;
		while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessageW(&msg);
		}
		uint32_t index{};
		require(vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, acquired, VK_NULL_HANDLE, &index), "acquire");
		require(vkResetCommandBuffer(cmd, 0), "reset commands");
		VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
		require(vkBeginCommandBuffer(cmd, &bi), "begin commands");
		VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
		barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.image = images[index];
		barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
		barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
		VkClearColorValue color{{0.15f + 0.1f * (frame % 2), 0.3f, 0.5f, 1.0f}};
		vkCmdClearColorImage(cmd, images[index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &color, 1, &barrier.subresourceRange);
		barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		barrier.dstAccessMask = 0;
		vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
		require(vkEndCommandBuffer(cmd), "end commands");
		VkPipelineStageFlags stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
		VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
		submit.waitSemaphoreCount = 1;
		submit.pWaitSemaphores = &acquired;
		submit.pWaitDstStageMask = &stage;
		submit.commandBufferCount = 1;
		submit.pCommandBuffers = &cmd;
		submit.signalSemaphoreCount = 1;
		submit.pSignalSemaphores = &rendered;
		require(vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE), "submit");
		VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
		present.waitSemaphoreCount = 1;
		present.pWaitSemaphores = &rendered;
		present.swapchainCount = 1;
		present.pSwapchains = &swapchain;
		present.pImageIndices = &index;
		require(vkQueuePresentKHR(queue, &present), "present");
		require(vkDeviceWaitIdle(device), "wait idle");
		std::this_thread::sleep_for(std::chrono::milliseconds(16));
	}
	vkDestroySemaphore(device, rendered, nullptr);
	vkDestroySemaphore(device, acquired, nullptr);
	vkDestroyCommandPool(device, pool, nullptr);
	vkDestroySwapchainKHR(device, swapchain, nullptr);
	vkDestroyDevice(device, nullptr);
	vkDestroySurfaceKHR(instance, surface, nullptr);
	vkDestroyInstance(instance, nullptr);
	if (GetModuleHandleW(L"pcsx2-settings-only.addon64"))
		throw std::runtime_error("overlay guard retained after Vulkan shutdown");
	if (enabled)
	{
		const auto log = neural_rendering::read_text(neural_rendering::root_path() + "/ReShade.log");
		if (!log.contains("inline feature 18 evaluation succeeded"))
			throw std::runtime_error("no successful neural evaluation");
		const auto expected = ordinal == 1 ? "intensity=0.600000" : "intensity=1.000000";
		if (!log.contains(expected) || !log.contains(ordinal == 1 ? "style=1" : "style=2"))
			throw std::runtime_error("changed neural settings were not reloaded");
	}
	QFile::copy(neural_rendering::root_path() + "/ReShade.log", neural_rendering::root_path() + QString("/cycle-%1-%2-ReShade.log").arg(GetCurrentProcessId()).arg(ordinal));
	QTextStream(stdout) << "PASS cycle " << ordinal << " enabled=" << enabled << Qt::endl;
}
int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	if (!app.arguments().contains("--disposable-runtime"))
		return 2;
	WNDCLASSW wc{};
	wc.lpfnWndProc = WindowProc;
	wc.hInstance = GetModuleHandleW(nullptr);
	wc.lpszClassName = L"PCSX2NeuralHiddenTest";
	RegisterClassW(&wc);
	HWND window = CreateWindowW(wc.lpszClassName, L"Hidden neural test", WS_OVERLAPPEDWINDOW, 0, 0, 640, 480, nullptr, nullptr, wc.hInstance, nullptr);
	if (!window)
		return 3;
	try
	{
		int n = 0;
		for (bool enabled : {false, true, false, true, false})
			cycle(window, enabled, n++);
	}
	catch (const std::exception& e)
	{
		QTextStream(stderr) << "FAIL " << e.what() << Qt::endl;
		DestroyWindow(window);
		return 1;
	}
	DestroyWindow(window);
	QTextStream(stdout) << "PASS all cycles in PID " << GetCurrentProcessId() << Qt::endl;
	return 0;
}
