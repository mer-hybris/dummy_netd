Name:           dummy_netd
Version:        1.0.0
Release:        1%{?dist}
Summary:        A simple dummy netd service implementation.

Group:          Applications/System
License:        BSD
URL:            https://github.com/mer-hybris/dummy_netd
Source:         %{name}-%{version}.tar.bz2

BuildRequires:  libgbinder-devel >= 1.0.7
BuildRequires:  pkgconfig(libsystemd)

%description
dummy_netd provides the android.system.net.netd@1.1 service for devices which cannot work without it.

%prep
%setup -q -n %{name}-%{version}

%build
make %{?_smp_mflags}

%install
rm -rf $RPM_BUILD_ROOT
make install DESTDIR=$RPM_BUILD_ROOT
mkdir -p $RPM_BUILD_ROOT/lib/systemd/system
cp dummy_netd.service $RPM_BUILD_ROOT/lib/systemd/system
mkdir $RPM_BUILD_ROOT/lib/systemd/system/graphical.target.wants
ln -s ../dummy_netd.service $RPM_BUILD_ROOT/lib/systemd/system/graphical.target.wants/dummy_netd.service

%clean
rm -rf $RPM_BUILD_ROOT
make clean

%files
%defattr(-,root,root,-)
/usr/sbin/dummy_netd
/lib/systemd/system/graphical.target.wants/dummy_netd.service
/lib/systemd/system/dummy_netd.service
