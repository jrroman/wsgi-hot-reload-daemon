CCC=gcc
FLAGS=-Ofast -Wall -o
ROOT_DIR=/daemon
SRC_DIR=$(ROOT_DIR)/src
PROG_NAME=daemon
RM=rm
EXT=.c

build:
	$(CCC) $(FLAGS) $(ROOT_DIR)/$(PROG_NAME) $(SRC_DIR)/$(PROG_NAME)$(EXT)

clean:
	$(RM) $(PROG_NAME)
