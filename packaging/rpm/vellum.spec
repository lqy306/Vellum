# vellum.spec
# 构建：rpmbuild -bb vellum.spec
# 在 Debian 系上需要 --define '_libdir /usr/lib/x86_64-linux-gnu'（RHEL/Fedora 自动使用 /usr/lib64）。

Name:           vellum
Version:        2026.08.27
Release:        1%{?dist}
Summary:        A focused GTK4 text editor
License:        BSD-2-Clause
URL:            https://github.com/lqy306/Vellum
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  gcc
BuildRequires:  meson
BuildRequires:  ninja-build
BuildRequires:  pkgconfig(gtk4) >= 4.10
BuildRequires:  pkgconfig(libadwaita-1) >= 1.5
BuildRequires:  pkgconfig(gtksourceview-5) >= 5.10
BuildRequires:  pkgconfig(libspelling-1) >= 0.2
BuildRequires:  pkgconfig(libsoup-3.0)
BuildRequires:  pkgconfig(json-glib-1.0)

Requires:       gtk4 >= 4.10
Requires:       libadwaita >= 1.5
Requires:       gtksourceview5 >= 5.10
Requires:       libspelling
Requires:       libsoup3
Requires:       json-glib

%description
Vellum is a clean, focused text editor built with GTK4, Libadwaita and
GtkSourceView. It supports multiple documents, syntax highlighting,
search and replace, draft recovery, and C plugins.

%prep
rm -rf %{_builddir}/vellum-src
mkdir -p %{_builddir}/vellum-src
tar -xzf %{SOURCE0} -C %{_builddir}/vellum-src --strip-components=1

%build
cd %{_builddir}/vellum-src
meson setup build --prefix=%{_prefix} --buildtype=release
ninja -C build

%install
cd %{_builddir}/vellum-src
meson install -C build --destdir %{buildroot}

%files
%{_bindir}/vellum
%{_bindir}/vellum-tui-debug
%{_libdir}/vellum/plugins/*.so
%{_datadir}/applications/io.github.vellum.Vellum.desktop
%{_datadir}/metainfo/io.github.vellum.Vellum.metainfo.xml
%{_datadir}/icons/hicolor/scalable/apps/io.github.vellum.Vellum.svg
%{_datadir}/locale/en/LC_MESSAGES/vellum.mo
%{_datadir}/locale/zh_CN/LC_MESSAGES/vellum.mo

%changelog
* Thu Aug 27 2026 Leo Lee <lqy.work.learning@gmail.com> - 2026.08.27-1
- Initial release
