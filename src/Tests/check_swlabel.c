/*
 * Tests for switch panel label formatting.
 *
 * Copyright (c) 2026 Window Maker Team
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "wconfig.h"

#include <string.h>

#include <WINGs/WUtil.h>

#include "swlabel.h"
#include "check_common.h"

static void expect(const char *format, const WSwitchLabelFields *fields, const char *expected)
{
	char *result = wSwitchPanelFormatLabel(format, fields);

	CHECK_STR(result, expected);
	if (result)
		wfree(result);
}

static void test_fields(void)
{
	WSwitchLabelFields fields = {
		"Document", "org.gnome.Nautilus", "files", "Desktop", "Shade", "S", 2
	};

	expect("literal %% %q %", &fields, "literal % %q %");
	expect("%T|%t|%I|%i|%C|%W|%w|%S|%s", &fields,
	       "Document|Document|org.gnome.Nautilus|Nautilus|files|Desktop|Desktop|Shade|S");
	expect("%10T|%-10T|%3T", &fields, "  Document|Document  |Document");
	expect("%-3q|%1234T|%-|%12", &fields, "%-3q|%1234T|%-|%12");
	fields.title = fields.wm_class;
	expect("%t", &fields, "");
	fields.workspace_count = 1;
	expect("%w|%W", &fields, "|Desktop");
	fields.title = "caf\xc3\xa9";
	expect("%6T|%-6T|%3T", &fields, "  caf\xc3\xa9|caf\xc3\xa9  |caf\xc3\xa9");
	fields.wm_class = "org.kde.konsole";
	expect("%i", &fields, "konsole");
	fields.wm_class = "XTerm";
	expect("%i", &fields, "XTerm");
	fields.wm_class = "org.example";
	expect("%i", &fields, "org.example");
	fields.wm_class = NULL;
	expect("%t", &fields, "caf\xc3\xa9");
	fields.title = NULL;
	fields.wm_instance = NULL;
	fields.workspace = NULL;
	fields.state_long = NULL;
	fields.state_short = NULL;
	expect("%T%t%I%i%C%W%w%S%s", &fields, "");
	expect("%T%t%I%i%C%W%w%S%s", NULL, "");
	expect("", &fields, "");
	CHECK(wSwitchPanelFormatLabel(NULL, &fields) == NULL);
}

static void test_limit(void)
{
	WSwitchLabelFields fields = { 0 };
	char text[4102];
	char format[4102];
	char *result;

	memset(text, 'a', 4095);
	memcpy(text + 4095, "\xc3\xa9" "xyz", 6);
	fields.title = text;
	result = wSwitchPanelFormatLabel("%T!", &fields);
	CHECK(result != NULL);
	if (result) {
		CHECK(strlen(result) == 4095);
		CHECK(result[4094] == 'a');
		wfree(result);
	}
	memset(format, 'b', 4094);
	memcpy(format + 4094, "\xc3\xa9" "x", 4);
	result = wSwitchPanelFormatLabel(format, &fields);
	CHECK(result != NULL);
	if (result) {
		CHECK(strlen(result) == 4096);
		CHECK(memcmp(result + 4094, "\xc3\xa9", 2) == 0);
		wfree(result);
	}
	memset(text, 'z', 4100);
	text[4100] = '\0';
	expect("%.4T", &fields, "%.4T");
	result = wSwitchPanelFormatLabel("%T", &fields);
	CHECK(result != NULL);
	if (result) {
		CHECK(strlen(result) == 4096);
		wfree(result);
	}
}

int main(void)
{
	test_fields();
	test_limit();
	return check_report("switch panel label formatter");
}
