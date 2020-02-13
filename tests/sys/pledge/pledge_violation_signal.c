#include <stdio.h>
#include <unistd.h>
#include <err.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/signal.h>

int main() {
	int r, status;
	pid_t pid;
	pid = fork();
	if (pid < 0)
		err(1, "fork");
	if (pid == 0) {
		r = pledge("stdio", "");
		if (r < 0)
			err(1, "pledge");
		close(open("/etc/rc", O_RDONLY));
		_exit(0);
	}
	r = waitpid(pid, &status, WEXITED);
	if (r < 0)
		err(1, "waitpid");
	if (r != pid || WTERMSIG(status) != SIGTRAP)
		errx(1, "child process did not terminate with SIGTRAP");
	return 0;
}
