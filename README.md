# unimrcp-ali
unimrcp server with plugin Ali SDKCpp3.X .

1、unimrcp-deps-1.6.0
chmod -R +x unimrcp-deps-1.6.0/
cd unimrcp-deps-1.6.0
./build-dep-libs.sh

2、unimrcp-1.6.0
chmod -R +x unimrcp-1.6.0/
cd unimrcp-1.6.0
./bootstrap
./configure
make
make install

3、dependence file
unimrcp-1.6.0\libs\NlsSdkCpp3.X\lib（five files：libalibabacloud-idst-*,libjsoncpp*）copy to destination directory: /usr/local/unimrcp/lib/
