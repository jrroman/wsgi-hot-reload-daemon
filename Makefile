CCC=gcc
FLAGS=-Ofast -Wall -o
ROOT_DIR=./
SRC_DIR=$(ROOT_DIR)src
BIN_DIR=/usr/bin
PROG_NAME=daemon
RM=rm
EXT=.c

build:
	$(CCC) $(FLAGS) $(ROOT_DIR)/$(PROG_NAME) $(SRC_DIR)/$(PROG_NAME)$(EXT)

install:
	$(CCC) $(FLAGS) $(BIN_DIR)/$(PROG_NAME) $(SRC_DIR)/$(PROG_NAME)$(EXT)

clean:
	$(RM) $(PROG_NAME)

clean-install:
	$(RM) $(BIN_DIR)/$(PROG_NAME)
