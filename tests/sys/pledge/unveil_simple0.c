#include <stdio.h>
#include <unistd.h>
#include <pledge.h>

#include "util.h"

int
main()
{
	int r;
	EXPECT(unveil("/", "r"));
	EXPECT(unveil(NULL, NULL));
	r = write(STDOUT_FILENO, "ok!\n", 4);
	if (r < 0)
		err(1, "write");
	return (0);
}
