#ifndef WMSR_H
#define WMSR_H

#ifndef CTL_CODE // no ntddk.h or windows.h

#define METHOD_BUFFERED 0
#define FILE_READ_ACCESS 0x0001
#define FILE_WRITE_ACCESS 0x0002
#define CTL_CODE(DeviceType, Function, Method, Access) \
    (((DeviceType) << 16) | ((Access) << 14) | ((Function) << 2) | (Method))
    
#endif

#define MSR_DEVICE_TYPE 40000
#define IOCTL_READ_MSR  CTL_CODE(MSR_DEVICE_TYPE, 0x800, METHOD_BUFFERED, FILE_READ_ACCESS)
#define IOCTL_WRITE_MSR CTL_CODE(MSR_DEVICE_TYPE, 0x801, METHOD_BUFFERED, FILE_WRITE_ACCESS)

#define MSR_NT_DEVICE_NAME      L"\\Device\\msr"
#define MSR_DOS_DEVICE_NAME     L"\\DosDevices\\msr"
#define MSR_WIN32_DEVICE_NAME   L"\\\\.\\msr"

typedef struct _MSR_VALUE {
    union {
        struct {
            unsigned __int32 l;
            unsigned __int32 h;
        };
        unsigned __int64 q;
    };
} MSR_VALUE;

typedef struct _MSR_REQUEST {
    unsigned __int32 msr_no;
    unsigned __int32 cpu;
    MSR_VALUE        val;
} MSR_REQUEST, *PMSR_REQUEST;

#endif // WMSR_H