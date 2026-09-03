#!/bin/sh
# Census of remaining Xlib usage: raw Xlib calls per directory, Xlib includes,
# XSetErrorHandler sites, Xlib types in public headers, and libX11 in the
# DT_NEEDED of built binaries. Progress metric for the xcb conversion.
#
# Usage: script/xlib-census.sh [topsrcdir]

top=${1:-.}
cd "$top" || exit 1

dirsrch() {
	# $1 = regexp, remaining args = directories
	pat=$1
	shift
	grep -E --include='*.[ch]' --include='*.c' -r "$pat" "$@" /dev/null 2>/dev/null \
		| grep -v -E '(^|/)(Tests|Examples|Extras|check_)' \
		| wc -l
}

dirsrchall() {
	pat=$1
	shift
	grep -E --include='*.[ch]' -r "$pat" "$@" /dev/null 2>/dev/null | wc -l
}

count() {
	grep -E --include='*.[ch]' -r "$1" src WINGs WPrefs.app util wrlib /dev/null 2>/dev/null | wc -l
}

countlib() {
	grep -E --include='*.[ch]' -r "$1" src WINGs WPrefs.app util wrlib /dev/null 2>/dev/null \
		| grep -v -E '(^|/)(Tests|Examples|Extras)' | wc -l
}

echo "== raw Xlib calls per directory (X<Name> function-prefix, excl. Tests/Examples) =="
for d in src WINGs WPrefs.app util wrlib; do
	n=$(dirsrch '\bX[A-Z][a-zA-Z]*\(' "$d")
	printf '%-12s %5d\n' "$d" "$n"
done
printf '%-12s %5d\n' TOTAL $(dirsrch '\bX[A-Z][a-zA-Z]*\(' src WINGs WPrefs.app util wrlib)

echo
echo "== XSetErrorHandler / XSynchronize sites =="
printf 'XSetErrorHandler  %4d\n' $(count '\bXSetErrorHandler\b')
printf 'XSynchronize      %4d\n' $(count '\bXSynchronize\b')

echo
echo "== Xlib includes (whole tree, incl. Tests/Examples) =="
for h in Xlib.h Xutil.h Xatom.h keysym.h Xlib-xcb.h Xproto.h XKBlib.h cursorfont.h Xlocale.h Xresource.h Xlibint.h panoramiXproto.h Xmd.h X.h; do
	printf '<X11/%-18s %4d\n' "$h>" $(dirsrchall "include <X11/$h>" src WINGs WPrefs.app util wrlib)
done
printf '<X11/total            %4d\n' $(dirsrchall 'include <X11/' src WINGs WPrefs.app util wrlib)

echo
echo "== Xlib type spellings (whole tree incl. Tests/Examples) =="
printf 'Display\\*      %4d\n' $(dirsrchall '\bDisplay *\*' src WINGs WPrefs.app util wrlib)
printf 'XEvent         %4d\n' $(dirsrchall '\bXEvent\b' src WINGs WPrefs.app util wrlib)
printf 'XContext       %4d\n' $(dirsrchall '\bXContext\b' src WINGs WPrefs.app util wrlib)

echo
echo "== libX11 in DT_NEEDED of built binaries =="
found=0
for b in src/wmaker WPrefs.app/WPrefs util/wxcopy util/wxpaste util/wmsetbg \
	util/setstyle util/wmagnify util/wmclipd util/wmglyph util/wmlock util/wmiv \
	WINGs/.libs/libWINGs.so WUtil/.libs/libWUtil.so wrlib/.libs/libwraster.so; do
	[ -f "$b" ] || [ -f "${b%.so}.so.0" ] || continue
	b=$(\ls "$b" ${b%.so}.so* 2>/dev/null | head -1)
	[ -n "$b" ] || continue
	x11=$(ldd "$b" 2>/dev/null | grep -E 'libX11|libXext' | tr -s ' ' | paste -sd';' -)
	[ -n "$x11" ] && { printf '%-28s %s\n' "$b" "$x11"; found=1; }
done
[ "$found" = 0 ] && echo "(none)"
