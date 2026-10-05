SDK       := /home/ali/PS5/PS5_PayloadSDK
PS5TOOL   := /home/ali/PS5/PS5_Vulkan/build/host/ps5-native-tool

TITLE_ID  := PPSA99003
PARENT    := ..

# Toolchain
CC        := PS5_PAYLOAD_SDK=$(SDK) $(SDK)/bin/prospero-clang
# prospero-clang18 uses system clang with correct PS5 target flags
LD        := $(SDK)/bin/prospero-lld

# Module constants
MODULE_SDK     := 0x02000009
COMPANION_SDK  := 0x08050001
FSELF_MAGIC    := 0x1D3D154F

# Build dirs
BUILD     := build
OBJ       := $(BUILD)/obj
DIST      := dist/$(TITLE_ID)

UI_SRCS := src/main.c \
           src/gfx.c \
           src/input.c \
           src/bt_mgr.c \
           src/ui.c

BT_SRCS := $(PARENT)/src/log.c \
           $(PARENT)/src/hci.c \
           $(PARENT)/src/l2cap.c \
           $(PARENT)/src/a2dp.c \
           $(PARENT)/src/avcap.c \
           $(PARENT)/src/joycon.c \
           $(PARENT)/src/vpad.c

SBC_SRCS := $(PARENT)/sbc/sbc.c \
            $(PARENT)/sbc/sbc_primitives.c

ALL_C_SRCS := $(UI_SRCS) $(BT_SRCS) $(SBC_SRCS)

# Object files under build/obj, preserving relative paths
ui_obj    = $(patsubst %.c,$(OBJ)/%.o,$(UI_SRCS))
bt_obj    = $(patsubst %.c,$(OBJ)/%.o,$(BT_SRCS))
sbc_obj   = $(patsubst %.c,$(OBJ)/%.o,$(SBC_SRCS))

CFLAGS := -Wall -Werror -O2 -g \
          -Isrc \
          -I$(PARENT)/src \
          -I$(PARENT)/sbc \
          -DSBC_TABLES_CONST


SBC_CFLAGS := -O2 -g \
              -I$(PARENT)/sbc \
              -DSBC_TABLES_CONST \
              -Wno-ignored-attributes \
              -USBC_BUILD_WITH_MMX_SUPPORT \
              -USBC_BUILD_WITH_SSE_SUPPORT \
              -USBC_BUILD_WITH_NEON_SUPPORT \
              -USBC_BUILD_WITH_IWMMXT_SUPPORT

crt_obj   = $(OBJ)/src/app_crt.o
ALL_OBJS  := $(crt_obj) $(ui_obj) $(bt_obj) $(sbc_obj)

PIE_ELF   := $(BUILD)/llvm-pie.elf
EBOOT_ELF := $(BUILD)/eboot.elf
EBOOT_BIN := $(DIST)/eboot.bin

.PHONY: all clean deploy

all: $(EBOOT_BIN)

# ── Final app assembly ──────────────────────────────────────────────────────

$(EBOOT_BIN): $(EBOOT_ELF) | $(DIST)/sce_sys $(DIST)/sce_module
	$(PS5TOOL) self --sign --in $< --out $@ --magic $(FSELF_MAGIC)
	cp sce_sys/param.json $(DIST)/sce_sys/param.json
	[ -f sce_sys/icon0.png ] && cp sce_sys/icon0.png $(DIST)/sce_sys/icon0.png || true
	cp sce_module/libc.prx $(DIST)/sce_module/libc.prx
	@echo "App ready: $(DIST)/"

$(DIST)/sce_sys:
	mkdir -p $@
$(DIST)/sce_module:
	mkdir -p $@

$(EBOOT_ELF): $(PIE_ELF)
	$(PS5TOOL) link \
	    --in $< --out $@ \
	    --stub-dir $(SDK)/target/lib \
	    --module-sdk $(MODULE_SDK) \
	    --companion-sdk $(COMPANION_SDK) \
	    --file-name eboot.elf

# ── PIE link (prospero-lld + ps5-pie.ld) ───────────────────────────────────

$(PIE_ELF): $(ALL_OBJS)
	$(LD) \
	    -T /home/ali/PS5/PS5_Vulkan/tooling/native/ps5-pie.ld \
	    --eh-frame-hdr \
	    --version-script /home/ali/PS5/PS5_Vulkan/tooling/native/app-symbols.map \
	    --exclude-libs=ALL \
	    -e _start \
	    -o $@ \
	    $^ \
	    $(SDK)/target/lib/libps5platform.a \
	    -L$(SDK)/target/lib \
	    --as-needed \
	    -lSceLibcInternal \
	    -lpthread \
	    -lkernel_sys \
	    -lSceNet \
	    -lSceNotification \
	    -lScePad \
	    -lSceUserService \
	    -lSceVideoOut \
	    -lSceSystemService

# ── Compile rules ───────────────────────────────────────────────────────────

$(OBJ)/src/%.o: src/%.c | $(OBJ)/src
	$(CC) $(CFLAGS) -ffunction-sections -fdata-sections -c -o $@ $<

$(OBJ)/$(PARENT)/src/%.o: $(PARENT)/src/%.c | $(OBJ)/$(PARENT)/src
	$(CC) $(CFLAGS) -ffunction-sections -fdata-sections -c -o $@ $<

$(OBJ)/$(PARENT)/sbc/%.o: $(PARENT)/sbc/%.c | $(OBJ)/$(PARENT)/sbc
	$(CC) $(SBC_CFLAGS) -ffunction-sections -fdata-sections -c -o $@ $<

# ── Directory creation ──────────────────────────────────────────────────────

$(OBJ)/src:
	mkdir -p $@
$(OBJ)/$(PARENT)/src:
	mkdir -p $@
$(OBJ)/$(PARENT)/sbc:
	mkdir -p $@

# ── Deploy via FTP ──────────────────────────────────────────────────────────

PS5_FTP_HOST ?= 192.168.4.87
PS5_FTP_PORT ?= 2121

deploy: all
	@echo "Uploading $(DIST) -> ftp://$(PS5_FTP_HOST):$(PS5_FTP_PORT)/data/homebrew/$(TITLE_ID)"
	curl --ftp-create-dirs -T $(DIST)/eboot.bin \
	    "ftp://$(PS5_FTP_HOST):$(PS5_FTP_PORT)/data/homebrew/$(TITLE_ID)/eboot.bin"
	curl --ftp-create-dirs -T $(DIST)/sce_sys/param.json \
	    "ftp://$(PS5_FTP_HOST):$(PS5_FTP_PORT)/data/homebrew/$(TITLE_ID)/sce_sys/param.json"
	[ -f $(DIST)/sce_sys/icon0.png ] && \
	    curl --ftp-create-dirs -T $(DIST)/sce_sys/icon0.png \
	        "ftp://$(PS5_FTP_HOST):$(PS5_FTP_PORT)/data/homebrew/$(TITLE_ID)/sce_sys/icon0.png" \
	    || true
	curl --ftp-create-dirs -T $(DIST)/sce_module/libc.prx \
	    "ftp://$(PS5_FTP_HOST):$(PS5_FTP_PORT)/data/homebrew/$(TITLE_ID)/sce_module/libc.prx"
	@echo "Deployed to PS5."

clean:
	rm -rf $(BUILD) $(DIST)
