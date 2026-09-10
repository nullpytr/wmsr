#include <ntddk.h>
#include <wdmsec.h>

#include "wmsr.h"

DRIVER_INITIALIZE DriverEntry;
DRIVER_UNLOAD     MsrUnload;
DRIVER_DISPATCH   MsrCreateClose;
DRIVER_DISPATCH   MsrDeviceControl;
KDEFERRED_ROUTINE MsrReadDpc;
KDEFERRED_ROUTINE MsrWriteDpc;

#pragma alloc_text(INIT, DriverEntry)
#pragma alloc_text(PAGE, MsrCreateClose)
#pragma alloc_text(PAGE, MsrDeviceControl)

typedef struct _MSR_DPC_CONTEXT {
    MSR_REQUEST req;
    NTSTATUS    status;
    KEVENT      done;
} MSR_DPC_CONTEXT;

VOID MsrReadDpc(
    PKDPC  Dpc,
    PVOID  DeferredContext,
    PVOID  SystemArgument1,
    PVOID  SystemArgument2
) {
    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    MSR_DPC_CONTEXT *ctx = (MSR_DPC_CONTEXT *)DeferredContext;
    __try {
        ctx->req.val.q = __readmsr(ctx->req.msr_no);
        ctx->status = STATUS_SUCCESS;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ctx->status = GetExceptionCode();
    }

    KeSetEvent(&ctx->done, IO_NO_INCREMENT, FALSE);
}

VOID MsrWriteDpc(
    PKDPC  Dpc,
    PVOID  DeferredContext,
    PVOID  SystemArgument1,
    PVOID  SystemArgument2
) {
    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    MSR_DPC_CONTEXT *ctx = (MSR_DPC_CONTEXT *)DeferredContext;
    __try {
        __writemsr(ctx->req.msr_no, ctx->req.val.q);
        ctx->status = STATUS_SUCCESS;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ctx->status = GetExceptionCode();
    }

    KeSetEvent(&ctx->done, IO_NO_INCREMENT, FALSE);
}

NTSTATUS MsrCreateClose(
    PDEVICE_OBJECT DeviceObject,
    PIRP           Irp
) {
    PAGED_CODE();

    UNREFERENCED_PARAMETER(DeviceObject);

    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = NULL;
    
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}
