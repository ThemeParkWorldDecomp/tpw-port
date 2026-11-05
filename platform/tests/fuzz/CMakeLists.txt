# The online client's fuzz targets (libFuzzer with AddressSanitizer and UndefinedBehaviorSanitizer): what it does with the
# online server's bytes, which a player's own server or a server in the middle may set. A project of its own, so that the
# port's build never needs a compiler with libFuzzer (Apple's clang has none); configure it with LLVM's clang, e.g.
#   cmake -S platform/tests/fuzz -B <build> -DCMAKE_CXX_COMPILER=/opt/homebrew/opt/llvm/bin/clang++ \
#         -DCMAKE_OSX_SYSROOT="$(xcrun --show-sdk-path)"
#   cmake --build <build>
#   <build>/fuzz_park_check -max_total_time=600 <corpus folder>
# - fuzz_park_check: the checks a downloaded park passes before the game may load it (CheckPark);
# - fuzz_boss_replies: the replies of the login, news, city and upload services, read from a fake connection and answered;
# - fuzz_ftp_download: a connect and a park download against a fake FTP service.
cmake_minimum_required(VERSION 3.20)
project(tpw_fuzz CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
get_filename_component(TPW_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../.. ABSOLUTE)
find_package(ZLIB REQUIRED)

option(TPW_PORT_FIX "Build with the port's fixes, as the launcher is" ON)
if(TPW_PORT_FIX)
  add_compile_definitions(TPW_PORT_FIX)
endif()
add_compile_options(-g -O1 -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=undefined)
add_link_options(-fsanitize=address,undefined)
include_directories(${TPW_ROOT}/include ${TPW_ROOT}/platform)

# The platform files the targets need; each target compiles its own file under test into itself.
set(TPW_FUZZ_COMMON ${TPW_ROOT}/platform/arena.cpp ${CMAKE_CURRENT_SOURCE_DIR}/fuzz_stubs.cpp)

function(tpw_fuzz name)
  add_executable(${name} ${CMAKE_CURRENT_SOURCE_DIR}/${name}.cpp ${TPW_FUZZ_COMMON} ${ARGN})
  target_compile_options(${name} PRIVATE -fsanitize=fuzzer)
  target_link_options(${name} PRIVATE -fsanitize=fuzzer)
  target_link_libraries(${name} PRIVATE ZLIB::ZLIB)
  target_compile_definitions(${name} PRIVATE TPW_FUZZ_SCRATCH_DEFAULT="${CMAKE_CURRENT_BINARY_DIR}/scratch")
endfunction()

tpw_fuzz(fuzz_park_check ${TPW_ROOT}/platform/online_ftp.cpp ${TPW_ROOT}/platform/online_boss.cpp)
tpw_fuzz(fuzz_boss_replies ${TPW_ROOT}/platform/online_ftp.cpp)
tpw_fuzz(fuzz_ftp_download ${TPW_ROOT}/platform/online_boss.cpp)
