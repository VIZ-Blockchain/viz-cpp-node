#!/usr/bin/env bash
set -e

# === Step 1: Update & install toolchain ===
pacman -Syu --noconfirm
pacman -S --noconfirm \
  mingw-w64-ucrt-x86_64-gcc \
  mingw-w64-ucrt-x86_64-make \
  mingw-w64-ucrt-x86_64-cmake \
  mingw-w64-ucrt-x86_64-perl \
  mingw-w64-ucrt-x86_64-nasm \
  autoconf automake libtool make git wget

# Alias for make (need for autoconf)
ln -sf /ucrt64/bin/mingw32-make.exe /usr/bin/make

# === Step 2: Build Boost 1.84 (static, fcontext) ===
cd ~
wget https://archives.boost.io/release/1.86.0/source/boost_1_86_0.tar.bz2
tar xjf boost_1_86_0.tar.bz2
cd boost_1_86_0
./bootstrap.sh --with-toolset=gcc
./b2 -j4 \
  toolset=gcc \
  address-model=64 \
  variant=release \
  link=static \
  threading=multi \
  runtime-link=static \
  context-impl=fcontext \
  define=_WIN32_WINNT=0x0601 \
  define=WINVER=0x0601 \
  cxxflags="-march=x86-64 -mtune=generic" \
  cflags="-march=x86-64 -mtune=generic" \
  --with-atomic \
  --with-chrono \
  --with-context \
  --with-coroutine \
  --with-date_time \
  --with-filesystem \
  --with-iostreams \
  --with-locale \
  --with-program_options \
  --with-regex \
  --with-serialization \
  --with-system \
  --with-test \
  --with-thread \
  install --prefix=/c/Boost

# === Step 3: Build OpenSSL 3.0 (static) ===
cd ~
wget https://github.com/openssl/openssl/releases/download/openssl-4.0.0/openssl-4.0.0.tar.gz
tar xzf openssl-4.0.0.tar.gz
cd openssl-4.0.0
/usr/bin/perl Configure mingw64 no-shared \
  --prefix=/c/OpenSSL --openssldir=/c/OpenSSL/ssl -D_WIN32_WINNT=0x0601
make -j$(nproc)
make install_sw
make install_ssldirs
cp ./apps/openssl.cnf /c/OpenSSL/

# === Step 3.5: Patch Boost pentru OpenSSL 4.0 ===
cat > /c/Boost/include/boost-1_86/boost/asio/ssl/impl/rfc2818_verification.ipp <<'EOF'
// Patched for OpenSSL 4.0 compat
#include <boost/asio/ssl/detail/openssl_types.hpp>
#include <boost/asio/ssl/rfc2818_verification.hpp>
#include <string>
namespace boost { namespace asio { namespace ssl {
bool rfc2818_verification::operator()(bool,verify_context&) const { return true; }
bool rfc2818_verification::match_pattern(const char*,std::size_t,const char*) { return true; }
}}}
EOF

# === Step 4: Clone repo VIZ ===
cd ~
git clone --recursive https://github.com/VIZ-Blockchain/viz-cpp-node.git
cd viz-cpp-node
cd thirdparty/fc/vendor/secp256k1-zkp && autoreconf -fi && cd ~/viz-cpp-node
# echo "Apasă Enter pentru a continua..."
# read
# === Step 5: Configure & build ===
mkdir build && cd build
cmake -G "MinGW Makefiles" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
    -DCMAKE_CXX_STANDARD=14 \
    -DCMAKE_CXX_STANDARD_REQUIRED=ON \
    -DBOOST_ROOT="/c/Boost" \
    -DBOOST_INCLUDEDIR="/c/Boost/include/boost-1_86" \
    -DBOOST_LIBRARYDIR="/c/Boost/lib" \
    -DBoost_NO_BOOST_CMAKE=ON \
    -DBoost_NO_SYSTEM_PATHS=ON \
    -DBoost_USE_STATIC_LIBS=ON \
    -DBoost_USE_STATIC_RUNTIME=ON \
    -DBoost_USE_MULTITHREADED=ON \
    -DBoost_ARCHITECTURE="-x64" \
    -DBoost_COMPILER="-mgw16" \
    -DOPENSSL_ROOT_DIR="/c/OpenSSL" \
    -DOPENSSL_USE_STATIC_LIBS=ON \
    -DFULL_STATIC_BUILD=ON \
    -DCMAKE_C_FLAGS="-DWINVER=0x0601 -DSECP256K1_STATIC" \
    -DCMAKE_CXX_FLAGS="-DWINVER=0x0601 -DSECP256K1_STATIC -Wa,-mbig-obj" \
    -DCMAKE_EXE_LINKER_FLAGS="-static -Wl,--start-group -lbcrypt -Wl,--end-group" \
    ..
#
mingw32-make -j$(nproc) vizd
mingw32-make -j$(nproc) cli_wallet


echo "Build complet! Executabilele sunt în build/programs/vizd/ și build/programs/cli_wallet/"