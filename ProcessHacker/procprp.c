/*
 * Process Hacker - 
 *   process properties
 * 
 * Copyright (C) 2009-2010 wj32
 * 
 * This file is part of Process Hacker.
 * 
 * Process Hacker is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Process Hacker is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Process Hacker.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <phapp.h>
#include <graph.h>
#include <procprpp.h>
#include <kph.h>
#include <settings.h>
#include <emenu.h>
#include <phplug.h>
#include <windowsx.h>

#define SET_BUTTON_BITMAP(Id, Bitmap) \
    SendMessage(GetDlgItem(hwndDlg, (Id)), BM_SETIMAGE, IMAGE_BITMAP, (LPARAM)(Bitmap))

PPH_OBJECT_TYPE PhpProcessPropContextType;
PPH_OBJECT_TYPE PhpProcessPropPageContextType;

BOOLEAN PhProcessPropInitialization()
{
    if (!NT_SUCCESS(PhCreateObjectType(
        &PhpProcessPropContextType,
        L"ProcessPropContext",
        0,
        PhpProcessPropContextDeleteProcedure
        )))
        return FALSE;

    if (!NT_SUCCESS(PhCreateObjectType(
        &PhpProcessPropPageContextType,
        L"ProcessPropPageContext",
        0,
        PhpProcessPropPageContextDeleteProcedure
        )))
        return FALSE;

    return TRUE;
}

PPH_PROCESS_PROPCONTEXT PhCreateProcessPropContext(
    __in HWND ParentWindowHandle,
    __in PPH_PROCESS_ITEM ProcessItem
    )
{
    PPH_PROCESS_PROPCONTEXT propContext;
    PROPSHEETHEADER propSheetHeader;

    if (!NT_SUCCESS(PhCreateObject(
        &propContext,
        sizeof(PH_PROCESS_PROPCONTEXT),
        0,
        PhpProcessPropContextType
        )))
        return NULL;

    memset(propContext, 0, sizeof(PH_PROCESS_PROPCONTEXT));

    propContext->PropSheetPages =
        PhAllocate(sizeof(HPROPSHEETPAGE) * PH_PROCESS_PROPCONTEXT_MAXPAGES);

    if (!PH_IS_FAKE_PROCESS_ID(ProcessItem->ProcessId))
    {
        propContext->Title = PhFormatString(
            L"%s (%u)",
            ProcessItem->ProcessName->Buffer,
            (ULONG)ProcessItem->ProcessId
            );
    }
    else
    {
        propContext->Title = ProcessItem->ProcessName;
        PhReferenceObject(propContext->Title);
    }

    memset(&propSheetHeader, 0, sizeof(PROPSHEETHEADER));
    propSheetHeader.dwSize = sizeof(PROPSHEETHEADER);
    propSheetHeader.dwFlags =
        PSH_MODELESS |
        PSH_NOAPPLYNOW |
        PSH_NOCONTEXTHELP |
        PSH_PROPTITLE |
        PSH_USECALLBACK |
        PSH_USEHICON;
    propSheetHeader.hwndParent = ParentWindowHandle;
    propSheetHeader.hIcon = ProcessItem->SmallIcon;
    propSheetHeader.pszCaption = propContext->Title->Buffer;
    propSheetHeader.pfnCallback = PhpPropSheetProc;

    propSheetHeader.nPages = 0;
    propSheetHeader.nStartPage = 0;
    propSheetHeader.phpage = propContext->PropSheetPages;

    memcpy(&propContext->PropSheetHeader, &propSheetHeader, sizeof(PROPSHEETHEADER));

    propContext->ProcessItem = ProcessItem;
    PhReferenceObject(ProcessItem);
    PhInitializeEvent(&propContext->CreatedEvent);

    return propContext;
}

VOID NTAPI PhpProcessPropContextDeleteProcedure(
    __in PVOID Object,
    __in ULONG Flags
    )
{
    PPH_PROCESS_PROPCONTEXT propContext = (PPH_PROCESS_PROPCONTEXT)Object;

    PhFree(propContext->PropSheetPages);
    PhDereferenceObject(propContext->Title);
    PhDereferenceObject(propContext->ProcessItem);
}

VOID PhRefreshProcessPropContext(
    __inout PPH_PROCESS_PROPCONTEXT PropContext
    )
{
    PropContext->PropSheetHeader.hIcon = PropContext->ProcessItem->SmallIcon;
}

VOID PhSetSelectThreadIdProcessPropContext(
    __inout PPH_PROCESS_PROPCONTEXT PropContext,
    __in HANDLE ThreadId
    )
{
    PropContext->SelectThreadId = ThreadId;
}

INT CALLBACK PhpPropSheetProc(
    __in HWND hwndDlg,
    __in UINT uMsg,
    __in LPARAM lParam
    )
{
#define PROPSHEET_ADD_STYLE (WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_THICKFRAME);

    switch (uMsg)
    {
    case PSCB_PRECREATE:
        {
            if (lParam)
            {
                if (((DLGTEMPLATEEX *)lParam)->signature == 0xffff)
                {
                    ((DLGTEMPLATEEX *)lParam)->style |= PROPSHEET_ADD_STYLE;
                }
                else
                {
                    ((DLGTEMPLATE *)lParam)->style |= PROPSHEET_ADD_STYLE;
                }
            }
        }
        break;
    case PSCB_INITIALIZED:
        {
            WNDPROC oldWndProc;
            PPH_LAYOUT_MANAGER layoutManager;

            oldWndProc = (WNDPROC)GetWindowLongPtr(hwndDlg, GWLP_WNDPROC);
            SetWindowLongPtr(hwndDlg, GWLP_WNDPROC, (LONG_PTR)PhpPropSheetWndProc);
            SetProp(hwndDlg, L"OldWndProc", (HANDLE)oldWndProc);

            layoutManager = PhAllocate(sizeof(PH_LAYOUT_MANAGER));
            PhInitializeLayoutManager(layoutManager, hwndDlg);
            SetProp(hwndDlg, L"LayoutManager", (HANDLE)layoutManager);
        }
        break;
    }

    return 0;
}

LRESULT CALLBACK PhpPropSheetWndProc(
    __in HWND hwnd,
    __in UINT uMsg,
    __in WPARAM wParam,
    __in LPARAM lParam
    )
{
    WNDPROC oldWndProc = (WNDPROC)GetProp(hwnd, L"OldWndProc");

    switch (uMsg)
    {
    case WM_DESTROY:
        {
            PPH_LAYOUT_MANAGER layoutManager;

            SetWindowLongPtr(hwnd, GWLP_WNDPROC, (LONG_PTR)oldWndProc);
            RemoveProp(hwnd, L"OldWndProc");

            RemoveProp(hwnd, L"TabPageItem");

            layoutManager = (PPH_LAYOUT_MANAGER)GetProp(hwnd, L"LayoutManager");
            PhDeleteLayoutManager(layoutManager);
            PhFree(layoutManager);
            RemoveProp(hwnd, L"LayoutManager");

            {
                WINDOWPLACEMENT placement = { sizeof(placement) };
                PH_RECTANGLE windowRectangle;
                HWND tabControl;
                TCITEM tabItem;
                WCHAR text[32];

                // Save the window position and size.

                GetWindowPlacement(hwnd, &placement);
                windowRectangle = PhRectToRectangle(placement.rcNormalPosition);

                PhSetIntegerPairSetting(L"ProcPropPosition", windowRectangle.Position);
                PhSetIntegerPairSetting(L"ProcPropSize", windowRectangle.Size);

                // Save the selected tab.

                tabControl = PropSheet_GetTabControl(hwnd);

                tabItem.mask = TCIF_TEXT;
                tabItem.pszText = text;
                tabItem.cchTextMax = sizeof(text) / 2 - 1;

                if (TabCtrl_GetItem(tabControl, TabCtrl_GetCurSel(tabControl), &tabItem))
                {
                    PhSetStringSetting(L"ProcPropPage", text);
                }
            }
        }
        break;
    case WM_COMMAND:
        {
            switch (LOWORD(wParam))
            {
            case IDOK:
                // Prevent the OK button from working (even though 
                // it's already hidden). This prevents the Enter 
                // key from closing the dialog box.
                return 0;
            }
        }
        break;
    case WM_SIZE:
        {
            if (!IsIconic(hwnd))
            {
                PhLayoutManagerLayout((PPH_LAYOUT_MANAGER)GetProp(hwnd, L"LayoutManager"));
            }
        }
        break;
    case WM_SIZING:
        {
            PhResizingMinimumSize((PRECT)lParam, wParam, 460, 530);
        }
        break;
    }

    return CallWindowProc(oldWndProc, hwnd, uMsg, wParam, lParam);
}

BOOLEAN PhpInitializePropSheetLayoutStage1(
    __in HWND hwnd
    )
{
    if (!GetProp(hwnd, L"LayoutInitialized"))
    {
        PPH_LAYOUT_MANAGER layoutManager;
        HWND tabControlHandle;
        PPH_LAYOUT_ITEM tabControlItem;
        PPH_LAYOUT_ITEM tabPageItem;

        layoutManager = (PPH_LAYOUT_MANAGER)GetProp(hwnd, L"LayoutManager");

        tabControlHandle = PropSheet_GetTabControl(hwnd);
        tabControlItem = PhAddLayoutItem(layoutManager, tabControlHandle,
            NULL, PH_ANCHOR_ALL | PH_LAYOUT_IMMEDIATE_RESIZE);
        tabPageItem = PhAddLayoutItem(layoutManager, tabControlHandle,
            NULL, PH_LAYOUT_TAB_CONTROL); // dummy item to fix multiline tab control

        SetProp(hwnd, L"TabPageItem", (HANDLE)tabPageItem);

        PhAddLayoutItem(layoutManager, GetDlgItem(hwnd, IDCANCEL),
            NULL, PH_ANCHOR_RIGHT | PH_ANCHOR_BOTTOM);

        // Hide the OK button.
        ShowWindow(GetDlgItem(hwnd, IDOK), SW_HIDE);
        // Set the Cancel button's text to "Close".
        SetDlgItemText(hwnd, IDCANCEL, L"Close");

        SetProp(hwnd, L"LayoutInitialized", (HANDLE)TRUE);

        return TRUE;
    }

    return FALSE;
}

VOID PhpInitializePropSheetLayoutStage2(
    __in HWND hwnd
    )
{
    PH_RECTANGLE windowRectangle;

    windowRectangle.Position = PhGetIntegerPairSetting(L"ProcPropPosition");
    windowRectangle.Size = PhGetIntegerPairSetting(L"ProcPropSize");
    PhAdjustRectangleToWorkingArea(hwnd, &windowRectangle);

    MoveWindow(hwnd, windowRectangle.Left, windowRectangle.Top,
        windowRectangle.Width, windowRectangle.Height, FALSE);

    // Implement cascading by saving an offsetted rectangle.
    windowRectangle.Left += 20;
    windowRectangle.Top += 20;

    PhSetIntegerPairSetting(L"ProcPropPosition", windowRectangle.Position);
    PhSetIntegerPairSetting(L"ProcPropSize", windowRectangle.Size);
}

BOOLEAN PhAddProcessPropPage(
    __inout PPH_PROCESS_PROPCONTEXT PropContext,
    __in __assumeRefs(1) PPH_PROCESS_PROPPAGECONTEXT PropPageContext
    )
{
    HPROPSHEETPAGE propSheetPageHandle;

    if (PropContext->PropSheetHeader.nPages == PH_PROCESS_PROPCONTEXT_MAXPAGES)
        return FALSE;

    propSheetPageHandle = CreatePropertySheetPage(
        &PropPageContext->PropSheetPage
        );
    // CreatePropertySheetPage would have sent PSPCB_ADDREF, 
    // which would have added a reference.
    PhDereferenceObject(PropPageContext);

    PropPageContext->PropContext = PropContext;
    PhReferenceObject(PropContext);

    PropContext->PropSheetPages[PropContext->PropSheetHeader.nPages] =
        propSheetPageHandle;
    PropContext->PropSheetHeader.nPages++;

    return TRUE;
}

BOOLEAN PhAddProcessPropPage2(
    __inout PPH_PROCESS_PROPCONTEXT PropContext,
    __in HPROPSHEETPAGE PropSheetPageHandle
    )
{
    if (PropContext->PropSheetHeader.nPages == PH_PROCESS_PROPCONTEXT_MAXPAGES)
        return FALSE;

    PropContext->PropSheetPages[PropContext->PropSheetHeader.nPages] =
        PropSheetPageHandle;
    PropContext->PropSheetHeader.nPages++;

    return TRUE;
}

PPH_PROCESS_PROPPAGECONTEXT PhCreateProcessPropPageContext(
    __in LPCWSTR Template,
    __in DLGPROC DlgProc,
    __in_opt PVOID Context
    )
{
    return PhCreateProcessPropPageContextEx(NULL, Template, DlgProc, Context);
}

PPH_PROCESS_PROPPAGECONTEXT PhCreateProcessPropPageContextEx(
    __in_opt PVOID InstanceHandle,
    __in LPCWSTR Template,
    __in DLGPROC DlgProc,
    __in_opt PVOID Context
    )
{
    PPH_PROCESS_PROPPAGECONTEXT propPageContext;

    if (!NT_SUCCESS(PhCreateObject(
        &propPageContext,
        sizeof(PH_PROCESS_PROPPAGECONTEXT),
        0,
        PhpProcessPropPageContextType
        )))
        return NULL;

    memset(propPageContext, 0, sizeof(PH_PROCESS_PROPPAGECONTEXT));

    propPageContext->PropSheetPage.dwSize = sizeof(PROPSHEETPAGE);
    propPageContext->PropSheetPage.dwFlags =
        PSP_USECALLBACK;
    propPageContext->PropSheetPage.hInstance = InstanceHandle;
    propPageContext->PropSheetPage.pszTemplate = Template;
    propPageContext->PropSheetPage.pfnDlgProc = DlgProc;
    propPageContext->PropSheetPage.lParam = (LPARAM)propPageContext;
    propPageContext->PropSheetPage.pfnCallback = PhpStandardPropPageProc;

    propPageContext->Context = Context;

    return propPageContext;
}

VOID NTAPI PhpProcessPropPageContextDeleteProcedure(
    __in PVOID Object,
    __in ULONG Flags
    )
{
    PPH_PROCESS_PROPPAGECONTEXT propPageContext = (PPH_PROCESS_PROPPAGECONTEXT)Object;

    if (propPageContext->PropContext)
        PhDereferenceObject(propPageContext->PropContext);
}

INT CALLBACK PhpStandardPropPageProc(
    __in HWND hwnd,
    __in UINT uMsg,
    __in LPPROPSHEETPAGE ppsp
    )
{
    PPH_PROCESS_PROPPAGECONTEXT propPageContext;

    propPageContext = (PPH_PROCESS_PROPPAGECONTEXT)ppsp->lParam;

    if (uMsg == PSPCB_ADDREF)
        PhReferenceObject(propPageContext);
    else if (uMsg == PSPCB_RELEASE)
        PhDereferenceObject(propPageContext);

    return 1;
}

FORCEINLINE BOOLEAN PhpPropPageDlgProcHeader(
    __in HWND hwndDlg,
    __in UINT uMsg,
    __in LPARAM lParam,
    __out LPPROPSHEETPAGE *PropSheetPage,
    __out PPH_PROCESS_PROPPAGECONTEXT *PropPageContext,
    __out PPH_PROCESS_ITEM *ProcessItem
    )
{
    LPPROPSHEETPAGE propSheetPage;
    PPH_PROCESS_PROPPAGECONTEXT propPageContext;

    if (uMsg == WM_INITDIALOG)
    {
        // Save the context.
        SetProp(hwndDlg, L"PropSheetPage", (HANDLE)lParam);
    }

    propSheetPage = (LPPROPSHEETPAGE)GetProp(hwndDlg, L"PropSheetPage");

    if (!propSheetPage)
        return FALSE;

    *PropSheetPage = propSheetPage;
    *PropPageContext = propPageContext = (PPH_PROCESS_PROPPAGECONTEXT)propSheetPage->lParam;
    *ProcessItem = propPageContext->PropContext->ProcessItem;

    return TRUE;
}

BOOLEAN PhPropPageDlgProcHeader(
    __in HWND hwndDlg,
    __in UINT uMsg,
    __in LPARAM lParam,
    __out LPPROPSHEETPAGE *PropSheetPage,
    __out PPH_PROCESS_PROPPAGECONTEXT *PropPageContext,
    __out PPH_PROCESS_ITEM *ProcessItem
    )
{
    return PhpPropPageDlgProcHeader(hwndDlg, uMsg, lParam, PropSheetPage, PropPageContext, ProcessItem);
}

VOID PhpPropPageDlgProcDestroy(
    __in HWND hwndDlg
    )
{
    RemoveProp(hwndDlg, L"PropSheetPage");
}

VOID PhPropPageDlgProcDestroy(
    __in HWND hwndDlg
    )
{
    PhpPropPageDlgProcDestroy(hwndDlg);
}

PPH_LAYOUT_ITEM PhAddPropPageLayoutItem(
    __in HWND hwnd,
    __in HWND Handle,
    __in PPH_LAYOUT_ITEM ParentItem,
    __in ULONG Anchor
    )
{
    HWND parent;
    PPH_LAYOUT_MANAGER layoutManager;
    PPH_LAYOUT_ITEM realParentItem;
    BOOLEAN doLayoutStage2;
    PPH_LAYOUT_ITEM item;

    parent = GetParent(hwnd);
    layoutManager = (PPH_LAYOUT_MANAGER)GetProp(parent, L"LayoutManager");

    doLayoutStage2 = PhpInitializePropSheetLayoutStage1(parent);

    if (ParentItem != PH_PROP_PAGE_TAB_CONTROL_PARENT)
        realParentItem = ParentItem;
    else
        realParentItem = (PPH_LAYOUT_ITEM)GetProp(parent, L"TabPageItem");

    // Use the HACK if the control is a direct child of the dialog.
    if (ParentItem && ParentItem != PH_PROP_PAGE_TAB_CONTROL_PARENT &&
        // We detect if ParentItem is the layout item for the dialog 
        // by looking at its parent.
        (ParentItem->ParentItem == &layoutManager->RootItem || 
        (ParentItem->ParentItem->Anchor & PH_LAYOUT_TAB_CONTROL)))
    {
        RECT dialogRect;
        RECT dialogSize;
        RECT margin;

        // MAKE SURE THESE NUMBERS ARE CORRECT.
        dialogSize.right = 260;
        dialogSize.bottom = 260;
        MapDialogRect(hwnd, &dialogSize);

        // Get the original dialog rectangle.
        GetWindowRect(hwnd, &dialogRect);
        dialogRect.right = dialogRect.left + dialogSize.right;
        dialogRect.bottom = dialogRect.top + dialogSize.bottom;

        // Calculate the margin from the original rectangle.
        GetWindowRect(Handle, &margin);
        margin = PhMapRect(margin, dialogRect);
        PhConvertRect(&margin, &dialogRect);

        item = PhAddLayoutItemEx(layoutManager, Handle, realParentItem, Anchor, margin);
    }
    else
    {
        item = PhAddLayoutItem(layoutManager, Handle, realParentItem, Anchor);
    }

    if (doLayoutStage2)
        PhpInitializePropSheetLayoutStage2(parent);

    return item;
}

VOID PhDoPropPageLayout(
    __in HWND hwnd
    )
{
    HWND parent;
    PPH_LAYOUT_MANAGER layoutManager;

    parent = GetParent(hwnd);
    layoutManager = (PPH_LAYOUT_MANAGER)GetProp(parent, L"LayoutManager");
    PhLayoutManagerLayout(layoutManager);
}

NTSTATUS PhpProcessGeneralOpenProcess(
    __out PHANDLE Handle,
    __in ACCESS_MASK DesiredAccess,
    __in_opt PVOID Context
    )
{
    return PhOpenProcess(Handle, DesiredAccess, (HANDLE)Context);
}

FORCEINLINE PWSTR PhpGetStringOrNa(
    __in PPH_STRING String
    )
{
    if (String)
        return String->Buffer;
    else
        return L"N/A";
}

VOID PhpUpdateProcessDep(
    __in HWND hwndDlg,
    __in PPH_PROCESS_ITEM ProcessItem
    )
{
    HANDLE processHandle;
    ULONG depStatus;

#ifdef _M_X64
    if (ProcessItem->IsWow64)
#else
    if (TRUE)
#endif
    {
        SetDlgItemText(hwndDlg, IDC_DEP, L"N/A");

        if (NT_SUCCESS(PhOpenProcess(
            &processHandle,
            PROCESS_QUERY_INFORMATION,
            ProcessItem->ProcessId
            )))
        {
            if (NT_SUCCESS(PhGetProcessDepStatus(processHandle, &depStatus)))
            {
                PPH_STRING depString;

                if (depStatus & PH_PROCESS_DEP_ENABLED)
                    depString = PhaCreateString(L"Enabled");
                else
                    depString = PhaCreateString(L"Disabled");

                if ((depStatus & PH_PROCESS_DEP_ENABLED) &&
                    (depStatus & PH_PROCESS_DEP_ATL_THUNK_EMULATION_DISABLED))
                {
                    depString = PhaConcatStrings2(depString->Buffer, L", DEP-ATL thunk emulation disabled");
                }

                if (depStatus & PH_PROCESS_DEP_PERMANENT)
                {
                    depString = PhaConcatStrings2(depString->Buffer, L", Permanent");
                }

                SetDlgItemText(hwndDlg, IDC_DEP, depString->Buffer);
                EnableWindow(GetDlgItem(hwndDlg, IDC_EDITDEP), !(depStatus & PH_PROCESS_DEP_PERMANENT));
            }

            NtClose(processHandle);
        }
    }
    else
    {
        SetDlgItemText(hwndDlg, IDC_DEP, L"Enabled, Permanent");
    }
}

INT_PTR CALLBACK PhpProcessGeneralDlgProc(
    __in HWND hwndDlg,
    __in UINT uMsg,
    __in WPARAM wParam,
    __in LPARAM lParam
    )
{
    LPPROPSHEETPAGE propSheetPage;
    PPH_PROCESS_PROPPAGECONTEXT propPageContext;
    PPH_PROCESS_ITEM processItem;

    if (!PhpPropPageDlgProcHeader(hwndDlg, uMsg, lParam,
        &propSheetPage, &propPageContext, &processItem))
        return FALSE;

    switch (uMsg)
    {
    case WM_INITDIALOG:
        {
            HANDLE processHandle = NULL;
            PPH_STRING curdir = NULL;
            PROCESS_BASIC_INFORMATION basicInfo;
#ifdef _M_X64
            PVOID peb32;
#endif
            PPH_PROCESS_ITEM parentProcess;
            CLIENT_ID clientId;
            BOOLEAN isProtected;

            {
                HBITMAP folder;
                HBITMAP magnifier;
                HBITMAP pencil;

                folder = PH_LOAD_SHARED_IMAGE(MAKEINTRESOURCE(IDB_FOLDER), IMAGE_BITMAP);
                magnifier = PH_LOAD_SHARED_IMAGE(MAKEINTRESOURCE(IDB_MAGNIFIER), IMAGE_BITMAP);
                pencil = PH_LOAD_SHARED_IMAGE(MAKEINTRESOURCE(IDB_PENCIL), IMAGE_BITMAP);

                SET_BUTTON_BITMAP(IDC_OPENFILENAME, folder);
                SET_BUTTON_BITMAP(IDC_VIEWPARENTPROCESS, magnifier);
                SET_BUTTON_BITMAP(IDC_EDITDEP, pencil);
                SET_BUTTON_BITMAP(IDC_EDITPROTECTION, pencil);
            }

            // File

            SendMessage(GetDlgItem(hwndDlg, IDC_FILEICON), STM_SETICON,
                (WPARAM)processItem->LargeIcon, 0);

            if (PH_IS_REAL_PROCESS_ID(processItem->ProcessId))
            {
                SetDlgItemText(hwndDlg, IDC_NAME, PhpGetStringOrNa(processItem->VersionInfo.FileDescription));
            }
            else
            {
                SetDlgItemText(hwndDlg, IDC_NAME, processItem->ProcessName->Buffer);
            }

            SetDlgItemText(hwndDlg, IDC_VERSION, PhpGetStringOrNa(processItem->VersionInfo.FileVersion));
            SetDlgItemText(hwndDlg, IDC_FILENAME, PhpGetStringOrNa(processItem->FileName));

            if (!processItem->FileName)
                EnableWindow(GetDlgItem(hwndDlg, IDC_OPENFILENAME), FALSE);

            if (processItem->VerifyResult == VrTrusted)
            {
                if (processItem->VerifySignerName)
                {
                    SetDlgItemText(hwndDlg, IDC_COMPANYNAME,
                        PhaConcatStrings2(L"(Verified) ", processItem->VerifySignerName->Buffer)->Buffer);
                }
                else
                {
                    SetDlgItemText(hwndDlg, IDC_COMPANYNAME,
                        PhaConcatStrings2(
                        L"(Verified) ",
                        PhGetStringOrEmpty(processItem->VersionInfo.CompanyName)
                        )->Buffer);
                }
            }
            else
            {
                SetDlgItemText(hwndDlg, IDC_COMPANYNAME,
                    PhpGetStringOrNa(processItem->VersionInfo.CompanyName));
            }

            // Command Line

            SetDlgItemText(hwndDlg, IDC_CMDLINE, PhpGetStringOrNa(processItem->CommandLine));

            // Current Directory

            if (NT_SUCCESS(PhOpenProcess(
                &processHandle,
                ProcessQueryAccess | PROCESS_VM_READ,
                processItem->ProcessId
                )))
            {
                PH_PEB_OFFSET pebOffset;

                pebOffset = PhpoCurrentDirectory;

#ifdef _M_X64
                // Tell the function to get the WOW64 current directory, because that's 
                // the one that actually gets updated.
                if (processItem->IsWow64)
                    pebOffset |= PhpoWow64;
#endif

                PhGetProcessPebString(
                    processHandle,
                    pebOffset,
                    &curdir
                    );

                NtClose(processHandle);
                processHandle = NULL;
            }

            SetDlgItemText(hwndDlg, IDC_CURDIR,
                PhpGetStringOrNa(curdir));

            if (curdir)
                PhDereferenceObject(curdir);

            // Started

            if (processItem->CreateTime.QuadPart != 0)
            {
                LARGE_INTEGER startTime;
                LARGE_INTEGER currentTime;
                SYSTEMTIME startTimeFields;
                PPH_STRING startTimeRelativeString;
                PPH_STRING startTimeString;

                startTime = processItem->CreateTime;
                PhQuerySystemTime(&currentTime);
                startTimeRelativeString = PHA_DEREFERENCE(PhFormatTimeSpanRelative(currentTime.QuadPart - startTime.QuadPart));

                PhLargeIntegerToLocalSystemTime(&startTimeFields, &startTime);
                startTimeString = PhaFormatDateTime(&startTimeFields);

                SetDlgItemText(hwndDlg, IDC_STARTED,
                    PhaFormatString(L"%s (%s)", startTimeRelativeString->Buffer, startTimeString->Buffer)->Buffer);
            }
            else
            {
                SetDlgItemText(hwndDlg, IDC_STARTED, L"N/A");
            }

            // Parent

            if (parentProcess = PhReferenceProcessItemForParent(
                processItem->ParentProcessId,
                processItem->ProcessId,
                &processItem->CreateTime
                ))
            {
                clientId.UniqueProcess = parentProcess->ProcessId;
                clientId.UniqueThread = NULL;

                SetDlgItemText(hwndDlg, IDC_PARENTPROCESS,
                    ((PPH_STRING)PHA_DEREFERENCE(PhGetClientIdNameEx(&clientId, parentProcess->ProcessName)))->Buffer);

                PhDereferenceObject(parentProcess);
            }
            else
            {
                SetDlgItemText(hwndDlg, IDC_PARENTPROCESS,
                    PhaFormatString(L"Non-existent process (%u)", (ULONG)processItem->ParentProcessId)->Buffer);
                EnableWindow(GetDlgItem(hwndDlg, IDC_VIEWPARENTPROCESS), FALSE);
            }

            // DEP

            PhpUpdateProcessDep(hwndDlg, processItem);

            // PEB address

            SetDlgItemText(hwndDlg, IDC_PEBADDRESS, L"N/A");

            PhOpenProcess(
                &processHandle,
                ProcessQueryAccess,
                processItem->ProcessId
                );

            if (processHandle)
            {
                PhGetProcessBasicInformation(processHandle, &basicInfo);
#ifdef _M_X64
                if (processItem->IsWow64)
                {
                    PhGetProcessPeb32(processHandle, &peb32);
                    SetDlgItemText(hwndDlg, IDC_PEBADDRESS,
                        PhaFormatString(L"0x%Ix (32-bit: 0x%x)", basicInfo.PebBaseAddress, (ULONG)peb32)->Buffer);
                }
                else
                {
#endif

                SetDlgItemText(hwndDlg, IDC_PEBADDRESS,
                    PhaFormatString(L"0x%Ix", basicInfo.PebBaseAddress)->Buffer);
#ifdef _M_X64
                }
#endif
            }

            // Protected

            SetDlgItemText(hwndDlg, IDC_PROTECTION, L"N/A");

            if (WINDOWS_HAS_LIMITED_ACCESS)
            {
                if (PhKphHandle)
                {
                    if (NT_SUCCESS(KphGetProcessProtected(PhKphHandle, processItem->ProcessId, &isProtected)))
                    {
                        SetDlgItemText(hwndDlg, IDC_PROTECTION, isProtected ? L"Protected" : L"Not Protected");
                        EnableWindow(GetDlgItem(hwndDlg, IDC_EDITPROTECTION), TRUE);
                    }
                }
                else if (processHandle)
                {
                    PROCESS_EXTENDED_BASIC_INFORMATION extendedBasicInfo;

                    if (NT_SUCCESS(PhGetProcessExtendedBasicInformation(
                        processHandle,
                        &extendedBasicInfo
                        )))
                    {
                        SetDlgItemText(hwndDlg, IDC_PROTECTION,
                            extendedBasicInfo.IsProtectedProcess ? L"Protected" : L"Not Protected");
                    }
                }
            }
            else
            {
                EnableWindow(GetDlgItem(hwndDlg, IDC_PROTECTION), FALSE);
            }

            if (processHandle)
                NtClose(processHandle);

#ifdef _M_X64
            if (processItem->IsWow64)
                SetDlgItemText(hwndDlg, IDC_PROCESSTYPETEXT, L"32-bit");
            else
                SetDlgItemText(hwndDlg, IDC_PROCESSTYPETEXT, L"64-bit");

            ShowWindow(GetDlgItem(hwndDlg, IDC_PROCESSTYPELABEL), SW_SHOW);
            ShowWindow(GetDlgItem(hwndDlg, IDC_PROCESSTYPETEXT), SW_SHOW);
#endif
        }
        break;
    case WM_DESTROY:
        {
            PhpPropPageDlgProcDestroy(hwndDlg);
        }
        break;
    case WM_SHOWWINDOW:
        {
            if (!propPageContext->LayoutInitialized)
            {
                PPH_LAYOUT_ITEM dialogItem;

                dialogItem = PhAddPropPageLayoutItem(hwndDlg, hwndDlg,
                    PH_PROP_PAGE_TAB_CONTROL_PARENT, PH_ANCHOR_ALL);
                PhAddPropPageLayoutItem(hwndDlg, GetDlgItem(hwndDlg, IDC_FILE),
                    dialogItem, PH_ANCHOR_LEFT | PH_ANCHOR_TOP | PH_ANCHOR_RIGHT);
                PhAddPropPageLayoutItem(hwndDlg, GetDlgItem(hwndDlg, IDC_NAME),
                    dialogItem, PH_ANCHOR_LEFT | PH_ANCHOR_TOP | PH_ANCHOR_RIGHT);
                PhAddPropPageLayoutItem(hwndDlg, GetDlgItem(hwndDlg, IDC_COMPANYNAME),
                    dialogItem, PH_ANCHOR_LEFT | PH_ANCHOR_TOP | PH_ANCHOR_RIGHT);
                PhAddPropPageLayoutItem(hwndDlg, GetDlgItem(hwndDlg, IDC_VERSION),
                    dialogItem, PH_ANCHOR_LEFT | PH_ANCHOR_TOP | PH_ANCHOR_RIGHT);
                PhAddPropPageLayoutItem(hwndDlg, GetDlgItem(hwndDlg, IDC_FILENAME),
                    dialogItem, PH_ANCHOR_LEFT | PH_ANCHOR_TOP | PH_ANCHOR_RIGHT);
                PhAddPropPageLayoutItem(hwndDlg, GetDlgItem(hwndDlg, IDC_OPENFILENAME),
                    dialogItem, PH_ANCHOR_TOP | PH_ANCHOR_RIGHT);
                PhAddPropPageLayoutItem(hwndDlg, GetDlgItem(hwndDlg, IDC_CMDLINE),
                    dialogItem, PH_ANCHOR_LEFT | PH_ANCHOR_TOP | PH_ANCHOR_RIGHT);
                PhAddPropPageLayoutItem(hwndDlg, GetDlgItem(hwndDlg, IDC_CURDIR),
                    dialogItem, PH_ANCHOR_LEFT | PH_ANCHOR_TOP | PH_ANCHOR_RIGHT);
                PhAddPropPageLayoutItem(hwndDlg, GetDlgItem(hwndDlg, IDC_STARTED),
                    dialogItem, PH_ANCHOR_LEFT | PH_ANCHOR_TOP | PH_ANCHOR_RIGHT);
                PhAddPropPageLayoutItem(hwndDlg, GetDlgItem(hwndDlg, IDC_PEBADDRESS),
                    dialogItem, PH_ANCHOR_LEFT | PH_ANCHOR_TOP | PH_ANCHOR_RIGHT);
                PhAddPropPageLayoutItem(hwndDlg, GetDlgItem(hwndDlg, IDC_PARENTPROCESS),
                    dialogItem, PH_ANCHOR_LEFT | PH_ANCHOR_TOP | PH_ANCHOR_RIGHT);
                PhAddPropPageLayoutItem(hwndDlg, GetDlgItem(hwndDlg, IDC_VIEWPARENTPROCESS),
                    dialogItem, PH_ANCHOR_TOP | PH_ANCHOR_RIGHT);
                PhAddPropPageLayoutItem(hwndDlg, GetDlgItem(hwndDlg, IDC_DEP),
                    dialogItem, PH_ANCHOR_LEFT | PH_ANCHOR_TOP | PH_ANCHOR_RIGHT);
                PhAddPropPageLayoutItem(hwndDlg, GetDlgItem(hwndDlg, IDC_EDITDEP),
                    dialogItem, PH_ANCHOR_TOP | PH_ANCHOR_RIGHT);
                PhAddPropPageLayoutItem(hwndDlg, GetDlgItem(hwndDlg, IDC_PROTECTION),
                    dialogItem, PH_ANCHOR_LEFT | PH_ANCHOR_TOP | PH_ANCHOR_RIGHT);
                PhAddPropPageLayoutItem(hwndDlg, GetDlgItem(hwndDlg, IDC_EDITPROTECTION),
                    dialogItem, PH_ANCHOR_TOP | PH_ANCHOR_RIGHT);
                PhAddPropPageLayoutItem(hwndDlg, GetDlgItem(hwndDlg, IDC_TERMINATE),
                    dialogItem, PH_ANCHOR_RIGHT | PH_ANCHOR_TOP);
                PhAddPropPageLayoutItem(hwndDlg, GetDlgItem(hwndDlg, IDC_PERMISSIONS),
                    dialogItem, PH_ANCHOR_RIGHT | PH_ANCHOR_TOP);
                PhAddPropPageLayoutItem(hwndDlg, GetDlgItem(hwndDlg, IDC_PROCESS),
                    dialogItem, PH_ANCHOR_ALL);

                PhDoPropPageLayout(hwndDlg);

                propPageContext->LayoutInitialized = TRUE;
            }
        }
        break;
    case WM_COMMAND:
        {
            INT id = LOWORD(wParam);

            switch (id)
            {
            case IDC_OPENFILENAME:
                {
                    if (processItem->FileName)
                        PhShellExploreFile(hwndDlg, processItem->FileName->Buffer);
                }
                break;
            case IDC_VIEWPARENTPROCESS:
                {
                    PPH_PROCESS_ITEM parentProcessItem;

                    if (parentProcessItem = PhReferenceProcessItem(processItem->ParentProcessId))
                    {
                        ProcessHacker_ShowProcessProperties(PhMainWndHandle, parentProcessItem);
                        PhDereferenceObject(parentProcessItem);
                    }
                    else
                    {
                        PhShowError(hwndDlg, L"The process does not exist.");
                    }
                }
                break;
            case IDC_EDITDEP:
                {
                    if (PhUiSetDepStatusProcess(hwndDlg, processItem))
                        PhpUpdateProcessDep(hwndDlg, processItem);
                }
                break;
            case IDC_EDITPROTECTION:
                {
                    if (PhUiSetProtectionProcess(hwndDlg, processItem))
                    {
                        BOOLEAN isProtected;

                        if (NT_SUCCESS(KphGetProcessProtected(PhKphHandle, processItem->ProcessId, &isProtected)))
                        {
                            SetDlgItemText(hwndDlg, IDC_PROTECTION,
                                isProtected ? L"Protected" : L"Not Protected");
                        }
                    }
                }
                break;
            case IDC_TERMINATE:
                {
                    PhUiTerminateProcesses(
                        hwndDlg,
                        &processItem,
                        1
                        );
                }
                break;
            case IDC_PERMISSIONS:
                {
                    PH_STD_OBJECT_SECURITY stdObjectSecurity;
                    PPH_ACCESS_ENTRY accessEntries;
                    ULONG numberOfAccessEntries;

                    stdObjectSecurity.OpenObject = PhpProcessGeneralOpenProcess;
                    stdObjectSecurity.ObjectType = L"Process";
                    stdObjectSecurity.Context = processItem->ProcessId;

                    if (PhGetAccessEntries(L"Process", &accessEntries, &numberOfAccessEntries))
                    {
                        PhEditSecurity(
                            hwndDlg,
                            processItem->ProcessName->Buffer,
                            PhStdGetObjectSecurity,
                            PhStdSetObjectSecurity,
                            &stdObjectSecurity,
                            accessEntries,
                            numberOfAccessEntries
                            );
                        PhFree(accessEntries);
                    }
                }
                break;
            }
        }
        break;
    }

    return FALSE;
}

static VOID NTAPI StatisticsUpdateHandler(
    __in_opt PVOID Parameter,
    __in_opt PVOID Context
    )
{
    PPH_STATISTICS_CONTEXT statisticsContext = (PPH_STATISTICS_CONTEXT)Context;

    if (statisticsContext->Enabled)
        PostMessage(statisticsContext->WindowHandle, WM_PH_STATISTICS_UPDATE, 0, 0);
}

VOID PhpUpdateProcessStatistics(
    __in HWND hwndDlg,
    __in PPH_PROCESS_ITEM ProcessItem
    )
{
    WCHAR timeSpan[PH_TIMESPAN_STR_LEN_1];

    SetDlgItemInt(hwndDlg, IDC_ZPRIORITY_V, ProcessItem->BasePriority, TRUE); // priority
    PhPrintTimeSpan(timeSpan, ProcessItem->KernelTime.QuadPart, PH_TIMESPAN_HMSM); // kernel time
    SetDlgItemText(hwndDlg, IDC_ZKERNELTIME_V, timeSpan);
    PhPrintTimeSpan(timeSpan, ProcessItem->UserTime.QuadPart, PH_TIMESPAN_HMSM); // user time
    SetDlgItemText(hwndDlg, IDC_ZUSERTIME_V, timeSpan);
    PhPrintTimeSpan(timeSpan,
        ProcessItem->KernelTime.QuadPart + ProcessItem->UserTime.QuadPart, PH_TIMESPAN_HMSM); // total time
    SetDlgItemText(hwndDlg, IDC_ZTOTALTIME_V, timeSpan);

    SetDlgItemText(hwndDlg, IDC_ZPRIVATEBYTES_V, 
        PhaFormatSize(ProcessItem->VmCounters.PagefileUsage, -1)->Buffer); // private bytes (same as PrivateUsage)
    SetDlgItemText(hwndDlg, IDC_ZPEAKPRIVATEBYTES_V, 
        PhaFormatSize(ProcessItem->VmCounters.PeakPagefileUsage, -1)->Buffer); // peak private bytes
    SetDlgItemText(hwndDlg, IDC_ZWORKINGSET_V, 
        PhaFormatSize(ProcessItem->VmCounters.WorkingSetSize, -1)->Buffer); // working set
    SetDlgItemText(hwndDlg, IDC_ZPEAKWORKINGSET_V, 
        PhaFormatSize(ProcessItem->VmCounters.PeakWorkingSetSize, -1)->Buffer); // peak working set
    SetDlgItemText(hwndDlg, IDC_ZVIRTUALSIZE_V, 
        PhaFormatSize(ProcessItem->VmCounters.VirtualSize, -1)->Buffer); // virtual size
    SetDlgItemText(hwndDlg, IDC_ZPEAKVIRTUALSIZE_V, 
        PhaFormatSize(ProcessItem->VmCounters.PeakVirtualSize, -1)->Buffer); // peak virtual size
    SetDlgItemText(hwndDlg, IDC_ZPAGEFAULTS_V, 
        PhaFormatUInt64(ProcessItem->VmCounters.PageFaultCount, TRUE)->Buffer); // page faults

    SetDlgItemText(hwndDlg, IDC_ZIOREADS_V, 
        PhaFormatUInt64(ProcessItem->IoCounters.ReadOperationCount, TRUE)->Buffer); // reads
    SetDlgItemText(hwndDlg, IDC_ZIOREADBYTES_V, 
        PhaFormatSize(ProcessItem->IoCounters.ReadTransferCount, -1)->Buffer); // read bytes
    SetDlgItemText(hwndDlg, IDC_ZIOWRITES_V, 
        PhaFormatUInt64(ProcessItem->IoCounters.WriteOperationCount, TRUE)->Buffer); // writes
    SetDlgItemText(hwndDlg, IDC_ZIOWRITEBYTES_V, 
        PhaFormatSize(ProcessItem->IoCounters.WriteTransferCount, -1)->Buffer); // write bytes
    SetDlgItemText(hwndDlg, IDC_ZIOOTHER_V, 
        PhaFormatUInt64(ProcessItem->IoCounters.OtherOperationCount, TRUE)->Buffer); // other
    SetDlgItemText(hwndDlg, IDC_ZIOOTHERBYTES_V, 
        PhaFormatSize(ProcessItem->IoCounters.OtherTransferCount, -1)->Buffer); // read bytes

    SetDlgItemText(hwndDlg, IDC_ZHANDLES_V, 
        PhaFormatUInt64(ProcessItem->NumberOfHandles, TRUE)->Buffer); // handles

    // Optional information
    if (PH_IS_REAL_PROCESS_ID(ProcessItem->ProcessId))
    {
        PPH_STRING peakHandles = NULL;
        PPH_STRING gdiHandles = NULL;
        PPH_STRING userHandles = NULL;
        PPH_STRING cycles = NULL;
        ULONG pagePriority = -1;
        ULONG ioPriority = -1;

        if (ProcessItem->QueryHandle)
        {
            ULONG64 cycleTime;

            if (WindowsVersion >= WINDOWS_7)
            {
                PROCESS_HANDLE_COUNT_EX handleCount;

                if (NT_SUCCESS(NtQueryInformationProcess(
                    ProcessItem->QueryHandle,
                    ProcessHandleCount,
                    &handleCount,
                    sizeof(PROCESS_HANDLE_COUNT_EX),
                    NULL
                    )))
                {
                    peakHandles = PhaFormatUInt64(handleCount.PeakHandleCount, TRUE);
                }
            }

            gdiHandles = PhaFormatUInt64(GetGuiResources(ProcessItem->QueryHandle, GR_GDIOBJECTS), TRUE); // GDI handles
            userHandles = PhaFormatUInt64(GetGuiResources(ProcessItem->QueryHandle, GR_USEROBJECTS), TRUE); // USER handles

            if (WINDOWS_HAS_CYCLE_TIME &&
                NT_SUCCESS(PhGetProcessCycleTime(ProcessItem->QueryHandle, &cycleTime)))
            {
                cycles = PhaFormatUInt64(cycleTime, TRUE);
            }

            if (WindowsVersion >= WINDOWS_VISTA)
            {
                PhGetProcessPagePriority(ProcessItem->QueryHandle, &pagePriority);
                PhGetProcessIoPriority(ProcessItem->QueryHandle, &ioPriority); 
            }
        }

        if (WindowsVersion >= WINDOWS_7)
            SetDlgItemText(hwndDlg, IDC_ZPEAKHANDLES_V, PhGetStringOrDefault(peakHandles, L"Unknown"));
        else
            SetDlgItemText(hwndDlg, IDC_ZPEAKHANDLES_V, L"N/A");

        SetDlgItemText(hwndDlg, IDC_ZGDIHANDLES_V, PhGetStringOrDefault(gdiHandles, L"Unknown"));
        SetDlgItemText(hwndDlg, IDC_ZUSERHANDLES_V, PhGetStringOrDefault(userHandles, L"Unknown"));
        SetDlgItemText(hwndDlg, IDC_ZCYCLES_V,
            PhGetStringOrDefault(cycles, WINDOWS_HAS_CYCLE_TIME ? L"Unknown" : L"N/A"));

        if (WindowsVersion >= WINDOWS_VISTA)
        {
            if (pagePriority != -1)
                SetDlgItemInt(hwndDlg, IDC_ZPAGEPRIORITY_V, pagePriority, FALSE);
            else
                SetDlgItemText(hwndDlg, IDC_ZPAGEPRIORITY_V, L"Unknown");

            if (ioPriority != -1)
                SetDlgItemInt(hwndDlg, IDC_ZIOPRIORITY_V, ioPriority, FALSE);
            else
                SetDlgItemText(hwndDlg, IDC_ZIOPRIORITY_V, L"Unknown");
        }
        else
        {
            SetDlgItemText(hwndDlg, IDC_ZPAGEPRIORITY_V, L"N/A");
            SetDlgItemText(hwndDlg, IDC_ZIOPRIORITY_V, L"N/A");
        }
    }
    else
    {
        SetDlgItemText(hwndDlg, IDC_ZPEAKHANDLES_V, L"N/A");
        SetDlgItemText(hwndDlg, IDC_ZGDIHANDLES_V, L"N/A");
        SetDlgItemText(hwndDlg, IDC_ZUSERHANDLES_V, L"N/A");
        SetDlgItemText(hwndDlg, IDC_ZCYCLES_V, L"N/A");
        SetDlgItemText(hwndDlg, IDC_ZPAGEPRIORITY_V, L"N/A");
        SetDlgItemText(hwndDlg, IDC_ZIOPRIORITY_V, L"N/A");
    }
}

INT_PTR CALLBACK PhpProcessStatisticsDlgProc(
    __in HWND hwndDlg,
    __in UINT uMsg,
    __in WPARAM wParam,
    __in LPARAM lParam
    )
{
    LPPROPSHEETPAGE propSheetPage;
    PPH_PROCESS_PROPPAGECONTEXT propPageContext;
    PPH_PROCESS_ITEM processItem;
    PPH_STATISTICS_CONTEXT statisticsContext;

    if (PhpPropPageDlgProcHeader(hwndDlg, uMsg, lParam,
        &propSheetPage, &propPageContext, &processItem))
    {
        statisticsContext = (PPH_STATISTICS_CONTEXT)propPageContext->Context;
    }
    else
    {
        return FALSE;
    }

    switch (uMsg)
    {
    case WM_INITDIALOG:
        {
            statisticsContext = propPageContext->Context =
                PhAllocate(sizeof(PH_STATISTICS_CONTEXT));

            statisticsContext->WindowHandle = hwndDlg;
            statisticsContext->Enabled = TRUE;

            PhRegisterCallback(
                &PhProcessesUpdatedEvent,
                StatisticsUpdateHandler,
                statisticsContext,
                &statisticsContext->ProcessesUpdatedRegistration
                );

            PhpUpdateProcessStatistics(hwndDlg, processItem);
        }
        break;
    case WM_DESTROY:
        {
            PhUnregisterCallback(
                &PhProcessesUpdatedEvent,
                &statisticsContext->ProcessesUpdatedRegistration
                );
            PhFree(statisticsContext);

            PhpPropPageDlgProcDestroy(hwndDlg);
        }
        break;
    case WM_SHOWWINDOW:
        {
            if (!propPageContext->LayoutInitialized)
            {
                PPH_LAYOUT_ITEM dialogItem;

                dialogItem = PhAddPropPageLayoutItem(hwndDlg, hwndDlg,
                    PH_PROP_PAGE_TAB_CONTROL_PARENT, PH_ANCHOR_ALL);

                PhDoPropPageLayout(hwndDlg);

                propPageContext->LayoutInitialized = TRUE;
            }
        }
        break;
    case WM_COMMAND:
        {
            switch (LOWORD(wParam))
            {
            case IDC_DETAILS:
                {
                    PhShowHandleStatisticsDialog(hwndDlg, processItem->ProcessId);
                }
                break;
            }
        }
        break;
    case WM_NOTIFY:
        {
            LPNMHDR header = (LPNMHDR)lParam;

            switch (header->code)
            {
            case PSN_SETACTIVE:
                statisticsContext->Enabled = TRUE;
                break;
            case PSN_KILLACTIVE:
                statisticsContext->Enabled = FALSE;
                break;
            }
        }
        break;
    case WM_PH_STATISTICS_UPDATE:
        {
            PhpUpdateProcessStatistics(hwndDlg, processItem);
        }
        break;
    }

    return FALSE;
}

static VOID NTAPI PerformanceUpdateHandler(
    __in_opt PVOID Parameter,
    __in_opt PVOID Context
    )
{
    PPH_PERFORMANCE_CONTEXT performanceContext = (PPH_PERFORMANCE_CONTEXT)Context;

    PostMessage(performanceContext->WindowHandle, WM_PH_PERFORMANCE_UPDATE, 0, 0);
}

INT_PTR CALLBACK PhpProcessPerformanceDlgProc(
    __in HWND hwndDlg,
    __in UINT uMsg,
    __in WPARAM wParam,
    __in LPARAM lParam
    )
{
    LPPROPSHEETPAGE propSheetPage;
    PPH_PROCESS_PROPPAGECONTEXT propPageContext;
    PPH_PROCESS_ITEM processItem;
    PPH_PERFORMANCE_CONTEXT performanceContext;

    if (PhpPropPageDlgProcHeader(hwndDlg, uMsg, lParam,
        &propSheetPage, &propPageContext, &processItem))
    {
        performanceContext = (PPH_PERFORMANCE_CONTEXT)propPageContext->Context;
    }
    else
    {
        return FALSE;
    }

    switch (uMsg)
    {
    case WM_INITDIALOG:
        {
            performanceContext = propPageContext->Context =
                PhAllocate(sizeof(PH_PERFORMANCE_CONTEXT));

            performanceContext->WindowHandle = hwndDlg;

            PhRegisterCallback(
                &PhProcessesUpdatedEvent,
                PerformanceUpdateHandler,
                performanceContext,
                &performanceContext->ProcessesUpdatedRegistration
                );

            // We have already set the group boxes to have WS_EX_TRANSPARENT to fix 
            // the drawing issue that arises when using WS_CLIPCHILDREN. However 
            // in removing the flicker from the graphs the group boxes will now flicker. 
            // It's a good tradeoff since no one stares at the group boxes.
            PhSetWindowStyle(hwndDlg, WS_CLIPCHILDREN, WS_CLIPCHILDREN);

            PhInitializeGraphState(&performanceContext->CpuGraphState);
            PhInitializeGraphState(&performanceContext->PrivateGraphState);
            PhInitializeGraphState(&performanceContext->IoGraphState);

            performanceContext->CpuGraphHandle = PhCreateGraphControl(hwndDlg, IDC_CPU);
            Graph_SetTooltip(performanceContext->CpuGraphHandle, TRUE);
            ShowWindow(performanceContext->CpuGraphHandle, SW_SHOW);
            BringWindowToTop(performanceContext->CpuGraphHandle);

            performanceContext->PrivateGraphHandle = PhCreateGraphControl(hwndDlg, IDC_PRIVATEBYTES);
            Graph_SetTooltip(performanceContext->PrivateGraphHandle, TRUE);
            ShowWindow(performanceContext->PrivateGraphHandle, SW_SHOW);
            BringWindowToTop(performanceContext->PrivateGraphHandle);

            performanceContext->IoGraphHandle = PhCreateGraphControl(hwndDlg, IDC_IO);
            Graph_SetTooltip(performanceContext->IoGraphHandle, TRUE);
            ShowWindow(performanceContext->IoGraphHandle, SW_SHOW);
            BringWindowToTop(performanceContext->IoGraphHandle);
        }
        break;
    case WM_DESTROY:
        {
            PhDeleteGraphState(&performanceContext->CpuGraphState);
            PhDeleteGraphState(&performanceContext->PrivateGraphState);
            PhDeleteGraphState(&performanceContext->IoGraphState);

            PhUnregisterCallback(
                &PhProcessesUpdatedEvent,
                &performanceContext->ProcessesUpdatedRegistration
                );
            PhFree(performanceContext);

            PhpPropPageDlgProcDestroy(hwndDlg);
        }
        break;
    case WM_SHOWWINDOW:
        {
            if (!propPageContext->LayoutInitialized)
            {
                PPH_LAYOUT_ITEM dialogItem;

                dialogItem = PhAddPropPageLayoutItem(hwndDlg, hwndDlg,
                    PH_PROP_PAGE_TAB_CONTROL_PARENT, PH_ANCHOR_ALL);

                PhDoPropPageLayout(hwndDlg);

                propPageContext->LayoutInitialized = TRUE;
            }
        }
        break;
    case WM_NOTIFY:
        {
            LPNMHDR header = (LPNMHDR)lParam;

            switch (header->code)
            {
            case GCN_GETDRAWINFO:
                {
                    PPH_GRAPH_GETDRAWINFO getDrawInfo = (PPH_GRAPH_GETDRAWINFO)header;
                    PPH_GRAPH_DRAW_INFO drawInfo = getDrawInfo->DrawInfo;

                    if (header->hwndFrom == performanceContext->CpuGraphHandle)
                    {
                        if (PhCsGraphShowText)
                        {
                            HDC hdc;

                            PhSwapReference2(&performanceContext->CpuGraphState.TooltipText,
                                PhFormatString(L"%.2f%% (K: %.2f%%, U: %.2f%%)",
                                processItem->CpuUsage * 100,
                                processItem->CpuKernelUsage * 100,
                                processItem->CpuUserUsage * 100
                                ));

                            hdc = Graph_GetBufferedContext(performanceContext->CpuGraphHandle);
                            SelectObject(hdc, PhApplicationFont);
                            PhSetGraphText(hdc, drawInfo, &performanceContext->CpuGraphState.TooltipText->sr,
                                &PhNormalGraphTextMargin, &PhNormalGraphTextPadding, PH_ALIGN_TOP | PH_ALIGN_LEFT);
                        }
                        else
                        {
                            drawInfo->Text.Buffer = NULL;
                        }

                        drawInfo->Flags = PH_GRAPH_USE_GRID | PH_GRAPH_USE_LINE_2;
                        drawInfo->LineColor1 = PhCsColorCpuKernel;
                        drawInfo->LineColor2 = PhCsColorCpuUser;
                        drawInfo->LineBackColor1 = PhHalveColorBrightness(PhCsColorCpuKernel);
                        drawInfo->LineBackColor2 = PhHalveColorBrightness(PhCsColorCpuUser);

                        PhGraphStateGetDrawInfo(
                            &performanceContext->CpuGraphState,
                            getDrawInfo,
                            processItem->CpuKernelHistory.Count
                            );

                        if (!performanceContext->CpuGraphState.Valid)
                        {
                            PhCopyCircularBuffer_FLOAT(&processItem->CpuKernelHistory,
                                performanceContext->CpuGraphState.Data1, drawInfo->LineDataCount);
                            PhCopyCircularBuffer_FLOAT(&processItem->CpuUserHistory,
                                performanceContext->CpuGraphState.Data2, drawInfo->LineDataCount);
                            performanceContext->CpuGraphState.Valid = TRUE;
                        }
                    }
                    else if (header->hwndFrom == performanceContext->PrivateGraphHandle)
                    {
                        if (PhCsGraphShowText)
                        {
                            HDC hdc;

                            PhSwapReference2(&performanceContext->PrivateGraphState.TooltipText,
                                PhConcatStrings2(
                                L"Private Bytes: ",
                                PhaFormatSize(processItem->VmCounters.PagefileUsage, -1)->Buffer
                                ));

                            hdc = Graph_GetBufferedContext(performanceContext->PrivateGraphHandle);
                            SelectObject(hdc, PhApplicationFont);
                            PhSetGraphText(hdc, drawInfo, &performanceContext->PrivateGraphState.TooltipText->sr,
                                &PhNormalGraphTextMargin, &PhNormalGraphTextPadding, PH_ALIGN_TOP | PH_ALIGN_LEFT);
                        }
                        else
                        {
                            drawInfo->Text.Buffer = NULL;
                        }

                        drawInfo->Flags = PH_GRAPH_USE_GRID;
                        drawInfo->LineColor1 = PhCsColorPrivate;
                        drawInfo->LineBackColor1 = PhHalveColorBrightness(PhCsColorPrivate);

                        PhGraphStateGetDrawInfo(
                            &performanceContext->PrivateGraphState,
                            getDrawInfo,
                            processItem->PrivateBytesHistory.Count
                            );

                        if (!performanceContext->PrivateGraphState.Valid)
                        {
                            ULONG i;

                            for (i = 0; i < drawInfo->LineDataCount; i++)
                            {
                                performanceContext->PrivateGraphState.Data1[i] =
                                    (FLOAT)PhGetItemCircularBuffer_SIZE_T(&processItem->PrivateBytesHistory, i);
                            }

                            if (processItem->VmCounters.PeakPagefileUsage != 0)
                            {
                                // Scale the data.
                                PhxfDivideSingle2U(
                                    performanceContext->PrivateGraphState.Data1,
                                    (FLOAT)processItem->VmCounters.PeakPagefileUsage,
                                    drawInfo->LineDataCount
                                    );
                            }

                            performanceContext->PrivateGraphState.Valid = TRUE;
                        }
                    }
                    else if (header->hwndFrom == performanceContext->IoGraphHandle)
                    {
                        if (PhCsGraphShowText)
                        {
                            HDC hdc;

                            PhSwapReference2(&performanceContext->IoGraphState.TooltipText,
                                PhFormatString(
                                L"R+O: %s, W: %s",
                                PhaFormatSize(processItem->IoReadDelta.Delta + processItem->IoOtherDelta.Delta, -1)->Buffer,
                                PhaFormatSize(processItem->IoWriteDelta.Delta, -1)->Buffer
                                ));

                            hdc = Graph_GetBufferedContext(performanceContext->IoGraphHandle);
                            SelectObject(hdc, PhApplicationFont);
                            PhSetGraphText(hdc, drawInfo, &performanceContext->IoGraphState.TooltipText->sr,
                                &PhNormalGraphTextMargin, &PhNormalGraphTextPadding, PH_ALIGN_TOP | PH_ALIGN_LEFT);
                        }
                        else
                        {
                            drawInfo->Text.Buffer = NULL;
                        }

                        drawInfo->Flags = PH_GRAPH_USE_GRID | PH_GRAPH_USE_LINE_2;
                        drawInfo->LineColor1 = PhCsColorIoReadOther;
                        drawInfo->LineColor2 = PhCsColorIoWrite;
                        drawInfo->LineBackColor1 = PhHalveColorBrightness(PhCsColorIoReadOther);
                        drawInfo->LineBackColor2 = PhHalveColorBrightness(PhCsColorIoWrite);

                        PhGraphStateGetDrawInfo(
                            &performanceContext->IoGraphState,
                            getDrawInfo,
                            processItem->IoReadHistory.Count
                            );

                        if (!performanceContext->IoGraphState.Valid)
                        {
                            ULONG i;
                            FLOAT max = 0;

                            for (i = 0; i < drawInfo->LineDataCount; i++)
                            {
                                FLOAT data1;
                                FLOAT data2;

                                performanceContext->IoGraphState.Data1[i] = data1 =
                                    (FLOAT)PhGetItemCircularBuffer_ULONG64(&processItem->IoReadHistory, i) +
                                    (FLOAT)PhGetItemCircularBuffer_ULONG64(&processItem->IoOtherHistory, i);
                                performanceContext->IoGraphState.Data2[i] = data2 =
                                    (FLOAT)PhGetItemCircularBuffer_ULONG64(&processItem->IoWriteHistory, i);

                                if (max < data1 + data2)
                                    max = data1 + data2;
                            }

                            if (max != 0)
                            {
                                // Scale the data.

                                PhxfDivideSingle2U(
                                    performanceContext->IoGraphState.Data1,
                                    max,
                                    drawInfo->LineDataCount
                                    );
                                PhxfDivideSingle2U(
                                    performanceContext->IoGraphState.Data2,
                                    max,
                                    drawInfo->LineDataCount
                                    );
                            }

                            performanceContext->IoGraphState.Valid = TRUE;
                        }
                    }
                }
                break;
            case GCN_GETTOOLTIPTEXT:
                {
                    PPH_GRAPH_GETTOOLTIPTEXT getTooltipText = (PPH_GRAPH_GETTOOLTIPTEXT)lParam;

                    if (
                        header->hwndFrom == performanceContext->CpuGraphHandle &&
                        getTooltipText->Index < getTooltipText->TotalCount
                        )
                    {
                        if (performanceContext->CpuGraphState.TooltipIndex != getTooltipText->Index)
                        {
                            FLOAT cpuKernel;
                            FLOAT cpuUser;

                            cpuKernel = PhGetItemCircularBuffer_FLOAT(&processItem->CpuKernelHistory, getTooltipText->Index);
                            cpuUser = PhGetItemCircularBuffer_FLOAT(&processItem->CpuUserHistory, getTooltipText->Index);

                            PhSwapReference2(&performanceContext->CpuGraphState.TooltipText, PhFormatString(
                                L"%.2f%% (K: %.2f%%, U: %.2f%%)\n%s",
                                (cpuKernel + cpuUser) * 100,
                                cpuKernel * 100,
                                cpuUser * 100,
                                ((PPH_STRING)PHA_DEREFERENCE(PhGetStatisticsTimeString(processItem, getTooltipText->Index)))->Buffer
                                ));
                        }

                        getTooltipText->Text = performanceContext->CpuGraphState.TooltipText->sr;
                    }
                    else if (
                        header->hwndFrom == performanceContext->PrivateGraphHandle &&
                        getTooltipText->Index < getTooltipText->TotalCount
                        )
                    {
                        if (performanceContext->PrivateGraphState.TooltipIndex != getTooltipText->Index)
                        {
                            SIZE_T privateBytes;

                            privateBytes = PhGetItemCircularBuffer_SIZE_T(&processItem->PrivateBytesHistory, getTooltipText->Index);

                            PhSwapReference2(&performanceContext->PrivateGraphState.TooltipText, PhFormatString(
                                L"Private Bytes: %s\n%s",
                                PhaFormatSize(privateBytes, -1)->Buffer,
                                ((PPH_STRING)PHA_DEREFERENCE(PhGetStatisticsTimeString(processItem, getTooltipText->Index)))->Buffer
                                ));
                        }

                        getTooltipText->Text = performanceContext->PrivateGraphState.TooltipText->sr;
                    }
                    else if (
                        header->hwndFrom == performanceContext->IoGraphHandle &&
                        getTooltipText->Index < getTooltipText->TotalCount
                        )
                    {
                        if (performanceContext->IoGraphState.TooltipIndex != getTooltipText->Index)
                        {
                            ULONG64 ioRead;
                            ULONG64 ioWrite;
                            ULONG64 ioOther;

                            ioRead = PhGetItemCircularBuffer_ULONG64(&processItem->IoReadHistory, getTooltipText->Index);
                            ioWrite = PhGetItemCircularBuffer_ULONG64(&processItem->IoWriteHistory, getTooltipText->Index);
                            ioOther = PhGetItemCircularBuffer_ULONG64(&processItem->IoOtherHistory, getTooltipText->Index);

                            PhSwapReference2(&performanceContext->IoGraphState.TooltipText, PhFormatString(
                                L"R: %s\nW: %s\nO: %s\n%s",
                                PhaFormatSize(ioRead, -1)->Buffer,
                                PhaFormatSize(ioWrite, -1)->Buffer,
                                PhaFormatSize(ioOther, -1)->Buffer,
                                ((PPH_STRING)PHA_DEREFERENCE(PhGetStatisticsTimeString(processItem, getTooltipText->Index)))->Buffer
                                ));
                        }

                        getTooltipText->Text = performanceContext->IoGraphState.TooltipText->sr;
                    }
                }
                break;
            }
        }
        break;
    case WM_SIZE:
        {
            HDWP deferHandle;
            HWND cpuGroupBox = GetDlgItem(hwndDlg, IDC_GROUPCPU);
            HWND privateBytesGroupBox = GetDlgItem(hwndDlg, IDC_GROUPPRIVATEBYTES);
            HWND ioGroupBox = GetDlgItem(hwndDlg, IDC_GROUPIO);
            RECT clientRect;
            RECT margin = { 13, 13, 13, 13 };
            RECT innerMargin = { 10, 20, 10, 10 };
            LONG between = 3;
            LONG width;
            LONG height;

            performanceContext->CpuGraphState.Valid = FALSE;
            performanceContext->CpuGraphState.TooltipIndex = -1;
            performanceContext->PrivateGraphState.Valid = FALSE;
            performanceContext->PrivateGraphState.TooltipIndex = -1;
            performanceContext->IoGraphState.Valid = FALSE;
            performanceContext->IoGraphState.TooltipIndex = -1;

            GetClientRect(hwndDlg, &clientRect);
            width = clientRect.right - margin.left - margin.right;
            height = (clientRect.bottom - margin.top - margin.bottom - between * 2) / 3;

            deferHandle = BeginDeferWindowPos(6);

            deferHandle = DeferWindowPos(deferHandle, cpuGroupBox, NULL, margin.left, margin.top,
                width, height, SWP_NOACTIVATE | SWP_NOZORDER);
            deferHandle = DeferWindowPos(
                deferHandle,
                performanceContext->CpuGraphHandle,
                NULL,
                margin.left + innerMargin.left,
                margin.top + innerMargin.top,
                width - innerMargin.left - innerMargin.right,
                height - innerMargin.top - innerMargin.bottom,
                SWP_NOACTIVATE | SWP_NOZORDER
                );

            deferHandle = DeferWindowPos(deferHandle, privateBytesGroupBox, NULL, margin.left, margin.top + height + between,
                width, height, SWP_NOACTIVATE | SWP_NOZORDER);
            deferHandle = DeferWindowPos(
                deferHandle,
                performanceContext->PrivateGraphHandle,
                NULL,
                margin.left + innerMargin.left,
                margin.top + height + between + innerMargin.top,
                width - innerMargin.left - innerMargin.right,
                height - innerMargin.top - innerMargin.bottom,
                SWP_NOACTIVATE | SWP_NOZORDER
                );

            deferHandle = DeferWindowPos(deferHandle, ioGroupBox, NULL, margin.left, margin.top + (height + between) * 2,
                width, height, SWP_NOACTIVATE | SWP_NOZORDER);
            deferHandle = DeferWindowPos(
                deferHandle,
                performanceContext->IoGraphHandle,
                NULL,
                margin.left + innerMargin.left,
                margin.top + (height + between) * 2 + innerMargin.top,
                width - innerMargin.left - innerMargin.right,
                height - innerMargin.top - innerMargin.bottom,
                SWP_NOACTIVATE | SWP_NOZORDER
                );

            EndDeferWindowPos(deferHandle);
        }
        break;
    case WM_PH_PERFORMANCE_UPDATE:
        {
            if (!(processItem->State & PH_PROCESS_ITEM_REMOVED))
            {
                performanceContext->CpuGraphState.Valid = FALSE;
                Graph_MoveGrid(performanceContext->CpuGraphHandle, 1);
                Graph_Draw(performanceContext->CpuGraphHandle);
                Graph_UpdateTooltip(performanceContext->CpuGraphHandle);
                InvalidateRect(performanceContext->CpuGraphHandle, NULL, FALSE);

                performanceContext->PrivateGraphState.Valid = FALSE;
                Graph_MoveGrid(performanceContext->PrivateGraphHandle, 1);
                Graph_Draw(performanceContext->PrivateGraphHandle);
                Graph_UpdateTooltip(performanceContext->PrivateGraphHandle);
                InvalidateRect(performanceContext->PrivateGraphHandle, NULL, FALSE);

                performanceContext->IoGraphState.Valid = FALSE;
                Graph_MoveGrid(performanceContext->IoGraphHandle, 1);
                Graph_Draw(performanceContext->IoGraphHandle);
                Graph_UpdateTooltip(performanceContext->IoGraphHandle);
                InvalidateRect(performanceContext->IoGraphHandle, NULL, FALSE);
            }
        }
        break;
    }

    return FALSE;
}

static VOID NTAPI ThreadAddedHandler(
    __in_opt PVOID Parameter,
    __in_opt PVOID Context
    )
{
    PPH_THREADS_CONTEXT threadsContext = (PPH_THREADS_CONTEXT)Context;

    // Parameter contains a pointer to the added thread item.
    PhReferenceObject(Parameter);
    PostMessage(
        threadsContext->WindowHandle,
        WM_PH_THREAD_ADDED,
        PhGetRunIdProvider(&threadsContext->ProviderRegistration),
        (LPARAM)Parameter
        );
}

static VOID NTAPI ThreadModifiedHandler(
    __in_opt PVOID Parameter,
    __in_opt PVOID Context
    )
{
    PPH_THREADS_CONTEXT threadsContext = (PPH_THREADS_CONTEXT)Context;

    PostMessage(threadsContext->WindowHandle, WM_PH_THREAD_MODIFIED, 0, (LPARAM)Parameter);
}

static VOID NTAPI ThreadRemovedHandler(
    __in_opt PVOID Parameter,
    __in_opt PVOID Context
    )
{
    PPH_THREADS_CONTEXT threadsContext = (PPH_THREADS_CONTEXT)Context;

    PostMessage(threadsContext->WindowHandle, WM_PH_THREAD_REMOVED, 0, (LPARAM)Parameter);
}

static VOID NTAPI ThreadsUpdatedHandler(
    __in_opt PVOID Parameter,
    __in_opt PVOID Context
    )
{
    PPH_THREADS_CONTEXT threadsContext = (PPH_THREADS_CONTEXT)Context;

    PostMessage(threadsContext->WindowHandle, WM_PH_THREADS_UPDATED, 0, 0);
}

static VOID NTAPI ThreadsLoadingStateChangedHandler(
    __in_opt PVOID Parameter,
    __in_opt PVOID Context
    )
{
    PPH_THREADS_CONTEXT threadsContext = (PPH_THREADS_CONTEXT)Context;

    PostMessage(
        GetDlgItem(threadsContext->WindowHandle, IDC_LIST),
        ELVM_SETCURSOR,
        0,
        // Parameter contains TRUE if loading symbols
        (LPARAM)(Parameter ? LoadCursor(NULL, IDC_APPSTARTING) : NULL)
        );
}

VOID PhpInitializeThreadMenu(
    __in PPH_EMENU Menu,
    __in HANDLE ProcessId,
    __in PPH_THREAD_ITEM *Threads,
    __in ULONG NumberOfThreads
    )
{
    PPH_EMENU_ITEM item;

    if (NumberOfThreads == 0)
    {
        PhSetFlagsAllEMenuItems(Menu, PH_EMENU_DISABLED, PH_EMENU_DISABLED);
    }
    else if (NumberOfThreads == 1)
    {
        // All menu items are enabled by default.
    }
    else
    {
        ULONG menuItemsMultiEnabled[] =
        {
            ID_THREAD_TERMINATE,
            ID_THREAD_FORCETERMINATE,
            ID_THREAD_SUSPEND,
            ID_THREAD_RESUME,
            ID_THREAD_COPY
        };
        ULONG i;

        PhSetFlagsAllEMenuItems(Menu, PH_EMENU_DISABLED, PH_EMENU_DISABLED);

        // These menu items are capable of manipulating 
        // multiple threads.
        for (i = 0; i < sizeof(menuItemsMultiEnabled) / sizeof(ULONG); i++)
        {
            PhEnableEMenuItem(Menu, menuItemsMultiEnabled[i], TRUE);
        }
    }

    // Remove irrelevant menu items.

    if (WindowsVersion < WINDOWS_VISTA)
    {
        // Remove I/O priority.
        if (item = PhFindEMenuItem(Menu, 0, L"I/O Priority", 0))
            PhDestroyEMenuItem(item);
    }

    if (!PhKphHandle)
    {
        // Remove Force Terminate.
        if (item = PhFindEMenuItem(Menu, 0, NULL, ID_THREAD_FORCETERMINATE))
            PhDestroyEMenuItem(item);
    }
    else
    {
        if (ProcessId == SYSTEM_PROCESS_ID)
        {
            // Remove Force Terminate because Terminate does 
            // the same job.
            if (item = PhFindEMenuItem(Menu, 0, NULL, ID_THREAD_FORCETERMINATE))
                PhDestroyEMenuItem(item);
        }
    }

    PhEnableEMenuItem(Menu, ID_THREAD_TOKEN, FALSE);

    // Priority
    if (NumberOfThreads == 1)
    {
        HANDLE threadHandle;
        ULONG ioPriority = -1;
        ULONG threadPriority = THREAD_PRIORITY_ERROR_RETURN;
        ULONG id = 0;

        if (NT_SUCCESS(PhOpenThread(
            &threadHandle,
            ThreadQueryAccess,
            Threads[0]->ThreadId
            )))
        {
            threadPriority = GetThreadPriority(threadHandle);

            if (WindowsVersion >= WINDOWS_VISTA)
            {
                if (!NT_SUCCESS(PhGetThreadIoPriority(
                    threadHandle,
                    &ioPriority
                    )))
                {
                    ioPriority = -1;
                }
            }

            // Token
            {
                HANDLE tokenHandle;

                if (NT_SUCCESS(PhOpenThreadToken(
                    &tokenHandle,
                    TOKEN_QUERY,
                    threadHandle,
                    TRUE
                    )))
                {
                    PhEnableEMenuItem(Menu, ID_THREAD_TOKEN, TRUE);
                    NtClose(tokenHandle);
                }
            }

            NtClose(threadHandle);
        }

        switch (threadPriority)
        {
        case THREAD_PRIORITY_TIME_CRITICAL:
            id = ID_PRIORITY_TIMECRITICAL;
            break;
        case THREAD_PRIORITY_HIGHEST:
            id = ID_PRIORITY_HIGHEST;
            break;
        case THREAD_PRIORITY_ABOVE_NORMAL:
            id = ID_PRIORITY_ABOVENORMAL;
            break;
        case THREAD_PRIORITY_NORMAL:
            id = ID_PRIORITY_NORMAL;
            break;
        case THREAD_PRIORITY_BELOW_NORMAL:
            id = ID_PRIORITY_BELOWNORMAL;
            break;
        case THREAD_PRIORITY_LOWEST:
            id = ID_PRIORITY_LOWEST;
            break;
        case THREAD_PRIORITY_IDLE:
            id = ID_PRIORITY_IDLE;
            break;
        }

        if (id != 0)
        {
            PhSetFlagsEMenuItem(Menu, id,
                PH_EMENU_CHECKED | PH_EMENU_RADIOCHECK,
                PH_EMENU_CHECKED | PH_EMENU_RADIOCHECK);
        }

        if (ioPriority != -1)
        {
            id = 0;

            switch (ioPriority)
            {
            case 0:
                id = ID_I_0;
                break;
            case 1:
                id = ID_I_1;
                break;
            case 2:
                id = ID_I_2;
                break;
            case 3:
                id = ID_I_3;
                break;
            }

            if (id != 0)
            {
                PhSetFlagsEMenuItem(Menu, id,
                    PH_EMENU_CHECKED | PH_EMENU_RADIOCHECK,
                    PH_EMENU_CHECKED | PH_EMENU_RADIOCHECK);
            }
        }
    }
}

static NTSTATUS NTAPI PhpThreadPermissionsOpenThread(
    __out PHANDLE Handle,
    __in ACCESS_MASK DesiredAccess,
    __in_opt PVOID Context
    )
{
    return PhOpenThread(Handle, DesiredAccess, (HANDLE)Context);
}

static INT NTAPI PhpThreadTidCompareFunction(
    __in PVOID Item1,
    __in PVOID Item2,
    __in_opt PVOID Context
    )
{
    PPH_THREAD_ITEM item1 = Item1;
    PPH_THREAD_ITEM item2 = Item2;

    return uintptrcmp((ULONG_PTR)item1->ThreadId, (ULONG_PTR)item2->ThreadId);
}

static INT NTAPI PhpThreadCyclesCompareFunction(
    __in PVOID Item1,
    __in PVOID Item2,
    __in_opt PVOID Context
    )
{
    PPH_THREAD_ITEM item1 = Item1;
    PPH_THREAD_ITEM item2 = Item2;
    PPH_THREADS_CONTEXT threadsContext = Context;

    if (threadsContext->UseCycleTime)
        return uint64cmp(item1->CyclesDelta.Delta, item2->CyclesDelta.Delta);
    else
        return uint64cmp(item1->ContextSwitchesDelta.Delta, item2->ContextSwitchesDelta.Delta);
}

static INT NTAPI PhpThreadStartAddressCompareFunction(
    __in PVOID Item1,
    __in PVOID Item2,
    __in_opt PVOID Context
    )
{
    PPH_THREAD_ITEM item1 = Item1;
    PPH_THREAD_ITEM item2 = Item2;

    return PhCompareStringWithNull(item1->StartAddressString, item2->StartAddressString, TRUE);
}

static INT NTAPI PhpThreadPriorityCompareFunction(
    __in PVOID Item1,
    __in PVOID Item2,
    __in_opt PVOID Context
    )
{
    PPH_THREAD_ITEM item1 = Item1;
    PPH_THREAD_ITEM item2 = Item2;

    return intcmp(item1->PriorityWin32, item2->PriorityWin32);
}

static INT NTAPI PhpThreadServiceCompareFunction(
    __in PVOID Item1,
    __in PVOID Item2,
    __in_opt PVOID Context
    )
{
    PPH_THREAD_ITEM item1 = Item1;
    PPH_THREAD_ITEM item2 = Item2;

    return PhCompareStringWithNull(item1->ServiceName, item2->ServiceName, TRUE);
}

static COLORREF NTAPI PhpThreadColorFunction(
    __in INT Index,
    __in PVOID Param,
    __in_opt PVOID Context
    )
{
    PPH_THREAD_ITEM item = Param;

    if (PhCsUseColorSuspended && item->WaitReason == Suspended)
        return PhCsColorSuspended;
    if (PhCsUseColorGuiThreads && item->IsGuiThread)
        return PhCsColorGuiThreads;

    return PhSysWindowColor;
}

static NTSTATUS NTAPI PhpOpenThreadTokenObject(
    __out PHANDLE Handle,
    __in ACCESS_MASK DesiredAccess,
    __in_opt PVOID Context
    )
{
    return PhOpenThreadToken(
        Handle,
        DesiredAccess,
        (HANDLE)Context,
        TRUE
        );
}

VOID PhpUpdateThreadDetails(
    __in HWND hwndDlg,
    __in BOOLEAN Force
    )
{
    HWND lvHandle;
    ULONG selectedCount;
    PPH_THREAD_ITEM threadItem;
    PPH_STRING startModule = NULL;
    PPH_STRING started = NULL;
    WCHAR kernelTime[PH_TIMESPAN_STR_LEN_1] = L"N/A";
    WCHAR userTime[PH_TIMESPAN_STR_LEN_1] = L"N/A";
    PPH_STRING contextSwitches = NULL;
    PPH_STRING cycles = NULL;
    PPH_STRING state = NULL;
    WCHAR priority[PH_INT32_STR_LEN_1] = L"N/A";
    WCHAR basePriority[PH_INT32_STR_LEN_1] = L"N/A";
    WCHAR ioPriority[PH_INT32_STR_LEN_1] = L"N/A";
    WCHAR pagePriority[PH_INT32_STR_LEN_1] = L"N/A";
    HANDLE threadHandle;
    SYSTEMTIME time;
    ULONG ioPriorityInteger;
    ULONG pagePriorityInteger;

    lvHandle = GetDlgItem(hwndDlg, IDC_LIST);
    selectedCount = ListView_GetSelectedCount(lvHandle);

    if (selectedCount != 1 && !Force)
        return;

    if (selectedCount == 1)
    {
        threadItem = PhGetSelectedListViewItemParam(lvHandle);

        if (!threadItem)
            return;

        startModule = threadItem->StartAddressFileName;

        PhLargeIntegerToLocalSystemTime(&time, &threadItem->CreateTime);
        started = PhaFormatDateTime(&time);

        PhPrintTimeSpan(kernelTime, threadItem->KernelTime.QuadPart, PH_TIMESPAN_HMSM);
        PhPrintTimeSpan(userTime, threadItem->UserTime.QuadPart, PH_TIMESPAN_HMSM);

        contextSwitches = PhaFormatUInt64(threadItem->ContextSwitchesDelta.Value, TRUE);

        if (WINDOWS_HAS_CYCLE_TIME)
            cycles = PhaFormatUInt64(threadItem->CyclesDelta.Value, TRUE);

        if (threadItem->State != Waiting)
        {
            if ((ULONG)threadItem->State < MaximumThreadState)
                state = PhaCreateString(PhKThreadStateNames[(ULONG)threadItem->State]);
        }
        else
        {
            if ((ULONG)threadItem->WaitReason < MaximumWaitReason)
                state = PhaConcatStrings2(L"Wait:", PhKWaitReasonNames[(ULONG)threadItem->WaitReason]);
            else
                state = PhaCreateString(L"Waiting");
        }

        PhPrintInt32(priority, threadItem->Priority);
        PhPrintInt32(basePriority, threadItem->BasePriority);

        if (NT_SUCCESS(PhOpenThread(&threadHandle, ThreadQueryAccess, threadItem->ThreadId)))
        {
            if (NT_SUCCESS(PhGetThreadIoPriority(threadHandle, &ioPriorityInteger)))
                PhPrintUInt32(ioPriority, ioPriorityInteger);
            if (NT_SUCCESS(PhGetThreadPagePriority(threadHandle, &pagePriorityInteger)))
                PhPrintUInt32(pagePriority, pagePriorityInteger);

            NtClose(threadHandle);
        }
    }

    if (Force)
    {
        // These don't change...

        SetDlgItemText(hwndDlg, IDC_STARTMODULE, PhGetStringOrEmpty(startModule));
        EnableWindow(GetDlgItem(hwndDlg, IDC_OPENSTARTMODULE), !!startModule);

        SetDlgItemText(hwndDlg, IDC_STARTED, PhGetStringOrDefault(started, L"N/A"));
    }

    SetDlgItemText(hwndDlg, IDC_KERNELTIME, kernelTime);
    SetDlgItemText(hwndDlg, IDC_USERTIME, userTime);
    SetDlgItemText(hwndDlg, IDC_CONTEXTSWITCHES, PhGetStringOrDefault(contextSwitches, L"N/A"));
    SetDlgItemText(hwndDlg, IDC_CYCLES, PhGetStringOrDefault(cycles, L"N/A"));
    SetDlgItemText(hwndDlg, IDC_STATE, PhGetStringOrDefault(state, L"N/A"));
    SetDlgItemText(hwndDlg, IDC_PRIORITY, priority);
    SetDlgItemText(hwndDlg, IDC_BASEPRIORITY, basePriority);
    SetDlgItemText(hwndDlg, IDC_IOPRIORITY, ioPriority);
    SetDlgItemText(hwndDlg, IDC_PAGEPRIORITY, pagePriority);
}

INT_PTR CALLBACK PhpProcessThreadsDlgProc(
    __in HWND hwndDlg,
    __in UINT uMsg,
    __in WPARAM wParam,
    __in LPARAM lParam
    )
{
    LPPROPSHEETPAGE propSheetPage;
    PPH_PROCESS_PROPPAGECONTEXT propPageContext;
    PPH_PROCESS_ITEM processItem;
    PPH_THREADS_CONTEXT threadsContext;
    HWND lvHandle;

    if (PhpPropPageDlgProcHeader(hwndDlg, uMsg, lParam,
        &propSheetPage, &propPageContext, &processItem))
    {
        threadsContext = (PPH_THREADS_CONTEXT)propPageContext->Context;
    }
    else
    {
        return FALSE;
    }

    lvHandle = GetDlgItem(hwndDlg, IDC_LIST);

    switch (uMsg)
    {
    case WM_INITDIALOG:
        {
            threadsContext = propPageContext->Context =
                PhAllocate(sizeof(PH_THREADS_CONTEXT));

            threadsContext->Provider = PhCreateThreadProvider(
                processItem->ProcessId
                );
            PhRegisterProvider(
                &PhSecondaryProviderThread,
                PhThreadProviderUpdate,
                threadsContext->Provider,
                &threadsContext->ProviderRegistration
                );
            PhRegisterCallback(
                &threadsContext->Provider->ThreadAddedEvent,
                ThreadAddedHandler,
                threadsContext,
                &threadsContext->AddedEventRegistration
                );
            PhRegisterCallback(
                &threadsContext->Provider->ThreadModifiedEvent,
                ThreadModifiedHandler,
                threadsContext,
                &threadsContext->ModifiedEventRegistration
                );
            PhRegisterCallback(
                &threadsContext->Provider->ThreadRemovedEvent,
                ThreadRemovedHandler,
                threadsContext,
                &threadsContext->RemovedEventRegistration
                );
            PhRegisterCallback(
                &threadsContext->Provider->UpdatedEvent,
                ThreadsUpdatedHandler,
                threadsContext,
                &threadsContext->UpdatedEventRegistration
                );
            PhRegisterCallback(
                &threadsContext->Provider->LoadingStateChangedEvent,
                ThreadsLoadingStateChangedHandler,
                threadsContext,
                &threadsContext->LoadingStateChangedEventRegistration
                );
            threadsContext->WindowHandle = hwndDlg;
            threadsContext->UseCycleTime = FALSE;
            threadsContext->NeedsRedraw = FALSE;
            threadsContext->NeedsSort = FALSE;

            // Use Cycles instead of Context Switches on Vista.
            if (WINDOWS_HAS_CYCLE_TIME)
                threadsContext->UseCycleTime = TRUE;

            // Initialize the list.
            PhSetListViewStyle(lvHandle, TRUE, TRUE);
            PhSetControlTheme(lvHandle, L"explorer");
            PhAddListViewColumn(lvHandle, 0, 0, 0, LVCFMT_LEFT, 50, L"TID");
            PhAddListViewColumn(lvHandle, 1, 1, 1, LVCFMT_LEFT, 80,
                threadsContext->UseCycleTime ? L"Cycles Delta" : L"Context Switches Delta"); 
            PhAddListViewColumn(lvHandle, 2, 2, 2, LVCFMT_LEFT, 180, L"Start Address"); 
            PhAddListViewColumn(lvHandle, 3, 3, 3, LVCFMT_LEFT, 80, L"Priority");

            if (processItem->ServiceList && processItem->ServiceList->Count != 0 && WINDOWS_HAS_SERVICE_TAGS)
                PhAddListViewColumn(lvHandle, 4, 4, 4, LVCFMT_LEFT, 100, L"Service");

            PhSetExtendedListViewWithSettings(lvHandle);
            PhLoadListViewColumnsFromSetting(L"ThreadListViewColumns", lvHandle);
            ExtendedListView_SetContext(lvHandle, threadsContext);
            ExtendedListView_SetSortFast(lvHandle, TRUE);
            ExtendedListView_SetCompareFunction(lvHandle, 0, PhpThreadTidCompareFunction);
            ExtendedListView_SetCompareFunction(lvHandle, 1, PhpThreadCyclesCompareFunction);
            ExtendedListView_SetCompareFunction(lvHandle, 2, PhpThreadStartAddressCompareFunction);
            ExtendedListView_SetCompareFunction(lvHandle, 3, PhpThreadPriorityCompareFunction);
            ExtendedListView_SetCompareFunction(lvHandle, 4, PhpThreadServiceCompareFunction);
            ExtendedListView_SetSort(lvHandle, 1, DescendingSortOrder);
            ExtendedListView_SetItemColorFunction(lvHandle, PhpThreadColorFunction);
            ExtendedListView_SetStateHighlighting(lvHandle, TRUE);

            // Sort by TID, Start Address, Priority, Cycles/Context Switches Delta, then Service.
            {
                ULONG fallbackColumns[] = { 0, 2, 3 };

                ExtendedListView_AddFallbackColumns(lvHandle,
                    sizeof(fallbackColumns) / sizeof(ULONG), fallbackColumns);
            }

            PhSetEnabledProvider(&threadsContext->ProviderRegistration, TRUE);
            PhBoostProvider(&threadsContext->ProviderRegistration, NULL);

            SET_BUTTON_BITMAP(IDC_OPENSTARTMODULE,
                PH_LOAD_SHARED_IMAGE(MAKEINTRESOURCE(IDB_FOLDER), IMAGE_BITMAP));
        }
        break;
    case WM_DESTROY:
        {
            // TODO: Fix unsafe cleanup logic.

            PhUnregisterCallback(
                &threadsContext->Provider->ThreadAddedEvent,
                &threadsContext->AddedEventRegistration
                );
            PhUnregisterCallback(
                &threadsContext->Provider->ThreadModifiedEvent,
                &threadsContext->ModifiedEventRegistration
                );
            PhUnregisterCallback(
                &threadsContext->Provider->ThreadRemovedEvent,
                &threadsContext->RemovedEventRegistration
                );
            PhUnregisterCallback(
                &threadsContext->Provider->UpdatedEvent,
                &threadsContext->UpdatedEventRegistration
                );
            PhUnregisterCallback(
                &threadsContext->Provider->LoadingStateChangedEvent,
                &threadsContext->LoadingStateChangedEventRegistration
                );
            PhUnregisterProvider(&threadsContext->ProviderRegistration);

            PhDereferenceAllThreadItems(threadsContext->Provider);

            PhDereferenceObject(threadsContext->Provider);
            PhFree(threadsContext);

            PhSaveListViewColumnsToSetting(L"ThreadListViewColumns", lvHandle);

            PhpPropPageDlgProcDestroy(hwndDlg);
        }
        break;
    case WM_SHOWWINDOW:
        {
            if (!propPageContext->LayoutInitialized)
            {
                PPH_LAYOUT_ITEM dialogItem;

                dialogItem = PhAddPropPageLayoutItem(hwndDlg, hwndDlg,
                    PH_PROP_PAGE_TAB_CONTROL_PARENT, PH_ANCHOR_ALL);
                PhAddPropPageLayoutItem(hwndDlg, GetDlgItem(hwndDlg, IDC_LIST),
                    dialogItem, PH_ANCHOR_ALL);

#define ADD_BL_ITEM(Id) \
    PhAddPropPageLayoutItem(hwndDlg, GetDlgItem(hwndDlg, Id), dialogItem, PH_ANCHOR_LEFT | PH_ANCHOR_BOTTOM)

                // Thread details area
                {
                    ULONG id;

                    for (id = IDC_STATICBL1; id <= IDC_STATICBL11; id++)
                        ADD_BL_ITEM(id);
                }

                PhAddPropPageLayoutItem(hwndDlg, GetDlgItem(hwndDlg, IDC_STARTMODULE),
                    dialogItem, PH_ANCHOR_LEFT | PH_ANCHOR_RIGHT | PH_ANCHOR_BOTTOM);
                PhAddPropPageLayoutItem(hwndDlg, GetDlgItem(hwndDlg, IDC_OPENSTARTMODULE),
                    dialogItem, PH_ANCHOR_RIGHT | PH_ANCHOR_BOTTOM);
                ADD_BL_ITEM(IDC_STARTED);
                ADD_BL_ITEM(IDC_KERNELTIME);
                ADD_BL_ITEM(IDC_USERTIME);
                ADD_BL_ITEM(IDC_CONTEXTSWITCHES);
                ADD_BL_ITEM(IDC_CYCLES);
                ADD_BL_ITEM(IDC_STATE);
                ADD_BL_ITEM(IDC_PRIORITY);
                ADD_BL_ITEM(IDC_BASEPRIORITY);
                ADD_BL_ITEM(IDC_IOPRIORITY);
                ADD_BL_ITEM(IDC_PAGEPRIORITY);

                PhDoPropPageLayout(hwndDlg);

                propPageContext->LayoutInitialized = TRUE;
            }
        }
        break;
    case WM_COMMAND:
        {
            INT id = LOWORD(wParam);

            switch (id)
            {
            case ID_THREAD_INSPECT:
                {
                    PPH_THREAD_ITEM threadItem = PhGetSelectedListViewItemParam(lvHandle);

                    if (threadItem)
                    {
                        PhReferenceObject(threadsContext->Provider->SymbolProvider);
                        PhShowThreadStackDialog(
                            hwndDlg,
                            threadsContext->Provider->ProcessId,
                            threadItem->ThreadId,
                            threadsContext->Provider->SymbolProvider
                            );
                        PhDereferenceObject(threadsContext->Provider->SymbolProvider);
                    }
                }
                break;
            case ID_THREAD_TERMINATE:
                {
                    PPH_THREAD_ITEM *threads;
                    ULONG numberOfThreads;

                    PhGetSelectedListViewItemParams(lvHandle, &threads, &numberOfThreads);
                    PhReferenceObjects(threads, numberOfThreads);

                    if (
                        processItem->ProcessId != SYSTEM_PROCESS_ID ||
                        !PhKphHandle
                        )
                    {
                        if (PhUiTerminateThreads(hwndDlg, threads, numberOfThreads))
                            PhSetStateAllListViewItems(lvHandle, 0, LVIS_SELECTED);
                    }
                    else
                    {
                        if (PhUiForceTerminateThreads(hwndDlg, processItem->ProcessId, threads, numberOfThreads))
                            PhSetStateAllListViewItems(lvHandle, 0, LVIS_SELECTED);
                    }

                    PhDereferenceObjects(threads, numberOfThreads);
                    PhFree(threads);
                }
                break;
            case ID_THREAD_FORCETERMINATE:
                {
                    PPH_THREAD_ITEM *threads;
                    ULONG numberOfThreads;

                    PhGetSelectedListViewItemParams(lvHandle, &threads, &numberOfThreads);
                    PhReferenceObjects(threads, numberOfThreads);

                    if (PhUiForceTerminateThreads(hwndDlg, processItem->ProcessId, threads, numberOfThreads))
                        PhSetStateAllListViewItems(lvHandle, 0, LVIS_SELECTED);

                    PhDereferenceObjects(threads, numberOfThreads);
                    PhFree(threads);
                }
                break;
            case ID_THREAD_SUSPEND:
                {
                    PPH_THREAD_ITEM *threads;
                    ULONG numberOfThreads;

                    PhGetSelectedListViewItemParams(lvHandle, &threads, &numberOfThreads);
                    PhReferenceObjects(threads, numberOfThreads);
                    PhUiSuspendThreads(hwndDlg, threads, numberOfThreads);
                    PhDereferenceObjects(threads, numberOfThreads);
                    PhFree(threads);
                }
                break;
            case ID_THREAD_RESUME:
                {
                    PPH_THREAD_ITEM *threads;
                    ULONG numberOfThreads;

                    PhGetSelectedListViewItemParams(lvHandle, &threads, &numberOfThreads);
                    PhReferenceObjects(threads, numberOfThreads);
                    PhUiResumeThreads(hwndDlg, threads, numberOfThreads);
                    PhDereferenceObjects(threads, numberOfThreads);
                    PhFree(threads);
                }
                break;
            case ID_THREAD_AFFINITY:
                {
                    PPH_THREAD_ITEM threadItem = PhGetSelectedListViewItemParam(lvHandle);

                    if (threadItem)
                    {
                        PhReferenceObject(threadItem);
                        PhShowProcessAffinityDialog(hwndDlg, NULL, threadItem);
                        PhDereferenceObject(threadItem);
                    }
                }
                break;
            case ID_THREAD_PERMISSIONS:
                {
                    PPH_THREAD_ITEM threadItem = PhGetSelectedListViewItemParam(lvHandle);
                    PH_STD_OBJECT_SECURITY stdObjectSecurity;
                    PPH_ACCESS_ENTRY accessEntries;
                    ULONG numberOfAccessEntries;

                    if (threadItem)
                    {
                        stdObjectSecurity.OpenObject = PhpThreadPermissionsOpenThread;
                        stdObjectSecurity.ObjectType = L"Thread";
                        stdObjectSecurity.Context = threadItem->ThreadId;

                        if (PhGetAccessEntries(L"Thread", &accessEntries, &numberOfAccessEntries))
                        {
                            PhEditSecurity(
                                hwndDlg,
                                PhaFormatString(L"Thread %u", (ULONG)threadItem->ThreadId)->Buffer,
                                PhStdGetObjectSecurity,
                                PhStdSetObjectSecurity,
                                &stdObjectSecurity,
                                accessEntries,
                                numberOfAccessEntries
                                );
                            PhFree(accessEntries);
                        }
                    }
                }
                break;
            case ID_THREAD_TOKEN:
                {
                    NTSTATUS status;
                    PPH_THREAD_ITEM threadItem = PhGetSelectedListViewItemParam(lvHandle);
                    HANDLE threadHandle;

                    if (threadItem)
                    {
                        if (NT_SUCCESS(status = PhOpenThread(
                            &threadHandle,
                            ThreadQueryAccess,
                            threadItem->ThreadId
                            )))
                        {
                            PhShowTokenProperties(
                                hwndDlg,
                                PhpOpenThreadTokenObject,
                                (PVOID)threadHandle,
                                NULL
                                );

                            NtClose(threadHandle);
                        }
                        else
                        {
                            PhShowStatus(hwndDlg, L"Unable to open the thread", status, 0);
                        }
                    }
                }
                break;
            case ID_ANALYZE_WAIT:
                {
                    PPH_THREAD_ITEM threadItem = PhGetSelectedListViewItemParam(lvHandle);

                    if (threadItem)
                    {
                        PhReferenceObject(threadsContext->Provider->SymbolProvider);
                        PhUiAnalyzeWaitThread(
                            hwndDlg,
                            processItem->ProcessId,
                            threadItem->ThreadId,
                            threadsContext->Provider->SymbolProvider
                            );
                        PhDereferenceObject(threadsContext->Provider->SymbolProvider);
                    }
                }
                break;
            case ID_PRIORITY_TIMECRITICAL:
            case ID_PRIORITY_HIGHEST:
            case ID_PRIORITY_ABOVENORMAL:
            case ID_PRIORITY_NORMAL:
            case ID_PRIORITY_BELOWNORMAL:
            case ID_PRIORITY_LOWEST:
            case ID_PRIORITY_IDLE:
                {
                    PPH_THREAD_ITEM threadItem = PhGetSelectedListViewItemParam(lvHandle);

                    if (threadItem)
                    {
                        ULONG threadPriorityWin32;

                        switch (id)
                        {
                        case ID_PRIORITY_TIMECRITICAL:
                            threadPriorityWin32 = THREAD_PRIORITY_TIME_CRITICAL;
                            break;
                        case ID_PRIORITY_HIGHEST:
                            threadPriorityWin32 = THREAD_PRIORITY_HIGHEST;
                            break;
                        case ID_PRIORITY_ABOVENORMAL:
                            threadPriorityWin32 = THREAD_PRIORITY_ABOVE_NORMAL;
                            break;
                        case ID_PRIORITY_NORMAL:
                            threadPriorityWin32 = THREAD_PRIORITY_NORMAL;
                            break;
                        case ID_PRIORITY_BELOWNORMAL:
                            threadPriorityWin32 = THREAD_PRIORITY_BELOW_NORMAL;
                            break;
                        case ID_PRIORITY_LOWEST:
                            threadPriorityWin32 = THREAD_PRIORITY_LOWEST;
                            break;
                        case ID_PRIORITY_IDLE:
                            threadPriorityWin32 = THREAD_PRIORITY_IDLE;
                            break;
                        }

                        PhReferenceObject(threadItem);
                        PhUiSetPriorityThread(hwndDlg, threadItem, threadPriorityWin32);
                        PhDereferenceObject(threadItem);
                    }
                }
                break;
            case ID_I_0:
            case ID_I_1:
            case ID_I_2:
            case ID_I_3:
                {
                    PPH_THREAD_ITEM threadItem = PhGetSelectedListViewItemParam(lvHandle);

                    if (threadItem)
                    {
                        ULONG ioPriority;

                        switch (id)
                        {
                        case ID_I_0:
                            ioPriority = 0;
                            break;
                        case ID_I_1:
                            ioPriority = 1;
                            break;
                        case ID_I_2:
                            ioPriority = 2;
                            break;
                        case ID_I_3:
                            ioPriority = 3;
                            break;
                        }

                        PhReferenceObject(threadItem);
                        PhUiSetIoPriorityThread(hwndDlg, threadItem, ioPriority);
                        PhDereferenceObject(threadItem);
                    }
                }
                break;
            case ID_THREAD_COPY:
                {
                    PhCopyListView(lvHandle);
                }
                break;
            case IDC_OPENSTARTMODULE:
                {
                    PPH_THREAD_ITEM threadItem = PhGetSelectedListViewItemParam(lvHandle);

                    if (threadItem && threadItem->StartAddressFileName)
                    {
                        PhShellExploreFile(hwndDlg, threadItem->StartAddressFileName->Buffer);
                    }
                }
                break;
            }
        }
        break;
    case WM_NOTIFY:
        {
            LPNMHDR header = (LPNMHDR)lParam;

            switch (header->code)
            {
            case PSN_SETACTIVE:
                PhSetEnabledProvider(&threadsContext->ProviderRegistration, TRUE);
                break;
            case PSN_KILLACTIVE:
                PhSetEnabledProvider(&threadsContext->ProviderRegistration, FALSE);
                break;
            case NM_DBLCLK:
                {
                    if (header->hwndFrom == lvHandle)
                    {
                        SendMessage(hwndDlg, WM_COMMAND, ID_THREAD_INSPECT, 0);
                    }
                }
                break;
            case NM_RCLICK:
                {
                    if (header->hwndFrom == lvHandle)
                    {
                        LPNMITEMACTIVATE itemActivate = (LPNMITEMACTIVATE)header;
                        PPH_THREAD_ITEM *threads;
                        ULONG numberOfThreads;

                        PhGetSelectedListViewItemParams(lvHandle, &threads, &numberOfThreads);

                        if (numberOfThreads != 0)
                        {
                            PPH_EMENU menu;
                            PPH_EMENU_ITEM item;
                            POINT location;

                            menu = PhCreateEMenu();
                            PhLoadResourceEMenuItem(menu, PhInstanceHandle, MAKEINTRESOURCE(IDR_THREAD), 0);
                            PhSetFlagsEMenuItem(menu, ID_THREAD_INSPECT, PH_EMENU_DEFAULT, PH_EMENU_DEFAULT);

                            PhpInitializeThreadMenu(menu, processItem->ProcessId, threads, numberOfThreads);

                            if (PhPluginsEnabled)
                            {
                                PH_PLUGIN_MENU_INFORMATION menuInfo;

                                menuInfo.Menu = menu;
                                menuInfo.OwnerWindow = hwndDlg;
                                menuInfo.u.Thread.ProcessId = processItem->ProcessId;
                                menuInfo.u.Thread.Threads = threads;
                                menuInfo.u.Thread.NumberOfThreads = numberOfThreads;

                                PhInvokeCallback(PhGetGeneralCallback(GeneralCallbackThreadMenuInitializing), &menuInfo);
                            }

                            location = itemActivate->ptAction;
                            ClientToScreen(lvHandle, &location);

                            item = PhShowEMenu(
                                menu,
                                hwndDlg,
                                PH_EMENU_SHOW_LEFTRIGHT,
                                PH_ALIGN_LEFT | PH_ALIGN_TOP,
                                location.x,
                                location.y
                                );

                            if (item)
                            {
                                BOOLEAN handled = FALSE;

                                if (PhPluginsEnabled)
                                    handled = PhPluginTriggerEMenuItem(hwndDlg, item);

                                if (!handled)
                                    SendMessage(hwndDlg, WM_COMMAND, item->Id, 0);
                            }

                            PhDestroyEMenu(menu);
                        }

                        PhFree(threads);
                    }
                }
                break;
            case LVN_KEYDOWN:
                {
                    if (header->hwndFrom == lvHandle)
                    {
                        LPNMLVKEYDOWN keyDown = (LPNMLVKEYDOWN)header;

                        switch (keyDown->wVKey)
                        {
                        case 'C':
                            if (GetKeyState(VK_CONTROL) < 0)
                                SendMessage(hwndDlg, WM_COMMAND, ID_THREAD_COPY, 0);
                            break;
                        case VK_DELETE:
                            SendMessage(hwndDlg, WM_COMMAND, ID_THREAD_TERMINATE, 0);
                            break;
                        }
                    }
                }
                break;
            case LVN_ITEMCHANGED:
                {
                    PhpUpdateThreadDetails(hwndDlg, TRUE);
                }
                break;
            }
        }
        break;
    case WM_PH_THREAD_ADDED:
        {
            INT lvItemIndex;
            ULONG runId = (ULONG)wParam;
            PPH_THREAD_ITEM threadItem = (PPH_THREAD_ITEM)lParam;

            if (!threadsContext->NeedsRedraw)
            {
                // Disable redraw. It will be re-enabled later.
                ExtendedListView_SetRedraw(lvHandle, FALSE);
                threadsContext->NeedsRedraw = TRUE;
            }

            if (runId == 1) ExtendedListView_SetStateHighlighting(lvHandle, FALSE);
            lvItemIndex = PhAddListViewItem(
                lvHandle,
                MAXINT,
                threadItem->ThreadIdString,
                threadItem
                );
            if (runId == 1) ExtendedListView_SetStateHighlighting(lvHandle, TRUE);
            PhSetListViewSubItem(lvHandle, lvItemIndex, 2, PhGetString(threadItem->StartAddressString));
            PhSetListViewSubItem(lvHandle, lvItemIndex, 3, PhGetString(threadItem->PriorityWin32String));

            threadsContext->NeedsSort = TRUE;
        }
        break;
    case WM_PH_THREAD_MODIFIED:
        {
            INT lvItemIndex;
            PPH_THREAD_ITEM threadItem = (PPH_THREAD_ITEM)lParam;

            if (!threadsContext->NeedsRedraw)
            {
                ExtendedListView_SetRedraw(lvHandle, FALSE);
                threadsContext->NeedsRedraw = TRUE;
            }

            lvItemIndex = PhFindListViewItemByParam(lvHandle, -1, threadItem);

            if (lvItemIndex != -1)
            {
                if (threadsContext->UseCycleTime)
                    PhSetListViewSubItem(lvHandle, lvItemIndex, 1, PhGetString(threadItem->CyclesDeltaString));
                else
                    PhSetListViewSubItem(lvHandle, lvItemIndex, 1, PhGetString(threadItem->ContextSwitchesDeltaString));

                PhSetListViewSubItem(lvHandle, lvItemIndex, 2, PhGetString(threadItem->StartAddressString));
                PhSetListViewSubItem(lvHandle, lvItemIndex, 3, PhGetString(threadItem->PriorityWin32String));

                if (processItem->ServiceList && processItem->ServiceList->Count != 0 && WINDOWS_HAS_SERVICE_TAGS)
                    PhSetListViewSubItem(lvHandle, lvItemIndex, 4, PhGetString(threadItem->ServiceName));

                threadsContext->NeedsSort = TRUE;
            }
        }
        break;
    case WM_PH_THREAD_REMOVED:
        {
            PPH_THREAD_ITEM threadItem = (PPH_THREAD_ITEM)lParam;

            if (!threadsContext->NeedsRedraw)
            {
                ExtendedListView_SetRedraw(lvHandle, FALSE);
                threadsContext->NeedsRedraw = TRUE;
            }

            PhRemoveListViewItem(
                lvHandle,
                PhFindListViewItemByParam(lvHandle, -1, threadItem)
                );
            PhDereferenceObject(threadItem);
        }
        break;
    case WM_PH_THREADS_UPDATED:
        {
            ExtendedListView_Tick(lvHandle);

            if (threadsContext->NeedsSort)
            {
                ExtendedListView_SortItems(lvHandle);
                threadsContext->NeedsSort = FALSE;
            }

            if (threadsContext->NeedsRedraw)
            {
                ExtendedListView_SetRedraw(lvHandle, TRUE);
                threadsContext->NeedsRedraw = FALSE;
            }

            if (propPageContext->PropContext->SelectThreadId)
            {
                PPH_THREAD_ITEM threadItem;
                INT lvItemIndex;

                if (threadItem = PhReferenceThreadItem(
                    threadsContext->Provider,
                    propPageContext->PropContext->SelectThreadId
                    ))
                {
                    lvItemIndex = PhFindListViewItemByParam(lvHandle, -1, threadItem);

                    if (lvItemIndex != -1)
                    {
                        ListView_SetItemState(lvHandle, lvItemIndex,
                            LVIS_FOCUSED | LVIS_SELECTED,
                            LVIS_FOCUSED | LVIS_SELECTED);
                    }

                    PhDereferenceObject(threadItem);
                }

                propPageContext->PropContext->SelectThreadId = NULL;
            }

            PhpUpdateThreadDetails(hwndDlg, FALSE);
        }
        break;
    }

    REFLECT_MESSAGE_DLG(hwndDlg, lvHandle, uMsg, wParam, lParam);

    return FALSE;
}

static NTSTATUS NTAPI PhpOpenProcessToken(
    __out PHANDLE Handle,
    __in ACCESS_MASK DesiredAccess,
    __in_opt PVOID Context
    )
{
    NTSTATUS status;
    HANDLE processHandle;

    if (!NT_SUCCESS(status = PhOpenProcess(
        &processHandle,
        ProcessQueryAccess,
        (HANDLE)Context
        )))
        return status;

    status = PhOpenProcessToken(Handle, DesiredAccess, processHandle);
    NtClose(processHandle);

    return status;
}

INT_PTR CALLBACK PhpProcessTokenHookProc(
    __in HWND hwndDlg,
    __in UINT uMsg,
    __in WPARAM wParam,
    __in LPARAM lParam
    )
{
    switch (uMsg)
    {
    case WM_DESTROY:
        {
            RemoveProp(hwndDlg, L"LayoutInitialized");
        }
        break;
    case WM_SHOWWINDOW:
        {
            if (!GetProp(hwndDlg, L"LayoutInitialized"))
            {
                PPH_LAYOUT_ITEM dialogItem;

                // This is a big violation of abstraction...

                dialogItem = PhAddPropPageLayoutItem(hwndDlg, hwndDlg,
                    PH_PROP_PAGE_TAB_CONTROL_PARENT, PH_ANCHOR_ALL);
                PhAddPropPageLayoutItem(hwndDlg, GetDlgItem(hwndDlg, IDC_USER),
                    dialogItem, PH_ANCHOR_LEFT | PH_ANCHOR_TOP | PH_ANCHOR_RIGHT);
                PhAddPropPageLayoutItem(hwndDlg, GetDlgItem(hwndDlg, IDC_USERSID),
                    dialogItem, PH_ANCHOR_LEFT | PH_ANCHOR_TOP | PH_ANCHOR_RIGHT);
                PhAddPropPageLayoutItem(hwndDlg, GetDlgItem(hwndDlg, IDC_VIRTUALIZED),
                    dialogItem, PH_ANCHOR_LEFT | PH_ANCHOR_TOP | PH_ANCHOR_RIGHT);
                PhAddPropPageLayoutItem(hwndDlg, GetDlgItem(hwndDlg, IDC_GROUPS),
                    dialogItem, PH_ANCHOR_LEFT | PH_ANCHOR_TOP | PH_ANCHOR_RIGHT);
                PhAddPropPageLayoutItem(hwndDlg, GetDlgItem(hwndDlg, IDC_PRIVILEGES),
                    dialogItem, PH_ANCHOR_ALL);
                PhAddPropPageLayoutItem(hwndDlg, GetDlgItem(hwndDlg, IDC_ADVANCED),
                    dialogItem, PH_ANCHOR_RIGHT | PH_ANCHOR_BOTTOM);

                PhDoPropPageLayout(hwndDlg);

                SetProp(hwndDlg, L"LayoutInitialized", (HANDLE)TRUE);
            }
        }
        break;
    }

    return FALSE;
}

static VOID NTAPI ModuleAddedHandler(
    __in_opt PVOID Parameter,
    __in_opt PVOID Context
    )
{
    PPH_MODULES_CONTEXT modulesContext = (PPH_MODULES_CONTEXT)Context;

    // Parameter contains a pointer to the added module item.
    PhReferenceObject(Parameter);
    PostMessage(
        modulesContext->WindowHandle,
        WM_PH_MODULE_ADDED,
        PhGetRunIdProvider(&modulesContext->ProviderRegistration),
        (LPARAM)Parameter
        );
}

static VOID NTAPI ModuleModifiedHandler(
    __in_opt PVOID Parameter,
    __in_opt PVOID Context
    )
{
    PPH_MODULES_CONTEXT modulesContext = (PPH_MODULES_CONTEXT)Context;

    PostMessage(modulesContext->WindowHandle, WM_PH_MODULE_MODIFIED, 0, (LPARAM)Parameter);
}

static VOID NTAPI ModuleRemovedHandler(
    __in_opt PVOID Parameter,
    __in_opt PVOID Context
    )
{
    PPH_MODULES_CONTEXT modulesContext = (PPH_MODULES_CONTEXT)Context;

    PostMessage(modulesContext->WindowHandle, WM_PH_MODULE_REMOVED, 0, (LPARAM)Parameter);
}

static VOID NTAPI ModulesUpdatedHandler(
    __in_opt PVOID Parameter,
    __in_opt PVOID Context
    )
{
    PPH_MODULES_CONTEXT modulesContext = (PPH_MODULES_CONTEXT)Context;

    PostMessage(modulesContext->WindowHandle, WM_PH_MODULES_UPDATED, 0, 0);
}

VOID PhpInitializeModuleMenu(
    __in PPH_EMENU Menu,
    __in HANDLE ProcessId,
    __in PPH_MODULE_ITEM *Modules,
    __in ULONG NumberOfModules
    )
{
    PPH_EMENU_ITEM item;
    PPH_STRING inspectExecutables;

    inspectExecutables = PhGetStringSetting(L"ProgramInspectExecutables");

    if (inspectExecutables->Length == 0)
    {
        if (item = PhFindEMenuItem(Menu, 0, NULL, ID_MODULE_INSPECT))
            PhDestroyEMenuItem(item);
    }

    PhDereferenceObject(inspectExecutables);

    if (NumberOfModules == 0)
    {
        PhSetFlagsAllEMenuItems(Menu, PH_EMENU_DISABLED, PH_EMENU_DISABLED);
    }
    else if (NumberOfModules == 1)
    {
        // Nothing
    }
    else
    {
        PhSetFlagsAllEMenuItems(Menu, PH_EMENU_DISABLED, PH_EMENU_DISABLED);
        PhEnableEMenuItem(Menu, ID_MODULE_COPY, TRUE);
    }
}

VOID PhShowModuleContextMenu(
    __in HWND hwndDlg,
    __in PPH_PROCESS_ITEM ProcessItem,
    __in PPH_MODULES_CONTEXT Context,
    __in POINT Location
    )
{
    PPH_MODULE_ITEM *modules;
    ULONG numberOfModules;

    PhGetSelectedModuleItems(&Context->ListContext, &modules, &numberOfModules);

    if (numberOfModules != 0)
    {
        PPH_EMENU menu;
        PPH_EMENU_ITEM item;

        menu = PhCreateEMenu();
        PhLoadResourceEMenuItem(menu, PhInstanceHandle, MAKEINTRESOURCE(IDR_MODULE), 0);

        PhpInitializeModuleMenu(menu, ProcessItem->ProcessId, modules, numberOfModules);

        if (PhPluginsEnabled)
        {
            PH_PLUGIN_MENU_INFORMATION menuInfo;

            menuInfo.Menu = menu;
            menuInfo.OwnerWindow = hwndDlg;
            menuInfo.u.Module.ProcessId = ProcessItem->ProcessId;
            menuInfo.u.Module.Modules = modules;
            menuInfo.u.Module.NumberOfModules = numberOfModules;

            PhInvokeCallback(PhGetGeneralCallback(GeneralCallbackModuleMenuInitializing), &menuInfo);
        }

        ClientToScreen(Context->ListContext.TreeListHandle, &Location);

        item = PhShowEMenu(
            menu,
            hwndDlg,
            PH_EMENU_SHOW_LEFTRIGHT,
            PH_ALIGN_LEFT | PH_ALIGN_TOP,
            Location.x,
            Location.y
            );

        if (item)
        {
            BOOLEAN handled = FALSE;

            if (PhPluginsEnabled)
                handled = PhPluginTriggerEMenuItem(hwndDlg, item);

            if (!handled)
                SendMessage(hwndDlg, WM_COMMAND, item->Id, 0);
        }

        PhDestroyEMenu(menu);
    }

    PhFree(modules);
}

INT_PTR CALLBACK PhpProcessModulesDlgProc(
    __in HWND hwndDlg,
    __in UINT uMsg,
    __in WPARAM wParam,
    __in LPARAM lParam
    )
{
    LPPROPSHEETPAGE propSheetPage;
    PPH_PROCESS_PROPPAGECONTEXT propPageContext;
    PPH_PROCESS_ITEM processItem;
    PPH_MODULES_CONTEXT modulesContext;
    HWND tlHandle;

    if (PhpPropPageDlgProcHeader(hwndDlg, uMsg, lParam,
        &propSheetPage, &propPageContext, &processItem))
    {
        modulesContext = (PPH_MODULES_CONTEXT)propPageContext->Context;

        if (modulesContext)
            tlHandle = modulesContext->ListContext.TreeListHandle;
    }
    else
    {
        return FALSE;
    }

    switch (uMsg)
    {
    case WM_INITDIALOG:
        {
            // Lots of boilerplate code...

            modulesContext = propPageContext->Context =
                PhAllocate(sizeof(PH_MODULES_CONTEXT));

            modulesContext->Provider = PhCreateModuleProvider(
                processItem->ProcessId
                );
            PhRegisterProvider(
                &PhSecondaryProviderThread,
                PhModuleProviderUpdate,
                modulesContext->Provider,
                &modulesContext->ProviderRegistration
                );
            PhRegisterCallback(
                &modulesContext->Provider->ModuleAddedEvent,
                ModuleAddedHandler,
                modulesContext,
                &modulesContext->AddedEventRegistration
                );
            PhRegisterCallback(
                &modulesContext->Provider->ModuleModifiedEvent,
                ModuleModifiedHandler,
                modulesContext,
                &modulesContext->ModifiedEventRegistration
                );
            PhRegisterCallback(
                &modulesContext->Provider->ModuleRemovedEvent,
                ModuleRemovedHandler,
                modulesContext,
                &modulesContext->RemovedEventRegistration
                );
            PhRegisterCallback(
                &modulesContext->Provider->UpdatedEvent,
                ModulesUpdatedHandler,
                modulesContext,
                &modulesContext->UpdatedEventRegistration
                );
            modulesContext->WindowHandle = hwndDlg;

            // Initialize the list.
            modulesContext->ListContext.TreeListHandle = GetDlgItem(hwndDlg, IDC_LIST);
            tlHandle = modulesContext->ListContext.TreeListHandle;
            BringWindowToTop(tlHandle);
            PhInitializeModuleList(hwndDlg, tlHandle, &modulesContext->ListContext);
            //SendMessage(tlHandle, WM_SETFONT, (WPARAM)SendMessage(hwndDlg, WM_GETFONT, 0, 0), FALSE);
            modulesContext->NeedsRedraw = FALSE;

            PhLoadSettingsModuleTreeList(&modulesContext->ListContext);

            PhSetEnabledProvider(&modulesContext->ProviderRegistration, TRUE);
            PhBoostProvider(&modulesContext->ProviderRegistration, NULL);
        }
        break;
    case WM_DESTROY:
        {
            PhUnregisterCallback(
                &modulesContext->Provider->ModuleAddedEvent,
                &modulesContext->AddedEventRegistration
                );
            PhUnregisterCallback(
                &modulesContext->Provider->ModuleModifiedEvent,
                &modulesContext->ModifiedEventRegistration
                );
            PhUnregisterCallback(
                &modulesContext->Provider->ModuleRemovedEvent,
                &modulesContext->RemovedEventRegistration
                );
            PhUnregisterCallback(
                &modulesContext->Provider->UpdatedEvent,
                &modulesContext->UpdatedEventRegistration
                );
            PhUnregisterProvider(&modulesContext->ProviderRegistration);
            PhDereferenceObject(modulesContext->Provider);

            PhSaveSettingsModuleTreeList(&modulesContext->ListContext);
            PhDeleteModuleList(&modulesContext->ListContext);

            PhFree(modulesContext);

            PhpPropPageDlgProcDestroy(hwndDlg);
        }
        break;
    case WM_SHOWWINDOW:
        {
            if (!propPageContext->LayoutInitialized)
            {
                PPH_LAYOUT_ITEM dialogItem;

                dialogItem = PhAddPropPageLayoutItem(hwndDlg, hwndDlg,
                    PH_PROP_PAGE_TAB_CONTROL_PARENT, PH_ANCHOR_ALL);
                PhAddPropPageLayoutItem(hwndDlg, modulesContext->ListContext.TreeListHandle,
                    dialogItem, PH_ANCHOR_ALL);

                PhDoPropPageLayout(hwndDlg);

                propPageContext->LayoutInitialized = TRUE;
            }
        }
        break;
    case WM_COMMAND:
        {
            switch (LOWORD(wParam))
            {
            case ID_SHOWCONTEXTMENU:
                {
                    POINT location;

                    location.x = LOWORD(lParam);
                    location.y = HIWORD(lParam);
                    PhShowModuleContextMenu(hwndDlg, processItem, modulesContext, location);
                }
                break;
            case ID_MODULE_UNLOAD:
                {
                    PPH_MODULE_ITEM moduleItem = PhGetSelectedModuleItem(&modulesContext->ListContext);

                    if (moduleItem)
                    {
                        PhReferenceObject(moduleItem);

                        if (PhUiUnloadModule(hwndDlg, processItem->ProcessId, moduleItem))
                            PhDeselectAllModuleNodes(&modulesContext->ListContext);

                        PhDereferenceObject(moduleItem);
                    }
                }
                break;
            case ID_MODULE_INSPECT:
                {
                    PPH_MODULE_ITEM moduleItem = PhGetSelectedModuleItem(&modulesContext->ListContext);

                    if (moduleItem)
                    {
                        PhShellExecuteUserString(
                            hwndDlg,
                            L"ProgramInspectExecutables",
                            moduleItem->FileName->Buffer,
                            FALSE
                            );
                    }
                }
                break;
            case ID_MODULE_SEARCHONLINE:
                {
                    PPH_MODULE_ITEM moduleItem = PhGetSelectedModuleItem(&modulesContext->ListContext);

                    if (moduleItem)
                    {
                        PhSearchOnlineString(hwndDlg, moduleItem->Name->Buffer);
                    }
                }
                break;
            case ID_MODULE_OPENCONTAININGFOLDER:
                {
                    PPH_MODULE_ITEM moduleItem = PhGetSelectedModuleItem(&modulesContext->ListContext);

                    if (moduleItem)
                    {
                        PhShellExploreFile(hwndDlg, moduleItem->FileName->Buffer);
                    }
                }
                break;
            case ID_MODULE_PROPERTIES:
                {
                    PPH_MODULE_ITEM moduleItem = PhGetSelectedModuleItem(&modulesContext->ListContext);

                    if (moduleItem)
                    {
                        PhShellProperties(hwndDlg, moduleItem->FileName->Buffer);
                    }
                }
                break;
            case ID_MODULE_COPY:
                {
                    PPH_FULL_STRING text;

                    text = PhGetTreeListText(tlHandle, PHMOTLC_MAXIMUM);
                    PhSetClipboardStringEx(tlHandle, text->Buffer, text->Length);
                    PhDereferenceObject(text);
                }
                break;
            }
        }
        break;
    case WM_NOTIFY:
        {
            LPNMHDR header = (LPNMHDR)lParam;

            switch (header->code)
            {
            case PSN_SETACTIVE:
                PhSetEnabledProvider(&modulesContext->ProviderRegistration, TRUE);
                break;
            case PSN_KILLACTIVE:
                PhSetEnabledProvider(&modulesContext->ProviderRegistration, FALSE);
                break;
            }
        }
        break;
    case WM_PH_MODULE_ADDED:
        {
            ULONG runId = (ULONG)wParam;
            PPH_MODULE_ITEM moduleItem = (PPH_MODULE_ITEM)lParam;

            if (!modulesContext->NeedsRedraw)
            {
                TreeList_SetRedraw(tlHandle, FALSE);
                modulesContext->NeedsRedraw = TRUE;
            }

            PhAddModuleNode(&modulesContext->ListContext, moduleItem, runId);
            PhDereferenceObject(moduleItem);
        }
        break;
    case WM_PH_MODULE_MODIFIED:
        {
            PPH_MODULE_ITEM moduleItem = (PPH_MODULE_ITEM)lParam;

            if (!modulesContext->NeedsRedraw)
            {
                TreeList_SetRedraw(tlHandle, FALSE);
                modulesContext->NeedsRedraw = TRUE;
            }

            PhUpdateModuleNode(&modulesContext->ListContext, PhFindModuleNode(&modulesContext->ListContext, moduleItem));
        }
        break;
    case WM_PH_MODULE_REMOVED:
        {
            PPH_MODULE_ITEM moduleItem = (PPH_MODULE_ITEM)lParam;

            if (!modulesContext->NeedsRedraw)
            {
                TreeList_SetRedraw(tlHandle, FALSE);
                modulesContext->NeedsRedraw = TRUE;
            }

            PhRemoveModuleNode(&modulesContext->ListContext, PhFindModuleNode(&modulesContext->ListContext, moduleItem));
        }
        break;
    case WM_PH_MODULES_UPDATED:
        {
            PhTickModuleNodes(&modulesContext->ListContext);

            if (modulesContext->NeedsRedraw)
            {
                TreeList_SetRedraw(tlHandle, TRUE);
                modulesContext->NeedsRedraw = FALSE;
            }
        }
        break;
    }

    return FALSE;
}

VOID PhpRefreshProcessMemoryList(
    __in HWND hwndDlg,
    __in PPH_PROCESS_PROPPAGECONTEXT PropPageContext
    )
{
    PPH_MEMORY_CONTEXT memoryContext = PropPageContext->Context;

    ExtendedListView_SetRedraw(memoryContext->ListViewHandle, FALSE);

    // Get rid of existing memory items.
    PhDereferenceObjects(memoryContext->MemoryList->Items, memoryContext->MemoryList->Count);
    PhClearList(memoryContext->MemoryList);
    ListView_DeleteAllItems(memoryContext->ListViewHandle);

    PhMemoryProviderUpdate(&memoryContext->Provider);
    ExtendedListView_SortItems(memoryContext->ListViewHandle);
    ExtendedListView_SetRedraw(memoryContext->ListViewHandle, TRUE);
}

BOOLEAN NTAPI PhpProcessMemoryCallback(
    __in PPH_MEMORY_PROVIDER Provider,
    __in __assumeRefs(1) PPH_MEMORY_ITEM MemoryItem
    )
{
    PPH_PROCESS_PROPPAGECONTEXT propPageContext = Provider->Context;
    PPH_MEMORY_CONTEXT memoryContext = propPageContext->Context;
    INT lvItemIndex;
    PPH_STRING string;
    PWSTR name;
    WCHAR protectionString[17];

    PhAddItemList(memoryContext->MemoryList, MemoryItem);

    // Name

    string = NULL;

    if (MemoryItem->Name)
    {
        string = PhConcatStrings(
            6,
            MemoryItem->Name->Buffer,
            L": ",
            PhGetMemoryTypeString(MemoryItem->Flags),
            L" (",
            PhGetMemoryStateString(MemoryItem->Flags),
            L")"
            );
        name = string->Buffer;
    }
    else if (MemoryItem->Flags & MEM_FREE)
    {
        name = L"Free";
    }
    else
    {
        string = PhConcatStrings(
            4,
            PhGetMemoryTypeString(MemoryItem->Flags),
            L" (",
            PhGetMemoryStateString(MemoryItem->Flags),
            L")"
            );
        name = string->Buffer;
    }

    lvItemIndex = PhAddListViewItem(memoryContext->ListViewHandle, MAXINT, name, MemoryItem);

    if (string)
        PhDereferenceObject(string);

    // Base address
    PhSetListViewSubItem(memoryContext->ListViewHandle, lvItemIndex, 1, MemoryItem->BaseAddressString);

    // Size
    string = PhFormatSize(MemoryItem->Size, -1);
    PhSetListViewSubItem(memoryContext->ListViewHandle, lvItemIndex, 2, string->Buffer);
    PhDereferenceObject(string);

    // Protection
    PhGetMemoryProtectionString(MemoryItem->Protection, protectionString);
    PhSetListViewSubItem(memoryContext->ListViewHandle, lvItemIndex, 3, protectionString);

    return TRUE;
}

VOID PhpUpdateMemoryItemInListView(
    __in HANDLE ListViewHandle,
    __in PPH_MEMORY_ITEM MemoryItem
    )
{
    INT lvItemIndex;

    lvItemIndex = PhFindListViewItemByParam(ListViewHandle, -1, MemoryItem);

    if (lvItemIndex != -1)
    {
        WCHAR protectionString[17];

        PhGetMemoryProtectionString(MemoryItem->Protection, protectionString);
        PhSetListViewSubItem(ListViewHandle, lvItemIndex, 3, protectionString);
    }
}

INT NTAPI PhpMemoryAddressCompareFunction(
    __in PVOID Item1,
    __in PVOID Item2,
    __in_opt PVOID Context
    )
{
    PPH_MEMORY_ITEM item1 = Item1;
    PPH_MEMORY_ITEM item2 = Item2;

    return uintptrcmp((ULONG_PTR)item1->BaseAddress, (ULONG_PTR)item2->BaseAddress);
}

INT NTAPI PhpMemorySizeCompareFunction(
    __in PVOID Item1,
    __in PVOID Item2,
    __in_opt PVOID Context
    )
{
    PPH_MEMORY_ITEM item1 = Item1;
    PPH_MEMORY_ITEM item2 = Item2;

    return uintptrcmp(item1->Size, item2->Size);
}

VOID PhpInitializeMemoryMenu(
    __in PPH_EMENU Menu,
    __in HANDLE ProcessId,
    __in PPH_MEMORY_ITEM *MemoryItems,
    __in ULONG NumberOfMemoryItems
    )
{
    if (NumberOfMemoryItems == 0)
    {
        PhSetFlagsAllEMenuItems(Menu, PH_EMENU_DISABLED, PH_EMENU_DISABLED);
    }
    else if (NumberOfMemoryItems == 1)
    {
        if (MemoryItems[0]->Flags & MEM_FREE)
        {
            PhEnableEMenuItem(Menu, ID_MEMORY_CHANGEPROTECTION, FALSE);
            PhEnableEMenuItem(Menu, ID_MEMORY_FREE, FALSE);
            PhEnableEMenuItem(Menu, ID_MEMORY_DECOMMIT, FALSE);
        }
        else if (MemoryItems[0]->Flags & (MEM_MAPPED | MEM_IMAGE))
        {
            PhEnableEMenuItem(Menu, ID_MEMORY_DECOMMIT, FALSE);
        }
    }
    else
    {
        PhSetFlagsAllEMenuItems(Menu, PH_EMENU_DISABLED, PH_EMENU_DISABLED);
        PhEnableEMenuItem(Menu, ID_MEMORY_SAVE, TRUE);
        PhEnableEMenuItem(Menu, ID_MEMORY_COPY, TRUE);
    }

    PhEnableEMenuItem(Menu, ID_MEMORY_READWRITEADDRESS, TRUE);
}

INT_PTR CALLBACK PhpProcessMemoryDlgProc(
    __in HWND hwndDlg,
    __in UINT uMsg,
    __in WPARAM wParam,
    __in LPARAM lParam
    )
{
    LPPROPSHEETPAGE propSheetPage;
    PPH_PROCESS_PROPPAGECONTEXT propPageContext;
    PPH_PROCESS_ITEM processItem;
    HWND lvHandle;

    if (!PhpPropPageDlgProcHeader(hwndDlg, uMsg, lParam,
        &propSheetPage, &propPageContext, &processItem))
        return FALSE;

    lvHandle = GetDlgItem(hwndDlg, IDC_LIST);

    switch (uMsg)
    {
    case WM_INITDIALOG:
        {
            PPH_MEMORY_CONTEXT memoryContext;

            memoryContext = propPageContext->Context =
                PhAllocate(sizeof(PH_MEMORY_CONTEXT));
            PhInitializeMemoryProvider(
                &memoryContext->Provider,
                processItem->ProcessId,
                PhpProcessMemoryCallback,
                propPageContext
                );
            memoryContext->MemoryList = PhCreateList(40);
            memoryContext->ListViewHandle = lvHandle;

            PhSetListViewStyle(lvHandle, TRUE, TRUE);
            PhSetControlTheme(lvHandle, L"explorer");
            PhAddListViewColumn(lvHandle, 0, 0, 0, LVCFMT_LEFT, 140, L"Name");
            PhAddListViewColumn(lvHandle, 1, 1, 1, LVCFMT_LEFT, 100, L"Address");
            PhAddListViewColumn(lvHandle, 2, 2, 2, LVCFMT_LEFT, 60, L"Size");
            PhAddListViewColumn(lvHandle, 3, 3, 3, LVCFMT_LEFT, 60, L"Protection");

            PhSetExtendedListView(lvHandle);
            ExtendedListView_SetCompareFunction(lvHandle, 1, PhpMemoryAddressCompareFunction);
            ExtendedListView_SetCompareFunction(lvHandle, 2, PhpMemorySizeCompareFunction);
            PhLoadListViewColumnsFromSetting(L"MemoryListViewColumns", lvHandle);
            ExtendedListView_SetSort(lvHandle, 1, AscendingSortOrder);

            PhpRefreshProcessMemoryList(hwndDlg, propPageContext);
        }
        break;
    case WM_DESTROY:
        {
            PPH_MEMORY_CONTEXT memoryContext;

            memoryContext = propPageContext->Context;

            PhDereferenceObjects(memoryContext->MemoryList->Items, memoryContext->MemoryList->Count);
            PhDereferenceObject(memoryContext->MemoryList);
            PhDeleteMemoryProvider(&memoryContext->Provider);
            PhFree(memoryContext);

            PhSaveListViewColumnsToSetting(L"MemoryListViewColumns", lvHandle);

            PhpPropPageDlgProcDestroy(hwndDlg);
        }
        break;
    case WM_SHOWWINDOW:
        {
            if (!propPageContext->LayoutInitialized)
            {
                PPH_LAYOUT_ITEM dialogItem;

                dialogItem = PhAddPropPageLayoutItem(hwndDlg, hwndDlg,
                    PH_PROP_PAGE_TAB_CONTROL_PARENT, PH_ANCHOR_ALL);
                PhAddPropPageLayoutItem(hwndDlg, GetDlgItem(hwndDlg, IDC_REFRESH),
                    dialogItem, PH_ANCHOR_TOP | PH_ANCHOR_RIGHT);
                PhAddPropPageLayoutItem(hwndDlg, lvHandle,
                    dialogItem, PH_ANCHOR_ALL);

                PhDoPropPageLayout(hwndDlg);

                propPageContext->LayoutInitialized = TRUE;
            }
        }
        break;
    case WM_COMMAND:
        {
            switch (LOWORD(wParam))
            {
            case ID_MEMORY_READWRITEMEMORY:
                {
                    PPH_MEMORY_ITEM memoryItem = PhGetSelectedListViewItemParam(lvHandle);

                    if (memoryItem)
                    {
                        if (memoryItem->Flags & MEM_COMMIT)
                        {
                            PPH_SHOWMEMORYEDITOR showMemoryEditor = PhAllocate(sizeof(PH_SHOWMEMORYEDITOR));

                            showMemoryEditor->ProcessId = processItem->ProcessId;
                            showMemoryEditor->BaseAddress = memoryItem->BaseAddress;
                            showMemoryEditor->RegionSize = memoryItem->Size;
                            showMemoryEditor->SelectOffset = -1;
                            showMemoryEditor->SelectLength = 0;
                            ProcessHacker_ShowMemoryEditor(PhMainWndHandle, showMemoryEditor);
                        }
                        else
                        {
                            PhShowError(hwndDlg, L"Unable to edit the memory region because it is not committed.");
                        }
                    }
                }
                break;
            case ID_MEMORY_SAVE:
                {
                    NTSTATUS status;
                    HANDLE processHandle;
                    PPH_MEMORY_ITEM *memoryItems;
                    ULONG numberOfMemoryItems;

                    if (!NT_SUCCESS(status = PhOpenProcess(
                        &processHandle,
                        PROCESS_VM_READ,
                        processItem->ProcessId
                        )))
                    {
                        PhShowStatus(hwndDlg, L"Unable to open the process", status, 0);
                        break;
                    }

                    PhGetSelectedListViewItemParams(lvHandle, &memoryItems, &numberOfMemoryItems);

                    if (numberOfMemoryItems != 0)
                    {
                        static PH_FILETYPE_FILTER filters[] =
                        {
                            { L"Binary files (*.bin)", L"*.bin" },
                            { L"All files (*.*)", L"*.*" }
                        };
                        PVOID fileDialog;

                        fileDialog = PhCreateSaveFileDialog();

                        PhSetFileDialogFilter(fileDialog, filters, sizeof(filters) / sizeof(PH_FILETYPE_FILTER));
                        PhSetFileDialogFileName(fileDialog, PhaConcatStrings2(processItem->ProcessName->Buffer, L".bin")->Buffer);

                        if (PhShowFileDialog(hwndDlg, fileDialog))
                        {
                            PPH_STRING fileName;
                            PPH_FILE_STREAM fileStream;
                            PVOID buffer;
                            ULONG i;
                            ULONG_PTR offset;

                            fileName = PhGetFileDialogFileName(fileDialog);
                            PhaDereferenceObject(fileName);

                            if (NT_SUCCESS(status = PhCreateFileStream(
                                &fileStream,
                                fileName->Buffer,
                                FILE_GENERIC_WRITE,
                                FILE_SHARE_READ,
                                FILE_OVERWRITE_IF,
                                0
                                )))
                            {
                                buffer = PhAllocatePage(PAGE_SIZE, NULL);

                                // Go through each selected memory item and append the region contents 
                                // to the file.
                                for (i = 0; i < numberOfMemoryItems; i++)
                                {
                                    PPH_MEMORY_ITEM memoryItem = memoryItems[i];

                                    if (!(memoryItem->Flags & MEM_COMMIT))
                                        continue;

                                    for (offset = 0; offset < memoryItem->Size; offset += PAGE_SIZE)
                                    {
                                        if (NT_SUCCESS(PhReadVirtualMemory(
                                            processHandle,
                                            PTR_ADD_OFFSET(memoryItem->BaseAddress, offset),
                                            buffer,
                                            PAGE_SIZE,
                                            NULL
                                            )))
                                        {
                                            PhWriteFileStream(fileStream, buffer, PAGE_SIZE);
                                        }
                                    }
                                }

                                PhFreePage(buffer);

                                PhDereferenceObject(fileStream);
                            }

                            if (!NT_SUCCESS(status))
                                PhShowStatus(hwndDlg, L"Unable to create the file", status, 0);
                        }

                        PhFreeFileDialog(fileDialog);
                    }

                    PhFree(memoryItems);
                    NtClose(processHandle);
                }
                break;
            case ID_MEMORY_CHANGEPROTECTION:
                {
                    PPH_MEMORY_ITEM memoryItem = PhGetSelectedListViewItemParam(lvHandle);

                    if (memoryItem)
                    {
                        PhReferenceObject(memoryItem);

                        PhShowMemoryProtectDialog(hwndDlg, processItem, memoryItem);
                        PhpUpdateMemoryItemInListView(lvHandle, memoryItem);

                        PhDereferenceObject(memoryItem);
                    }
                }
                break;
            case ID_MEMORY_FREE:
                {
                    PPH_MEMORY_ITEM memoryItem = PhGetSelectedListViewItemParam(lvHandle);

                    if (memoryItem)
                    {
                        PhReferenceObject(memoryItem);
                        PhUiFreeMemory(hwndDlg, processItem->ProcessId, memoryItem, TRUE);
                        PhDereferenceObject(memoryItem);
                        // TODO: somehow update the list
                    }
                }
                break;
            case ID_MEMORY_DECOMMIT:
                {
                    PPH_MEMORY_ITEM memoryItem = PhGetSelectedListViewItemParam(lvHandle);

                    if (memoryItem)
                    {
                        PhReferenceObject(memoryItem);
                        PhUiFreeMemory(hwndDlg, processItem->ProcessId, memoryItem, FALSE);
                        PhDereferenceObject(memoryItem);
                    }
                }
                break;
            case ID_MEMORY_READWRITEADDRESS:
                {
                    PPH_MEMORY_CONTEXT memoryContext = propPageContext->Context;
                    PPH_STRING selectedChoice = NULL;

                    while (PhaChoiceDialog(
                        hwndDlg,
                        L"Read/Write Address",
                        L"Enter an address:",
                        NULL,
                        0,
                        NULL,
                        PH_CHOICE_DIALOG_USER_CHOICE,
                        &selectedChoice,
                        NULL,
                        L"MemoryReadWriteAddressChoices"
                        ))
                    {
                        ULONG64 address64;
                        ULONG_PTR address;

                        if (selectedChoice->Length == 0)
                            continue;

                        if (PhStringToInteger64(&selectedChoice->sr, 0, &address64))
                        {
                            ULONG i;
                            PPH_MEMORY_ITEM memoryItem = NULL;
                            BOOLEAN found = FALSE;

                            address = (ULONG_PTR)address64;

                            for (i = 0; i < memoryContext->MemoryList->Count; i++)
                            {
                                memoryItem = memoryContext->MemoryList->Items[i];

                                // Dumb linear search.
                                if (
                                    address >= (ULONG_PTR)memoryItem->BaseAddress &&
                                    address < (ULONG_PTR)memoryItem->BaseAddress + memoryItem->Size
                                    )
                                {
                                    found = TRUE;
                                    break;
                                }
                            }

                            if (found)
                            {
                                PPH_SHOWMEMORYEDITOR showMemoryEditor = PhAllocate(sizeof(PH_SHOWMEMORYEDITOR));

                                showMemoryEditor->ProcessId = processItem->ProcessId;
                                showMemoryEditor->BaseAddress = memoryItem->BaseAddress;
                                showMemoryEditor->RegionSize = memoryItem->Size;
                                showMemoryEditor->SelectOffset = (ULONG)(address - (ULONG_PTR)memoryItem->BaseAddress);
                                showMemoryEditor->SelectLength = 0;
                                ProcessHacker_ShowMemoryEditor(PhMainWndHandle, showMemoryEditor);
                                break;
                            }
                            else
                            {
                                PhShowError(hwndDlg, L"Unable to find the memory region for the selected address.");
                            }
                        }
                    }
                }
                break;
            case ID_MEMORY_COPY:
                {
                    PhCopyListView(lvHandle);
                }
                break;
            case IDC_REFRESH:
                PhpRefreshProcessMemoryList(hwndDlg, propPageContext);
                break;
            case IDC_STRINGS:
                PhShowMemoryStringDialog(hwndDlg, processItem);
                break;
            }
        }
        break;
    case WM_NOTIFY:
        {
            LPNMHDR header = (LPNMHDR)lParam;

            PhHandleListViewNotifyForCopy(lParam, lvHandle);

            switch (header->code)
            {
            case NM_DBLCLK:
                {
                    if (header->hwndFrom == lvHandle)
                    {
                        SendMessage(hwndDlg, WM_COMMAND, ID_MEMORY_READWRITEMEMORY, 0);
                    }
                }
                break;
            case NM_RCLICK:
                {
                    if (header->hwndFrom == lvHandle)
                    {
                        LPNMITEMACTIVATE itemActivate = (LPNMITEMACTIVATE)header;
                        PPH_MEMORY_ITEM *memoryItems;
                        ULONG numberOfMemoryItems;

                        PhGetSelectedListViewItemParams(lvHandle, &memoryItems, &numberOfMemoryItems);

                        // Allow menu to show when there are no items selected so 
                        // the user can use Read/Write Address.
                        //if (numberOfMemoryItems != 0)
                        {
                            PPH_EMENU menu;
                            PPH_EMENU_ITEM item;
                            POINT location;

                            menu = PhCreateEMenu();
                            PhLoadResourceEMenuItem(menu, PhInstanceHandle, MAKEINTRESOURCE(IDR_MEMORY), 0);
                            PhSetFlagsEMenuItem(menu, ID_MEMORY_READWRITEMEMORY, PH_EMENU_DEFAULT, PH_EMENU_DEFAULT);

                            PhpInitializeMemoryMenu(menu, processItem->ProcessId, memoryItems, numberOfMemoryItems);

                            if (PhPluginsEnabled)
                            {
                                PH_PLUGIN_MENU_INFORMATION menuInfo;

                                menuInfo.Menu = menu;
                                menuInfo.OwnerWindow = hwndDlg;
                                menuInfo.u.Memory.ProcessId = processItem->ProcessId;
                                menuInfo.u.Memory.MemoryItems = memoryItems;
                                menuInfo.u.Memory.NumberOfMemoryItems = numberOfMemoryItems;

                                PhInvokeCallback(PhGetGeneralCallback(GeneralCallbackMemoryMenuInitializing), &menuInfo);
                            }

                            location = itemActivate->ptAction;
                            ClientToScreen(lvHandle, &location);

                            item = PhShowEMenu(
                                menu,
                                hwndDlg,
                                PH_EMENU_SHOW_LEFTRIGHT,
                                PH_ALIGN_LEFT | PH_ALIGN_TOP,
                                location.x,
                                location.y
                                );

                            if (item)
                            {
                                BOOLEAN handled = FALSE;

                                if (PhPluginsEnabled)
                                    handled = PhPluginTriggerEMenuItem(hwndDlg, item);

                                if (!handled)
                                    SendMessage(hwndDlg, WM_COMMAND, item->Id, 0);
                            }

                            PhDestroyEMenu(menu);
                        }

                        PhFree(memoryItems);
                    }
                }
                break;
            }
        }
        break;
    }

    return FALSE;
}

INT_PTR CALLBACK PhpProcessEnvironmentDlgProc(
    __in HWND hwndDlg,
    __in UINT uMsg,
    __in WPARAM wParam,
    __in LPARAM lParam
    )
{
    LPPROPSHEETPAGE propSheetPage;
    PPH_PROCESS_PROPPAGECONTEXT propPageContext;
    PPH_PROCESS_ITEM processItem;

    if (!PhpPropPageDlgProcHeader(hwndDlg, uMsg, lParam,
        &propSheetPage, &propPageContext, &processItem))
        return FALSE;

    switch (uMsg)
    {
    case WM_INITDIALOG:
        {
            HANDLE processHandle;
            PPH_ENVIRONMENT_VARIABLE variables;
            ULONG numberOfVariables;
            ULONG i;
            HWND lvHandle = GetDlgItem(hwndDlg, IDC_LIST);

            PhSetListViewStyle(lvHandle, TRUE, TRUE);
            PhSetControlTheme(lvHandle, L"explorer");
            PhAddListViewColumn(lvHandle, 0, 0, 0, LVCFMT_LEFT, 140, L"Name");
            PhAddListViewColumn(lvHandle, 1, 1, 1, LVCFMT_LEFT, 200, L"Value");

            PhSetExtendedListView(lvHandle);
            PhLoadListViewColumnsFromSetting(L"EnvironmentListViewColumns", lvHandle);

            if (NT_SUCCESS(PhOpenProcess(
                &processHandle,
                PROCESS_QUERY_INFORMATION | PROCESS_VM_READ,
                processItem->ProcessId
                )))
            {
                ULONG flags;

                flags = 0;

#ifdef _M_X64
                if (processItem->IsWow64)
                    flags |= PH_GET_PROCESS_ENVIRONMENT_WOW64;
#endif

                if (NT_SUCCESS(PhGetProcessEnvironmentVariablesEx(
                    processHandle,
                    flags,
                    &variables,
                    &numberOfVariables
                    )))
                {
                    for (i = 0; i < numberOfVariables; i++)
                    {
                        INT lvItemIndex;

                        // Don't display pairs with no name.
                        if (variables[i].Name->Length == 0)
                            continue;

                        lvItemIndex = PhAddListViewItem(lvHandle, MAXINT, PhGetString(variables[i].Name), NULL);
                        PhSetListViewSubItem(lvHandle, lvItemIndex, 1, PhGetString(variables[i].Value));
                    }

                    PhFreeProcessEnvironmentVariables(variables, numberOfVariables);
                }

                NtClose(processHandle);
            }
        }
        break;
    case WM_DESTROY:
        {
            PhSaveListViewColumnsToSetting(L"EnvironmentListViewColumns", GetDlgItem(hwndDlg, IDC_LIST));
            PhpPropPageDlgProcDestroy(hwndDlg);
        }
        break;
    case WM_SHOWWINDOW:
        {
            if (!propPageContext->LayoutInitialized)
            {
                PPH_LAYOUT_ITEM dialogItem;

                dialogItem = PhAddPropPageLayoutItem(hwndDlg, hwndDlg,
                    PH_PROP_PAGE_TAB_CONTROL_PARENT, PH_ANCHOR_ALL);
                PhAddPropPageLayoutItem(hwndDlg, GetDlgItem(hwndDlg, IDC_LIST),
                    dialogItem, PH_ANCHOR_ALL);

                PhDoPropPageLayout(hwndDlg);

                propPageContext->LayoutInitialized = TRUE;
            }
        }
        break;
    case WM_NOTIFY:
        {
            PhHandleListViewNotifyForCopy(lParam, GetDlgItem(hwndDlg, IDC_LIST));
        }
        break;
    }

    return FALSE;
}

static VOID NTAPI HandleAddedHandler(
    __in_opt PVOID Parameter,
    __in_opt PVOID Context
    )
{
    PPH_HANDLES_CONTEXT handlesContext = (PPH_HANDLES_CONTEXT)Context;

    // Parameter contains a pointer to the added handle item.
    PhReferenceObject(Parameter);
    PostMessage(
        handlesContext->WindowHandle,
        WM_PH_HANDLE_ADDED,
        PhGetRunIdProvider(&handlesContext->ProviderRegistration),
        (LPARAM)Parameter
        );
}

static VOID NTAPI HandleModifiedHandler(
    __in_opt PVOID Parameter,
    __in_opt PVOID Context
    )
{
    PPH_HANDLES_CONTEXT handlesContext = (PPH_HANDLES_CONTEXT)Context;

    PostMessage(handlesContext->WindowHandle, WM_PH_HANDLE_MODIFIED, 0, (LPARAM)Parameter);
}

static VOID NTAPI HandleRemovedHandler(
    __in_opt PVOID Parameter,
    __in_opt PVOID Context
    )
{
    PPH_HANDLES_CONTEXT handlesContext = (PPH_HANDLES_CONTEXT)Context;

    PostMessage(handlesContext->WindowHandle, WM_PH_HANDLE_REMOVED, 0, (LPARAM)Parameter);
}

static VOID NTAPI HandlesUpdatedHandler(
    __in_opt PVOID Parameter,
    __in_opt PVOID Context
    )
{
    PPH_HANDLES_CONTEXT handlesContext = (PPH_HANDLES_CONTEXT)Context;

    PostMessage(handlesContext->WindowHandle, WM_PH_HANDLES_UPDATED, 0, 0);
}

VOID PhpInitializeHandleMenu(
    __in PPH_EMENU Menu,
    __in HANDLE ProcessId,
    __in PPH_HANDLE_ITEM *Handles,
    __in ULONG NumberOfHandles,
    __inout PPH_HANDLES_CONTEXT HandlesContext
    )
{
    PPH_EMENU_ITEM item;

    if (NumberOfHandles == 0)
    {
        PhSetFlagsAllEMenuItems(Menu, PH_EMENU_DISABLED, PH_EMENU_DISABLED);
    }
    else if (NumberOfHandles == 1)
    {
        // Nothing
    }
    else
    {
        PhSetFlagsAllEMenuItems(Menu, PH_EMENU_DISABLED, PH_EMENU_DISABLED);

        PhEnableEMenuItem(Menu, ID_HANDLE_CLOSE, TRUE);
        PhEnableEMenuItem(Menu, ID_HANDLE_COPY, TRUE);
    }

    // Remove irrelevant menu items.

    if (!PhKphHandle)
    {
        if (item = PhFindEMenuItem(Menu, 0, NULL, ID_HANDLE_PROTECTED))
            PhDestroyEMenuItem(item);
        if (item = PhFindEMenuItem(Menu, 0, NULL, ID_HANDLE_INHERIT))
            PhDestroyEMenuItem(item);
    }

    // Protected, Inherit
    if (NumberOfHandles == 1 && PhKphHandle)
    {
        HandlesContext->SelectedHandleProtected = FALSE;
        HandlesContext->SelectedHandleInherit = FALSE;

        if (Handles[0]->Attributes & OBJ_PROTECT_CLOSE)
        {
            HandlesContext->SelectedHandleProtected = TRUE;
            PhSetFlagsEMenuItem(Menu, ID_HANDLE_PROTECTED, PH_EMENU_CHECKED, PH_EMENU_CHECKED);
        }

        if (Handles[0]->Attributes & OBJ_INHERIT)
        {
            HandlesContext->SelectedHandleInherit = TRUE;
            PhSetFlagsEMenuItem(Menu, ID_HANDLE_INHERIT, PH_EMENU_CHECKED, PH_EMENU_CHECKED);
        }
    }
}

static INT NTAPI PhpHandleTypeCompareFunction(
    __in PVOID Item1,
    __in PVOID Item2,
    __in_opt PVOID Context
    )
{
    PPH_HANDLE_ITEM item1 = Item1;
    PPH_HANDLE_ITEM item2 = Item2;

    return PhCompareString(item1->TypeName, item2->TypeName, TRUE);
}

static INT NTAPI PhpHandleNameCompareFunction(
    __in PVOID Item1,
    __in PVOID Item2,
    __in_opt PVOID Context
    )
{
    PPH_HANDLE_ITEM item1 = Item1;
    PPH_HANDLE_ITEM item2 = Item2;

    return PhCompareString(item1->BestObjectName, item2->BestObjectName, TRUE);
}

static INT NTAPI PhpHandleHandleCompareFunction(
    __in PVOID Item1,
    __in PVOID Item2,
    __in_opt PVOID Context
    )
{
    PPH_HANDLE_ITEM item1 = Item1;
    PPH_HANDLE_ITEM item2 = Item2;

    return uintptrcmp((ULONG_PTR)item1->Handle, (ULONG_PTR)item2->Handle);
}

static COLORREF NTAPI PhpHandleColorFunction(
    __in INT Index,
    __in PVOID Param,
    __in_opt PVOID Context
    )
{
    PPH_HANDLE_ITEM item = Param;

    if (PhCsUseColorProtectedHandles && (item->Attributes & OBJ_PROTECT_CLOSE))
        return PhCsColorProtectedHandles;
    if (PhCsUseColorInheritHandles && (item->Attributes & OBJ_INHERIT))
        return PhCsColorInheritHandles;

    return PhSysWindowColor;
}

static VOID PhpAddHandleItem(
    __in HWND ListViewHandle,
    __in PPH_HANDLE_ITEM HandleItem
    )
{
    INT lvItemIndex;

    lvItemIndex = PhAddListViewItem(
        ListViewHandle,
        MAXINT,
        PhGetString(HandleItem->TypeName),
        HandleItem
        );
    PhSetListViewSubItem(ListViewHandle, lvItemIndex, 1, PhGetString(HandleItem->BestObjectName));
    PhSetListViewSubItem(ListViewHandle, lvItemIndex, 2, HandleItem->HandleString);
}

INT_PTR CALLBACK PhpProcessHandlesDlgProc(
    __in HWND hwndDlg,
    __in UINT uMsg,
    __in WPARAM wParam,
    __in LPARAM lParam
    )
{
    LPPROPSHEETPAGE propSheetPage;
    PPH_PROCESS_PROPPAGECONTEXT propPageContext;
    PPH_PROCESS_ITEM processItem;
    PPH_HANDLES_CONTEXT handlesContext;
    HWND lvHandle;

    if (PhpPropPageDlgProcHeader(hwndDlg, uMsg, lParam,
        &propSheetPage, &propPageContext, &processItem))
    {
        handlesContext = (PPH_HANDLES_CONTEXT)propPageContext->Context;
    }
    else
    {
        return FALSE;
    }

    lvHandle = GetDlgItem(hwndDlg, IDC_LIST);

    switch (uMsg)
    {
    case WM_INITDIALOG:
        {
            handlesContext = propPageContext->Context =
                PhAllocate(sizeof(PH_HANDLES_CONTEXT));

            handlesContext->Provider = PhCreateHandleProvider(
                processItem->ProcessId
                );
            PhRegisterProvider(
                &PhSecondaryProviderThread,
                PhHandleProviderUpdate,
                handlesContext->Provider,
                &handlesContext->ProviderRegistration
                );
            PhRegisterCallback(
                &handlesContext->Provider->HandleAddedEvent,
                HandleAddedHandler,
                handlesContext,
                &handlesContext->AddedEventRegistration
                );
            PhRegisterCallback(
                &handlesContext->Provider->HandleModifiedEvent,
                HandleModifiedHandler,
                handlesContext,
                &handlesContext->ModifiedEventRegistration
                );
            PhRegisterCallback(
                &handlesContext->Provider->HandleRemovedEvent,
                HandleRemovedHandler,
                handlesContext,
                &handlesContext->RemovedEventRegistration
                );
            PhRegisterCallback(
                &handlesContext->Provider->UpdatedEvent,
                HandlesUpdatedHandler,
                handlesContext,
                &handlesContext->UpdatedEventRegistration
                );
            handlesContext->WindowHandle = hwndDlg;
            handlesContext->HandleList = PhCreatePointerList(100);
            handlesContext->NeedsRedraw = FALSE;
            handlesContext->NeedsSort = FALSE;
            handlesContext->HideUnnamedHandles = !!PhGetIntegerSetting(L"HideUnnamedHandles");

            Button_SetCheck(GetDlgItem(hwndDlg, IDC_HIDEUNNAMEDHANDLES),
                handlesContext->HideUnnamedHandles ? BST_CHECKED : BST_UNCHECKED);

            // Initialize the list.
            PhSetListViewStyle(lvHandle, TRUE, TRUE);
            PhSetControlTheme(lvHandle, L"explorer");
            PhAddListViewColumn(lvHandle, 0, 0, 0, LVCFMT_LEFT, 100, L"Type");
            PhAddListViewColumn(lvHandle, 1, 1, 1, LVCFMT_LEFT, 200, L"Name"); 
            PhAddListViewColumn(lvHandle, 2, 2, 2, LVCFMT_LEFT, 80, L"Handle");

            PhSetExtendedListViewWithSettings(lvHandle);
            PhLoadListViewColumnsFromSetting(L"HandleListViewColumns", lvHandle);
            ExtendedListView_SetSortFast(lvHandle, TRUE);
            ExtendedListView_SetCompareFunction(lvHandle, 0, PhpHandleTypeCompareFunction);
            ExtendedListView_SetCompareFunction(lvHandle, 1, PhpHandleNameCompareFunction);
            ExtendedListView_SetCompareFunction(lvHandle, 2, PhpHandleHandleCompareFunction);
            ExtendedListView_SetItemColorFunction(lvHandle, PhpHandleColorFunction);
            ExtendedListView_SetStateHighlighting(lvHandle, TRUE);

            // Sort by Type, Handle, Name.
            {
                ULONG fallbackColumns[] = { 0, 2, 1 };

                ExtendedListView_AddFallbackColumns(lvHandle,
                    sizeof(fallbackColumns) / sizeof(ULONG), fallbackColumns);
            }

            PhSetEnabledProvider(&handlesContext->ProviderRegistration, TRUE);
            PhBoostProvider(&handlesContext->ProviderRegistration, NULL);
        }
        break;
    case WM_DESTROY:
        {
            PhUnregisterCallback(
                &handlesContext->Provider->HandleAddedEvent,
                &handlesContext->AddedEventRegistration
                );
            PhUnregisterCallback(
                &handlesContext->Provider->HandleModifiedEvent,
                &handlesContext->ModifiedEventRegistration
                );
            PhUnregisterCallback(
                &handlesContext->Provider->HandleRemovedEvent,
                &handlesContext->RemovedEventRegistration
                );
            PhUnregisterCallback(
                &handlesContext->Provider->UpdatedEvent,
                &handlesContext->UpdatedEventRegistration
                );
            PhUnregisterProvider(&handlesContext->ProviderRegistration);

            PhDereferenceAllHandleItems(handlesContext->Provider);

            PhDereferenceObject(handlesContext->Provider);
            PhDereferenceObject(handlesContext->HandleList);
            PhFree(handlesContext);

            PhSaveListViewColumnsToSetting(L"HandleListViewColumns", lvHandle);

            PhpPropPageDlgProcDestroy(hwndDlg);
        }
        break;
    case WM_SHOWWINDOW:
        {
            if (!propPageContext->LayoutInitialized)
            {
                PPH_LAYOUT_ITEM dialogItem;

                dialogItem = PhAddPropPageLayoutItem(hwndDlg, hwndDlg,
                    PH_PROP_PAGE_TAB_CONTROL_PARENT, PH_ANCHOR_ALL);
                PhAddPropPageLayoutItem(hwndDlg, GetDlgItem(hwndDlg, IDC_LIST),
                    dialogItem, PH_ANCHOR_ALL);

                PhDoPropPageLayout(hwndDlg);

                propPageContext->LayoutInitialized = TRUE;
            }
        }
        break;
    case WM_COMMAND:
        {
            INT id = LOWORD(wParam);

            switch (id)
            {
            case ID_HANDLE_CLOSE:
                {
                    PPH_HANDLE_ITEM *handles;
                    ULONG numberOfHandles;

                    PhGetSelectedListViewItemParams(lvHandle, &handles, &numberOfHandles);
                    PhReferenceObjects(handles, numberOfHandles);

                    if (PhUiCloseHandles(hwndDlg, processItem->ProcessId, handles, numberOfHandles, !!lParam))
                        PhSetStateAllListViewItems(lvHandle, 0, LVIS_SELECTED);

                    PhDereferenceObjects(handles, numberOfHandles);
                    PhFree(handles);
                }
                break;
            case ID_HANDLE_PROTECTED:
            case ID_HANDLE_INHERIT:
                {
                    PPH_HANDLE_ITEM handleItem = PhGetSelectedListViewItemParam(lvHandle);

                    if (handleItem)
                    {
                        ULONG attributes = 0;

                        // Re-create the attributes.

                        if (handlesContext->SelectedHandleProtected)
                            attributes |= OBJ_PROTECT_CLOSE;
                        if (handlesContext->SelectedHandleInherit)
                            attributes |= OBJ_INHERIT;

                        // Toggle the appropriate bit.

                        if (id == ID_HANDLE_PROTECTED)
                            attributes ^= OBJ_PROTECT_CLOSE;
                        else if (id == ID_HANDLE_INHERIT)
                            attributes ^= OBJ_INHERIT;

                        PhReferenceObject(handleItem);
                        PhUiSetAttributesHandle(hwndDlg, processItem->ProcessId, handleItem, attributes);
                        PhDereferenceObject(handleItem);
                    }
                }
                break;
            case ID_HANDLE_PROPERTIES:
                {
                    PPH_HANDLE_ITEM handleItem = PhGetSelectedListViewItemParam(lvHandle);

                    if (handleItem)
                    {
                        // The object relies on the list view reference, which could 
                        // disappear if we don't reference the object here.
                        PhReferenceObject(handleItem);
                        PhShowHandleProperties(hwndDlg, processItem->ProcessId, handleItem);
                        PhDereferenceObject(handleItem);
                    }
                }
                break;
            case ID_HANDLE_COPY:
                {
                    PhCopyListView(lvHandle);
                }
                break;
            case IDC_HIDEUNNAMEDHANDLES:
                {
                    BOOLEAN hide = Button_GetCheck(GetDlgItem(hwndDlg, IDC_HIDEUNNAMEDHANDLES)) == BST_CHECKED;
                    ULONG enumerationKey;
                    PPH_HANDLE_ITEM handleItem;

                    handlesContext->HideUnnamedHandles = hide;

                    SetCursor(LoadCursor(NULL, IDC_WAIT));
                    ExtendedListView_SetRedraw(lvHandle, FALSE);

                    if (!hide)
                    {
                        // Add all hidden handle items from the list.

                        ExtendedListView_SetStateHighlighting(lvHandle, FALSE);

                        enumerationKey = 0;

                        while (PhEnumPointerList(handlesContext->HandleList, &enumerationKey, &handleItem))
                        {
                            if (PhIsNullOrEmptyString(handleItem->BestObjectName))
                            {
                                PhpAddHandleItem(lvHandle, handleItem);
                            }
                        }

                        ExtendedListView_SetStateHighlighting(lvHandle, TRUE);
                        ExtendedListView_SortItems(lvHandle);
                    }
                    else
                    {
                        // Find all items without a name and remove them.

                        ExtendedListView_SetStateHighlighting(lvHandle, FALSE);

                        enumerationKey = 0;

                        while (PhEnumPointerList(handlesContext->HandleList, &enumerationKey, &handleItem))
                        {
                            if (PhIsNullOrEmptyString(handleItem->BestObjectName))
                            {
                                PhRemoveListViewItem(
                                    lvHandle,
                                    PhFindListViewItemByParam(lvHandle, -1, handleItem)
                                    );
                            }
                        }

                        ExtendedListView_SetStateHighlighting(lvHandle, TRUE);
                    }

                    ExtendedListView_SetRedraw(lvHandle, TRUE);
                    SetCursor(LoadCursor(NULL, IDC_ARROW));
                }
                break;
            }
        }
        break;
    case WM_NOTIFY:
        {
            LPNMHDR header = (LPNMHDR)lParam;

            switch (header->code)
            {
            case PSN_SETACTIVE:
                PhSetEnabledProvider(&handlesContext->ProviderRegistration, TRUE);
                break;
            case PSN_KILLACTIVE:
                PhSetEnabledProvider(&handlesContext->ProviderRegistration, FALSE);
                break;
            case NM_DBLCLK:
                {
                    if (header->hwndFrom == lvHandle)
                    {
                        SendMessage(hwndDlg, WM_COMMAND, ID_HANDLE_PROPERTIES, 0);
                    }
                }
                break;
            case NM_RCLICK:
                {
                    if (header->hwndFrom == lvHandle)
                    {
                        LPNMITEMACTIVATE itemActivate = (LPNMITEMACTIVATE)header;
                        PPH_HANDLE_ITEM *handles;
                        ULONG numberOfHandles;

                        PhGetSelectedListViewItemParams(lvHandle, &handles, &numberOfHandles);

                        if (numberOfHandles != 0)
                        {
                            PPH_EMENU menu;
                            PPH_EMENU_ITEM item;
                            POINT location;

                            menu = PhCreateEMenu();
                            PhLoadResourceEMenuItem(menu, PhInstanceHandle, MAKEINTRESOURCE(IDR_HANDLE), 0);
                            PhSetFlagsEMenuItem(menu, ID_HANDLE_PROPERTIES, PH_EMENU_DEFAULT, PH_EMENU_DEFAULT);

                            PhpInitializeHandleMenu(
                                menu,
                                processItem->ProcessId,
                                handles,
                                numberOfHandles,
                                handlesContext
                                );

                            if (PhPluginsEnabled)
                            {
                                PH_PLUGIN_MENU_INFORMATION menuInfo;

                                menuInfo.Menu = menu;
                                menuInfo.OwnerWindow = hwndDlg;
                                menuInfo.u.Handle.ProcessId = processItem->ProcessId;
                                menuInfo.u.Handle.Handles = handles;
                                menuInfo.u.Handle.NumberOfHandles = numberOfHandles;

                                PhInvokeCallback(PhGetGeneralCallback(GeneralCallbackHandleMenuInitializing), &menuInfo);
                            }

                            location = itemActivate->ptAction;
                            ClientToScreen(lvHandle, &location);

                            item = PhShowEMenu(
                                menu,
                                hwndDlg,
                                PH_EMENU_SHOW_LEFTRIGHT,
                                PH_ALIGN_LEFT | PH_ALIGN_TOP,
                                location.x,
                                location.y
                                );

                            if (item)
                            {
                                BOOLEAN handled = FALSE;

                                if (PhPluginsEnabled)
                                    handled = PhPluginTriggerEMenuItem(hwndDlg, item);

                                if (!handled)
                                    SendMessage(hwndDlg, WM_COMMAND, item->Id, 0);
                            }

                            PhDestroyEMenu(menu);
                        }

                        PhFree(handles);
                    }
                }
                break;
            case LVN_KEYDOWN:
                {
                    if (header->hwndFrom == lvHandle)
                    {
                        LPNMLVKEYDOWN keyDown = (LPNMLVKEYDOWN)header;

                        switch (keyDown->wVKey)
                        {
                        case 'C':
                            if (GetKeyState(VK_CONTROL) < 0)
                                SendMessage(hwndDlg, WM_COMMAND, ID_HANDLE_COPY, 0);
                            break;
                        case VK_DELETE:
                            // Pass a 1 in lParam to indicate that warnings should be 
                            // enabled.
                            SendMessage(hwndDlg, WM_COMMAND, ID_HANDLE_CLOSE, 1);
                            break;
                        }
                    }
                }
                break;
            }
        }
        break;
    case WM_PH_HANDLE_ADDED:
        {
            ULONG runId = (ULONG)wParam;
            PPH_HANDLE_ITEM handleItem = (PPH_HANDLE_ITEM)lParam;

            PhAddItemPointerList(handlesContext->HandleList, handleItem);

            // If we're hiding unnamed handles and this handle doesn't 
            // have a name, don't add it.
            if (
                handlesContext->HideUnnamedHandles &&
                PhIsNullOrEmptyString(handleItem->BestObjectName)
                )
            {
                // No need to dereference; if we re-add the handle when 
                // the user changes the "hide unnamed handles" setting 
                // we can just assume we have a reference to the handle 
                // item. This is important for consistency with the 
                // PhDereferenceAllHandleItems call in WM_DESTROY.
                break;
            }

            if (!handlesContext->NeedsRedraw)
            {
                ExtendedListView_SetRedraw(lvHandle, FALSE);
                handlesContext->NeedsRedraw = TRUE;
            }

            if (runId == 1) ExtendedListView_SetStateHighlighting(lvHandle, FALSE);
            PhpAddHandleItem(lvHandle, handleItem);
            if (runId == 1) ExtendedListView_SetStateHighlighting(lvHandle, TRUE);

            handlesContext->NeedsSort = TRUE;
        }
        break;
    case WM_PH_HANDLE_MODIFIED:
        {
            INT lvItemIndex;
            PPH_HANDLE_ITEM handleItem = (PPH_HANDLE_ITEM)lParam;

            lvItemIndex = PhFindListViewItemByParam(lvHandle, -1, handleItem);

            if (lvItemIndex != -1)
            {
                // Force redraw of the item because its color may have 
                // changed.
                ListView_RedrawItems(lvHandle, lvItemIndex, lvItemIndex);
            }
        }
        break;
    case WM_PH_HANDLE_REMOVED:
        {
            PPH_HANDLE_ITEM handleItem = (PPH_HANDLE_ITEM)lParam;
            HANDLE pointerHandle;

            if (!handlesContext->NeedsRedraw)
            {
                ExtendedListView_SetRedraw(lvHandle, FALSE);
                handlesContext->NeedsRedraw = TRUE;
            }

            PhRemoveListViewItem(
                lvHandle,
                PhFindListViewItemByParam(lvHandle, -1, handleItem)
                );

            if (pointerHandle = PhFindItemPointerList(handlesContext->HandleList, handleItem))
                PhRemoveItemPointerList(handlesContext->HandleList, pointerHandle);

            PhDereferenceObject(handleItem);
        }
        break;
    case WM_PH_HANDLES_UPDATED:
        {
            ExtendedListView_Tick(lvHandle);

            if (handlesContext->NeedsSort)
            {
                ExtendedListView_SortItems(lvHandle);
                handlesContext->NeedsSort = FALSE;
            }

            if (handlesContext->NeedsRedraw)
            {
                ExtendedListView_SetRedraw(lvHandle, TRUE);
                handlesContext->NeedsRedraw = FALSE;
            }
        }
        break;
    }

    REFLECT_MESSAGE_DLG(hwndDlg, lvHandle, uMsg, wParam, lParam);

    return FALSE;
}

static NTSTATUS NTAPI PhpOpenProcessJob(
    __out PHANDLE Handle,
    __in ACCESS_MASK DesiredAccess,
    __in_opt PVOID Context
    )
{
    NTSTATUS status;
    HANDLE processHandle;
    HANDLE jobHandle = NULL;

    if (!NT_SUCCESS(status = PhOpenProcess(
        &processHandle,
        ProcessQueryAccess,
        (HANDLE)Context
        )))
        return status;

    status = KphOpenProcessJob(PhKphHandle, &jobHandle, processHandle, DesiredAccess);
    NtClose(processHandle);

    if (NT_SUCCESS(status) && status != STATUS_PROCESS_NOT_IN_JOB && jobHandle)
    {
        *Handle = jobHandle;
    }
    else if (NT_SUCCESS(status))
    {
        status = STATUS_UNSUCCESSFUL;
    }

    return status;
}

INT_PTR CALLBACK PhpProcessJobHookProc(
    __in HWND hwndDlg,
    __in UINT uMsg,
    __in WPARAM wParam,
    __in LPARAM lParam
    )
{
    switch (uMsg)
    {
    case WM_DESTROY:
        {
            RemoveProp(hwndDlg, L"LayoutInitialized");
        }
        break;
    case WM_SHOWWINDOW:
        {
            if (!GetProp(hwndDlg, L"LayoutInitialized"))
            {
                PPH_LAYOUT_ITEM dialogItem;

                // This is a big violation of abstraction...

                dialogItem = PhAddPropPageLayoutItem(hwndDlg, hwndDlg,
                    PH_PROP_PAGE_TAB_CONTROL_PARENT, PH_ANCHOR_ALL);
                PhAddPropPageLayoutItem(hwndDlg, GetDlgItem(hwndDlg, IDC_NAME),
                    dialogItem, PH_ANCHOR_LEFT | PH_ANCHOR_TOP | PH_ANCHOR_RIGHT);
                PhAddPropPageLayoutItem(hwndDlg, GetDlgItem(hwndDlg, IDC_TERMINATE),
                    dialogItem, PH_ANCHOR_TOP | PH_ANCHOR_RIGHT);
                PhAddPropPageLayoutItem(hwndDlg, GetDlgItem(hwndDlg, IDC_PROCESSES),
                    dialogItem, PH_ANCHOR_LEFT | PH_ANCHOR_TOP | PH_ANCHOR_RIGHT);
                PhAddPropPageLayoutItem(hwndDlg, GetDlgItem(hwndDlg, IDC_ADD),
                    dialogItem, PH_ANCHOR_TOP | PH_ANCHOR_RIGHT);
                PhAddPropPageLayoutItem(hwndDlg, GetDlgItem(hwndDlg, IDC_LIMITS),
                    dialogItem, PH_ANCHOR_ALL);
                PhAddPropPageLayoutItem(hwndDlg, GetDlgItem(hwndDlg, IDC_ADVANCED),
                    dialogItem, PH_ANCHOR_RIGHT | PH_ANCHOR_BOTTOM);

                PhDoPropPageLayout(hwndDlg);

                SetProp(hwndDlg, L"LayoutInitialized", (HANDLE)TRUE);
            }
        }
        break;
    }

    return FALSE;
}

static VOID PhpLayoutServiceListControl(
    __in HWND hwndDlg,
    __in HWND ServiceListHandle
    )
{
    RECT rect;

    GetWindowRect(GetDlgItem(hwndDlg, IDC_SERVICES_LAYOUT), &rect);
    MapWindowPoints(NULL, hwndDlg, (POINT *)&rect, 2);

    MoveWindow(
        ServiceListHandle,
        rect.left,
        rect.top,
        rect.right - rect.left,
        rect.bottom - rect.top,
        TRUE
        );
}

INT_PTR CALLBACK PhpProcessServicesDlgProc(
    __in HWND hwndDlg,
    __in UINT uMsg,
    __in WPARAM wParam,
    __in LPARAM lParam
    )
{
    LPPROPSHEETPAGE propSheetPage;
    PPH_PROCESS_PROPPAGECONTEXT propPageContext;
    PPH_PROCESS_ITEM processItem;

    if (!PhpPropPageDlgProcHeader(hwndDlg, uMsg, lParam,
        &propSheetPage, &propPageContext, &processItem))
    {
        return FALSE;
    }

    switch (uMsg)
    {
    case WM_INITDIALOG:
        {
            PPH_SERVICE_ITEM *services;
            ULONG numberOfServices;
            ULONG i;
            HWND serviceListHandle;

            // Get a copy of the process' service list.

            PhAcquireQueuedLockShared(&processItem->ServiceListLock);

            numberOfServices = processItem->ServiceList->Count;
            services = PhAllocate(numberOfServices * sizeof(PPH_SERVICE_ITEM));

            {
                ULONG enumerationKey = 0;
                PPH_SERVICE_ITEM serviceItem;

                i = 0;

                while (PhEnumPointerList(processItem->ServiceList, &enumerationKey, &serviceItem))
                {
                    PhReferenceObject(serviceItem);
                    services[i++] = serviceItem;
                }
            }

            PhReleaseQueuedLockShared(&processItem->ServiceListLock);

            serviceListHandle = PhCreateServiceListControl(
                hwndDlg,
                services,
                numberOfServices
                );
            SendMessage(serviceListHandle, WM_PH_SET_LIST_VIEW_SETTINGS, 0, (LPARAM)L"ProcessServiceListViewColumns");
            ShowWindow(serviceListHandle, SW_SHOW);

            propPageContext->Context = serviceListHandle;
        }
        break;
    case WM_DESTROY:
        {
            PhpPropPageDlgProcDestroy(hwndDlg);
        }
        break;
    case WM_SHOWWINDOW:
        {
            if (!propPageContext->LayoutInitialized)
            {
                PPH_LAYOUT_ITEM dialogItem;

                dialogItem = PhAddPropPageLayoutItem(hwndDlg, hwndDlg,
                    PH_PROP_PAGE_TAB_CONTROL_PARENT, PH_ANCHOR_ALL);
                PhAddPropPageLayoutItem(hwndDlg, GetDlgItem(hwndDlg, IDC_SERVICES_LAYOUT),
                    dialogItem, PH_ANCHOR_ALL);

                PhDoPropPageLayout(hwndDlg);
                PhpLayoutServiceListControl(hwndDlg, (HWND)propPageContext->Context);

                propPageContext->LayoutInitialized = TRUE;
            }
        }
        break;
    case WM_SIZE:
        {
            PhpLayoutServiceListControl(hwndDlg, (HWND)propPageContext->Context);
        }
        break;
    }

    return FALSE;
}

NTSTATUS PhpProcessPropertiesThreadStart(
    __in PVOID Parameter
    )
{
    PH_AUTO_POOL autoPool;
    PPH_PROCESS_PROPCONTEXT PropContext = (PPH_PROCESS_PROPCONTEXT)Parameter;
    PPH_PROCESS_PROPPAGECONTEXT newPage;
    PPH_STRING startPage;
    HWND hwnd;
    BOOL result;
    MSG message;

    PhInitializeAutoPool(&autoPool);

    // Wait for stage 1 to be processed.
    PhWaitForEvent(&PropContext->ProcessItem->Stage1Event, NULL);
    // Refresh the icon which may have been updated due to 
    // stage 1.
    PhRefreshProcessPropContext(PropContext);

    // Add the pages...

    // General
    newPage = PhCreateProcessPropPageContext(
        MAKEINTRESOURCE(IDD_PROCGENERAL),
        PhpProcessGeneralDlgProc,
        NULL
        );
    PhAddProcessPropPage(PropContext, newPage);

    // Statistics
    newPage = PhCreateProcessPropPageContext(
        MAKEINTRESOURCE(IDD_PROCSTATISTICS),
        PhpProcessStatisticsDlgProc,
        NULL
        );
    PhAddProcessPropPage(PropContext, newPage);

    // Performance
    newPage = PhCreateProcessPropPageContext(
        MAKEINTRESOURCE(IDD_PROCPERFORMANCE),
        PhpProcessPerformanceDlgProc,
        NULL
        );
    PhAddProcessPropPage(PropContext, newPage);

    // Threads
    newPage = PhCreateProcessPropPageContext(
        MAKEINTRESOURCE(IDD_PROCTHREADS),
        PhpProcessThreadsDlgProc,
        NULL
        );
    PhAddProcessPropPage(PropContext, newPage);

    // Token
    PhAddProcessPropPage2(
        PropContext,
        PhCreateTokenPage(PhpOpenProcessToken, (PVOID)PropContext->ProcessItem->ProcessId, PhpProcessTokenHookProc)
        );

    // Modules
    newPage = PhCreateProcessPropPageContext(
        MAKEINTRESOURCE(IDD_PROCMODULES),
        PhpProcessModulesDlgProc,
        NULL
        );
    PhAddProcessPropPage(PropContext, newPage);

    // Memory
    newPage = PhCreateProcessPropPageContext(
        MAKEINTRESOURCE(IDD_PROCMEMORY),
        PhpProcessMemoryDlgProc,
        NULL
        );
    PhAddProcessPropPage(PropContext, newPage);

    // Environment
    newPage = PhCreateProcessPropPageContext(
        MAKEINTRESOURCE(IDD_PROCENVIRONMENT),
        PhpProcessEnvironmentDlgProc,
        NULL
        );
    PhAddProcessPropPage(PropContext, newPage);

    // Handles
    newPage = PhCreateProcessPropPageContext(
        MAKEINTRESOURCE(IDD_PROCHANDLES),
        PhpProcessHandlesDlgProc,
        NULL
        );
    PhAddProcessPropPage(PropContext, newPage);

    // Job
    if (
        PropContext->ProcessItem->IsInJob &&
        // There's no way the job page can function without KPH since it needs 
        // to open a handle to the job.
        PhKphHandle
        )
    {
        PhAddProcessPropPage2(
            PropContext,
            PhCreateJobPage(PhpOpenProcessJob, (PVOID)PropContext->ProcessItem->ProcessId, PhpProcessJobHookProc)
            );
    }

    // Services
    if (PropContext->ProcessItem->ServiceList && PropContext->ProcessItem->ServiceList->Count != 0)
    {
        newPage = PhCreateProcessPropPageContext(
            MAKEINTRESOURCE(IDD_PROCSERVICES),
            PhpProcessServicesDlgProc,
            NULL
            );
        PhAddProcessPropPage(PropContext, newPage);
    }

    // Plugin-supplied pages
    if (PhPluginsEnabled)
    {
        PH_PLUGIN_PROCESS_PROPCONTEXT pluginProcessPropContext;

        pluginProcessPropContext.PropContext = PropContext;
        pluginProcessPropContext.ProcessItem = PropContext->ProcessItem;

        PhInvokeCallback(PhGetGeneralCallback(GeneralCallbackProcessPropertiesInitializing), &pluginProcessPropContext);
    }

    // Create the property sheet

    if (PropContext->SelectThreadId)
        PhSetStringSetting(L"ProcPropPage", L"Threads");

    startPage = PhGetStringSetting(L"ProcPropPage");
    PropContext->PropSheetHeader.dwFlags |= PSH_USEPSTARTPAGE;
    PropContext->PropSheetHeader.pStartPage = startPage->Buffer;

    hwnd = (HWND)PropertySheet(&PropContext->PropSheetHeader);

    PhDereferenceObject(startPage);

    PropContext->WindowHandle = hwnd;
    PhSetEvent(&PropContext->CreatedEvent);

    // Main event loop

    while (result = GetMessage(&message, NULL, 0, 0))
    {
        if (result == -1)
            break;

        if (!PropSheet_IsDialogMessage(hwnd, &message))
        {
            TranslateMessage(&message);
            DispatchMessage(&message);
        }

        PhDrainAutoPool(&autoPool);

        // Destroy the window when necessary.
        if (!PropSheet_GetCurrentPageHwnd(hwnd))
        {
            DestroyWindow(hwnd);
            break;
        }
    }

    PhDereferenceObject(PropContext);

    PhDeleteAutoPool(&autoPool);

    return STATUS_SUCCESS;
}

BOOLEAN PhShowProcessProperties(
    __in PPH_PROCESS_PROPCONTEXT Context
    )
{
    HANDLE threadHandle;

    PhReferenceObject(Context);
    threadHandle = PhCreateThread(0, PhpProcessPropertiesThreadStart, Context);

    if (threadHandle)
    {
        NtClose(threadHandle);
        return TRUE;
    }
    else
    {
        PhDereferenceObject(Context);
        return FALSE;
    }
}
