# Yo-kai Watch 1 Recompiled build
# macOS / Linux / Windows: SDL3 + OpenGL 4.5 Compatibility + FFmpeg + OpenGL3 ImGui frontend.
UNAME := $(shell uname -s)
comma := ,
ifneq ($(filter MINGW% MSYS% CYGWIN% Windows_NT,$(UNAME) $(OS)),)
PLATFORM := windows
else ifeq ($(UNAME),Linux)
PLATFORM := linux
else
PLATFORM := macos
endif

# Linux: ARCH (x86_64 / aarch64, default this machine's) picks the target, separately from PLATFORM. The generated code
# differs per arch (AArch64 hosts run the guest's FP/SIMD as inline asm, others use the portable C of simd.h), so a
# cross build has its own generated-<arch>/ and build/<arch>/; SYSROOT is the target's root filesystem (headers, libs).
GEN      ?= generated
ifeq ($(PLATFORM),linux)
HOSTARCH := $(patsubst arm64,aarch64,$(shell uname -m))
ARCH     ?= $(HOSTARCH)
ifneq ($(ARCH),$(HOSTARCH))
CROSS    := --target=$(ARCH)-linux-gnu $(if $(SYSROOT),--sysroot=$(SYSROOT))
GEN      := generated-$(ARCH)
B        ?= build/$(ARCH)
endif
GENSIMD  := --simd $(if $(filter aarch64,$(ARCH)),asm,c)
endif

# B: build dir. MACOS_MIN: oldest macOS to run on (default: this one). DEPS: prefix with SDL3 / FFmpeg instead of
# Homebrew's (make deps). `make app` sets all three (build/mac15, 15.0, build/deps); plain `make` uses none.
B         ?= build
MACOS_MIN ?=
DEPS      ?=
MINFLAG  := $(if $(MACOS_MIN),-mmacosx-version-min=$(MACOS_MIN))
CC       := clang $(MINFLAG) $(CROSS)
CXX      := clang++ $(MINFLAG) $(CROSS)
IMGUI    := third_party/imgui
GENFLAGS := -O1 -fno-strict-aliasing -Wno-return-type -Wno-unused-label -fvisibility=hidden -Iruntime

# The game code (made by `make gen` from your own game files) is its own library next to the app (runtime/gamelib.h);
# the app itself holds none of it
# (strip: with no generated code the space between the two would be all that's left, and $(if) takes a lone space as
# true. No comment after the value: make keeps the spaces before it)
GEN_SRC  := $(strip $(wildcard $(GEN)/main/*.c) $(wildcard $(GEN)/sdk/*.c) $(wildcard $(GEN)/subsdk0/*.c))
GEN_OBJ  := $(patsubst %.c,$(B)/obj/%.o,$(patsubst $(GEN)/%,%,$(GEN_SRC))) $(B)/obj/tools/gamelib_entry.o
RT_SRC   := $(wildcard runtime/*.c runtime/*.cpp) $(wildcard runtime/hle/*.c) $(wildcard runtime/gl/*.c runtime/gl/*.cpp) \
            $(wildcard runtime/services/*.c) runtime/platform/$(PLATFORM).c
RT_HDRS  := $(wildcard runtime/*.h runtime/gl/*.h runtime/game_addresses/*.h) $(wildcard runtime/platform/*.h) \
            $(wildcard runtime/ui/*.h)
UI_SRC   := $(wildcard runtime/ui/*.mm) $(wildcard runtime/ui/*.cpp)
UI_OBJ   := $(patsubst runtime/%.cpp,$(B)/rt/%.o,$(patsubst runtime/%.mm,$(B)/rt/%.o,$(UI_SRC)))
IMGUI_OBJ := $(B)/imgui/imgui.o $(B)/imgui/imgui_draw.o $(B)/imgui/imgui_tables.o $(B)/imgui/imgui_widgets.o \
             $(B)/imgui/imgui_impl_sdl3.o $(B)/imgui/imgui_impl_opengl3.o

ifeq ($(PLATFORM),macos)
SDL      := $(or $(DEPS),/opt/homebrew/opt/sdl3)
FFMPEG   := $(or $(DEPS),/opt/homebrew/opt/ffmpeg)
EXE      := $(B)/yokaiwatch1recomp
DEPINC   := -I$(SDL)/include -I$(FFMPEG)/include
LDFLAGS  := -L$(SDL)/lib -lSDL3 -Wl,-rpath,$(SDL)/lib -L$(FFMPEG)/lib -lavformat -lavcodec -lavutil -lswscale -Wl,-rpath,$(FFMPEG)/lib \
            -framework CoreFoundation -framework Foundation -framework AppKit -framework OpenGL -framework ImageIO
BACKEND_SRC :=
BACKEND_OBJ :=
UIFLAGS  := -fobjc-arc
GAMELIB  := $(B)/yw1game.dylib
GAMELIB_LDFLAGS := -dynamiclib -undefined dynamic_lookup  # (the runtime functions it calls: the app's own)
GAMELIB_SHARED := -shared -Wl,-undefined,dynamic_lookup  # (the same, for the start screen's build: zig cc)
else ifeq ($(PLATFORM),linux)
EXE      := $(B)/yokaiwatch1recomp
DEPINC   := $(if $(DEPS),-I$(DEPS)/include)
ifeq ($(ARCH),x86_64)
GENARCH  ?= -march=x86-64-v3  # as on Windows: AVX2 + FMA for the lifted FP/SIMD code (checked at startup)
endif
GENFLAGS += $(GENARCH) -fPIC
OSFLAGS  := -Wno-format  # (%llx for uint64_t, which is unsigned long here: the same size)
# -rdynamic: crash backtraces name host functions (runtime/platform/linux.c)
LDFLAGS  := -fuse-ld=lld -rdynamic $(if $(DEPS),-L$(DEPS)/lib) -lSDL3 -lavformat -lavcodec -lavutil -lswscale \
            -lGL -lpthread -ldl -lm
BACKEND_SRC :=
BACKEND_OBJ :=
UIFLAGS  := -x c++  # the .mm UI sources are plain C++ apart from __APPLE__ blocks
GAMELIB  := $(B)/yw1game.so
GAMELIB_LDFLAGS := -shared -fuse-ld=lld  # (the runtime functions it calls: the app's own, -rdynamic)
GAMELIB_SHARED := -shared
else
EXE      := $(B)/YokaiWatch1Recomp.exe
COMPAT_EXE := $(B)/lasrecomp.exe
DEPINC   := $(if $(DEPS),-I$(DEPS)/include)
OSFLAGS  := -D_FILE_OFFSET_BITS=64 -D_POSIX_THREAD_SAFE_FUNCTIONS -D_CRT_RAND_S -DWIN32_LEAN_AND_MEAN -DNOMINMAX \
            $(if $(RELEASE),-DLARECOMP_ZIP_BUILD)  # (the log's [info] line: the release zip)
GENARCH  ?= -march=x86-64-v3  # AVX2 + FMA for the lifted FP/SIMD code (simd.h); override for older CPUs
GENFLAGS += -D_FILE_OFFSET_BITS=64 $(GENARCH)
LDFLAGS  := -fuse-ld=lld -Wl,--stack,16777216 $(if $(DEPS),-L$(DEPS)/lib) -lSDL3 -lavformat -lavcodec -lavutil -lswscale \
            -lopengl32 -lws2_32 -lwinmm -lole32 -lshell32 -lcomdlg32 -luuid \
            $(if $(RELEASE),-Wl$(comma)--subsystem$(comma)windows -Wl$(comma)--strip-debug)  # release: no console window (log -> yokaiwatch1recomp.log), no debug info
BACKEND_SRC :=
BACKEND_OBJ := $(B)/rt/platform/windows_rc.o  # the .exe's icon (Explorer, taskbar)
WINDRES  ?= windres
UIFLAGS  := -x c++ $(OSFLAGS)  # the .mm UI sources are plain C++ apart from __APPLE__ blocks
GAMELIB  := $(B)/yw1game.dll
# the runtime functions it calls: exported by the .exe, linked through its import library
LDFLAGS  += -Wl,--export-all-symbols -Wl,--out-implib,$(B)/YokaiWatch1Recomp.dll.a
GAMELIB_LDFLAGS := -shared -fuse-ld=lld $(B)/YokaiWatch1Recomp.dll.a
GAMELIB_SHARED := -shared  # (+ the import library: runtime/gamebuild.c)
endif

# GAMELIB_ID: what the game code library is built from (the generator, the headers it is compiled against, the
# flags). A library with another id is rebuilt by the start screen, never loaded.
GAMELIB_SRCS := tools/game_addresses.py runtime/game_addresses/versions.h tools/gen.py tools/lifter.py tools/analyze.py \
                tools/nso.py tools/passes.py tools/simd_c.py tools/gamelib_entry.c runtime/hle/hle_symbols.txt \
                runtime/cpu.h runtime/simd.h runtime/simd_fp.h runtime/gamelib.h
GAMELIB_ID := $(shell (cat $(GAMELIB_SRCS); echo '$(GENFLAGS)') | (shasum -a 256 2>/dev/null || sha256sum) | cut -c1-16)
# The library's own id adds the game's main build ID (what `make gen` generated from: $(GEN)/main/main_build_id.txt),
# so game code for another game version is never loaded (runtime/gamelib.h gamelib_full_id)
GAMELIB_BUILD := $(shell cut -c1-16 $(GEN)/main/main_build_id.txt 2>/dev/null)
GAMELIB_FULL_ID := $(GAMELIB_ID)-$(or $(GAMELIB_BUILD),unknown)
# GAMELIB_BASE_ID: what every compiled piece of it depends on (the headers, the flags, the pinned compiler). The same
# base keeps the start screen's earlier build pieces: only the pieces whose generated C changed are compiled again
# (runtime/gamebuild.c). Another base: everything is compiled again.
GAMELIB_BASE_SRCS := runtime/cpu.h runtime/simd.h runtime/simd_fp.h runtime/gamelib.h tools/fetch_zig.sh
GAMELIB_BASE_ID := $(shell (cat $(GAMELIB_BASE_SRCS); echo '$(GENFLAGS) $(GAMELIB_SHARED)') | \
                     (shasum -a 256 2>/dev/null || sha256sum) | cut -c1-16)

RT_OBJ   := $(patsubst runtime/%.cpp,$(B)/rt/%.o,$(patsubst runtime/%.c,$(B)/rt/%.o,$(RT_SRC)))
# no path of this machine in what ships: debug info and __FILE__ name the checkout as "." (Windows: its C:/ form too)
PATHMAP  := -ffile-prefix-map=$(CURDIR)=. $(if $(filter windows,$(PLATFORM)),-ffile-prefix-map=$(shell cygpath -m '$(CURDIR)')=.)
RTFLAGS  := -O2 -g -fno-strict-aliasing -Wall -Wno-unused-function -finput-charset=UTF-8 -fexec-charset=UTF-8 -Iruntime $(DEPINC) $(OSFLAGS) $(PATHMAP)
CXXFLAGS := -O2 -g -std=c++17 -finput-charset=UTF-8 -fexec-charset=UTF-8 -I$(IMGUI) -I$(IMGUI)/backends -Iruntime $(DEPINC) $(PATHMAP)

all: $(EXE) $(if $(and $(filter windows,$(PLATFORM)),$(if $(RELEASE),,1)),$(COMPAT_EXE)) $(if $(GEN_SRC),$(GAMELIB))

$(B)/lasrecomp.exe: tools/win_compat_shim.c | $(EXE)
	@mkdir -p $(dir $@)
	$(CC) -O2 -municode $< -o $@

$(B)/obj/%.o: $(GEN)/%.c runtime/cpu.h runtime/simd.h runtime/simd_fp.h
	@mkdir -p $(dir $@)
	@$(CC) $(GENFLAGS) -c $< -o $@

$(B)/obj/tools/gamelib_entry.o: tools/gamelib_entry.c $(GAMELIB_SRCS) $(wildcard $(GEN)/main/main_build_id.txt)
	@mkdir -p $(dir $@)
	$(CC) $(GENFLAGS) -DGAMELIB_ID='"$(GAMELIB_FULL_ID)"' $(if $(wildcard $(GEN)/subsdk0/*.c),,-DNO_SUBSDK0) -c $< -o $@
$(B)/rt/gamelib.o: $(GAMELIB_SRCS)
$(B)/rt/gamelib.o: RTFLAGS += -DGAMELIB_ID='"$(GAMELIB_ID)"'
# the start screen's build of it (runtime/gamebuild.c) uses the same flags
$(B)/rt/gamebuild.o: $(GAMELIB_SRCS) $(GAMELIB_BASE_SRCS) Makefile
$(B)/rt/gamebuild.o: RTFLAGS += -DGAMELIB_ID='"$(GAMELIB_ID)"' -DGAMELIB_BASE_ID='"$(GAMELIB_BASE_ID)"' \
                                -DGAMELIB_CFLAGS='"$(strip $(filter-out -Iruntime,$(GENFLAGS)))"' \
                                -DGAMELIB_SHARED='"$(GAMELIB_SHARED)"'

$(B)/rt/%.o: runtime/%.c $(RT_HDRS)
	@mkdir -p $(dir $@)
	$(CC) $(RTFLAGS) -c $< -o $@

$(B)/rt/%.o: runtime/%.cpp $(RT_HDRS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(OSFLAGS) -c $< -o $@

$(B)/rt/%.o: runtime/%.m $(RT_HDRS)
	@mkdir -p $(dir $@)
	$(CC) $(RTFLAGS) -fobjc-arc -c $< -o $@

# linked to a temp file, then renamed over: a copy of the game still running keeps its (old) file - relinking in
# place gets running copies killed by macOS (code signature of the changed pages). The temp file keeps the real name
# (in a folder of its own): on Windows the import library records the name the game code library loads it by.
$(EXE): $(RT_OBJ) $(BACKEND_OBJ) $(UI_OBJ) $(IMGUI_OBJ)
	@mkdir -p $(dir $@)link
	$(CXX) -o $(dir $@)link/$(notdir $@) $^ $(LDFLAGS)
	mv -f $(dir $@)link/$(notdir $@) $@

# its id file (<library>.id) says what it was built from without loading it (runtime/gamelib.c)
$(GAMELIB): $(GEN_OBJ) | $(EXE)
	$(CC) $(GAMELIB_LDFLAGS) -o $@.tmp $^
	mv -f $@.tmp $@
	echo $(GAMELIB_FULL_ID) > $@.id

$(B)/rt/ui/%.o: runtime/ui/%.mm $(RT_HDRS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(UIFLAGS) -c $< -o $@

$(B)/imgui/%.o: $(IMGUI)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(B)/imgui/imgui_impl_%.o: $(IMGUI)/backends/imgui_impl_%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Python: tools/venv (macOS, Linux: bin/python, Windows: Scripts/python.exe)
PY := $(firstword $(wildcard tools/venv/bin/python) $(wildcard tools/venv/Scripts/python.exe))
EXEFS ?= extracted/exefs

gen:
	cd tools && ../$(PY) gen.py sdk "$(abspath $(EXEFS))/sdk" 0x7004000000 "$(abspath $(GEN))/sdk" --hle ../runtime/hle/hle_symbols.txt $(GENSIMD) --generic
	cd tools && ../$(PY) gen.py main "$(abspath $(EXEFS))/main" 0x7000000000 "$(abspath $(GEN))/main" $(GENSIMD) --generic

gen-objs: $(GEN_OBJ)

# Runtime objects only (no generated code, no link): quick compile check
runtime-objs: $(RT_OBJ) $(BACKEND_OBJ) $(UI_OBJ)

$(B)/rt/platform/windows_rc.o: runtime/platform/windows.rc docs/icon/icon.ico runtime/platform/windows.manifest
	@mkdir -p $(dir $@)
	$(WINDRES) -I docs/icon -I runtime/platform -O coff $< -o $@

VERSION  := $(shell sed -n 's/^\#define LARECOMP_VERSION "\(.*\)"/\1/p' runtime/ui/launcher.mm)

APP_MIN  := 15.0
APP_B    := build/mac15
deps:
	MACOS_MIN=$(APP_MIN) tools/build_deps_macos.sh
app:
	@[ -f build/deps/lib/libSDL3.0.dylib ] || { echo "make deps first"; exit 1; }
	$(MAKE) B=$(APP_B) MACOS_MIN=$(APP_MIN) DEPS=$(CURDIR)/build/deps $(APP_B)/yokaiwatch1recomp
	MACOS_MIN=$(APP_MIN) tools/bundle_macos.sh $(APP_B)/yokaiwatch1recomp

dmg: app
	tools/make_dmg.sh

DIST     := build/dist/YokaiWatch1Recomp
WIN_B    := build/win-release
deps-windows:
	tools/build_deps_windows.sh
dist:
	@[ -f build/deps-win/lib/libavcodec.dll.a ] || { echo "make deps-windows first (LGPL FFmpeg)"; exit 1; }
	@[ -d build/helper/yw1gen ] || { echo "make gen-helper first (the game code generator)"; exit 1; }
	@[ -x build/zig/zig.exe ] || { echo "tools/fetch_zig.sh first (the compiler)"; exit 1; }
	$(MAKE) B=$(WIN_B) DEPS=$(CURDIR)/build/deps-win RELEASE=1 $(WIN_B)/YokaiWatch1Recomp.exe
	! nm $(WIN_B)/YokaiWatch1Recomp.exe | grep ' f_70' >/dev/null  # (no game code inside)
	rm -rf build/dist && mkdir -p "$(DIST)/gamelib-src" "$(DIST)/helper"
	cp $(WIN_B)/YokaiWatch1Recomp.exe $(WIN_B)/YokaiWatch1Recomp.dll.a "$(DIST)/"
	tools/zig_slim.sh $(strip $(filter-out -Iruntime,$(GENFLAGS)))  # (zig with its runtime built ahead of time)
	cp -R build/helper/yw1gen "$(DIST)/helper/"
	cp -R build/zig-slim "$(DIST)/zig"
	cp runtime/cpu.h runtime/simd.h runtime/simd_fp.h runtime/gamelib.h tools/gamelib_entry.c runtime/hle/hle_symbols.txt \
	    "$(DIST)/gamelib-src/"
	if [ -f docs/icon/icon_1024.png ]; then cp docs/icon/icon_1024.png "$(DIST)/icon.png"; fi
	if [ -f third_party/fonts/Baloo2/Baloo2-SemiBold.ttf ]; then \
	    cp third_party/fonts/Baloo2/Baloo2-SemiBold.ttf "$(DIST)/"; \
	    cp third_party/fonts/Baloo2/OFL.txt "$(DIST)/Baloo2-OFL.txt"; fi
	if [ -f third_party/fonts/DejaVu/DejaVuSans.ttf ]; then \
	    cp third_party/fonts/DejaVu/DejaVuSans.ttf third_party/fonts/DejaVu/DejaVuSans-Bold.ttf "$(DIST)/"; \
	    cp third_party/fonts/DejaVu/LICENSE "$(DIST)/DejaVu-LICENSE.txt"; fi
	cp README.md README_zh.md LICENSE THIRD_PARTY_LICENSES.md recomplauncher.json "$(DIST)/"  # (licenses and launcher metadata)
	PATH="$(CURDIR)/build/deps-win/bin:$$PATH" ldd $(WIN_B)/YokaiWatch1Recomp.exe | awk '(/ucrt64/ || /deps-win/) {print $$3}' | sort -u | \
	    xargs -I{} cp -u {} "$(DIST)/"
	! ls "$(DIST)" | grep -iE 'x264|x265|xvid|postproc|vulkan|shaderc'  # (no GPL codecs or Vulkan/shaderc in the release)
	if [ "$${CI:-}" != true ]; then \
	    case "$$(basename "$$(cygpath -u "$$USERPROFILE")")" in runner|runneradmin) h= ;; *) h=$$(cygpath -m "$$USERPROFILE") ;; esac; \
	    ! grep -rlaiF -e "$(CURDIR)" -e "$$(cygpath -m '$(CURDIR)')" -e "$$(cygpath -w '$(CURDIR)')" \
	        $${h:+-e "$$h" -e "$$(cygpath -w "$$h")"} "$(DIST)"; fi
	cd build/dist && if command -v zip >/dev/null; then zip -qr "YokaiWatch1Recomp-$(VERSION)-windows.zip" "YokaiWatch1Recomp"; \
	    else /c/Windows/System32/tar.exe -a -cf "YokaiWatch1Recomp-$(VERSION)-windows.zip" "YokaiWatch1Recomp"; fi
	@echo "build/dist/YokaiWatch1Recomp-$(VERSION)-windows.zip"

# Seed generator as a standalone helper, so players need no Python: build/helper/rando-gen/ (rando-gen[.exe] +
# _internal/), frozen by PyInstaller from tools/rando/rando_gen.spec in its own venv (build/rando-venv). Ship the
# rando-gen folder next to the exe (Windows) or as "LAS Recomp.app"/Contents/Resources/rando-gen (runtime/ui/rando_tab.mm
# generator_cmd). PyInstaller only builds for the OS it runs on.
# macOS arm64: the relocatable Python 3.12 and keystone from `make deps` (macOS 11+ / 15+: the helper runs where the
#   app does); without them Homebrew's python@3.12 and keystone (then only on the macOS they were built for). The
#   keystone wheel has no dylib: KEYSTONE_LIB tells rando_gen.spec which one to take.
# Windows x86-64 (untested): from the MSYS2 shell with a python.org CPython 3.12 (MSYS2's Python has no oead wheel),
#   e.g. make rando-helper RANDO_PY="/c/Users/<you>/AppData/Local/Programs/Python/Python312/python.exe"
ifeq ($(PLATFORM),macos)
RANDO_PY  ?= $(firstword $(wildcard build/deps/python/bin/python3.12) python3.12)
export KEYSTONE_LIB ?= $(wildcard $(CURDIR)/build/deps/lib/libkeystone.dylib)
RANDO_VPY := build/rando-venv/bin/python
else
RANDO_PY  ?= py -3.12
RANDO_VPY := build/rando-venv/Scripts/python.exe
endif

rando-helper: build/rando-venv/.ok
	$(if $(filter windows,$(PLATFORM)),USERPROFILE="$$(cygpath -w ~ 2>/dev/null || echo $$HOME)") \
	$(RANDO_VPY) -m PyInstaller --noconfirm --clean --log-level WARN --distpath build/helper --workpath build/pyinstaller \
	    tools/rando/rando_gen.spec
	@du -sh build/helper/rando-gen

# The game code generator (tools/yw1gen.spec): build/helper/yw1gen/, shipped under helper/yw1gen/ next to the exe.
# Override GEN_PY="/path/to/python.exe" to freeze with an existing Python installation containing pyinstaller, capstone, unicorn, lz4.
GEN_PY ?=
gen-helper: $(if $(GEN_PY),,build/rando-venv/.ok build/rando-venv/.gen-ok)
	$(if $(filter windows,$(PLATFORM)),USERPROFILE="$$(cygpath -w ~ 2>/dev/null || echo $$HOME)") \
	"$(or $(GEN_PY),$(RANDO_VPY))" -m PyInstaller --noconfirm --clean --log-level WARN --distpath build/helper --workpath build/pyinstaller \
	    tools/yw1gen.spec
	$(if $(filter windows,$(PLATFORM)),"$(or $(GEN_PY),$(RANDO_VPY))" tools/pe_no_cfg.py build/helper/yw1gen/yw1gen.exe)
	@du -sh build/helper/yw1gen

build/rando-venv/.gen-ok: build/rando-venv/.ok
	$(RANDO_VPY) -m pip install -q capstone==5.0.9 unicorn==2.1.4 lz4==4.4.5
	touch $@

build/rando-venv/.ok:
	$(RANDO_PY) -m venv build/rando-venv
	$(RANDO_VPY) -m pip install -q pyinstaller==6.22.3
	touch $@

# Linux: build/dist/LAS Recomp-<version>-linux-<arch>.AppImage, built in Ubuntu 22.04 containers (podman) with SDL3, LGPL
# FFmpeg, shaderc and the seed generator helper inside; the Vulkan loader and GPU drivers are the system's.
# `make appimage` (ARCH: this machine's, or aarch64 cross-compiled), `make appimages` both. See docs/linux.md.
appimage:
	tools/linux/appimage.sh $(ARCH)
appimages:
	tools/linux/appimage.sh x86_64
	tools/linux/appimage.sh aarch64
# release check of the x86-64 AppImage: start screen, into the game, warps to the overworld and every dungeon
smoketest:
	tools/linux/smoketest.sh

.PHONY: dmg all gen gen-helper gen-objs runtime-objs deps deps-windows app dist rando-helper appimage appimages smoketest
