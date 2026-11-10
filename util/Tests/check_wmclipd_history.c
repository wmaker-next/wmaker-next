/*
 *  wmclipd history regression tests
 *
 *  Copyright (c) 2026 Window Maker Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 */

#include <sys/socket.h>

#define main wmclipd_program_main
#include "../wmclipd.c"
#undef main

#include "check_common.h"

static int mock_clipboard_claim;
static int mock_primary_claim;
static int mock_claim_selection(xcb_atom_t selection, ClipBuffer *buf, xcb_timestamp_t timestamp);

static void test_reorder(void)
{
	unsigned char a[] = "first", b[] = "second";
	ClipBuffer buf = { 0 };
	ClipHistoryEntry *first, *second;

	history_enabled = 1;
	history.capacity = 10;
	buf.kind = CONTENT_CAPTURED;
	buf.type = 100;
	buf.data = a;
	buf.size = sizeof(a);
	history_add_from_buffer(&buf);
	first = history.entries[0];
	first->pinned = 1;
	history.pinned_count = 1;
	first->wall_time = 1;
	first->mono_ms = 1;

	buf.data = b;
	buf.size = sizeof(b);
	history_add_from_buffer(&buf);
	second = history.entries[0];
	popup_history_dirty = 0;
	buf.data = a;
	buf.size = sizeof(a);
	history_add_from_buffer(&buf);
	CHECK(history.count == 2);
	CHECK(history.entries[0] == first);
	CHECK(history.entries[1] == second);
	CHECK(first->pinned == 1 && history.pinned_count == 1);
	CHECK(first->wall_time > 1 && first->mono_ms > 1);
	CHECK(popup_history_dirty == 1);

	popup_history_dirty = 0;
	history_add_from_buffer(&buf);
	CHECK(history.count == 2 && history.entries[0] == first);
	CHECK(popup_history_dirty == 0);
	history_clear_all();
}

static void test_metadata(void)
{
	unsigned char data[] = "image";
	ClipBuffer buf = { 0 };
	ClipHistoryEntry *first;

	history.capacity = 10;
	buf.kind = CONTENT_SCREENSHOT;
	buf.type = 100;
	buf.data = data;
	buf.size = sizeof(data);
	buf.path = "/tmp/first";
	buf.uri = "file:///tmp/first\r\n";
	history_add_from_buffer(&buf);
	first = history.entries[0];
	buf.path = "/tmp/second";
	buf.uri = "file:///tmp/second\r\n";
	history_add_from_buffer(&buf);
	CHECK(history.count == 2);
	buf.path = "/tmp/first";
	buf.uri = "file:///tmp/first\r\n";
	history_add_from_buffer(&buf);
	CHECK(history.count == 2 && history.entries[0] == first);
	buf.uri = NULL;
	history_add_from_buffer(&buf);
	CHECK(history.count == 3);
	buf.path = NULL;
	history_add_from_buffer(&buf);
	CHECK(history.count == 4);
	buf.kind = CONTENT_CAPTURED;
	history_add_from_buffer(&buf);
	CHECK(history.count == 5);
	buf.type = 101;
	history_add_from_buffer(&buf);
	CHECK(history.count == 6);
	history_clear_all();
}

static void test_existing_duplicates(void)
{
	unsigned char a[] = "first", b[] = "second";
	ClipBuffer buf = { 0 };
	ClipHistoryEntry *duplicate, *first, *other;

	history.capacity = 10;
	buf.kind = CONTENT_CAPTURED;
	buf.type = 100;
	buf.data = a;
	buf.size = sizeof(a);
	buf.path = "/one";
	history_add_from_buffer(&buf);
	first = history.entries[0];
	buf.path = "/two";
	history_add_from_buffer(&buf);
	duplicate = history.entries[0];
	wfree(duplicate->image_path);
	duplicate->image_path = wstrdup("/one");
	buf.path = NULL;
	buf.data = b;
	buf.size = sizeof(b);
	history_add_from_buffer(&buf);
	other = history.entries[0];

	buf.path = "/one";
	buf.data = a;
	buf.size = sizeof(a);
	history_add_from_buffer(&buf);
	CHECK(history.count == 3);
	CHECK(history.entries[0] == duplicate);
	CHECK(history.entries[1] == other);
	CHECK(history.entries[2] == first);
	history_clear_all();
}

static void check_rejected_file(const char *path, const unsigned char *bytes, size_t len)
{
	unsigned char actual[1024];
	ssize_t got;
	int fd;

	fd = open(path, O_WRONLY | O_TRUNC);
	CHECK(fd >= 0);
	if (fd < 0)
		return;
	CHECK(write(fd, bytes, len) == (ssize_t)len);
	CHECK(close(fd) == 0);
	history.capacity = 10;
	history_writes_suppressed = 0;
	history_load();
	CHECK(history.count == 0 && history_writes_suppressed);
	history_save();
	fd = open(path, O_RDONLY);
	CHECK(fd >= 0);
	got = fd >= 0 ? read(fd, actual, sizeof(actual)) : -1;
	if (fd >= 0)
		CHECK(close(fd) == 0);
	CHECK(got == (ssize_t)len);
	if (got == (ssize_t)len)
		CHECK(memcmp(actual, bytes, len) == 0);
	history_clear_all();
}

static void test_persistence(void)
{
	unsigned char a[] = "/tmp/path", b[] = "other", bytes[1024], check[1024], bad[1024];
	ClipBuffer buf = { 0 };
	char path[] = "/tmp/wmclipd-history-unit-XXXXXX";
	ssize_t len, got;
	size_t first, second, path_offset;
	struct stat st;
	int fd;

	fd = mkstemp(path);
	CHECK(fd >= 0);
	if (fd < 0)
		return;
	CHECK(close(fd) == 0);
	prog = "check_wmclipd_history";
	atom_png = 100;
	atom_utf8 = 101;
	history.capacity = 10;
	persist_enabled = 1;
	history_file_override = path;
	buf.kind = CONTENT_CAPTURED;
	buf.type = atom_utf8;
	buf.data = a;
	buf.size = sizeof(a) - 1;
	buf.path = "/tmp/path";
	buf.uri = "file:///tmp/path\r\n";
	history_add_from_buffer(&buf);
	buf.data = b;
	buf.size = sizeof(b);
	buf.path = NULL;
	buf.uri = NULL;
	history_add_from_buffer(&buf);
	history_save();

	fd = open(path, O_RDONLY);
	CHECK(fd >= 0);
	len = fd >= 0 ? read(fd, bytes, sizeof(bytes)) : -1;
	if (fd >= 0)
		CHECK(close(fd) == 0);
	CHECK(len > 16 && (size_t)len < sizeof(bytes));
	if (len <= 16 || (size_t)len >= sizeof(bytes))
		goto out;
	CHECK(get_u32(bytes + 8) == 2);
	CHECK(get_u32(bytes + 12) == 2);

	history_clear_all();
	history.capacity = 10;
	history_load();
	CHECK(history.count == 2);
	if (history.count == 2) {
		CHECK_STR(history.entries[1]->image_path, "/tmp/path");
		CHECK_STR(history.entries[1]->uri, "file:///tmp/path\r\n");
		copy_entry_to_buffer(history.entries[1], &clipboard_buffer);
		CHECK(buffer_is_saved_path(&clipboard_buffer));
		clear_buffer(&clipboard_buffer);
	}
	history_clear_all();
	history.capacity = 10;

	memcpy(bad, bytes, (size_t)len);
	put_u32(bad + 12, UINT32_MAX);
	check_rejected_file(path, bad, (size_t)len);
	memcpy(bad, bytes, (size_t)len);
	bad[len] = 0;
	check_rejected_file(path, bad, (size_t)len + 1);
	first = 16 + 4 + get_u16(bytes + 18);
	memcpy(bad, bytes, (size_t)len);
	put_u64(bad + first, UINT64_MAX);
	check_rejected_file(path, bad, (size_t)len);
	memcpy(bad, bytes, (size_t)len);
	put_u64(bad + first + 8, UINT64_MAX);
	check_rejected_file(path, bad, (size_t)len);
	second = first + 16 + sizeof(b) + 8;
	path_offset = second + 4 + get_u16(bytes + second + 2) + 16 + sizeof(a) - 1;
	CHECK(path_offset + 4 < (size_t)len);
	if (path_offset + 4 < (size_t)len) {
		memcpy(bad, bytes, (size_t)len);
		bad[path_offset + 4] = '\0';
		check_rejected_file(path, bad, (size_t)len);
		memcpy(bad, bytes, (size_t)len);
		put_u32(bad + path_offset, UINT32_MAX);
		check_rejected_file(path, bad, (size_t)len);
	}

	fd = open(path, O_WRONLY | O_TRUNC);
	CHECK(fd >= 0);
	if (fd < 0)
		goto out;
	CHECK(write(fd, bytes, (size_t)len - 1) == len - 1);
	CHECK(close(fd) == 0);
	history_writes_suppressed = 0;
	history_load();
	CHECK(history.count == 0);
	CHECK(history_writes_suppressed == 1);
	history_save();
	fd = open(path, O_RDONLY);
	CHECK(fd >= 0);
	got = fd >= 0 ? read(fd, check, sizeof(check)) : -1;
	if (fd >= 0)
		CHECK(close(fd) == 0);
	CHECK(got == len - 1);
	if (got == len - 1)
		CHECK(memcmp(check, bytes, (size_t)got) == 0);

	CHECK(unlink(path) == 0);
	CHECK(mkfifo(path, 0600) == 0);
	history_writes_suppressed = 0;
	history_load();
	CHECK(history.count == 0 && history_writes_suppressed);
	history_save();
	CHECK(lstat(path, &st) == 0 && S_ISFIFO(st.st_mode));

out:
	history_clear_all();
	history_file_override = NULL;
	history_writes_suppressed = 0;
	persist_enabled = 0;
	CHECK(unlink(path) == 0);
}

static void test_saved_path_targets(void)
{
	unsigned char path[] = "/tmp/\xc3\xa9";
	static const unsigned char valid[] = { 'x', 0, 0xf0, 0x9f, 0x98, 0x80 };
	static const unsigned char overlong[] = { 0xc0, 0x80 };
	static const unsigned char surrogate[] = { 0xed, 0xa0, 0x80 };
	static const unsigned char too_high[] = { 0xf4, 0x90, 0x80, 0x80 };
	static const unsigned char truncated[] = { 0xe2, 0x82 };
	ClipBuffer buf = { 0 }, copied = { 0 };
	xcb_atom_t targets[12], type;
	const unsigned char *data;
	size_t size;
	int count, i, saw_uri, saw_string;

	CHECK(valid_utf8(valid, sizeof(valid)));
	CHECK(!valid_utf8(overlong, sizeof(overlong)));
	CHECK(!valid_utf8(surrogate, sizeof(surrogate)));
	CHECK(!valid_utf8(too_high, sizeof(too_high)));
	CHECK(!valid_utf8(truncated, sizeof(truncated)));
	atom_utf8 = 101;
	atom_uri_list = 102;
	atom_text_plain = 103;
	atom_text_plain_utf8 = 104;
	atom_targets = 105;
	buf.kind = CONTENT_CAPTURED;
	buf.type = atom_utf8;
	buf.data = path;
	buf.size = sizeof(path) - 1;
	buf.path = (char *)path;
	buf.uri = "file:///tmp/%C3%A9\r\n";
	count = make_targets(&buf, targets);
	saw_uri = saw_string = 0;
	for (i = 0; i < count; i++) {
		saw_uri |= targets[i] == atom_uri_list;
		saw_string |= targets[i] == XCB_ATOM_STRING;
	}
	CHECK(saw_uri && !saw_string);
	CHECK(payload_for_request(&buf, atom_uri_list, &data, &size, &type));
	CHECK(type == atom_uri_list && size == strlen(buf.uri));
	CHECK(!payload_for_request(&buf, XCB_ATOM_STRING, &data, &size, &type));
	CHECK(copy_captured_payload(&copied, &buf));
	CHECK(buffer_is_saved_path(&copied));
	clear_buffer(&copied);
}

static void test_version_one(void)
{
	unsigned char bytes[64], *p;
	char path[] = "/tmp/wmclipd-v1-unit-XXXXXX";
	int fd;

	fd = mkstemp(path);
	CHECK(fd >= 0);
	if (fd < 0)
		return;
	memcpy(bytes, CLIP_HISTORY_MAGIC, 8);
	put_u32(bytes + 8, 1);
	put_u32(bytes + 12, 1);
	p = bytes + 16;
	p[0] = CONTENT_CAPTURED;
	p[1] = 0;
	put_u16(p + 2, 9);
	memcpy(p + 4, "image/png", 9);
	p += 13;
	put_u64(p, (uint64_t)time(NULL));
	put_u64(p + 8, 3);
	p += 16;
	memcpy(p, "abc", 3);
	p += 3;
	CHECK(write(fd, bytes, (size_t)(p - bytes)) == p - bytes);
	CHECK(close(fd) == 0);
	prog = "check_wmclipd_history";
	atom_png = 100;
	history.capacity = 10;
	persist_enabled = 1;
	history_file_override = path;
	history_load();
	CHECK(history.count == 1);
	if (history.count == 1) {
		CHECK(history.entries[0]->size == 3);
		CHECK(memcmp(history.entries[0]->data, "abc", 3) == 0);
	}
	history_clear_all();
	history_file_override = NULL;
	history_writes_suppressed = 0;
	persist_enabled = 0;
	CHECK(unlink(path) == 0);
}

static void restore_env(const char *name, char *value)
{
	if (value) {
		CHECK(setenv(name, value, 1) == 0);
		free(value);
	} else {
		CHECK(unsetenv(name) == 0);
	}
}

static int write_fixture(const char *path, const char *content)
{
	size_t len = strlen(content);
	int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	ssize_t written;
	int closed;

	if (fd < 0)
		return 0;
	written = write(fd, content, len);
	closed = close(fd);
	return written == (ssize_t)len && closed == 0;
}

static void test_export_templates(void)
{
	char root[] = "/tmp/wmclipd-template-unit-XXXXXX";
	char user[512], global[512], stem[256], long_format[300];
	char *format, *uri, *old_tz;
	time_t when = 1609459200;

	if (!mkdtemp(root)) {
		CHECK(0);
		return;
	}
	snprintf(user, sizeof(user), "%s/user", root);
	snprintf(global, sizeof(global), "%s/global", root);
	old_tz = getenv("TZ") ? strdup(getenv("TZ")) : NULL;
	CHECK(setenv("TZ", "UTC", 1) == 0);
	tzset();
	CHECK(read_export_template(user, global, &format));
	CHECK_STR(format, "clipboard_%Y-%m-%d_at_%H:%M:%S");
	wfree(format);
	CHECK(write_fixture(global, "{ clipboardfilenametemplate = \"global_%Y\"; }"));
	CHECK(write_fixture(user, "{ unrelated = value; }"));
	CHECK(read_export_template(user, global, &format));
	CHECK_STR(format, "global_%Y");
	CHECK(format_export_stem(format, when, stem, sizeof(stem)));
	CHECK_STR(stem, "global_2021");
	wfree(format);
	CHECK(write_fixture(user, "{ ClipboardFilenameTemplate = \"first_%H\"; }"));
	CHECK(read_export_template(user, global, &format));
	CHECK_STR(format, "first_%H");
	wfree(format);
	CHECK(write_fixture(user, "{ clipboardfilenametemplate = \"second_%M\"; }"));
	CHECK(read_export_template(user, global, &format));
	CHECK_STR(format, "second_%M");
	wfree(format);
	CHECK(write_fixture(user, "{ ClipboardFilenameTemplate = (bad); }"));
	CHECK(!read_export_template(user, global, &format));
	CHECK(write_fixture(user, "{ ClipboardFilenameTemplate = \"broken\"; "));
	CHECK(!read_export_template(user, global, &format));
	CHECK(write_fixture(user, "(not, a, dictionary)"));
	CHECK(!read_export_template(user, global, &format));
	CHECK(unlink(user) == 0);
	CHECK(read_export_template(user, global, &format));
	CHECK_STR(format, "global_%Y");
	wfree(format);
	CHECK(unlink(global) == 0);
	CHECK(read_export_template(user, global, &format));
	CHECK_STR(format, DEF_CLIPBOARD_FILENAME_TEMPLATE);
	wfree(format);
	CHECK(!format_export_stem("", when, stem, sizeof(stem)));
	CHECK(!format_export_stem(".", when, stem, sizeof(stem)));
	CHECK(!format_export_stem("..", when, stem, sizeof(stem)));
	CHECK(!format_export_stem("../bad", when, stem, sizeof(stem)));
	CHECK(!format_export_stem("bad\\name", when, stem, sizeof(stem)));
	CHECK(!format_export_stem("bad_%Q", when, stem, sizeof(stem)));
	memset(long_format, 'a', sizeof(long_format) - 1);
	long_format[sizeof(long_format) - 1] = '\0';
	CHECK(!format_export_stem(long_format, when, stem, sizeof(stem)));
	uri = file_uri_from_path("/tmp/a #\xc3\xa9%");
	CHECK_STR(uri, "file:///tmp/a%20%23%C3%A9%25\r\n");
	wfree(uri);
	restore_env("TZ", old_tz);
	tzset();
	CHECK(rmdir(root) == 0);
}

static void test_export_files(void)
{
	static const unsigned char data[] = { 'x', 0, 0xff, '\n' };
	char root[] = "/tmp/wmclipd-export-unit-XXXXXX";
	char user[512], global[512], xdg[512], wm[512], dir[512];
	char fixed[512], second[512], target[512], invalid[512];
	char invalid_wm[512], invalid_dir[512], invalid_file[512];
	char masked[512], masked_wm[512], masked_dir[512];
	char *old_home, *old_data, *path;
	unsigned char actual[sizeof(data)];
	xcb_atom_t types[8];
	ClipHistoryEntry item = { 0 }, *original, *other;
	ClipBuffer buf = { 0 };
	unsigned char *old_clipboard, *old_primary;
	const unsigned char *selection_data;
	size_t selection_size;
	xcb_atom_t selection_type;
	struct stat st;
	mode_t old_umask;
	int fd, i;

	if (!mkdtemp(root)) {
		CHECK(0);
		return;
	}
	old_home = getenv("HOME") ? strdup(getenv("HOME")) : NULL;
	old_data = getenv("XDG_DATA_HOME") ? strdup(getenv("XDG_DATA_HOME")) : NULL;
	snprintf(user, sizeof(user), "%s/user", root);
	snprintf(global, sizeof(global), "%s/global", root);
	snprintf(xdg, sizeof(xdg), "%s/data", root);
	snprintf(wm, sizeof(wm), "%s/data/WindowMaker", root);
	snprintf(dir, sizeof(dir), "%s/data/WindowMaker/Clipboard", root);
	snprintf(fixed, sizeof(fixed), "%s/data/WindowMaker/Clipboard/fixed.txt", root);
	snprintf(second, sizeof(second), "%s/data/WindowMaker/Clipboard/fixed_1.txt", root);
	snprintf(target, sizeof(target), "%s/target", root);
	snprintf(invalid, sizeof(invalid), "%s/bad\xff", root);
	snprintf(invalid_wm, sizeof(invalid_wm), "%s/bad\xff/WindowMaker", root);
	snprintf(invalid_dir, sizeof(invalid_dir), "%s/bad\xff/WindowMaker/Clipboard", root);
	snprintf(invalid_file, sizeof(invalid_file), "%s/bad\xff/WindowMaker/Clipboard/fixed.txt", root);
	snprintf(masked, sizeof(masked), "%s/masked", root);
	snprintf(masked_wm, sizeof(masked_wm), "%s/masked/WindowMaker", root);
	snprintf(masked_dir, sizeof(masked_dir), "%s/masked/WindowMaker/Clipboard", root);
	CHECK(setenv("HOME", root, 1) == 0);
	CHECK(setenv("XDG_DATA_HOME", xdg, 1) == 0);
	CHECK(write_fixture(user, "{ ClipboardFilenameTemplate = fixed; }"));
	atom_utf8 = 101;
	atom_text = 102;
	atom_compound_text = 103;
	atom_text_plain = 104;
	atom_text_plain_utf8 = 105;
	atom_png = 106;
	atom_jpeg = 107;
	atom_uri_list = 108;
	types[0] = atom_utf8;
	types[1] = XCB_ATOM_STRING;
	types[2] = atom_text;
	types[3] = atom_compound_text;
	types[4] = atom_text_plain;
	types[5] = atom_text_plain_utf8;
	types[6] = atom_png;
	types[7] = atom_jpeg;
	item.kind = CONTENT_CAPTURED;
	item.data = (unsigned char *)data;
	item.size = sizeof(data);
	for (i = 0; i < 8; i++) {
		item.type = types[i];
		path = export_entry_file(&item, user, global);
		CHECK(path != NULL);
		if (!path)
			continue;
		CHECK_STR(strrchr(path, '.'), i == 6 ? ".png" : i == 7 ? ".jpg" : ".txt");
		CHECK(stat(path, &st) == 0 && (st.st_mode & 0777) == 0600);
		fd = open(path, O_RDONLY);
		CHECK(fd >= 0);
		if (fd >= 0) {
			CHECK(read(fd, actual, sizeof(actual)) == (ssize_t)sizeof(actual));
			CHECK(memcmp(actual, data, sizeof(data)) == 0);
			CHECK(close(fd) == 0);
		}
		CHECK(unlink(path) == 0);
		wfree(path);
	}
	CHECK(stat(dir, &st) == 0 && (st.st_mode & 0777) == 0700);
	item.type = atom_utf8;
	old_umask = umask(0777);
	path = export_entry_file(&item, user, global);
	umask(old_umask);
	CHECK(path != NULL);
	if (path) {
		CHECK(stat(path, &st) == 0 && (st.st_mode & 0777) == 0600);
		CHECK(unlink(path) == 0);
		wfree(path);
	}
	CHECK(setenv("XDG_DATA_HOME", masked, 1) == 0);
	old_umask = umask(0777);
	path = export_entry_file(&item, user, global);
	umask(old_umask);
	CHECK(path != NULL);
	CHECK(stat(masked, &st) == 0 && (st.st_mode & 0777) == 0700);
	CHECK(stat(masked_wm, &st) == 0 && (st.st_mode & 0777) == 0700);
	CHECK(stat(masked_dir, &st) == 0 && (st.st_mode & 0777) == 0700);
	if (path) {
		CHECK(stat(path, &st) == 0 && (st.st_mode & 0777) == 0600);
		CHECK(unlink(path) == 0);
		wfree(path);
	}
	CHECK(rmdir(masked_dir) == 0);
	CHECK(rmdir(masked_wm) == 0);
	CHECK(rmdir(masked) == 0);
	CHECK(setenv("XDG_DATA_HOME", xdg, 1) == 0);
	CHECK(symlink(target, fixed) == 0);
	path = export_entry_file(&item, user, global);
	CHECK_STR(path, second);
	CHECK(lstat(fixed, &st) == 0 && S_ISLNK(st.st_mode));
	CHECK(access(target, F_OK) < 0 && errno == ENOENT);
	if (path) {
		CHECK(unlink(path) == 0);
		wfree(path);
	}
	CHECK(unlink(fixed) == 0);

	history.capacity = 1;
	buf.kind = CONTENT_CAPTURED;
	buf.type = atom_utf8;
	buf.data = (unsigned char *)data;
	buf.size = sizeof(data);
	history_add_from_buffer(&buf);
	original = history.entries[0];
	clipboard_buffer.kind = CONTENT_CAPTURED;
	clipboard_buffer.data = (unsigned char *)wstrdup("old clipboard");
	clipboard_buffer.size = strlen((char *)clipboard_buffer.data);
	clipboard_buffer.owns = 1;
	primary_buffer.kind = CONTENT_CAPTURED;
	primary_buffer.data = (unsigned char *)wstrdup("old primary");
	primary_buffer.size = strlen((char *)primary_buffer.data);
	primary_pending_ts = 7;
	primary_pending_deadline = 42;
	atom_clipboard = 200;
	atom_primary = 201;
	mock_clipboard_claim = 1;
	mock_primary_claim = 1;
	CHECK(save_history_entry_with_domains(original, user, global, mock_claim_selection));
	CHECK(history.count == 1 && history.entries[0] == original);
	CHECK_STR((char *)clipboard_buffer.data, fixed);
	CHECK_STR((char *)primary_buffer.data, fixed);
	CHECK(clipboard_buffer.data != primary_buffer.data);
	CHECK(clipboard_buffer.path != primary_buffer.path);
	CHECK(clipboard_buffer.uri != primary_buffer.uri);
	CHECK(buffer_is_saved_path(&clipboard_buffer) && buffer_is_saved_path(&primary_buffer));
	CHECK(payload_for_request(&clipboard_buffer, atom_uri_list,
				  &selection_data, &selection_size, &selection_type));
	CHECK(selection_type == atom_uri_list && selection_size == strlen(clipboard_buffer.uri));
	CHECK(payload_for_request(&primary_buffer, atom_uri_list,
				  &selection_data, &selection_size, &selection_type));
	CHECK(selection_type == atom_uri_list && selection_size == strlen(primary_buffer.uri));
	CHECK(primary_pending_deadline == 0);
	CHECK(stat(fixed, &st) == 0);
	CHECK(unlink(fixed) == 0);

	history.capacity = 2;
	buf.data = (unsigned char *)"other";
	buf.size = 5;
	history_add_from_buffer(&buf);
	other = history.entries[0];
	other->pinned = 1;
	original->pinned = 1;
	history.pinned_count = 2;
	CHECK(save_history_entry_with_domains(original, user, global, mock_claim_selection));
	CHECK(history.count == 2 && history.entries[0] == other && history.entries[1] == original);
	CHECK(history.pinned_count == 2 && original->pinned && other->pinned);
	CHECK_STR((char *)clipboard_buffer.data, fixed);
	CHECK_STR((char *)primary_buffer.data, fixed);
	CHECK(unlink(fixed) == 0);

	old_clipboard = clipboard_buffer.data;
	old_primary = primary_buffer.data;
	primary_pending_deadline = 42;
	mock_clipboard_claim = 0;
	mock_primary_claim = 0;
	incr.active = 1;
	incr_source = &clipboard_buffer;
	CHECK(!save_history_entry_with_domains(original, user, global, mock_claim_selection));
	CHECK(incr.active && incr_source == &clipboard_buffer);
	CHECK(clipboard_buffer.data == old_clipboard && primary_buffer.data == old_primary);
	CHECK(primary_pending_deadline == 42);
	CHECK(history.count == 2 && history.entries[0] == other && history.entries[1] == original);
	CHECK(unlink(fixed) == 0);
	incr.active = 0;
	incr_source = NULL;

	mock_clipboard_claim = 1;
	CHECK(!save_history_entry_with_domains(original, user, global, mock_claim_selection));
	CHECK_STR((char *)clipboard_buffer.data, fixed);
	CHECK(primary_buffer.data == old_primary && primary_pending_deadline == 42);
	CHECK(unlink(fixed) == 0);
	old_clipboard = clipboard_buffer.data;
	mock_clipboard_claim = 0;
	mock_primary_claim = 1;
	CHECK(!save_history_entry_with_domains(original, user, global, mock_claim_selection));
	CHECK(clipboard_buffer.data == old_clipboard);
	CHECK_STR((char *)primary_buffer.data, fixed);
	CHECK(primary_pending_deadline == 0);
	CHECK(history.count == 2 && history.entries[0] == other && history.entries[1] == original);
	CHECK(unlink(fixed) == 0);

	CHECK(setenv("XDG_DATA_HOME", invalid, 1) == 0);
	CHECK(!save_history_entry_with_domains(original, user, global, mock_claim_selection));
	CHECK(history.count == 2 && history.entries[1] == original);
	CHECK(stat(invalid_file, &st) == 0);
	CHECK(unlink(invalid_file) == 0);
	CHECK(rmdir(invalid_dir) == 0);
	CHECK(rmdir(invalid_wm) == 0);
	CHECK(rmdir(invalid) == 0);
	history_clear_all();
	clear_buffer(&clipboard_buffer);
	clear_buffer(&primary_buffer);
	primary_pending_ts = 0;
	primary_pending_deadline = 0;
	CHECK(unlink(user) == 0);
	CHECK(rmdir(dir) == 0);
	CHECK(rmdir(wm) == 0);
	CHECK(rmdir(xdg) == 0);
	CHECK(rmdir(root) == 0);
	restore_env("HOME", old_home);
	restore_env("XDG_DATA_HOME", old_data);
}

static void test_history_paths(void)
{
	char root[] = "/tmp/wmclipd-path-unit-XXXXXX";
	char state[512], state_wm[512], path[512], data[512], override[512];
	char local[512], local_state[512], fallback_dir[512], fallback[512];
	char *old_home, *old_state, *old_data, *resolved;
	unsigned char bytes[] = "history", actual[16];
	ClipBuffer buf = { 0 };
	struct stat st;
	int fd;

	if (!mkdtemp(root)) {
		CHECK(0);
		return;
	}
	old_home = getenv("HOME") ? strdup(getenv("HOME")) : NULL;
	old_state = getenv("XDG_STATE_HOME") ? strdup(getenv("XDG_STATE_HOME")) : NULL;
	old_data = getenv("XDG_DATA_HOME") ? strdup(getenv("XDG_DATA_HOME")) : NULL;
	snprintf(state, sizeof(state), "%s/state", root);
	snprintf(state_wm, sizeof(state_wm), "%s/state/WindowMaker", root);
	snprintf(path, sizeof(path), "%s/state/WindowMaker/wmclipd-history", root);
	snprintf(data, sizeof(data), "%s/data", root);
	snprintf(override, sizeof(override), "%s/override", root);
	snprintf(local, sizeof(local), "%s/.local", root);
	snprintf(local_state, sizeof(local_state), "%s/.local/state", root);
	snprintf(fallback_dir, sizeof(fallback_dir), "%s/.local/state/WindowMaker", root);
	snprintf(fallback, sizeof(fallback), "%s/.local/state/WindowMaker/wmclipd-history", root);
	CHECK(setenv("HOME", root, 1) == 0);
	CHECK(setenv("XDG_STATE_HOME", state, 1) == 0);
	CHECK(setenv("XDG_DATA_HOME", data, 1) == 0);
	history.capacity = 10;
	persist_enabled = 1;
	history_file_override = NULL;
	resolved = history_state_path();
	CHECK_STR(resolved, path);
	wfree(resolved);
	history_load();
	CHECK(history.count == 0 && !history_writes_suppressed);
	CHECK(access(state, F_OK) < 0 && errno == ENOENT);
	history_enabled = 0;
	history_load();
	history_save();
	CHECK(access(state, F_OK) < 0 && errno == ENOENT);
	history_enabled = 1;
	persist_enabled = 0;
	history_load();
	history_save();
	CHECK(access(state, F_OK) < 0 && errno == ENOENT);
	persist_enabled = 1;

	buf.kind = CONTENT_CAPTURED;
	buf.type = atom_png;
	buf.data = bytes;
	buf.size = sizeof(bytes);
	history_add_from_buffer(&buf);
	history_save();
	CHECK(stat(state_wm, &st) == 0 && (st.st_mode & 0777) == 0700);
	CHECK(stat(path, &st) == 0 && (st.st_mode & 0777) == 0600);
	CHECK(access(data, F_OK) < 0 && errno == ENOENT);
	history_clear_all();
	history.capacity = 10;
	history_load();
	CHECK(history.count == 1 && !history_writes_suppressed);
	history_clear_all();

	fd = open(path, O_WRONLY | O_TRUNC);
	CHECK(fd >= 0);
	if (fd >= 0) {
		CHECK(write(fd, "bad", 3) == 3);
		CHECK(close(fd) == 0);
	}
	history.capacity = 10;
	history_load();
	CHECK(history.count == 0 && history_writes_suppressed);
	history_save();
	fd = open(path, O_RDONLY);
	CHECK(fd >= 0);
	if (fd >= 0) {
		CHECK(read(fd, actual, sizeof(actual)) == 3);
		CHECK(memcmp(actual, "bad", 3) == 0);
		CHECK(close(fd) == 0);
	}

	history_writes_suppressed = 0;
	history_file_override = override;
	resolved = history_state_path();
	CHECK_STR(resolved, override);
	wfree(resolved);
	history.capacity = 10;
	history_add_from_buffer(&buf);
	history_save();
	history_clear_all();
	history.capacity = 10;
	history_load();
	CHECK(history.count == 1 && !history_writes_suppressed);
	CHECK(access(data, F_OK) < 0 && errno == ENOENT);
	history_clear_all();

	history_file_override = NULL;
	CHECK(unlink(path) == 0);
	CHECK(symlink(override, path) == 0);
	history.capacity = 10;
	history_load();
	CHECK(history.count == 0 && history_writes_suppressed);
	history_save();
	CHECK(lstat(path, &st) == 0 && S_ISLNK(st.st_mode));
	CHECK(unlink(path) == 0);
	history_writes_suppressed = 0;

	CHECK(unsetenv("XDG_STATE_HOME") == 0);
	resolved = history_state_path();
	CHECK_STR(resolved, fallback);
	wfree(resolved);
	CHECK(setenv("XDG_STATE_HOME", "relative", 1) == 0);
	resolved = history_state_path();
	CHECK_STR(resolved, fallback);
	wfree(resolved);
	history.capacity = 10;
	history_add_from_buffer(&buf);
	history_save();
	CHECK(stat(fallback, &st) == 0 && (st.st_mode & 0777) == 0600);
	history_clear_all();
	history.capacity = 10;
	history_load();
	CHECK(history.count == 1 && !history_writes_suppressed);
	CHECK(access(data, F_OK) < 0 && errno == ENOENT);

	history_clear_all();
	history_writes_suppressed = 0;
	persist_enabled = 0;
	CHECK(unlink(override) == 0);
	CHECK(unlink(fallback) == 0);
	CHECK(rmdir(fallback_dir) == 0);
	CHECK(rmdir(local_state) == 0);
	CHECK(rmdir(local) == 0);
	CHECK(rmdir(state_wm) == 0);
	CHECK(rmdir(state) == 0);
	CHECK(rmdir(root) == 0);
	restore_env("HOME", old_home);
	restore_env("XDG_STATE_HOME", old_state);
	restore_env("XDG_DATA_HOME", old_data);
}

static void test_popup_ipc(void)
{
	static const char unkeyed[] = "{first = NO;keyed = NO;payload = \"wmclipd-popup\";}";
	static const char keyed[] = "{first = NO;keyed = YES;payload = \"wmclipd-popup\";}";
	int sockets[2], rc;

	rc = socketpair(AF_UNIX, SOCK_STREAM, 0, sockets);
	CHECK(rc == 0);
	if (rc < 0)
		return;
	popup_ipc_fd = sockets[0];
	popup_active = 1;
	popup_tick_pending = 0;
	popup_tick_keyed = false;
	CHECK(wIPCSend(sockets[1], WIPC_EVENT_BIT | WIPC_EV_TICK,
		       unkeyed, strlen(unkeyed)) == 0);
	CHECK(wIPCSend(sockets[1], WIPC_EVENT_BIT | WIPC_EV_TICK,
		       keyed, strlen(keyed)) == 0);
	drain_popup_ipc();
	CHECK(popup_tick_pending && popup_tick_keyed);
	service_popup_tick();
	CHECK(popup_tick_pending);
	CHECK(close(sockets[1]) == 0);
	drain_popup_ipc();
	CHECK(popup_ipc_fd == -1);
	popup_active = 0;
	popup_tick_pending = 0;
	popup_tick_keyed = false;
}

static void test_base64(void)
{
	static const unsigned char latin[] = { 'A', 0xe9, 0, 'Z' };
	static const unsigned char compound[] = {
		0x1b, '$', '(', 'B', 0x24, 0x22, 0x1b, '(', 'B', 0, 'Z'
	};
	static const unsigned char compound_utf8[] = {
		0x1b, '%', 'G', 'a', 'b', 0, 'c', 'd', 0x1b, '%', '@'
	};
	static const unsigned char truncated[] = { 0x1b, '$', '(', 'B', 0x24 };
	static const unsigned char bad_escape[] = { 'a', 0x1b };
	static const unsigned char bad_designator[] = { 0x1b, '(', 0x1b };
	static const unsigned char invalid_utf8[] = { 0xc0, 0x80 };
	static const unsigned char utf8_data[] = { 'a', 'b', 0, 'c', 'd' };
	static const unsigned char invalid_compound_utf8[] = { 0x1b, '%', 'G', 0xed, 0xa0, 0x80 };
	static const unsigned char bytes[] = { 0, 0xff, 0xfe };
	static const unsigned char big5[] = {
		0x1b, '%', '/', '2', 0x80, 0x89, 'b', 'i', 'g', '5', '-', '0', 2, 0xa4, 0x40
	};
	static const unsigned char bad_extended[] = {
		0x1b, '%', '/', '2', 0x80, 0x09, 'b', 'i', 'g', '5', '-', '0', 2, 0xa4, 0x40
	};
	ClipHistoryEntry entry = { 0 };
	unsigned char *large, *decoded, *long_compound;
	char *uri;
	size_t len, decoded_len, prefix_len, i, segment_len;
	xcb_atom_t utf8_types[3];

	atom_utf8 = 101;
	atom_text = 102;
	atom_compound_text = 103;
	atom_text_plain = 104;
	atom_text_plain_utf8 = 105;
	atom_png = 106;
	atom_jpeg = 107;
	uri = base64_encode_uri("prefix:", bytes, 0, &len);
	CHECK_STR(uri, "prefix:");
	CHECK(len == 7);
	wfree(uri);
	uri = base64_encode_uri("prefix:", bytes, 1, &len);
	CHECK_STR(uri, "prefix:AA==");
	wfree(uri);
	uri = base64_encode_uri("prefix:", bytes, 2, &len);
	CHECK_STR(uri, "prefix:AP8=");
	wfree(uri);
	uri = base64_encode_uri("prefix:", bytes, 3, &len);
	CHECK_STR(uri, "prefix:AP/+");
	wfree(uri);
	CHECK(base64_encode_uri("", bytes, SIZE_MAX, &len) == NULL);
	entry.data = (unsigned char *)latin;
	entry.size = sizeof(latin);
	entry.type = XCB_ATOM_STRING;
	uri = entry_base64_uri(&entry, &len);
	CHECK_STR(uri, "data:text/plain;charset=utf-8;base64,QcOpAFo=");
	wfree(uri);
	utf8_types[0] = atom_utf8;
	utf8_types[1] = atom_text_plain;
	utf8_types[2] = atom_text_plain_utf8;
	entry.data = (unsigned char *)utf8_data;
	entry.size = sizeof(utf8_data);
	for (i = 0; i < 3; i++) {
		entry.type = utf8_types[i];
		uri = entry_base64_uri(&entry, &len);
		CHECK_STR(uri, "data:text/plain;charset=utf-8;base64,YWIAY2Q=");
		wfree(uri);
	}
	entry.data = (unsigned char *)compound;
	entry.size = sizeof(compound);
	entry.type = atom_compound_text;
	uri = entry_base64_uri(&entry, &len);
	CHECK_STR(uri, "data:text/plain;charset=utf-8;base64,44GCAFo=");
	wfree(uri);
	entry.data = (unsigned char *)compound_utf8;
	entry.size = sizeof(compound_utf8);
	entry.type = atom_text;
	uri = entry_base64_uri(&entry, &len);
	CHECK_STR(uri, "data:text/plain;charset=utf-8;base64,YWIAY2Q=");
	wfree(uri);
	CHECK(!compound_text_to_utf8(truncated, sizeof(truncated), &decoded, &decoded_len));
	CHECK(!compound_text_to_utf8(bad_escape, sizeof(bad_escape), &decoded, &decoded_len));
	CHECK(!compound_text_to_utf8(bad_designator, sizeof(bad_designator), &decoded, &decoded_len));
	CHECK(!compound_text_to_utf8(bad_extended, sizeof(bad_extended), &decoded, &decoded_len));
	CHECK(!compound_text_to_utf8(invalid_compound_utf8, sizeof(invalid_compound_utf8),
				     &decoded, &decoded_len));
	decoded = NULL;
	CHECK(compound_text_to_utf8(big5, sizeof(big5), &decoded, &decoded_len));
	if (decoded) {
		CHECK(decoded_len == 3 && memcmp(decoded, "\xe4\xb8\x80", 3) == 0);
		wfree(decoded);
	}
	long_compound = wmalloc(13 + 3000);
	memcpy(long_compound, big5, 13);
	segment_len = 7 + 3000;
	long_compound[4] = 0x80 | (segment_len >> 7);
	long_compound[5] = 0x80 | (segment_len & 0x7f);
	for (i = 0; i < 1500; i++) {
		long_compound[13 + i * 2] = 0xa4;
		long_compound[14 + i * 2] = 0x40;
	}
	decoded = NULL;
	CHECK(compound_text_to_utf8(long_compound, 13 + 3000, &decoded, &decoded_len));
	if (decoded) {
		CHECK(decoded_len == 4500);
		if (decoded_len == 4500) {
			for (i = 0; i < 1500; i++)
				CHECK(memcmp(decoded + i * 3, "\xe4\xb8\x80", 3) == 0);
		}
		wfree(decoded);
	}
	wfree(long_compound);
	entry.data = (unsigned char *)invalid_utf8;
	entry.size = sizeof(invalid_utf8);
	entry.type = atom_utf8;
	CHECK(entry_base64_uri(&entry, &len) == NULL);
	entry.data = (unsigned char *)bytes;
	entry.size = sizeof(bytes);
	entry.type = atom_png;
	uri = entry_base64_uri(&entry, &len);
	CHECK_STR(uri, "data:image/png;base64,AP/+");
	wfree(uri);
	entry.size = 0;
	uri = entry_base64_uri(&entry, &len);
	CHECK_STR(uri, "data:image/png;base64,");
	wfree(uri);
	entry.size = sizeof(bytes);
	entry.type = atom_jpeg;
	uri = entry_base64_uri(&entry, &len);
	CHECK_STR(uri, "data:image/jpeg;base64,AP/+");
	wfree(uri);
	entry.type = 999;
	CHECK(entry_base64_uri(&entry, &len) == NULL);
	large = wmalloc(CLIP_BASE64_MAX_INPUT);
	entry.data = large;
	entry.size = CLIP_BASE64_MAX_INPUT;
	entry.type = atom_png;
	uri = entry_base64_uri(&entry, &len);
	prefix_len = strlen("data:image/png;base64,");
	CHECK(uri != NULL && len == prefix_len + ((CLIP_BASE64_MAX_INPUT + 2) / 3) * 4);
	if (uri)
		CHECK_STR(uri + len - 4, "AA==");
	wfree(uri);
	entry.size++;
	CHECK(entry_base64_uri(&entry, &len) == NULL);
	memset(large, 0xe9, CLIP_BASE64_MAX_INPUT);
	entry.size = CLIP_BASE64_MAX_INPUT;
	entry.type = XCB_ATOM_STRING;
	uri = entry_base64_uri(&entry, &len);
	prefix_len = strlen("data:text/plain;charset=utf-8;base64,");
	CHECK(uri != NULL && len == prefix_len + ((CLIP_BASE64_MAX_INPUT * 2 + 2) / 3) * 4);
	if (uri)
		CHECK(memcmp(uri + prefix_len, "w6nDqcOp", 8) == 0);
	wfree(uri);
	entry.data = NULL;
	entry.size = 0;
	CHECK(entry_base64_uri(&entry, &len) == NULL);
	wfree(large);
}

static int mock_claim_selection(xcb_atom_t selection, ClipBuffer *buf, xcb_timestamp_t timestamp)
{
	int result;

	(void)timestamp;
	result = selection == atom_clipboard ? mock_clipboard_claim : mock_primary_claim;
	buf->owns = result;
	return result;
}

static void test_base64_publication(void)
{
	static unsigned char payload[] = "original";
	ClipHistoryEntry entry = { 0 };
	unsigned char *old_clipboard, *old_primary;
	int original_count;

	atom_clipboard = 200;
	atom_primary = 201;
	atom_utf8 = 101;
	entry.kind = CONTENT_CAPTURED;
	entry.type = atom_utf8;
	entry.data = payload;
	entry.size = sizeof(payload) - 1;
	clipboard_buffer.kind = CONTENT_CAPTURED;
	clipboard_buffer.data = (unsigned char *)wstrdup("old clipboard");
	clipboard_buffer.size = strlen((char *)clipboard_buffer.data);
	clipboard_buffer.owns = 1;
	primary_buffer.kind = CONTENT_CAPTURED;
	primary_buffer.data = (unsigned char *)wstrdup("old primary");
	primary_buffer.size = strlen((char *)primary_buffer.data);
	primary_buffer.owns = 1;
	old_clipboard = clipboard_buffer.data;
	old_primary = primary_buffer.data;
	primary_pending_deadline = 42;
	original_count = history.count;
	mock_clipboard_claim = 0;
	mock_primary_claim = 0;
	incr.active = 1;
	incr_source = &clipboard_buffer;
	CHECK(!publish_base64_entry_with_claim(&entry, mock_claim_selection));
	CHECK(incr.active && incr_source == &clipboard_buffer);
	CHECK(clipboard_buffer.data == old_clipboard && primary_buffer.data == old_primary);
	CHECK(primary_pending_deadline == 42 && history.count == original_count);
	incr.active = 0;
	incr_source = NULL;

	mock_clipboard_claim = 1;
	CHECK(!publish_base64_entry_with_claim(&entry, mock_claim_selection));
	CHECK_STR((char *)clipboard_buffer.data, "data:text/plain;charset=utf-8;base64,b3JpZ2luYWw=");
	CHECK(primary_buffer.data == old_primary && primary_pending_deadline == 42);
	old_clipboard = clipboard_buffer.data;

	mock_clipboard_claim = 0;
	mock_primary_claim = 1;
	CHECK(!publish_base64_entry_with_claim(&entry, mock_claim_selection));
	CHECK(clipboard_buffer.data == old_clipboard);
	CHECK_STR((char *)primary_buffer.data, "data:text/plain;charset=utf-8;base64,b3JpZ2luYWw=");
	CHECK(primary_pending_deadline == 0);

	mock_clipboard_claim = 1;
	CHECK(publish_base64_entry_with_claim(&entry, mock_claim_selection));
	CHECK(clipboard_buffer.owns && primary_buffer.owns);
	CHECK(clipboard_buffer.data != primary_buffer.data);
	CHECK(clipboard_buffer.size == primary_buffer.size);
	CHECK(memcmp(clipboard_buffer.data, primary_buffer.data, clipboard_buffer.size) == 0);
	CHECK(history.count == original_count);
	clear_buffer(&clipboard_buffer);
	clear_buffer(&primary_buffer);
}

int main(void)
{
	test_reorder();
	test_metadata();
	test_existing_duplicates();
	test_saved_path_targets();
	test_persistence();
	test_version_one();
	test_export_templates();
	test_export_files();
	test_history_paths();
	test_popup_ipc();
	test_base64();
	test_base64_publication();
	return check_report("check_wmclipd_history");
}
