BUILD_DIR = build
OBJECTS_DIR = ${BUILD_DIR}/obj
DEPS_DIR = ${BUILD_DIR}/deps
SRC_DIR = src
INCLUDE_DIR = include

CC = gcc
CC_ARGS = -g -Wall -Wextra -I ${INCLUDE_DIR}
DEP_ARGS = -MT $@ -MMD -MP -MF ${DEPS_DIR}/$*.d

SOURCES = main.c vectors_definition.c maps_definition.c proxy/proxy.c proxy/request_analysis.c scheduler/aio_scheduler.c scheduler/task_list.c

main: ${BUILD_DIR}/main

${OBJECTS_DIR}/%.o: ${SRC_DIR}/%.c ${DEPS_DIR}/%.d Makefile
	mkdir -p ${OBJECTS_DIR}/$(dir $*.c)
	mkdir -p ${DEPS_DIR}/$(dir $*.c)
	${CC} ${DEP_ARGS} ${CC_ARGS} -c -o $@ $<

${BUILD_DIR}/main: $(patsubst %.c, ${OBJECTS_DIR}/%.o, ${SOURCES})
	mkdir -p ${BUILD_DIR}
	${CC} ${CC_ARGS} -o $@ $^

${BUILD_DIR}:
	mkdir -p $@
${OBJECTS_TO_BUILD}:
	mkdir -p $@
${DEPS_DIR}:
	mkdir -p $@

$(patsubst %.c, ${DEPS_DIR}/%.d, ${SOURCES}):

include $(patsubst %.c, ${DEPS_DIR}/%.d, ${SOURCES})

.PHONY: main
