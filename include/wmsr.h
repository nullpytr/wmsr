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

#endif // WMSR_H
