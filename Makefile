

SHELL = /bin/bash -vc

MESON := meson
BUILD_DIR := buildDir
PREFIX ?=

ifneq (${PREFIX},)
  PREFIX_OPT := --prefix ${PREFIX}
else
  PREFIX_OPT :=
endif

# Returns name if exists else nothing
test_for_dir = $(shell test -d $1 && echo $1)
test_for_file = $(shell test -f $1 && echo $1)

.PHONY: Makefile

all: appImage

.PHONY: prep
prep: $(if $(call test_for_dir,${BUILD_DIR}),clean)
	${MESON} setup ${PREFIX_OPT} ${BUILD_DIR} .

${BUILD_DIR}: prep

.PHONY: build
build: ${BUILD_DIR}
	${MESON} compile -C ${BUILD_DIR}

.PHONY: clean
clean:
	${MESON} setup --wipe ${BUILD_DIR}
	rm -rf build ${BUILD_DIR}

.PHONY: appImage
appImage: clean prep
	./dist.sh

.PHONY: install
install: build
	$(MESON) install -C ${BUILD_DIR}

