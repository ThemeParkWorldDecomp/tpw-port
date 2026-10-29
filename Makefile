# Builds the launcher on an Apple Silicon or Intel Mac, or on a 64-bit Linux (x86-64 or AArch64). `make` builds
# build/tpw_launcher; `make run` starts it; `make test` builds everything and runs the tests.
#
# The CMake release fetched when a Mac has no CMake 3.20 or newer (the macOS universal tarball, its SHA-256):
CMAKE_VERSION := 4.4.4
CMAKE_URL     := https://github.com/Kitware/CMake/releases/download/v4.4.4/cmake-4.4.4-macos-universal.tar.gz
CMAKE_SHA256  := 4b7b73704b1db9b374e5c9ab8e17ac6148b817b6396ee75cd94a852cbac9d305
#
# Nothing is installed on the Mac. The build needs Apple's Command Line Tools (git, make, the compiler, Python 3) and
# CMake. CMake is the one on the PATH when it is 3.20 or newer; otherwise the release above is downloaded into the build
# folder, checked against its SHA-256 and kept there (the whole CMake.app, about 90 MB to download), so `make clean`
# removes it with the rest of the build.
#
# On Linux the distribution's packages bring all of it, CMake included (the README's Building on Linux lists them, and
# install.sh installs them): nothing is fetched, and a CMake that is missing or older than 3.20 is said in plain words.
#
# Settings (make NAME=value):
#   BUILD     the build folder (default: build)
#   CMAKE     the cmake program to use (default: as above)
#   JOBS      how many files compile at once (default: the computer's cores)
#   RUN_ARGS  options for the launcher on `make run` (build/tpw_launcher --help lists them)
#
# Apple's make is GNU make 3.81: nothing here needs a newer one.

SYSTEM   := $(shell uname -s)
BUILD    := build
JOBS     := $(shell sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)
RUN_ARGS :=
LAUNCHER  = $(BUILD)/tpw_launcher
FETCHED   = $(BUILD)/cmake/CMake.app/Contents/bin/cmake

# CMAKE given on the command line is used as it is. Otherwise: cmake on the PATH when it is 3.20 or newer, else on a Mac
# the fetched one (on Linux, find-cmake then says what to install).
ifneq ($(origin CMAKE),command line)
  CMAKE := $(shell v=`cmake --version 2>/dev/null | sed -n 's/^cmake version \([0-9][0-9]*\)\.\([0-9][0-9]*\).*/\1 \2/p'`; \
             set -- $$v; [ -n "$$2" ] && { [ $$1 -gt 3 ] || { [ $$1 -eq 3 ] && [ $$2 -ge 20 ]; }; } && command -v cmake)
  ifeq ($(CMAKE),)
    ifeq ($(SYSTEM),Darwin)
      CMAKE := $(FETCHED)
      FETCH := yes
    else
      CMAKE := cmake
      FETCH := missing
    endif
  endif
endif

# The build runs make again (CMake's Makefiles): it must not see this make's settings (BUILD=..., CMAKE=...).
CLEAN_ENV := MAKEFLAGS= MFLAGS=

# ctest comes with cmake: beside it, or on the PATH when CMAKE is a bare name.
CTEST = $(if $(findstring /,$(CMAKE)),$(dir $(CMAKE))ctest,ctest)

.PHONY: all run test clean check-system check-tools find-cmake launcher

all: launcher
	@echo "==> Done. The launcher is $(LAUNCHER). Start it with: make run"

# Each step needs the one before it, so the order holds under make -j too.
check-system:
ifeq ($(SYSTEM),Darwin)
	@case "`uname -m`" in arm64|x86_64) ;; *) echo "This build needs an Apple Silicon or Intel Mac."; exit 1 ;; esac
	@echo "==> macOS `sw_vers -productVersion`, `uname -m`"
else ifeq ($(SYSTEM),Linux)
	@case "`uname -m`" in x86_64|aarch64|arm64) ;; *) echo "This build needs a 64-bit Linux on an x86-64 or AArch64 processor."; exit 1 ;; esac
	@echo "==> Linux, `(. /etc/os-release 2>/dev/null && echo "$${PRETTY_NAME:-$$NAME}")`, `uname -m`"
else
	@echo "This build is for macOS and Linux. On Windows, use build.cmd."
	@exit 1
endif

check-tools: check-system
ifeq ($(SYSTEM),Darwin)
	@if ! xcode-select -p >/dev/null 2>&1; then \
	  echo "Apple's Command Line Tools are missing. To install them:"; \
	  echo "  1. Run: xcode-select --install"; \
	  echo "  2. Click Install in the window that opens. Wait until it finishes."; \
	  echo "  3. Run make again."; \
	  exit 1; \
	fi
	@echo "==> Command Line Tools: `xcode-select -p`"
else
	@missing=""; \
	command -v c++ >/dev/null 2>&1 || missing="a C++ compiler (c++)"; \
	command -v python3 >/dev/null 2>&1 || missing="$${missing:+$$missing, }python3"; \
	if [ -n "$$missing" ]; then \
	  echo "Missing: $$missing. The README's Building on Linux lists the packages the build needs."; \
	  exit 1; \
	fi
	@echo "==> Compiler: `c++ --version 2>/dev/null | sed -n 1p`"
endif

find-cmake: check-tools
ifeq ($(FETCH),yes)
	@if [ -x "$(FETCHED)" ]; then \
	  echo "==> CMake $(CMAKE_VERSION): $(FETCHED) (fetched earlier)"; \
	  exit 0; \
	fi; \
	echo "==> No CMake 3.20 or newer on this Mac: fetching CMake $(CMAKE_VERSION) into $(BUILD)/cmake (about 90 MB, once)"; \
	rm -rf "$(BUILD)/cmake.tmp" "$(BUILD)/cmake"; \
	mkdir -p "$(BUILD)/cmake.tmp" || exit 1; \
	if ! curl -fL --retry 2 --progress-bar -o "$(BUILD)/cmake.tmp/cmake.tar.gz" "$(CMAKE_URL)"; then \
	  rm -rf "$(BUILD)/cmake.tmp"; rmdir "$(BUILD)" 2>/dev/null; \
	  echo "Could not download CMake from $(CMAKE_URL). Check the internet connection and run make again."; \
	  exit 1; \
	fi; \
	sum=`shasum -a 256 "$(BUILD)/cmake.tmp/cmake.tar.gz" | cut -d ' ' -f 1`; \
	if [ "$$sum" != "$(CMAKE_SHA256)" ]; then \
	  rm -rf "$(BUILD)/cmake.tmp"; rmdir "$(BUILD)" 2>/dev/null; \
	  echo "The downloaded CMake does not match the checksum in this Makefile, so it was deleted and nothing was kept."; \
	  echo "  expected $(CMAKE_SHA256)"; \
	  echo "  got      $$sum"; \
	  echo "Run make again. If it fails the same way, the download has changed: report it, do not use it."; \
	  exit 1; \
	fi; \
	tar -xzf "$(BUILD)/cmake.tmp/cmake.tar.gz" -C "$(BUILD)/cmake.tmp" && \
	mkdir -p "$(BUILD)/cmake" && \
	mv "$(BUILD)/cmake.tmp/cmake-$(CMAKE_VERSION)-macos-universal/CMake.app" "$(BUILD)/cmake/CMake.app" && \
	rm -rf "$(BUILD)/cmake.tmp" && \
	"$(FETCHED)" --version >/dev/null || { \
	  rm -rf "$(BUILD)/cmake.tmp" "$(BUILD)/cmake"; \
	  echo "Could not unpack the CMake download. Run make again."; \
	  exit 1; \
	}; \
	echo "==> Fetched CMake $(CMAKE_VERSION) from $(CMAKE_URL); its SHA-256 checksum matched"
else ifeq ($(FETCH),missing)
	@v=`cmake --version 2>/dev/null | sed -n 's/^cmake version //p'`; \
	if [ -n "$$v" ]; then echo "CMake $$v is too old: the build needs CMake 3.20 or newer."; else echo "CMake is not installed."; fi; \
	echo "Install your distribution's cmake package (Debian and Ubuntu: sudo apt install cmake; Fedora: sudo dnf install"; \
	echo "cmake; Arch: sudo pacman -S cmake), or name a CMake 3.20 or newer with CMAKE=, then run make again."; \
	exit 1
else
	@v=`"$(CMAKE)" --version 2>/dev/null | sed -n 's/^cmake version //p'`; \
	if [ -z "$$v" ]; then echo "CMAKE=$(CMAKE) is not a cmake program."; exit 1; fi; \
	echo "==> CMake $$v: $(CMAKE)"
endif

# Configures once. After that the build reconfigures by itself when a CMakeLists.txt changes.
launcher: find-cmake
	@if [ -f "$(BUILD)/CMakeCache.txt" ] && { [ -f "$(BUILD)/Makefile" ] || [ -f "$(BUILD)/build.ninja" ]; }; then \
	  echo "==> Build folder $(BUILD)/ is configured"; \
	else \
	  echo "==> Configuring the build in $(BUILD)/ (the first build takes a few minutes)"; \
	  $(CLEAN_ENV) "$(CMAKE)" -S . -B "$(BUILD)" || exit 1; \
	fi
	@echo "==> Building the launcher ($(JOBS) jobs at once)"
	@$(CLEAN_ENV) "$(CMAKE)" --build "$(BUILD)" -j $(JOBS) --target tpw_launcher

# Builds first when anything changed, then opens the launcher's window.
run: all
	@echo "==> Starting $(LAUNCHER) $(RUN_ARGS)"
	@"$(LAUNCHER)" $(RUN_ARGS)

# Builds the launcher, then everything else (the test programs), then runs the tests.
test: launcher
	@echo "==> Building the tests ($(JOBS) jobs at once)"
	@$(CLEAN_ENV) "$(CMAKE)" --build "$(BUILD)" -j $(JOBS)
	@echo "==> Running the tests"
	@$(CLEAN_ENV) "$(CTEST)" --test-dir "$(BUILD)" --output-on-failure

# Deletes the build folder and nothing else: the launcher, and CMake when it was fetched. The launcher's data folder
# (~/Library/Application Support/STP-TPW Launcher on a Mac, ~/.local/share/STP-TPW Launcher on Linux: your checked game
# files, your saves) is never touched.
clean:
	@case "$(BUILD)" in ""|/|.|..|"$$HOME") echo "Not removing BUILD=$(BUILD)."; exit 1 ;; esac
	@echo "==> Removing $(BUILD)/"
	@rm -rf "$(BUILD)"
