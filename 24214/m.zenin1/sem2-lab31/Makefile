BUILD_DIR = build
OBJECTS_DIR = ${BUILD_DIR}/obj
DEPS_DIR = ${BUILD_DIR}/deps
SRC_DIR = src
INCLUDE_DIR = include

CC = gcc
CC_ARGS = -g -Wall -Wextra -I ${INCLUDE_DIR}
DEP_ARGS = -MT $@ -MMD -MP -MF ${DEPS_DIR}/$*.d

SOURCES = main.c cache.c vectors_definition.c exchange_buffer.c aio_scheduler.c

main: ${BUILD_DIR}/main

${OBJECTS_DIR}/%.o: ${SRC_DIR}/%.c ${DEPS_DIR}/%.d Makefile | ${DEPS_DIR} ${OBJECTS_DIR}
	${CC} ${DEP_ARGS} ${CC_ARGS} -c -o $@ $<

${BUILD_DIR}/main: $(patsubst %.c, ${OBJECTS_DIR}/%.o, ${SOURCES}) | ${BUILD_DIR}
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
