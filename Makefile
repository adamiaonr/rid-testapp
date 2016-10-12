CC = g++

# special directories
SRCDIR = src
BUILDDIR = build
BINDIR = bin

# target: rid_fwd
TARGET = rid_fwd
# common source files
COMMON = lookup_stats pt rid_utils

# other helpful variables
SRCEXT = c
SOURCES = $(shell find $(SRCDIR) -type f -name *.$(SRCEXT))
OBJECTS = $(patsubst $(SRCDIR)/%,$(BUILDDIR)/%,$(SOURCES:.$(SRCEXT)=.o))

# debuggin' options
ifdef DEBUG
CFLAGS += -g -ggdb -gdwarf-2 -Wall -Wno-comment -std=c++11
else
CFLAGS +=-O3 -gdwarf-2 -Wall -Wno-comment -std=c++11
endif

# search for libs here
LDFLAGS += -Llib/libbloom/build -Llib/threadpool -Llib/uthash
# add these libs for linking
LIB += -lbloom -lthreadpool -lpthread $(LIBS)
# special include dirs to add
INC += -Iinclude -Ilib/libbloom -Ilib/threadpool/src -Ilib/uthash/src

all: $(TARGET)
	mkdir -p $(BINDIR)
	mv $(TARGET) $(BINDIR)

$(TARGET): $(BUILDDIR)/argvparser.o $(OBJECTS)
	@echo " Linking..."
	@echo " $(CC) -o $@ $^ $(LDFLAGS) $(LIB)"; $(CC) -o $@ $^ $(LDFLAGS) $(LIB)

$(BUILDDIR)/%.o: $(SRCDIR)/%.$(SRCEXT)
	make -C lib/libbloom
	make -C lib/threadpool
	@mkdir -p $(BUILDDIR)
	@echo "$(CC) $(CFLAGS) $(INC) -c $< -o $@"; $(CC) $(CFLAGS) $(INC) -c $< -o $@

$(BUILDDIR)/argvparser.o: $(SRCDIR)/argvparser.cpp
	@mkdir -p $(BUILDDIR)
	@echo "$(CC) $(CFLAGS) $(INC) -c $< -o $@"; $(CC) $(CFLAGS) $(INC) -c $< -o $@
	
clean:
	@echo " Cleaning...";
	make -C lib/libbloom clean
	make -C lib/threadpool clean
	@echo " $(RM) -r $(BUILDDIR) $(BINDIR)"; $(RM) -r $(BUILDDIR) $(BINDIR) *~

.PHONY: clean
