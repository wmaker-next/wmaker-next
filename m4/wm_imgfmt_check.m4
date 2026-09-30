# wm_imgfmt_check.m4 - Macros to check for image file format support libraries
#
# Copyright (c) 2013 Christophe CURIS
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License along
# with this program; if not, see <https://www.gnu.org/licenses/>.


# WM_IMGFMT_CHECK_JPEG
# --------------------
#
# Check for JPEG file support through 'libjpeg'
# The check depends on variable 'enable_jpeg' being either:
#   yes  - detect, fail if not found
#   no   - do not detect, disable support
#   auto - detect, disable if not found
#
# When found, append appropriate stuff in GFXLIBS, and append info to
# the variable 'supported_gfx'
# When not found, append info to variable 'unsupported'
AC_DEFUN_ONCE([WM_IMGFMT_CHECK_JPEG],
[WM_LIB_CHECK([JPEG], [-ljpeg], [jpeg_destroy_compress], [],
    [AC_COMPILE_IFELSE(
                [AC_LANG_PROGRAM(
                    [@%:@include <stdlib.h>
@%:@include <stdio.h>
@%:@include <jpeglib.h>],
                    [  struct jpeg_decompress_struct cinfo;

  jpeg_destroy_decompress(&cinfo);])],
                [],
                [AS_ECHO([failed])
                 AS_ECHO(["$as_me: error: found $CACHEVAR but cannot compile header"])
                 AS_ECHO(["$as_me: error:   - does header 'jpeglib.h' exists? (is package 'jpeg-dev' missing?)"])
                 AS_ECHO(["$as_me: error:   - version of header is not supported? (report to dev team)"])
                 AC_MSG_ERROR([JPEG library is not usable, cannot continue])])
           ],
    [supported_gfx], [GFXLIBS])dnl
]) dnl AC_DEFUN


# WM_IMGFMT_CHECK_JXL
# -------------------
#
# Check for JXL (JPEG XL) file support through 'libjxl'
# The check depends on variable 'enable_jxl' being either:
#   yes  - detect, fail if not found
#   no   - do not detect, disable support
#   auto - detect, disable if not found
#
# When found, append appropriate stuff in GFXLIBS, and append info to
# the variable 'supported_gfx'
# When not found, append info to variable 'unsupported'
AC_DEFUN_ONCE([WM_IMGFMT_CHECK_JXL],
[WM_LIB_CHECK([JXL], [-ljxl], [JxlDecoderCreate], [],
    [AC_COMPILE_IFELSE(
                [AC_LANG_PROGRAM(
                    [@%:@include <stdlib.h>
@%:@include <jxl/decode.h>],
                    [  JxlDecoder* dec = JxlDecoderCreate(NULL);
  JxlDecoderDestroy(dec);])],
                [],
                [AS_ECHO([failed])
                 AS_ECHO(["$as_me: error: found $CACHEVAR but cannot compile header"])
                 AS_ECHO(["$as_me: error:   - does header 'jxl/decode.h' exists? (is package 'libjxl-dev' missing?)"])
                 AS_ECHO(["$as_me: error:   - version of header is not supported? (report to dev team)"])
                 AC_MSG_ERROR([JXL library is not usable, cannot continue])])
           ],
    [supported_gfx], [GFXLIBS])dnl
]) dnl AC_DEFUN


# WM_IMGFMT_CHECK_PNG
# -------------------
#
# Check for PNG file support through 'libpng'
# The check depends on variable 'enable_png' being either:
#   yes  - detect, fail if not found
#   no   - do not detect, disable support
#   auto - detect, disable if not found
#
# When found, append appropriate stuff in GFXLIBS, and append info to
# the variable 'supported_gfx'
# When not found, append info to variable 'unsupported'
AC_DEFUN_ONCE([WM_IMGFMT_CHECK_PNG],
[WM_LIB_CHECK([PNG], ["-lpng" "-lpng -lz" "-lpng -lz -lm"], [png_get_valid], [],
    [wm_save_CFLAGS="$CFLAGS"
     AS_IF([wm_fn_lib_try_compile "png.h" "" "return 0" ""],
         [],
         [AC_MSG_ERROR([found $CACHEVAR but could not find appropriate header - are you missing libpng-dev package?])])
     AS_IF([wm_fn_lib_try_compile "png.h" "" "png_get_valid(NULL, NULL, PNG_INFO_tRNS)" ""],
         [],
         [AC_MSG_ERROR([found $CACHEVAR and header, but cannot compile - unsupported version?])])
     CFLAGS="$wm_save_CFLAGS"],
    [supported_gfx], [GFXLIBS])dnl
]) dnl AC_DEFUN


# WM_IMGFMT_CHECK_TIFF
# --------------------
#
# Check for TIFF file support through 'libtiff'
# The check depends on variable 'enable_tiff' being either:
#   yes  - detect, fail if not found
#   no   - do not detect, disable support
#   auto - detect, disable if not found
#
# When found, append appropriate stuff in GFXLIBS, and append info to
# the variable 'supported_gfx'
# When not found, append info to variable 'unsupported'
AC_DEFUN_ONCE([WM_IMGFMT_CHECK_TIFF],
[WM_LIB_CHECK([TIFF],
    ["-ltiff"  \
     dnl TIFF can have a dependancy over zlib
     "-ltiff -lz" "-ltiff -lz -lm"  \
     dnl It may also have a dependancy to jpeg_lib
     "-ltiff -ljpeg" "-ltiff -ljpeg -lz" "-ltiff -ljpeg -lz -lm"  \
     dnl There is also a possible dependancy on JBIGKit
     "-ltiff -ljpeg -ljbig -lz"  \
     dnl Probably for historical reasons?
     "-ltiff34" "-ltiff34 -ljpeg" "-ltiff34 -ljpeg -lm"],
    [TIFFGetVersion], [],
    [wm_save_CFLAGS="$CFLAGS"
     AS_IF([wm_fn_lib_try_compile "tiffio.h" "" "return 0" ""],
         [],
         [AC_MSG_ERROR([found $CACHEVAR but could not find appropriate header - are you missing libtiff-dev package?])])
     AS_IF([wm_fn_lib_try_compile "tiffio.h" 'const char *filename = "dummy";' 'TIFFOpen(filename, "r")' ""],
         [],
         [AC_MSG_ERROR([found $CACHEVAR and header, but cannot compile - unsupported version?])])
     CFLAGS="$wm_save_CFLAGS"],
    [supported_gfx], [GFXLIBS])dnl
]) dnl AC_DEFUN


# WM_IMGFMT_CHECK_WEBP
# --------------------
#
# Check for WEBP file support through 'libwebp'
# The check depends on variable 'enable_webp' being either:
#   yes  - detect, fail if not found
#   no   - do not detect, disable support
#   auto - detect, disable if not found
#
# When found, append appropriate stuff in GFXLIBS, and append info to
# the variable 'supported_gfx'
# When not found, append info to variable 'unsupported'
AC_DEFUN_ONCE([WM_IMGFMT_CHECK_WEBP],
[AS_IF([test "x$enable_webp" = "xno"],
    [unsupported="$unsupported WebP"],
    [AC_CACHE_CHECK([for WebP support library], [wm_cv_imgfmt_webp],
        [wm_cv_imgfmt_webp=no
         dnl
         dnl The library is using a special trick on the functions to provide
         dnl compatibility between versions, so we cannot try linking against
         dnl a symbol without first using the header to handle it
         wm_save_LIBS="$LIBS"
         LIBS="$LIBS -lwebp"
         AC_LINK_IFELSE(
             [AC_LANG_PROGRAM([@%:@include <webp/decode.h>],
                 [WebPGetFeatures(NULL, 1024, NULL);])],
             [wm_cv_imgfmt_webp="-lwebp"])
         LIBS="$wm_save_LIBS"
         AS_IF([test "x$enable_webp$wm_cv_imgfmt_webp" = "xyesno"],
              [AC_MSG_ERROR([explicit WebP support requested but no library found])])dnl
        ])
     AS_IF([test "x$wm_cv_imgfmt_webp" = "xno"],
         [unsupported="$unsupported WebP"
          enable_webp="no"],
         [supported_gfx="$supported_gfx WebP"
          WM_APPEND_ONCE([$wm_cv_imgfmt_webp], [GFXLIBS])dnl
          AC_DEFINE([USE_WEBP], [1],
              [defined when valid Webp library with header was found])])dnl
    ])
AM_CONDITIONAL([USE_WEBP], [test "x$enable_webp" != "xno"])dnl
])


# WM_IMGFMT_CHECK_SVG
# -------------------
#
# Check for SVG support through librsvg (>= 2.46 for
# rsvg_handle_render_document). The check depends on variable
# 'enable_svg' being either yes/no/auto. When found, appends to
# GFXLIBS and supported_gfx, else to unsupported.
AC_DEFUN_ONCE([WM_IMGFMT_CHECK_SVG],
[AS_IF([test "x$enable_svg" = "xno"],
    [unsupported="$unsupported SVG"],
    [PKG_CHECK_MODULES([RSVG], [librsvg-2.0 >= 2.46],
        [wm_cv_imgfmt_svg=yes], [wm_cv_imgfmt_svg=no])
     AS_IF([test "x$enable_svg$wm_cv_imgfmt_svg" = "xyesno"],
        [AC_MSG_ERROR([explicit SVG support requested but librsvg >= 2.46 not found -- $RSVG_PKG_ERRORS])])
     AS_IF([test "x$wm_cv_imgfmt_svg" = "xno"],
        [unsupported="$unsupported SVG"
         enable_svg="no"],
        [supported_gfx="$supported_gfx SVG"
         WM_APPEND_ONCE([$RSVG_LIBS], [GFXLIBS])
         AC_DEFINE([USE_SVG], [1],
             [defined when librsvg is available for the SVG loader])])
    ])
AM_CONDITIONAL([USE_SVG], [test "x$enable_svg" != "xno"])dnl
AC_SUBST([RSVG_CFLAGS])dnl
])


# WM_IMGFMT_CHECK_HEIF
# --------------------
#
# Check for HEIF/HEIC/AVIF file support through 'libheif'
# The check depends on variable 'enable_heif' being either:
#   yes  - detect, fail if not found
#   no   - do not detect, disable support
#   auto - detect, disable if not found
#
# When found, append appropriate stuff in GFXLIBS, and append info to
# the variable 'supported_gfx'
# When not found, append info to variable 'unsupported'
AC_DEFUN_ONCE([WM_IMGFMT_CHECK_HEIF],
[AS_IF([test "x$enable_heif" = "xno"],
    [unsupported="$unsupported HEIF"],
    [PKG_CHECK_MODULES([HEIF], [libheif >= 1.15],
        [wm_cv_imgfmt_heif=yes], [wm_cv_imgfmt_heif=no])
     AS_IF([test "x$enable_heif$wm_cv_imgfmt_heif" = "xyesno"],
        [AC_MSG_ERROR([explicit HEIF support requested but libheif >= 1.15 not found -- $HEIF_PKG_ERRORS])])
     AS_IF([test "x$wm_cv_imgfmt_heif" = "xno"],
        [unsupported="$unsupported HEIF"
         enable_heif="no"],
        [supported_gfx="$supported_gfx HEIF"
         WM_APPEND_ONCE([$HEIF_LIBS], [GFXLIBS])dnl
         AC_DEFINE([USE_HEIF], [1],
             [defined when libheif is available for the HEIF loader])])dnl
    ])
AM_CONDITIONAL([USE_HEIF], [test "x$enable_heif" != "xno"])dnl
AC_SUBST([HEIF_CFLAGS])dnl
AC_SUBST([HEIF_LIBS])dnl
])


# WM_IMGFMT_CHECK_MAGICK
# ----------------------
#
# Check for MagickWand library to support more image file formats
# The check depends on variable 'enable_magick' being either:
#   yes  - detect, fail if not found
#   no   - do not detect, disable support
#   auto - detect, disable if not found
#
# When found, store the appropriate compilation flags in MAGICKFLAGS
# and MAGICKLIBS, and append info to the variable 'supported_gfx'
# When not found, append info to variable 'unsupported'
AC_DEFUN_ONCE([WM_IMGFMT_CHECK_MAGICK],
[AC_REQUIRE([_WM_LIB_CHECK_FUNCTS])
AS_IF([test "x$enable_magick" = "xno"],
    [unsupported="$unsupported Magick"],
    [AC_CACHE_CHECK([for Magick support library], [wm_cv_libchk_magick],
        [wm_cv_libchk_magick=no
         dnl First try to get the configuration from either pkg-config (the official way)
         dnl or with the fallback MagickWand-config
         AS_IF([test "x$PKG_CONFIG" = "x"],
             [AC_PATH_PROGS_FEATURE_CHECK([magickwand], [MagickWand-config],
                 [wm_cv_libchk_magick_cflags=`$ac_path_magickwand --cflags`
                  wm_cv_libchk_magick_libs=`$ac_path_magickwand --ldflags`
                  wm_cv_libchk_magick=magickwand])],
             [AS_IF([$PKG_CONFIG --exists MagickWand],
                 [wm_cv_libchk_magick_cflags=`$PKG_CONFIG --cflags MagickWand`
                  wm_cv_libchk_magick_libs=`$PKG_CONFIG --libs MagickWand`
                  wm_cv_libchk_magick=pkgconfig])])
         AS_IF([test "x$wm_cv_libchk_magick" = "xno"],
             [AS_IF([test "x$enable_magick" != "xauto"],
                 [AC_MSG_RESULT([not found])
                  AC_MSG_ERROR([explicit Magick support requested but configuration not found with pkg-config and MagickWand-config - are you missing libmagickwand-dev package?])])],
             [dnl The configuration was found, check that it actually works
              wm_save_LIBS="$LIBS"
              dnl
              dnl We check that the library is available
              AS_IF([wm_fn_lib_try_link "NewMagickWand" "$wm_cv_libchk_magick_libs"],
                  [wm_cv_libchk_magick=maybe])
              LIBS="$wm_save_LIBS"
              AS_IF([test "x$wm_cv_libchk_magick" != "xmaybe"],
                  [AC_MSG_ERROR([MagickWand was found but the library does not link])])
              dnl
              dnl The library was found, check if header is available and compiles
              wm_save_CFLAGS="$CFLAGS"
              AS_IF([wm_fn_lib_try_compile "MagickWand/MagickWand.h" "MagickWand *wand;" "wand = NewMagickWand()" "$wm_cv_libchk_magick_cflags"],
                  [wm_cv_libchk_magick="$wm_cv_libchk_magick_cflags % $wm_cv_libchk_magick_libs"
		   wm_cv_libchk_magick_version=7],
		  [wm_fn_lib_try_compile "wand/magick_wand.h" "MagickWand *wand;" "wand = NewMagickWand()" "$wm_cv_libchk_magick_cflags"],
                  [wm_cv_libchk_magick="$wm_cv_libchk_magick_cflags % $wm_cv_libchk_magick_libs"
		   wm_cv_libchk_magick_version=6],
		  [AC_MSG_ERROR([found MagickWand library but could not compile its header])])
              CFLAGS="$wm_save_CFLAGS"])dnl
         ])
     AS_IF([test "x$wm_cv_libchk_magick" = "xno"],
         [unsupported="$unsupported Magick"
          enable_magick="no"],
         [supported_gfx="$supported_gfx Magick"
          MAGICKFLAGS=`echo "$wm_cv_libchk_magick" | sed -e 's, *%.*$,,' `
           MAGICKLIBS=`echo "$wm_cv_libchk_magick" | sed -e 's,^.*% *,,' `
          AC_DEFINE_UNQUOTED([USE_MAGICK], [$wm_cv_libchk_magick_version],
              [defined when MagickWand library with header was found])])
     ])
AM_CONDITIONAL([USE_MAGICK], [test "x$enable_magick" != "xno"])dnl
AC_SUBST(MAGICKFLAGS)dnl
AC_SUBST(MAGICKLIBS)dnl
]) dnl AC_DEFUN
