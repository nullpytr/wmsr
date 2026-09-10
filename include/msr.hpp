#ifndef MSR_HPP
#define MSR_HPP

#ifdef MSR_HPP_KERNEL_DRIVER_MODE
#include <ntddk.h>
#else
#include <windows.h>
#endif

#define MSR_DEVICE_TYPE 40000
#define IOCTL_READ_MSR  CTL_CODE(MSR_DEVICE_TYPE, 0x800, METHOD_BUFFERED, FILE_READ_ACCESS)
#define IOCTL_WRITE_MSR CTL_CODE(MSR_DEVICE_TYPE, 0x801, METHOD_BUFFERED, FILE_WRITE_ACCESS)

#define MSR_NT_DEVICE_NAME      L"\\Device\\msr"
#define MSR_DOS_DEVICE_NAME     L"\\DosDevices\\msr"
#define MSR_WIN32_DEVICE_NAME   L"\\\\.\\msr"

typedef unsigned __int32 MSR_DOUBLE;
typedef unsigned __int64 MSR_QUAD;
typedef unsigned __int32 MSR_NO;
typedef unsigned __int32 MSR_CPU;

#pragma warning(push)
#pragma warning(disable: 4201)
typedef struct _MSR_VALUE {
    union {
        struct {
            MSR_DOUBLE l;
            MSR_DOUBLE h;
        };
        MSR_QUAD q;
    };
} MSR_VALUE;
#pragma warning(pop)

typedef struct _MSR_REQUEST {
    MSR_NO    msr_no;
    MSR_CPU   cpu;
    MSR_VALUE val;
} MSR_REQUEST, *PMSR_REQUEST;

/* -- C++20 Userspace API -- */
#ifndef MSR_HPP_KERNEL_DRIVER_MODE

#include <utility>
#include <cstdint>
#include <system_error>

namespace msr {

using u32 = std::uint32_t;
using u64 = std::uint64_t;

class device {
public:
    device() {
        m_handle = CreateFileW(
            /* [in] lpFileName            */ MSR_WIN32_DEVICE_NAME,
            /* [in] dwDesiredAccess       */ GENERIC_READ | GENERIC_WRITE,
            /* [in] dwShareMode           */ 0,
            /* [in] lpSecurityAttributes  */ NULL,
            /* [in] dwCreationDisposition */ OPEN_EXISTING,
            /* [in] dwFlagsAndAttributes  */ 0,
            /* [in] hTemplateFile         */ NULL)
        ;
            
        if (m_handle == INVALID_HANDLE_VALUE)
            error("Failed to open MSR device");
    }

    ~device() {
        if (m_handle != INVALID_HANDLE_VALUE)
            CloseHandle(m_handle);
    }

    device(device const&) = delete;
    device& operator=(device const&) = delete;

    device(device&& other) noexcept : m_handle(std::exchange(other.m_handle, INVALID_HANDLE_VALUE)) {}

    device& operator=(device&& other) noexcept {
        if (this != &other) {
            if (m_handle != INVALID_HANDLE_VALUE)
                CloseHandle(m_handle);
            m_handle = std::exchange(other.m_handle, INVALID_HANDLE_VALUE);
        }
        return *this;
    }

    u64 read(u32 const reg, u32 const cpu) const {
        MSR_REQUEST request { 
            .msr_no = reg, 
            .cpu = cpu, 
            .val = {} 
        };

        if (ioctl(IOCTL_READ_MSR, request)) 
            return request.val.q;
        
        error("IOCTL_READ_MSR failed");
    }

    void write(u32 const reg, u64 const value, u32 const cpu) const {
        MSR_REQUEST request { 
            .msr_no = reg, 
            .cpu = cpu, 
            .val = { .q = value } 
        };

        if (!ioctl(IOCTL_WRITE_MSR, request)) error("IOCTL_WRITE_MSR failed");
    }

private:
    /* Helpers */
    auto ioctl(u32 const control_code, MSR_REQUEST& request) const {
        [[maybe_unused]] DWORD bytes_returned;
        return DeviceIoControl(
            /* [in ] hDevice          */ m_handle,
            /* [in ] dwIoControlCode  */ control_code,
            /* [in ] lpInBuffer       */ &request,
            /* [in ] nInBufferSize    */ sizeof(request),
            /* [out] lpOutBuffer      */ &request,
            /* [in ] nOutBufferSize   */ sizeof(request),
            /* [out] lpBytesReturned  */ &bytes_returned,
            /* [in ] lpOverlapped     */ NULL
        );
    }

    [[noreturn]] void error(char const* message) const {
        throw std::system_error(GetLastError(), std::system_category(), message);
    }

    /* Members */
    HANDLE m_handle;
};

} // namespace msr

#endif // !MSR_HPP_KERNEL_DRIVER_MODE

#endif // MSR_HPP
