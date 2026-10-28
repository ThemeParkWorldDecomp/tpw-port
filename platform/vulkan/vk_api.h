// vk_api.h: the Vulkan calls the renderer makes, as function pointers. Nothing of Vulkan is linked: SDL opens the loader
// (SDL_Vulkan_LoadLibrary, when the window is made with SDL_WINDOW_VULKAN) and hands out vkGetInstanceProcAddr, from which
// every other call is taken, the instance ones from the instance and the device ones from the device, so each call is
// the driver's own entry point. The headers are the vendored third_party/vulkan (VK_NO_PROTOTYPES: declarations only).
#pragma once
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

namespace tpw::vkr {

// X(name): the calls, by where they are taken from.
#define TPW_VK_GLOBAL_CALLS(X)              \
    X(vkCreateInstance)                     \
    X(vkEnumerateInstanceExtensionProperties) \
    X(vkEnumerateInstanceLayerProperties)   \
    X(vkEnumerateInstanceVersion)
#define TPW_VK_INSTANCE_CALLS(X)            \
    X(vkDestroyInstance)                    \
    X(vkEnumeratePhysicalDevices)           \
    X(vkGetPhysicalDeviceProperties)        \
    X(vkGetPhysicalDeviceQueueFamilyProperties) \
    X(vkGetPhysicalDeviceMemoryProperties)  \
    X(vkGetPhysicalDeviceFormatProperties)  \
    X(vkEnumerateDeviceExtensionProperties) \
    X(vkCreateDevice)                       \
    X(vkGetDeviceProcAddr)                  \
    X(vkDestroySurfaceKHR)                  \
    X(vkGetPhysicalDeviceSurfaceSupportKHR) \
    X(vkGetPhysicalDeviceSurfaceCapabilitiesKHR) \
    X(vkGetPhysicalDeviceSurfaceFormatsKHR) \
    X(vkGetPhysicalDeviceSurfacePresentModesKHR)
#define TPW_VK_DEVICE_CALLS(X)              \
    X(vkDestroyDevice)                      \
    X(vkGetDeviceQueue)                     \
    X(vkDeviceWaitIdle)                     \
    X(vkQueueWaitIdle)                      \
    X(vkQueueSubmit)                        \
    X(vkCreateCommandPool)                  \
    X(vkDestroyCommandPool)                 \
    X(vkResetCommandPool)                   \
    X(vkAllocateCommandBuffers)             \
    X(vkBeginCommandBuffer)                 \
    X(vkEndCommandBuffer)                   \
    X(vkCreateFence)                        \
    X(vkDestroyFence)                       \
    X(vkWaitForFences)                      \
    X(vkResetFences)                        \
    X(vkCreateSemaphore)                    \
    X(vkDestroySemaphore)                   \
    X(vkCreateImage)                        \
    X(vkDestroyImage)                       \
    X(vkGetImageMemoryRequirements)         \
    X(vkBindImageMemory)                    \
    X(vkCreateImageView)                    \
    X(vkDestroyImageView)                   \
    X(vkCreateBuffer)                       \
    X(vkDestroyBuffer)                      \
    X(vkGetBufferMemoryRequirements)        \
    X(vkBindBufferMemory)                   \
    X(vkAllocateMemory)                     \
    X(vkFreeMemory)                         \
    X(vkMapMemory)                          \
    X(vkUnmapMemory)                        \
    X(vkCreateSampler)                      \
    X(vkDestroySampler)                     \
    X(vkCreateRenderPass)                   \
    X(vkDestroyRenderPass)                  \
    X(vkCreateFramebuffer)                  \
    X(vkDestroyFramebuffer)                 \
    X(vkCreateShaderModule)                 \
    X(vkDestroyShaderModule)                \
    X(vkCreateDescriptorSetLayout)          \
    X(vkDestroyDescriptorSetLayout)         \
    X(vkCreatePipelineLayout)               \
    X(vkDestroyPipelineLayout)              \
    X(vkCreateGraphicsPipelines)            \
    X(vkDestroyPipeline)                    \
    X(vkCreateDescriptorPool)               \
    X(vkDestroyDescriptorPool)              \
    X(vkResetDescriptorPool)                \
    X(vkAllocateDescriptorSets)             \
    X(vkUpdateDescriptorSets)               \
    X(vkCmdPipelineBarrier)                 \
    X(vkCmdBeginRenderPass)                 \
    X(vkCmdEndRenderPass)                   \
    X(vkCmdBindPipeline)                    \
    X(vkCmdSetViewport)                     \
    X(vkCmdSetScissor)                      \
    X(vkCmdBindDescriptorSets)              \
    X(vkCmdPushConstants)                   \
    X(vkCmdBindVertexBuffers)               \
    X(vkCmdBindIndexBuffer)                 \
    X(vkCmdDraw)                            \
    X(vkCmdDrawIndexed)                     \
    X(vkCmdClearAttachments)                \
    X(vkCmdClearColorImage)                 \
    X(vkCmdClearDepthStencilImage)          \
    X(vkCmdCopyBufferToImage)               \
    X(vkCmdCopyImageToBuffer)               \
    X(vkCmdBlitImage)                       \
    X(vkCreateSwapchainKHR)                 \
    X(vkDestroySwapchainKHR)                \
    X(vkGetSwapchainImagesKHR)              \
    X(vkAcquireNextImageKHR)                \
    X(vkQueuePresentKHR)

struct Api {
    PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr = nullptr;
#define TPW_VK_DECLARE(name) PFN_##name name = nullptr;
    TPW_VK_GLOBAL_CALLS(TPW_VK_DECLARE)
    TPW_VK_INSTANCE_CALLS(TPW_VK_DECLARE)
    TPW_VK_DEVICE_CALLS(TPW_VK_DECLARE)
#undef TPW_VK_DECLARE
    // False when a call is missing (an old loader, a driver without the swapchain extension).
    bool LoadGlobal(PFN_vkGetInstanceProcAddr getInstanceProcAddr);
    bool LoadInstance(VkInstance instance);
    bool LoadDevice(VkDevice device);
};

}  // namespace tpw::vkr
