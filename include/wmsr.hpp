#ifndef WMSR_H
#define WMSR_H

#ifdef __cplusplus
#include <windows.h> // user space
#else
#include <ntddk.h> // kernel space
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

typedef struct _MSR_VALUE {
    union {
        struct {
            MSR_DOUBLE l;
            MSR_DOUBLE h;
        };
        MSR_QUAD q;
    };
} MSR_VALUE;

typedef struct _MSR_REQUEST {
    MSR_NO    msr_no;
    MSR_CPU   cpu;
    MSR_VALUE val;
} MSR_REQUEST, *PMSR_REQUEST;

/* -- C++20 Userspace API -- */
#ifdef __cplusplus
#ifndef WMSR_DEVICE_HPP
#define WMSR_DEVICE_HPP

#include <utility>
#include <cstdint>
#include <system_error>

namespace wmsr {

using u32 = std::uint32_t;
using u64 = std::uint64_t;

class device {
public:
    device() {
        m_handle = CreateFileW(
            /* lpFileName            */ MSR_WIN32_DEVICE_NAME,
            /* dwDesiredAccess       */ GENERIC_READ | GENERIC_WRITE,
            /* dwShareMode           */ 0,
            /* lpSecurityAttributes  */ NULL,
            /* dwCreationDisposition */ OPEN_EXISTING,
            /* dwFlagsAndAttributes  */ 0,
            /* hTemplateFile         */ NULL)
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
            /* hDevice          */ m_handle,
            /* dwIoControlCode  */ control_code,
            /* lpInBuffer       */ &request,
            /* nInBufferSize    */ sizeof(request),
            /* lpOutBuffer      */ &request,
            /* nOutBufferSize   */ sizeof(request),
            /* lpBytesReturned  */ &bytes_returned,
            /* lpOverlapped     */ NULL
        );
    }

    [[noreturn]] void error(char const* message) const {
        throw std::system_error(GetLastError(), std::system_category(), message);
    }

    /* Members */
    HANDLE m_handle;
};

} // namespace wmsr

#endif // WMSR_DEVICE_HPP
#endif // __cplusplus

#endif // WMSR_H
