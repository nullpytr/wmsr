#include <ntddk.h>
#include <wdmsec.h>

#include "wmsr.h"

DRIVER_INITIALIZE DriverEntry;
DRIVER_UNLOAD     MsrHandlerUnload;
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

    MSR_DPC_CONTEXT *context = (MSR_DPC_CONTEXT *)DeferredContext;
    __try {
        context->request.val.q = __readmsr(context->request.msr_no);
        context->status = STATUS_SUCCESS;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        context->status = GetExceptionCode();
    }

    KeSetEvent(&context->done, IO_NO_INCREMENT, FALSE);
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

    MSR_DPC_CONTEXT *context = (MSR_DPC_CONTEXT *)DeferredContext;
    __try {
        __writemsr(context->request.msr_no, context->request.val.q);
        context->status = STATUS_SUCCESS;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        context->status = GetExceptionCode();
    }

    KeSetEvent(&context->done, IO_NO_INCREMENT, FALSE);
}

NTSTATUS MsrStatusTerminate(
    PIRP     Irp,
    NTSTATUS status,
    ULONG    info
) {
    Irp->IoStatus.Status = status;
    Irp->IoStatus.Information = info;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
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
