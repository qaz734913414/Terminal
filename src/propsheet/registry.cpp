/*++

Copyright (c) 1990  Microsoft Corporation

Module Name:

    registry.c

Abstract:

    This file contains functions to read and _rite values
    to the registry.

Author:

    Jerry Shea (JerrySh) 30-Sep-1994

Revision History:

--*/

#include "precomp.h"
#include <shlwapi.h>
#include <strsafe.h>
#include <intsafe.h>
#pragma hdrstop

#define CONSOLE_REGISTRY_CURRENTPAGE  (L"CurrentPage")

extern BOOL g_fEditKeys;

BOOL GetConsoleBoolValue(__in PCWSTR pszValueName)
{
    return SHRegGetBoolUSValue(CONSOLE_REGISTRY_STRING,
                               pszValueName,
                               FALSE /*fIgnoreHKCU*/,
                               FALSE /*fDefault*/);
}

/*++

Routine Description:

    This routine allocates a state info structure and fill it in with
    default values.

Arguments:

    none

Return Value:

    pStateInfo - pointer to structure to receive information

--*/

VOID InitRegistryValues(
    __out PCONSOLE_STATE_INFO pStateInfo)
{
    pStateInfo->ScreenAttributes = 0x07;            // white on black
    pStateInfo->PopupAttributes = 0xf5;             // purple on white
    pStateInfo->InsertMode = FALSE;
    pStateInfo->QuickEdit = FALSE;
    pStateInfo->FullScreen = FALSE;
    pStateInfo->ScreenBufferSize.X = 80;
    pStateInfo->ScreenBufferSize.Y = 25;
    pStateInfo->WindowSize.X = 80;
    pStateInfo->WindowSize.Y = 25;
    pStateInfo->WindowPosX = 0;
    pStateInfo->WindowPosY = 0;
    pStateInfo->AutoPosition = TRUE;
    pStateInfo->FontSize.X = 0;
    pStateInfo->FontSize.Y = 0;
    pStateInfo->FontFamily = 0;
    pStateInfo->FontWeight = 0;
    pStateInfo->FaceName[0] = TEXT('\0');
    pStateInfo->CursorSize = 25;
    pStateInfo->HistoryBufferSize = 25;
    pStateInfo->NumberOfHistoryBuffers = 4;
    pStateInfo->HistoryNoDup = 0;
    pStateInfo->ColorTable[ 0] = RGB(0,   0,   0   );
    pStateInfo->ColorTable[ 1] = RGB(0,   0,   0x80);
    pStateInfo->ColorTable[ 2] = RGB(0,   0x80,0   );
    pStateInfo->ColorTable[ 3] = RGB(0,   0x80,0x80);
    pStateInfo->ColorTable[ 4] = RGB(0x80,0,   0   );
    pStateInfo->ColorTable[ 5] = RGB(0x80,0,   0x80);
    pStateInfo->ColorTable[ 6] = RGB(0x80,0x80,0   );
    pStateInfo->ColorTable[ 7] = RGB(0xC0,0xC0,0xC0);
    pStateInfo->ColorTable[ 8] = RGB(0x80,0x80,0x80);
    pStateInfo->ColorTable[ 9] = RGB(0,   0,   0xFF);
    pStateInfo->ColorTable[10] = RGB(0,   0xFF,0   );
    pStateInfo->ColorTable[11] = RGB(0,   0xFF,0xFF);
    pStateInfo->ColorTable[12] = RGB(0xFF,0,   0   );
    pStateInfo->ColorTable[13] = RGB(0xFF,0,   0xFF);
    pStateInfo->ColorTable[14] = RGB(0xFF,0xFF,0   );
    pStateInfo->ColorTable[15] = RGB(0xFF,0xFF,0xFF);
    pStateInfo->CodePage = OEMCP;
    pStateInfo->hWnd = NULL;
    pStateInfo->OriginalTitle = NULL;
    pStateInfo->LinkTitle = NULL;

    // v2 console state
    pStateInfo->fWrapText = TRUE;
    pStateInfo->fFilterOnPaste = TRUE;
    pStateInfo->fCtrlKeyShortcutsDisabled = FALSE;
    pStateInfo->fLineSelection= TRUE;
    pStateInfo->bWindowTransparency = BYTE_MAX;
    // end v2 console state
}

/*++

Routine Description:

    This routine reads in values from the registry and places them
    in the supplied structure.

Arguments:

    pStateInfo - optional pointer to structure to receive information

Return Value:

    current page number

--*/

DWORD GetRegistryValues(
    __out_opt PCONSOLE_STATE_INFO pStateInfo)
{
    HKEY hCurrentUserKey, hConsoleKey, hTitleKey;
    NTSTATUS Status;
    DWORD dwValue, dwRet = 0, i;
    WCHAR awchBuffer[LF_FACESIZE];

    // initial values for global v2 settings
    g_fForceV2 = GetConsoleBoolValue(CONSOLE_REGISTRY_FORCEV2);
    g_fEditKeys = GetConsoleBoolValue(CONSOLE_REGISTRY_EXTENDEDEDITKEY);

    //
    // Open the current user registry key and console key.
    //
    Status = RegistrySerialization::s_OpenConsoleKey(&hCurrentUserKey, &hConsoleKey);

    if (!NT_SUCCESS(Status)) {
        return 0;
    }

    //
    // If there is no structure to fill out, just get the current
    // page and bail out.
    //

    if (pStateInfo == nullptr) {
        Status = RegistrySerialization::s_QueryValue(hConsoleKey,
                                                     CONSOLE_REGISTRY_CURRENTPAGE,
                                                     sizeof(dwValue),
                                                     (PBYTE)&dwValue,
                                                     NULL);
        if (NT_SUCCESS(Status)) {
            dwRet = dwValue;
        }

        goto CloseKeys;
    }

    //
    // Open the console title subkey unless we're changing the defaults.
    //
    if (pStateInfo->Defaults) {
        hTitleKey = hConsoleKey;
    } else {
        Status = RegistrySerialization::s_OpenKey(hConsoleKey,
                                                  pStateInfo->OriginalTitle,
                                                  &hTitleKey);
        if (!NT_SUCCESS(Status)) {
            RegCloseKey(hConsoleKey);
            RegCloseKey(hCurrentUserKey);
            return 0;
        }
    }

    //
    // Initial screen fill
    //

    Status = RegistrySerialization::s_QueryValue(hTitleKey,
                                                 CONSOLE_REGISTRY_FILLATTR,
                                                 sizeof(dwValue),
                                                 (PBYTE)&dwValue,
                                                 NULL);
    if (NT_SUCCESS(Status)) {
        pStateInfo->ScreenAttributes = (WORD)dwValue;
    }

    //
    // Initial popup fill
    //

    Status = RegistrySerialization::s_QueryValue(hTitleKey,
                                                 CONSOLE_REGISTRY_POPUPATTR,
                                                 sizeof(dwValue),
                                                 (PBYTE)&dwValue,
                                                 NULL);
    if (NT_SUCCESS(Status)) {
        pStateInfo->PopupAttributes = (WORD)dwValue;
    }

    //
    // Initial color table
    //

    for (i = 0; i < 16; i++) {
        StringCchPrintf(awchBuffer,
                        ARRAYSIZE(awchBuffer),
                        CONSOLE_REGISTRY_COLORTABLE,
                        i);
        Status = RegistrySerialization::s_QueryValue(hTitleKey,
                                                     awchBuffer,
                                                     sizeof(dwValue),
                                                     (PBYTE)&dwValue,
                                                     NULL);
        if (NT_SUCCESS(Status)) {
            pStateInfo->ColorTable[i] = dwValue;
        }
    }

    //
    // Initial insert mode.
    //
    Status = RegistrySerialization::s_QueryValue(hTitleKey,
                                                 CONSOLE_REGISTRY_INSERTMODE,
                                                 sizeof(dwValue),
                                                 (PBYTE)&dwValue,
                                                 NULL);
    if (NT_SUCCESS(Status)) {
        pStateInfo->InsertMode = !!dwValue;
    }

    //
    // Initial quick edit mode
    //
    Status = RegistrySerialization::s_QueryValue(hTitleKey,
                                                 CONSOLE_REGISTRY_QUICKEDIT,
                                                 sizeof(dwValue),
                                                 (PBYTE)&dwValue,
                                                 NULL);
    if (NT_SUCCESS(Status)) {
        pStateInfo->QuickEdit = !!dwValue;
    }

    //
    // Initial code page
    //

    assert(OEMCP != 0);
    Status = RegistrySerialization::s_QueryValue(hTitleKey,
                                                 CONSOLE_REGISTRY_CODEPAGE,
                                                 sizeof(dwValue),
                                                 (PBYTE)&dwValue,
                                                 NULL);
    if (NT_SUCCESS(Status)) {
        if (IsValidCodePage(dwValue)) {
            pStateInfo->CodePage = (UINT) dwValue;
        }
    }

    //
    // Initial screen buffer size.
    //
    Status = RegistrySerialization::s_QueryValue(hTitleKey,
                                                 CONSOLE_REGISTRY_BUFFERSIZE,
                                                 sizeof(dwValue),
                                                 (PBYTE)&dwValue,
                                                 NULL);
    if (NT_SUCCESS(Status)) {
        pStateInfo->ScreenBufferSize.X = LOWORD(dwValue);
        pStateInfo->ScreenBufferSize.Y = HIWORD(dwValue);
    }

    //
    // Initial window size.
    //
    Status = RegistrySerialization::s_QueryValue(hTitleKey,
                                                 CONSOLE_REGISTRY_WINDOWSIZE,
                                                 sizeof(dwValue),
                                                 (PBYTE)&dwValue,
                                                 NULL);
    if (NT_SUCCESS(Status)) {
        pStateInfo->WindowSize.X = LOWORD(dwValue);
        pStateInfo->WindowSize.Y = HIWORD(dwValue);
    }

    //
    // Initial window position.
    //
    Status = RegistrySerialization::s_QueryValue(hTitleKey,
                                                 CONSOLE_REGISTRY_WINDOWPOS,
                                                 sizeof(dwValue),
                                                 (PBYTE)&dwValue,
                                                 NULL);
    if (NT_SUCCESS(Status)) {
        pStateInfo->WindowPosX = (SHORT)LOWORD(dwValue);
        pStateInfo->WindowPosY = (SHORT)HIWORD(dwValue);
        pStateInfo->AutoPosition = FALSE;
    }

    //
    // Initial font size
    //

    Status = RegistrySerialization::s_QueryValue(hTitleKey,
                                                 CONSOLE_REGISTRY_FONTSIZE,
                                                 sizeof(dwValue),
                                                 (PBYTE)&dwValue,
                                                 NULL);
    if (NT_SUCCESS(Status)) {
        pStateInfo->FontSize.X = LOWORD(dwValue);
        pStateInfo->FontSize.Y = HIWORD(dwValue);
    }

    //
    // Initial font family.
    //
    Status = RegistrySerialization::s_QueryValue(hTitleKey,
                                                 CONSOLE_REGISTRY_FONTFAMILY,
                                                 sizeof(dwValue),
                                                 (PBYTE)&dwValue,
                                                 NULL);
    if (NT_SUCCESS(Status)) {
        pStateInfo->FontFamily = dwValue;
    }

    //
    // Initial font weight.
    //
    Status = RegistrySerialization::s_QueryValue(hTitleKey,
                                                 CONSOLE_REGISTRY_FONTWEIGHT,
                                                 sizeof(dwValue),
                                                 (PBYTE)&dwValue,
                                                 NULL);
    if (NT_SUCCESS(Status)) {
        pStateInfo->FontWeight = dwValue;
    }

    //
    // Initial font face name.
    //
    Status = RegistrySerialization::s_QueryValue(hTitleKey,
                                                 CONSOLE_REGISTRY_FACENAME,
                                                 sizeof(awchBuffer),
                                                 (PBYTE)awchBuffer,
                                                 NULL);
    if (NT_SUCCESS(Status)) {
        RtlCopyMemory(pStateInfo->FaceName, awchBuffer, sizeof(awchBuffer));
    }

    //
    // Initial cursor size.
    //
    Status = RegistrySerialization::s_QueryValue(hTitleKey,
                                                 CONSOLE_REGISTRY_CURSORSIZE,
                                                 sizeof(dwValue),
                                                 (PBYTE)&dwValue,
                                                 NULL);
    if (NT_SUCCESS(Status)) {
        pStateInfo->CursorSize = dwValue;
    }

    //
    // Initial history buffer size.
    //
    Status = RegistrySerialization::s_QueryValue(hTitleKey,
                                                 CONSOLE_REGISTRY_HISTORYSIZE,
                                                 sizeof(dwValue),
                                                 (PBYTE)&dwValue,
                                                 NULL);
    if (NT_SUCCESS(Status)) {
        pStateInfo->HistoryBufferSize = dwValue;
    }

    //
    // Initial number of history buffers.
    //
    Status = RegistrySerialization::s_QueryValue(hTitleKey,
                                                 CONSOLE_REGISTRY_HISTORYBUFS,
                                                 sizeof(dwValue),
                                                 (PBYTE)&dwValue,
                                                 NULL);
    if (NT_SUCCESS(Status)) {
        pStateInfo->NumberOfHistoryBuffers = dwValue;
    }

    //
    // Initial history duplication mode.
    //
    Status = RegistrySerialization::s_QueryValue(hTitleKey,
                                                 CONSOLE_REGISTRY_HISTORYNODUP,
                                                 sizeof(dwValue),
                                                 (PBYTE)&dwValue,
                                                 NULL);
    if (NT_SUCCESS(Status)) {
        pStateInfo->HistoryNoDup = dwValue;
    }

    //
    // Initial text wrapping.
    //
    Status = RegistrySerialization::s_QueryValue(hTitleKey,
                                                 CONSOLE_REGISTRY_LINEWRAP,
                                                 sizeof(dwValue),
                                                 (PBYTE)&dwValue,
                                                 NULL);
    if (NT_SUCCESS(Status)) {
        pStateInfo->fWrapText = dwValue;
    }

    //
    // Initial filter on paste.
    //
    Status = RegistrySerialization::s_QueryValue(hTitleKey,
                                                 CONSOLE_REGISTRY_FILTERONPASTE,
                                                 sizeof(dwValue),
                                                 (PBYTE)&dwValue,
                                                 NULL);
    if (NT_SUCCESS(Status)) {
        pStateInfo->fFilterOnPaste = dwValue;
    }

    //
    // Initial ctrl shortcuts disabled.
    //
    Status = RegistrySerialization::s_QueryValue(hTitleKey,
                                                 CONSOLE_REGISTRY_CTRLKEYSHORTCUTS_DISABLED,
                                                 sizeof(dwValue),
                                                 (PBYTE)&dwValue,
                                                 NULL);
    if (NT_SUCCESS(Status)) {
        pStateInfo->fCtrlKeyShortcutsDisabled = dwValue;
    }

    //
    // Initial line selection.
    //
    Status = RegistrySerialization::s_QueryValue(hTitleKey,
                                                 CONSOLE_REGISTRY_LINESELECTION,
                                                 sizeof(dwValue),
                                                 (PBYTE)&dwValue,
                                                 NULL);
    if (NT_SUCCESS(Status)) {
        pStateInfo->fLineSelection = dwValue;
    }

    //
    // Initial transparency.
    //
    Status = RegistrySerialization::s_QueryValue(hTitleKey,
                                                 CONSOLE_REGISTRY_WINDOWALPHA,
                                                 sizeof(dwValue),
                                                 (PBYTE)&dwValue,
                                                 NULL);
    if (NT_SUCCESS(Status)) {
        if (dwValue <= BYTE_MAX)
        {
            pStateInfo->bWindowTransparency = (BYTE)dwValue;
        }
    }

    //
    // Close the registry keys
    //

    if (hTitleKey != hConsoleKey) {
        RegCloseKey(hTitleKey);
    }

CloseKeys:
    RegCloseKey(hConsoleKey);
    RegCloseKey(hCurrentUserKey);

    return dwRet;
}

VOID SetGlobalRegistryValues()
{
    // save global v2 settings
    const DWORD dwForceV2 = g_fForceV2;
    SHSetValue(HKEY_CURRENT_USER,
               CONSOLE_REGISTRY_STRING,
               CONSOLE_REGISTRY_FORCEV2,
               REG_DWORD,
               (LPBYTE)&dwForceV2,
               sizeof(dwForceV2));

    const DWORD dwEditKeys = g_fEditKeys;
    SHSetValue(HKEY_CURRENT_USER,
               CONSOLE_REGISTRY_STRING,
               CONSOLE_REGISTRY_EXTENDEDEDITKEY,
               REG_DWORD,
               (LPBYTE)&dwEditKeys,
               sizeof(dwEditKeys));
}

/*++

Routine Description:

    This routine writes values to the registry from the supplied
    structure.

Arguments:

    pStateInfo - optional pointer to structure containing information
    dwPage     - current page number

Return Value:

    none

--*/

VOID SetRegistryValues(
    PCONSOLE_STATE_INFO pStateInfo,
    DWORD dwPage)
{
    HKEY hCurrentUserKey, hConsoleKey, hTitleKey;
    NTSTATUS Status;
    DWORD dwValue, i;
    WCHAR awchBuffer[LF_FACESIZE];

    //
    // Open the current user registry key and console registry key.
    //
    Status = RegistrySerialization::s_OpenConsoleKey(&hCurrentUserKey, &hConsoleKey);

    if (!NT_SUCCESS(Status)) {
        return;
    }

    //
    // Save the current page.
    //
    RegistrySerialization::s_SetValue(hConsoleKey,
                  CONSOLE_REGISTRY_CURRENTPAGE,
                  REG_DWORD,
                  (BYTE*)&dwPage,
                  sizeof(dwPage));

    //
    // Open the console title subkey unless we're changing the defaults.
    //
    if (pStateInfo->Defaults) {
        hTitleKey = hConsoleKey;
    } else {
        Status = RegistrySerialization::s_CreateKey(hConsoleKey,
                                pStateInfo->OriginalTitle,
                                &hTitleKey);
        if (!NT_SUCCESS(Status)) {
            RegCloseKey(hConsoleKey);
            RegCloseKey(hCurrentUserKey);
            return;
        }
    }

    //
    // Save screen and popup colors and color table
    //

    dwValue = pStateInfo->ScreenAttributes;
    RegistrySerialization::s_UpdateValue(hConsoleKey, hTitleKey, CONSOLE_REGISTRY_FILLATTR,
                     REG_DWORD, (BYTE*)&dwValue, sizeof(dwValue));
    dwValue = pStateInfo->PopupAttributes;
    RegistrySerialization::s_UpdateValue(hConsoleKey, hTitleKey, CONSOLE_REGISTRY_POPUPATTR,
                     REG_DWORD, (BYTE*)&dwValue, sizeof(dwValue));
    for (i = 0; i < 16; i++) {
        dwValue = pStateInfo->ColorTable[i];
        if (SUCCEEDED(StringCchPrintf(awchBuffer, ARRAYSIZE(awchBuffer), CONSOLE_REGISTRY_COLORTABLE, i)))
        {
            RegistrySerialization::s_UpdateValue(hConsoleKey, hTitleKey, awchBuffer,
                            REG_DWORD, (BYTE*)&dwValue, sizeof(dwValue));
        }
    }

    //
    // Save insert, quickedit, and fullscreen mode settings
    //

    dwValue = pStateInfo->InsertMode;
    RegistrySerialization::s_UpdateValue(hConsoleKey,
                     hTitleKey,
                     CONSOLE_REGISTRY_INSERTMODE,
                     REG_DWORD,
                     (BYTE*)&dwValue,
                     sizeof(dwValue));
    dwValue = pStateInfo->QuickEdit;
    RegistrySerialization::s_UpdateValue(hConsoleKey,
                     hTitleKey,
                     CONSOLE_REGISTRY_QUICKEDIT,
                     REG_DWORD,
                     (BYTE*)&dwValue,
                     sizeof(dwValue));

    assert(OEMCP != 0);
    if (g_fEastAsianSystem) {
        dwValue = (DWORD) pStateInfo->CodePage;
        RegistrySerialization::s_UpdateValue(hConsoleKey,
                         hTitleKey,
                         CONSOLE_REGISTRY_CODEPAGE,
                         REG_DWORD,
                         (BYTE*)&dwValue,
                         sizeof(dwValue));
    }

    //
    // Save screen buffer size
    //

    dwValue = MAKELONG(pStateInfo->ScreenBufferSize.X,
                       pStateInfo->ScreenBufferSize.Y);
    RegistrySerialization::s_UpdateValue(hConsoleKey,
                     hTitleKey,
                     CONSOLE_REGISTRY_BUFFERSIZE,
                     REG_DWORD,
                     (BYTE*)&dwValue,
                     sizeof(dwValue));

    //
    // Save window size
    //

    dwValue = MAKELONG(pStateInfo->WindowSize.X,
                       pStateInfo->WindowSize.Y);
    RegistrySerialization::s_UpdateValue(hConsoleKey,
                     hTitleKey,
                     CONSOLE_REGISTRY_WINDOWSIZE,
                     REG_DWORD,
                     (BYTE*)&dwValue,
                     sizeof(dwValue));

    //
    // Save window position
    //

    if (pStateInfo->AutoPosition) {
        RegistrySerialization::s_DeleteValue(hTitleKey, CONSOLE_REGISTRY_WINDOWPOS);
    } else {
        dwValue = MAKELONG(pStateInfo->WindowPosX,
                           pStateInfo->WindowPosY);
        RegistrySerialization::s_UpdateValue(hConsoleKey,
                         hTitleKey,
                         CONSOLE_REGISTRY_WINDOWPOS,
                         REG_DWORD,
                         (BYTE*)&dwValue,
                         sizeof(dwValue));
    }

    //
    // Save font size, family, weight, and face name
    //

    dwValue = MAKELONG(pStateInfo->FontSize.X,
                       pStateInfo->FontSize.Y);
    RegistrySerialization::s_UpdateValue(hConsoleKey,
                     hTitleKey,
                     CONSOLE_REGISTRY_FONTSIZE,
                     REG_DWORD,
                     (BYTE*)&dwValue,
                     sizeof(dwValue));
    dwValue = pStateInfo->FontFamily;
    RegistrySerialization::s_UpdateValue(hConsoleKey,
                     hTitleKey,
                     CONSOLE_REGISTRY_FONTFAMILY,
                     REG_DWORD,
                     (BYTE*)&dwValue,
                     sizeof(dwValue));
    dwValue = pStateInfo->FontWeight;
    RegistrySerialization::s_UpdateValue(hConsoleKey,
                     hTitleKey,
                     CONSOLE_REGISTRY_FONTWEIGHT,
                     REG_DWORD,
                     (BYTE*)&dwValue,
                     sizeof(dwValue));
    RegistrySerialization::s_UpdateValue(hConsoleKey,
                     hTitleKey,
                     CONSOLE_REGISTRY_FACENAME,
                     REG_SZ,
                     (BYTE*)(pStateInfo->FaceName),
                     (DWORD)(wcslen(pStateInfo->FaceName) + 1) * sizeof(TCHAR));

    //
    // Save cursor size
    //

    dwValue = pStateInfo->CursorSize;
    RegistrySerialization::s_UpdateValue(hConsoleKey,
                     hTitleKey,
                     CONSOLE_REGISTRY_CURSORSIZE,
                     REG_DWORD,
                     (BYTE*)&dwValue,
                     sizeof(dwValue));

    //
    // Save history buffer size and number
    //

    dwValue = pStateInfo->HistoryBufferSize;
    RegistrySerialization::s_UpdateValue(hConsoleKey,
                     hTitleKey,
                     CONSOLE_REGISTRY_HISTORYSIZE,
                     REG_DWORD,
                     (BYTE*)&dwValue,
                     sizeof(dwValue));
    dwValue = pStateInfo->NumberOfHistoryBuffers;
    RegistrySerialization::s_UpdateValue(hConsoleKey,
                     hTitleKey,
                     CONSOLE_REGISTRY_HISTORYBUFS,
                     REG_DWORD,
                     (BYTE*)&dwValue,
                     sizeof(dwValue));
    dwValue = pStateInfo->HistoryNoDup;
    RegistrySerialization::s_UpdateValue(hConsoleKey,
                     hTitleKey,
                     CONSOLE_REGISTRY_HISTORYNODUP,
                     REG_DWORD,
                     (BYTE*)&dwValue,
                     sizeof(dwValue));

    // Save per-title V2 console state
    dwValue = pStateInfo->fWrapText;
    RegistrySerialization::s_UpdateValue(hConsoleKey,
                     hTitleKey,
                     CONSOLE_REGISTRY_LINEWRAP,
                     REG_DWORD,
                     (BYTE*)&dwValue,
                     sizeof(dwValue));
    dwValue = pStateInfo->fFilterOnPaste;
    RegistrySerialization::s_UpdateValue(hConsoleKey,
                     hTitleKey,
                     CONSOLE_REGISTRY_FILTERONPASTE,
                     REG_DWORD,
                     (BYTE*)&dwValue,
                     sizeof(dwValue));
    dwValue = pStateInfo->fCtrlKeyShortcutsDisabled;
    RegistrySerialization::s_UpdateValue(hConsoleKey,
                     hTitleKey,
                     CONSOLE_REGISTRY_CTRLKEYSHORTCUTS_DISABLED,
                     REG_DWORD,
                     (BYTE*)&dwValue,
                     sizeof(dwValue));
    dwValue = pStateInfo->fLineSelection;
    RegistrySerialization::s_UpdateValue(hConsoleKey,
                     hTitleKey,
                     CONSOLE_REGISTRY_LINESELECTION,
                     REG_DWORD,
                     (BYTE*)&dwValue,
                     sizeof(dwValue));
    dwValue = pStateInfo->bWindowTransparency;
    RegistrySerialization::s_UpdateValue(hConsoleKey,
                     hTitleKey,
                     CONSOLE_REGISTRY_WINDOWALPHA,
                     REG_DWORD,
                     (BYTE*)&dwValue,
                     sizeof(dwValue));

    SetGlobalRegistryValues();



    //
    // Close the registry keys
    //
    if (hTitleKey != hConsoleKey) {
        RegCloseKey(hTitleKey);
    }

    RegCloseKey(hConsoleKey);
    RegCloseKey(hCurrentUserKey);
}