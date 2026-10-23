# FFmpeg 9.0.2, trimmed to what the game's films need and built as one static library from the sources in this folder
# (see NOTE.md). Ours, not FFmpeg's: FFmpeg's configure and makefiles are not here and never run. config/ (written by
# gen_config.py, kept in the tree) stands in for what configure writes: no assembly, no intrinsics, no threads, no
# hardware decoding, no external library but zlib, so the same C builds on macOS (arm64, x86_64), Linux and Windows.
cmake_minimum_required(VERSION 3.20)

# The films are QuickTime movies (the mov demuxer, read through the file protocol) with Sorenson Video 1 (svq1) pictures
# and IMA4 (adpcm_ima_qt) sound; libswscale makes the RGB24 pictures platform/quicktime.cpp draws. Only the files a link
# of those pulls in are listed (and kept).
add_library(tpw_ffmpeg STATIC
  libavutil/aes.c libavutil/aes_ctr.c libavutil/ambient_viewing_environment.c libavutil/avsscanf.c
  libavutil/avstring.c libavutil/bprint.c libavutil/buffer.c libavutil/channel_layout.c libavutil/container_fifo.c
  libavutil/cpu.c libavutil/crc.c libavutil/csp.c libavutil/dict.c libavutil/display.c libavutil/dovi_meta.c
  libavutil/encryption_info.c libavutil/error.c libavutil/eval.c libavutil/fifo.c libavutil/file_open.c
  libavutil/frame.c libavutil/half2float.c libavutil/hwcontext.c libavutil/iamf.c libavutil/imgutils.c
  libavutil/integer.c libavutil/log.c libavutil/log2_tab.c libavutil/mastering_display_metadata.c
  libavutil/mathematics.c libavutil/mem.c libavutil/opt.c libavutil/parseutils.c libavutil/pixdesc.c
  libavutil/random_seed.c libavutil/rational.c libavutil/refstruct.c libavutil/reverse.c libavutil/samplefmt.c
  libavutil/sha.c libavutil/side_data.c libavutil/slicethread.c libavutil/spherical.c libavutil/stereo3d.c
  libavutil/time.c libavutil/timecode.c libavutil/timecode_internal.c libavutil/timestamp.c libavutil/tree.c
  libavutil/utils.c

  libavcodec/ac3_channel_layout_tab.c libavcodec/adpcm.c libavcodec/adpcm_data.c libavcodec/allcodecs.c
  libavcodec/avcodec.c libavcodec/bitstream_filters.c libavcodec/bsf/null.c libavcodec/bsf.c libavcodec/codec_desc.c
  libavcodec/codec_par.c libavcodec/decode.c libavcodec/encode.c libavcodec/exif.c libavcodec/get_buffer.c
  libavcodec/h263data.c libavcodec/hpeldsp.c libavcodec/mpeg4audio.c libavcodec/mpeg4audio_sample_rates.c
  libavcodec/mpegaudiotabs.c libavcodec/options.c libavcodec/packet.c libavcodec/parser.c libavcodec/parsers.c
  libavcodec/profiles.c libavcodec/raw.c libavcodec/svq1.c libavcodec/svq1dec.c libavcodec/threadprogress.c
  libavcodec/tiff_common.c libavcodec/to_upper4.c libavcodec/utils.c libavcodec/vlc.c

  libavformat/allformats.c libavformat/avformat.c libavformat/avio.c libavformat/aviobuf.c libavformat/demux.c
  libavformat/demux_utils.c libavformat/dovi_isom.c libavformat/dvdclut.c libavformat/file.c libavformat/format.c
  libavformat/id3v1.c libavformat/id3v2.c libavformat/isom.c libavformat/isom_tags.c libavformat/metadata.c
  libavformat/mov.c libavformat/mov_chan.c libavformat/mov_esds.c libavformat/options.c libavformat/packet_list.c
  libavformat/protocols.c libavformat/qtpalette.c libavformat/replaygain.c libavformat/riff.c libavformat/riffdec.c
  libavformat/seek.c libavformat/url.c libavformat/urldecode.c libavformat/utils.c

  libswscale/alphablend.c libswscale/cms.c libswscale/csputils.c libswscale/filters.c libswscale/format.c
  libswscale/framepool.c libswscale/gamma.c libswscale/graph.c libswscale/hscale.c libswscale/hscale_fast_bilinear.c
  libswscale/input.c libswscale/lut3d.c libswscale/ops.c libswscale/ops_chain.c libswscale/ops_dispatch.c
  libswscale/ops_memcpy.c libswscale/ops_optimizer.c libswscale/options.c libswscale/output.c libswscale/rational64.c
  libswscale/rgb2rgb.c libswscale/slice.c libswscale/swscale.c libswscale/swscale_unscaled.c libswscale/uops.c
  libswscale/uops_backend.c libswscale/utils.c libswscale/vscale.c libswscale/yuv2rgb.c)

# The library's users include <libavcodec/avcodec.h> and the like from this folder; config/public holds the one generated
# public header (libavutil/avconfig.h). SYSTEM, so that the game's own warning flags are not raised in FFmpeg's headers.
target_include_directories(tpw_ffmpeg SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/config/public)
# config.h and config_components.h are FFmpeg's own business: only its sources see them.
target_include_directories(tpw_ffmpeg PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/config)
# What FFmpeg's configure and makefiles define for every source file of the libraries.
target_compile_definitions(tpw_ffmpeg PRIVATE HAVE_AV_CONFIG_H _ISOC11_SOURCE _FILE_OFFSET_BITS=64 _LARGEFILE_SOURCE
  _POSIX_C_SOURCE=200112 _XOPEN_SOURCE=600 $<$<PLATFORM_ID:Darwin>:_DARWIN_C_SOURCE>)
# C17 and configure's own optimization flags for gcc and clang, in every build type: FFmpeg leaves calls to the
# components it does not build in branches like `if (CONFIG_X)` and counts on the optimizer to drop them, and -O3,
# -fno-math-errno and -fno-signed-zeros are what a stock build of FFmpeg (the one the films were checked against) uses.
# FFmpeg's C is not ours to warn about (-w).
set_target_properties(tpw_ffmpeg PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF
  POSITION_INDEPENDENT_CODE ON)
target_compile_options(tpw_ffmpeg PRIVATE -O3 -fno-math-errno -fno-signed-zeros -w)
# configure's workaround for older gcc's vectorizer.
if(CMAKE_C_COMPILER_ID STREQUAL "GNU" AND CMAKE_C_COMPILER_VERSION VERSION_LESS 13)
  target_compile_options(tpw_ffmpeg PRIVATE -fno-tree-vectorize)
endif()
# The films' movie headers are compressed (a cmov atom): the mov demuxer inflates them with zlib, the game's own. On
# Linux the math functions are a library of their own (libm); macOS has them in libSystem.
target_link_libraries(tpw_ffmpeg PUBLIC ZLIB::ZLIB $<$<NOT:$<PLATFORM_ID:Darwin>>:m>)
