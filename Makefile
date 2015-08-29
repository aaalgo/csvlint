.PHONY:	all clean 

CC=g++

STATIC = #-static
CXX = g++ #-std=c++11 -fopenmp  
OPT = -O3 -march=corei7
CXXFLAGS += -std=c++11 -fopenmp -g $(OPT) $(STATIC) -Wall -DBOOST_LOG_DYN_LINK
LDFLAGS += -fopenmp -g $(STATIC)  # -Lsegment 
LDLIBS += -lboost_program_options -lboost_log -lboost_log_setup -ltcmalloc

PROGS = csvlint-probe csvlint-stat

all:	$(PROGS)

csvlint-probe:	csvlint-probe.o csvlint.o
	
csvlint-stat:	csvlint-stat.o csvlint.o

clean:
	rm $(PROGS) *.o

