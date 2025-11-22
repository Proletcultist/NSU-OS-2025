#include <stdio.h>
#include "exc.h"

DEFEXCEPTIONS(EXCEPTION);

int main(){
	INITMAIN;

	TRY{
		THROW(EXCEPTION);
	}
	CATCH(EXCEPTION){
		printf("Caught\n");
	}
	TRYCATCHEND;

	return 0;
}
