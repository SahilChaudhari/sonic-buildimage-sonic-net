# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: Copyright (C) 2024 Advanced Micro Devices, Inc.

# Version is overwritten by `make dist`. Release conditionally overriden by %%kmod_release
Name:           tawk_ipc
Version:        0.1.0.dev
Release:        1%{?dist}
Summary:        AMD Pensando TAWK IPC Driver
Vendor:         AMD Pensando
URL:            https://www.amd.com/en/accelerators/pensando
Group:          System Environment/Kernel
License:        MIT
ExclusiveArch:  x86_64
Source0:        %{name}-%{version}.tar.gz
%global flavors_to_build default

# Add `rpmbuild --without *` options for skipping subpackages
%bcond_without common
%bcond_without debuginfo
%bcond_without dkms
%bcond_without akmod
%bcond_without kmod


# When called by /usr/sbin/akmodsbuild, build only kmod
%if 0%{?kernels:1}
%undefine with_common
%undefine with_debuginfo
%undefine with_dkms
%undefine with_akmod
%global kernel_version %{kernels}
%endif

#### DEBUGINFO SUBPACKAGE ####
%if %{without debuginfo}
%define debug_package %{nil}
%endif

#### COMMON FILES SUBPACKAGE ####
%if %{with common}
%package common
Summary:        Common files for %{summary}
BuildArch:      noarch
Requires:       dracut kmod
Requires:       %{name}-kmod = %{version}-%{release}
Provides:       %{name}-kmod-common = %{version}-%{release}

%description common
Systemd configuration and license files for %{summary}.

%files common
%defattr(0644, root, root, 0755)
%license LICENSE*
%doc README.md assets
%{_libdir}/depmod.d/*
%{_libdir}/dracut.conf.d/*
%endif

#### DKMS SUBPACKAGE ####
%if %{with dkms}
%package dkms
Summary:        %{summary} as DKMS
BuildArch:      noarch
Requires:       dkms >= 2.6
Requires:       %{name}-kmod-common = %{version}-%{release}
Provides:       %{name}-kmod = %{version}-%{release}

%description dkms
%{summary} as DKMS.

%files dkms
%defattr(0644, root, root, 0755)
%{_usrsrc}/%{name}-%{version}

%post dkms
%{_prefix}/lib/dkms/common.postinst %{name} %{version}

%preun dkms
dkms remove -m %{name} -v %{version} --all --rpm_safe_upgrade
%endif

#### AKMOD SUBPACKAGE ####
%if %{with akmod}
%package akmod
Summary:        %{summary} as Akmod
Requires:       akmods make gcc kernel-rpm-macros
Provides:       %{name}-kmod = %{version}-%{release}
Requires:       %{name}-kmod-common = %{version}-%{release}
BuildArch:      noarch

%description akmod
%{summary} as Akmod

%posttrans akmod
nohup /usr/sbin/akmods --from-akmod-posttrans --akmod %{name} &> /dev/null &

%post akmod
[ -x /usr/sbin/akmods-ostree-post ] && /usr/sbin/akmods-ostree-post %{name} %{_usrsrc}/akmods/kmod-%{name}-%{version}-%{release}.src.rpm

%files akmod
%defattr(-,root,root,-)
%{_usrsrc}/akmods/*
%endif

#### KMOD SUBPACKAGE ####
%if %{with kmod}
%global kmod_preamble \
Requires:       %{name}-kmod-common = %{version}-%{release} \
BuildRequires:  make gcc kernel-rpm-macros

# Optionally, uniquely identify the kmod RPM by kernel version string
%if 0%{?kernel_version:1}
%global kmod_release -r %(echo "%{release}" | cut -d. -f1).%(echo "%{kernel_version}" | tr - .)%{?dist}
%global kmod_preamble %{?kmod_preamble} \
Provides:       %{name}-kmod = %{version}-%{release} \
Provides:       kmod-%{name} = %{version}-%{release} \
BuildRequires:  %%%%(realpath %%%%{kernel_source %{flavors_to_build}}) \
%endif

%global kmod_preamble_tmpfile %(mktemp --suffix=.kmod_preamble.spec)
%(echo "%{kmod_preamble}" > %{kmod_preamble_tmpfile})

# Macro defined in:
# - RHEL: kernel-srpm-macros (/usr/lib/rpm/macros.d/macros.kmp), or
# - SUSE: kernel-macros (/usr/lib/rpm/macros.d/macros.kernel-source)
%kernel_module_package %{?suse_version:-b} -p %{kmod_preamble_tmpfile} %{?kmod_release} %{flavors_to_build}
%(rm %{kmod_preamble_tmpfile})

%else
# Only kmod package contains binaries
%define debug_package %{nil}
%endif

#### ALL ####

%description
%{summary}

%prep
%autosetup

%build
%if %{with kmod}
%make_build KSRC=%{kernel_source %{flavors_to_build}}
%endif

%install
%if %{with kmod}
%{__install} -D -m 755 %{name}.ko %{buildroot}/lib/modules/%{kernel_version}/%{kernel_module_package_moddir}/%{name}/%{name}.ko
%endif

%if %{with common}
%{__install} -D -m 644 depmod.conf %{buildroot}%{_libdir}/depmod.d/kmod-%{name}.conf
%{__install} -D -m 644 dracut.conf %{buildroot}%{_libdir}/dracut.conf.d/%{name}.conf
%endif

%if %{with dkms}
mkdir -p %{buildroot}%{_usrsrc}/%{name}-%{version}/
tar xf %{SOURCE0} -C %{buildroot}%{_usrsrc}/
%endif

%if %{with akmod}
# Based on output of EPEL `kmodtool --akmod`
mkdir -p %{buildroot}/%{_usrsrc}/akmods/
rpmbuild \
    --define "_sourcedir %{_sourcedir}" \
    --define "_srcrpmdir %{buildroot}/%{_usrsrc}/akmods/" \
    %{?dist:--define 'dist %{dist}'} \
    -bs --nodeps %{_specdir}/%{name}.spec
ln -s $(ls %{buildroot}/%{_usrsrc}/akmods/) %{buildroot}/%{_usrsrc}/akmods/%{name}-kmod.latest
%endif
