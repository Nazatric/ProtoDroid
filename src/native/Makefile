# Native host (no Wine): the recompiled engine plus the Win32 emulation layer.
#   make                         Linux x86-64 executable (build/native/proto-native)
#   make PLATFORM=android [ABI=arm64-v8a|x86_64]
#                                libmain.so for the APK (android/build-apk.sh), against the libraries from
#                                android/build-deps.sh
PLATFORM ?= linux
# Generated for the rebased engine: recomp.py --functions all --base 0x7c000000 --split 400 --out build/recomp/native (see guest.h)
RECOMP_DIR ?= ../../build/recomp/native
DXVK ?= $(HOME)/protodroid-data/deps/dxvk
# Guest memory: a 4 GiB window at 64 GiB (guest.h kGuestBase), clear of the low 4 GiB that Android's Java heap and
# Qualcomm's GPU driver use.
COMMON_FLAGS := -DRECOMP_FLAT_MEMORY -DRECOMP_GUEST_BASE=0x1000000000ull -fsigned-char -I../recomp
# Generated code: flat memory turns G() into plain pointers. -O1 on the desktop keeps rebuilds quick; Android gets
# -O2 (clang: barely slower to build).
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
# arm64 is weakly ordered: guest memory accesses get x86-like ordering (runtime.h RECOMP_TSO); tso=0 to measure.
TSO ?= 1
RECOMP_EXTRA := $(if $(filter 1,$(TSO)),-DRECOMP_TSO)
DEP_INCLUDES := -I$(DEPS)/include
FFMPEG_LIBS := -Wl,--start-group $(DEPS)/lib/libavformat.a $(DEPS)/lib/libavcodec.a \
               $(DEPS)/lib/libswresample.a $(DEPS)/lib/libavutil.a -Wl,--end-group -lz -lm
# Keep symbol tables for crash stack traces (dladdr) even after strip: --export-dynamic preserves symbols in dynamic table
LDFLAGS := -shared -Wl,-Bsymbolic -static-libstdc++ -Wl,--no-undefined -Wl,--export-dynamic -L$(DEPS)/lib \
           -ldxvk_d3d9 -lFAudio -lSDL2 $(FFMPEG_LIBS) -llog -landroid
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
           -lSDL2 $(FFMPEG_LIBS)
BINARY := proto-native
endif

SRCS := main.cpp memory.cpp loader.cpp imports.cpp runtime.cpp thread.cpp kernel32.cpp msvcr80.cpp user32.cpp \
        advapi32.cpp misc.cpp d3d9.cpp d3d9_dxt.cpp input.cpp xaudio2.cpp bink.cpp bink_media.cpp bink_blit.cpp dinput.cpp gamepad.cpp config.cpp watchdog.cpp hot_audio.cpp hot_skin.cpp \
        vulkan_driver.cpp perf_hint.cpp present_stats.cpp
GEN := $(wildcard $(RECOMP_DIR)/*.c)
OBJS = $(SRCS:%.cpp=$(OUT)/%.o) $(GEN:$(RECOMP_DIR)/%.c=$(OUT)/gen/%.o)

all: $(OUT)/$(BINARY) $(EXTRA)

$(OUT)/libturnipprobe.so: turnip_probe.cpp turnip_load.h gpu_profile.h
	@mkdir -p $(dir $@)
	$(TOOLCHAIN)/aarch64-linux-android29-clang++ -std=c++17 -O2 -fPIE -pie -march=armv8-a -moutline-atomics -static-libstdc++ \
		-I$(TURNIP)/include -o $@ $< -L$(TURNIP)/lib -ladrenotools -llinkernsbypass -ldl -llog

$(OUT)/$(BINARY): $(OBJS)
	$(CXX) -o $@ $^ $(LDFLAGS)

# The BC1-3 decoder is a self-contained hot loop; keep this final flag after the shared -O2 on every platform.
$(OUT)/d3d9_dxt.o: CXXFLAGS += -O3

$(OUT)/perf_hint.o: perf_hint_session.h
$(OUT)/main.o $(OUT)/d3d9.o $(OUT)/present_stats.o: present_stats.h
$(OUT)/vulkan_driver.o: gpu_profile.h

$(OUT)/%.o: %.cpp guest.h win.h turnip_load.h ../recomp/runtime.h d3d9_gen.inc
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(OUT)/gen/%.o: $(RECOMP_DIR)/%.c ../recomp/runtime.h ../recomp/runtime_fp.h
	@mkdir -p $(dir $@)
	$(CC) $(RECOMP_CFLAGS) -c -o $@ $<

clean:
	rm -rf $(OUT)

.PHONY: all clean
