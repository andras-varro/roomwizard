#!/bin/sh
# Buildroot post-build hook: runs after the target tree is assembled, before the
# tarball is made. $1 is the target directory.
#
# Deliberately empty for now: the init scripts, udev rules and first-boot host
# key generation are installed here once they exist. The root password is empty
# until then, so the image is for bench use only.
set -eu
exit 0
