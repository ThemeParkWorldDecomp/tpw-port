#version 450
// tpw.frag: the one fragment shader of the Vulkan renderer (platform/vulkan/): OpenGL 1.1's fixed function as the game
// uses it. The colour is the vertex's, multiplied by the texel (GL_MODULATE) or replaced by it (GL_REPLACE) when
// texturing is on; then the alpha test, with the function and reference the game set (glAlphaFunc). Blending, the depth
// test and the colour mask are the pipeline's.
layout(location = 0) in vec4 vColour;
layout(location = 1) in vec2 vTexCoord;

layout(set = 0, binding = 0) uniform sampler2D page;

layout(push_constant) uniform Constants {
    mat4 mvp;
    ivec4 flags;
    vec4 params;
} constants;

layout(location = 0) out vec4 outColour;

void main() {
    vec4 colour = vColour;
    if (constants.flags.x != 0) {
        vec4 texel = texture(page, vTexCoord);
        colour = constants.flags.y == 1 ? texel : colour * texel;
    }
    // GL_NEVER .. GL_ALWAYS are 0x200 .. 0x207: 0 never, 1 less, 2 equal, 3 lequal, 4 greater, 5 notequal, 6 gequal, 7 always.
    int func = constants.flags.z;
    float ref = constants.params.x;
    bool pass = true;
    if (func == 0) pass = false;
    else if (func == 1) pass = colour.a < ref;
    else if (func == 2) pass = colour.a == ref;
    else if (func == 3) pass = colour.a <= ref;
    else if (func == 4) pass = colour.a > ref;
    else if (func == 5) pass = colour.a != ref;
    else if (func == 6) pass = colour.a >= ref;
    if (!pass) discard;
    outColour = colour;
}
