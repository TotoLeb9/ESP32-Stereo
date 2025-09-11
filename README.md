# Embedded glasses
The goal of this project is to develop smart embedded glasses that assist blind people in walking on their own.
Based on the ESP32-S3 Sense, this device is lightweight and inexpensive, costing only **€36** and weighing just **12g** of electronics without batteries.
You will need idf to build the code and flash it into the cards.

## Installing dependencies for esp32s3
```sh
git clone --recursive https://github.com/espressif/esp-idf.git
cd esp-idf
```
On windows 
```sh
install.bat
export.bat // To configure the environment
```
On linux
```sh
./install.sh
. ./export.sh // To configure the environment
```
Now it should be good, on both system:
```sh
idf.py --version // Check of version and good installation
```
## Launch the project
```sh
git clone https://github.com/TotoLeb9/ESP32-Stereo.git
cd ESP32-Stereo/
idf.py build
```
This step can take about 2 minutes.  
To flash the code onto your boards, connect a USB-C cable from the ESP to your PC.  
Then use
```sh
idf.py flash
```
This command will write the code onto the ESP32. You can add the option **monitor** to debug.  
Repeat this operation for both esp32.  
Once flashed, the ESP32 will start running the program automatically. The devices will exchange their MAC addresses, and the one with the highest address will become the **master**.
The master will start in Access Point mode, and you will be able to connect to it.  
The SSID is **ESP32_AP**, the password **12345678**.  
Once connected, launch your favorite browser and type 192.168.4.1 in the address bar, it's the static IP of the master esp32.
