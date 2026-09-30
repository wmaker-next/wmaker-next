# Reference RPM spec for the wmaker-crm `next` branch.
#
# Build from a `make dist` tarball:
#   ./autogen.sh && ./configure && make dist
#   cp WindowMaker-*.tar.gz ~/rpmbuild/SOURCES/
#   cp distros/WindowMaker-*.desktop distros/WindowMaker-WMRootMenu-fedora ~/rpmbuild/SOURCES/
#   rpmbuild -ba distros/wmaker-crm-next.spec

%global wm_version 0.96.0
%global snapdate   20260608

Name:           wmaker-crm-next
Version:        %{wm_version}
# Leading 0 keeps `next` snapshots ordered below the eventual tagged release.
Release:        0.next.%{snapdate}.1%{?dist}
Summary:        Bleeding-edge development branch of the Window Maker window manager
License:        GPL-2.0-or-later
URL:            https://repo.or.cz/wmaker-crm.git

Conflicts:      WindowMaker
Provides:       WindowMaker = %{version}-%{release}

Source0:        WindowMaker-%{wm_version}.tar.gz
Source2:        WindowMaker-application.desktop
Source3:        WindowMaker-WMRootMenu-fedora

BuildRequires:  gcc
BuildRequires:  make
BuildRequires:  gettext
# X11 client libraries
BuildRequires:  pkgconfig(x11)
BuildRequires:  pkgconfig(x11-xcb)
BuildRequires:  pkgconfig(xcb)
BuildRequires:  pkgconfig(xcb-icccm)
BuildRequires:  pkgconfig(xext)
BuildRequires:  pkgconfig(xft)
BuildRequires:  pkgconfig(xinerama)
BuildRequires:  pkgconfig(xpm)
BuildRequires:  pkgconfig(xrender)
BuildRequires:  pkgconfig(xrandr)
BuildRequires:  pkgconfig(xfixes)
BuildRequires:  pkgconfig(ice)
BuildRequires:  pkgconfig(sm)
# event loop, text layout, startup-notification
BuildRequires:  libev-devel
BuildRequires:  pkgconfig(fontconfig)
BuildRequires:  pkgconfig(pangoxft)
BuildRequires:  pkgconfig(libstartup-notification-1.0)
# image loaders
BuildRequires:  pkgconfig(libpng)
BuildRequires:  libjpeg-turbo-devel
BuildRequires:  giflib-devel
BuildRequires:  pkgconfig(libtiff-4)
BuildRequires:  pkgconfig(libwebp)
BuildRequires:  pkgconfig(libheif)
BuildRequires:  pkgconfig(MagickWand)
# wmiv archive support
BuildRequires:  pkgconfig(libarchive)

Requires:       WINGs-next-libs%{?_isa} = %{version}-%{release}
Requires:       desktop-backgrounds-compat

%description
Window Maker is an X11 window manager designed to give additional
integration support to the GNUstep Desktop Environment.  In every way
possible, it reproduces the elegant look and feel of the NEXTSTEP GUI.
It is fast, feature rich, easy to configure, and easy to use.

This package tracks the wmaker-crm `next` development branch.

%package -n WINGs-next-libs
Summary:        Widget, utility and image libraries used by Window Maker
Conflicts:      WINGs-libs
Provides:       WINGs-libs = %{version}-%{release}

%description -n WINGs-next-libs
libWINGs (widget toolkit), libWUtil (containers, proplist, IPC, event
loop) and libwraster (image loading and compositing) shared libraries.

%package -n WINGs-next-devel
Summary:        Development files for the WINGs, WUtil and wraster libraries
Requires:       WINGs-next-libs%{?_isa} = %{version}-%{release}
Conflicts:      WINGs-devel
Provides:       WINGs-devel = %{version}-%{release}

%description -n WINGs-next-devel
Headers and pkg-config files for libWINGs, libWUtil and libwraster.

%prep
%autosetup -n WindowMaker-%{wm_version}

%build
# Build every shipped translation.
LINGUAS="$(cd po && ls *.po | sed 's/\.po$//' | tr '\n' ' ')"
export LINGUAS

%configure \
    --disable-static \
    --enable-modelock \
    --enable-randr \
    --enable-usermenu \
    --enable-wmreplace \
    --enable-pango \
    --with-libarchive \
    --x-includes=%{_includedir} \
    --x-libraries=%{_libdir} \
    NLSDIR=%{_datadir}/locale

# Drop the rpath libtool would otherwise embed in the installed binaries
# and shared libraries, no chrpath post-processing needed.
sed -i -e 's|^hardcode_libdir_flag_spec=.*|hardcode_libdir_flag_spec=""|' \
       -e 's|^runpath_var=LD_RUN_PATH|runpath_var=DIE_RPATH_DIE|' libtool

%make_build

%install
%make_install NLSDIR=%{_datadir}/locale

%find_lang WindowMaker
%find_lang WPrefs
%find_lang WINGs
%find_lang WRaster
%find_lang wmgenmenu
cat WPrefs.lang WINGs.lang WRaster.lang wmgenmenu.lang >> WindowMaker.lang

install -D -m0644 -p %{SOURCE2} \
    %{buildroot}%{_datadir}/applications/WindowMaker.desktop

# Fedora-specific defaults: ship a curated root menu and point the
# default workspace background at the distro wallpaper.
install -D -m0644 -p %{SOURCE3} \
    %{buildroot}%{_sysconfdir}/WindowMaker/WMRootMenu
sed -i -e 's:WorkspaceBack = (solid:WorkspaceBack = (mpixmap, "/usr/share/backgrounds/default.png":' \
    %{buildroot}%{_sysconfdir}/WindowMaker/WindowMaker

find %{buildroot} -type f -name '*.la' -delete
chmod 755 %{buildroot}%{_datadir}/WindowMaker/autostart.sh \
          %{buildroot}%{_datadir}/WindowMaker/exitscript.sh

%ldconfig_scriptlets
%ldconfig_scriptlets -n WINGs-next-libs

%files -f WindowMaker.lang
%license COPYING COPYING.WTFPL
%doc AUTHORS ChangeLog NEWS FAQ* README* doc/wmaker-ipc.txt
%dir %{_sysconfdir}/WindowMaker
%config(noreplace) %{_sysconfdir}/WindowMaker/*
%{_bindir}/wmaker
%{_bindir}/wmaker.inst
%{_bindir}/WPrefs
%{_bindir}/convertfonts
%{_bindir}/geticonset
%{_bindir}/getstyle
%{_bindir}/seticons
%{_bindir}/setstyle
%{_bindir}/wdread
%{_bindir}/wdwrite
%{_bindir}/wkdemenu.pl
%{_bindir}/wm-oldmenu2new
%{_bindir}/wmagnify
%{_bindir}/wmakermsg
%{_bindir}/wmclipd
%{_bindir}/wmgenmenu
%{_bindir}/wmiv
%{_bindir}/wmmenugen
%{_bindir}/wmsetbg
%{_bindir}/wxcopy
%{_bindir}/wxpaste
%{_datadir}/WindowMaker/
%{_datadir}/WPrefs/
%{_datadir}/xsessions/wmaker.desktop
%{_datadir}/applications/WindowMaker.desktop
%{_datadir}/applications/WPrefs.desktop
%{_mandir}/man1/*.1*
%lang(cs) %{_mandir}/cs/man1/*.1*
%lang(ru) %{_mandir}/ru/man1/*.1*
%lang(sk) %{_mandir}/sk/man1/*.1*

%files -n WINGs-next-libs
%license COPYING COPYING.WTFPL
%doc WINGs/README WINGs/NEWS WINGs/ChangeLog
%{_libdir}/libWINGs.so.*
%{_libdir}/libWUtil.so.*
%{_libdir}/libwraster.so.*
%{_libdir}/libWMaker.so.*
%{_datadir}/WINGs/

%files -n WINGs-next-devel
%{_libdir}/libWINGs.so
%{_libdir}/libWUtil.so
%{_libdir}/libwraster.so
%{_libdir}/libWMaker.so
%{_libdir}/pkgconfig/WINGs.pc
%{_libdir}/pkgconfig/WUtil.pc
%{_libdir}/pkgconfig/wrlib.pc
%{_libdir}/pkgconfig/wmlib.pc
%{_includedir}/WINGs/
%{_includedir}/wraster.h
%{_includedir}/WMaker.h

%changelog
* Mon Jun 08 2026 David Maciejak <dmaciejak@fortinet.com> - 0.96.0-0.next.20260608.1
- Rebase on the `make dist` tarball, drop autoreconf/iconv/sed prep hacks
- Sync BuildRequires with configure.ac: libev, xcb/xcb-icccm/x11-xcb,
  pango, fontconfig, XFixes, startup-notification, libarchive, libwebp,
  giflib (was libungif)
- Add wmclipd, drop get-*-flags scripts and upgrade-windowmaker-defaults
- Replace chrpath post-processing with the libtool rpath sed
- Glob data/icon/theme lists and library sonames, modern RPM macros

* Wed May 27 2015 Alexey I. Froloff <raorn@raorn.name> - 2015.05.27.gb295a20-1
- Initial package for the "next" branch of wmaker-crm repo
