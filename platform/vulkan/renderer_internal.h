// renderer_internal.h: what the Vulkan renderer's files share (renderer.h is its outside). device.cpp owns the device, the
// swapchain, the frame's command buffer and the resources; draw.cpp the OpenGL state and the drawing.
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "../gl_names.h"
#include "vk_api.h"

struct SDL_Window;

namespace tpw {
void Log(const char* format, ...) __attribute__((format(printf, 1, 2)));   // platform.h
}

namespace tpw::vkr {

using namespace tpw::glnames;

struct Buffer {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceSize size = 0;
    uint8_t* mapped = nullptr;
};

struct Image {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    uint32_t width = 0, height = 0, levels = 1;
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
};

// A picture the game draws into: colour and depth, and the framebuffer of the one render pass.
struct Target {
    Image colour, depth;
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    bool Ready() const { return framebuffer != VK_NULL_HANDLE; }
};

// A texture object of the game's (glGenTextures): an RGBA8 image with a full mip chain, of which `filled` levels hold
// pixels (1 after glTexImage2D and glTexSubImage2D, all after gluBuild2DMipmaps), and its sampling parameters.
struct Texture {
    Image image;
    uint32_t filled = 0;
    int32_t minFilter = GL_NEAREST_MIPMAP_LINEAR, magFilter = GL_LINEAR, wrapS = GL_REPEAT, wrapT = GL_REPEAT;
};

// The vertex every draw is made of (the shaders' input): 32 bytes, as the engine's own vertex format lays out its
// position, colour and texture coordinate (tb_drawprimitive's 0x1c4, which has the specular colour where the padding is).
struct Vertex {
    float x, y, z, w;
    uint8_t colour[4];
    uint32_t padding;
    float u, v;
};
static_assert(sizeof(Vertex) == 32, "Vertex layout");

// The frame's memory: host-visible buffers filled from the start at each frame (vertices, indices, the uploads' pixels).
struct Ring {
    std::vector<Buffer> chunks;
    size_t chunk = 0;
    VkDeviceSize offset = 0;
    void Reset() {
        chunk = 0;
        offset = 0;
    }
};

// The OpenGL state the game sets (draw.cpp); the parts glPushAttrib saves.
struct Matrix {
    double m[16];   // column-major, as OpenGL's
};

struct State {
    // GL_ENABLE_BIT and the groups each flag also belongs to
    bool texture2d = false, depthTest = false, alphaTest = false, blend = false, cullFace = false, fog = false,
         lighting = false, dither = true, scissor = false, stencil = false;
    // GL_DEPTH_BUFFER_BIT
    uint32_t depthFunc = GL_LESS;
    bool depthMask = true;
    // GL_COLOR_BUFFER_BIT
    uint32_t alphaFunc = GL_ALWAYS;
    float alphaRef = 0.0f;
    uint32_t blendSource = GL_ONE, blendDest = GL_ZERO;
    float clearColour[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    uint8_t colourMask = 0xf;
    uint32_t drawBuffer = GL_BACK;
    // GL_VIEWPORT_BIT
    int32_t viewport[4] = {0, 0, 0, 0};
    // GL_TRANSFORM_BIT
    uint32_t matrixMode = GL_MODELVIEW;
    // GL_TEXTURE_BIT
    uint32_t boundTexture = 0;
    uint32_t texEnvMode = GL_MODULATE;
    // GL_CURRENT_BIT
    float colour[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    float texCoord[2] = {0.0f, 0.0f};
    double rasterPos[3] = {0.0, 0.0, 0.0};   // window coordinates (x, y from the bottom left; z the depth, 0 to 1)
    bool rasterValid = true;
    // GL_PIXEL_MODE_BIT
    float zoomX = 1.0f, zoomY = 1.0f;
    uint32_t readBuffer = GL_BACK;
    // GL_LIGHTING_BIT, GL_HINT_BIT, GL_POLYGON_BIT: only remembered
    uint32_t shadeModel = GL_SMOOTH;
    uint32_t perspectiveHint = GL_DONT_CARE;
    uint32_t frontFace = GL_CCW;
};

// The client state (glPushClientAttrib): the pixel store and the arrays.
struct Array {
    int32_t size = 4;
    uint32_t type = GL_FLOAT;
    int32_t stride = 0;
    const uint8_t* pointer = nullptr;
};
struct ClientState {
    int32_t unpackAlignment = 4, unpackRowLength = 0, unpackSkipRows = 0, unpackSkipPixels = 0;
    int32_t packAlignment = 4, packRowLength = 0, packSkipRows = 0, packSkipPixels = 0;
    Array vertex{4, GL_FLOAT}, texCoord{2, GL_FLOAT}, colour{4, GL_UNSIGNED_BYTE};
    bool vertexArray = false, texCoordArray = false, colourArray = false;
};

// What the renderer is, all in one place (one instance, gRenderer).
struct Renderer {
    Api api;
    SDL_Window* window = nullptr;
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties properties{};
    bool named = false;   // TPW_VULKAN_DEVICE named the device (PickDevice)
    VkPhysicalDeviceMemoryProperties memoryProperties{};
    VkDevice device = VK_NULL_HANDLE;
    uint32_t queueFamily = 0;
    VkQueue queue = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    std::string description;

    // The swapchain and its presentation.
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    std::vector<VkImage> swapImages;
    std::vector<VkSemaphore> presented;   // one per swapchain image: the blit into it is done
    VkSemaphore acquired = VK_NULL_HANDLE;
    VkExtent2D swapExtent{};
    VkFormat swapFormat = VK_FORMAT_UNDEFINED;
    bool swapchainStale = false;
    int swapInterval = 1;

    // The frame's command buffer, run and waited for at each Flush.
    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkCommandBuffer commands = VK_NULL_HANDLE;
    VkCommandBuffer presentCommands = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    bool recording = false;
    Ring ring;
    Buffer readback;
    std::vector<std::function<void()>> garbage;   // destroyed once the frame that used them has run

    // The one render pass and its pipelines.
    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkShaderModule vertexShader = VK_NULL_HANDLE, fragmentShader = VK_NULL_HANDLE;
    std::unordered_map<uint64_t, VkPipeline> pipelines;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    uint32_t descriptorPoolSize = 0;
    std::unordered_map<uint64_t, VkDescriptorSet> descriptorSets;   // this frame's, by image view and sampler
    std::unordered_map<uint64_t, VkSampler> samplers;
    Texture white;   // 1 x 1 white: what an untextured draw samples

    // The pictures: the game's and the present copy, and which one the OpenGL calls draw into and read from.
    Target game, present;
    bool fit = false;
    int drawWide = 0, drawHigh = 0;
    Target* drawTarget = nullptr;
    bool inPass = false;
    Target* passTarget = nullptr;
    VkPipeline boundPipeline = VK_NULL_HANDLE;

    // The OpenGL state (draw.cpp).
    State state;
    ClientState client;
    std::vector<std::pair<uint32_t, State>> attribStack;
    std::vector<std::pair<uint32_t, ClientState>> clientStack;
    std::vector<Matrix> projection, modelview;
    std::unordered_map<uint32_t, Texture> textures;
    uint32_t nextTexture = 1;
    bool lockedArrays = false;
    int32_t lockFirst = 0, lockCount = 0;
    bool inBegin = false;
    uint32_t beginMode = 0;
    std::vector<Vertex> immediate;
    uint32_t error = GL_NO_ERROR;
    bool lost = false;   // a Vulkan call failed: nothing is drawn any more, and glGetError says so once

    bool Alive() const { return device != VK_NULL_HANDLE && !lost; }
};
extern Renderer gRenderer;
Renderer* Current();   // the renderer when it has a device, else NULL (the OpenGL calls then do nothing)

// ---- device.cpp
void Fail(const char* what, VkResult result);   // logs once and marks the renderer lost
VkCommandBuffer Cmd();                          // the frame's command buffer, begun if it was not
void EndPass();                                 // ends the render pass if one is open
void RunFrame();                                // runs what was recorded and waits for it; the frame's memory starts again
void EnsureLayout(Image& image, VkImageLayout layout);   // a barrier to the layout, outside the render pass
bool CreateImage(Image& image, uint32_t width, uint32_t height, uint32_t levels, VkFormat format, VkImageUsageFlags usage,
                 VkImageAspectFlags aspect);
void DestroyImage(Image& image);                // now
void DeferDestroyImage(const Image& image);     // after the frame has run
bool CreateBuffer(Buffer& buffer, VkDeviceSize size, VkBufferUsageFlags usage, bool hostVisible);
void DestroyBuffer(Buffer& buffer);
// Room in the frame's memory: the buffer and offset the data is at, and where to write it (NULL when it cannot be had).
uint8_t* RingAlloc(VkDeviceSize size, VkDeviceSize alignment, VkBuffer& buffer, VkDeviceSize& offset);
VkSampler SamplerFor(const Texture& texture);
VkDescriptorSet DescriptorSetFor(VkImageView view, VkSampler sampler);
VkPipeline PipelineFor(uint64_t key);
// Copies pixels (RGBA8, tightly packed) into a level of an image, with the layout left SHADER_READ_ONLY_OPTIMAL.
bool UploadPixels(Image& image, uint32_t level, int32_t x, int32_t y, int32_t width, int32_t height, const uint8_t* rgba);
// Reads a rectangle of an image's level 0 (RGBA8) into `out` (rows top down, as the image has them), running the frame first.
bool ReadImage(Image& image, int32_t x, int32_t y, int32_t width, int32_t height, std::vector<uint8_t>& out);
// Begins the render pass on the draw target if none is open (or another target's is), binding nothing.
bool EnsurePass();

// ---- draw.cpp
void ResetState(int wide, int high);   // OpenGL's initial state for a surface of that size
void DestroyTextures();

}  // namespace tpw::vkr
