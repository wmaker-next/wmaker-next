/*
 *  check_accent_picker.c
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

/*
 * check_accent_picker - unit tests for the pure accent picker data layer.
 * No display needed.
 */

#include "wconfig.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <WINGs/WUtil.h>

#include "accent_data.h"

#include "check_common.h"

static const APSet *setByName(const char *name)
{
	return apSetFind(name);
}

static bool hasVariant(char **variants, size_t count, const char *value)
{
	size_t i;

	for (i = 0; i < count; i++)
		if (strcmp(variants[i], value) == 0)
			return true;
	return false;
}

static size_t resolve(const char *const *tokens, size_t token_count,
		      const char *const *layouts, size_t layout_count,
		      int active, const APSet **out, bool *unknown)
{
	return apResolveSets(tokens, token_count, layouts, layout_count,
			     active, out, 64, unknown);
}

static void test_set_names(void)
{
	size_t i, j, count = apSetCount();

	CHECK(count >= 47);
	for (i = 0; i < count; i++) {
		CHECK(apSetAt(i) != NULL);
		CHECK(apSetAt(i)->name != NULL);
		CHECK(apSetAt(i)->mappings != NULL);
		for (j = i + 1; j < count; j++)
			CHECK(strcmp(apSetAt(i)->name, apSetAt(j)->name) != 0);
	}
	CHECK(setByName("French") != NULL);
	CHECK(setByName("french") != NULL);
	CHECK(setByName("Currency") != NULL);
	CHECK(setByName("IPA") != NULL);
	CHECK(setByName("NoSuchSet") == NULL);
}

static void test_french(void)
{
	const APSet *set = setByName("French");
	char **variants;
	size_t count = 0;

	CHECK(set != NULL);
	variants = apMergeVariants(&set, 1, NULL, 0, "e", false, &count);
	CHECK(variants != NULL);
	CHECK(count == 5);
	if (count == 5) {
		CHECK_STR(variants[0], "é");
		CHECK_STR(variants[1], "è");
		CHECK_STR(variants[2], "ê");
		CHECK_STR(variants[3], "ë");
		CHECK_STR(variants[4], "€");
	}
	apFreeVariants(variants, count);
}

static void test_currency(void)
{
	const APSet *set = setByName("Currency");
	char **variants;
	size_t count = 0;

	variants = apMergeVariants(&set, 1, NULL, 0, "e", false, &count);
	CHECK(count == 1 && variants && strcmp(variants[0], "€") == 0);
	apFreeVariants(variants, count);

	variants = apMergeVariants(&set, 1, NULL, 0, "s", false, &count);
	CHECK(count == 1 && variants && strcmp(variants[0], "$") == 0);
	apFreeVariants(variants, count);
}

static void test_auto(void)
{
	static const char *const layouts[] = { "us", "fr" };
	static const char *const tokens[] = { "auto", "Currency" };
	const APSet *sets[64];
	bool unknown = false;
	size_t count;

	count = resolve(tokens, 2, layouts, 2, 0, sets, &unknown);
	CHECK(!unknown);
	CHECK(count == 2);
	if (count == 2) {
		CHECK_STR(sets[0]->name, "French");
		CHECK_STR(sets[1]->name, "Currency");
	}

	memset(sets, 0, sizeof(sets));
	count = resolve(tokens, 2, layouts, 2, 1, sets, &unknown);
	CHECK(count == 2);
	if (count == 2)
		CHECK_STR(sets[0]->name, "French");

	/* auto never selects IPA */
	memset(sets, 0, sizeof(sets));
	count = resolve(tokens, 2, layouts, 2, 0, sets, &unknown);
	{
		size_t i;

		for (i = 0; i < count; i++)
			CHECK(strcmp(sets[i]->name, "IPA") != 0);
	}

	/* no mapped layout contributes nothing */
	{
		static const char *const us[] = { "us" };
		static const char *const auto_only[] = { "auto" };
		const APSet *plain[64];

		count = resolve(auto_only, 1, us, 1, 0, plain, &unknown);
		CHECK(count == 0);

		count = resolve(auto_only, 1, NULL, 0, 0, plain, &unknown);
		CHECK(count == 0);

		{
			static const char *const zz[] = { "zz" };

			count = resolve(auto_only, 1, zz, 1, 0, plain, &unknown);
			CHECK(count == 0);
		}
	}
}

static void test_all_and_explicit(void)
{
	static const char *const all[] = { "all" };
	static const char *const explicit[] = { "French", "IPA" };
	static const char *const dup[] = { "French", "french" };
	static const char *const unknown_token[] = { "NoSuchSet" };
	const APSet *sets[64];
	bool unknown = false;
	size_t count, i;
	bool saw_ipa = false;

	count = resolve(all, 1, NULL, 0, 0, sets, &unknown);
	CHECK(count == apSetCount());
	CHECK(!unknown);
	for (i = 0; i < count; i++)
		if (strcmp(sets[i]->name, "IPA") == 0)
			saw_ipa = true;
	CHECK(saw_ipa);

	memset(sets, 0, sizeof(sets));
	count = resolve(explicit, 2, NULL, 0, 0, sets, &unknown);
	CHECK(count == 2);
	if (count == 2) {
		CHECK_STR(sets[0]->name, "French");
		CHECK_STR(sets[1]->name, "IPA");
	}

	memset(sets, 0, sizeof(sets));
	count = resolve(dup, 2, NULL, 0, 0, sets, &unknown);
	CHECK(count == 1);

	memset(sets, 0, sizeof(sets));
	count = resolve(unknown_token, 1, NULL, 0, 0, sets, &unknown);
	CHECK(count == 0);
	CHECK(unknown);
}

static void test_layout_names(void)
{
	static const char *const layouts[] = { "English (US)", "French" };
	static const char *const auto_only[] = { "auto" };
	const APSet *sets[64];
	bool unknown = false;
	size_t count;

	count = resolve(auto_only, 1, layouts, 2, 0, sets, &unknown);
	CHECK(count == 1);
	if (count == 1)
		CHECK_STR(sets[0]->name, "French");

	{
		static const char *const swiss[] = { "German (Switzerland)" };

		memset(sets, 0, sizeof(sets));
		count = resolve(auto_only, 1, swiss, 1, 0, sets, &unknown);
		CHECK(count == 3);
		if (count == 3) {
			CHECK_STR(sets[0]->name, "German");
			CHECK_STR(sets[1]->name, "French");
			CHECK_STR(sets[2]->name, "Italian");
		}
	}

	{
		static const char *const english[] = { "English (US)" };

		memset(sets, 0, sizeof(sets));
		count = resolve(auto_only, 1, english, 1, 0, sets, &unknown);
		CHECK(count == 0);
	}
}

static void test_active_order(void)
{
	static const char *const layouts[] = { "fr", "de" };
	static const char *const auto_only[] = { "auto" };
	const APSet *sets[64];
	bool unknown = false;
	size_t count;

	count = resolve(auto_only, 1, layouts, 2, 1, sets, &unknown);
	CHECK(count == 2);
	if (count == 2) {
		CHECK_STR(sets[0]->name, "German");
		CHECK_STR(sets[1]->name, "French");
	}
}

static void test_merge_dedup(void)
{
	const APSet *sets[2];
	char **variants;
	size_t count = 0;

	sets[0] = setByName("French");
	sets[1] = setByName("Currency");
	variants = apMergeVariants(sets, 2, NULL, 0, "e", false, &count);
	CHECK(count == 5);
	CHECK(hasVariant(variants, count, "€"));
	apFreeVariants(variants, count);
}

static void test_uppercase(void)
{
	const APSet *set = setByName("French");
	char **variants;
	char *upper;
	bool is_letter = false;
	size_t count = 0;

	upper = apUppercaseCopy("ß");
	CHECK_STR(upper, "ẞ");
	if (upper)
		wfree(upper);

	upper = apNormalizeBaseCopy("E", &is_letter);
	CHECK(is_letter);
	CHECK_STR(upper, "e");
	if (upper)
		wfree(upper);

	upper = apNormalizeBaseCopy("$", &is_letter);
	CHECK(!is_letter);
	CHECK_STR(upper, "$");
	if (upper)
		wfree(upper);

	variants = apMergeVariants(&set, 1, NULL, 0, "e", true, &count);
	CHECK(count == 5);
	if (count == 5) {
		CHECK_STR(variants[0], "É");
		CHECK(hasVariant(variants, count, "È"));
	}
	apFreeVariants(variants, count);
}

static void test_user_override(void)
{
	const APSet *set = setByName("Currency");
	static const char *const user_variants[] = { "é", "è", "€" };
	APMapping user = { "e", user_variants, 3 };
	char **variants;
	size_t count = 0;

	variants = apMergeVariants(&set, 1, &user, 1, "e", false, &count);
	CHECK(count == 3);
	if (count == 3) {
		CHECK_STR(variants[0], "é");
		CHECK_STR(variants[1], "è");
		CHECK_STR(variants[2], "€");
	}
	apFreeVariants(variants, count);
}

static void test_limits(void)
{
	char too_long[AP_MAX_VARIANT_BYTES + 2];
	char many_buf[80][8];
	const char *many[80];
	static const char *const bad_values[] = { "a", "\xff", "b" };
	APMapping many_map = { "e", many, 80 };
	APMapping bad_map = { "e", bad_values, 3 };
	char **variants;
	size_t count = 0;
	const char *bad_utf8 = "\xff";
	char *copy;
	int i;

	CHECK(apValidUtf8("é"));
	CHECK(!apValidUtf8(bad_utf8));
	CHECK(apUtf8Codepoints("é") == 1);
	CHECK(apUtf8Codepoints("\xff") == 0);

	memset(too_long, 'a', sizeof(too_long) - 1);
	too_long[sizeof(too_long) - 1] = '\0';
	copy = apUppercaseCopy(too_long);
	CHECK(copy != NULL);
	if (copy) {
		CHECK(strlen(copy) == strlen(too_long));
		wfree(copy);
	}

	CHECK(apMergeVariants(NULL, 0, NULL, 0, NULL, false, NULL) == NULL);

	for (i = 0; i < 80; i++) {
		snprintf(many_buf[i], sizeof(many_buf[i]), "v%d", i);
		many[i] = many_buf[i];
	}
	variants = apMergeVariants(NULL, 0, &many_map, 1, "e", false, &count);
	CHECK(count == AP_MAX_VARIANTS);
	apFreeVariants(variants, count);

	variants = apMergeVariants(NULL, 0, &bad_map, 1, "e", false, &count);
	CHECK(count == 2);
	if (count == 2) {
		CHECK_STR(variants[0], "a");
		CHECK_STR(variants[1], "b");
	}
	apFreeVariants(variants, count);
}

int main(void)
{
	test_set_names();
	test_french();
	test_currency();
	test_auto();
	test_all_and_explicit();
	test_layout_names();
	test_active_order();
	test_merge_dedup();
	test_uppercase();
	test_user_override();
	test_limits();

	return check_report("check_accent_picker");
}
