#! /bin/sh
#
# generate autotools files
#

set -x

case `uname -s` in
    MINGW*)
        # autoreconf doesn't always work on MinGW
        aclocal --force -I m4 && \
        libtoolize --install --force && \
        autoconf --force && \
        automake --add-missing --copy --force-missing && \
        true
    ;;
    *)
        aclocal --force -I m4
        automake --add-missing
        autoreconf --install --force
    ;;
esac
