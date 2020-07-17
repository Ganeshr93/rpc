# Remote Provisioning Client (RPC)

RPC is an application which enables remote capabilities for AMT, such as as device activation. To accomplish this, RPC communicates with the RPS (Remote Provisioning Server).

As a prerequisite, a Local Management Service (LMS) must be installed and running on the operating system.

## Linux

Steps below are for Ubuntu 18.04.

### Dependencies

- sudo apt install git cmake build-essential libboost-system-dev  libboost-thread-dev libboost-random-dev libboost-regex-dev libboost-filesystem-dev libssl-dev zlib1g-dev
- RPC requires CMake version 3.17.
  - Please use "cmake --version" to check version.
  - CMake be downloaded from <https://cmake.org/download/>.

### Build

- mkdir build
- cd build
- cmake -DCMAKE_BUILD_TYPE=Release ..
  - Build debug: cmake -DCMAKE_BUILD_TYPE=Debug ..
- cmake --build .

### Run

- See ./rpc --help for details.
- Example
  - sudo ./rpc --url wss://localhost:8080 --profile profile1

## Windows

Steps below are for Windows 10 and Visual Studio 2019 Professional.

### Dependencies

- RPC requires CMake version 3.17, which is included with Visual Studio 2019.
  - Please use "cmake --version" to check version.
  - CMake can be downloaded from <https://cmake.org/download/>.

### Build VCPKG

Open an x64 native command prompt for Visual Studio 2019 as Administrator.

- git clone --branch 2020.01 https://github.com/microsoft/vcpkg.git
- cd vcpkg
- bootstrap-vcpkg.bat
- vcpkg integrate install

### Build C++ REST SDK

Open an x64 native tools command prompt for Visual Studio 2019.

- cd vcpkg
- vcpkg install cpprestsdk:x64-windows-static

### Build

Open an x64 native tools command prompt for Visual Studio 2019.

- mkdir build
- cd build
- cmake .. -DVCPKG_TARGET_TRIPLET=x64-windows-static -DCMAKE_TOOLCHAIN_FILE=/vcpkg/scripts/buildsystems/vcpkg.cmake
- cmake --build . --config Release
  - Build debug: cmake --build . --config Debug

### Run

Open a command prompt as Administrator.

- See rpc.exe --help for details.
- Example
  - cd build\Release
  - rpc.exe --url wss://localhost:8080 --profile profile1
