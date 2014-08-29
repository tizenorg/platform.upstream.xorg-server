%bcond_with mesa
%bcond_with x

Name:           xorg-server
Version:        1.16
Release:        1
License:        MIT
Summary:        X Server
Url:            http://www.x.org
Group:          Graphics/X Window System
Source0:        %{name}-%{version}.tar.bz2
Source1001: 	xorg-server.manifest
BuildRequires:  libgcrypt-devel
BuildRequires:  pkgconfig(gestureproto)
BuildRequires:  pkgconfig(xf86dgaproto)
BuildRequires:  pkgconfig(bigreqsproto)
BuildRequires:  pkgconfig(compositeproto)
BuildRequires:  pkgconfig(damageproto)
BuildRequires:  pkgconfig(dri2proto)
BuildRequires:  pkgconfig(fixesproto)
BuildRequires:  pkgconfig(fontsproto)
BuildRequires:  pkgconfig(fontutil)
BuildRequires:  pkgconfig(inputproto)
BuildRequires:  pkgconfig(kbproto)
BuildRequires:  pkgconfig(libdrm)
BuildRequires:  pkgconfig(libudev)
BuildRequires:  pkgconfig(openssl)
BuildRequires:  pkgconfig(pciaccess)
BuildRequires:  pkgconfig(pixman-1)
BuildRequires:  pkgconfig(randrproto)
BuildRequires:  pkgconfig(recordproto)
BuildRequires:  pkgconfig(renderproto)
BuildRequires:  pkgconfig(resourceproto)
BuildRequires:  pkgconfig(scrnsaverproto)
BuildRequires:  pkgconfig(videoproto)
BuildRequires:  pkgconfig(videoproto)
BuildRequires:  pkgconfig(xcmiscproto)
BuildRequires:  pkgconfig(xdmcp)
BuildRequires:  pkgconfig(xextproto)
BuildRequires:  pkgconfig(xf86vidmodeproto)
BuildRequires:  pkgconfig(xfont)
BuildRequires:  pkgconfig(xineramaproto)
BuildRequires:  pkgconfig(xkbfile)
BuildRequires:  pkgconfig(xorg-macros)
BuildRequires:  pkgconfig(xproto)
BuildRequires:  pkgconfig(xtrans)
BuildRequires:  pkgconfig(xv)
BuildRequires:  pkgconfig(presentproto)
BuildRequires:  pkgconfig(dri3proto)

%ifarch %ix86 x86_64
BuildRequires:  pkgconfig(glproto)
BuildRequires:  pkgconfig(xf86driproto)
%endif

%if %{with mesa}
BuildRequires:  pkgconfig(dri)
BuildRequires:  pkgconfig(gl)
%else
# BuildRequires: pkgconfig(gles20)
%endif

Provides:	xorg-x11-server
Obsoletes:	xorg-x11-server < 1.13.0
Provides:	xorg-x11-server-common
Obsoletes:	xorg-x11-server-common < 1.13.0
Provides:	xorg-x11-server-Xorg
Obsoletes:	xorg-x11-server-Xorg < 1.13.0
Conflicts:	xorg-server-setuid

%if !%{with x}
ExclusiveArch:
%endif


%description
X.org X Server

%package devel
Summary:        SDK for X server driver module development
Group:          Development/Libraries
Requires:       %{name} = %{version}
Requires:       libpciaccess-devel
Requires:       pixman-devel

%description devel
The SDK package provides the developmental files which are necessary for
developing X server driver modules, and for compiling driver modules
outside of the standard X11 source code tree.  Developers writing video
drivers, input drivers, or other X modules should install this package.

%package setuid
Summary:        Setuid X server
Group:          Graphics/X Window System
Provides:       xorg-x11-server
Obsoletes:      xorg-x11-server < 1.13.0
Provides:       xorg-x11-server-common
Obsoletes:      xorg-x11-server-common < 1.13.0
Provides:       xorg-x11-server-Xorg
Obsoletes:      xorg-x11-server-Xorg < 1.13.0
Conflicts:      xorg-server

%description setuid
The setuid version of X server.

%prep
%setup -q
cp %{SOURCE1001} .


%build
NOCONFIGURE=1 ./autogen.sh
%reconfigure \
%if %{with mesa}
%ifarch %ix86 x86_64
          --enable-dri \
          --enable-dri2 \
	--enable-xaa \
%else
	--disable-debug \
	  --disable-dri \
	--disable-glx \
	  --enable-dri2 \
	--disable-aiglx \
	--disable-glx-tls \
	--disable-vgahw \
	--disable-vbe \
%endif
%else
	--disable-dri \
	--disable-glx \
	--enable-dri2 \
	--disable-aiglx \
	--disable-glx-tls \
	--disable-vgahw \
	--disable-vbe \
%endif
	--enable-dga \
	--disable-strict-compilation \
	--disable-static \
	--disable-unit-tests \
	--disable-sparkle \
	--disable-install-libxf86config \
	--enable-registry \
	--enable-gesture \
	--enable-composite \
	--enable-xres \
	--enable-record \
	--enable-xv \
	--enable-xvmc \
	--disable-screensaver \
	--enable-xdmcp \
	--enable-xdm-auth-1 \
	--enable-xinerama \
	--enable-xf86vidmode \
	--enable-xace \
	--disable-xselinux \
	--disable-xcsecurity \
	--disable-tslib \
	--disable-dbe \
	--disable-xf86bigfont \
	--enable-dpms \
	--enable-config-udev \
	--disable-config-hal \
	--enable-xfree86-utils \
	--disable-windowswm \
	--enable-libdrm \
	--enable-xorg \
	--disable-dmx \
	--disable-xvfb \
	--disable-xnest \
	--disable-xquartz \
	--disable-xwin \
	--disable-kdrive \
	--disable-xephyr \
	--disable-xfake \
	--disable-xwayland \
	--disable-xfbdev \
	--disable-kdrive-kbd \
	--disable-kdrive-mouse \
	--disable-kdrive-evdev \
	--without-dtrace \
	--with-os-vendor="Tizen" \
	--with-xkb-path="/usr/share/X11/xkb" \
	--with-xkb-output="/var/lib/xkb/compiled" \
	--with-default-font-path="built-ins" \
	--disable-install-setuid \
	--with-sha1=libgcrypt \
	CFLAGS="${CFLAGS} \
                -Wall -g \
                -D_F_UDEV_DEBUG_ \
                -D_F_NO_GRABTIME_UPDATE_ \
                -D_F_NO_CATCH_SIGNAL_ \
                -D_F_CHECK_NULL_CLIENT_ \
                -D_F_COMP_OVL_PATCH \
                -D_F_PUT_ON_PIXMAP_ \
                -D_F_IGNORE_MOVE_SPRITE_FOR_FLOATING_POINTER_ \
                -D_F_NOT_ALWAYS_CREATE_FRONTBUFFER_ \
                -D_F_GESTURE_EXTENSION_ \
                -D_F_DISABLE_SCALE_TO_DESKTOP_FOR_DIRECT_TOUCH_ \
                -D_F_DPMS_PHONE_CTRL_ \
                -D_F_DRI2_FIX_INVALIDATE \
                -D_F_DRI2_SWAP_REGION_ \
                -D_F_NOT_USE_SW_CURSOR_ \
                -D_F_EXCLUDE_NON_MASK_SELECTED_FD_FROM_MAXCLIENTS_ \
 		" \
	CPPFLAGS="${CPPFLAGS} "

make %{?_smp_mflags}

%install
%make_install

rm %{buildroot}/%{_datadir}/X11/xorg.conf.d/10-evdev.conf
rm %{buildroot}/%{_datadir}/X11/xorg.conf.d/10-quirks.conf
rm %{buildroot}/var/lib/xkb/compiled/README.compiled

%remove_docs

%files
%manifest %{name}.manifest
%defattr(-,root,root,-)
%license COPYING
%{_libdir}/xorg/protocol.txt
%{_bindir}/X
%{_bindir}/Xorg
%{_bindir}/gtf
%{_bindir}/cvt
%dir %{_libdir}/xorg
%dir %{_libdir}/xorg/modules
%if %{with mesa}
%ifarch %ix86 x86_64
%dir %{_libdir}/xorg/modules/extensions
%{_libdir}/xorg/modules/extensions/*.so
%endif
%endif
%dir %{_libdir}/xorg/modules/multimedia
%{_libdir}/xorg/modules/multimedia/*.so
%{_libdir}/xorg/modules/*.so

%files setuid
%manifest %{name}.manifest
%defattr(-,root,root,-)
%license COPYING
%{_libdir}/xorg/protocol.txt
%{_bindir}/X
# Is there a way to list subpackage files and the package
# files in a single %files directive?
# only a single difference here
%attr(4755,root,root) %{_bindir}/Xorg
%{_bindir}/gtf
%{_bindir}/cvt
%dir %{_libdir}/xorg
%dir %{_libdir}/xorg/modules
%if %{with mesa}
%ifarch %ix86 x86_64
%dir %{_libdir}/xorg/modules/extensions
%{_libdir}/xorg/modules/extensions/*.so
%endif
%endif
%dir %{_libdir}/xorg/modules/multimedia
%{_libdir}/xorg/modules/multimedia/*.so
%{_libdir}/xorg/modules/*.so

%files devel
%manifest %{name}.manifest
%defattr(-,root,root,-)
%{_libdir}/pkgconfig/xorg-server.pc
%dir %{_includedir}/xorg
%{_includedir}/xorg/*.h
%{_datadir}/aclocal/xorg-server.m4

