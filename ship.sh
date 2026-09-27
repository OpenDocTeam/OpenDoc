#!/usr/bin/env bash

set -euo pipefail

cd "$(dirname "$0")"
ROOT="$PWD"
BUILD="$ROOT/build-ship-linux"
STAGE="$BUILD/stage"
SHIPDIR="$ROOT/ship"

echo "[ship] OpenDoc Linux shipper"
echo

# ---- version from CMakeLists.txt -----------------------------------------
VERSION=$(sed -n 's/^project(OpenDoc VERSION \([0-9][0-9.]*\).*/\1/p' CMakeLists.txt)
if [ -z "$VERSION" ]; then
    echo "[ship] ERROR: could not read project version from CMakeLists.txt"
    exit 1
fi
echo "[ship] version: $VERSION"

# ---- preflight ------------------------------------------------------------
missing=()
for t in cmake ninja g++ zip unzip ldd file; do
    command -v "$t" >/dev/null 2>&1 || missing+=("$t")
done
if [ ! -e /usr/include/yaml-cpp/yaml.h ] && [ ! -e /usr/local/include/yaml-cpp/yaml.h ]; then
    missing+=("yaml-cpp headers (libyaml-cpp-dev)")
fi
if [ "${#missing[@]}" -gt 0 ]; then
    echo "[ship] ERROR: missing: ${missing[*]}"
    echo "[ship] install with:"
    echo "       sudo apt install build-essential cmake ninja-build zip libyaml-cpp-dev"
    exit 1
fi

# ---- build ----------------------------------------------------------------
echo "[ship] === Linux (DEB) ==="
cmake -S "$ROOT" -B "$BUILD" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DOPENDOC_STATIC_YAML_CPP=ON \
    -DCMAKE_BUILD_RPATH_USE_ORIGIN=ON \
    -DCMAKE_EXE_LINKER_FLAGS="-pthread" \
    -DCMAKE_SHARED_LINKER_FLAGS="-pthread"
cmake --build "$BUILD"

# ---- stage ----------------------------------------------------------------
rm -rf "$STAGE"
mkdir -p "$STAGE"
cp "$BUILD/opendoc" "$STAGE/"
cp "$BUILD/libopendoc_sdk.so" "$STAGE/"

# ---- verify ----------------------------------------------------------------
echo "[ship] --- binary checks ---"
file "$STAGE/opendoc"
if ! file "$STAGE/opendoc" | grep -q "ELF 64-bit"; then
    echo "[ship] ERROR: opendoc is not a 64-bit ELF binary"
    exit 1
fi

if ldd "$STAGE/opendoc" | grep -q "not found"; then
    echo "[ship] ERROR: unresolved shared library dependencies:"
    ldd "$STAGE/opendoc" | grep "not found"
    exit 1
fi
if ldd "$STAGE/opendoc" | grep -q "libyaml-cpp"; then
    echo "[ship] WARNING: binary still depends on the shared libyaml-cpp:"
    ldd "$STAGE/opendoc" | grep "libyaml-cpp"
else
    echo "[ship] yaml-cpp: statically linked"
fi
echo "[ship] shared dependencies:"
ldd "$STAGE/opendoc" | sed 's/^/    /'

if command -v readelf >/dev/null 2>&1; then
    if readelf -d "$STAGE/opendoc" | grep -E 'R(UN)?PATH' | grep -qF '$ORIGIN'; then
        echo "[ship] rpath: resolves libopendoc_sdk.so via \$ORIGIN"
    else
        echo "[ship] ERROR: opendoc has no \$ORIGIN rpath, the zip would not run elsewhere:"
        readelf -d "$STAGE/opendoc" | grep -E 'R(UN)?PATH' || echo "    (no runpath at all)"
        exit 1
    fi
fi

# smoke test
"$STAGE/opendoc" --version

# ---- zip ------------------------------------------------------------------
ZIP="$SHIPDIR/OpenDoc-$VERSION-linux-deb.zip"
mkdir -p "$SHIPDIR"
rm -f "$ZIP"
( cd "$STAGE" && zip -r "$ZIP" . ) >/dev/null
echo "[ship] created $ZIP"

# verify the executable bit survived the zip round-trip
if ! zipinfo -l "$ZIP" | grep -q -- "-rwx.*opendoc$"; then
    echo "[ship] ERROR: opendoc lost its executable permission inside the zip"
    exit 1
fi

echo "[ship] zip contents:"
zipinfo -l "$ZIP" | sed 's/^/    /'

echo "[ship] ---------------------------------------------------------------"
if [ -f "$SHIPDIR/OpenDoc-$VERSION-windows-x86_64.zip" ] \
   && [ -f "$SHIPDIR/OpenDoc-$VERSION-windows-arm64.zip" ]; then
    echo "[ship] all three zips are present in ship/"
else
    echo "[ship] note: run ship.bat on Windows to produce the Windows zips"
fi
echo "[ship] RESULT: OK"
