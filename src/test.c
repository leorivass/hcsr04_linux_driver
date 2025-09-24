#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>

int main() {

	int fd;
	char distancia[10];

	fd = open("/dev/hcsr00", O_RDONLY);

	while (1) {
		read(fd, distancia, sizeof(distancia));
		printf("Distancia: %s", distancia);
		sleep(1);
	}

	return 0;
}
