TEST_SRCS = $(wildcard tests/*.cc)
TESTS = $(patsubst tests/%.cc, tests/test_%, $(TEST_SRCS))

LDFLAGS = -Wl,-rpath=$(shell pwd)/lib -L$(shell pwd)/lib -lrdc -ldl -lpthread

tests/test_% : tests/%.cc $(SLIB)
	$(CXX) $(INCFLAGS) $(CFLAGS) -DLOGGING_IMPLEMENTATION=1  -MM -MT tests/$* $< >tests/$*.d
	$(CXX) $(INCFLAGS) $(CFLAGS) -DLOGGING_IMPLEMENTATION=1 -o $@ $(filter %.cc %.a, $^) $(LDFLAGS)

-include tests/*.d
