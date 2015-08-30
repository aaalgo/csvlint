.PHONY:	all clean 

CC=g++

STATIC =
CXX = g++
OPT = -O3 -march=corei7
CXXFLAGS += -std=c++11 -fopenmp -g $(OPT) $(STATIC) -Wall -DBOOST_LOG_DYN_LINK
LDFLAGS += -fopenmp -g $(STATIC)  
LDLIBS += csvlint.o -lboost_program_options -lboost_log -lboost_log_setup -ltcmalloc

PROGS = csvlint.o csvlint-probe csvlint-stat csvlint-dump

all:	$(PROGS)

clean:
	rm $(PROGS) *.o

