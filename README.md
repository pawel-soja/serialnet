# SerialNet
SerialNet is a converter that allows access to the serial port through a network connection.
It was created to connect the SynScan App with the Sky-Watcher Az mount via Raspberry PI

Supported network protocols:
+ UDP
+ TCP
+ WebSocket (unsecure, binnary mode)

# Building

## Install Pre-requisites

On Debian/Ubuntu/Raspberry:

```
sudo apt-get install -y git libqt5websockets5-dev libqt5serialport5-dev
```

## Create Project Directory
```
mkdir -p ~/Projects
cd ~/Projects
```

## Get the code
```
git clone https://github.com/pawel-soja/serialnet.git
```

## Build serialnet

```
mkdir -p ~/Projects/build/serialnet
cd ~/Projects/build/serialnet
qmake ~/Projects/serialnet
make
sudo make install
```

# Example

## Sky-Watcher Az mount via Raspberry PI
Connect Sky-Watcher Az mount to Raspberry PI with RJ12 cable to UART pins, unlock uart by turning it on in raspi-config, then run serialnet:
```
serialnet --device /dev/serial0 --baud 9600 --cr-flush --udp-port 11880
```
Now you can connect to the SynScan App by entering the Raspberry IP address in the settings.
