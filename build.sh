git submodule update --init --recursive
cd support/libssh/build
cmake -DCMAKE_INSTALL_PREFIX=/usr/local -DCMAKE_BUILD_TYPE=Debug -DWITH_STATIC_LIB=ON ..
make install
PKG_CONFIG_PATH=/usr/local/lib/pkgconfig
export PKG_CONFIG_PATH
cd ../../..
node-waf configure
node-waf build