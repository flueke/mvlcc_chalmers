MVLCC_DIR = ../

MVLCC_CONFIG = $(MVLCC_DIR)/bin/mvlcc-config.sh

CFLAGS +=  $(shell $(MVLCC_CONFIG) --cflags) -ggdb -O0
LDFLAGS += $(shell $(MVLCC_CONFIG) --ldflags)
LIBS +=    $(shell $(MVLCC_CONFIG) --libs)

.PHONY: all $(BUILD_DIR)/libmvlcc.a

all: test_mvlcc_wrap

%.o: %.c
	$(CC) -c -o $@ $< $(CFLAGS)

test_mvlcc_wrap: test_mvlcc_wrap.o $(BUILD_DIR)/libmvlcc.a
	$(CC) -o $@ $< $(CFLAGS) $(LDFLAGS) $(LIBS)

clean:
	rm -rf test_mvlcc_wrap test_mvlcc_wrap.o
