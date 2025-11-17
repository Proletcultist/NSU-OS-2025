#include <stdio.h>
#include "exc.h"

DEFEXCEPTIONS(EXCEPTION, NOTEXCEPTION);

void DEFUN(foo){
	INITEXC;

	THROW(EXCEPTION);
}

void DEFUN(bar){
	INITEXC;

	TRY{
		CALL(foo);
	}
	CATCH(EXCEPTION){
		THROW(NOTEXCEPTION);
	}
	TRYCATCHEND;
}

int main(){
	INITMAIN;

	TRY{
		CALL(bar);
	}
	CATCH(NOTEXCEPTION){
		printf("Not exception caught!\n");
	}
	TRYCATCHEND;

	return 0;
}

