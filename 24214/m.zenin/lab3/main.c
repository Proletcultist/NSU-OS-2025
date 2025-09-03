#include <stdio.h>
#include <unistd.h>

int main(){
	printf("Real UID: %u\n", getuid());
	printf("Effective UID: %u\n\n", geteuid());

	FILE *file = fopen("iamfile", "r");
	if (!file){
		perror(NULL);
	}
	else{
		printf("Successful\n");
		fclose(file);
	}

	setuid(getuid());

	file = fopen("iamfile", "r");
	if (!file){
		perror(NULL);
	}
	else{
		printf("Successful\n");
		fclose(file);
	}

	return 0;
}
