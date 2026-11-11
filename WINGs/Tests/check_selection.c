/*
 *  WindowMaker headless unit tests for the selection format-32 repack
 *  core (W_XSelectionFormat32Repack) extracted from selection.c's
 *  writeSelection: a format-32 WMData is a long[] on LP64 and the wire
 *  wants one uint32 per item.  A producer that hands the property
 *  writer a uint32-sized array (the TARGETS bug class) halves the item
 *  count on LP64, which these tests pin.
 *
 *  Never opens a Display or a connection.
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
 */

#include "wconfig.h"
#include <stdbool.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <xcb/xproto.h>

#include <WINGs/WUtil.h>
#include <WINGs/WINGsP.h>

static int failures;

static void check(const char *what, bool ok)
{
	printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
	if (!ok)
		failures++;
}

/* the TARGETS shape every format-32 producer must build: long-sized
 * items so writeSelection's sizeof(long) sizing sees the full list */
static void test_long_sized_targets(void)
{
	unsigned long atoms[4] = { 71, 83, 130, 1 };
	uint32_t *wire;
	size_t n;

	n = W_XSelectionFormat32Repack((const unsigned char *)atoms,
				       sizeof(atoms), &wire);
	check("four long-sized items survive the repack", n == 4);
	check("item 0 narrowed exactly", n == 4 && wire && wire[0] == 71);
	check("item 1 narrowed exactly", n == 4 && wire && wire[1] == 83);
	check("item 2 narrowed exactly", n == 4 && wire && wire[2] == 130);
	check("item 3 narrowed exactly", n == 4 && wire && wire[3] == 1);
	wfree(wire);
}

/* what the TARGETS bug class produces: a uint32-sized array half the
 * expected byte length, the repack sees two items and the wire loses
 * STRING and COMPOUND_TEXT — the test pins that this must fail loudly
 * at the item count, never silently pass with shifted atoms */
static void test_underlong_producer_detected(void)
{
	uint32_t atoms[4] = { 71, 83, 130, 1 };
	uint32_t *wire;
	size_t n;

	n = W_XSelectionFormat32Repack((const unsigned char *)atoms,
				       sizeof(atoms), &wire);
	check("a uint32-sized producer halves the item count (4 bytes each -> 2 items)", n == 2);
	check("each surviving item is two merged atoms (the shifted-list symptom)",
	      n == 2 && wire && wire[0] == 71UL && wire[1] == 130UL);
	wfree(wire);
}

/* high atoms: the long carries the full 32 bits, values above the
 * signed range must not sign-extend through the narrowing cast */
static void test_high_atom_values(void)
{
	unsigned long atoms[2] = { 0xFFFFFF80UL, 0x7FFFFFFFUL };
	uint32_t *wire;
	size_t n;

	n = W_XSelectionFormat32Repack((const unsigned char *)atoms,
				       sizeof(atoms), &wire);
	check("two high-value items survive", n == 2);
	check("0xFFFFFF80 survives the narrowing", n == 2 && wire &&
	      wire[0] == 0xFFFFFF80U);
	check("0x7FFFFFFF survives the narrowing", n == 2 && wire &&
	      wire[1] == 0x7FFFFFFFU);
	wfree(wire);
}

/* a trailing partial long is truncated, matching the byte-length-aware
 * sizing contract (4 bytes cannot make an item) */
static void test_partial_trailing_long(void)
{
	unsigned long atoms[2] = { 5, 6 };
	uint32_t *wire;
	size_t n;

	n = W_XSelectionFormat32Repack((const unsigned char *)atoms,
				       sizeof(atoms) - 4, &wire);
	check("four leftover bytes make no item", n == 1);
	check("the whole item survives", n == 1 && wire && wire[0] == 5);
	wfree(wire);
}

/* nothing to send: zero items, *out stays NULL */
static void test_empty_payload(void)
{
	uint32_t *wire = (uint32_t *) 0x1;

	check("a four-byte payload makes no item",
	      W_XSelectionFormat32Repack((const unsigned char *)"abcd", 4, &wire) == 0);
	check("*out is NULL when there is nothing to send", wire == NULL);
	wire = (uint32_t *) 0x1;
	check("a zero-byte payload makes no item",
	      W_XSelectionFormat32Repack((const unsigned char *)"", 0, &wire) == 0);
	check("*out is NULL for the empty payload", wire == NULL);
}

/* the WMData round trip: the way producers actually build the payload
 * (WMCreateDataWithBytes over a long array + WMSetDataFormat(32)),
 * the byte length must be a whole number of longs */
static void test_wmdata_contract(void)
{
	WMData *data;
	unsigned long atoms[2] = { XCB_ATOM_STRING, XCB_ATOM_ATOM };
	uint32_t *wire;
	size_t n;

	data = WMCreateDataWithBytes(atoms, sizeof(atoms));
	WMSetDataFormat(data, 32);
	check("the long-sized WMData byte length is a whole number of longs",
	      WMGetDataLength(data) % sizeof(long) == 0);
	n = W_XSelectionFormat32Repack(WMDataBytes(data), WMGetDataLength(data), &wire);
	check("the WMData-shaped payload repacks to two items", n == 2);
	check("the atoms ride through in order", n == 2 && wire &&
	      wire[0] == XCB_ATOM_STRING && wire[1] == XCB_ATOM_ATOM);
	wfree(wire);
	WMReleaseData(data);
}

int main(void)
{
	test_long_sized_targets();
	test_underlong_producer_detected();
	test_high_atom_values();
	test_partial_trailing_long();
	test_empty_payload();
	test_wmdata_contract();

	if (failures) {
		printf("%d FAILURES\n", failures);
		return 1;
	}
	printf("check_selection: all tests passed\n");
	return 0;
}
