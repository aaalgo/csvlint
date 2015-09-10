.PHONY:	all clean 

CC=g++

STATIC =
CXX = g++
OPT = -O3 -march=corei7
CXXFLAGS += -std=c++11 -fopenmp -g $(OPT) $(STATIC) -Wall -DBOOST_LOG_DYN_LINK
LDFLAGS += -fopenmp -g $(STATIC)  
LDLIBS += -lboost_program_options -lboost_log -lboost_log_setup -lboost_thread -lboost_system -ltcmalloc

PROGS = csvlint-probe csvlint-stat csvlint-dump csvlint-sample csvlint-sqlite csvlint-special csvlint-cut

all:	$(PROGS)

$(PROGS):	%:	%.o csvlint.o

clean:
	rm $(PROGS) *.o

