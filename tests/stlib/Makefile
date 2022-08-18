CLANG = g++

COMM_DIR = ../..

INCLUDE = -I$(COMM_DIR) -I. -I$(COMM_DIR)/stlib
STLIB_SRC = $(COMM_DIR)/stlib

# USE_TCMALLOC = -ltcmalloc
# USE_PROFILER = -lprofiler

DEBUG = -g2 -DTRACE
FLAGS = -lpthread $(USE_TCMALLOC) $(USE_PROFILER) -ldl \
	-Wno-backslash-newline-escape -Wno-format \
	-Wno-deprecated-declarations $(DEBUG) \
	-fsanitize=address -fno-omit-frame-pointer

stlib_hash_list: 
	$(CLANG) st_hash_list_test.cc $(STLIB_SRC)/st_log.cc $(STLIB_SRC)/st_test.cc $(INCLUDE) $(FLAGS) -o st_hash_list_test

stlib_buffer: 
	$(CLANG) st_buffer_test.cc $(STLIB_SRC)/st_log.cc $(STLIB_SRC)/st_test.cc $(INCLUDE) $(FLAGS) -o st_buffer_test

stlib_heap: 
	$(CLANG) st_heap_test.cc $(STLIB_SRC)/st_log.cc $(STLIB_SRC)/st_test.cc $(INCLUDE) $(FLAGS) -o st_heap_test

stlib_heap_timer: 
	$(CLANG) st_heap_timer_test.cc $(STLIB_SRC)/st_log.cc $(STLIB_SRC)/st_test.cc $(INCLUDE) $(FLAGS) -o st_heap_timer_test