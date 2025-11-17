#include <stdio.h>
#include "exc.h"

DEFEXCEPTIONS(NULLEXC);

int DEFUN(foo, int i) {
	INITEXC;

	// Throw exception
	if (i == 0){
		THROW(NULLEXC);
	}

	// Return normally
	return i - 1;
}

int main() {
	INITMAIN;

	TRY{
		int a;
		printf("Enter number: ");
		scanf("%d", &a);
		int res = CALL(foo, a);
		printf("Res: %d\n", res);
	}
	CATCH(NULLEXC){
		printf("NULLEXC\n");
	}
	TRYCATCHEND;

	return 0;
}
