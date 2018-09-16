BENCH_SRCS = $(wildcard benchs/*.cc)
BENCHS = $(patsubst benchs/%.cc, benchs/bench_%, $(BENCH_SRCS))

LDFLAGS = -Wl,-rpath=$(shell pwd)/lib -L$(shell pwd)/lib -lrdc -ldl -pthread

benchs/bench_% : benchs/%.cc $(SLIB)
	$(CXX) $(INCFLAGS) $(CFLAGS) -DLOGGING_IMPLEMENTATION=1  -MM -MT benchs/$* $< >benchs/$*.d
	$(CXX) $(INCFLAGS) $(CFLAGS) -DLOGGING_IMPLEMENTATION=1 -o $@ $(filter %.cc %.a, $^) $(LDFLAGS)

-include benchs/*.d
