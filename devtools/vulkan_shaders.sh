#!/bin/sh
# Compiles the Vulkan renderer's shaders (platform/vulkan/shaders/*.vert, *.frag) to SPIR-V and writes each as a C array
# beside its source (tpw_vert.inc, tpw_frag.inc), which platform/vulkan/device.cpp includes: the build then needs no
# shader compiler. Run it after changing a shader; CI checks that the committed arrays match (build.yml).
#
#   sh devtools/vulkan_shaders.sh            writes the .inc files
#   sh devtools/vulkan_shaders.sh --check    exits 1 when a committed .inc differs from what the sources give
#
# Needs glslangValidator (the glslang-tools package on Debian and Ubuntu, or the Vulkan SDK), targeting Vulkan 1.0.
set -eu
here=$(cd "$(dirname "$0")/.." && pwd)
dir="$here/platform/vulkan/shaders"
check=0
[ "${1:-}" = "--check" ] && check=1
command -v glslangValidator >/dev/null 2>&1 || { echo "vulkan_shaders: glslangValidator is not installed"; exit 1; }
status=0
for src in "$dir"/*.vert "$dir"/*.frag; do
    name=$(basename "$src")
    stem=${name%.*}_${name##*.}
    out="$dir/$stem.inc"
    tmp=$(mktemp)
    glslangValidator -V --target-env vulkan1.0 --vn "k_$stem" -o "$tmp" "$src" >/dev/null
    {
        echo "// Written by devtools/vulkan_shaders.sh from $name (glslangValidator, SPIR-V for Vulkan 1.0). Do not edit."
        # glslang's header writes the array as "const uint32_t k_name[] = {"; the renderer wants it static and sized.
        sed -e 's/^\t*\/\/.*$//' -e '/^$/d' -e 's/^const uint32_t/static const uint32_t/' "$tmp"
    } > "$tmp.inc"
    rm -f "$tmp"
    if [ $check = 1 ]; then
        if ! cmp -s "$tmp.inc" "$out"; then
            echo "vulkan_shaders: $out differs from $name: run devtools/vulkan_shaders.sh"
            status=1
        fi
        rm -f "$tmp.inc"
    else
        mv "$tmp.inc" "$out"
        echo "wrote $out"
    fi
done
exit $status
