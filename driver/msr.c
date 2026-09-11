#include <ntddk.h>
#include <wdmsec.h>

#define MSR_HPP_KERNEL_DRIVER_MODE
#include "msr.hpp"

DRIVER_INITIALIZE DriverEntry;
DRIVER_UNLOAD     DriverExit;
DRIVER_DISPATCH   MsrHandlerCreateClose;
DRIVER_DISPATCH   MsrHandlerDeviceControl;
KDEFERRED_ROUTINE MsrDpcReadRoutine;
KDEFERRED_ROUTINE MsrDpcWriteRoutine;

#pragma alloc_text(INIT, DriverEntry)
#pragma alloc_text(PAGE, MsrHandlerCreateClose)
#pragma alloc_text(PAGE, MsrHandlerDeviceControl)

typedef struct _MSR_DPC_CONTEXT {
    MSR_REQUEST request;
    NTSTATUS    status;
    KEVENT      done;
} MSR_DPC_CONTEXT, *PMSR_DPC_CONTEXT;

VOID MsrDpcReadRoutine(
    PKDPC  Dpc,
    PVOID  DeferredContext,
    PVOID  SystemArgument1,
    PVOID  SystemArgument2
) {
    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    PMSR_DPC_CONTEXT context = (PMSR_DPC_CONTEXT)DeferredContext;
    __try {
        context->request.val.q = __readmsr(context->request.msr_no);
        context->status = STATUS_SUCCESS;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        context->status = GetExceptionCode();
    }

    KeSetEvent(
        /* [in] Event     */ &context->done,
        /* [in] Increment */ IO_NO_INCREMENT,
        /* [in] Wait      */ FALSE
    );
}

VOID MsrDpcWriteRoutine(
    PKDPC  Dpc,
    PVOID  DeferredContext,
    PVOID  SystemArgument1,
    PVOID  SystemArgument2
) {
    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    PMSR_DPC_CONTEXT context = (PMSR_DPC_CONTEXT)DeferredContext;
    __try {
        __writemsr(context->request.msr_no, context->request.val.q);
        context->status = STATUS_SUCCESS;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        context->status = GetExceptionCode();
    }

    KeSetEvent(
        /* [in] Event     */ &context->done,
        /* [in] Increment */ IO_NO_INCREMENT,
        /* [in] Wait      */ FALSE
    );
}

VOID MsrDpcMakeContext(
    PMSR_DPC_CONTEXT context, 
    PMSR_REQUEST request
) {
    context->request = *request;
    context->status = STATUS_UNSUCCESSFUL;
}

VOID MsrDpcExecuteRoutineOnProc(
    PMSR_DPC_CONTEXT context, 
    PKDEFERRED_ROUTINE routine, 
    PROCESSOR_NUMBER *proc_number
) {
    KeInitializeEvent(
        /* [out] Event */ &context->done,
        /* [in ] Type  */ NotificationEvent,
        /* [in ] State */ FALSE
    );

    KDPC dpc;
    KeInitializeDpc(
        /* [out] Dpc             */ &dpc,
        /* [in ] DeferredRoutine */ routine,
        /* [in ] DeferredContext */ context
    );
    KeSetImportanceDpc(
        /* [in,out] Dpc        */ &dpc,
        /* [in    ] Importance */ HighImportance
    );
    KeSetTargetProcessorDpcEx(
        /* [in,out] Dpc        */ &dpc,
        /* [in    ] ProcNumber */ proc_number
    );

    BOOLEAN queued = KeInsertQueueDpc(
        /* [in,out] Dpc             */ &dpc,
        /* [in    ] SystemArgument1 */ NULL,
        /* [in    ] SystemArgument2 */ NULL
    );
    if (!queued) {
        context->status = STATUS_DRIVER_INTERNAL_ERROR;
        return;
    };
    
    KeWaitForSingleObject(
        /* [in] Object     */ &context->done,
        /* [in] WaitReason */ Executive,
        /* [in] WaitMode   */ KernelMode,
        /* [in] Alertable  */ FALSE,
        /* [in] Timeout    */ NULL
    );
}

BOOLEAN MsrIsValidCpu(
    MSR_CPU cpu
) { 
    return cpu < KeQueryActiveProcessorCountEx(ALL_PROCESSOR_GROUPS); 
}

NTSTATUS MsrStatusTerminate(
    PIRP     Irp,
    NTSTATUS status,
    ULONG    info
) {
    Irp->IoStatus.Status = status;
    Irp->IoStatus.Information = info;
    IoCompleteRequest(
        /* [in,out] Irp           */ Irp,
        /* [in    ] PriorityBoost */ IO_NO_INCREMENT
    );
    return status;
}

NTSTATUS MsrHandlerCreateClose(
    PDEVICE_OBJECT DeviceObject,
    PIRP           Irp
) {
    PAGED_CODE();
    UNREFERENCED_PARAMETER(DeviceObject);
    return MsrStatusTerminate(Irp, STATUS_SUCCESS, 0);
}

NTSTATUS MsrHandlerDeviceControl(
    PDEVICE_OBJECT DeviceObject,
    PIRP           Irp
) {
    PAGED_CODE();

    UNREFERENCED_PARAMETER(DeviceObject);

    PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(Irp);

    ULONG control_code = stack->Parameters.DeviceIoControl.IoControlCode;

    if (control_code != IOCTL_READ_MSR && control_code != IOCTL_WRITE_MSR)
        return MsrStatusTerminate(Irp, STATUS_INVALID_DEVICE_REQUEST, 0);

    if (stack->Parameters.DeviceIoControl.InputBufferLength < sizeof(MSR_REQUEST))
        return MsrStatusTerminate(Irp, STATUS_BUFFER_TOO_SMALL, 0);

    PMSR_REQUEST request = (PMSR_REQUEST)Irp->AssociatedIrp.SystemBuffer;

    if (!MsrIsValidCpu(request->cpu)) 
        return MsrStatusTerminate(Irp, STATUS_INVALID_PARAMETER, 0);

    PROCESSOR_NUMBER proc_number;
    NTSTATUS proc_status = KeGetProcessorNumberFromIndex(
        /* [in ] ProcIndex  */ request->cpu,
        /* [out] ProcNumber */ &proc_number
    );
    if (!NT_SUCCESS(proc_status)) 
        return MsrStatusTerminate(Irp, proc_status, 0);

    MSR_DPC_CONTEXT context;
    MsrDpcMakeContext(&context, request);

    if (control_code == IOCTL_WRITE_MSR) { // writes are simple
        MsrDpcExecuteRoutineOnProc(&context, MsrDpcWriteRoutine, &proc_number);
        return MsrStatusTerminate(Irp, context.status, 0);
    }

    // reads are a little more work

    if (stack->Parameters.DeviceIoControl.OutputBufferLength < sizeof(MSR_REQUEST)) 
        return MsrStatusTerminate(Irp, STATUS_BUFFER_TOO_SMALL, 0);

    MsrDpcExecuteRoutineOnProc(&context, MsrDpcReadRoutine, &proc_number);

    if (!NT_SUCCESS(context.status)) 
        return MsrStatusTerminate(Irp, context.status, 0);

    request->val = context.request.val;

    return MsrStatusTerminate(Irp, context.status, sizeof(MSR_REQUEST));
}

VOID DriverExit(
    PDRIVER_OBJECT DriverObject
) {
    UNICODE_STRING dos_name = RTL_CONSTANT_STRING(MSR_DOS_DEVICE_NAME);
    IoDeleteSymbolicLink(&dos_name);
    IoDeleteDevice(DriverObject->DeviceObject);
}

NTSTATUS DriverEntry(
    PDRIVER_OBJECT  DriverObject,
    PUNICODE_STRING RegistryPath
) {
    UNREFERENCED_PARAMETER(RegistryPath);

    static const GUID MSR_CLASS_GUID = {
        0x8D2432F0,
        0x85D8,
        0x4208,
        { 0xBF, 0x0E, 0x38, 0x77, 0xF5, 0x44, 0xD8, 0x23 }
    };

    UNICODE_STRING device_name = RTL_CONSTANT_STRING(MSR_NT_DEVICE_NAME);

    PDEVICE_OBJECT device_object;
    NTSTATUS device_status = WdmlibIoCreateDeviceSecure(
        /* [in ] DriverObject          */ DriverObject,
        /* [in ] DeviceExtensionSize   */ 0,
        /* [in ] DeviceName            */ &device_name,
        /* [in ] DeviceType            */ FILE_DEVICE_UNKNOWN,
        /* [in ] DeviceCharacteristics */ FILE_DEVICE_SECURE_OPEN,
        /* [in ] Exclusive             */ FALSE,
        /* [in ] DefaultSDDLString     */ &SDDL_DEVOBJ_SYS_ALL_ADM_ALL,
        /* [in ] DeviceClassGuid       */ &MSR_CLASS_GUID,
        /* [out] DeviceObject          */ &device_object
    );

    if (!NT_SUCCESS(device_status))
        return device_status;

    DriverObject->MajorFunction[IRP_MJ_CREATE]         = MsrHandlerCreateClose;
    DriverObject->MajorFunction[IRP_MJ_CLOSE]          = MsrHandlerCreateClose;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = MsrHandlerDeviceControl;
    DriverObject->DriverUnload                         = DriverExit;

    UNICODE_STRING dos_name = RTL_CONSTANT_STRING(MSR_DOS_DEVICE_NAME);

    NTSTATUS symlink_status = IoCreateSymbolicLink(
        /* [in] SymbolicLinkName */ &dos_name,
        /* [in] DeviceName       */ &device_name
    );
    if (!NT_SUCCESS(symlink_status)) {
        IoDeleteDevice(device_object);
        return symlink_status;
    }

    return STATUS_SUCCESS;
}
