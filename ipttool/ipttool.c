#include <Windows.h>
#include <stdio.h>
#include <libipt.h>

typedef enum _IPT_TL_ACTION
{
    IptTlStartTrace,
    IptTlStopTrace,
    IptTlGetTrace
} IPT_TL_ACTION;

FORCEINLINE
DWORD
ConvertToPASizeToSizeOption (
    _In_ DWORD dwSize
    )
{
    DWORD dwIndex;

    //
    // Cap the size to 128MB. Sizes below 4KB will result in 0 anyway.
    //
    if (dwSize > (128 * 1024 * 1024))
    {
        dwSize = 128 * 1024 * 1024;
    }

    //
    // Find the nearest power of two that's set (align down)
    //
    BitScanReverse(&dwIndex, dwSize);

    //
    // The value starts at 4KB
    //
    dwIndex -= 12;
    return dwIndex;
}

BOOL
EnableIpt (
    VOID
    )
{
    SC_HANDLE hScm, hSc;
    BOOL bRes;
    bRes = FALSE;

    //
    // Open a handle to the SCM
    //
    hScm = OpenSCManager(NULL, NULL, SC_MANAGER_CONNECT);
    if (hScm != NULL)
    {
        //
        // Open a handle to the IPT Service
        //
        hSc = OpenService(hScm, L"Ipt", SERVICE_START);
        if (hSc != NULL)
        {
            //
            // Start it
            //
            bRes = StartService(hSc, 0, NULL);
            if ((bRes == FALSE) &&
                (GetLastError() == ERROR_SERVICE_ALREADY_RUNNING))
            {
                //
                // If it's already started, that's OK
                //
                bRes = TRUE;
            }
            else
            {
                wprintf(L"[-] Unable to start Intel PT Service (err=%d)\n",
                        GetLastError());
            }

            //
            // Done with the service
            //
            CloseServiceHandle(hSc);
        }
        else
        {
            wprintf(L"[-] Unable to open Intel PT Service (err=%d). "
                    L"Are you running Windows 10 1809?\n",
                    GetLastError());
        }

        //
        // Done with the SCM
        //
        CloseServiceHandle(hScm);
    }
    else
    {
        wprintf(L"[-] Unable to open a handle to the Service Control Manager (err=%d)\n",
                GetLastError());
    }

    //
    // Return the result
    //
    return bRes;
}

BOOL
EnableAndValidateIptServices (
    VOID
    )
{
    WORD wTraceVersion;
    DWORD dwBufferVersion;
    BOOL bRes;

    //
    // First enable IPT
    //
    bRes = EnableIpt();
    if (bRes == FALSE)
    {
        wprintf(L"[-] Intel PT Service could not be started!\n");
        goto Cleanup;
    }

    //
    // Next, check if the driver uses a dialect we understand
    //
    bRes = GetIptBufferVersion(&dwBufferVersion);
    if (bRes == FALSE)
    {
        wprintf(L"[-] Can't even understand how to communicate to Intel PT Service: (err=%d)\n",
                GetLastError());
        goto Cleanup;
    }
    if (dwBufferVersion != IPT_BUFFER_MAJOR_VERSION_CURRENT)
    {
        wprintf(L"[-] Intel PT Service speaks a dialect we don't understand: %d\n",
                dwBufferVersion);
        goto Cleanup;
    }

    //
    // Then, check if the driver uses trace versions we speak
    //
    bRes = GetIptTraceVersion(&wTraceVersion);
    if (bRes == FALSE)
    {
        wprintf(L"[-] Failed to request Trace Version from Intel PT Service (err=%d)\n",
                GetLastError());
        goto Cleanup;
    }
    if (wTraceVersion != IPT_TRACE_VERSION_CURRENT)
    {
        wprintf(L"[-] Intel PT Service returns traces we don't understand: %d\n",
                wTraceVersion);
        goto Cleanup;
    }

Cleanup:
    //
    // Return result
    //
    return bRes;
}

BOOL
ConfigureTraceFlags (
    _In_ PWCHAR pwszFlags,
    _Inout_ PIPT_OPTIONS pOptions
    )
{
    DWORD dwFlags;
    BOOL bRes;
    bRes = FALSE;

    //
    // Read the flags now
    //
    dwFlags = wcstoul(pwszFlags, NULL, 10);
    if (dwFlags != 0)
    {
        wprintf(L"[-] Invalid flags: %s\n", pwszFlags);
        goto Cleanup;
    }

    //
    // In the future, flags will allow configuring these settings
    //
    pOptions->MatchSettings = IptMatchByAnyApp;
    pOptions->ModeSettings = IptCtlUserModeOnly;
    pOptions->TimingSettings = IptNoTimingPackets;
    bRes = TRUE;
    wprintf(L"[+] Tracing Options:\n"
            L"           Match by: %s\n"
            L"         Trace mode: %s\n"
            L"    Timing patckets: %s\n",
            L"Any process",
            L"User-mode only",
            L"None");

Cleanup:
    //
    // Return result
    //
    return bRes;
}

BOOL
ConfigureBufferSize (
    _In_ PWCHAR pwszSize,
    _Inout_ PIPT_OPTIONS pOptions
    )
{
    DWORD dwSize;
    BOOL bRes;
    bRes = FALSE;

    //
    // Get the buffer size
    //
    dwSize = wcstoul(pwszSize, NULL, 10);
    if (dwSize == 0)
    {
        wprintf(L"[-] Invalid size: %s\n", pwszSize);
        goto Cleanup;
    }

    //
    // Warn the user about incorrect values
    //
    if (!((dwSize) && ((dwSize & (~dwSize + 1)) == dwSize)))
    {
        wprintf(L"[*] Size will be aligned to a power of 2\n");
    }
    else if (dwSize < 4096)
    {
        wprintf(L"[*] Size will be set to minimum of 4KB\n");
    }
    else if (dwSize > (128 * 1024 * 1024))
    {
        wprintf(L"[*] Size will be set to a maximum of 128MB\n");
    }

    //
    // Compute the size option
    //
    pOptions->TopaPagesPow2 = ConvertToPASizeToSizeOption(dwSize);
    bRes = TRUE;
    wprintf(L"[+] Using size: %d bytes\n", 1 << (pOptions->TopaPagesPow2 + 12));

Cleanup:
    //
    // Return result
    //
    return bRes;
}

BOOL
ConfigureProcess (
    _In_ PWCHAR pwszPid,
    _Out_ PHANDLE phProcess
    )
{
    DWORD dwPid;
    BOOL bRes;
    bRes = FALSE;
    *phProcess = NULL;

    //
    // Get the PID first
    //
    dwPid = wcstoul(pwszPid, NULL, 10);
    if (dwPid == 0)
    {
        wprintf(L"[-] Invalid PID: %s\n", pwszPid);
        goto Cleanup;
    }

    //
    // Open a handle to it
    //
    *phProcess = OpenProcess(PROCESS_VM_READ, FALSE, dwPid);
    if (*phProcess == NULL)
    {
        wprintf(L"[-] Unable to open PID %d (err=%d)\n", dwPid, GetLastError());
        goto Cleanup;
    }
    bRes = TRUE;

Cleanup:
    //
    // Return result
    //
    return bRes;
}

INT
wmain (
    _In_ DWORD dwArgumentCount,
    _In_ PWCHAR pwszArguments[]
    )
{
    BOOL bRes;
    DWORD dwTraceSize;
    HANDLE hProcess;
    HANDLE hTraceFile;
    DWORD dwResult;
    IPT_TL_ACTION dwAction;
    IPT_OPTIONS options;
    PIPT_TRACE_DATA pTraceData;

    //
    // Setup cleanup path
    //
    hTraceFile = INVALID_HANDLE_VALUE;
    hProcess = NULL;
    pTraceData = NULL;
    dwResult = 0xFFFFFFFF;
    options.AsULonglong = 0;

    //
    // Shameless banner header
    //
    wprintf(L"/-----------------------------------------\\\n");
    wprintf(L"|=== Windows 10 RS5 1809 IPT Test Tool ===|\n");
    wprintf(L"|===  Copyright (c) 2018 Alex Ionescu  ===|\n");
    wprintf(L"|===    http://github.com/ionescu007   ===|\n");
    wprintf(L"|===  http://www.windows-internals.com ===|\n");
    wprintf(L"\\-----------------------------------------/\n");
    wprintf(L"\n");

    //
    // Print banner if invalid/no arguments
    //
    if (dwArgumentCount <= 1)
    {
Banner:
        wprintf(L"Usage: IptTool.exe [action] <PID> <Flags>\n");
        wprintf(L"       --start <PID> <Size> <Flags>     "
                L"Starts Intel PT tracing for the given PID\n");
        wprintf(L"                                        "
                L"Size is the trace buffer size (4KB to 128MB, in bytes)\n");
        wprintf(L"                                        "
                L"No flags currently defined\n");
        wprintf(L"       --trace <PID> <File>             "
                L"Writes into the given file the current trace data for the given PID\n");
        wprintf(L"       --stop <PID>                     "
                L"Stops Intel PT tracing for the given PID\n");
        wprintf(L"\n");
        wprintf(L"All operations require PROCESS_VM_READ rights on the target PID\n");
        goto Cleanup;
    }

    //
    // Start parsing arguments
    //
    if (wcscmp(pwszArguments[1], L"--start") == 0)
    {
        //
        // Five arguments are neded to start a trace
        //
        if (dwArgumentCount != 5)
        {
            goto Banner;
        }

        //
        // Open a handle to the PID
        //
        bRes = ConfigureProcess(pwszArguments[2], &hProcess);
        if (bRes == FALSE)
        {
            goto Cleanup;
        }

        //
        // Initialize options for Intel PT Trace
        //
        options.OptionVersion = 1;

        //
        // Configure the buffer size
        //
        bRes = ConfigureBufferSize(pwszArguments[3], &options);
        if (bRes == FALSE)
        {
            goto Cleanup;
        }

        //
        // Configure the trace flag
        //
        bRes = ConfigureTraceFlags(pwszArguments[4], &options);
        if (bRes == FALSE)
        {
            goto Cleanup;
        }

        //
        // We are starting a trace, once we know the driver works
        //
        dwAction = IptTlStartTrace;
    }
    else if (wcscmp(pwszArguments[1], L"--stop") == 0)
    {
        //
        // Stopping a trace needs 3 arguments
        //
        if (dwArgumentCount != 3)
        {
            goto Banner;
        }

        //
        // Open a handle to the PID
        //
        bRes = ConfigureProcess(pwszArguments[2], &hProcess);
        if (bRes == FALSE)
        {
            goto Cleanup;
        }

        //
        // We are stopping a trace, once we know the driver works
        //
        dwAction = IptTlStopTrace;
    }
    else if (wcscmp(pwszArguments[1], L"--trace") == 0)
    {
        //
        // Writing a trace needs 4 arguments
        //
        if (dwArgumentCount != 4)
        {
            goto Banner;
        }

        //
        // Open a handle to the PID
        //
        bRes = ConfigureProcess(pwszArguments[2], &hProcess);
        if (bRes == FALSE)
        {
            goto Cleanup;
        }

        //
        // Open a handle to the trace file
        //
        hTraceFile = CreateFile(pwszArguments[3],
                                FILE_GENERIC_WRITE,
                                FILE_SHARE_READ,
                                NULL,
                                CREATE_ALWAYS,
                                FILE_ATTRIBUTE_NORMAL,
                                NULL);
        if (hTraceFile == INVALID_HANDLE_VALUE)
        {
            wprintf(L"[-] Unable to create trace file %s (err=%d)\n",
                    pwszArguments[3],
                    GetLastError());
            goto Cleanup;
        }

        //
        // We are getting a trace, once we know the driver works
        //
        dwAction = IptTlGetTrace;
    }
    else
    {
        goto Banner;
    }

    //
    // Enable and validate IPT support works
    //
    bRes = EnableAndValidateIptServices();
    if (bRes == FALSE)
    {
        goto Cleanup;
    }

    //
    // Check what we're doing
    //
    switch (dwAction)
    {
        case IptTlStartTrace:
        {
            //
            // Start the trace
            //
            bRes = StartProcessIptTrace(hProcess, options);
            if (bRes == FALSE)
            {
                wprintf(L"[-] Failed to start a trace (err=%d)\n",
                        GetLastError());
                goto Cleanup;
            }

            //
            // Print out the status
            //
            wprintf(L"[+] Trace for PID %s started\n",
                    pwszArguments[2]);
            dwResult = 0;
            break;
        }

        case IptTlStopTrace:
        {
            //
            // Stop the trace
            //
            bRes = StopProcessIptTrace(hProcess);
            if (bRes == FALSE)
            {
                wprintf(L"[-] Failed to stop the trace (err=%d)\n",
                        GetLastError());
                goto Cleanup;
            }

            //
            // Print out the status
            //
            wprintf(L"[+] Trace for PID %s stopped\n",
                    pwszArguments[2]);
            dwResult = 0;
            break;
        }

        case IptTlGetTrace:
        {
            //
            // Get the size of the trace
            //
            bRes = GetProcessIptTraceSize(hProcess, &dwTraceSize);
            if (bRes == FALSE)
            {
                wprintf(L"[-] Failed to query trace size (err=%d). "
                        L"Are you sure one is active?\n",
                        GetLastError());
                goto Cleanup;
            }

            //
            // Allocate a local buffer
            //
            pTraceData = HeapAlloc(GetProcessHeap(),
                                   HEAP_ZERO_MEMORY,
                                   dwTraceSize);
            if (pTraceData == NULL)
            {
                wprintf(L"[-] Out of memory while trying to allocate trace data\n");
                goto Cleanup;
            }

            //
            // Query the trace
            //
            wprintf(L"[+] Found active trace with %d bytes so far\n", dwTraceSize);
            bRes = GetProcessIptTrace(hProcess, pTraceData, dwTraceSize);
            if (bRes == FALSE)
            {
                wprintf(L"[-] Failed to query trace (err=%d)\n",
                        GetLastError());
                goto Cleanup;
            }

            //
            // Parse it
            //
            PIPT_TRACE_HEADER traceHeader;
            DWORD headerEntries;
            DWORD i;

            headerEntries = (dwTraceSize - (dwTraceSize & ~0xFFF)) /
                            sizeof (*traceHeader);
            wprintf(L"[+] Trace contains %d headers\n", headerEntries);

            //
            // Parse each entry
            //
            traceHeader = (PIPT_TRACE_HEADER)pTraceData->TraceData;
            for (i = 0; i < headerEntries; i++)
            {
                //
                // Print out information from it
                //
                wprintf(L"[+] Trace Entry %d for TID %p\n",
                        i,
                        traceHeader->ThreadId);
                wprintf(L"       Trace Size: %08d  [Unknown: %d]\n", 
                        traceHeader->TraceSize,
                        traceHeader->UnknownSize);
                wprintf(L"      Timing Mode: %d         [MTC Frequency: %d, ClockTsc Ratio: %d]\n",
                        traceHeader->TimingSettings,
                        traceHeader->MtcFrequency,
                        traceHeader->FrequencyToTscRatio);

                //
                // Move to the next trace header
                //
                traceHeader = (PIPT_TRACE_HEADER)(traceHeader->Trace + traceHeader->TraceSize);
            }

            //
            // Write it to disk
            //
            bRes = WriteFile(hTraceFile,
                             pTraceData->TraceData,
                             dwTraceSize - FIELD_OFFSET(IPT_TRACE_DATA, TraceData),
                             NULL,
                             NULL);
            if (bRes == FALSE)
            {
                wprintf(L"[-] Failed to write trace to disk (err=%d)\n",
                        GetLastError());
                goto Cleanup;
            }

            //
            // Print out the status
            //
            wprintf(L"[+] Trace for PID %s written to %s\n",
                    pwszArguments[2],
                    pwszArguments[3]);
            dwResult = 0;
            break;
        }

        DEFAULT_UNREACHABLE;
    }

Cleanup:
    //
    // Cleanup trace data if any was left over
    //
    if (pTraceData != NULL)
    {
        HeapFree(GetProcessHeap(), 0, pTraceData);
    }

    //
    // Close the trace file if we had one
    //
    if (hTraceFile != INVALID_HANDLE_VALUE)
    {
        CloseHandle(hTraceFile);
    }

    //
    // Close the process handle if we had one
    //
    if (hProcess != NULL)
    {
        CloseHandle(hProcess);
    }

    //
    // Return to caller
    //
    return dwResult;
}
