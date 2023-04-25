#!/bin/bash
#PAR=$(nproc)
PAR=8
set -eux
cd $(dirname $0)
DIR="$PWD"
DEPFLAGS=
BLDFLAGS=
case "$*" in
	*stable*)
	       BDIR=stable
	       ;;
	*)
	       BDIR=build
	       ;;
esac
case "$*" in
	*super*)
		PROD=super
		BDIR="super${BDIR}"
		;;
	*)
		PROD=prusa
		;;
esac
DDIR="deps/$BDIR"
case "$*" in
	*cmd*)
	       BDIR="${BDIR}cmd"
	       BLDFLAGS="$BLDFLAGS -DSLIC3R_GUI=no"
	       ;;
esac
case "$*" in
	*dyn*)
	       DDIR="${DDIR}dyn"
	       BDIR="${BDIR}dyn"
	       DEPFLAGS="$DEPFLAGS -DPrusaSlicer_deps_PACKAGE_EXCLUDES:STRING=Blosc;Boost;Catch2;Cereal;CGAL;CURL;Eigen;EXPAT;GLEW;GMP;JPEG;MPFR;NLopt;OCCT;OpenCSG;OpenEXR;OpenSSL;PNG;Qhull;TBB;TIFF;z3;ZLIB"
	       DEPFLAGS="$DEPFLAGS -DSlic3r-deps_PACKAGE_EXCLUDES:STRING=Blosc;Boost;Catch2;Cereal;CGAL;CURL;Eigen;EXPAT;GLEW;GMP;JPEG;MPFR;NLopt;OCCT;OpenCSG;OpenEXR;OpenSSL;PNG;Qhull;TBB;TIFF;z3;ZLIB"
	       BLDFLAGS="$BLDFLAGS -DCMAKE_FIND_PACKAGE_PREFER_CONFIG=ON -DFORCE_OpenCASCADE_VERSION=7.5.0 -DOPENVDB_USE_STATIC_LIBS=ON -DUSE_BLOSC=TRUE"
	       ;;
esac
case "$*" in
	*debug*)
	       BDIR="${BDIR}debug"
	       BLDFLAGS="$BLDFLAGS -DCMAKE_BUILD_TYPE=Debug"
	       STRIP=
	       ;;
       *)
	       STRIP=/strip
	       ;;
esac
case "$*" in
	*clean*) rm -rf "$DIR/$DDIR" "$DIR/$BDIR";;
esac
export CMAKE_C_COMPILER_LAUNCHER=ccache
export CMAKE_CXX_COMPILER_LAUNCHER=ccache
mkdir -p "$DIR/$DDIR"
cd "$DIR/$DDIR"
cmake .. $DEPFLAGS -DDEP_WX_GTK3=ON -DDEP_DOWNLOAD_DIR="$DIR/deps/download"
make -j $PAR
test -r destdir/usr/local/lib/libwx_gtk3u_scintilla-3.2.a ||
	ln -s libwxscintilla-3.2.a destdir/usr/local/lib/libwx_gtk3u_scintilla-3.2.a
mkdir -p "$DIR/$BDIR"
cd "$DIR/$BDIR"
cmake .. $BLDFLAGS \
	-DSLIC3R_GTK=3 -DSLIC3R_PCH=OFF \
	-DCMAKE_PREFIX_PATH="$DIR/$DDIR/destdir/usr/local" \
	-DCMAKE_INSTALL_PREFIX=~/tools/${PROD}slicer/$BDIR
make -j $PAR
make install$STRIP
