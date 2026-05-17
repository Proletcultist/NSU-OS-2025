BUILD_DIR = build
OBJECTS_DIR = ${BUILD_DIR}/obj
DEPS_DIR = ${BUILD_DIR}/deps
SRC_DIR = src
INCLUDE_DIR = include

CC = gcc
CC_ARGS = -g -Wall -Wextra -I ${INCLUDE_DIR}
LIBS = -lsocket -lnsl
DEP_ARGS = -MT $@ -MMD -MP -MF ${DEPS_DIR}/$*.d

SOURCES = main.c \
          vectors_definition.c \
          maps_definition.c \
          cache/cache.c \
          proxy/util.c \
          proxy/client.c \
          proxy/server.c \
          proxy/proxy.c \
          proxy/responses.c \
          http.c \
          scheduler/aio_scheduler.c \
          scheduler/task_list.c

main: ${BUILD_DIR}/main

${OBJECTS_DIR}/%.o: ${SRC_DIR}/%.c Makefile
	mkdir -p ${@D}
	mkdir -p ${DEPS_DIR}/$(*D)
	${CC} ${DEP_ARGS} ${CC_ARGS} -c -o $@ $<

${BUILD_DIR}/main: $(SOURCES:%.c=${OBJECTS_DIR}/%.o)
	mkdir -p ${BUILD_DIR}
	${CC} ${CC_ARGS} -o $@ $^

${DEPS_DIR}/%.d: ${OBJECTS_DIR}/%.o

$(SOURCES:%.c=${DEPS_DIR}/%.d):

include $(SOURCES:%.c=${DEPS_DIR}/%.d)

.PHONY: main
