#include <windows.h>
#include <stdlib.h>
#include <stdio.h>
#include <winioctl.h>
#include <string.h>
#include <crtdbg.h>
#include <assert.h>
#include <fltuser.h>
#include "../inc/common.h"
#include <dontuse.h>

#include <windows.h>
#include <strsafe.h>

void ErrorExit(LPTSTR lpszFunction) 
{ 
    // Retrieve the system error message for the last-error code


    LPVOID lpMsgBuf;
    LPVOID lpDisplayBuf;
    DWORD dw = GetLastError(); 

    FormatMessage(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | 
        FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL,
        dw,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPTSTR) &lpMsgBuf,
        0, NULL );

    // Display the error message and exit the process


    lpDisplayBuf = (LPVOID)LocalAlloc(LMEM_ZEROINIT, 
        (lstrlen((LPCTSTR)lpMsgBuf) + lstrlen((LPCTSTR)lpszFunction) + 40) * sizeof(TCHAR)); 
    StringCchPrintf((LPTSTR)lpDisplayBuf, 
        LocalSize(lpDisplayBuf) / sizeof(TCHAR),
        TEXT("%s failed with error %d: %s"), 
        lpszFunction, dw, lpMsgBuf); 
    //MessageBox(NULL, (LPCTSTR)lpDisplayBuf, TEXT("Error"), MB_OK); 
    printf("dw: %d\r\n", dw);
    printf("%s\r\n", (LPCTSTR)lpDisplayBuf);

    LocalFree(lpMsgBuf);
    LocalFree(lpDisplayBuf);
    ExitProcess(dw); 
}

VOID __cdecl
main(
        __in ULONG argc,
        __in_ecount(argc) PCHAR argv[]
    )
{
    HANDLE hDevice;
    BOOL bRc;
    ULONG bytesReturned;
    DWORD errNum = 0;
    TCHAR driverLocation[MAX_PATH];
    ULONG enable = 0;
    if (argc < 2)
    {
        printf("%s enable|disable\r\n", argv[0]);
        return;
    }
    if (strcmp(argv[1], "enable") == 0)
    {
        enable = 1;
    }
    if (strcmp(argv[1], "disable") == 0)
    {
        enable = 0;
    }
    printf("open the device\r\n");
    if ((hDevice = CreateFile( "\\\\.\\mou_filter",
                    0,
                    FILE_SHARE_READ | FILE_SHARE_WRITE,
                    NULL,
                    OPEN_EXISTING,
                    0,
                    NULL)) == INVALID_HANDLE_VALUE) {
        errNum = GetLastError();
        printf("errNum %d\r\n", errNum);
        if (errNum != ERROR_FILE_NOT_FOUND) {
            printf("CreateFile failed!  ERROR_FILE_NOT_FOUND = %d\n", errNum);
            return ;
        }
    }
    printf("hDevice %08x\r\n", hDevice);
    if (enable)
    {
        bRc = DeviceIoControl ( hDevice,
                (DWORD) IOCTL_FILTER_ENABLE,
                NULL, 0, NULL, 0, &bytesReturned, NULL);
    }
    else
    {
        bRc = DeviceIoControl ( hDevice,
                (DWORD) IOCTL_FILTER_DISABLE,
                NULL, 0, NULL, 0, &bytesReturned, NULL);
    }
    if ( !bRc )
    {
        printf ( "Error in DeviceIoControl : %d", GetLastError());
        ErrorExit("");
        return;
    }
    printf("finish");
    CloseHandle ( hDevice );
}
