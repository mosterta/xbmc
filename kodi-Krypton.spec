# Don't try fancy stuff like debuginfo, which is useless on binary-only
# packages. Don't strip binary either
# Be sure buildpolicy set to do nothing
%define        __spec_install_post %{nil}
%define          debug_package %{nil}
%define        __os_install_post %{_dbpath}/brp-compress
%define _unpackaged_files_terminate_build 0 

Summary: KODI - A home entertainment system
Name: kodi-17.0
Version: alpha1_Krypton_git_20160409_3283a64.mga5
Release: 102
License: GPL+
Group: Entertainment/Multimedia
SOURCE0 : %{name}-%{version}.tar.xz
URL: http://kodi.tv/

BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}-root

%description
%{summary}

%prep
%setup -q

%build
# Empty section.

%install
rm -rf %{buildroot}
mkdir -p  %{buildroot}

# in builddir
cp -a * %{buildroot}


%clean
rm -rf %{buildroot}


%files
%defattr(-,root,root,-)
#%{_bindir}/*
/opt
/etc

%changelog
* Sat Feb 06 2016  Stig-Ørjan Smelror <smelror@gmail.com> 17-ALPHA1-GIT
- Reverted back to the standard $HOME/.kodi
- Added a wmsession file as suggested on the Mageia Forum by Latte
- Will now install to /opt/krypton instead of /opt/kodi/krypton

* Fri Feb 05 2016  Stig-Ørjan Smelror <smelror@gmail.com> 17-ALPHA1-GIT
- First Build
