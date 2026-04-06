# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: Copyright (C) 2024 Advanced Micro Devices, Inc.

# Use Automake conventions

PACKAGE := tawk_ipc
VERSION = $$([ -s "$(CURDIR)/git_tag" ] && cat "$(CURDIR)/git_tag" || echo "0.1.0.dev")
DIST_TARGETS := dist-gzip

EXTRA_DIST := README.md assets LICENSE* dkms.conf tawk_ipc.spec depmod.conf dracut.conf
DIST_COMMON := Makefile Kbuild dist.mk git_tag kernel-gcc.sh
DIST_SOURCES := src include
DISTFILES := $(DIST_COMMON) $(DIST_SOURCES) $(EXTRA_DIST)

distdir := $(PACKAGE)-$(VERSION)

git_tag:
	git describe --tags | tr - . | tee "$(CURDIR)/git_tag"

.PHONY: distdir dist-gzip dist
distdir: git_tag
	mkdir -p "$(distdir)"
	cp --dereference -r $(DISTFILES) "$(distdir)"
	sed -i "/^Version:/ s/\(Version: *\).*/\1$(VERSION)/" "$(distdir)"/tawk_ipc.spec
	sed -i "/^PACKAGE_VERSION=/ s/.*/PACKAGE_VERSION=$(VERSION)/" "$(distdir)"/dkms.conf
	sed -i "/^#define TAWK_IPC_VERSION/ s/\(.*VERSION *\).*/\1\"$(VERSION)\"/" "$(distdir)"/src/ipc_main.h

dist-gzip: distdir
	tar --create --dereference --gzip --owner root --group root -f "$(distdir).tar.gz" "$(distdir)"

dist:
	$(MAKE) $(DIST_TARGETS)
	test -d "$(distdir)" && rm -irf "$(distdir)"
