// draw.cpp: the Vulkan renderer's OpenGL calls (renderer.h): the state the game sets and the drawing that state shapes
// (renderer_internal.h; the device and the frame are device.cpp's).
#include "renderer_internal.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "renderer.h"

namespace tpw::vkr {

namespace {

Renderer& R() { return gRenderer; }

// The call does nothing without a device (before the window's context is made, or after a failure); the state is still
// kept, so that what the game set before the context comes to it then.
bool Drawing() { return R().Alive() && R().drawTarget != nullptr && R().drawTarget->Ready(); }

void SetError(uint32_t error) {
    if (R().error == GL_NO_ERROR) R().error = error;
}

// ---- Matrices (column-major, as OpenGL's)

Matrix Identity() {
    Matrix m{};
    m.m[0] = m.m[5] = m.m[10] = m.m[15] = 1.0;
    return m;
}

Matrix Multiply(const Matrix& a, const Matrix& b) {
    Matrix out{};
    for (int column = 0; column < 4; ++column)
        for (int row = 0; row < 4; ++row) {
            double sum = 0.0;
            for (int k = 0; k < 4; ++k) sum += a.m[k * 4 + row] * b.m[column * 4 + k];
            out.m[column * 4 + row] = sum;
        }
    return out;
}

std::vector<Matrix>& CurrentStack() { return R().state.matrixMode == GL_PROJECTION ? R().projection : R().modelview; }

// The projection times the modelview, as the vertex shader wants it (floats).
void ModelViewProjection(float out[16]) {
    const Matrix mvp = Multiply(R().projection.back(), R().modelview.back());
    for (int i = 0; i < 16; ++i) out[i] = static_cast<float>(mvp.m[i]);
}

// ---- The pipeline's part of the state

uint32_t FactorIndex(uint32_t factor) {
    if (factor == GL_ZERO) return 0;
    if (factor == GL_ONE) return 1;
    if (factor >= GL_SRC_COLOR && factor <= GL_SRC_ALPHA_SATURATE) return 2 + (factor - GL_SRC_COLOR);
    return 1;
}

uint64_t PipelineKey(VkPrimitiveTopology topology) {
    const State& s = R().state;
    uint64_t key = static_cast<uint64_t>(topology) & 0xf;
    if (s.blend) key |= 1u << 4;
    key |= static_cast<uint64_t>(FactorIndex(s.blendSource)) << 5;
    key |= static_cast<uint64_t>(FactorIndex(s.blendDest)) << 9;
    if (s.depthTest) key |= 1u << 13;
    if (s.depthMask) key |= 1u << 14;
    const uint32_t func = s.depthFunc >= GL_NEVER && s.depthFunc <= GL_ALWAYS ? s.depthFunc - GL_NEVER : 1;
    key |= static_cast<uint64_t>(func) << 15;
    key |= static_cast<uint64_t>(s.colourMask & 0xf) << 18;
    return key;
}

struct PushConstants {
    float mvp[16];
    int32_t flags[4];
    float params[4];
};
static_assert(sizeof(PushConstants) == 96, "the push constants are 96 bytes (MakeLayouts)");

Texture* BoundTexture() {
    Renderer& r = R();
    if (r.state.boundTexture == 0) return nullptr;
    const auto found = r.textures.find(r.state.boundTexture);
    return found != r.textures.end() ? &found->second : nullptr;
}

// One draw of `count` vertices (already in the frame's memory), indexed when `indexCount` is not 0, with the viewport
// `viewport` (OpenGL's, from the bottom left), textured by `texture` (or the state's), with `envReplace` the texture
// environment forced to GL_REPEAT (glDrawPixels).
struct DrawSetup {
    VkPrimitiveTopology topology;
    const int32_t* viewport;
    Texture* texture;        // what is sampled, NULL for the state's choice
    bool forceTexture;       // texturing on whatever the state (glDrawPixels)
    bool envReplace;
};

bool DrawVertices(const DrawSetup& setup, const Vertex* vertices, uint32_t count, const void* indices, uint32_t indexCount,
                  VkIndexType indexType) {
    Renderer& r = R();
    if (!Drawing() || count == 0) return false;
    if (!EnsurePass()) return false;
    VkCommandBuffer cmd = Cmd();
    const VkPipeline pipeline = PipelineFor(PipelineKey(setup.topology));
    if (pipeline == VK_NULL_HANDLE) return false;
    if (pipeline != r.boundPipeline) {
        r.api.vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        r.boundPipeline = pipeline;
    }
    // The viewport: OpenGL's y counts from the bottom, Vulkan's from the top (the shader turns y over), and Vulkan takes
    // no viewport that starts outside the picture.
    const int32_t high = static_cast<int32_t>(r.drawTarget->colour.height), wide = static_cast<int32_t>(r.drawTarget->colour.width);
    int32_t vx = setup.viewport[0], vy = setup.viewport[1], vw = std::max(setup.viewport[2], 0), vh = std::max(setup.viewport[3], 0);
    int32_t top = high - vy - vh;
    if (vx < 0) {
        vw = std::max(vw + vx, 0);
        vx = 0;
    }
    if (top < 0) {
        vh = std::max(vh + top, 0);
        top = 0;
    }
    vw = std::min(vw, wide - vx);
    vh = std::min(vh, high - top);
    if (vw <= 0 || vh <= 0) return false;
    VkViewport viewport{static_cast<float>(vx), static_cast<float>(top), static_cast<float>(vw), static_cast<float>(vh), 0.0f, 1.0f};
    r.api.vkCmdSetViewport(cmd, 0, 1, &viewport);
    VkRect2D scissor{{0, 0}, {static_cast<uint32_t>(wide), static_cast<uint32_t>(high)}};
    r.api.vkCmdSetScissor(cmd, 0, 1, &scissor);
    // The texture: the one asked for, else the bound one while texturing is on, else the white pixel.
    Texture* texture = setup.texture;
    bool textured = setup.forceTexture;
    if (texture == nullptr && r.state.texture2d) {
        texture = BoundTexture();
        textured = texture != nullptr && texture->image.image != VK_NULL_HANDLE && texture->filled > 0;
    }
    if (!textured || texture == nullptr || texture->image.image == VK_NULL_HANDLE) {
        texture = &r.white;
        textured = false;
    }
    const VkSampler sampler = SamplerFor(*texture);
    const VkDescriptorSet set = DescriptorSetFor(texture->image.view, sampler);
    if (sampler == VK_NULL_HANDLE || set == VK_NULL_HANDLE) return false;
    r.api.vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r.pipelineLayout, 0, 1, &set, 0, nullptr);
    PushConstants constants{};
    ModelViewProjection(constants.mvp);
    constants.flags[0] = textured ? 1 : 0;
    constants.flags[1] = setup.envReplace || r.state.texEnvMode == GL_REPLACE ? 1 : 0;
    constants.flags[2] = r.state.alphaTest && r.state.alphaFunc >= GL_NEVER && r.state.alphaFunc <= GL_ALWAYS
                             ? static_cast<int32_t>(r.state.alphaFunc - GL_NEVER)
                             : 8;
    constants.params[0] = r.state.alphaRef;
    r.api.vkCmdPushConstants(cmd, r.pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof constants,
                             &constants);
    // The vertices and indices into the frame's memory.
    VkBuffer vertexBuffer = VK_NULL_HANDLE;
    VkDeviceSize vertexOffset = 0;
    uint8_t* room = RingAlloc(static_cast<VkDeviceSize>(count) * sizeof(Vertex), 16, vertexBuffer, vertexOffset);
    if (room == nullptr) return false;
    std::memcpy(room, vertices, static_cast<size_t>(count) * sizeof(Vertex));
    r.api.vkCmdBindVertexBuffers(cmd, 0, 1, &vertexBuffer, &vertexOffset);
    if (indexCount > 0) {
        const VkDeviceSize indexBytes = static_cast<VkDeviceSize>(indexCount) * (indexType == VK_INDEX_TYPE_UINT16 ? 2 : 4);
        VkBuffer indexBuffer = VK_NULL_HANDLE;
        VkDeviceSize indexOffset = 0;
        uint8_t* indexRoom = RingAlloc(indexBytes, 16, indexBuffer, indexOffset);
        if (indexRoom == nullptr) return false;
        std::memcpy(indexRoom, indices, static_cast<size_t>(indexBytes));
        r.api.vkCmdBindIndexBuffer(cmd, indexBuffer, indexOffset, indexType);
        r.api.vkCmdDrawIndexed(cmd, indexCount, 1, 0, 0, 0);
    } else {
        r.api.vkCmdDraw(cmd, count, 1, 0, 0);
    }
    return true;
}

// OpenGL's primitive as Vulkan's, with the ones Vulkan has no topology for (quads, polygons, loops) turned into index
// lists by `Expand` below. false for a mode OpenGL has not.
bool Topology(uint32_t mode, VkPrimitiveTopology& topology, bool& expand) {
    expand = false;
    switch (mode) {
        case GL_POINTS: topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST; return true;
        case GL_LINES: topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST; return true;
        case GL_LINE_STRIP: topology = VK_PRIMITIVE_TOPOLOGY_LINE_STRIP; return true;
        case GL_LINE_LOOP: topology = VK_PRIMITIVE_TOPOLOGY_LINE_STRIP; expand = true; return true;
        case GL_TRIANGLES: topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST; return true;
        case GL_TRIANGLE_STRIP: topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP; return true;
        case GL_TRIANGLE_FAN:
        case GL_POLYGON: topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN; return true;
        case GL_QUADS:
        case GL_QUAD_STRIP: topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST; expand = true; return true;
        default: return false;
    }
}

// The index list that draws `count` indexed vertices (indices[i], or i when NULL) of `mode` as `topology`.
std::vector<uint32_t> Expand(uint32_t mode, const uint32_t* indices, uint32_t count) {
    std::vector<uint32_t> out;
    auto at = [&](uint32_t i) { return indices != nullptr ? indices[i] : i; };
    if (mode == GL_QUADS) {
        for (uint32_t i = 0; i + 3 < count; i += 4) {
            const uint32_t quad[6] = {at(i), at(i + 1), at(i + 2), at(i), at(i + 2), at(i + 3)};
            out.insert(out.end(), quad, quad + 6);
        }
    } else if (mode == GL_QUAD_STRIP) {
        for (uint32_t i = 0; i + 3 < count; i += 2) {
            const uint32_t quad[6] = {at(i), at(i + 1), at(i + 3), at(i), at(i + 3), at(i + 2)};
            out.insert(out.end(), quad, quad + 6);
        }
    } else if (mode == GL_LINE_LOOP) {
        for (uint32_t i = 0; i < count; ++i) out.push_back(at(i));
        if (count > 1) out.push_back(at(0));
    }
    return out;
}

// A draw of `count` vertices of `mode`, with or without indices (uint32), through DrawVertices.
void DrawMode(uint32_t mode, const Vertex* vertices, uint32_t count, const uint32_t* indices, uint32_t indexCount) {
    VkPrimitiveTopology topology;
    bool expand = false;
    if (!Topology(mode, topology, expand)) {
        SetError(GL_INVALID_ENUM);   // (the engine's "bad primitive type" draws nothing on OpenGL too)
        return;
    }
    DrawSetup setup{topology, R().state.viewport, nullptr, false, false};
    if (expand) {
        const std::vector<uint32_t> expanded = Expand(mode, indices, indices != nullptr ? indexCount : count);
        if (expanded.empty()) return;
        DrawVertices(setup, vertices, count, expanded.data(), static_cast<uint32_t>(expanded.size()), VK_INDEX_TYPE_UINT32);
    } else if (indices != nullptr) {
        DrawVertices(setup, vertices, count, indices, indexCount, VK_INDEX_TYPE_UINT32);
    } else {
        DrawVertices(setup, vertices, count, nullptr, 0, VK_INDEX_TYPE_UINT32);
    }
}

uint8_t Byte(float value) { return static_cast<uint8_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f)); }

// ---- Pixels (glDrawPixels, glTexImage2D, glReadPixels): formats to and from RGBA8

size_t BytesPerPixel(uint32_t format) {
    switch (format) {
        case GL_RGB:
        case GL_BGR: return 3;
        case GL_RGBA:
        case GL_BGRA: return 4;
        default: return 4;
    }
}

// `pixels` as the client's unpack state describes them (alignment, row length, skips), as tightly packed RGBA8.
std::vector<uint8_t> UnpackRGBA(const void* pixels, int32_t width, int32_t height, uint32_t format, uint32_t type) {
    std::vector<uint8_t> out(static_cast<size_t>(width) * static_cast<size_t>(height) * 4);
    if (pixels == nullptr || width <= 0 || height <= 0) return out;
    const ClientState& c = R().client;
    const size_t bpp = BytesPerPixel(format);
    const size_t rowPixels = c.unpackRowLength > 0 ? static_cast<size_t>(c.unpackRowLength) : static_cast<size_t>(width);
    const size_t alignment = static_cast<size_t>(std::max(c.unpackAlignment, 1));
    const size_t stride = (rowPixels * bpp + alignment - 1) / alignment * alignment;
    const uint8_t* base = static_cast<const uint8_t*>(pixels) + static_cast<size_t>(std::max(c.unpackSkipRows, 0)) * stride +
                          static_cast<size_t>(std::max(c.unpackSkipPixels, 0)) * bpp;
    (void)type;   // GL_UNSIGNED_BYTE is the only type the game uses
    for (int32_t row = 0; row < height; ++row) {
        const uint8_t* from = base + static_cast<size_t>(row) * stride;
        uint8_t* to = out.data() + static_cast<size_t>(row) * static_cast<size_t>(width) * 4;
        for (int32_t column = 0; column < width; ++column, from += bpp, to += 4) {
            switch (format) {
                case GL_RGB: to[0] = from[0]; to[1] = from[1]; to[2] = from[2]; to[3] = 255; break;
                case GL_BGR: to[0] = from[2]; to[1] = from[1]; to[2] = from[0]; to[3] = 255; break;
                case GL_BGRA: to[0] = from[2]; to[1] = from[1]; to[2] = from[0]; to[3] = from[3]; break;
                default: to[0] = from[0]; to[1] = from[1]; to[2] = from[2]; to[3] = from[3]; break;
            }
        }
    }
    return out;
}

// ---- Textures

uint32_t LevelsFor(int32_t width, int32_t height) {
    uint32_t levels = 1;
    for (int32_t size = std::max(width, height); size > 1; size >>= 1) ++levels;
    return levels;
}

Texture& TextureObject(uint32_t name) {
    Renderer& r = R();
    auto found = r.textures.find(name);
    if (found == r.textures.end()) found = r.textures.emplace(name, Texture()).first;
    return found->second;
}

// Gives the texture an image of the size with its whole mip chain (its old one goes with the frame).
bool AllocateTexture(Texture& texture, int32_t width, int32_t height) {
    if (texture.image.image != VK_NULL_HANDLE) {
        if (static_cast<int32_t>(texture.image.width) == width && static_cast<int32_t>(texture.image.height) == height) return true;
        DeferDestroyImage(texture.image);
        texture.image = Image();
    }
    texture.filled = 0;
    if (width <= 0 || height <= 0) return false;
    const int largest = static_cast<int>(R().properties.limits.maxImageDimension2D);
    if (width > largest || height > largest) {
        SetError(GL_INVALID_VALUE);
        return false;
    }
    return CreateImage(texture.image, static_cast<uint32_t>(width), static_cast<uint32_t>(height), LevelsFor(width, height),
                       VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                       VK_IMAGE_ASPECT_COLOR_BIT);
}

// The next level of a mip chain: each pixel the mean of the 2 x 2 under it (a row or column that has no pair is used alone).
std::vector<uint8_t> HalfSize(const std::vector<uint8_t>& level, int32_t width, int32_t height, int32_t& outWide, int32_t& outHigh) {
    outWide = std::max(width / 2, 1);
    outHigh = std::max(height / 2, 1);
    std::vector<uint8_t> out(static_cast<size_t>(outWide) * static_cast<size_t>(outHigh) * 4);
    for (int32_t y = 0; y < outHigh; ++y) {
        for (int32_t x = 0; x < outWide; ++x) {
            const int32_t x0 = std::min(x * 2, width - 1), x1 = std::min(x * 2 + 1, width - 1);
            const int32_t y0 = std::min(y * 2, height - 1), y1 = std::min(y * 2 + 1, height - 1);
            for (int channel = 0; channel < 4; ++channel) {
                const uint32_t sum = level[(static_cast<size_t>(y0) * width + x0) * 4 + channel] +
                                     level[(static_cast<size_t>(y0) * width + x1) * 4 + channel] +
                                     level[(static_cast<size_t>(y1) * width + x0) * 4 + channel] +
                                     level[(static_cast<size_t>(y1) * width + x1) * 4 + channel];
                out[(static_cast<size_t>(y) * outWide + x) * 4 + channel] = static_cast<uint8_t>((sum + 2) / 4);
            }
        }
    }
    return out;
}

// ---- glPushAttrib's groups

bool InMask(uint32_t mask, uint32_t group) { return (mask & group) != 0; }

void RestoreAttrib(uint32_t mask, const State& saved) {
    State& s = R().state;
    const bool enables = InMask(mask, GL_ENABLE_BIT);
    if (enables || InMask(mask, GL_TEXTURE_BIT)) s.texture2d = saved.texture2d;
    if (enables || InMask(mask, GL_DEPTH_BUFFER_BIT)) s.depthTest = saved.depthTest;
    if (enables || InMask(mask, GL_COLOR_BUFFER_BIT)) {
        s.alphaTest = saved.alphaTest;
        s.blend = saved.blend;
        s.dither = saved.dither;
    }
    if (enables || InMask(mask, GL_POLYGON_BIT)) s.cullFace = saved.cullFace;
    if (enables || InMask(mask, GL_FOG_BIT)) s.fog = saved.fog;
    if (enables || InMask(mask, GL_LIGHTING_BIT)) s.lighting = saved.lighting;
    if (enables || InMask(mask, GL_SCISSOR_BIT)) s.scissor = saved.scissor;
    if (enables) s.stencil = saved.stencil;
    if (InMask(mask, GL_DEPTH_BUFFER_BIT)) {
        s.depthFunc = saved.depthFunc;
        s.depthMask = saved.depthMask;
    }
    if (InMask(mask, GL_COLOR_BUFFER_BIT)) {
        s.alphaFunc = saved.alphaFunc;
        s.alphaRef = saved.alphaRef;
        s.blendSource = saved.blendSource;
        s.blendDest = saved.blendDest;
        std::memcpy(s.clearColour, saved.clearColour, sizeof s.clearColour);
        s.colourMask = saved.colourMask;
        s.drawBuffer = saved.drawBuffer;
    }
    if (InMask(mask, GL_VIEWPORT_BIT)) std::memcpy(s.viewport, saved.viewport, sizeof s.viewport);
    if (InMask(mask, GL_TRANSFORM_BIT)) s.matrixMode = saved.matrixMode;
    if (InMask(mask, GL_TEXTURE_BIT)) {
        s.boundTexture = saved.boundTexture;
        s.texEnvMode = saved.texEnvMode;
    }
    if (InMask(mask, GL_CURRENT_BIT)) {
        std::memcpy(s.colour, saved.colour, sizeof s.colour);
        std::memcpy(s.texCoord, saved.texCoord, sizeof s.texCoord);
        std::memcpy(s.rasterPos, saved.rasterPos, sizeof s.rasterPos);
        s.rasterValid = saved.rasterValid;
    }
    if (InMask(mask, GL_PIXEL_MODE_BIT)) {
        s.zoomX = saved.zoomX;
        s.zoomY = saved.zoomY;
        s.readBuffer = saved.readBuffer;
    }
    if (InMask(mask, GL_LIGHTING_BIT)) s.shadeModel = saved.shadeModel;
    if (InMask(mask, GL_HINT_BIT)) s.perspectiveHint = saved.perspectiveHint;
    if (InMask(mask, GL_POLYGON_BIT)) s.frontFace = saved.frontFace;
}

// The vertex at index `i` of the client arrays, as the shader's vertex.
Vertex GatherVertex(uint32_t i) {
    const ClientState& c = R().client;
    const State& s = R().state;
    Vertex v{};
    v.x = v.y = v.z = 0.0f;
    v.w = 1.0f;
    auto component = [](const uint8_t* at, uint32_t type, int32_t index) -> float {
        if (type == GL_DOUBLE) {
            double d;
            std::memcpy(&d, at + index * 8, 8);
            return static_cast<float>(d);
        }
        float f;
        std::memcpy(&f, at + index * 4, 4);
        return f;
    };
    if (c.vertex.pointer != nullptr) {
        const int32_t size = std::clamp(c.vertex.size, 2, 4);
        const size_t stride = c.vertex.stride > 0 ? static_cast<size_t>(c.vertex.stride) : static_cast<size_t>(size) * (c.vertex.type == GL_DOUBLE ? 8 : 4);
        const uint8_t* at = c.vertex.pointer + stride * i;
        v.x = component(at, c.vertex.type, 0);
        v.y = component(at, c.vertex.type, 1);
        if (size > 2) v.z = component(at, c.vertex.type, 2);
        if (size > 3) v.w = component(at, c.vertex.type, 3);
    }
    if (c.colour.pointer != nullptr) {
        const int32_t size = std::clamp(c.colour.size, 3, 4);
        if (c.colour.type == GL_UNSIGNED_BYTE) {
            const size_t stride = c.colour.stride > 0 ? static_cast<size_t>(c.colour.stride) : static_cast<size_t>(size);
            const uint8_t* at = c.colour.pointer + stride * i;
            v.colour[0] = at[0];
            v.colour[1] = at[1];
            v.colour[2] = at[2];
            v.colour[3] = size > 3 ? at[3] : 255;
        } else {
            const size_t stride = c.colour.stride > 0 ? static_cast<size_t>(c.colour.stride) : static_cast<size_t>(size) * (c.colour.type == GL_DOUBLE ? 8 : 4);
            const uint8_t* at = c.colour.pointer + stride * i;
            for (int32_t k = 0; k < 4; ++k) v.colour[k] = k < size ? Byte(component(at, c.colour.type, k)) : 255;
        }
    } else {
        for (int k = 0; k < 4; ++k) v.colour[k] = Byte(s.colour[k]);
    }
    if (c.texCoord.pointer != nullptr) {
        const int32_t size = std::clamp(c.texCoord.size, 1, 4);
        const size_t stride = c.texCoord.stride > 0 ? static_cast<size_t>(c.texCoord.stride) : static_cast<size_t>(size) * (c.texCoord.type == GL_DOUBLE ? 8 : 4);
        const uint8_t* at = c.texCoord.pointer + stride * i;
        v.u = component(at, c.texCoord.type, 0);
        v.v = size > 1 ? component(at, c.texCoord.type, 1) : 0.0f;
    } else {
        v.u = s.texCoord[0];
        v.v = s.texCoord[1];
    }
    return v;
}

}  // namespace

// ---- renderer_internal.h

void ResetState(int wide, int high) {
    Renderer& r = R();
    r.state = State();
    r.state.viewport[2] = wide;
    r.state.viewport[3] = high;
    r.client = ClientState();
    r.attribStack.clear();
    r.clientStack.clear();
    r.projection.assign(1, Identity());
    r.modelview.assign(1, Identity());
    r.immediate.clear();
    r.inBegin = false;
}

void DestroyTextures() {
    Renderer& r = R();
    for (auto& entry : r.textures) DestroyImage(entry.second.image);
    r.textures.clear();
}

// ---- renderer.h: the OpenGL calls

void PushAttrib(uint32_t mask) {
    Renderer& r = R();
    if (r.attribStack.size() >= 16) {
        SetError(GL_INVALID_OPERATION);   // GL_STACK_OVERFLOW; the game pushes one deep
        return;
    }
    r.attribStack.emplace_back(mask, r.state);
}

void PopAttrib() {
    Renderer& r = R();
    if (r.attribStack.empty()) {
        SetError(GL_INVALID_OPERATION);
        return;
    }
    RestoreAttrib(r.attribStack.back().first, r.attribStack.back().second);
    r.attribStack.pop_back();
}

void PushClientAttrib(uint32_t mask) {
    Renderer& r = R();
    if (r.clientStack.size() >= 16) {
        SetError(GL_INVALID_OPERATION);
        return;
    }
    r.clientStack.emplace_back(mask, r.client);
}

void PopClientAttrib() {
    Renderer& r = R();
    if (r.clientStack.empty()) {
        SetError(GL_INVALID_OPERATION);
        return;
    }
    const uint32_t mask = r.clientStack.back().first;
    const ClientState& saved = r.clientStack.back().second;
    ClientState& c = r.client;
    if (InMask(mask, GL_CLIENT_PIXEL_STORE_BIT)) {
        c.unpackAlignment = saved.unpackAlignment;
        c.unpackRowLength = saved.unpackRowLength;
        c.unpackSkipRows = saved.unpackSkipRows;
        c.unpackSkipPixels = saved.unpackSkipPixels;
        c.packAlignment = saved.packAlignment;
        c.packRowLength = saved.packRowLength;
        c.packSkipRows = saved.packSkipRows;
        c.packSkipPixels = saved.packSkipPixels;
    }
    if (InMask(mask, GL_CLIENT_VERTEX_ARRAY_BIT)) {
        c.vertex = saved.vertex;
        c.texCoord = saved.texCoord;
        c.colour = saved.colour;
        c.vertexArray = saved.vertexArray;
        c.texCoordArray = saved.texCoordArray;
        c.colourArray = saved.colourArray;
    }
    r.clientStack.pop_back();
}

namespace {
void SetCap(uint32_t cap, bool on) {
    State& s = R().state;
    switch (cap) {
        case GL_TEXTURE_2D: s.texture2d = on; break;
        case GL_DEPTH_TEST: s.depthTest = on; break;
        case GL_ALPHA_TEST: s.alphaTest = on; break;
        case GL_BLEND: s.blend = on; break;
        case GL_CULL_FACE: s.cullFace = on; break;
        case GL_FOG: s.fog = on; break;
        case GL_LIGHTING: s.lighting = on; break;
        case GL_DITHER: s.dither = on; break;
        case GL_SCISSOR_TEST: s.scissor = on; break;
        case GL_STENCIL_TEST: s.stencil = on; break;
        default: break;   // (the enables the game never names: taken without complaint, as OpenGL would)
    }
}
}  // namespace

void Enable(uint32_t cap) { SetCap(cap, true); }
void Disable(uint32_t cap) { SetCap(cap, false); }

void PixelStorei(uint32_t name, int32_t value) {
    ClientState& c = R().client;
    switch (name) {
        case GL_UNPACK_ALIGNMENT: c.unpackAlignment = value; break;
        case GL_UNPACK_ROW_LENGTH: c.unpackRowLength = value; break;
        case GL_UNPACK_SKIP_ROWS: c.unpackSkipRows = value; break;
        case GL_UNPACK_SKIP_PIXELS: c.unpackSkipPixels = value; break;
        case GL_PACK_ALIGNMENT: c.packAlignment = value; break;
        case GL_PACK_ROW_LENGTH: c.packRowLength = value; break;
        case GL_PACK_SKIP_ROWS: c.packSkipRows = value; break;
        case GL_PACK_SKIP_PIXELS: c.packSkipPixels = value; break;
        default: SetError(GL_INVALID_ENUM); break;
    }
}

// The raster position: (x, y, z, 1) through the matrices to clip space, then to window coordinates with the viewport;
// a point outside the clip volume leaves the position invalid, and glDrawPixels then draws nothing.
void RasterPos3f(float x, float y, float z) {
    State& s = R().state;
    const Matrix mvp = Multiply(R().projection.back(), R().modelview.back());
    const double in[4] = {x, y, z, 1.0};
    double clip[4];
    for (int row = 0; row < 4; ++row) {
        clip[row] = 0.0;
        for (int k = 0; k < 4; ++k) clip[row] += mvp.m[k * 4 + row] * in[k];
    }
    const double w = clip[3];
    if (w == 0.0 || std::fabs(clip[0]) > std::fabs(w) || std::fabs(clip[1]) > std::fabs(w) || std::fabs(clip[2]) > std::fabs(w)) {
        s.rasterValid = false;
        return;
    }
    s.rasterPos[0] = (clip[0] / w + 1.0) * 0.5 * s.viewport[2] + s.viewport[0];
    s.rasterPos[1] = (clip[1] / w + 1.0) * 0.5 * s.viewport[3] + s.viewport[1];
    s.rasterPos[2] = (clip[2] / w + 1.0) * 0.5;
    s.rasterValid = true;
}

void WindowPos2i(int32_t x, int32_t y) {
    State& s = R().state;
    s.rasterPos[0] = x;
    s.rasterPos[1] = y;
    s.rasterPos[2] = 0.0;
    s.rasterValid = true;
}

void PixelZoom(float x, float y) {
    R().state.zoomX = x;
    R().state.zoomY = y;
}

// The pixels as a quad of window pixels from the raster position, rows going up (or down with a negative zoom), each
// pixel zoomed, textured by the picture itself (point sampled, as glPixelZoom replicates and drops pixels), under the
// current fragment state (the alpha test, blending, the depth test at the raster position's depth).
void DrawPixels(int32_t width, int32_t height, uint32_t format, uint32_t type, const void* pixels) {
    Renderer& r = R();
    if (width < 0 || height < 0) {
        SetError(GL_INVALID_VALUE);
        return;
    }
    if (!Drawing() || !r.state.rasterValid || width == 0 || height == 0 || pixels == nullptr) return;
    const std::vector<uint8_t> rgba = UnpackRGBA(pixels, width, height, format, type);
    Texture picture;
    picture.minFilter = GL_NEAREST;
    picture.magFilter = GL_NEAREST;
    picture.wrapS = picture.wrapT = GL_CLAMP;
    if (!CreateImage(picture.image, static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1, VK_FORMAT_R8G8B8A8_UNORM,
                     VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_IMAGE_ASPECT_COLOR_BIT))
        return;
    picture.filled = 1;
    if (!UploadPixels(picture.image, 0, 0, 0, width, height, rgba.data())) {
        DestroyImage(picture.image);
        return;
    }
    const State& s = r.state;
    const double wide = r.drawWide, high = r.drawHigh;
    const double x0 = s.rasterPos[0], y0 = s.rasterPos[1];
    const double x1 = x0 + width * static_cast<double>(s.zoomX), y1 = y0 + height * static_cast<double>(s.zoomY);
    // Clip coordinates against the whole picture (glDrawPixels is not placed by the viewport), with the depth as the
    // vertex shader will map it back (z = 2 depth - 1, w = 1).
    const float depth = static_cast<float>(s.rasterPos[2] * 2.0 - 1.0);
    auto corner = [&](double x, double y, float u, float v) {
        Vertex out{};
        out.x = static_cast<float>(2.0 * x / wide - 1.0);
        out.y = static_cast<float>(2.0 * y / high - 1.0);
        out.z = depth;
        out.w = 1.0f;
        out.colour[0] = out.colour[1] = out.colour[2] = out.colour[3] = 255;
        out.u = u;
        out.v = v;
        return out;
    };
    const Vertex quad[4] = {corner(x0, y0, 0.0f, 0.0f), corner(x1, y0, 1.0f, 0.0f), corner(x1, y1, 1.0f, 1.0f),
                            corner(x0, y1, 0.0f, 1.0f)};
    const uint32_t indices[6] = {0, 1, 2, 0, 2, 3};
    const int32_t whole[4] = {0, 0, r.drawWide, r.drawHigh};
    DrawSetup setup{VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, whole, &picture, true, true};
    DrawVertices(setup, quad, 4, indices, 6, VK_INDEX_TYPE_UINT32);
    DeferDestroyImage(picture.image);
}

uint32_t GetError() {
    const uint32_t error = R().error;
    R().error = GL_NO_ERROR;
    return error;
}

const uint8_t* GetString(uint32_t name) {
    static const char* const kVendor = "tpw-port";
    static const char* const kRenderer = "tpw-port Vulkan renderer";
    static const char* const kVersion = "1.1 tpw-port";
    static const char* const kExtensions = "GL_EXT_compiled_vertex_array";
    switch (name) {
        case GL_VENDOR: return reinterpret_cast<const uint8_t*>(kVendor);
        case GL_RENDERER: return reinterpret_cast<const uint8_t*>(kRenderer);
        case GL_VERSION: return reinterpret_cast<const uint8_t*>(kVersion);
        case GL_EXTENSIONS: return reinterpret_cast<const uint8_t*>(kExtensions);
        default: SetError(GL_INVALID_ENUM); return nullptr;
    }
}

void GetIntegerv(uint32_t name, int32_t* values) {
    if (values == nullptr) return;
    const Renderer& r = R();
    const int largest = r.device != VK_NULL_HANDLE ? static_cast<int>(std::min(r.properties.limits.maxImageDimension2D,
                                                                                 r.properties.limits.maxFramebufferWidth))
                                                   : 0;
    switch (name) {
        case GL_MAX_TEXTURE_SIZE:
        case GL_MAX_RENDERBUFFER_SIZE_EXT: values[0] = largest; break;
        case GL_MAX_VIEWPORT_DIMS: values[0] = values[1] = largest; break;
        default: SetError(GL_INVALID_ENUM); break;
    }
}

void ClearColor(float red, float green, float blue, float alpha) {
    float* colour = R().state.clearColour;
    colour[0] = std::clamp(red, 0.0f, 1.0f);
    colour[1] = std::clamp(green, 0.0f, 1.0f);
    colour[2] = std::clamp(blue, 0.0f, 1.0f);
    colour[3] = std::clamp(alpha, 0.0f, 1.0f);
}

// The whole picture (the scissor test is never on in the game) to the clear colour and depth 1.
void Clear(uint32_t mask) {
    Renderer& r = R();
    if (!Drawing() || (mask & (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT)) == 0) return;
    if (!EnsurePass()) return;
    VkClearAttachment attachments[2];
    uint32_t count = 0;
    if ((mask & GL_COLOR_BUFFER_BIT) != 0) {
        attachments[count].aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        attachments[count].colorAttachment = 0;
        std::memcpy(attachments[count].clearValue.color.float32, r.state.clearColour, sizeof(float) * 4);
        ++count;
    }
    if ((mask & GL_DEPTH_BUFFER_BIT) != 0) {
        attachments[count].aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        attachments[count].colorAttachment = 0;
        attachments[count].clearValue.depthStencil = {1.0f, 0};
        ++count;
    }
    VkClearRect rect{};
    rect.rect = {{0, 0}, {r.drawTarget->colour.width, r.drawTarget->colour.height}};
    rect.layerCount = 1;
    r.api.vkCmdClearAttachments(Cmd(), count, attachments, 1, &rect);
}

void DepthMask(uint8_t flag) { R().state.depthMask = flag != 0; }

void DepthFunc(uint32_t func) {
    if (func < GL_NEVER || func > GL_ALWAYS) {
        SetError(GL_INVALID_ENUM);
        return;
    }
    R().state.depthFunc = func;
}

void ColorMask(uint8_t red, uint8_t green, uint8_t blue, uint8_t alpha) {
    R().state.colourMask = static_cast<uint8_t>((red != 0 ? 1 : 0) | (green != 0 ? 2 : 0) | (blue != 0 ? 4 : 0) | (alpha != 0 ? 8 : 0));
}

void EnableClientState(uint32_t array) {
    ClientState& c = R().client;
    switch (array) {
        case GL_VERTEX_ARRAY: c.vertexArray = true; break;
        case GL_COLOR_ARRAY: c.colourArray = true; break;
        case GL_TEXTURE_COORD_ARRAY: c.texCoordArray = true; break;
        default: SetError(GL_INVALID_ENUM); break;
    }
}

void Viewport(int32_t x, int32_t y, int32_t width, int32_t height) {
    if (width < 0 || height < 0) {
        SetError(GL_INVALID_VALUE);
        return;
    }
    int32_t* viewport = R().state.viewport;
    viewport[0] = x;
    viewport[1] = y;
    viewport[2] = width;
    viewport[3] = height;
}

void MatrixMode(uint32_t mode) {
    if (mode != GL_MODELVIEW && mode != GL_PROJECTION && mode != GL_TEXTURE) {
        SetError(GL_INVALID_ENUM);
        return;
    }
    R().state.matrixMode = mode;
}

void LoadIdentity() {
    if (R().state.matrixMode == GL_TEXTURE) return;
    CurrentStack().back() = Identity();
}

void PushMatrix() {
    if (R().state.matrixMode == GL_TEXTURE) return;
    std::vector<Matrix>& stack = CurrentStack();
    if (stack.size() >= 32) {
        SetError(GL_INVALID_OPERATION);
        return;
    }
    stack.push_back(stack.back());
}

void PopMatrix() {
    if (R().state.matrixMode == GL_TEXTURE) return;
    std::vector<Matrix>& stack = CurrentStack();
    if (stack.size() <= 1) {
        SetError(GL_INVALID_OPERATION);
        return;
    }
    stack.pop_back();
}

void Ortho(double left, double right, double bottom, double top, double near, double far) {
    if (R().state.matrixMode == GL_TEXTURE || left == right || bottom == top || near == far) {
        if (left == right || bottom == top || near == far) SetError(GL_INVALID_VALUE);
        return;
    }
    Matrix ortho = Identity();
    ortho.m[0] = 2.0 / (right - left);
    ortho.m[5] = 2.0 / (top - bottom);
    ortho.m[10] = -2.0 / (far - near);
    ortho.m[12] = -(right + left) / (right - left);
    ortho.m[13] = -(top + bottom) / (top - bottom);
    ortho.m[14] = -(far + near) / (far - near);
    Matrix& current = CurrentStack().back();
    current = Multiply(current, ortho);
}

void FrontFace(uint32_t mode) { R().state.frontFace = mode; }
void ShadeModel(uint32_t mode) { R().state.shadeModel = mode; }
void Hint(uint32_t target, uint32_t mode) {
    if (target == GL_PERSPECTIVE_CORRECTION_HINT) R().state.perspectiveHint = mode;
}

void AlphaFunc(uint32_t func, float ref) {
    if (func < GL_NEVER || func > GL_ALWAYS) {
        SetError(GL_INVALID_ENUM);
        return;
    }
    R().state.alphaFunc = func;
    R().state.alphaRef = std::clamp(ref, 0.0f, 1.0f);
}

void BlendFunc(uint32_t source, uint32_t dest) {
    auto valid = [](uint32_t factor) {
        return factor == GL_ZERO || factor == GL_ONE || (factor >= GL_SRC_COLOR && factor <= GL_SRC_ALPHA_SATURATE);
    };
    if (!valid(source) || !valid(dest)) {
        SetError(GL_INVALID_ENUM);
        return;
    }
    R().state.blendSource = source;
    R().state.blendDest = dest;
}

void Flush() {}   // (everything recorded reaches the device at the present, or at the next read; nothing waits here)

void Finish() {
    if (R().Alive()) RunFrame();   // the frame so far runs and is waited for
}

void GenTextures(int32_t n, uint32_t* textures) {
    Renderer& r = R();
    if (n < 0) {
        SetError(GL_INVALID_VALUE);
        return;
    }
    for (int32_t i = 0; i < n; ++i) textures[i] = r.nextTexture++;
}

void DeleteTextures(int32_t n, const uint32_t* textures) {
    Renderer& r = R();
    if (n < 0) {
        SetError(GL_INVALID_VALUE);
        return;
    }
    for (int32_t i = 0; i < n; ++i) {
        const auto found = r.textures.find(textures[i]);
        if (found == r.textures.end()) continue;
        if (found->second.image.image != VK_NULL_HANDLE) DeferDestroyImage(found->second.image);
        r.textures.erase(found);
        if (r.state.boundTexture == textures[i]) r.state.boundTexture = 0;
    }
}

void BindTexture(uint32_t target, uint32_t texture) {
    if (target != GL_TEXTURE_2D) {
        SetError(GL_INVALID_ENUM);
        return;
    }
    R().state.boundTexture = texture;
    if (texture != 0) TextureObject(texture);
}

void TexEnvi(uint32_t target, uint32_t name, int32_t value) {
    if (target != GL_TEXTURE_ENV || name != GL_TEXTURE_ENV_MODE) {
        SetError(GL_INVALID_ENUM);
        return;
    }
    R().state.texEnvMode = static_cast<uint32_t>(value);
}

void TexParameteri(uint32_t target, uint32_t name, int32_t value) {
    if (target != GL_TEXTURE_2D) {
        SetError(GL_INVALID_ENUM);
        return;
    }
    if (R().state.boundTexture == 0) return;   // (the default texture: nothing the game draws with)
    Texture& texture = TextureObject(R().state.boundTexture);
    switch (name) {
        case GL_TEXTURE_MIN_FILTER: texture.minFilter = value; break;
        case GL_TEXTURE_MAG_FILTER: texture.magFilter = value; break;
        case GL_TEXTURE_WRAP_S: texture.wrapS = value; break;
        case GL_TEXTURE_WRAP_T: texture.wrapT = value; break;
        default: SetError(GL_INVALID_ENUM); break;
    }
}

void TexImage2D(uint32_t target, int32_t level, int32_t internalFormat, int32_t width, int32_t height, int32_t border,
                uint32_t format, uint32_t type, const void* pixels) {
    (void)internalFormat;   // every texture is RGBA8 here (a GL_RGBA4 page is kept at 8 bits a channel, as today's drivers keep it)
    if (target != GL_TEXTURE_2D) {
        SetError(GL_INVALID_ENUM);
        return;
    }
    if (width < 0 || height < 0 || border != 0 || level < 0) {
        SetError(GL_INVALID_VALUE);
        return;
    }
    Renderer& r = R();
    if (r.state.boundTexture == 0 || !r.Alive()) return;
    Texture& texture = TextureObject(r.state.boundTexture);
    if (level != 0) {   // a level of a chain the game fills itself: not a path the game takes (gluBuild2DMipmaps does it)
        if (texture.image.image != VK_NULL_HANDLE && static_cast<uint32_t>(level) < texture.image.levels && pixels != nullptr) {
            const std::vector<uint8_t> rgba = UnpackRGBA(pixels, width, height, format, type);
            UploadPixels(texture.image, static_cast<uint32_t>(level), 0, 0, width, height, rgba.data());
        }
        return;
    }
    if (!AllocateTexture(texture, width, height)) return;
    if (pixels != nullptr) {
        const std::vector<uint8_t> rgba = UnpackRGBA(pixels, width, height, format, type);
        if (UploadPixels(texture.image, 0, 0, 0, width, height, rgba.data())) texture.filled = 1;
    } else {
        // No pixels: the level is there but holds nothing yet; it is sampled as black until something is uploaded.
        std::vector<uint8_t> zero(static_cast<size_t>(width) * static_cast<size_t>(height) * 4, 0);
        if (UploadPixels(texture.image, 0, 0, 0, width, height, zero.data())) texture.filled = 1;
    }
}

void TexSubImage2D(uint32_t target, int32_t level, int32_t x, int32_t y, int32_t width, int32_t height, uint32_t format,
                   uint32_t type, const void* pixels) {
    if (target != GL_TEXTURE_2D) {
        SetError(GL_INVALID_ENUM);
        return;
    }
    Renderer& r = R();
    if (r.state.boundTexture == 0 || !r.Alive() || pixels == nullptr) return;
    Texture& texture = TextureObject(r.state.boundTexture);
    if (texture.image.image == VK_NULL_HANDLE || level < 0 || static_cast<uint32_t>(level) >= texture.image.levels) {
        SetError(GL_INVALID_OPERATION);
        return;
    }
    const int32_t levelWide = std::max(static_cast<int32_t>(texture.image.width) >> level, 1);
    const int32_t levelHigh = std::max(static_cast<int32_t>(texture.image.height) >> level, 1);
    if (x < 0 || y < 0 || width < 0 || height < 0 || x + width > levelWide || y + height > levelHigh) {
        SetError(GL_INVALID_VALUE);
        return;
    }
    if (width == 0 || height == 0) return;
    const std::vector<uint8_t> rgba = UnpackRGBA(pixels, width, height, format, type);
    UploadPixels(texture.image, static_cast<uint32_t>(level), x, y, width, height, rgba.data());
    if (level == 0 && texture.filled == 0) texture.filled = 1;
    if (level != 0 && texture.filled > 1) texture.filled = std::max(texture.filled, static_cast<uint32_t>(level) + 1);
}

// gluBuild2DMipmaps: the picture and every smaller level, each half the one before (a 2 x 2 mean, which is what GLU's box
// filter gives for the powers of two the game's pages are).
int32_t BuildMipmaps(uint32_t target, int32_t internalFormat, int32_t width, int32_t height, uint32_t format, uint32_t type,
                     const void* pixels) {
    (void)internalFormat;
    if (target != GL_TEXTURE_2D) return GL_INVALID_ENUM;
    if (width < 1 || height < 1 || pixels == nullptr) return GL_INVALID_VALUE;
    Renderer& r = R();
    if (r.state.boundTexture == 0 || !r.Alive()) return 0;
    Texture& texture = TextureObject(r.state.boundTexture);
    if (!AllocateTexture(texture, width, height)) return GL_OUT_OF_MEMORY;
    std::vector<uint8_t> level = UnpackRGBA(pixels, width, height, format, type);
    int32_t wide = width, high = height;
    for (uint32_t i = 0; i < texture.image.levels; ++i) {
        if (!UploadPixels(texture.image, i, 0, 0, wide, high, level.data())) return GL_OUT_OF_MEMORY;
        if (i + 1 < texture.image.levels) {
            int32_t nextWide = 0, nextHigh = 0;
            level = HalfSize(level, wide, high, nextWide, nextHigh);
            wide = nextWide;
            high = nextHigh;
        }
    }
    texture.filled = texture.image.levels;
    return 0;
}

void VertexPointer(int32_t size, uint32_t type, int32_t stride, const void* pointer) {
    R().client.vertex = {size, type, stride, static_cast<const uint8_t*>(pointer)};
}
void TexCoordPointer(int32_t size, uint32_t type, int32_t stride, const void* pointer) {
    R().client.texCoord = {size, type, stride, static_cast<const uint8_t*>(pointer)};
}
void ColorPointer(int32_t size, uint32_t type, int32_t stride, const void* pointer) {
    R().client.colour = {size, type, stride, static_cast<const uint8_t*>(pointer)};
}

void LockArrays(int32_t first, int32_t count) {
    Renderer& r = R();
    r.lockedArrays = true;
    r.lockFirst = first;
    r.lockCount = count;
}

void UnlockArrays() { R().lockedArrays = false; }

void DrawElements(uint32_t mode, int32_t count, uint32_t type, const void* indices) {
    Renderer& r = R();
    if (count < 0) {
        SetError(GL_INVALID_VALUE);
        return;
    }
    if (!Drawing() || count == 0 || indices == nullptr || r.client.vertex.pointer == nullptr) return;
    // The indices as 32-bit, and the vertices they name (the locked range when the arrays are locked, as the engine
    // locks exactly the vertices of each primitive).
    std::vector<uint32_t> list(static_cast<size_t>(count));
    uint32_t highest = 0;
    for (int32_t i = 0; i < count; ++i) {
        uint32_t index = 0;
        switch (type) {
            case GL_UNSIGNED_BYTE: index = static_cast<const uint8_t*>(indices)[i]; break;
            case GL_UNSIGNED_SHORT: index = static_cast<const uint16_t*>(indices)[i]; break;
            case GL_UNSIGNED_INT: index = static_cast<const uint32_t*>(indices)[i]; break;
            default: SetError(GL_INVALID_ENUM); return;
        }
        list[static_cast<size_t>(i)] = index;
        highest = std::max(highest, index);
    }
    uint32_t vertexCount = highest + 1;
    if (r.lockedArrays && r.lockFirst == 0 && r.lockCount > 0) vertexCount = std::max(vertexCount, static_cast<uint32_t>(r.lockCount));
    std::vector<Vertex> vertices(vertexCount);
    for (uint32_t i = 0; i < vertexCount; ++i) vertices[i] = GatherVertex(i);
    DrawMode(mode, vertices.data(), vertexCount, list.data(), static_cast<uint32_t>(count));
}

// The game's two calls that name the window's buffers (GL_FRONT, GL_BACK) draw and read the picture whatever they name:
// there is one picture, the game's, and its last frame is what the screen shows (what GL_FRONT would be read as).
void DrawBuffer(uint32_t mode) { R().state.drawBuffer = mode; }
void ReadBuffer(uint32_t mode) { R().state.readBuffer = mode; }

void ReadPixels(int32_t x, int32_t y, int32_t width, int32_t height, uint32_t format, uint32_t type, void* pixels) {
    Renderer& r = R();
    if (width < 0 || height < 0) {
        SetError(GL_INVALID_VALUE);
        return;
    }
    if (type != GL_UNSIGNED_BYTE || (format != GL_RGBA && format != GL_RGB && format != GL_BGRA && format != GL_BGR)) {
        SetError(GL_INVALID_ENUM);
        return;
    }
    if (!Drawing() || pixels == nullptr || width == 0 || height == 0) return;
    Target& target = *r.drawTarget;
    const int32_t wide = static_cast<int32_t>(target.colour.width), high = static_cast<int32_t>(target.colour.height);
    // The rows OpenGL counts from the bottom are the image's from the top; what lies outside the picture is left as it is.
    const int32_t left = std::max(x, 0), right = std::min(x + width, wide);
    const int32_t bottom = std::max(y, 0), top = std::min(y + height, high);
    if (left >= right || bottom >= top) return;
    std::vector<uint8_t> read;
    if (!ReadImage(target.colour, left, high - top, right - left, top - bottom, read)) return;
    const ClientState& c = r.client;
    const size_t bpp = BytesPerPixel(format);
    const size_t rowPixels = c.packRowLength > 0 ? static_cast<size_t>(c.packRowLength) : static_cast<size_t>(width);
    const size_t alignment = static_cast<size_t>(std::max(c.packAlignment, 1));
    const size_t stride = (rowPixels * bpp + alignment - 1) / alignment * alignment;
    uint8_t* base = static_cast<uint8_t*>(pixels) + static_cast<size_t>(std::max(c.packSkipRows, 0)) * stride +
                    static_cast<size_t>(std::max(c.packSkipPixels, 0)) * bpp;
    const int32_t readWide = right - left;
    for (int32_t row = bottom; row < top; ++row) {
        // Image row (high - 1 - row) of the whole picture is row (top - 1 - row) of what was read.
        const uint8_t* from = read.data() + static_cast<size_t>(top - 1 - row) * static_cast<size_t>(readWide) * 4;
        uint8_t* to = base + static_cast<size_t>(row - y) * stride + static_cast<size_t>(left - x) * bpp;
        for (int32_t column = 0; column < readWide; ++column, from += 4, to += bpp) {
            switch (format) {
                case GL_RGB: to[0] = from[0]; to[1] = from[1]; to[2] = from[2]; break;
                case GL_BGR: to[0] = from[2]; to[1] = from[1]; to[2] = from[0]; break;
                case GL_BGRA: to[0] = from[2]; to[1] = from[1]; to[2] = from[0]; to[3] = from[3]; break;
                default: to[0] = from[0]; to[1] = from[1]; to[2] = from[2]; to[3] = from[3]; break;
            }
        }
    }
}

void Begin(uint32_t mode) {
    Renderer& r = R();
    if (r.inBegin) {
        SetError(GL_INVALID_OPERATION);
        return;
    }
    VkPrimitiveTopology topology;
    bool expand;
    if (!Topology(mode, topology, expand)) {
        SetError(GL_INVALID_ENUM);
        return;
    }
    r.inBegin = true;
    r.beginMode = mode;
    r.immediate.clear();
}

void End() {
    Renderer& r = R();
    if (!r.inBegin) {
        SetError(GL_INVALID_OPERATION);
        return;
    }
    r.inBegin = false;
    if (!r.immediate.empty()) DrawMode(r.beginMode, r.immediate.data(), static_cast<uint32_t>(r.immediate.size()), nullptr, 0);
    r.immediate.clear();
}

void Color4ub(uint8_t red, uint8_t green, uint8_t blue, uint8_t alpha) {
    float* colour = R().state.colour;
    colour[0] = red / 255.0f;
    colour[1] = green / 255.0f;
    colour[2] = blue / 255.0f;
    colour[3] = alpha / 255.0f;
}

void Color4f(float red, float green, float blue, float alpha) {
    float* colour = R().state.colour;
    colour[0] = red;
    colour[1] = green;
    colour[2] = blue;
    colour[3] = alpha;
}

void TexCoord2f(float s, float t) {
    R().state.texCoord[0] = s;
    R().state.texCoord[1] = t;
}

void Vertex4f(float x, float y, float z, float w) {
    Renderer& r = R();
    if (!r.inBegin) {
        SetError(GL_INVALID_OPERATION);
        return;
    }
    Vertex v{};
    v.x = x;
    v.y = y;
    v.z = z;
    v.w = w;
    for (int k = 0; k < 4; ++k) v.colour[k] = Byte(r.state.colour[k]);
    v.u = r.state.texCoord[0];
    v.v = r.state.texCoord[1];
    r.immediate.push_back(v);
}

}  // namespace tpw::vkr
