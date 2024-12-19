#! /bin/bash

# Prerequisites: Anything that you need to build joyosc on the host platform +
# the install_name_tool, codesign, and hdiutil utilities on macOS (these
# should be included in your Xcode installation) + the fileicon utility
# (available in Homebrew).

# The script can be called from anywhere as long as the proper (relative or
# absolute) path to the script is included. (Usually you'll invoke it from the
# main source directory or the package subdirectory.) It populates the staging
# area (in the $build subdirectory of the current working directory) using
# `make install`, constructs the package contents from it, and zips up the
# package. On macOS and Windows, the required libraries from Homebrew or Mingw
# will be included in the package.

# On Windows, the zip file contains the joyosc application directory, on macOS
# a disk image (dmg file) with the application directory. This can be
# installed in your Applications folder on macOS, or your 'Program Files'
# folder on Windows. The application is launched with the application icon
# (Joyosc.app bundle on macOS, .lnk file on Windows) contained in the joyosc
# folder. Please check the README.md file included in the package for details.

# The package can also be built on Linux, but this is mostly for testing
# purposes right now. In the future, it may become useful as a basis for
# building a flatpak. The Linux package doesn't contain any libraries, so
# you'll have to make sure that libSDL2 and libtinyxml are installed from the
# package manager of your distribution. A .desktop file is included in the
# package which can be used to launch the program. However, you may have to
# adjust the desktop file depending on where you install the package, and copy
# the png/gamepad.png icon file to some location searched by the desktop
# environment that you use.

scriptdir=$(dirname $0)
srcdir="$(cd $scriptdir/..; pwd)"
cwd=$(pwd)
build=build

# You can pass the joyosc version as the first script argument. By default, it
# is extracted from the configure.ac file.
version=$(grep AC_INIT $srcdir/configure.ac | sed 's/AC_INIT.*\[\([0-9.]*\)\].*/\1/')

# Usually you shouldn't have to edit anything below this line.

if [ -n "$1" ]; then version="$1"; fi

os=$(uname -o)
arch=$(uname -m)

# canonical OS/platform name
if [ "$os" == "Darwin" ]; then
    os="macos"
elif [ "$os" == "Msys" ]; then
    os="mingw"
elif [ "$os" == "GNU/Linux" ]; then
    os="linux"
fi

# platform-specific configuration
prefix=/usr/local
if [ "$os" == "macos" ]; then
    echo "building macOS package"
    if [ "$arch" == "arm64" ]; then
	libdir=/opt/homebrew/opt
    else
	libdir=/usr/local/homebrew/opt
    fi
    libs="$libdir/sdl2/lib/libSDL2-2.0.0.dylib $libdir/liblo/lib/liblo.7.dylib $libdir/tinyxml2/lib/libtinyxml2.10.dylib"
elif [ "$os" == "mingw" ]; then
    echo "building Windows (mingw) package"
    prefix=/mingw64
    libdir=/mingw64/bin
    libs="$libdir/SDL2.dll $libdir/libgcc_s_seh-1.dll $libdir/liblo-7.dll $libdir/libstdc++-6.dll $libdir/libtinyxml2.dll $libdir/libwinpthread-1.dll"
elif [ "$os" == "linux" ]; then
    echo "building Linux package"
    # don't package any libraries
    libs=
else
    echo "unrecognized OS $os, exiting" >&2
    exit 1
fi

app=joyosc
pkgname=$app-$version-$os-$arch
targetdir=$build/$app/$app

# install to staging area
rm -rf $build
make -C $srcdir install DESTDIR=$cwd/$build >/dev/null

# populate the package contents
mkdir -p $targetdir/bin
cp $build/$prefix/bin/* $libs $targetdir/bin
cp -r $build/$prefix/share/doc/joyosc $targetdir/doc
cp $targetdir/doc/joyosc.bmp $targetdir/bin
cp $scriptdir/app/README.md $targetdir

# copy the app icon
if [ "$os" == "macos" ]; then
    cp -r $scriptdir/app/Joyosc.app $targetdir
    fileicon set $targetdir/Joyosc.app $targetdir/doc/joyosc.icns
elif [ "$os" == "mingw" ]; then
    cp $scriptdir/app/joyosc.lnk $targetdir
elif [ "$os" == "linux" ]; then
    cp $scriptdir/app/joyosc.desktop $targetdir
fi

# zip it up

cd $build

if [ "$os" == "macos" ]; then

# fix up the lib dependenciess

for lib in $libs; do
    install_name_tool -change $lib @executable_path/$(basename $lib) $app/$app/bin/lsjs
    install_name_tool -change $lib @executable_path/$(basename $lib) $app/$app/bin/joyosc
done

# self-sign
codesign --deep --sign "-" $app/$app

# TODO: add the icon to the app bundle, not sure how to do this from the
# command line

# create dmg
rm -f $app.dmg
hdiutil create -volname $app -srcfolder $app $app.dmg

# zip
zip $pkgname.zip $app.dmg

else

# linux/mingw: just zip the package contents

cd $app
zip -r $pkgname.zip $app
mv $pkgname.zip ..

fi

cd $cwd

# move the zip to the package dir
rm -f $scriptdir/$pkgname.zip
mv $build/$pkgname.zip $scriptdir

# remove the staging directory
rm -rf $build

# done
exit 0
