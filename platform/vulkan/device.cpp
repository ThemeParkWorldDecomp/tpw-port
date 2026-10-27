// device.cpp: the Vulkan renderer's device, swapchain, frame and resources (renderer_internal.h; the drawing is draw.cpp).
#include "renderer_internal.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "renderer.h"

namespace tpw::vkr {

Renderer gRenderer;

Renderer* Current() { return gRenderer.device != VK_NULL_HANDLE ? &gRenderer : nullptr; }

namespace {

constexpr VkFormat kColourFormat = VK_FORMAT_R8G8B8A8_UNORM;
constexpr VkDeviceSize kRingChunk = 8u << 20;   // the frame's memory grows by chunks of this size (or the request's)
constexpr uint32_t kDescriptorPoolSets = 1024;

#include "shaders/tpw_frag.inc"
#include "shaders/tpw_vert.inc"

// ---- The function table

template <typename F> F Proc(PFN_vkGetInstanceProcAddr get, VkInstance instance, const char* name) {
    return reinterpret_cast<F>(get(instance, name));
}

}  // namespace

bool Api::LoadGlobal(PFN_vkGetInstanceProcAddr get) {
    vkGetInstanceProcAddr = get;
    bool ok = get != nullptr;
#define TPW_VK_LOAD(name) \
    name = Proc<PFN_##name>(get, VK_NULL_HANDLE, #name); \
    if (name == nullptr && std::strcmp(#name, "vkEnumerateInstanceVersion") != 0) ok = false;
    TPW_VK_GLOBAL_CALLS(TPW_VK_LOAD)
#undef TPW_VK_LOAD
    return ok;
}

bool Api::LoadInstance(VkInstance instance) {
    bool ok = true;
#define TPW_VK_LOAD(name) \
    name = Proc<PFN_##name>(vkGetInstanceProcAddr, instance, #name); \
    if (name == nullptr) ok = false;
    TPW_VK_INSTANCE_CALLS(TPW_VK_LOAD)
#undef TPW_VK_LOAD
    return ok;
}

bool Api::LoadDevice(VkDevice device) {
    bool ok = true;
#define TPW_VK_LOAD(name) \
    name = reinterpret_cast<PFN_##name>(vkGetDeviceProcAddr(device, #name)); \
    if (name == nullptr) ok = false;
    TPW_VK_DEVICE_CALLS(TPW_VK_LOAD)
#undef TPW_VK_LOAD
    return ok;
}

namespace {

Renderer& R() { return gRenderer; }

const char* ResultName(VkResult result) {
    switch (result) {
        case VK_ERROR_OUT_OF_HOST_MEMORY: return "out of host memory";
        case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "out of device memory";
        case VK_ERROR_DEVICE_LOST: return "the device was lost";
        case VK_ERROR_INITIALIZATION_FAILED: return "initialisation failed";
        case VK_ERROR_EXTENSION_NOT_PRESENT: return "an extension is missing";
        case VK_ERROR_FEATURE_NOT_PRESENT: return "a feature is missing";
        case VK_ERROR_INCOMPATIBLE_DRIVER: return "no compatible driver";
        case VK_ERROR_SURFACE_LOST_KHR: return "the surface was lost";
        case VK_ERROR_OUT_OF_DATE_KHR: return "the swapchain is out of date";
        case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR: return "the window is in use";
        case VK_TIMEOUT: return "a timeout";
        default: return "an error";
    }
}

// ---- Memory and resources

uint32_t MemoryType(uint32_t typeBits, VkMemoryPropertyFlags wanted, VkMemoryPropertyFlags fallback) {
    const auto& types = R().memoryProperties;
    for (int pass = 0; pass < 2; ++pass) {
        const VkMemoryPropertyFlags flags = pass == 0 ? wanted : fallback;
        for (uint32_t i = 0; i < types.memoryTypeCount; ++i) {
            if ((typeBits & (1u << i)) != 0 && (types.memoryTypes[i].propertyFlags & flags) == flags) return i;
        }
    }
    return UINT32_MAX;
}

VkPipelineStageFlags StageOf(VkImageLayout layout) {
    switch (layout) {
        case VK_IMAGE_LAYOUT_UNDEFINED: return VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL: return VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
            return VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL: return VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
        case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL: return VK_PIPELINE_STAGE_TRANSFER_BIT;
        case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR: return VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        default: return VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    }
}

VkAccessFlags AccessOf(VkImageLayout layout) {
    switch (layout) {
        case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
            return VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
            return VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL: return VK_ACCESS_SHADER_READ_BIT;
        case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL: return VK_ACCESS_TRANSFER_READ_BIT;
        case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL: return VK_ACCESS_TRANSFER_WRITE_BIT;
        default: return 0;
    }
}

// A layout change of a whole image (every level), recorded into `cmd`.
void Barrier(VkCommandBuffer cmd, VkImage image, VkImageAspectFlags aspect, uint32_t levels, VkImageLayout from,
             VkImageLayout to) {
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcAccessMask = AccessOf(from);
    barrier.dstAccessMask = AccessOf(to);
    barrier.oldLayout = from;
    barrier.newLayout = to;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = {aspect, 0, levels, 0, 1};
    R().api.vkCmdPipelineBarrier(cmd, StageOf(from), StageOf(to), 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

VkFormat DepthFormat() {
    for (VkFormat format : {VK_FORMAT_D32_SFLOAT, VK_FORMAT_X8_D24_UNORM_PACK32, VK_FORMAT_D24_UNORM_S8_UINT,
                            VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D16_UNORM}) {
        VkFormatProperties props{};
        R().api.vkGetPhysicalDeviceFormatProperties(R().physical, format, &props);
        if ((props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0) return format;
    }
    return VK_FORMAT_D32_SFLOAT;
}

bool HasStencil(VkFormat format) {
    return format == VK_FORMAT_D24_UNORM_S8_UINT || format == VK_FORMAT_D32_SFLOAT_S8_UINT;
}

// ---- Targets

void DestroyTarget(Target& target) {
    Renderer& r = R();
    if (target.framebuffer != VK_NULL_HANDLE) r.api.vkDestroyFramebuffer(r.device, target.framebuffer, nullptr);
    target.framebuffer = VK_NULL_HANDLE;
    DestroyImage(target.colour);
    DestroyImage(target.depth);
}

// A target of the size, cleared to black (and the depth to 1), in the layouts the render pass takes.
bool CreateTarget(Target& target, int wide, int high) {
    Renderer& r = R();
    DestroyTarget(target);
    const VkFormat depthFormat = DepthFormat();
    if (!CreateImage(target.colour, static_cast<uint32_t>(wide), static_cast<uint32_t>(high), 1, kColourFormat,
                     VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                         VK_IMAGE_USAGE_SAMPLED_BIT,
                     VK_IMAGE_ASPECT_COLOR_BIT) ||
        !CreateImage(target.depth, static_cast<uint32_t>(wide), static_cast<uint32_t>(high), 1, depthFormat,
                     VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                     VK_IMAGE_ASPECT_DEPTH_BIT | (HasStencil(depthFormat) ? VK_IMAGE_ASPECT_STENCIL_BIT : 0))) {
        DestroyTarget(target);
        return false;
    }
    const VkImageView views[2] = {target.colour.view, target.depth.view};
    VkFramebufferCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    info.renderPass = r.renderPass;
    info.attachmentCount = 2;
    info.pAttachments = views;
    info.width = static_cast<uint32_t>(wide);
    info.height = static_cast<uint32_t>(high);
    info.layers = 1;
    const VkResult made = r.api.vkCreateFramebuffer(r.device, &info, nullptr, &target.framebuffer);
    if (made != VK_SUCCESS) {
        Fail("making a framebuffer", made);
        DestroyTarget(target);
        return false;
    }
    VkCommandBuffer cmd = Cmd();
    EnsureLayout(target.colour, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    EnsureLayout(target.depth, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    const VkClearColorValue black = {{0.0f, 0.0f, 0.0f, 1.0f}};
    const VkImageSubresourceRange colourRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    r.api.vkCmdClearColorImage(cmd, target.colour.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &black, 1, &colourRange);
    const VkClearDepthStencilValue far = {1.0f, 0};
    const VkImageSubresourceRange depthRange = {target.depth.aspect, 0, 1, 0, 1};
    r.api.vkCmdClearDepthStencilImage(cmd, target.depth.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &far, 1, &depthRange);
    EnsureLayout(target.colour, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    EnsureLayout(target.depth, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    return true;
}

// ---- The device

struct Candidate {
    VkPhysicalDevice device = VK_NULL_HANDLE;
    uint32_t queueFamily = 0;
    int score = -1;
    VkPhysicalDeviceProperties properties{};
};

bool HasSwapchainExtension(VkPhysicalDevice device) {
    Renderer& r = R();
    uint32_t count = 0;
    r.api.vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> extensions(count);
    r.api.vkEnumerateDeviceExtensionProperties(device, nullptr, &count, extensions.data());
    for (const VkExtensionProperties& extension : extensions)
        if (std::strcmp(extension.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0) return true;
    return false;
}

const char* TypeName(VkPhysicalDeviceType type) {
    switch (type) {
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: return "discrete";
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return "integrated";
        case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: return "virtual";
        case VK_PHYSICAL_DEVICE_TYPE_CPU: return "software";
        default: return "other";
    }
}

// The device the window is drawn with: the first discrete one, else integrated, virtual, then a software one (lavapipe),
// each with a queue that draws and presents to the surface and the swapchain extension. TPW_VULKAN_DEVICE (a number, or a
// part of a name) picks another. Every device is listed in the log once.
bool PickDevice(bool log) {
    Renderer& r = R();
    uint32_t count = 0;
    r.api.vkEnumeratePhysicalDevices(r.instance, &count, nullptr);
    std::vector<VkPhysicalDevice> devices(count);
    if (count == 0) {
        tpw::Log("vulkan: no device");
        return false;
    }
    r.api.vkEnumeratePhysicalDevices(r.instance, &count, devices.data());
    const char* wanted = std::getenv("TPW_VULKAN_DEVICE");
    Candidate best;
    for (uint32_t i = 0; i < count; ++i) {
        Candidate candidate;
        candidate.device = devices[i];
        r.api.vkGetPhysicalDeviceProperties(devices[i], &candidate.properties);
        uint32_t families = 0;
        r.api.vkGetPhysicalDeviceQueueFamilyProperties(devices[i], &families, nullptr);
        std::vector<VkQueueFamilyProperties> queues(families);
        r.api.vkGetPhysicalDeviceQueueFamilyProperties(devices[i], &families, queues.data());
        bool usable = false;
        for (uint32_t family = 0; family < families && !usable; ++family) {
            if ((queues[family].queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0) continue;
            VkBool32 presents = VK_FALSE;
            r.api.vkGetPhysicalDeviceSurfaceSupportKHR(devices[i], family, r.surface, &presents);
            if (presents == VK_TRUE) {
                candidate.queueFamily = family;
                usable = true;
            }
        }
        usable = usable && HasSwapchainExtension(devices[i]);
        switch (candidate.properties.deviceType) {
            case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: candidate.score = 4; break;
            case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: candidate.score = 3; break;
            case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: candidate.score = 2; break;
            case VK_PHYSICAL_DEVICE_TYPE_CPU: candidate.score = 1; break;
            default: candidate.score = 0; break;
        }
        bool asked = false;
        if (wanted != nullptr && wanted[0] != '\0') {
            char* end = nullptr;
            const long index = std::strtol(wanted, &end, 10);
            asked = (end != wanted && *end == '\0') ? index == static_cast<long>(i)
                                                     : std::strstr(candidate.properties.deviceName, wanted) != nullptr;
            if (asked) candidate.score += 100;
        }
        if (log) {
            tpw::Log("vulkan: device %u: \"%s\" (%s, Vulkan %u.%u)%s%s", i, candidate.properties.deviceName,
                     TypeName(candidate.properties.deviceType), VK_API_VERSION_MAJOR(candidate.properties.apiVersion),
                     VK_API_VERSION_MINOR(candidate.properties.apiVersion),
                     usable ? "" : " (cannot draw to the window)", asked ? " (TPW_VULKAN_DEVICE)" : "");
        }
        if (usable && candidate.score > best.score) best = candidate;
    }
    if (best.device == VK_NULL_HANDLE) {
        tpw::Log("vulkan: no device can draw to the window");
        return false;
    }
    r.physical = best.device;
    r.queueFamily = best.queueFamily;
    r.properties = best.properties;
    r.api.vkGetPhysicalDeviceMemoryProperties(r.physical, &r.memoryProperties);
    return true;
}

bool MakeInstance() {
    Renderer& r = R();
    auto get = reinterpret_cast<PFN_vkGetInstanceProcAddr>(SDL_Vulkan_GetVkGetInstanceProcAddr());
    if (get == nullptr || !r.api.LoadGlobal(get)) {
        tpw::Log("vulkan: no loader (%s)", SDL_GetError());
        return false;
    }
    uint32_t extensionCount = 0;
    const char* const* extensions = SDL_Vulkan_GetInstanceExtensions(&extensionCount);
    if (extensions == nullptr) {
        tpw::Log("vulkan: SDL names no instance extensions (%s)", SDL_GetError());
        return false;
    }
    std::vector<const char*> layers;
    if (std::getenv("TPW_VULKAN_VALIDATION") != nullptr) {   // a development aid: the validation layer, when it is installed
        uint32_t layerCount = 0;
        r.api.vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
        std::vector<VkLayerProperties> available(layerCount);
        r.api.vkEnumerateInstanceLayerProperties(&layerCount, available.data());
        for (const VkLayerProperties& layer : available)
            if (std::strcmp(layer.layerName, "VK_LAYER_KHRONOS_validation") == 0) layers.push_back("VK_LAYER_KHRONOS_validation");
        tpw::Log("vulkan: validation layer %s", layers.empty() ? "asked for but not installed" : "on");
    }
    VkApplicationInfo app{};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "tpw_launcher";
    app.pEngineName = "tpw-port";
    app.apiVersion = VK_API_VERSION_1_0;
    VkInstanceCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    info.pApplicationInfo = &app;
    info.enabledExtensionCount = extensionCount;
    info.ppEnabledExtensionNames = extensions;
    info.enabledLayerCount = static_cast<uint32_t>(layers.size());
    info.ppEnabledLayerNames = layers.data();
    const VkResult made = r.api.vkCreateInstance(&info, nullptr, &r.instance);
    if (made != VK_SUCCESS) {
        tpw::Log("vulkan: no instance (%s)", ResultName(made));
        r.instance = VK_NULL_HANDLE;
        return false;
    }
    if (!r.api.LoadInstance(r.instance)) {
        tpw::Log("vulkan: the loader lacks an instance call");
        return false;
    }
    return true;
}

bool MakeDevice() {
    Renderer& r = R();
    const float priority = 1.0f;
    VkDeviceQueueCreateInfo queue{};
    queue.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue.queueFamilyIndex = r.queueFamily;
    queue.queueCount = 1;
    queue.pQueuePriorities = &priority;
    const char* extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkDeviceCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    info.queueCreateInfoCount = 1;
    info.pQueueCreateInfos = &queue;
    info.enabledExtensionCount = 1;
    info.ppEnabledExtensionNames = extensions;
    VkPhysicalDeviceFeatures features{};   // none asked for: the renderer uses the core alone
    info.pEnabledFeatures = &features;
    const VkResult made = r.api.vkCreateDevice(r.physical, &info, nullptr, &r.device);
    if (made != VK_SUCCESS) {
        tpw::Log("vulkan: no device (%s)", ResultName(made));
        r.device = VK_NULL_HANDLE;
        return false;
    }
    if (!r.api.LoadDevice(r.device)) {
        tpw::Log("vulkan: the driver lacks a device call");
        return false;
    }
    r.api.vkGetDeviceQueue(r.device, r.queueFamily, 0, &r.queue);
    char text[512];
    const uint32_t version = r.properties.apiVersion, driver = r.properties.driverVersion;
    std::snprintf(text, sizeof text, "Vulkan %u.%u on %s (%s; driver %u.%u.%u)", VK_API_VERSION_MAJOR(version),
                  VK_API_VERSION_MINOR(version), r.properties.deviceName, TypeName(r.properties.deviceType),
                  VK_API_VERSION_MAJOR(driver), VK_API_VERSION_MINOR(driver), VK_API_VERSION_PATCH(driver));
    r.description = text;
    return true;
}

// The instance, the surface and the device on the window. `log` lists the devices.
bool InitDevice(SDL_Window* window, bool log) {
    Renderer& r = R();
    r.window = window;
    if (!MakeInstance()) return false;
    if (!SDL_Vulkan_CreateSurface(window, r.instance, nullptr, &r.surface)) {
        tpw::Log("vulkan: no surface on the window (%s)", SDL_GetError());
        r.surface = VK_NULL_HANDLE;
        return false;
    }
    if (!PickDevice(log)) return false;
    return MakeDevice();
}

int LargestSize() {
    const VkPhysicalDeviceLimits& limits = R().properties.limits;
    return static_cast<int>(std::min({limits.maxImageDimension2D, limits.maxFramebufferWidth, limits.maxFramebufferHeight}));
}

// ---- The swapchain

void DestroySwapchain() {
    Renderer& r = R();
    if (r.device == VK_NULL_HANDLE) return;
    r.api.vkDeviceWaitIdle(r.device);
    for (VkSemaphore semaphore : r.presented) r.api.vkDestroySemaphore(r.device, semaphore, nullptr);
    r.presented.clear();
    if (r.swapchain != VK_NULL_HANDLE) r.api.vkDestroySwapchainKHR(r.device, r.swapchain, nullptr);
    r.swapchain = VK_NULL_HANDLE;
    r.swapImages.clear();
    r.swapExtent = {0, 0};
}

// A swapchain for the window's pixels: FIFO (one presentation a refresh, the vsync the pacing counts on), images the
// present blits into. False (and no swapchain) while the window has no size, or when the surface refuses.
bool CreateSwapchain(int wide, int high) {
    Renderer& r = R();
    DestroySwapchain();
    r.swapchainStale = false;
    if (wide <= 0 || high <= 0) return false;
    VkSurfaceCapabilitiesKHR caps{};
    VkResult result = r.api.vkGetPhysicalDeviceSurfaceCapabilitiesKHR(r.physical, r.surface, &caps);
    if (result == VK_ERROR_SURFACE_LOST_KHR) {
        // The surface is gone (the window system took it back): a new one on the same window, asked once more.
        if (r.surface != VK_NULL_HANDLE) SDL_Vulkan_DestroySurface(r.instance, r.surface, nullptr);
        r.surface = VK_NULL_HANDLE;
        if (!SDL_Vulkan_CreateSurface(r.window, r.instance, nullptr, &r.surface)) {
            tpw::Log("vulkan: the surface could not be made again: %s", SDL_GetError());
            r.lost = true;
            return false;
        }
        result = r.api.vkGetPhysicalDeviceSurfaceCapabilitiesKHR(r.physical, r.surface, &caps);
    }
    if (result != VK_SUCCESS) {
        Fail("asking the surface's capabilities", result);
        return false;
    }
    if ((caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT) == 0) {
        tpw::Log("vulkan: the surface's images cannot be copied into");
        r.lost = true;
        return false;
    }
    VkExtent2D extent = caps.currentExtent;
    if (extent.width == UINT32_MAX) {   // the surface takes the window's size
        extent.width = std::clamp(static_cast<uint32_t>(wide), caps.minImageExtent.width, caps.maxImageExtent.width);
        extent.height = std::clamp(static_cast<uint32_t>(high), caps.minImageExtent.height, caps.maxImageExtent.height);
    }
    if (extent.width == 0 || extent.height == 0) return false;   // minimised
    uint32_t formatCount = 0;
    r.api.vkGetPhysicalDeviceSurfaceFormatsKHR(r.physical, r.surface, &formatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    r.api.vkGetPhysicalDeviceSurfaceFormatsKHR(r.physical, r.surface, &formatCount, formats.data());
    VkSurfaceFormatKHR chosen = formats.empty() ? VkSurfaceFormatKHR{VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR}
                                                : formats[0];
    for (const VkSurfaceFormatKHR& format : formats) {
        if (format.format == VK_FORMAT_B8G8R8A8_UNORM || format.format == VK_FORMAT_R8G8B8A8_UNORM) {
            chosen = format;
            break;
        }
    }
    uint32_t images = std::max(caps.minImageCount, 2u);
    if (caps.maxImageCount != 0) images = std::min(images, caps.maxImageCount);
    VkSwapchainCreateInfoKHR info{};
    info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    info.surface = r.surface;
    info.minImageCount = images;
    info.imageFormat = chosen.format;
    info.imageColorSpace = chosen.colorSpace;
    info.imageExtent = extent;
    info.imageArrayLayers = 1;
    info.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.preTransform = (caps.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR) != 0
                            ? VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR
                            : caps.currentTransform;
    info.compositeAlpha = (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR) != 0
                              ? VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR
                              : static_cast<VkCompositeAlphaFlagBitsKHR>(1u << __builtin_ctz(caps.supportedCompositeAlpha));
    info.presentMode = VK_PRESENT_MODE_FIFO_KHR;   // every surface has it
    info.clipped = VK_TRUE;
    result = r.api.vkCreateSwapchainKHR(r.device, &info, nullptr, &r.swapchain);
    if (result != VK_SUCCESS) {
        r.swapchain = VK_NULL_HANDLE;
        Fail("making the swapchain", result);
        return false;
    }
    uint32_t count = 0;
    r.api.vkGetSwapchainImagesKHR(r.device, r.swapchain, &count, nullptr);
    r.swapImages.resize(count);
    r.api.vkGetSwapchainImagesKHR(r.device, r.swapchain, &count, r.swapImages.data());
    r.presented.resize(count, VK_NULL_HANDLE);
    VkSemaphoreCreateInfo semaphore{};
    semaphore.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    for (VkSemaphore& made : r.presented) r.api.vkCreateSemaphore(r.device, &semaphore, nullptr, &made);
    r.swapExtent = extent;
    r.swapFormat = chosen.format;
    return true;
}

// ---- The render pass, the pipelines' fixed parts

bool MakeRenderPass() {
    Renderer& r = R();
    VkAttachmentDescription attachments[2]{};
    attachments[0].format = kColourFormat;
    attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    attachments[0].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    attachments[1].format = DepthFormat();
    attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    const VkAttachmentReference colour = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    const VkAttachmentReference depth = {1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colour;
    subpass.pDepthStencilAttachment = &depth;
    // What came before (an upload, a copy, the frame before) is finished before the pass reads or writes, and the pass is
    // finished before what comes after.
    VkSubpassDependency dependencies[2]{};
    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass = 0;
    dependencies[0].srcStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    dependencies[0].dstStageMask = VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT;
    dependencies[0].srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    dependencies[0].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    dependencies[1].srcSubpass = 0;
    dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[1].srcStageMask = VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT;
    dependencies[1].dstStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    dependencies[1].srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    dependencies[1].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    VkRenderPassCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    info.attachmentCount = 2;
    info.pAttachments = attachments;
    info.subpassCount = 1;
    info.pSubpasses = &subpass;
    info.dependencyCount = 2;
    info.pDependencies = dependencies;
    const VkResult made = r.api.vkCreateRenderPass(r.device, &info, nullptr, &r.renderPass);
    if (made != VK_SUCCESS) {
        Fail("making the render pass", made);
        return false;
    }
    return true;
}

bool MakeShader(VkShaderModule& module, const uint32_t* code, size_t bytes) {
    Renderer& r = R();
    VkShaderModuleCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    info.codeSize = bytes;
    info.pCode = code;
    const VkResult made = r.api.vkCreateShaderModule(r.device, &info, nullptr, &module);
    if (made != VK_SUCCESS) Fail("making a shader", made);
    return made == VK_SUCCESS;
}

bool MakeLayouts() {
    Renderer& r = R();
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo set{};
    set.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    set.bindingCount = 1;
    set.pBindings = &binding;
    VkResult made = r.api.vkCreateDescriptorSetLayout(r.device, &set, nullptr, &r.setLayout);
    if (made != VK_SUCCESS) {
        Fail("making the descriptor set layout", made);
        return false;
    }
    // The push constants: the matrix (64 bytes) for the vertex shader, the flags and parameters (32) for both.
    VkPushConstantRange range{};
    range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    range.offset = 0;
    range.size = 96;
    VkPipelineLayoutCreateInfo layout{};
    layout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout.setLayoutCount = 1;
    layout.pSetLayouts = &r.setLayout;
    layout.pushConstantRangeCount = 1;
    layout.pPushConstantRanges = &range;
    made = r.api.vkCreatePipelineLayout(r.device, &layout, nullptr, &r.pipelineLayout);
    if (made != VK_SUCCESS) {
        Fail("making the pipeline layout", made);
        return false;
    }
    return MakeShader(r.vertexShader, k_tpw_vert, sizeof k_tpw_vert) && MakeShader(r.fragmentShader, k_tpw_frag, sizeof k_tpw_frag);
}

bool MakeDescriptorPool(uint32_t sets) {
    Renderer& r = R();
    VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, sets};
    VkDescriptorPoolCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    info.maxSets = sets;
    info.poolSizeCount = 1;
    info.pPoolSizes = &size;
    VkDescriptorPool pool = VK_NULL_HANDLE;
    const VkResult made = r.api.vkCreateDescriptorPool(r.device, &info, nullptr, &pool);
    if (made != VK_SUCCESS) {
        Fail("making a descriptor pool", made);
        return false;
    }
    if (r.descriptorPool != VK_NULL_HANDLE) {   // the old one goes once this frame has run
        VkDescriptorPool old = r.descriptorPool;
        r.garbage.push_back([old] { R().api.vkDestroyDescriptorPool(R().device, old, nullptr); });
    }
    r.descriptorPool = pool;
    r.descriptorPoolSize = sets;
    return true;
}

bool MakeFrame() {
    Renderer& r = R();
    VkCommandPoolCreateInfo pool{};
    pool.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool.queueFamilyIndex = r.queueFamily;
    VkResult made = r.api.vkCreateCommandPool(r.device, &pool, nullptr, &r.commandPool);
    if (made != VK_SUCCESS) {
        Fail("making the command pool", made);
        return false;
    }
    VkCommandBufferAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc.commandPool = r.commandPool;
    alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc.commandBufferCount = 2;
    VkCommandBuffer buffers[2];
    made = r.api.vkAllocateCommandBuffers(r.device, &alloc, buffers);
    if (made != VK_SUCCESS) {
        Fail("making the command buffers", made);
        return false;
    }
    r.commands = buffers[0];
    r.presentCommands = buffers[1];
    VkFenceCreateInfo fence{};
    fence.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    made = r.api.vkCreateFence(r.device, &fence, nullptr, &r.fence);
    if (made != VK_SUCCESS) {
        Fail("making the fence", made);
        return false;
    }
    VkSemaphoreCreateInfo semaphore{};
    semaphore.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    made = r.api.vkCreateSemaphore(r.device, &semaphore, nullptr, &r.acquired);
    if (made != VK_SUCCESS) {
        Fail("making a semaphore", made);
        return false;
    }
    return MakeDescriptorPool(kDescriptorPoolSets);
}

bool MakeWhite() {
    Renderer& r = R();
    if (!CreateImage(r.white.image, 1, 1, 1, kColourFormat, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                     VK_IMAGE_ASPECT_COLOR_BIT))
        return false;
    const uint8_t white[4] = {255, 255, 255, 255};
    r.white.filled = 1;
    r.white.minFilter = GL_NEAREST;
    r.white.magFilter = GL_NEAREST;
    return UploadPixels(r.white.image, 0, 0, 0, 1, 1, white);
}

void ShutdownDevice() {
    Renderer& r = R();
    if (r.device != VK_NULL_HANDLE) {
        r.api.vkDeviceWaitIdle(r.device);
        DestroySwapchain();
        DestroyTextures();
        DestroyImage(r.white.image);
        DestroyTarget(r.game);
        DestroyTarget(r.present);
        for (auto& entry : r.pipelines) r.api.vkDestroyPipeline(r.device, entry.second, nullptr);
        r.pipelines.clear();
        for (auto& entry : r.samplers) r.api.vkDestroySampler(r.device, entry.second, nullptr);
        r.samplers.clear();
        r.descriptorSets.clear();
        for (auto& destroy : r.garbage) destroy();
        r.garbage.clear();
        if (r.descriptorPool != VK_NULL_HANDLE) r.api.vkDestroyDescriptorPool(r.device, r.descriptorPool, nullptr);
        if (r.vertexShader != VK_NULL_HANDLE) r.api.vkDestroyShaderModule(r.device, r.vertexShader, nullptr);
        if (r.fragmentShader != VK_NULL_HANDLE) r.api.vkDestroyShaderModule(r.device, r.fragmentShader, nullptr);
        if (r.pipelineLayout != VK_NULL_HANDLE) r.api.vkDestroyPipelineLayout(r.device, r.pipelineLayout, nullptr);
        if (r.setLayout != VK_NULL_HANDLE) r.api.vkDestroyDescriptorSetLayout(r.device, r.setLayout, nullptr);
        if (r.renderPass != VK_NULL_HANDLE) r.api.vkDestroyRenderPass(r.device, r.renderPass, nullptr);
        for (Buffer& chunk : r.ring.chunks) DestroyBuffer(chunk);
        r.ring.chunks.clear();
        r.ring.Reset();
        DestroyBuffer(r.readback);
        if (r.acquired != VK_NULL_HANDLE) r.api.vkDestroySemaphore(r.device, r.acquired, nullptr);
        if (r.fence != VK_NULL_HANDLE) r.api.vkDestroyFence(r.device, r.fence, nullptr);
        if (r.commandPool != VK_NULL_HANDLE) r.api.vkDestroyCommandPool(r.device, r.commandPool, nullptr);
        r.api.vkDestroyDevice(r.device, nullptr);
    }
    if (r.instance != VK_NULL_HANDLE) {
        if (r.surface != VK_NULL_HANDLE) SDL_Vulkan_DestroySurface(r.instance, r.surface, nullptr);
        r.api.vkDestroyInstance(r.instance, nullptr);
    }
    const Api api = r.api;   // the loader's entry stays loaded (SDL keeps the library while a Vulkan window exists)
    r = Renderer();
    r.api = api;
}

bool gProbed = false, gProbeOk = false;
int gProbeLargest = 0;

}  // namespace

// ---- renderer_internal.h's calls

void Fail(const char* what, VkResult result) {
    Renderer& r = R();
    if (!r.lost) tpw::Log("vulkan: %s failed (%s); nothing more is drawn", what, ResultName(result));
    r.lost = true;
    r.error = GL_OUT_OF_MEMORY;
}

VkCommandBuffer Cmd() {
    Renderer& r = R();
    if (!r.recording) {
        VkCommandBufferBeginInfo begin{};
        begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        const VkResult began = r.api.vkBeginCommandBuffer(r.commands, &begin);
        if (began != VK_SUCCESS) Fail("beginning the command buffer", began);
        r.recording = true;
        r.boundPipeline = VK_NULL_HANDLE;
    }
    return r.commands;
}

void EndPass() {
    Renderer& r = R();
    if (!r.inPass) return;
    r.api.vkCmdEndRenderPass(r.commands);
    r.inPass = false;
    r.passTarget = nullptr;
    r.boundPipeline = VK_NULL_HANDLE;
}

void RunFrame() {
    Renderer& r = R();
    if (r.device == VK_NULL_HANDLE) return;
    EndPass();
    if (r.recording) {
        const VkResult ended = r.api.vkEndCommandBuffer(r.commands);
        r.recording = false;
        if (ended == VK_SUCCESS && !r.lost) {
            VkSubmitInfo submit{};
            submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            submit.commandBufferCount = 1;
            submit.pCommandBuffers = &r.commands;
            VkResult result = r.api.vkQueueSubmit(r.queue, 1, &submit, r.fence);
            if (result == VK_SUCCESS) result = r.api.vkWaitForFences(r.device, 1, &r.fence, VK_TRUE, UINT64_MAX);
            if (result != VK_SUCCESS) Fail("running the frame", result);
            r.api.vkResetFences(r.device, 1, &r.fence);
        } else if (ended != VK_SUCCESS) {
            Fail("ending the command buffer", ended);
        }
    }
    // The frame's memory, sets and the things deleted during it are free again.
    r.ring.Reset();
    r.descriptorSets.clear();
    if (r.descriptorPool != VK_NULL_HANDLE) r.api.vkResetDescriptorPool(r.device, r.descriptorPool, 0);
    for (auto& destroy : r.garbage) destroy();
    r.garbage.clear();
}

void EnsureLayout(Image& image, VkImageLayout layout) {
    if (image.image == VK_NULL_HANDLE || image.layout == layout) return;
    EndPass();
    Barrier(Cmd(), image.image, image.aspect, image.levels, image.layout, layout);
    image.layout = layout;
}

bool CreateImage(Image& image, uint32_t width, uint32_t height, uint32_t levels, VkFormat format, VkImageUsageFlags usage,
                 VkImageAspectFlags aspect) {
    Renderer& r = R();
    image = Image();
    image.format = format;
    image.aspect = aspect;
    image.width = width;
    image.height = height;
    image.levels = levels;
    VkImageCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = format;
    info.extent = {width, height, 1};
    info.mipLevels = levels;
    info.arrayLayers = 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkResult made = r.api.vkCreateImage(r.device, &info, nullptr, &image.image);
    if (made != VK_SUCCESS) {
        image.image = VK_NULL_HANDLE;
        Fail("making an image", made);
        return false;
    }
    VkMemoryRequirements needs{};
    r.api.vkGetImageMemoryRequirements(r.device, image.image, &needs);
    VkMemoryAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc.allocationSize = needs.size;
    alloc.memoryTypeIndex = MemoryType(needs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0);
    if (alloc.memoryTypeIndex == UINT32_MAX || (made = r.api.vkAllocateMemory(r.device, &alloc, nullptr, &image.memory)) != VK_SUCCESS ||
        (made = r.api.vkBindImageMemory(r.device, image.image, image.memory, 0)) != VK_SUCCESS) {
        Fail("giving an image its memory", made);
        DestroyImage(image);
        return false;
    }
    VkImageViewCreateInfo view{};
    view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view.image = image.image;
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.format = format;
    view.subresourceRange = {aspect, 0, levels, 0, 1};
    made = r.api.vkCreateImageView(r.device, &view, nullptr, &image.view);
    if (made != VK_SUCCESS) {
        image.view = VK_NULL_HANDLE;
        Fail("making an image view", made);
        DestroyImage(image);
        return false;
    }
    return true;
}

void DestroyImage(Image& image) {
    Renderer& r = R();
    if (image.view != VK_NULL_HANDLE) r.api.vkDestroyImageView(r.device, image.view, nullptr);
    if (image.image != VK_NULL_HANDLE) r.api.vkDestroyImage(r.device, image.image, nullptr);
    if (image.memory != VK_NULL_HANDLE) r.api.vkFreeMemory(r.device, image.memory, nullptr);
    image = Image();
}

void DeferDestroyImage(const Image& image) {
    Image copy = image;
    R().garbage.push_back([copy]() mutable { DestroyImage(copy); });
}

bool CreateBuffer(Buffer& buffer, VkDeviceSize size, VkBufferUsageFlags usage, bool hostVisible) {
    Renderer& r = R();
    buffer = Buffer();
    VkBufferCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    info.size = size;
    info.usage = usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VkResult made = r.api.vkCreateBuffer(r.device, &info, nullptr, &buffer.buffer);
    if (made != VK_SUCCESS) {
        buffer.buffer = VK_NULL_HANDLE;
        Fail("making a buffer", made);
        return false;
    }
    VkMemoryRequirements needs{};
    r.api.vkGetBufferMemoryRequirements(r.device, buffer.buffer, &needs);
    VkMemoryAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc.allocationSize = needs.size;
    const VkMemoryPropertyFlags host = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    alloc.memoryTypeIndex = MemoryType(needs.memoryTypeBits, hostVisible ? host : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                       hostVisible ? host : 0);
    if (alloc.memoryTypeIndex == UINT32_MAX || (made = r.api.vkAllocateMemory(r.device, &alloc, nullptr, &buffer.memory)) != VK_SUCCESS ||
        (made = r.api.vkBindBufferMemory(r.device, buffer.buffer, buffer.memory, 0)) != VK_SUCCESS) {
        Fail("giving a buffer its memory", made);
        DestroyBuffer(buffer);
        return false;
    }
    if (hostVisible) {
        void* mapped = nullptr;
        made = r.api.vkMapMemory(r.device, buffer.memory, 0, VK_WHOLE_SIZE, 0, &mapped);
        if (made != VK_SUCCESS) {
            Fail("mapping a buffer", made);
            DestroyBuffer(buffer);
            return false;
        }
        buffer.mapped = static_cast<uint8_t*>(mapped);
    }
    buffer.size = size;
    return true;
}

void DestroyBuffer(Buffer& buffer) {
    Renderer& r = R();
    if (buffer.mapped != nullptr) r.api.vkUnmapMemory(r.device, buffer.memory);
    if (buffer.buffer != VK_NULL_HANDLE) r.api.vkDestroyBuffer(r.device, buffer.buffer, nullptr);
    if (buffer.memory != VK_NULL_HANDLE) r.api.vkFreeMemory(r.device, buffer.memory, nullptr);
    buffer = Buffer();
}

uint8_t* RingAlloc(VkDeviceSize size, VkDeviceSize alignment, VkBuffer& buffer, VkDeviceSize& offset) {
    Renderer& r = R();
    Ring& ring = r.ring;
    const VkBufferUsageFlags usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    for (;;) {
        if (ring.chunk < ring.chunks.size()) {
            Buffer& chunk = ring.chunks[ring.chunk];
            const VkDeviceSize aligned = (ring.offset + alignment - 1) / alignment * alignment;
            if (aligned + size <= chunk.size) {
                ring.offset = aligned + size;
                buffer = chunk.buffer;
                offset = aligned;
                return chunk.mapped + aligned;
            }
            ++ring.chunk;
            ring.offset = 0;
            continue;
        }
        Buffer chunk;
        if (!CreateBuffer(chunk, std::max(kRingChunk, size + alignment), usage, true)) return nullptr;
        ring.chunks.push_back(chunk);
    }
}

VkSampler SamplerFor(const Texture& texture) {
    Renderer& r = R();
    const bool mipmapped = texture.filled == texture.image.levels && texture.image.levels > 1 &&
                           (texture.minFilter == GL_NEAREST_MIPMAP_NEAREST || texture.minFilter == GL_LINEAR_MIPMAP_NEAREST ||
                            texture.minFilter == GL_NEAREST_MIPMAP_LINEAR || texture.minFilter == GL_LINEAR_MIPMAP_LINEAR);
    const bool minLinear = texture.minFilter == GL_LINEAR || texture.minFilter == GL_LINEAR_MIPMAP_NEAREST ||
                           texture.minFilter == GL_LINEAR_MIPMAP_LINEAR;
    const bool mipLinear = texture.minFilter == GL_NEAREST_MIPMAP_LINEAR || texture.minFilter == GL_LINEAR_MIPMAP_LINEAR;
    const bool magLinear = texture.magFilter != GL_NEAREST;
    const bool repeatS = texture.wrapS == GL_REPEAT, repeatT = texture.wrapT == GL_REPEAT;
    const uint64_t key = (mipmapped ? 1u : 0u) | (minLinear ? 2u : 0u) | (mipLinear ? 4u : 0u) | (magLinear ? 8u : 0u) |
                         (repeatS ? 16u : 0u) | (repeatT ? 32u : 0u);
    const auto found = r.samplers.find(key);
    if (found != r.samplers.end()) return found->second;
    VkSamplerCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    info.magFilter = magLinear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    info.minFilter = minLinear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    // Without the chain the base level alone is sampled: nearest between levels and a maximum LOD of 0, or a quarter of
    // the never-written level 1 would blend in (noise) at the LOD 0.25 the clamp below allows a linear mode.
    info.mipmapMode = mipmapped && mipLinear ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
    info.addressModeU = repeatS ? VK_SAMPLER_ADDRESS_MODE_REPEAT : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    info.addressModeV = repeatT ? VK_SAMPLER_ADDRESS_MODE_REPEAT : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    info.maxLod = mipmapped ? VK_LOD_CLAMP_NONE : 0.0f;
    info.maxAnisotropy = 1.0f;
    info.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
    VkSampler sampler = VK_NULL_HANDLE;
    const VkResult made = r.api.vkCreateSampler(r.device, &info, nullptr, &sampler);
    if (made != VK_SUCCESS) {
        Fail("making a sampler", made);
        return VK_NULL_HANDLE;
    }
    r.samplers[key] = sampler;
    return sampler;
}

VkDescriptorSet DescriptorSetFor(VkImageView view, VkSampler sampler) {
    Renderer& r = R();
    const uint64_t key = reinterpret_cast<uint64_t>(view) * 1000003u ^ reinterpret_cast<uint64_t>(sampler);
    const auto found = r.descriptorSets.find(key);
    if (found != r.descriptorSets.end()) return found->second;
    VkDescriptorSetAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc.descriptorPool = r.descriptorPool;
    alloc.descriptorSetCount = 1;
    alloc.pSetLayouts = &r.setLayout;
    VkDescriptorSet set = VK_NULL_HANDLE;
    VkResult made = r.api.vkAllocateDescriptorSets(r.device, &alloc, &set);
    if (made != VK_SUCCESS) {   // the frame used the pool up: a larger one takes over (the old one goes with the frame)
        if (!MakeDescriptorPool(r.descriptorPoolSize * 2)) return VK_NULL_HANDLE;
        r.descriptorSets.clear();
        alloc.descriptorPool = r.descriptorPool;
        made = r.api.vkAllocateDescriptorSets(r.device, &alloc, &set);
        if (made != VK_SUCCESS) {
            Fail("making a descriptor set", made);
            return VK_NULL_HANDLE;
        }
    }
    VkDescriptorImageInfo image{};
    image.sampler = sampler;
    image.imageView = view;
    image.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = set;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &image;
    r.api.vkUpdateDescriptorSets(r.device, 1, &write, 0, nullptr);
    r.descriptorSets[key] = set;
    return set;
}

// The pipeline for a key (draw.cpp's PipelineKey): bits 0..3 the topology, 4 blending, 5..8 the source factor, 9..12 the
// destination factor, 13 the depth test, 14 depth writes, 15..17 the depth function, 18..21 the colour mask.
VkPipeline PipelineFor(uint64_t key) {
    Renderer& r = R();
    const auto found = r.pipelines.find(key);
    if (found != r.pipelines.end()) return found->second;
    static const VkBlendFactor kFactors[11] = {
        VK_BLEND_FACTOR_ZERO,      VK_BLEND_FACTOR_ONE,                 VK_BLEND_FACTOR_SRC_COLOR,
        VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR, VK_BLEND_FACTOR_SRC_ALPHA,  VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        VK_BLEND_FACTOR_DST_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA, VK_BLEND_FACTOR_DST_COLOR,
        VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR, VK_BLEND_FACTOR_SRC_ALPHA_SATURATE};
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = r.vertexShader;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = r.fragmentShader;
    stages[1].pName = "main";
    VkVertexInputBindingDescription binding{0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription attributes[3] = {
        {0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 0},
        {1, 0, VK_FORMAT_R8G8B8A8_UNORM, 16},
        {2, 0, VK_FORMAT_R32G32_SFLOAT, 24},
    };
    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &binding;
    vertexInput.vertexAttributeDescriptionCount = 3;
    vertexInput.pVertexAttributeDescriptions = attributes;
    VkPipelineInputAssemblyStateCreateInfo assembly{};
    assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    assembly.topology = static_cast<VkPrimitiveTopology>(key & 0xf);
    VkPipelineViewportStateCreateInfo viewport{};
    viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo raster{};
    raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;   // the game switches culling off
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo depth{};
    depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth.depthTestEnable = (key >> 13 & 1) != 0 ? VK_TRUE : VK_FALSE;
    depth.depthWriteEnable = (key >> 14 & 1) != 0 ? VK_TRUE : VK_FALSE;
    depth.depthCompareOp = static_cast<VkCompareOp>(key >> 15 & 7);   // OpenGL's GL_NEVER..GL_ALWAYS are Vulkan's order
    VkPipelineColorBlendAttachmentState blend{};
    blend.blendEnable = (key >> 4 & 1) != 0 ? VK_TRUE : VK_FALSE;
    blend.srcColorBlendFactor = kFactors[std::min<uint64_t>(key >> 5 & 0xf, 10)];
    blend.dstColorBlendFactor = kFactors[std::min<uint64_t>(key >> 9 & 0xf, 10)];
    blend.colorBlendOp = VK_BLEND_OP_ADD;
    blend.srcAlphaBlendFactor = blend.srcColorBlendFactor;
    blend.dstAlphaBlendFactor = blend.dstColorBlendFactor;
    blend.alphaBlendOp = VK_BLEND_OP_ADD;
    blend.colorWriteMask = static_cast<VkColorComponentFlags>(key >> 18 & 0xf);
    VkPipelineColorBlendStateCreateInfo blending{};
    blending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blending.attachmentCount = 1;
    blending.pAttachments = &blend;
    const VkDynamicState dynamicStates[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{};
    dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates = dynamicStates;
    VkGraphicsPipelineCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    info.stageCount = 2;
    info.pStages = stages;
    info.pVertexInputState = &vertexInput;
    info.pInputAssemblyState = &assembly;
    info.pViewportState = &viewport;
    info.pRasterizationState = &raster;
    info.pMultisampleState = &multisample;
    info.pDepthStencilState = &depth;
    info.pColorBlendState = &blending;
    info.pDynamicState = &dynamic;
    info.layout = r.pipelineLayout;
    info.renderPass = r.renderPass;
    VkPipeline pipeline = VK_NULL_HANDLE;
    const VkResult made = r.api.vkCreateGraphicsPipelines(r.device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline);
    if (made != VK_SUCCESS) {
        Fail("making a pipeline", made);
        return VK_NULL_HANDLE;
    }
    r.pipelines[key] = pipeline;
    return pipeline;
}

bool UploadPixels(Image& image, uint32_t level, int32_t x, int32_t y, int32_t width, int32_t height, const uint8_t* rgba) {
    Renderer& r = R();
    if (width <= 0 || height <= 0 || image.image == VK_NULL_HANDLE) return false;
    const VkDeviceSize bytes = static_cast<VkDeviceSize>(width) * static_cast<VkDeviceSize>(height) * 4;
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceSize offset = 0;
    uint8_t* room = RingAlloc(bytes, 16, buffer, offset);
    if (room == nullptr) return false;
    std::memcpy(room, rgba, static_cast<size_t>(bytes));
    EnsureLayout(image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkBufferImageCopy region{};
    region.bufferOffset = offset;
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level, 0, 1};
    region.imageOffset = {x, y, 0};
    region.imageExtent = {static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};
    r.api.vkCmdCopyBufferToImage(Cmd(), buffer, image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    EnsureLayout(image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    return !r.lost;
}

bool ReadImage(Image& image, int32_t x, int32_t y, int32_t width, int32_t height, std::vector<uint8_t>& out) {
    Renderer& r = R();
    out.clear();
    if (image.image == VK_NULL_HANDLE || width <= 0 || height <= 0) return false;
    const VkDeviceSize bytes = static_cast<VkDeviceSize>(width) * static_cast<VkDeviceSize>(height) * 4;
    if (r.readback.size < bytes) {
        DestroyBuffer(r.readback);
        if (!CreateBuffer(r.readback, bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true)) return false;
    }
    const VkImageLayout was = image.layout;
    EnsureLayout(image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageOffset = {x, y, 0};
    region.imageExtent = {static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};
    r.api.vkCmdCopyImageToBuffer(Cmd(), image.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, r.readback.buffer, 1, &region);
    if (was == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL || was == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) EnsureLayout(image, was);
    RunFrame();
    if (r.lost) return false;
    out.assign(r.readback.mapped, r.readback.mapped + bytes);
    return true;
}

bool EnsurePass() {
    Renderer& r = R();
    if (!r.Alive() || r.drawTarget == nullptr || !r.drawTarget->Ready()) return false;
    if (r.inPass && r.passTarget == r.drawTarget) return true;
    EndPass();
    Target& target = *r.drawTarget;
    EnsureLayout(target.colour, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    EnsureLayout(target.depth, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    VkRenderPassBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    begin.renderPass = r.renderPass;
    begin.framebuffer = target.framebuffer;
    begin.renderArea = {{0, 0}, {target.colour.width, target.colour.height}};
    r.api.vkCmdBeginRenderPass(Cmd(), &begin, VK_SUBPASS_CONTENTS_INLINE);
    r.inPass = true;
    r.passTarget = &target;
    r.boundPipeline = VK_NULL_HANDLE;
    return true;
}

// ---- renderer.h: the backend

bool Probe(int& largest) {
    largest = 0;
    if (gProbed) {
        largest = gProbeLargest;
        return gProbeOk;
    }
    gProbed = true;
    SDL_Window* probe = SDL_CreateWindow("", 1, 1, SDL_WINDOW_VULKAN | SDL_WINDOW_HIDDEN);
    if (probe == nullptr) {
        tpw::Log("vulkan: no Vulkan window (%s)", SDL_GetError());
        return false;
    }
    const bool ok = InitDevice(probe, true);
    if (ok) {
        gProbeLargest = LargestSize();
        tpw::Log("vulkan: %s; frames up to %d pixels", R().description.c_str(), gProbeLargest);
    }
    ShutdownDevice();
    SDL_DestroyWindow(probe);
    gProbeOk = ok;
    largest = gProbeLargest;
    return ok;
}

uint32_t WindowFlags() { return SDL_WINDOW_VULKAN; }

bool Create(SDL_Window* window, bool fit, int wide, int high) {
    Renderer& r = R();
    if (r.device != VK_NULL_HANDLE) Destroy();
    if (!InitDevice(window, !gProbed) || !MakeFrame() || !MakeRenderPass() || !MakeLayouts() || !MakeWhite()) {
        ShutdownDevice();
        return false;
    }
    // The fit's targets at the game's own size, or, when the device cannot make them (larger than its frames, out of
    // memory), the window's own size: the caller then drops the fit (window.cpp, FitReady), as it does with OpenGL.
    if (!Fit(fit, wide, high) && (!fit || !Fit(false, wide, high))) {
        ShutdownDevice();
        return false;
    }
    int pixelsWide = 0, pixelsHigh = 0;
    SDL_GetWindowSizeInPixels(window, &pixelsWide, &pixelsHigh);
    CreateSwapchain(pixelsWide, pixelsHigh);   // (a window without a size yet gets one at the first present)
    RunFrame();
    return !r.lost;
}

void Destroy() {
    Renderer& r = R();
    if (r.device == VK_NULL_HANDLE) return;
    RunFrame();
    ShutdownDevice();
}

bool Created() { return R().device != VK_NULL_HANDLE; }

bool Fit(bool fit, int wide, int high) {
    Renderer& r = R();
    if (r.device == VK_NULL_HANDLE) return false;
    int drawWide = wide, drawHigh = high;
    if (!fit) SDL_GetWindowSizeInPixels(r.window, &drawWide, &drawHigh);   // the window's own pixels
    if (drawWide <= 0 || drawHigh <= 0) return false;
    const int largest = LargestSize();
    if (drawWide > largest || drawHigh > largest) {
        tpw::Log("vulkan: %dx%d is larger than the device's %d-pixel frames", drawWide, drawHigh, largest);
        return false;
    }
    r.fit = fit;
    if (drawWide == r.drawWide && drawHigh == r.drawHigh && r.game.Ready() && r.present.Ready()) return true;
    EndPass();
    if (!CreateTarget(r.game, drawWide, drawHigh) || !CreateTarget(r.present, drawWide, drawHigh)) {
        DestroyTarget(r.game);
        DestroyTarget(r.present);
        r.drawWide = r.drawHigh = 0;
        r.drawTarget = nullptr;
        return false;
    }
    const int wasWide = r.drawWide, wasHigh = r.drawHigh;
    r.drawWide = drawWide;
    r.drawHigh = drawHigh;
    r.drawTarget = &r.game;
    if (wasWide == 0 || wasHigh == 0) {
        ResetState(drawWide, drawHigh);   // the first targets: the state as a new context has it
    } else {
        // New targets for a new size (a resolution change, the fit put on or off): the state stays as the game left
        // it, as the OpenGL backend keeps its context's; only a viewport that was the whole old picture follows the size.
        int32_t* v = r.state.viewport;
        if (v[0] == 0 && v[1] == 0 && v[2] == wasWide && v[3] == wasHigh) {
            v[2] = drawWide;
            v[3] = drawHigh;
        }
    }
    return true;
}

bool FitReady() { return R().fit && R().game.Ready(); }

bool SetSwapInterval(int interval) {
    R().swapInterval = std::clamp(interval, 1, 8);
    return true;
}

void BeginPresent() {
    Renderer& r = R();
    if (!r.Alive() || !r.game.Ready()) return;
    EndPass();
    EnsureLayout(r.game.colour, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    EnsureLayout(r.present.colour, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkImageBlit blit{};
    blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    blit.srcOffsets[1] = {static_cast<int32_t>(r.game.colour.width), static_cast<int32_t>(r.game.colour.height), 1};
    blit.dstSubresource = blit.srcSubresource;
    blit.dstOffsets[1] = blit.srcOffsets[1];
    r.api.vkCmdBlitImage(Cmd(), r.game.colour.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, r.present.colour.image,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_NEAREST);
    EnsureLayout(r.game.colour, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    EnsureLayout(r.present.colour, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    r.drawTarget = &r.present;
}

bool ReadPresent(uint8_t* rgb, int width, int height) {
    Renderer& r = R();
    if (!r.Alive() || !r.present.Ready() || width != r.drawWide || height != r.drawHigh) return false;
    std::vector<uint8_t> pixels;
    if (!ReadImage(r.present.colour, 0, 0, width, height, pixels)) return false;
    // The image's rows go down; OpenGL's (what the dump wants) go up.
    for (int row = 0; row < height; ++row) {
        const uint8_t* from = pixels.data() + static_cast<size_t>(height - 1 - row) * static_cast<size_t>(width) * 4;
        uint8_t* to = rgb + static_cast<size_t>(row) * static_cast<size_t>(width) * 3;
        for (int column = 0; column < width; ++column, from += 4, to += 3) {
            to[0] = from[0];
            to[1] = from[1];
            to[2] = from[2];
        }
    }
    return true;
}

void EndPresent(const fit::PixelBox& box, int windowWide, int windowHigh) {
    Renderer& r = R();
    if (!r.Alive() || !r.present.Ready()) return;
    EndPass();
    EnsureLayout(r.present.colour, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    RunFrame();   // the frame is drawn
    r.drawTarget = &r.game;
    if (r.lost) return;
    if (r.swapchain == VK_NULL_HANDLE || r.swapchainStale || r.swapExtent.width != static_cast<uint32_t>(windowWide) ||
        r.swapExtent.height != static_cast<uint32_t>(windowHigh)) {
        if (!CreateSwapchain(windowWide, windowHigh)) return;
    }
    const int32_t extentWide = static_cast<int32_t>(r.swapExtent.width), extentHigh = static_cast<int32_t>(r.swapExtent.height);
    VkImageBlit blit{};
    blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    blit.srcOffsets[1] = {static_cast<int32_t>(r.present.colour.width), static_cast<int32_t>(r.present.colour.height), 1};
    blit.dstSubresource = blit.srcSubresource;
    blit.dstOffsets[0] = {std::clamp(box.left, 0, extentWide), std::clamp(box.top, 0, extentHigh), 0};
    blit.dstOffsets[1] = {std::clamp(box.right, 0, extentWide), std::clamp(box.bottom, 0, extentHigh), 1};
    const bool sameSize = blit.dstOffsets[1].x - blit.dstOffsets[0].x == blit.srcOffsets[1].x &&
                          blit.dstOffsets[1].y - blit.dstOffsets[0].y == blit.srcOffsets[1].y;
    const bool blits = blit.dstOffsets[1].x > blit.dstOffsets[0].x && blit.dstOffsets[1].y > blit.dstOffsets[0].y;
    // Each presentation of a FIFO swapchain waits for one refresh of the screen: the swap interval is that many
    // presentations of the same picture (platform/window.cpp's pacing to 60 frames a second).
    for (int round = 0; round < r.swapInterval; ++round) {
        uint32_t index = 0;
        VkResult result = r.api.vkAcquireNextImageKHR(r.device, r.swapchain, 1000000000ull, r.acquired, VK_NULL_HANDLE, &index);
        if (result == VK_ERROR_OUT_OF_DATE_KHR) {
            if (!CreateSwapchain(windowWide, windowHigh)) return;
            result = r.api.vkAcquireNextImageKHR(r.device, r.swapchain, 1000000000ull, r.acquired, VK_NULL_HANDLE, &index);
        }
        if (result == VK_SUBOPTIMAL_KHR) r.swapchainStale = true;
        else if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_ERROR_SURFACE_LOST_KHR) {
            // Out of date again (a window still being resized), or the surface gone: the next present makes the
            // swapchain anew (and the surface with it, CreateSwapchain); this frame is not shown. Not a failure.
            r.swapchainStale = true;
            return;
        } else if (result != VK_SUCCESS) {
            if (result != VK_TIMEOUT && result != VK_NOT_READY) Fail("taking a swapchain image", result);
            return;
        }
        VkCommandBuffer cmd = r.presentCommands;
        VkCommandBufferBeginInfo begin{};
        begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        r.api.vkBeginCommandBuffer(cmd, &begin);
        VkImage swapImage = r.swapImages[index];
        Barrier(cmd, swapImage, VK_IMAGE_ASPECT_COLOR_BIT, 1, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        const VkClearColorValue black = {{0.0f, 0.0f, 0.0f, 1.0f}};
        const VkImageSubresourceRange range = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        r.api.vkCmdClearColorImage(cmd, swapImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &black, 1, &range);
        if (blits) {
            r.api.vkCmdBlitImage(cmd, r.present.colour.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, swapImage,
                                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, sameSize ? VK_FILTER_NEAREST : VK_FILTER_LINEAR);
        }
        Barrier(cmd, swapImage, VK_IMAGE_ASPECT_COLOR_BIT, 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
        r.api.vkEndCommandBuffer(cmd);
        const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        VkSubmitInfo submit{};
        submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.waitSemaphoreCount = 1;
        submit.pWaitSemaphores = &r.acquired;
        submit.pWaitDstStageMask = &waitStage;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &cmd;
        submit.signalSemaphoreCount = 1;
        submit.pSignalSemaphores = &r.presented[index];
        result = r.api.vkQueueSubmit(r.queue, 1, &submit, r.fence);
        if (result != VK_SUCCESS) {
            Fail("presenting a frame", result);
            return;
        }
        VkPresentInfoKHR present{};
        present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        present.waitSemaphoreCount = 1;
        present.pWaitSemaphores = &r.presented[index];
        present.swapchainCount = 1;
        present.pSwapchains = &r.swapchain;
        present.pImageIndices = &index;
        result = r.api.vkQueuePresentKHR(r.queue, &present);
        r.api.vkWaitForFences(r.device, 1, &r.fence, VK_TRUE, UINT64_MAX);
        r.api.vkResetFences(r.device, 1, &r.fence);
        if (result == VK_SUBOPTIMAL_KHR || result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_ERROR_SURFACE_LOST_KHR) {
            r.swapchainStale = true;
            return;
        }
        if (result != VK_SUCCESS) {
            Fail("showing a frame", result);
            return;
        }
    }
}

const char* Description() { return R().description.c_str(); }

}  // namespace tpw::vkr
