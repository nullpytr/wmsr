# wmsr

A minimal Windows kernel driver that exposes x86 **Model-Specific Register (MSR)** read/write to userspace via IOCTLs, paired with a lightweight header-only C++20 userspace library. 

CPUs with >64 cores are handled properly, and the device `\\.\msr` is accessible to **SYSTEM** and **Administrators** only.  

Inspired by the Linux `msr` kernel module (`arch/x86/kernel/msr.c`).

## Usage

### Loading the `msr.sys` driver

> **Note:** Test-signed drivers require test signing mode to be enabled. This is a one-time step that requires a reboot.

```cmd
:: Enable test signing (one-time, requires reboot)
bcdedit /set testsigning on

:: Create and start the driver service
sc create msr type= kernel binPath= C:\full\path\to\msr.sys
sc start msr
```

### Unloading the `msr.sys` driver

```cmd
sc stop msr
sc delete msr

:: Optionally re-disable test signing
bcdedit /set testsigning off
```

## Userspace API

Requires C++20. Drop in `wmsr.hpp` to your project's include path, and simply: 

```cpp
#include <wmsr.hpp>
```

### Example

```cpp
int main() {
    try {
        wmsr::device dev; // opens \\.\msr; throws std::system_error on failure

        // Read MSR 0x1A2 (MSR_TEMPERATURE_TARGET) on logical CPU 0
        uint64_t value = dev.read(0x1A2, 0);

        // Write a value to an MSR on logical CPU 0
        dev.write(0x1A2, 0x3640000, 0);

    } catch (std::exception const& error) {
        std::cerr << "Error: " << error.what();
    }
}
```

## IOCTL interface

The driver exposes two IOCTLs over the `\\.\msr` device using **buffered I/O**.

| IOCTL | Code | Direction |
|-------|------|-----------|
| `IOCTL_READ_MSR` | `CTL_CODE(40000, 0x800, METHOD_BUFFERED, FILE_READ_ACCESS)` | In/Out |
| `IOCTL_WRITE_MSR` | `CTL_CODE(40000, 0x801, METHOD_BUFFERED, FILE_WRITE_ACCESS)` | In |

Both IOCTLs use `MSR_REQUEST` as the input (and output for reads) buffer:

```c
typedef struct _MSR_REQUEST {
    MSR_NO    msr_no;   // MSR register number (32-bit)
    MSR_CPU   cpu;      // Flat system-wide processor index (32-bit)
    MSR_VALUE val;      // Value: input for write, output for read (64-bit)
} MSR_REQUEST;
```


## Build `msr.sys`

Use the provided build script (Release x64 by default):

```cmd
scripts\build <configuration, optional> <platform, optional>
```

Or, build directly with MSBuild:

```cmd
msbuild driver\msr.vcxproj /p:Configuration=Release /p:Platform=x64
```

The driver is output at `driver\<platform>\<configuration>\msr.sys`


### Requirements

- Windows 10 x64 (or later)
- **Visual Studio** with the C++ desktop & WDK workload
- **Windows Development SDK** matching the installed VS version
- **Windows Driver Kit (WDK)** matching the installed SDK version
