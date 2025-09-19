CC=gcc
CFLAGS=-Wall                                  \
       -Werror                                \
       -pedantic                              \
       -ggdb                                  \
       -std=c2x                               \
       -fsanitize=address                     \
       -fsanitize=undefined                   \
       -fno-omit-frame-pointer                \
       -pg                                    \
       -Wno-gnu-zero-variadic-macro-arguments \

TERA_SRC = src/timeutil.c       \
           src/iomux.c          \
           src/bin.c            \
           src/mqtt.c           \
           src/connect.c        \
           src/disconnect.c     \
           src/connack.c        \
           src/publish.c        \
           src/subscribe.c      \
           src/suback.c         \
           src/ack.c            \
           src/pingreq.c        \
           src/pingresp.c       \
           src/arena.c          \
	       src/buffer.c         \
		   src/config.c         \
           src/net.c            \
           src/server.c
TERA_OBJ = $(TERA_SRC:.c=.o)
TERA_EXEC = tera

all: $(TERA_EXEC)

$(TERA_EXEC): $(TERA_OBJ)
	$(CC) $(CFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(TERA_OBJ) $(TERA_EXEC)

.PHONY: all clean

