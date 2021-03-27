#include <stdio.h>
#include <unistd.h>
#include <err.h>
#include <pledge.h>

#include "util.h"

int
main()
{
	EXPECT(pledge("stdio exec", NULL));
	EXPECT(unveilself("/", "x"));
	EXPECT(unveilself(NULL, NULL));
	EXPECT(execlp("test", "test", "-r", "/etc/rc", NULL));
	return (1);
}
