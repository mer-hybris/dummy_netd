Name:           dummy_netd
Version:        1.0.0
Release:        1
Summary:        A simple dummy netd service implementation.
License:        BSD
URL:            https://github.com/mer-hybris/dummy_netd
Source:         %{name}-%{version}.tar.bz2

BuildRequires:  libgbinder-devel >= 1.0.7
BuildRequires:  pkgconfig(libsystemd)
BuildRequires:  pkgconfig(glib-2.0)

%description
dummy_netd provides the android.system.net.netd@1.1 service for devices which cannot work without it.

%prep
%autosetup -n %{name}-%{version}

%build
%make_build

%install
%make_install
mkdir -p %{buildroot}%{_unitdir}
install -D -p -m 644 dummy_netd.service %{buildroot}%{_unitdir}/dummy_netd.service
cp dummy_netd.service %{buildroot}%{_unitdir}
mkdir %{buildroot}%{_unitdir}/graphical.target.wants
ln -s ../dummy_netd.service %{buildroot}%{_unitdir}/graphical.target.wants/dummy_netd.service

%clean
rm -rf %{buildroot}
make clean

%files
%defattr(-,root,root,-)
%{_sbindir}/dummy_netd
%{_unitdir}/graphical.target.wants/dummy_netd.service
%{_unitdir}/dummy_netd.service
