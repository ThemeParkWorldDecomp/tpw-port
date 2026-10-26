# Vulkan-Headers

The Linux build's renderer (platform/vulkan/) draws with Vulkan. It needs only the API's C headers: the loader
(libvulkan.so.1, part of every Linux system with a Vulkan driver) is opened at run time through SDL
(SDL_Vulkan_LoadLibrary), so nothing of Vulkan's is linked, and no Vulkan SDK or development package has to be installed
to build.

- Project: Vulkan-Headers, the Khronos Group's Vulkan API headers.
- Origin: https://github.com/KhronosGroup/Vulkan-Headers, tag v1.3.275 (commit 217e93c664ec6704ec2d8c36fa116c1a4a1e2d40).
- Version: VK_HEADER_VERSION 275 (Vulkan 1.3.275). The renderer uses Vulkan 1.0 and VK_KHR_swapchain only.
- License: Apache License 2.0 or MIT, at the user's choice (LICENSE.md here, as published).
- Taken: 2026-10-26, by a clone of the tag (the release archive is not reachable from the maintainer's build machine).
- Kept: include/vulkan/vulkan.h, vulkan_core.h and vk_platform.h, the seven include/vk_video/ headers vulkan_core.h
  includes (the video codecs' types; nothing here uses them), and LICENSE.md. The platform headers (vulkan_xlib.h,
  vulkan_wayland.h and the rest) are not kept: SDL makes the window's surface, so the renderer never names a window system.

| File | Bytes | SHA-256 | git blob |
|---|---|---|---|
| include/vulkan/vulkan.h | 1555 | 52297513cc6b6cc8db2921aa849f72e3f55d678b3a58a2c3eb297c02ded7a837 | ef94006bb319d65f46ef228af8a8804939352dfc |
| include/vulkan/vulkan_core.h | 986840 | 3dba3ef8adaff8f24e9080e74d365fc64b5fe53ec8bda607c29c103e15888fcf | 7d8bb4c82d66729296fc25145d3fa5b0e0806bc9 |
| include/vulkan/vk_platform.h | 2828 | c4cabbcf699c90cde344095ae54da9435d117951aad114ba3a540c5997d7906f | 0ecd4f64719968f57fabbc40b5d884fb6483fd39 |
| include/vk_video/vulkan_video_codec_h264std.h | 13853 | 072ee6eac80e73d93bb4bfe0059f99e581fc8b55b1437236185bb4ff377914b1 | 6d27af37b73342388f3281a70c8af34efe1de567 |
| include/vk_video/vulkan_video_codec_h264std_decode.h | 2722 | e1708b3996a740eeedb79f51d0f0e379f9bcae13566538375acaf73301023b58 | 439cb885e71b7dcbde9b477b2bfb3fd4613bec10 |
| include/vk_video/vulkan_video_codec_h264std_encode.h | 6878 | 052a6e357be8d88bf4be0db1b3da5a9cc1a6011cf4b3533759ff588e1e26f9f1 | 9e24aa5d991d3b4388b4bd297d66faf46a698f7a |
| include/vk_video/vulkan_video_codec_h265std.h | 23416 | 0cbdfd6c45ee8ca902e9c961cd7e49a4d601d443ea0ef2a60a8496b127ed397c | d0a1bacbea378c7e0acc1ebcf2596da80525bfe9 |
| include/vk_video/vulkan_video_codec_h265std_decode.h | 2428 | 800946aa19d83d305d504e2e4141bc938f989e0cf8872cef97480238eefef1ea | 0178793e5111ac4eea8f26185bea2312ce645dd3 |
| include/vk_video/vulkan_video_codec_h265std_encode.h | 7462 | 7239f9f4e811546639111e562a8be1ec73ad5bef1c23d5fad80889f35e125e9c | ee34491f4e9c0a5fa3740193802ecd437f93473f |
| include/vk_video/vulkan_video_codecs_common.h | 660 | 78cf3c99d3d57afa7ab1782ec6018b4bcdbe273f2843d76e3375282db875b71e | 5e6ef1db48ee560e2004388f95d25c6a5bc6a19b |
| LICENSE.md | 458 | ac24e5ea920e4318e4d02c4086ae51f53cfb03feed06c18df1019e7ada1ec7bc | d6a0648b652ade1defb1eb481643d2f8d14828b5 |

The project publishes no checksums. The SHA-256 values are of our copies; each file's git blob id is the one the tag's
tree gives for it. No local changes. The renderer defines VK_NO_PROTOTYPES and includes <vulkan/vulkan.h> only.
