This repo contains the router logic of Octopus implemented in Mahimahi.

## Install dependencies

sudo apt-get install -y protobuf-compiler libprotobuf-dev autotools-dev dh-autoreconf iptables pkg-config dnsmasq-base apache2-bin apache2-dev libssl-dev ssl-cert libxcb-present-dev libcairo2-dev libpango1.0-dev

## Configure and build

./autogen.sh
./configure
make -j8
sudo make install
