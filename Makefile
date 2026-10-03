PS5_HOST ?= ps5
PS5_PORT ?= 9021

ifdef PS5_PAYLOAD_SDK
    include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk
else
    $(error PS5_PAYLOAD_SDK is undefined)
endif

ELF := joyconnect.elf

SBC_SRCS := sbc/sbc.c sbc/sbc_primitives.c
SBC_OBJS := $(SBC_SRCS:.c=.o)

SRCS := src/main.c \
        src/log.c \
        src/hci.c \
        src/l2cap.c \
        src/a2dp.c \
        src/avcap.c \
        src/joycon.c \
        src/vpad.c

OBJS := $(SRCS:.c=.o)

CFLAGS     := -Wall -Werror -O2 -g -Isrc -Isbc -DSBC_TABLES_CONST
SBC_CFLAGS := -O2 -g -Isbc -DSBC_TABLES_CONST -Wno-ignored-attributes \
              -USBC_BUILD_WITH_MMX_SUPPORT -USBC_BUILD_WITH_SSE_SUPPORT \
              -USBC_BUILD_WITH_NEON_SUPPORT -USBC_BUILD_WITH_IWMMXT_SUPPORT
LDFLAGS    := -lpthread -lSceNet -lSceNotification -lScePad -lSceUserService

all: $(ELF)

$(ELF): $(OBJS) $(SBC_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(SBC_OBJS): %.o: %.c
	$(CC) $(SBC_CFLAGS) -c -o $@ $<

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(ELF)

send: $(ELF)
	$(PROSPERO_SEND) $(PS5_HOST) $(PS5_PORT) $(ELF)
