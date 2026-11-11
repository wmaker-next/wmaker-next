/*
 *  swp_label.c
 *
 *  Window Maker window manager
 *
 *  Copyright (c) 2026 Window Maker Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#include "wconfig.h"

#include <string.h>

#include <WINGs/WUtil.h>

#include "swp_label.h"

#define LABEL_MAX_BYTES 4096

typedef struct {
	char *text;
	size_t length;
	size_t capacity;
	int full;
	int failed;
} LabelBuffer;

static size_t characterBytes(const char *text)
{
	unsigned char c = (unsigned char)text[0];
	size_t length, i;

	if (c >= 0xc2 && c <= 0xdf)
		length = 2;
	else if (c >= 0xe0 && c <= 0xef)
		length = 3;
	else if (c >= 0xf0 && c <= 0xf4)
		length = 4;
	else
		return 1;

	for (i = 1; i < length; i++) {
		if (!text[i] || ((unsigned char)text[i] & 0xc0) != 0x80)
			return 1;
	}
	return length;
}

static void appendBytes(LabelBuffer *buffer, const char *text, size_t length)
{
	size_t bytes, capacity;
	char *new_text;

	while (length && !buffer->full) {
		bytes = characterBytes(text);
		if (bytes > length)
			bytes = 1;
		if (bytes > LABEL_MAX_BYTES - buffer->length) {
			buffer->full = 1;
			break;
		}
		if (buffer->length + bytes + 1 > buffer->capacity) {
			capacity = buffer->capacity * 2;
			if (capacity > LABEL_MAX_BYTES + 1)
				capacity = LABEL_MAX_BYTES + 1;
			new_text = wrealloc(buffer->text, capacity);
			if (!new_text) {
				buffer->full = 1;
				buffer->failed = 1;
				break;
			}
			buffer->text = new_text;
			buffer->capacity = capacity;
		}
		memcpy(buffer->text + buffer->length, text, bytes);
		buffer->length += bytes;
		text += bytes;
		length -= bytes;
	}
	buffer->text[buffer->length] = '\0';
}

static void appendPadding(LabelBuffer *buffer, unsigned int count)
{
	while (count-- && !buffer->full)
		appendBytes(buffer, " ", 1);
}

static const char *fieldValue(char code, const WSwitchLabelFields *fields)
{
	const char *value = NULL;
	const char *dot;

	if (!fields)
		return NULL;

	switch (code) {
	case 'T':
		value = fields->title;
		break;
	case 't':
		if (fields->title && (!fields->wm_class || strcmp(fields->title, fields->wm_class)))
			value = fields->title;
		break;
	case 'I':
		value = fields->wm_class;
		break;
	case 'i':
		value = fields->wm_class;
		if (value && strncmp(value, "org.", 4) == 0) {
			dot = strchr(value + 4, '.');
			if (dot)
				value = dot + 1;
		}
		break;
	case 'C':
		value = fields->wm_instance;
		break;
	case 'W':
		value = fields->workspace;
		break;
	case 'w':
		if (fields->workspace_count > 1)
			value = fields->workspace;
		break;
	case 'S':
		value = fields->state_long;
		break;
	case 's':
		value = fields->state_short;
		break;
	}
	return value;
}

char *wSwitchPanelFormatLabel(const char *format, const WSwitchLabelFields *fields)
{
	LabelBuffer buffer;
	const char *start, *value, *p;
	unsigned int width, digits, columns, padding;
	int left;
	size_t bytes;

	if (!format)
		return NULL;

	buffer.text = wmalloc(128);
	if (!buffer.text)
		return NULL;
	buffer.length = 0;
	buffer.capacity = 128;
	buffer.full = 0;
	buffer.failed = 0;

	while (*format && !buffer.full) {
		if (*format != '%') {
			bytes = characterBytes(format);
			appendBytes(&buffer, format, bytes);
			format += bytes;
			continue;
		}
		start = format++;
		if (*format == '%') {
			appendBytes(&buffer, format++, 1);
			continue;
		}
		left = *format == '-';
		if (left)
			format++;
		width = 0;
		digits = 0;
		while (digits < 3 && *format >= '0' && *format <= '9') {
			width = width * 10 + (unsigned int)(*format++ - '0');
			digits++;
		}
		if (!*format || !strchr("TtIiCWwSs", *format)) {
			if (*format)
				format += characterBytes(format);
			appendBytes(&buffer, start, (size_t)(format - start));
			continue;
		}
		value = fieldValue(*format++, fields);
		if (!value)
			value = "";
		columns = 0;
		for (p = value; *p && columns < width; p += characterBytes(p))
			columns++;
		padding = width > columns ? width - columns : 0;
		if (!left)
			appendPadding(&buffer, padding);
		for (p = value; *p && !buffer.full; p += bytes) {
			bytes = characterBytes(p);
			appendBytes(&buffer, p, bytes);
		}
		if (left)
			appendPadding(&buffer, padding);
	}
	if (buffer.failed) {
		wfree(buffer.text);
		return NULL;
	}
	return buffer.text;
}
