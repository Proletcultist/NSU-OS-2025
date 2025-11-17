#include "exc.h"

DEFEXCEPTIONS(EXCEPTION);

void DEFUN(foo){
	INITEXC;

	THROW(EXCEPTION);
}

void DEFUN(bar){
	INITEXC;

	CALL(foo);
}

int main(){
	INITMAIN;

	CALL(bar);

	return 0;
}
