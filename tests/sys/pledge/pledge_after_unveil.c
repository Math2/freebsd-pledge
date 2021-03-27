#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <pledge.h>

#include "util.h"

int
main()
{
	int fd;
	EXPECT(unveil("/etc", "r"));
	EXPECT(unveil(NULL, NULL));
	EXPECT(pledge("stdio rpath", NULL));
	REJECT(open("/tmp", O_RDONLY));
	EXPECT(fd = open("/dev/null", O_RDONLY));
	EXPECT(close(fd));
	return (0);
}
