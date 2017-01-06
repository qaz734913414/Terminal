/*++
Copyright (c) Microsoft Corporation

Module Name:
- stream.h

Abstract:
- This file implements the NT console server stream API

Author:
- Therese Stowell (ThereseS) 6-Nov-1990

Revision History:
--*/

#pragma once

#include "cmdline.h"

#define IS_CONTROL_CHAR(wch)  ((wch) < L' ')

// Routine Description:
// - This routine is used in stream input.  It gets input and filters it for unicode characters.
// Arguments:
// - InputInfo - Pointer to input buffer information.
// - Char - Unicode char input.
// - Wait - TRUE if the routine shouldn't wait for input.
// - Console - Pointer to console buffer information.
// - HandleData - Pointer to handle data structure.
// - Message - csr api message.
// - WaitRoutine - Routine to call when wait is woken up.
// - WaitParameter - Parameter to pass to wait routine.
// - WaitParameterLength - Length of wait parameter.
// - WaitBlockExists - TRUE if wait block has already been created.
// - CommandLineEditingKeys - if present, arrow keys will be returned. on output, if TRUE, Char contains virtual key code for arrow key.
// - CommandLinePopupKeys - if present, arrow keys will be returned. on output, if TRUE, Char contains virtual key code for arrow key.
// Return Value:
// - <none>
NTSTATUS GetChar(_In_ PINPUT_INFORMATION pInputInfo,
                 _Out_ PWCHAR pwchChar,
                 _In_ const BOOL fWait,
                 _In_opt_ INPUT_READ_HANDLE_DATA* pHandleData,
                 _In_opt_ PCONSOLE_API_MSG pConsoleMessage,
                 _In_opt_ ConsoleWaitRoutine pWaitRoutine,
                 _In_opt_ PVOID pvWaitParameter,
                 _In_opt_ ULONG ulWaitParameterLength,
                 _In_opt_ BOOLEAN fWaitBlockExists,
                 _Out_opt_ PBOOLEAN pfCommandLineEditingKeys,
                 _Out_opt_ PBOOLEAN pfCommandLinePopupKeys,
                 _Out_opt_ PBOOLEAN pfEnableScrollMode,
                 _Out_opt_ PDWORD pdwKeyState);

// Routine Description:
// - This routine returns the total number of screen spaces the characters up to the specified character take up.
ULONG RetrieveTotalNumberOfSpaces(_In_ const SHORT sOriginalCursorPositionX,
                                  _In_reads_(ulCurrentPosition) const WCHAR * const pwchBuffer,
                                  _In_ const ULONG ulCurrentPosition);

// Routine Description:
// - This routine returns the number of screen spaces the specified character takes up.
ULONG RetrieveNumberOfSpaces(_In_ SHORT sOriginalCursorPositionX,
                             _In_reads_(ulCurrentPosition + 1) const WCHAR * const pwchBuffer,
                             _In_ ULONG ulCurrentPosition);


// Return Value:
// - TRUE if read is completed
BOOL ProcessCookedReadInput(_In_ PCOOKED_READ_DATA pCookedReadData, _In_ WCHAR wch, _In_ const DWORD dwKeyState, _Out_ NTSTATUS* pStatus);

NTSTATUS CookedRead(_In_ PCOOKED_READ_DATA pCookedReadData, _In_ PCONSOLE_API_MSG pWaitReplyMessage, _In_ const BOOLEAN fWaitRoutine);

BOOL CookedReadWaitRoutine(_In_ PCONSOLE_API_MSG pWaitReplyMessage,
                           _In_ PCOOKED_READ_DATA pCookedReadData,
                           _In_ WaitTerminationReason TerminationReason);

// Routine Description:
// - This routine reads characters from the input stream.
NTSTATUS SrvReadConsole(_Inout_ PCONSOLE_API_MSG m, _Inout_ PBOOL ReplyPending);

VOID UnblockWriteConsole(_In_ const DWORD dwReason);

// Routine Description:
// -  This routine writes characters to the output stream.
NTSTATUS SrvWriteConsole(_Inout_ PCONSOLE_API_MSG m, _Inout_ PBOOL ReplyPending);

BOOL WriteConsoleWaitRoutine(_In_ PCONSOLE_API_MSG pWaitReplyMessage,
                             _In_ PVOID pvWaitParameter,
                             _In_ WaitTerminationReason const TerminationReason);


NTSTATUS SrvCloseHandle(_In_ PCONSOLE_API_MSG m);