#!/bin/sh
# Count remaining readers of the runtime globals the WDisplay refactor moves.
#
# Usage: script/wdisplay-census.sh [topsrcdir]

top=${1:-.}
cd "$top" || exit 1

count() {
	grep -E --include='*.[ch]' -r "$1" src /dev/null 2>/dev/null \
		| grep -v -E '/Tests/' | wc -l
}

countall() {
	grep -E --include='*.[ch]' -r "$1" src /dev/null 2>/dev/null | wc -l
}

echo "== connection =="
printf 'xconn                       %5d\n' "$(countall '\bxconn\b')"
printf 'dflt_screen                 %5d\n' "$(countall '\bdflt_screen\b')"
printf 'wdisp                       %5d\n' "$(countall '\bwdisp\b')"

echo
echo "== w_global field groups =="
printf 'screen_count                %5d\n' "$(count '\bw_global\.screen_count\b')"
printf 'atom.*                      %5d\n' "$(count '\bw_global\.atom\.')"
printf 'context.*                   %5d\n' "$(count '\bw_global\.context\.')"
printf 'xext.*                      %5d\n' "$(count '\bw_global\.xext\.')"
printf 'timestamp.*                 %5d\n' "$(count '\bw_global\.timestamp\.')"
printf 'ignore_crossing.*           %5d\n' "$(count '\bw_global\.ignore_crossing\.')"

echo
echo "== wPreferences runtime half =="
printf 'supports_tiff               %5d\n' "$(count '\bwPreferences\.supports_tiff\b')"
printf 'cursor[*]                   %5d\n' "$(count '\bwPreferences\.cursor\b')"
printf 'modifier_mask               %5d\n' "$(count '\bwPreferences\.modifier_mask\b')"
printf 'tab_drag_modifier_mask      %5d\n' "$(count '\bwPreferences\.tab_drag_modifier_mask\b')"
printf 'modifier_labels             %5d\n' "$(count '\bwPreferences\.modifier_labels\b')"
printf 'modifier_short_labels       %5d\n' "$(count '\bwPreferences\.modifier_short_labels\b')"
printf 'swtile/swtileArrow          %5d\n' "$(count '\bwPreferences\.swtile')"
printf 'swbackImage                 %5d\n' "$(count '\bwPreferences\.swbackImage\b')"
printf 'wsmbackTexture              %5d\n' "$(count '\bwPreferences\.wsmbackTexture\b')"
printf 'hot_corner_actions          %5d\n' "$(count '\bwPreferences\.hot_corner_actions\b')"
printf 'window_place_origin         %5d\n' "$(count '\bwPreferences\.window_place_origin\b')"
printf 'flags.*                     %5d\n' "$(count '\bwPreferences\.flags\.')"
