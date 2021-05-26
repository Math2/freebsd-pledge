#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <assert.h>
#include <err.h>
#include <errno.h>
#include <paths.h>
#include <atf-c.h>
#include <pledge.h>

#include "path-utils.h"

ATF_TC_WITHOUT_HEAD(getcwd);
ATF_TC_BODY(getcwd, tc)
{
	char paths[2][PATH_MAX];
	ATF_REQUIRE(getcwd(paths[0], sizeof paths[0]) == paths[0]);
	ATF_REQUIRE(pledge("stdio rpath", "") >= 0);
	ATF_REQUIRE(getcwd(paths[1], sizeof paths[1]) == paths[1]);
	ATF_CHECK_STREQ(paths[0], paths[1]);
}

ATF_TC_WITHOUT_HEAD(path_exceptions);
ATF_TC_BODY(path_exceptions, tc)
{
	/* Some paths are always allowed with just the "stdio" promise. */
	struct {
		const char *path;
		const char *flags;
	} paths[] = {
		{ "/etc/malloc.conf", "r" },
		{ "/dev/null", "rw" },
		{ "/dev/random", "r" },
		{ "/dev/urandom", "r" }, /* NOTE: symlink */
		{ "/etc/localtime", "r" },
	}, *fill, *iter;
	for (fill = iter = paths; iter != &paths[nitems(paths)]; iter++)
		if (try_stat(iter->path) >= 0)
			*fill++ = *iter;
	ATF_REQUIRE(pledge("stdio", "") >= 0);
	for (iter = paths; iter != fill; iter++)
		check_access(iter->path, iter->flags);
}

ATF_TC_WITHOUT_HEAD(path_deny);
ATF_TC_BODY(path_deny, tc)
{
	ATF_REQUIRE(try_creat("0") >= 0);
	ATF_REQUIRE(try_mkdir("d") >= 0);
	ATF_REQUIRE(try_creat("d/1") >= 0);
	ATF_REQUIRE(pledge("stdio error", "") >= 0);
	/*
	 * Some files like /dev/null remain accessible, but / and /dev shouldn't.
	 */
	check_access("/", "d");
	check_access("/dev", "d");
	check_access("/etc", "d");
	check_access("/var", "d");
	check_access(".", "");
	check_access("..", "");
	check_access("0", "");
	check_access("d", "d");
	check_access("d/1", "");
	check_access("2", "");
	check_access("d/2", "");
}

ATF_TC_WITHOUT_HEAD(rpath_allow);
ATF_TC_BODY(rpath_allow, tc)
{
	ATF_REQUIRE(try_creat("0") >= 0);
	ATF_REQUIRE(try_mkdir("d") >= 0);
	ATF_REQUIRE(try_creat("d/1") >= 0);
	ATF_REQUIRE(pledge("stdio rpath", "") >= 0);
	check_access("/", "rd");
	check_access("/dev", "rd");
	check_access("/dev/null", "r+");
	check_access("/etc", "rd");
	check_access("/var", "rd");
	check_access(".", "rd");
	check_access("0", "r");
	check_access("d", "rd");
	check_access("d/1", "r");
}

ATF_TC_WITHOUT_HEAD(wpath_allow);
ATF_TC_BODY(wpath_allow, tc)
{
	ATF_REQUIRE(try_creat("0") >= 0);
	ATF_REQUIRE(try_mkdir("d") >= 0);
	ATF_REQUIRE(try_creat("d/1") >= 0);
	ATF_REQUIRE(pledge("stdio wpath", "") >= 0);
	check_access("/dev/null", "w+");
	check_access("0", "w");
	check_access("d", "wd");
	check_access("d/1", "w");
}

ATF_TC_WITHOUT_HEAD(cpath_allow);
ATF_TC_BODY(cpath_allow, tc)
{
	ATF_REQUIRE(try_creat("0") >= 0);
	ATF_REQUIRE(try_mkdir("d") >= 0);
	ATF_REQUIRE(try_creat("d/1") >= 0);
	ATF_REQUIRE(pledge("stdio cpath", "") >= 0);
	check_access("0", "e");
	check_access("d", "ed");
	check_access("d/1", "e");
	ATF_CHECK(unlink("0") >= 0);
	ATF_CHECK(unlink("d/1") >= 0);
	ATF_CHECK(rmdir("d") >= 0);
	ATF_CHECK(try_mkdir("d") >= 0);
}

ATF_TC_WITHOUT_HEAD(cpath_deny);
ATF_TC_BODY(cpath_deny, tc)
{
	ATF_REQUIRE(try_creat("0") >= 0);
	ATF_REQUIRE(try_mkdir("d") >= 0);
	ATF_REQUIRE(try_creat("d/1") >= 0);
	ATF_REQUIRE(pledge("stdio error rpath wpath", "") >= 0);
	ATF_CHECK_ERRNO(EACCES, try_creat("x") < 0);
	ATF_CHECK_ERRNO(EACCES, try_creat("d/x") < 0);
	/* XXX stop exposing Capsicum errnos? */
	ATF_CHECK_ERRNO(ECAPMODE, try_mkdir("x") < 0);
	ATF_CHECK_ERRNO(ECAPMODE, try_mkdir("d/x") < 0);
}

ATF_TC_WITHOUT_HEAD(fattr_allow);
ATF_TC_BODY(fattr_allow, tc)
{
	int fd;
	ATF_REQUIRE(try_creat("0") >= 0);
	ATF_REQUIRE(try_mkdir("d") >= 0);
	ATF_REQUIRE(try_creat("d/1") >= 0);
	ATF_REQUIRE((fd = open("0", O_WRONLY)) >= 0);
	ATF_REQUIRE(pledge("stdio fattr", "") >= 0);
	check_access("0", "e");
	check_access("d", "ed");
	check_access("d/1", "e");
	ATF_CHECK(fchmod(fd, 0000) >= 0);
	ATF_CHECK(fchmod(fd, 0600) >= 0);
	ATF_CHECK(chmod("0", 0000) >= 0);
	ATF_CHECK(chmod("0", 0600) >= 0);
	ATF_CHECK(chmod("d", 0000) >= 0);
	ATF_CHECK(chmod("d", 0700) >= 0);
	ATF_CHECK(chmod("d/1", 0000) >= 0);
	ATF_CHECK(chmod("d/1", 0600) >= 0);
}

ATF_TC_WITHOUT_HEAD(fattr_deny);
ATF_TC_BODY(fattr_deny, tc)
{
	int fd;
	ATF_REQUIRE(try_creat("0") >= 0);
	ATF_REQUIRE(try_mkdir("d") >= 0);
	ATF_REQUIRE(try_creat("d/1") >= 0);
	ATF_REQUIRE((fd = open("0", O_WRONLY)) >= 0);
	ATF_REQUIRE(pledge("stdio error rpath wpath cpath", "") >= 0);
	check_access("0", "rw");
	check_access("d", "rwd");
	check_access("d/1", "rw");
	/* XXX stop exposing Capsicum errnos? */
	ATF_CHECK_ERRNO(ECAPMODE, fchmod(fd, 0000) < 0);
	ATF_CHECK_ERRNO(ECAPMODE, fchmod(fd, 0600) < 0);
	ATF_CHECK_ERRNO(ECAPMODE, chmod("0", 0000) < 0);
	ATF_CHECK_ERRNO(ECAPMODE, chmod("d", 0000) < 0);
	ATF_CHECK_ERRNO(ECAPMODE, chmod("d/1", 0000) < 0);
}

ATF_TC_WITHOUT_HEAD(tmppath_allow);
ATF_TC_BODY(tmppath_allow, tc)
{
	const char *tmpdir;
	ATF_REQUIRE(pledge("stdio tmppath", "") >= 0);
	tmpdir = getenv("TMPDIR");
	if (!tmpdir || !*tmpdir)
		tmpdir = _PATH_TMP;
	check_access(tmpdir, "ie");

	char *path;
	ATF_REQUIRE(asprintf(&path, "%s/%s.XXXXXXXXXX", tmpdir, getprogname()) > 0);
	ATF_REQUIRE((path = mktemp(path)));

	ATF_REQUIRE(try_creat(path) >= 0);
	ATF_CHECK(try_stat(path) >= 0);
	ATF_CHECK(try_open(path, O_RDONLY) >= 0);
	ATF_CHECK(try_open(path, O_WRONLY) >= 0);
	ATF_CHECK(try_open(path, O_RDWR) >= 0);

	ATF_REQUIRE(unlink(path) >= 0);
}

ATF_TC_WITHOUT_HEAD(tmppath_deny);
ATF_TC_BODY(tmppath_deny, tc)
{
	const char *tmpdir;
	tmpdir = getenv("TMPDIR");
	if (!tmpdir || !*tmpdir)
		tmpdir = _PATH_TMP;

	char *subdir, *subfile1, *subfile2;
	ATF_REQUIRE(asprintf(&subdir, "%s/%s.XXXXXXXXXX", tmpdir, getprogname()) > 0);
	ATF_REQUIRE((subdir = mktemp(subdir)));
	ATF_REQUIRE(mkdir(subdir, 0777) >= 0);
	ATF_REQUIRE(asprintf(&subfile1, "%s/subfile1", subdir));
	ATF_REQUIRE(asprintf(&subfile2, "%s/subfile2", subdir));
	ATF_REQUIRE(try_creat(subfile1) >= 0);

	ATF_REQUIRE(pledge("stdio error tmppath", "") >= 0);
	check_access(tmpdir, "ie");

	ATF_CHECK_ERRNO(ENOENT, try_open(subdir, O_RDONLY) < 0);
	ATF_CHECK_ERRNO(ENOENT, try_open(subfile1, O_RDONLY) < 0);
	ATF_CHECK_ERRNO(ENOENT, try_creat(subfile2) < 0);

	char *path;
	ATF_REQUIRE(asprintf(&path, "%s/%s.XXXXXXXXXX", tmpdir, getprogname()) > 0);
	ATF_REQUIRE((path = mktemp(path)));

	ATF_CHECK_ERRNO(EACCES, mkdir(path, 0777) < 0);
	ATF_CHECK_ERRNO(EACCES, symlink("symlink-test", path) < 0);
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, getcwd);
	ATF_TP_ADD_TC(tp, path_exceptions);
	ATF_TP_ADD_TC(tp, path_deny);
	ATF_TP_ADD_TC(tp, rpath_allow);
	ATF_TP_ADD_TC(tp, wpath_allow);
	ATF_TP_ADD_TC(tp, cpath_allow);
	ATF_TP_ADD_TC(tp, cpath_deny);
	ATF_TP_ADD_TC(tp, fattr_allow);
	ATF_TP_ADD_TC(tp, fattr_deny);
	ATF_TP_ADD_TC(tp, tmppath_allow);
	ATF_TP_ADD_TC(tp, tmppath_deny);
	return (atf_no_error());
}
