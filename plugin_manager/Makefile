CC = g++

INC_DIR = -I/home/w/include
LIB_DIR = -L/home/w/lib64

CFLAGS = -g -shared -fPIC -W -Wall -Wno-unused-parameter -Werror
LDFLAGS = -lprotobuf -ldl

OBJS = plugin_manager.o plugin_manager.conf.pb.o

PROG = libplugin_manager.so

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
