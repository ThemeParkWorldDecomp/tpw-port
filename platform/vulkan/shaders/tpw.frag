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
    // The test compares 8-bit values, as OpenGL's fixed-function alpha test did (the reference and the fragment's alpha
    // both converted to the colour buffer's precision). The game draws its cut-out textures (the river banks, a ride's
    // fur) with GL_GEQUAL 1.0: a filtered alpha of 0.9999, which bilinear filtering gives between texels that are all
    // 1.0, passes that test rounded, and fails it as a float, in stripes wherever the filter's weights fall short.
    int func = constants.flags.z;
    int a = int(clamp(colour.a, 0.0, 1.0) * 255.0 + 0.5);
    int ref = int(clamp(constants.params.x, 0.0, 1.0) * 255.0 + 0.5);
    bool pass = true;
    if (func == 0) pass = false;
    else if (func == 1) pass = a < ref;
    else if (func == 2) pass = a == ref;
    else if (func == 3) pass = a <= ref;
    else if (func == 4) pass = a > ref;
    else if (func == 5) pass = a != ref;
    else if (func == 6) pass = a >= ref;
    if (!pass) discard;
    outColour = colour;
}
