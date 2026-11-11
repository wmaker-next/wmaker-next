/*
 *  wipcreply - parse the success field of a wmaker IPC command reply
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
 *  wmaker answers command verbs with a property-list description such
 *  as {success = YES;} or {success = NO;error = "not running";}.
 *  wmlib only needs the success flag, so this reads just that instead
 *  of dragging the full proplist parser into libWMaker.
 *
 *  The accepted grammar is the subset of WMGetPropListDescription()
 *  output a reply can contain: a top-level dictionary whose keys and
 *  values are quoted strings, bare atoms or nested {...}/(...) groups.
 *  Whitespace is tolerated, the structure is checked strictly. Input
 *  is already bounded by WIPC_MAX_PAYLOAD at the transport and holds
 *  no interior NUL, so the scan is linear.
 */

#include <string.h>

#include "wipcreply.h"

/* nesting cap for skipped groups, far above any real reply */
#define REPLY_MAX_DEPTH 16

struct scan {
	const char *p, *end;
};

static void skip_ws(struct scan *s)
{
	while (s->p < s->end &&
	       (*s->p == ' ' || *s->p == '\t' || *s->p == '\n' || *s->p == '\r'))
		s->p++;
}

/* the writer only emits these bare, everything else gets quoted */
static int is_atom(int c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
	       (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '$';
}

/* the cursor sits on the opening quote, skip to its unescaped twin */
static int skip_quoted(struct scan *s)
{
	s->p++;
	while (s->p < s->end) {
		if (*s->p == '\\') {
			s->p++;
			if (s->p >= s->end)
				return 0;
			s->p++;
			continue;
		}
		if (*s->p == '"') {
			s->p++;
			return 1;
		}
		s->p++;
	}
	return 0;
}

static int skip_atom(struct scan *s)
{
	const char *start = s->p;

	while (s->p < s->end && is_atom((unsigned char)*s->p))
		s->p++;
	return s->p > start;
}

/* skip one value: quoted string, bare atom or balanced {...}/(...) group */
static int skip_value(struct scan *s, int depth)
{
	char open, close;

	skip_ws(s);
	if (s->p >= s->end)
		return 0;

	if (*s->p == '"')
		return skip_quoted(s);

	if (is_atom((unsigned char)*s->p))
		return skip_atom(s);

	if (*s->p != '{' && *s->p != '(')
		return 0;

	if (depth >= REPLY_MAX_DEPTH)
		return 0;

	open = *s->p;
	close = (open == '{') ? '}' : ')';
	s->p++;

	while (s->p < s->end) {
		if (*s->p == '"') {
			if (!skip_quoted(s))
				return 0;
			continue;
		}
		if (*s->p == open) {
			if (!skip_value(s, depth + 1))
				return 0;
			continue;
		}
		if (*s->p == close) {
			s->p++;
			return 1;
		}
		if ((*s->p == '{' || *s->p == '(') &&
		    !skip_value(s, depth + 1))
			return 0;
		s->p++;
	}
	return 0;
}

/* read a key or the success value as either a bare atom or a quoted
 * string; text and tlen describe the unescaped content, plain is 0
 * when escapes were seen (so the content cannot be a plain
 * YES/NO/success) */
static int read_token(struct scan *s, const char **text, size_t *tlen,
		      int *plain)
{
	const char *start;

	skip_ws(s);
	if (s->p >= s->end)
		return 0;

	if (*s->p == '"') {
		s->p++;
		start = s->p;
		*plain = 1;
		while (s->p < s->end && *s->p != '"') {
			if (*s->p == '\\') {
				*plain = 0;
				s->p++;
				if (s->p >= s->end)
					return 0;
				s->p++;
				continue;
			}
			s->p++;
		}
		if (s->p >= s->end)
			return 0;
		*text = start;
		*tlen = (size_t)(s->p - start);
		s->p++;		/* closing quote */
		return 1;
	}

	if (!is_atom((unsigned char)*s->p))
		return 0;

	start = s->p;
	*plain = 1;
	while (s->p < s->end && is_atom((unsigned char)*s->p))
		s->p++;
	*text = start;
	*tlen = (size_t)(s->p - start);
	return 1;
}

static int is_word(const char *text, size_t len, const char *word)
{
	size_t wlen = strlen(word);

	return len == wlen && memcmp(text, word, wlen) == 0;
}

int wipcreply_success(const char *desc)
{
	struct scan s;
	int found, yes;

	if (desc == NULL)
		return -1;

	s.p = desc;
	s.end = desc + strlen(desc);

	skip_ws(&s);
	if (s.p >= s.end || *s.p != '{')
		return -1;
	s.p++;

	found = 0;
	yes = 0;

	for (;;) {
		const char *text;
		size_t tlen;
		int plain, match;

		skip_ws(&s);
		if (s.p >= s.end)
			return -1;	/* unterminated dictionary */
		if (*s.p == '}') {
			s.p++;
			break;
		}

		if (!read_token(&s, &text, &tlen, &plain))
			return -1;
		match = plain && is_word(text, tlen, "success");
		if (match)
			found = 1;

		skip_ws(&s);
		if (s.p >= s.end || *s.p != '=')
			return -1;
		s.p++;

		if (match) {
			/* only YES accepts, later duplicates override */
			if (!read_token(&s, &text, &tlen, &plain))
				return -1;
			yes = plain && is_word(text, tlen, "YES");
		} else if (!skip_value(&s, 0)) {
			return -1;
		}

		skip_ws(&s);
		if (s.p >= s.end || *s.p != ';')
			return -1;
		s.p++;
	}

	/* trailing garbage after the closing brace is a malformed reply */
	skip_ws(&s);
	if (s.p != s.end)
		return -1;

	if (!found)
		return 0;
	return yes ? 1 : 0;
}
