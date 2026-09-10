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
    KeInitializeEvent(&context->done, NotificationEvent, FALSE);

    KDPC dpc;
    KeInitializeDpc(&dpc, routine, context);
    KeSetTargetProcessorDpcEx(&dpc, proc_number);

    KeInsertQueueDpc(&dpc, NULL, NULL);
    KeWaitForSingleObject(&context->done, Executive, KernelMode, FALSE, NULL);
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

NTSTATUS MsrHandlerDeviceControl(
    PDEVICE_OBJECT DeviceObject,
    PIRP           Irp
) {
    PAGED_CODE();

    UNREFERENCED_PARAMETER(DeviceObject);

    PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(Irp);

    if (stack->Parameters.DeviceIoControl.InputBufferLength < sizeof(MSR_REQUEST))
        return MsrStatusTerminate(Irp, STATUS_BUFFER_TOO_SMALL, 0);

    ULONG control_code = stack->Parameters.DeviceIoControl.IoControlCode;

    if (control_code != IOCTL_READ_MSR && control_code != IOCTL_WRITE_MSR)
        return MsrStatusTerminate(Irp, STATUS_INVALID_DEVICE_REQUEST, 0);

    PMSR_REQUEST request = (PMSR_REQUEST)Irp->AssociatedIrp.SystemBuffer;

    if (!MsrIsValidCpu(request->cpu)) 
        return MsrStatusTerminate(Irp, STATUS_INVALID_PARAMETER, 0);

    PROCESSOR_NUMBER proc_number;
    NTSTATUS proc_status = KeGetProcessorNumberFromIndex(request->cpu, &proc_number);
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