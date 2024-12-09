MVLCC_DIR = ../

MVLCC_CONFIG = $(MVLCC_DIR)/bin/mvlcc-config.sh

CFLAGS +=  $(shell $(MVLCC_CONFIG) --cflags) -ggdb -O0
LDFLAGS += $(shell $(MVLCC_CONFIG) --ldflags)
LIBS +=    $(shell $(MVLCC_CONFIG) --libs)

.PHONY: all

all: test test2 test3 test4 mvlcc_mini_daq

%.o: %.c
	$(CC) -c -o $@ $< $(CFLAGS)

%: %.o
	$(CC) -o $@ $< $(LDFLAGS) $(LIBS)

test: test.o
test2: test2.o
test3: test3.o
test4: test4.o
mvlcc_mini_daq: mvlcc_mini_daq.o

clean:
	rm -rf test test.o test2 test2.o test3 test3.o test4 test4.o mvlcc_mini_daq mvlcc_mini_daq.o
