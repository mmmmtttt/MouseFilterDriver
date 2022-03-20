/* 
   描述：过滤键盘驱动对象mouclass的所有设备对象 
 */  
#include <ntddk.h>  
#include <ntddkbd.h>  
// 外部变量声明  
extern POBJECT_TYPE *IoDriverObjectType;  
NTSTATUS new_AddDevice(
  struct _DRIVER_OBJECT *DriverObject,
  struct _DEVICE_OBJECT *PhysicalDeviceObject
);
PDRIVER_ADD_DEVICE g_old_adddevice = NULL;
PDRIVER_ADD_DEVICE g_new_adddevice = new_AddDevice;
PDRIVER_OBJECT g_driver_object = NULL;
ULONG g_enable_filter = 0;
#define CONTROL_DEVICE_OBJECT_NAME L"\\Device\\mou_filter"
#define CONTROL_DEVICE_OBJECT_LINK_NAME L"\\DosDevices\\Global\\mou_filter"
#include "../inc/common.h"
PDEVICE_OBJECT g_FilterControlDeviceObject = NULL;
NTSTATUS CreateControlDevice(  
        IN PDRIVER_OBJECT DriverObject  
        );
NTSTATUS DeleteControlDevice(  
        IN PDRIVER_OBJECT DriverObject  
        );
// 通过驱动对象名称取得驱动对象的引用(未文档化)  
NTSTATUS ObReferenceObjectByName(  
        IN PUNICODE_STRING ObjectName,  
        IN ULONG Attributes,  
        IN PACCESS_STATE AccessState,  
        IN ACCESS_MASK DesiredAccess,  
        IN POBJECT_TYPE ObjectType,  
        IN KPROCESSOR_MODE AccessMode,  
        IN PVOID ParseContext,  
        OUT PVOID *Object  
        );  
// 过滤设备扩展  
typedef struct _FILTER_EXT  
{  
    ULONG IsControlDevice;
    PDEVICE_OBJECT LowerDeviceObject;  
} FILTER_EXT, *PFILTER_EXT;  
// 全局计数  
//ULONG gKeyCount;  
// 驱动入口例程  
NTSTATUS DriverEntry(  
        IN PDRIVER_OBJECT DriverObject,  
        IN PUNICODE_STRING RegistryPath  
        );  
// 驱动卸载例程  
VOID DriverUnload(  
        IN PDRIVER_OBJECT DriverObject  
        );  
// IRP处理例程  
NTSTATUS Dispatch(  
        IN PDEVICE_OBJECT DeviceObject,  
        IN PIRP Irp  
        );  
// 挂载例程  
VOID Attach(  
        IN PDRIVER_OBJECT DriverObject  
        );  
VOID Detach(  
        IN PDRIVER_OBJECT DriverObject  
        );  
// Read完成例程  
NTSTATUS ReadCompletionRoutine(  
        IN PDEVICE_OBJECT DeviceObject,  
        IN PIRP Irp,  
        IN PVOID Context  
        );  
#ifdef ALLOC_PRAGMA  
#pragma alloc_text(INIT, DriverEntry)  
#pragma alloc_text(PAGE, DriverUnload)  
#pragma alloc_text(PAGE, Dispatch)  
#pragma alloc_text(INIT, Attach)  
#pragma alloc_text(PAGE, ReadCompletionRoutine)  
#endif  
/* 
   描述：驱动入口例程 
 */  
NTSTATUS DriverEntry(  
        IN PDRIVER_OBJECT DriverObject,  
        IN PUNICODE_STRING RegistryPath  
        )  
{  
    NTSTATUS status = STATUS_SUCCESS;  
    USHORT idx;  
    KdPrint(("DriverEntry invoke\n"));  
    g_driver_object = DriverObject;
    for (idx = 0; idx <= IRP_MJ_MAXIMUM_FUNCTION; ++idx) {  
        DriverObject->MajorFunction[idx] = Dispatch;  
    }  
#if DBG
    DriverObject->DriverUnload = DriverUnload;  
#endif
    //gKeyCount = 0;  
    Attach(DriverObject);  
    CreateControlDevice(DriverObject);
    return status;  
}  
/* 
   描述：驱动卸载例程 
 */  
VOID DriverUnload(  
        IN PDRIVER_OBJECT DriverObject  
        )  
{  
    LARGE_INTEGER interval;  
    PDEVICE_OBJECT curDeviceObject;  
    KdPrint(("DriverUnload invoke\n"));  
    DeleteControlDevice(DriverObject);
    //还原AddDevice
    Detach(DriverObject);
    // 降低当前线程的优先级，避免延时对系统的影响  
    KeSetPriorityThread(KeGetCurrentThread(), LOW_REALTIME_PRIORITY);  
    curDeviceObject = DriverObject->DeviceObject;  
    while (curDeviceObject != NULL) {  
        IoDetachDevice(((PFILTER_EXT)curDeviceObject->DeviceExtension)->LowerDeviceObject);  
        IoDeleteDevice(curDeviceObject);  
        curDeviceObject = curDeviceObject->NextDevice;  
    }  
    interval.QuadPart = (-1) * 100 * 1000;  
/*
    while (gKeyCount > 0) {  
        KeDelayExecutionThread(KernelMode, FALSE, &interval);  
    }  
*/
    g_driver_object = NULL;
    KdPrint(("DriverUnload ok\n"));  
}  
NTSTATUS DispatchIrpDeviceControl(  
        IN PDEVICE_OBJECT DeviceObject,  
        IN PIRP Irp  
        )  
{
    NTSTATUS status = STATUS_SUCCESS;
    PIO_STACK_LOCATION irpsp = IoGetCurrentIrpStackLocation(Irp);  
    switch (irpsp->Parameters.DeviceIoControl.IoControlCode)
    {
        case IOCTL_FILTER_ENABLE:
            g_enable_filter = 0;
            break;
        case IOCTL_FILTER_DISABLE:
            g_enable_filter = 1;
            break;
        default:
            status = STATUS_INVALID_PARAMETER;
    }
    KdPrint(("set g_enable_filter: %d\r\n", g_enable_filter));
    Irp->IoStatus.Status = status;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return status;
}
NTSTATUS DispatchControlDevice(  
        IN PDEVICE_OBJECT DeviceObject,  
        IN PIRP Irp  
        )  
{
    NTSTATUS status;
    PIO_STACK_LOCATION irpsp = IoGetCurrentIrpStackLocation(Irp);  
    KdPrint(("DispatchControlDevice\r\n"));
    if (irpsp->MajorFunction == IRP_MJ_DEVICE_CONTROL)
    {
        return DispatchIrpDeviceControl(DeviceObject, Irp);
    }
    status = STATUS_SUCCESS;
    Irp->IoStatus.Status = status;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return status;
}
/* 
   描述：IRP处理例程 
 */  
NTSTATUS Dispatch(  
        IN PDEVICE_OBJECT DeviceObject,  
        IN PIRP Irp  
        )  
{  
    PDEVICE_OBJECT lowerDeviceObject = ((PFILTER_EXT)DeviceObject->DeviceExtension)->LowerDeviceObject;  
    PIO_STACK_LOCATION irpsp = IoGetCurrentIrpStackLocation(Irp);  

    if (((PFILTER_EXT)DeviceObject->DeviceExtension)->IsControlDevice)
    {
        return DispatchControlDevice(DeviceObject, Irp);
    }
    switch (irpsp->MajorFunction) {  
        case IRP_MJ_POWER:  
            {  
                KdPrint(("IRP_MJ_POWER\n"));  
                PoStartNextPowerIrp(Irp);  
                IoSkipCurrentIrpStackLocation(Irp);  
                return PoCallDriver(lowerDeviceObject, Irp);  
                break;  
            }  
        case IRP_MJ_PNP:  
            {  
                KdPrint(("IRP_MJ_PNP\n"));  
                switch (irpsp->MinorFunction) {  
                    case IRP_MN_REMOVE_DEVICE:  
                        {  
                            KdPrint(("IRP_MN_REMOVE_DEVICE\n"));  
                            IoDetachDevice(lowerDeviceObject);  
                            IoDeleteDevice(DeviceObject);  
                            IoSkipCurrentIrpStackLocation(Irp);  
                            return IoCallDriver(lowerDeviceObject, Irp);  
                        }  
                    default:  
                        {  
                            KdPrint(("IRP_MJ_PNP -> Unknown MinorFunction : %x\n", irpsp->MinorFunction));  
                            IoSkipCurrentIrpStackLocation(Irp);  
                            return IoCallDriver(lowerDeviceObject, Irp);  
                        }  
                }  
            }  
        case IRP_MJ_READ:  
            {  
                //KdPrint(("IRP_MJ_READ\n"));  
                //gKeyCount++;  
                IoCopyCurrentIrpStackLocationToNext(Irp);  
                IoSetCompletionRoutine(Irp, ReadCompletionRoutine, DeviceObject, TRUE, TRUE, TRUE);  
                return IoCallDriver(lowerDeviceObject, Irp);  
            }  
        default:  
            {  
                KdPrint(("Unknown IRP : %x\n", irpsp->MajorFunction));  
                IoSkipCurrentIrpStackLocation(Irp);  
                return IoCallDriver(lowerDeviceObject, Irp);  
            }  
    }  
}  
/* 
   描述：挂载例程 
 */  
VOID Attach(  
        IN PDRIVER_OBJECT DriverObject  
        )  
{  
    NTSTATUS status;  
    PDRIVER_OBJECT targetDriverObject;  
    PDEVICE_OBJECT curDeviceObject;  
    PDEVICE_OBJECT lowerDeviceObject;  
    PDEVICE_OBJECT filterDeviceObject;  
    UNICODE_STRING kbdClassName;  
    KdPrint(("Attach invoke\n"));  
    RtlInitUnicodeString(&kbdClassName, L"\\Driver\\mouclass");  
    status = ObReferenceObjectByName(&kbdClassName, OBJ_CASE_INSENSITIVE, NULL, 0,   
            *IoDriverObjectType, KernelMode, NULL, &targetDriverObject);  
    if (!NT_SUCCESS(status)) {  
        KdPrint(("ObReferenceObjectByName failed\n"));  
        KdPrint(("status %08x\n", status));  
        return ;  
    }  
    g_old_adddevice = targetDriverObject->DriverExtension->AddDevice;
    if (g_new_adddevice)
    {
        KdPrint(("use new adddevice\r\n"));
        targetDriverObject->DriverExtension->AddDevice = g_new_adddevice;
    }
    ObDereferenceObject(targetDriverObject);  
    curDeviceObject = targetDriverObject->DeviceObject;  
    while (curDeviceObject != NULL) {  
        status = IoCreateDevice(DriverObject, sizeof(FILTER_EXT), NULL, curDeviceObject->DeviceType,   
                curDeviceObject->Characteristics, FALSE, &filterDeviceObject);  
        if (!NT_SUCCESS(status)) {  
            KdPrint(("IoCreateDevice failed\n"));  
        } else {  
            lowerDeviceObject = IoAttachDeviceToDeviceStack(filterDeviceObject, curDeviceObject);  
            if (lowerDeviceObject == NULL) {  
                KdPrint(("IoAttachDeviceToDeviceStack failed\n"));  
                IoDeleteDevice(filterDeviceObject);  
            } else {  
                ((PFILTER_EXT)filterDeviceObject->DeviceExtension)->LowerDeviceObject = lowerDeviceObject;  
                filterDeviceObject->Flags |=   
                    lowerDeviceObject->Flags & (DO_BUFFERED_IO | DO_DIRECT_IO | DO_POWER_PAGABLE);  
                filterDeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;  
            }  
        }  
        curDeviceObject = curDeviceObject->NextDevice;  
    }  
}  
/* 
   描述：Read完成例程 
 */  
NTSTATUS ReadCompletionRoutine(  
        IN PDEVICE_OBJECT DeviceObject,  
        IN PIRP Irp,  
        IN PVOID Context  
        )  
{  
    //KdPrint(("ReadCompletionRoutine invoke\n"));  
/*
    if (NT_SUCCESS(Irp->IoStatus.Status)) {  
        ULONG len, idx;  
        PUCHAR buf;  
        PKEYBOARD_INPUT_DATA inputData;  
        len = Irp->IoStatus.Information;  
        buf = (PUCHAR)Irp->AssociatedIrp.SystemBuffer;  
        for (idx = 0; idx < len; idx += sizeof(KEYBOARD_INPUT_DATA)) {  
            buf += idx;  
            inputData = (PKEYBOARD_INPUT_DATA)buf;  
            KdPrint(("ScanCode : %x  %s\n", inputData->MakeCode, inputData->Flags?"Up" : "Down"));  
        }  
    }  
*/
    //屏蔽
    if (1 == g_enable_filter)
    {
        Irp->IoStatus.Information = 0;
    }
    //gKeyCount--;  
    if (Irp->PendingReturned) {  
        IoMarkIrpPending(Irp);  
    }  
    return Irp->IoStatus.Status;  
}  
NTSTATUS new_AddDevice(
  struct _DRIVER_OBJECT *DriverObject,
  struct _DEVICE_OBJECT *PhysicalDeviceObject
)
{
    NTSTATUS status;
    NTSTATUS status2 = STATUS_SUCCESS;
    PDEVICE_OBJECT curDeviceObject;  
    PDEVICE_OBJECT lowerDeviceObject;  
    PDEVICE_OBJECT filterDeviceObject;  
    KdPrint(("new_AddDevice driver %08x device %08x\r\n", DriverObject, PhysicalDeviceObject));
    status = g_old_adddevice(DriverObject, PhysicalDeviceObject);
    if (NT_SUCCESS(status))
    {
        status2 = status;
        curDeviceObject = PhysicalDeviceObject;
        if (g_driver_object)
        {
            status = IoCreateDevice(g_driver_object, sizeof(FILTER_EXT), NULL, curDeviceObject->DeviceType,   
                    curDeviceObject->Characteristics, FALSE, &filterDeviceObject);  
            if (!NT_SUCCESS(status)) {  
                KdPrint(("IoCreateDevice failed\n"));  
            } else {  
                lowerDeviceObject = IoAttachDeviceToDeviceStack(filterDeviceObject, curDeviceObject);  
                if (lowerDeviceObject == NULL) {  
                    KdPrint(("IoAttachDeviceToDeviceStack failed\n"));  
                    IoDeleteDevice(filterDeviceObject);  
                } else {  
                    ((PFILTER_EXT)filterDeviceObject->DeviceExtension)->LowerDeviceObject = lowerDeviceObject;  
                    filterDeviceObject->Flags |=   
                        lowerDeviceObject->Flags & (DO_BUFFERED_IO | DO_DIRECT_IO | DO_POWER_PAGABLE);  
                    filterDeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;  
                }  
            }  
        }
    }
    return status2;
}
VOID Detach(  
        IN PDRIVER_OBJECT DriverObject  
        )  
{  
    NTSTATUS status;  
    PDRIVER_OBJECT targetDriverObject;  
    PDEVICE_OBJECT curDeviceObject;  
    PDEVICE_OBJECT lowerDeviceObject;  
    PDEVICE_OBJECT filterDeviceObject;  
    UNICODE_STRING kbdClassName;  
    KdPrint(("Attach invoke\n"));  
    RtlInitUnicodeString(&kbdClassName, L"\\Driver\\mouclass");  
    status = ObReferenceObjectByName(&kbdClassName, OBJ_CASE_INSENSITIVE, NULL, 0,   
            *IoDriverObjectType, KernelMode, NULL, &targetDriverObject);  
    if (!NT_SUCCESS(status)) {  
        KdPrint(("ObReferenceObjectByName failed\n"));  
        KdPrint(("status %08x\n", status));  
        return ;  
    }  
    targetDriverObject->DriverExtension->AddDevice = g_old_adddevice;
    ObDereferenceObject(targetDriverObject);  
}
NTSTATUS CreateControlDevice(  
        IN PDRIVER_OBJECT DriverObject  
        )  
{
    NTSTATUS status;
    UNICODE_STRING nameString;
    ULONG i;
    RtlInitUnicodeString( &nameString, CONTROL_DEVICE_OBJECT_NAME );
    status = IoCreateDevice( DriverObject,
                             sizeof(FILTER_EXT),
                             &nameString,
                             FILE_DEVICE_UNKNOWN,
                             FILE_DEVICE_SECURE_OPEN,
                             FALSE,
                             &g_FilterControlDeviceObject);

    if ( !NT_SUCCESS( status ) ) {
        return status;
    }
    ((PFILTER_EXT)g_FilterControlDeviceObject->DeviceExtension)->IsControlDevice = 1;
    KdPrint(("CreateDevice %08x %wZ\r\n", status, &nameString));
    {
        UNICODE_STRING linkname;
        RtlInitUnicodeString(&linkname, CONTROL_DEVICE_OBJECT_LINK_NAME);

        status = IoCreateSymbolicLink(&linkname, &nameString);
        if ( !NT_SUCCESS( status ) ) {
            IoDeleteDevice(g_FilterControlDeviceObject);
            return status;
        }
        KdPrint(("CreateSymbolicLink %08x %wZ\r\n", status, &linkname));
    }
    g_FilterControlDeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;
    return status;
}
NTSTATUS DeleteControlDevice(  
        IN PDRIVER_OBJECT DriverObject  
        )  
{
    NTSTATUS status = STATUS_SUCCESS;
    if (g_FilterControlDeviceObject)
    {
        {
            UNICODE_STRING linkname;
            RtlInitUnicodeString(&linkname, CONTROL_DEVICE_OBJECT_LINK_NAME);
            IoDeleteSymbolicLink(&linkname);
        }
        IoDeleteDevice(g_FilterControlDeviceObject);
    }
    return status;
}
