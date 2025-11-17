#include "exc.h"

DEFEXCEPTIONS(EXCEPTION);

void DEFUN(foo){
	INITEXC;

	THROW(EXCEPTION);
}

int main(){
	INITMAIN;

	CALL(foo);

	return 0;
}
