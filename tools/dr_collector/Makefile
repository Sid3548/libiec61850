LIBIEC_HOME=../..

PROJECT_BINARY_NAME = dr_collector
PROJECT_SOURCES = dr_collector.c
PROJECT_HEADERS = dr_collector_part1.inc dr_collector_part2.inc dr_collector_part3.inc dr_collector_part4.inc
CONFIG_FILE ?= dr_collector.local.json
RUN_ARGS ?=

include $(LIBIEC_HOME)/make/target_system.mk
include $(LIBIEC_HOME)/make/stack_includes.mk

all: $(PROJECT_BINARY_NAME)

include $(LIBIEC_HOME)/make/common_targets.mk

$(PROJECT_BINARY_NAME): $(PROJECT_SOURCES) $(PROJECT_HEADERS) $(LIB_NAME)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $(PROJECT_BINARY_NAME) $(PROJECT_SOURCES) $(INCLUDES) $(LIB_NAME) $(LDLIBS)

$(CONFIG_FILE):
	cp dr_collector.sample.json $(CONFIG_FILE)
	@echo "Created $(CONFIG_FILE). Edit relay.ip and relay.trigger_rcb before connecting to a relay."

run: $(PROJECT_BINARY_NAME) $(CONFIG_FILE)
	./$(PROJECT_BINARY_NAME) $(CONFIG_FILE) $(RUN_ARGS)

once: RUN_ARGS=--once
once: run

clean:
	rm -f $(PROJECT_BINARY_NAME) $(PROJECT_BINARY_NAME).exe
