Name:           dummy_netd
Version:        1.0.2
Release:        1
Summary:        A simple dummy netd service implementation.
License:        BSD
URL:            https://github.com/mer-hybris/dummy_netd
Source:         %{name}-%{version}.tar.bz2

%define libgbinder_version 1.0.26

BuildRequires:  pkgconfig(libgbinder) >= %{libgbinder_version}
BuildRequires:  pkgconfig(libglibutil)
BuildRequires:  pkgconfig(libsystemd)
BuildRequires:  pkgconfig(glib-2.0)

Requires:       libgbinder >= %{libgbinder_version}

%description
dummy_netd provides the android.system.net.netd@1.1 service for devices which cannot work without it.

%prep
%autosetup -n %{name}-%{version}

%build
make KEEP_SYMBOLS=1 release

%install
%define target_wants_dir %{_unitdir}/graphical.target.wants
%define service dummy_netd
rm -rf %{buildroot}
make install DESTDIR=%{buildroot} UNITDIR=%{_unitdir}
mkdir -p %{buildroot}/%{target_wants_dir}
ln -s ../%{service}.service %{buildroot}/%{target_wants_dir}/%{service}.service

%clean
rm -rf %{buildroot}
make clean

%pre
systemctl stop %{service} ||:

%post
systemctl daemon-reload ||:
systemctl start %{service} ||:

%postun
systemctl daemon-reload ||:

%files
%defattr(-,root,root,-)
%license LICENSE
%{_sbindir}/dummy_netd
%{target_wants_dir}/%{service}.service
%{_unitdir}/%{service}.service
