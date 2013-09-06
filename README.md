hulk-exchange
=============

cd ~

rm -rf hulk-*

mkdir -p hulk-build/core
mkdir -p hulk-build/fix
mkdir -p hulk-build/script
mkdir -p hulk-build/exchange
git clone https://github.com/mmcilroy/hulk-core.git
git clone https://github.com/mmcilroy/hulk-fix.git
git clone https://github.com/mmcilroy/hulk-script.git
git clone https://github.com/mmcilroy/hulk-exchange.git
cd hulk-build/core
cmake -DCMAKE_BUILD_TYPE=Debug ../../hulk-core
make
cd ../fix
cmake -DCMAKE_BUILD_TYPE=Debug ../../hulk-fix
make
cd ../script
cmake -DCMAKE_BUILD_TYPE=Debug ../../hulk-script
make
cd ../exchange
cmake -DCMAKE_BUILD_TYPE=Debug ../../hulk-exchange
make
