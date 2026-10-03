PS5_HOST ?= ps5
PS5_PORT ?= 9021

ifdef PS5_PAYLOAD_SDK
    include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk
else
    $(error PS5_PAYLOAD_SDK is undefined)
endif

ELF     := joyconnect-ui.elf
PARENT  := ..

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

UI_OBJS  := $(UI_SRCS:.c=.o)
BT_OBJS  := $(BT_SRCS:.c=.o)
SBC_OBJS := $(SBC_SRCS:.c=.o)

CFLAGS     := -Wall -Werror -O2 -g \
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

LDFLAGS    := -lpthread \
              -lSceNet \
              -lSceNotification \
              -lScePad \
              -lSceUserService \
              -lSceVideoOut \
              -lSceSystemService

all: $(ELF)

$(ELF): $(UI_OBJS) $(BT_OBJS) $(SBC_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(SBC_OBJS): %.o: %.c
	$(CC) $(SBC_CFLAGS) -c -o $@ $<

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(UI_OBJS) $(BT_OBJS) $(SBC_OBJS) $(ELF)

send: $(ELF)
	$(PROSPERO_SEND) $(PS5_HOST) $(PS5_PORT) $(ELF)
