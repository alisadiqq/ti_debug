# Introduction 
Hello. This little project is based TI's Simple Link SDK. The MCU / board being used here is CC2340R53 Launchpad.
This project has a build.sh and CMakeLists.txt. It also has VS Code meta deta.
VSCode will use build.sh to build a firmware image from CMakeLists.txt

# Getting Started
In order to clone this branch and execute on your system, please install
1.	Simplelink SDK 8.40.02.01
2.	VS Code (Any version)

3.	CCS 20.1.1 - This will install the TI Compiler, Uni flash and all other versions accordingly
4.	CMake any version above 3.26.5 Windows x86_64
5. 	gcc-arm-none-eabi-10.3-2021.10
6. 	TI Embedded Debug VS Code Extension
7.	make
8.	ninja

Lastly, Copy paste the binary folders into C:/Simple_link_toolchain:
1.	cmake-3.26.5-windows-x86_64	- Copy paste the full folder from installation directory
2.	gcc-arm-none-eabi-10.3-2021.10	- Copy paste the full folder from installation directory
3.	uniflash_9.1.0			- Copy paste full folder from TI Installation directory, normally in C:
4.	make
5.	ninja
6.	ti-embedded-debug		- VS Code extension full folder, normally will be found in Texas Instruments App Data folder
7. 	ti_debugserver_bin		- Copy paste full folder from TI Installation directory, normally in C:
8.	ti-cgt-armllvm_4.0.2.LTS	- Copy paste full folder from TI Installation directory, normally in C:

# Build and Test
1.	Choose your compiler - Simply open CmakeLists.txt and select your compiler by setting the COMPILER_CHOICE variable to either "GCC" or "TI" on line 5.
2. 	Open VSCode
3.	Terminal -> Clean TI or Build basic_ble
4.	To attempt debugging, go to debug window and select Debug (TI XDS via Open OCD)

# Contribute
TODO: Explain how other users and developers can contribute to make your code better. 

If you want to learn more about creating good readme files then refer the following [guidelines](https://docs.microsoft.com/en-us/azure/devops/repos/git/create-a-readme?view=azure-devops). You can also seek inspiration from the below readme files:
- [ASP.NET Core](https://github.com/aspnet/Home)
- [Visual Studio Code](https://github.com/Microsoft/vscode)
- [Chakra Core](https://github.com/Microsoft/ChakraCore)