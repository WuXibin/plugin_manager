CC = g++

INC_DIR = -I../
LIB_DIR = -L../ 

CFLAGS = -g -shared -fPIC -W -Wall -Wno-unused-parameter -Werror
LDFLAGS =

TCFLAGS = -W -Wall -Wno-unused-parameter -Werror
TLDFLAGS = -lsharelib

OBJS = plugin_deliver.o plugin_manager_test.o
PROG = libdeliver.so

TOBJS = plugin_manager_test.o
TPROG = plugin_manager_test


.PHONY: all test clean

all: $(PROG)

test: $(TPROG)

clean:
	-rm -rf $(OBJS) $(TOBJS) $(PROG) $(TPROG)

$(PROG): $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o $@ $(LIB_DIR) $(LDFLAGS)           
	
$(OBJS): %.o : %.cc
	$(CC) $(INC_DIR) $(CFLAGS) -c $< -o $@              

$(TPROG): $(OBJS)
	$(CC) $(TFLAGS) $(OBJS) -o $@ $(LIB_DIR) $(TLDFLAGS)

