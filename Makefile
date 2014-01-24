cc=gcc
CFLAGS=-g -c -Wall -mthumb -O3 -march=armv7-a -mcpu=cortex-a9 -mtune=cortex-a9 -mfpu=neon -mvectorize-with-neon-quad -mfloat-abi=hard -DLINUX -DEGL_API_FB -I/usr/include/freetype2
LDFLAGS=-lEGL -lGLESv2 -lfontconfig -lfreetype
SRCS=main.c
OBJS=$(SRCS:.c=.o)
TARGET=project-11

all: $(SRCS) $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) $(OBJS) -o $@

.c.o:
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm -fr $(OBJS) $(TARGET)
