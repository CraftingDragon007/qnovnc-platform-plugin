%if 0%{?fedora}
%global qt_major 6
%global qt_pkg_prefix qt6
%elif 0%{?rhel} && 0%{?rhel} < 10
%global qt_major 5
%global qt_pkg_prefix qt5
%else
%global qt_major 6
%global qt_pkg_prefix qt6
%endif

%global qt_plugin_dir %{_libdir}/qt%{qt_major}/plugins

Name:           qt-novnc-platform-plugin
Version:        1.0.0
Release:        1%{?dist}
Summary:        Qt QPA platform plugin providing noVNC server support

License:        LGPLv3+
URL:            https://github.com/CraftingDragon007/qt-novnc-platform-plugin
Source0:        %{url}/archive/refs/tags/v%{version}.tar.gz

BuildRequires:  cmake
BuildRequires:  gcc-c++
BuildRequires:  make
BuildRequires:  pkgconfig(zlib)

%if %{qt_major} == 6
BuildRequires:  qt6-qtbase-devel
BuildRequires:  qt6-qtbase-private-devel
BuildRequires:  qt6-qtbase-static
BuildRequires:  qt6-qtwebsockets-devel
Requires:       qt6-qtbase%{?_isa}
Requires:       qt6-qtwebsockets%{?_isa}
%else
BuildRequires:  qt5-qtbase-devel
BuildRequires:  qt5-qtbase-private-devel
BuildRequires:  qt5-qtbase-static
BuildRequires:  qt5-qtwebsockets-devel
Requires:       qt5-qtbase%{?_isa}
Requires:       qt5-qtwebsockets%{?_isa}
%endif

%description
QNoVNC is a Qt Platform Abstraction (QPA) plugin that exposes a built-in noVNC
server so that Qt applications can be remoted via a modern browser. It can be
built for both Qt 5 and Qt 6, with RHEL-family distributions below major
version 10 defaulting to Qt 5 while Fedora and newer releases use Qt 6.

%prep
%autosetup

%build
%cmake -DQT_DEFAULT_MAJOR_VERSION=%{qt_major}
%cmake_build

%install
%cmake_install

%files
%license LICENSE
%doc README.md
%{qt_plugin_dir}/platforms/libqnovnc.so

%changelog
* Wed Mar 12 2025 CraftingDragon007 <info@craftingdragon.ch> - 1.0.0-1
- Initial package for qt-novnc-platform-plugin
