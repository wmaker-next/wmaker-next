/*
 *  check_wmlib - unit tests for the wmlib IPC command client
 *
 *  Copyright (c) 2026 Window Maker Team
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Library General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Library General Public License for more details.
 *
 *  You should have received a copy of the GNU Library General Public
 *  License along with this library; if not, see <https://www.gnu.org/licenses/>.
 *
 *  Covers the command-reply reader (wipcreply.c) against hostile and
 *  producer-shaped input, and drives WMHideApplication end to end over
 *  a stub AF_UNIX server built on libWUtil's own framing, with no X
 *  involvement. Run by `make check`.
 *
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>

#include <WINGs/WUtil.h>
#include <WINGs/WIPC.h>

#include "WMaker.h"
#include "app.h"
#include "wipcreply.h"
#include "check_common.h"

/* ---- reply reader: accepted replies --------------------------------- */

static void test_reader_accept(void)
{
	CHECK(wipcreply_success("{success = YES;}") == 1);
	CHECK(wipcreply_success("{success = \"YES\";}") == 1);
	/* whitespace tolerated, structure not */
	CHECK(wipcreply_success("{ success = YES ; }") == 1);
	CHECK(wipcreply_success("{success = YES;} ") == 1);
	/* other keys are skipped, in any order */
	CHECK(wipcreply_success("{success = YES;error = \"boom\";}") == 1);
	CHECK(wipcreply_success("{error = \"boom\";success = YES;}") == 1);
	/* nested values in other keys must not derail the scan */
	CHECK(wipcreply_success("{success = YES;info = {pid = 4;"
				"actions = (\"restart\", \"exit {no}\");};}") == 1);
	/* quoted key spelling */
	CHECK(wipcreply_success("{\"success\" = YES;}") == 1);
	/* the last duplicate success value wins */
	CHECK(wipcreply_success("{success = NO;success = YES;}") == 1);
}

/* ---- reply reader: well-formed refusals ------------------------------ */

static void test_reader_refuse(void)
{
	CHECK(wipcreply_success("{success = NO;}") == 0);
	CHECK(wipcreply_success("{success = \"NO\";}") == 0);
	/* case matters, the writer only ever emits YES/NO */
	CHECK(wipcreply_success("{success = yes;}") == 0);
	/* missing key */
	CHECK(wipcreply_success("{}") == 0);
	CHECK(wipcreply_success("{error = \"no such window\";}") == 0);
	/* YES inside an error string must not be mistaken for success */
	CHECK(wipcreply_success("{error = \"it said YES\";success = NO;}") == 0);
	CHECK(wipcreply_success("{success = YES;success = NO;}") == 0);
	/* escaped content is never a plain YES */
	CHECK(wipcreply_success("{success = \"Y\\\\ES\";}") == 0);
	/* a non-string success value */
	CHECK(wipcreply_success("{success = {nested = 1;};}") == -1);
}

/* ---- reply reader: malformed replies --------------------------------- */

static void test_reader_malformed(void)
{
	CHECK(wipcreply_success(NULL) == -1);
	CHECK(wipcreply_success("") == -1);
	CHECK(wipcreply_success("success = YES;") == -1);
	CHECK(wipcreply_success("junk{success = YES;}") == -1);
	CHECK(wipcreply_success("{success = YES") == -1);
	CHECK(wipcreply_success("{success = YES}  ") == -1);
	CHECK(wipcreply_success("{success = \"YES;}") == -1);
	CHECK(wipcreply_success("{success YES;}") == -1);
	CHECK(wipcreply_success("{success = ;}") == -1);
	CHECK(wipcreply_success("{success = YES,}") == -1);
	CHECK(wipcreply_success("{success = YES;;}") == -1);
	CHECK(wipcreply_success("{success = YES;}extra") == -1);
	CHECK(wipcreply_success("{success = \"Y") == -1);
	CHECK(wipcreply_success("{\\}") == -1);
}

/* ---- reply reader: hostile input ------------------------------------- */

static void test_reader_hostile(void)
{
	char buf[8192];
	size_t i;

	/* deep nesting beyond the skip cap is malformed, not a crash */
	memcpy(buf, "{success = YES;x = ", 19);
	for (i = 0; i < 64; i++)
		memcpy(buf + 19 + 2 * i, "{a=", 3);
	buf[19 + 2 * 64] = '\0';
	CHECK(wipcreply_success(buf) == -1);

	/* the cap must reject a terminated over-depth value */
	memcpy(buf, "{success = YES;x = ", 19);
	for (i = 0; i < 32; i++)
		buf[19 + i] = '{';
	for (i = 0; i < 32; i++)
		buf[19 + 32 + i] = '}';
	buf[19 + 64] = ';';
	buf[19 + 65] = '}';
	buf[19 + 66] = '\0';
	CHECK(wipcreply_success(buf) == -1);

	/* large flat payload scans in linear time */
	memcpy(buf, "{success = YES;junk = \"", 23);
	memset(buf + 23, 'a', sizeof(buf) - 27);
	memcpy(buf + sizeof(buf) - 4, "\";}", 3);
	buf[sizeof(buf) - 1] = '\0';
	CHECK(wipcreply_success(buf) == 1);
}

/* ---- producer cross-check -------------------------------------------- */

/* build replies through the real writer (WUtil proplist) exactly like
 * src/ipc.c does, so the reader and the producer cannot drift apart */

static void test_producer_replies(void)
{
	WMPropList *d, *k, *v, *arr, *s1, *s2;
	char *desc;

	/* default wrapper, accepted */
	d = WMCreatePLDictionary(NULL, NULL);
	k = WMCreatePLString("success");
	v = WMCreatePLString("YES");
	WMPutInPLDictionary(d, k, v);
	WMReleasePropList(k);
	WMReleasePropList(v);
	desc = WMGetPropListDescription(d, false);
	CHECK(wipcreply_success(desc) == 1);
	wfree(desc);
	WMReleasePropList(d);

	/* refusal with a messy error string */
	d = WMCreatePLDictionary(NULL, NULL);
	k = WMCreatePLString("success");
	v = WMCreatePLString("NO");
	WMPutInPLDictionary(d, k, v);
	WMReleasePropList(k);
	WMReleasePropList(v);
	k = WMCreatePLString("error");
	v = WMCreatePLString("can't do \"this\"\nnow \\ escaped");
	WMPutInPLDictionary(d, k, v);
	WMReleasePropList(k);
	WMReleasePropList(v);
	desc = WMGetPropListDescription(d, false);
	CHECK(wipcreply_success(desc) == 0);
	wfree(desc);
	WMReleasePropList(d);

	/* info-shaped reply: flat keys plus an array of strings */
	d = WMCreatePLDictionary(NULL, NULL);
	k = WMCreatePLString("version");
	v = WMCreatePLString("0.0.1");
	WMPutInPLDictionary(d, k, v);
	WMReleasePropList(k);
	WMReleasePropList(v);
	s1 = WMCreatePLString("reconfigure");
	s2 = WMCreatePLString("restart");
	arr = WMCreatePLArray(s1, s2, NULL);
	WMReleasePropList(s1);
	WMReleasePropList(s2);
	k = WMCreatePLString("actions");
	WMPutInPLDictionary(d, k, arr);
	WMReleasePropList(k);
	WMReleasePropList(arr);
	k = WMCreatePLString("success");
	v = WMCreatePLString("YES");
	WMPutInPLDictionary(d, k, v);
	WMReleasePropList(k);
	WMReleasePropList(v);
	desc = WMGetPropListDescription(d, false);
	CHECK(wipcreply_success(desc) == 1);
	wfree(desc);
	WMReleasePropList(d);
}

/* ---- end-to-end over the real transport ------------------------------ */

/* stub server: child process accepting one connection on the override
 * socket, validating the request and answering with the real writer */

#define STUB_SOCKDIR_TEMPLATE "/tmp/check_wmlib-XXXXXX"

static int stub_serve(const char *sockpath, int readyfd)
{
	struct sockaddr_un addr;
	int lfd, cfd, ok;
	uint32_t type, len;
	char *req, *reply;

	lfd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (lfd < 0)
		return 1;

	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, sockpath, sizeof(addr.sun_path) - 1);

	if (bind(lfd, (struct sockaddr *)&addr, sizeof(addr)) < 0 ||
	    listen(lfd, 1) < 0) {
		close(lfd);
		return 1;
	}

	if (write(readyfd, "r", 1) != 1) {
		close(lfd);
		return 1;
	}

	cfd = accept(lfd, NULL, NULL);
	if (cfd < 0) {
		close(lfd);
		return 1;
	}

	ok = 0;
	req = NULL;
	if (wIPCRecv(cfd, &type, &req, &len) == 0 && type == WIPC_COMMAND &&
	    req != NULL && strncmp(req, "((action Hide ", 14) == 0) {
		WMPropList *d, *k, *v;

		d = WMCreatePLDictionary(NULL, NULL);
		k = WMCreatePLString("success");
		v = WMCreatePLString("YES");
		WMPutInPLDictionary(d, k, v);
		WMReleasePropList(k);
		WMReleasePropList(v);
		reply = WMGetPropListDescription(d, false);
		WMReleasePropList(d);

		ok = (wIPCSend(cfd, WIPC_COMMAND, reply, strlen(reply)) == 0);
		wfree(reply);
	}
	wfree(req);
	close(cfd);
	close(lfd);
	return ok ? 0 : 1;
}

static void test_e2e_hide(void)
{
	char sockdir[] = STUB_SOCKDIR_TEMPLATE;
	char sockpath[64];
	WMAppContext fake;
	pid_t kid;
	int readyfd[2], status;
	char b;
	char *p;

	CHECK(mkdtemp(sockdir) != NULL);
	snprintf(sockpath, sizeof(sockpath), "%s/sock", sockdir);
	setenv("WMAKER_IPC_SOCKET", sockpath, 1);

	CHECK(pipe(readyfd) == 0);

	memset(&fake, 0, sizeof(fake));
	fake.main_window = 0x1234;

	kid = fork();
	if (kid == 0) {
		close(readyfd[0]);
		_exit(stub_serve(sockpath, readyfd[1]));
	}
	CHECK(kid > 0);
	close(readyfd[1]);

	CHECK(read(readyfd[0], &b, 1) == 1 && b == 'r');
	close(readyfd[0]);

	/* the IPC path must answer without ever reaching the legacy
	 * root-window fallback, which has no display here */
	WMHideApplication(&fake);

	CHECK(waitpid(kid, &status, 0) == kid);
	CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);

	unsetenv("WMAKER_IPC_SOCKET");
	p = sockpath;
	(void)unlink(p);
	(void)rmdir(sockdir);
}

int main(void)
{
	test_reader_accept();
	test_reader_refuse();
	test_reader_malformed();
	test_reader_hostile();
	test_producer_replies();
	test_e2e_hide();

	return check_report("check_wmlib");
}
