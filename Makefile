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
		   src/unsubscribe.c    \
           src/suback.c         \
           src/unsuback.c       \
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

TEST_SRC = tests/tests.c                 \
           tests/mqtt_tests.c            \
		   src/mqtt.c                    \
		   src/timeutil.c

TEST_OBJ = $(TEST_SRC:.c=.o)
TEST_EXEC = tera-tests

# Release Build Variables
CFLAGS_RELEASE = -Wall -pedantic -std=c2x -O2
TERA_EXEC_RELEASE = tera-release

all: $(TERA_EXEC) $(TEST_EXEC)

release: $(TERA_EXEC_RELEASE)

$(TERA_EXEC): $(TERA_OBJ)
	$(CC) $(CFLAGS) -o $@ $^

$(TERA_EXEC_RELEASE): $(TERA_SRC:.c=.o.release)
	$(CC) $(CFLAGS_RELEASE) -o $@ $^

%.o.release: %.c
	$(CC) $(CFLAGS_RELEASE) -c $< -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

$(TEST_EXEC): $(TEST_OBJ)
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f $(TERA_OBJ) $(TERA_EXEC) $(TERA_EXEC_RELEASE) $(TERA_SRC:.c=.o.release)
	rm -f $(TEST_OBJ) $(TEST_EXEC)

.PHONY: all clean

