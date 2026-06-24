# OsmiumOS
OsmiumOS is a scratch-built, 16-bit, command-line-interface operating system written in **NASM Assembly**, designed to operate free of dependencies.

## Features and Commands

### 📔 Features:
* **Completely interactive CLI interface with live backspacing, character replacement, and command history.**
### 📄 Commands:
* **"help"** - Displays command registry and descriptions of each command.  
* **"clear"** - Flushes the interface and resets the viewport.  
* **"greeting"** - Displays the system greeting and available memory.  
* **"mem"** - Prints the amount of memory available to the system in raw text.  
* **"echo [text]"** - Prints out the provided user input back to the console.  
* **"reboot"** - Reboots the system directly from the CLI

## 💾 Build instructions and dependencies:

### Prerequisites
To compile and run OsmiumOS, the following programs are needed
* **NASM** 
* **An x86 emulator program such as QEMU or Bochs** (Only if you wish to emulate)

### Compilation
 *If you are on Linux, there is a shell script written to make the compilation process easier, granted it requires QEMU*.

#### 1. First, compile the binary files using NASM:
- Bootloader: ``` nasm -f src/boot.asm -o build/boot.bin```
- Kernel: ```nasm -f src/kernel.asm -o build/kernel.bin```

#### 2. Generate the disk image
* **Linux, MacOS, or bash:**  
```cat build/boot.bin build/kernel.bin > build/os.img```  
  * *If you are planning to write this to physical media, you need to pad the file:*  
```dd if=/dev/zero of=build.img bs=512 seek=..., count=... conv=notrunc```  

* **Windows Powershell:**  
* ``` Get-Content build/boot.bin -Encoding Byte, Nothing | Set-Content build/os.img -Encoding Byte```
* ```Add-Content build/os.img (Get-Content build/kernel.bin -Encoding Byte) -Encoding Byte```  

* *If you are planning to write this to physical media, you need to pad the file in command prompt:*  
```fsutil file seteof build\os.img 1474560```


#### 3. Execution
You can run the compiled image universally using this QEMU command:  
```qemu-system-i386 -drive format=raw,file=build/os.img```
