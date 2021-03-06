#include <stdlib.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <assert.h>
#include <err.h>
#include <errno.h>
#include <atf-c.h>
#include <pledge.h>

static int __unused
try_creat(const char *path)
{
	int r;
	r = creat(path, S_IRWXU | S_IRWXG | S_IRWXO);
	if (r >= 0)
		close(r);
	return (r);
}

static int __unused
try_mkdir(const char *path)
{
	return (mkdir(path, S_IRWXU | S_IRWXG | S_IRWXO));
}

static int __unused
try_open(const char *path, int flags)
{
	int r;
	r = open(path, flags);
	if (r >= 0)
		close(r);
	return (r);
}

static int __unused
try_openat(int atfd, const char *path, int flags)
{
	int r, r2;
	r = openat(atfd, path, flags);
	if (r >= 0)
		close(r);
	if (atfd == AT_FDCWD) {
		r2 = try_open(path, flags);
		ATF_CHECK_EQ(r, r2);
	}
	return (r);
}

static int __unused
try_stat(const char *path)
{
	struct stat st;
	return (stat(path, &st));
}

static int __unused
try_statat(int atfd, const char *path)
{
	int r, r2;
	struct stat st;
	r = fstatat(atfd, path, &st, 0);
	if (atfd == AT_FDCWD) {
		r2 = try_stat(path);
		ATF_CHECK_EQ(r, r2);
	}
	return (r);
}

static int __unused
try_access(const char *path, int mode)
{
	return (eaccess(path, mode));
}

static int __unused
try_accessat(int atfd, const char *path, int mode)
{
	int r, r2;
	r = faccessat(atfd, path, mode, AT_EACCESS);
	if (atfd == AT_FDCWD) {
		r2 = try_access(path, mode);
		ATF_CHECK_EQ(r, r2);
	}
	return (r);
}

static void __unused
check_accessat(int atfd, const char *path, const char *flags)
{
	bool e, /* not hiding existence */
	     s, /* searchable (for directories) */
	     i, /* stat()-able */
	     r, /* readable */
	     w, /* writable */
	     x, /* executable (for regular files) */
	     d, /* is a directory */
	     p; /* may have extra permissions */
	e = s = i = r = w = x = d = p = false;
	for (const char *ptr = flags; *ptr; ptr++)
		switch (*ptr) {
		case 's':         s     = true; break;
		case 'e':         s = e = true; break;
		case 'i':     i = s = e = true; break;
		case 'r': r = i = s = e = true; break;
		case 'w': w =     s = e = true; break;
		case 'x': x =     s = e = true; break;
		case 'd': d =             true; break;
		case '+': p =             true; break;
		default: assert(0); break;
		}
	if (atfd == AT_FDCWD && strcmp(path, "/") == 0)
		/*
		 * This implementation always give some limited access to the
		 * root directory (see comments in libcurtain), but the deny
		 * errno is still ENOENT as if the path was not unveiled.
		 */
		s = true;

	warnx("%s: %s %s", __FUNCTION__, path, flags);

	int expected_errno = e ? EACCES : ENOENT;

	if (i)
		ATF_CHECK(try_statat(atfd, path) >= 0);
		/*
		 * TODO: Test chdir()?  It should be possible to chdir into
		 * inspectable directories but not to access their content.
		 */
	else if (!p)
		ATF_CHECK_ERRNO(expected_errno, try_statat(atfd, path) < 0);

	/*
	 * NOTE: The pledge(3)/unveil(3) library currently always maintain
	 * UPERM_SEARCH on the root directory.
	 */
	if (d && s) {
		ATF_CHECK(try_accessat(atfd, path, X_OK) >= 0);
		ATF_CHECK(try_openat(atfd, path, O_SEARCH) >= 0);
		ATF_CHECK(try_openat(atfd, path, O_PATH|O_EXEC) >= 0);
		ATF_CHECK(try_openat(atfd, path, O_SEARCH|O_DIRECTORY) >= 0);
		ATF_CHECK(try_openat(atfd, path, O_PATH|O_EXEC|O_DIRECTORY) >= 0);
	} else if (d && !s && !p) {
		ATF_CHECK_ERRNO(expected_errno, try_accessat(atfd, path, X_OK) < 0);
		ATF_CHECK_ERRNO(expected_errno, try_openat(atfd, path, O_SEARCH) < 0);
		ATF_CHECK_ERRNO(expected_errno, try_openat(atfd, path, O_PATH|O_EXEC) < 0);
		ATF_CHECK_ERRNO(expected_errno, try_openat(atfd, path, O_SEARCH|O_DIRECTORY) < 0);
		ATF_CHECK_ERRNO(expected_errno, try_openat(atfd, path, O_PATH|O_EXEC|O_DIRECTORY) < 0);
	}

	if (r) {
		ATF_CHECK(try_accessat(atfd, path, R_OK) >= 0);
		ATF_CHECK(try_openat(atfd, path, O_RDONLY) >= 0);
	} else if (!p) {
		ATF_CHECK_ERRNO(expected_errno, try_accessat(atfd, path, R_OK) < 0);
		ATF_CHECK_ERRNO(expected_errno, try_openat(atfd, path, O_RDONLY) < 0);
	}

	if (w) {
		ATF_CHECK(try_accessat(atfd, path, W_OK) >= 0);
		if (r)
			ATF_CHECK(try_accessat(atfd, path, R_OK|W_OK) >= 0);
		if (!d) {
			ATF_CHECK(try_openat(atfd, path, O_WRONLY) >= 0);
			if (r)
				ATF_CHECK(try_openat(atfd, path, O_RDWR) >= 0);
			ATF_CHECK(try_openat(atfd, path, O_WRONLY|O_CREAT) >= 0);
			if (r)
				ATF_CHECK(try_openat(atfd, path, O_RDWR|O_CREAT) >= 0);
		}
	} else if (!p) {
		ATF_CHECK_ERRNO(expected_errno, try_accessat(atfd, path, W_OK) < 0);
		ATF_CHECK_ERRNO(expected_errno, try_accessat(atfd, path, R_OK|W_OK) < 0);
		if (!d) {
			ATF_CHECK_ERRNO(expected_errno, try_openat(atfd, path, O_WRONLY) < 0);
			ATF_CHECK_ERRNO(expected_errno, try_openat(atfd, path, O_RDWR) < 0);
		}
	}

	if (!d) {
		if (x) {
			ATF_CHECK(try_accessat(atfd, path, X_OK) >= 0);
			ATF_CHECK(try_openat(atfd, path, O_EXEC) >= 0);
		} else if (!p) {
			ATF_CHECK_ERRNO(expected_errno, try_accessat(atfd, path, X_OK) < 0);
			ATF_CHECK_ERRNO(expected_errno, try_openat(atfd, path, O_EXEC) < 0);
		}
	}
}

static void __unused
check_access(const char *path, const char *flags)
{
	return (check_accessat(AT_FDCWD, path, flags));
}
