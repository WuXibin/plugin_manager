CC = g++

INC_DIR = 
LIB_DIR = 

CFLAGS = -g -shared -fPIC -W -Wall -Wno-unused-parameter -Werror
LDFLAGS = -lprotobuf

OBJS = simulator_worker.o engine_adfront.pb.o

PROG = libadserver_test.so

.PHONY: all clean

all: $(PROG)

$(PROG): $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o $@ $(LIB_DIR) $(LDFLAGS)           
	
$(OBJS): %.o : %.cc
	$(CC) $(INC_DIR) $(CFLAGS) -c $< -o $@              

$(TEST_PROG): $(TOBJS) $(OBJS)
	$(CC) $(TFLAGS) $(TOBJS) $(OBJS) -o $@ $(LIB_DIR) $(TLDFLAGS)

clean:
	-rm -rf $(OBJS) $(PROG)
