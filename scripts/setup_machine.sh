# Add ib_ipoib [1] module to the Linux Kernel
sudo modprobe ib_ipoib
# Configure the IP address and Network mask of the Infiniband interface
sudo ifconfig ib0 $1/24
# Download the Mellanox OFED SRC drivers (version 4.6) for Ubuntu 14.04 
wget http://www.mellanox.com/downloads/ofed/MLNX_OFED-4.6-1.0.1.1/MLNX_OFED_SRC-debian-4.6-1.0.1.1.tgz
# Extract files from the tarball
tar -xvzf MLNX_OFED_SRC-debian-4.6-1.0.1.1.tgz 
# Install required packages
sudo apt-get update
sudo apt-get install -y pkg-config libnl-3-dev libnl-route-3-dev libibumad-dev
# Build and install libibverbs library
cd MLNX_OFED_SRC-4.6-1.0.1.1/SOURCES/
tar -xvzf libibverbs_41mlnx1.orig.tar.gz 
cd libibverbs-41mlnx1/
./autogen.sh
./configure --prefix=/usr libdir=/usr/lib64
make
sudo make install
# Build and install libmlx4 library
cd ..; tar -xvzf libmlx4_41mlnx1.orig.tar.gz
cd libmlx4-41mlnx1/
./autogen.sh 
./configure --prefix=/usr libdir=/usr/lib64
make
sudo make install
# Install recent perftest package
sudo apt-get remove perftest -y
sudo apt-get install perftest -y
# Added installed Mellanox OEFD libraries to LD_LIBRARY_PATH
export LD_LIBRARY_PATH=/usr/lib64:$LD_LIBRARY_PATH

sudo apt-get install librdmacm-dev
