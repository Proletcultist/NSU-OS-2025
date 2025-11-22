#pragma once

#include <setjmp.h>
#include <stdlib.h>
#include <stdio.h>

#define DEFUN(name, ...) name(jmp_buf exc_handler_up __VA_OPT__(,) __VA_ARGS__)

#define DEFEXCEPTIONS(...) enum exception { \
				NO_EXC __VA_OPT__(,) \
				__VA_ARGS__ \
			   }


#define INITEXC jmp_buf exc_handler, default_handler; \
		jmp_buf *curr_handler; \
		int exc; \
		exc = setjmp(default_handler); \
		if (exc != 0){ \
			longjmp(exc_handler_up, exc); \
		} \
		curr_handler = &default_handler

#define INITMAIN 	jmp_buf exc_handler_up; \
			int exc_main = setjmp(exc_handler_up); \
			if (exc_main != 0){ \
				fprintf(stderr, "Uncaught exception\n"); \
				exit(-1); \
			} \
			INITEXC \


#define THROW(exception) longjmp(*curr_handler, exception)

#define CALL(name, ...) name(*curr_handler __VA_OPT__(,) __VA_ARGS__)


#define TRY 		curr_handler = &exc_handler; \
			exc = setjmp(exc_handler); \
			if (exc == 0) 
#define CATCH(name) 	if (exc == name && (exc = 0) == 0)
#define TRYCATCHEND	if (exc != 0){ \
				longjmp(exc_handler_up, exc); \
			} \
			curr_handler = &default_handler

