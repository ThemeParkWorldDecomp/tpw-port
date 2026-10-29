#version 450
// tpw.vert: the one vertex shader of the Vulkan renderer (platform/vulkan/). A vertex is what the game's OpenGL calls give:
// a position in clip space already (the engine projects its own vertices; the matrices are identity, or the cursor
// overlay's orthographic one), a colour and a texture coordinate. OpenGL's clip space is turned into Vulkan's: y points
// down there, and the depth range is 0..1 where OpenGL's is -1..1.
layout(location = 0) in vec4 inPosition;
layout(location = 1) in vec4 inColour;
layout(location = 2) in vec2 inTexCoord;

layout(push_constant) uniform Constants {
    mat4 mvp;       // projection * modelview, as the matrix stacks have them
    ivec4 flags;    // x: texturing on; y: the texture environment (0 modulate, 1 replace); z: the alpha test's function (8: off)
    vec4 params;    // x: the alpha test's reference value
} constants;

layout(location = 0) out vec4 vColour;
layout(location = 1) out vec2 vTexCoord;

void main() {
    vec4 p = constants.mvp * inPosition;
    gl_Position = vec4(p.x, -p.y, (p.z + p.w) * 0.5, p.w);
    vColour = inColour;
    vTexCoord = inTexCoord;
}
