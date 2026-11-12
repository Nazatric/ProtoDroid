# Native host (no Wine): the recompiled engine plus the Win32 emulation layer.
#   make                         Linux x86-64 executable (build/native/proto-native)
#   make PLATFORM=android [ABI=arm64-v8a|x86_64]
#                                libmain.so for the APK (android/build-apk.sh), against the libraries from
#                                android/build-deps.sh
PLATFORM ?= linux
# Generated for the rebased engine: recomp.py --functions all --base 0x7c000000 --split 400 --out build/recomp/native (see guest.h)
RECOMP_DIR ?= ../../build/recomp/native
LUA_DIR := ../../third_party/lua-5.4.8
DXVK ?= $(HOME)/protodroid-data/deps/dxvk
# CI supplies SOURCE_REVISION=${{ github.sha }}; local builds identify their checked-out commit when Git is available.
# Keep this in the native binary as well as the APK metadata so a device log cannot be tied to the wrong source tree.
SOURCE_REVISION ?= $(shell git rev-parse --verify HEAD 2>/dev/null || printf unknown)
# Guest memory: a 4 GiB window at 64 GiB (guest.h kGuestBase), clear of the low 4 GiB that Android's Java heap and
# Qualcomm's GPU driver use.
COMMON_FLAGS := -DRECOMP_FLAT_MEMORY -DRECOMP_GUEST_BASE=0x1000000000ull -fsigned-char -I../recomp \
                -DPROTODROID_SOURCE_REVISION=\"$(SOURCE_REVISION)\"
# Generated code: flat memory turns G() into plain pointers. -O1 on the desktop keeps rebuilds quick; Android gets
# -O2, matching the upstream port and avoiding the code-size/register-pressure regression of globally optimizing the
# recompiled engine at -O3. Keep the established TSO ordering flags unchanged.
RECOMP_OPT ?= -O1
RECOMP_CFLAGS = -std=c11 $(RECOMP_OPT) $(RECOMP_EXTRA) -g0 -fno-strict-aliasing -fwrapv -fno-delete-null-pointer-checks -w $(COMMON_FLAGS) \
                $(TARGET_FLAGS)
CXXFLAGS = -std=c++17 -O2 -g -Wall -Wextra -Wno-unused-parameter $(COMMON_FLAGS) $(TARGET_FLAGS) \
           -I$(DXVK)/include/native/directx -I$(DXVK)/include/native/windows -I$(DXVK)/include/native/wsi \
           $(DEP_INCLUDES)

ifeq ($(PLATFORM),android)
ABI ?= arm64-v8a
NDK ?= $(HOME)/Android/Sdk/ndk/28.2.13676358
DEPS ?= $(HOME)/protodroid-data/android-deps/$(ABI)
TRIPLE := $(if $(filter x86_64,$(ABI)),x86_64,aarch64)-linux-android26
TOOLCHAIN := $(NDK)/toolchains/llvm/prebuilt/linux-x86_64/bin
CXX := $(TOOLCHAIN)/$(TRIPLE)-clang++
CC := $(TOOLCHAIN)/$(TRIPLE)-clang
OUT ?= ../../build/android/$(ABI)
# arm64: ARMv8.2 + RCpc (Cortex-A55/A76/X1 and newer, i.e. phones from 2019 on): LDAPR for the TSO acquire loads,
# inline LSE atomics. ARM_ARCH=armv8-a builds for older cores (Cortex-A53/A73), with outline atomics.
ARM_ARCH ?= armv8.2-a+rcpc
TARGET_FLAGS := -fPIC $(if $(filter arm64-v8a,$(ABI)),-march=$(ARM_ARCH) -mtune=cortex-a76 \
                $(if $(filter armv8-a,$(ARM_ARCH)),-moutline-atomics))
RECOMP_OPT := -O2
# Optional native mods use source-built static Dobby on Android; no additional shared library is packaged.
COMMON_FLAGS += -DPROTODROID_DOBBY
# arm64 is weakly ordered: guest memory accesses get x86-like ordering (runtime.h RECOMP_TSO); tso=0 to measure.
TSO ?= 1
RECOMP_EXTRA := $(if $(filter 1,$(TSO)),-DRECOMP_TSO)
DEP_INCLUDES := -I$(DEPS)/include
FFMPEG_LIBS := -Wl,--start-group $(DEPS)/lib/libavformat.a $(DEPS)/lib/libavcodec.a \
               $(DEPS)/lib/libswresample.a $(DEPS)/lib/libavutil.a -Wl,--end-group -lz -lm
# Keep symbol tables for crash stack traces (dladdr) even after strip: --export-dynamic preserves symbols in dynamic table
LDFLAGS := -shared -Wl,-Bsymbolic -static-libstdc++ -Wl,--no-undefined -Wl,--export-dynamic -L$(DEPS)/lib \
           $(DEPS)/lib/libdobby.a -ldxvk_d3d9 -lFAudio -lSDL2 $(FFMPEG_LIBS) -llog -landroid -ldl
# Adreno: Turnip and libadrenotools from android/build-turnip.sh (arm64 only; built without them otherwise).
TURNIP ?= $(HOME)/protodroid-data/android-turnip
ifneq ($(and $(filter arm64-v8a,$(ABI)),$(wildcard $(TURNIP)/lib/libadrenotools.a)),)
DEP_INCLUDES += -I$(TURNIP)/include -DPROTODROID_ADRENOTOOLS
LDFLAGS += -L$(TURNIP)/lib -ladrenotools -llinkernsbypass -ldl \
           -Wl,--defsym=android_get_device_api_level=protodroid_device_api_level
# The driver probe program (turnip_probe.cpp): API 29 like libadrenotools, plain ARMv8.0 so one build runs on all.
EXTRA := $(OUT)/libturnipprobe.so
endif
BINARY := libmain.so
else
CXX ?= g++
CC ?= gcc
OUT ?= ../../build/native
# DXVK native (D3D9 on Vulkan) and FAudio (XAudio2 reimplementation, zlib license), built in deps.
DXVK_LIB ?= $(DXVK)/build-native/src/d3d9
FAUDIO ?= $(HOME)/protodroid-data/deps/FAudio
FAUDIO_LIB ?= $(FAUDIO)/build-native
FFMPEG ?= $(HOME)/protodroid-data/deps/ffmpeg
FFMPEG_LIBS ?= -Wl,--start-group $(FFMPEG)/lib/libavformat.a $(FFMPEG)/lib/libavcodec.a \
               $(FFMPEG)/lib/libswresample.a $(FFMPEG)/lib/libavutil.a -Wl,--end-group -lz -lm
TARGET_FLAGS :=
DEP_INCLUDES := -I$(FAUDIO)/include -I$(FFMPEG)/include
LDFLAGS := -rdynamic -pthread -L$(DXVK_LIB) -Wl,-rpath,$(DXVK_LIB) -ldxvk_d3d9 -L$(FAUDIO_LIB) -Wl,-rpath,$(FAUDIO_LIB) -lFAudio \
           -lSDL2 $(FFMPEG_LIBS) -ldl
BINARY := proto-native
endif

SRCS := main.cpp diagnostic_tour.cpp diagnostics.cpp memory.cpp loader.cpp imports.cpp runtime.cpp thread.cpp path.cpp save_path.cpp kernel32.cpp msvcr80.cpp user32.cpp prime_affinity.cpp \
        advapi32.cpp misc.cpp d3d9.cpp d3d9_dxt.cpp input.cpp xaudio2.cpp bink.cpp bink_media.cpp bink_blit.cpp dinput.cpp gamepad.cpp config.cpp watchdog.cpp hot_audio.cpp hot_skin.cpp \
        vulkan_driver.cpp perf_hint.cpp present_stats.cpp state_cache.cpp video_settings.cpp \
        mod_platform.cpp mod_files.cpp mod_textures.cpp mod_hook.cpp mod_features.cpp mod_plugins.cpp mod_settings.cpp \
        mod_symbols.cpp mod_scripts.cpp
LUA_SRCS := $(LUA_DIR)/lapi.c $(LUA_DIR)/lcode.c $(LUA_DIR)/lctype.c $(LUA_DIR)/ldebug.c $(LUA_DIR)/ldo.c \
            $(LUA_DIR)/ldump.c $(LUA_DIR)/lfunc.c $(LUA_DIR)/lgc.c $(LUA_DIR)/llex.c $(LUA_DIR)/lmem.c \
            $(LUA_DIR)/lobject.c $(LUA_DIR)/lopcodes.c $(LUA_DIR)/lparser.c $(LUA_DIR)/lstate.c $(LUA_DIR)/lstring.c \
            $(LUA_DIR)/ltable.c $(LUA_DIR)/ltm.c $(LUA_DIR)/lundump.c $(LUA_DIR)/lvm.c $(LUA_DIR)/lzio.c \
            $(LUA_DIR)/lauxlib.c $(LUA_DIR)/lbaselib.c $(LUA_DIR)/lmathlib.c $(LUA_DIR)/lstrlib.c \
            $(LUA_DIR)/ltablib.c $(LUA_DIR)/lutf8lib.c
GEN := $(wildcard $(RECOMP_DIR)/*.c)
OBJS = $(SRCS:%.cpp=$(OUT)/%.o) $(GEN:$(RECOMP_DIR)/%.c=$(OUT)/gen/%.o) $(LUA_SRCS:$(LUA_DIR)/%.c=$(OUT)/lua/%.o)

all: $(OUT)/$(BINARY) $(EXTRA)

$(OUT)/libturnipprobe.so: turnip_probe.cpp turnip_load.h gpu_profile.h
	@mkdir -p $(dir $@)
	$(TOOLCHAIN)/aarch64-linux-android29-clang++ -std=c++17 -O2 -fPIE -pie -march=armv8-a -moutline-atomics -static-libstdc++ \
		-I$(TURNIP)/include -o $@ $< -L$(TURNIP)/lib -ladrenotools -llinkernsbypass -ldl -llog

$(OUT)/$(BINARY): $(OBJS)
	$(CXX) -o $@ $^ $(LDFLAGS)

# The BC1-3 decoder is a self-contained hot loop; keep this final flag after the shared -O2 on every platform.
$(OUT)/d3d9_dxt.o: CXXFLAGS += -O3
# Native replacements preserve the original arithmetic association on GCC as well as Clang; do not fuse a multiply
# with a following add on targets with FMA. This is explicit rather than relying only on source-level pragmas.
$(OUT)/hot_audio.o $(OUT)/hot_skin.o: CXXFLAGS += -ffp-contract=off

$(OUT)/perf_hint.o: perf_hint_session.h
$(OUT)/main.o $(OUT)/state_cache.o: state_cache.h
$(OUT)/main.o: dxvk_config.h input.h
$(OUT)/main.o $(OUT)/path.o $(OUT)/save_path.o: save_path.h
$(OUT)/main.o $(OUT)/d3d9.o $(OUT)/present_stats.o $(OUT)/diagnostics.o: present_stats.h timing_window.h
$(OUT)/main.o $(OUT)/d3d9.o $(OUT)/diagnostics.o: diagnostics.h
$(OUT)/main.o $(OUT)/runtime.o $(OUT)/diagnostics.o $(OUT)/vulkan_driver.o $(OUT)/diagnostic_tour.o: diagnostic_tour.h
$(OUT)/runtime.o: diagnostic_tour_probe.h
$(OUT)/main.o $(OUT)/kernel32.o $(OUT)/misc.o $(OUT)/bink.o: guest_time.h
$(OUT)/kernel32.o: diagnostics.h
$(OUT)/watchdog.o: diagnostic_tour.h
$(OUT)/diagnostic_tour.o: diagnostic_tour_plan.h input.h guest_time.h
$(OUT)/input.o: input.h input_lookup.h
$(OUT)/dinput.o: input_lookup.h
$(OUT)/diagnostics.o: proc_stats.h
$(OUT)/vulkan_driver.o: gpu_profile.h
$(OUT)/video_settings.o: video_settings.h
$(OUT)/main.o $(OUT)/d3d9.o $(OUT)/bink_media.o $(OUT)/kernel32.o $(OUT)/msvcr80.o $(OUT)/mod_platform.o $(OUT)/mod_files.o \
$(OUT)/mod_textures.o $(OUT)/mod_hook.o $(OUT)/mod_features.o $(OUT)/mod_plugins.o: mod_platform.h
$(OUT)/mod_plugins.o: ../../modding/include/protodroid_plugin.h mod_settings.h mod_symbols.h
$(OUT)/mod_scripts.o: mod_platform.h mod_settings.h mod_symbols.h $(LUA_DIR)/lua.h $(LUA_DIR)/lauxlib.h $(LUA_DIR)/lualib.h
$(OUT)/mod_symbols.o: mod_symbols.h mod_symbols.inc
$(OUT)/mod_settings.o: mod_settings.h mod_platform.h

$(OUT)/lua/%.o: $(LUA_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) -std=c99 -O2 -g0 -fno-strict-aliasing -fwrapv $(TARGET_FLAGS) -I$(LUA_DIR) -c -o $@ $<

$(OUT)/%.o: %.cpp guest.h win.h turnip_load.h ../recomp/runtime.h d3d9_gen.inc
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(OUT)/gen/%.o: $(RECOMP_DIR)/%.c ../recomp/runtime.h ../recomp/runtime_fp.h
	@mkdir -p $(dir $@)
	$(CC) $(RECOMP_CFLAGS) -c -o $@ $<

clean:
	rm -rf $(OUT)

.PHONY: all clean
