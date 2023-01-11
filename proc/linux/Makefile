# Makefile for C projects

# options
CC     = gcc
LINKER = $(CC)
CFLAGS =  -O0 -pthread -g -std=gnu99
LFLAGS = -ldl -lX11 -lXi -lm -lpthread -lavformat -lavcodec -lswscale -lavutil -lm

TARGET   = App

# directories
SRC_DIR  = ../../source
BIN_DIR  = ../../bin
INCLUDE  = -I../../third_party/include
OBJ_DIR  = $(BIN_DIR)/.obj

# finds source files two directories deep
SOURCES  = $(wildcard $(SRC_DIR)/*.c $(SRC_DIR)/*/*.c)
OBJECTS  = $(SOURCES:$(SRC_DIR)/%.c=$(OBJ_DIR)/%.o)
DEPENDS  = $(SOURCES:$(SRC_DIR)/%.c=$(OBJ_DIR)/%.d)

.PHONY: all build run clean

all: build

build: $(BIN_DIR) $(DEPENDS) $(BIN_DIR)/$(TARGET)

$(BIN_DIR):
	mkdir -p $@

# update dependencies
$(OBJ_DIR)/%.d : $(SRC_DIR)/%.c
	@mkdir -p $(@D)
	@echo -n "$(@D)/" > $@
	@$(CC) $(CFLAGS) -MM $< | sed "s/ /\t/" >> $@;
	@echo "Updating dependencies for $<"

# add dependencies
include $(DEPENDS)

# compile
$(OBJ_DIR)/%.o : $(SRC_DIR)/%.c
	@mkdir -p $(@D)
	$(CC) -c $(INCLUDE) $(CFLAGS) $< -o $@

# link
$(BIN_DIR)/$(TARGET): $(OBJECTS)
	$(LINKER) $(OBJECTS) $(LFLAGS) -o $@

run: build
	./$(BIN_DIR)/$(TARGET)

clean:
	rm -f $(OBJECTS) $(GS_OBJ)
	rm -f $(BIN_DIR)/$(TARGET)
