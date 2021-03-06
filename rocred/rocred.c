// -----------------------------------------------------------------
// RO Credentials (ROCred)
// (c) 2012+ Ai4rei/AN
//
// -----------------------------------------------------------------

#include <stdio.h>

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <wincred.h>

#include <btypes.h>
#include <bvcstr.h>
#include <dlgabout.h>
#include <dlgtempl.h>
#include <macaddr.h>
#include <md5.h>
#include <memtaf.h>
#include <w32dll.h>  /* needed for w32cred */
#include <w32cred.h>
#include <w32ex.h>
#include <w32ui.h>
#include <xf_binhex.h>

#include "bgskin.h"
#include "button.h"
#include "config.h"
#include "rocred.h"

static const DLGTEMPLATEITEMINFO l_DlgItems[] =
{
    { DLGTEMPLATEITEM_CLASS_STATIC, 0,                                                  0, IDS_USERNAME,    7,      10, 60,     8,  },
    { DLGTEMPLATEITEM_CLASS_EDIT,   ES_AUTOHSCROLL|WS_BORDER|WS_TABSTOP,                0, IDC_USERNAME,    73,     7,  110,    14, },
    { DLGTEMPLATEITEM_CLASS_STATIC, 0,                                                  0, IDS_PASSWORD,    7,      28, 60,     8,  },
    { DLGTEMPLATEITEM_CLASS_EDIT,   ES_AUTOHSCROLL|ES_PASSWORD|WS_BORDER|WS_TABSTOP,    0, IDC_PASSWORD,    73,     25, 110,    14, },
    { DLGTEMPLATEITEM_CLASS_BUTTON, BS_AUTOCHECKBOX|WS_TABSTOP,                         0, IDC_CHECKSAVE,   73,     43, 110,    10, },
    { DLGTEMPLATEITEM_CLASS_BUTTON, BS_DEFPUSHBUTTON|WS_TABSTOP,                        0, IDOK,            79,     61, 50,     14, },
    { DLGTEMPLATEITEM_CLASS_BUTTON, BS_PUSHBUTTON|WS_TABSTOP,                           0, IDCANCEL,        133,    61, 50,     14, },
};
static const DLGTEMPLATEINFO l_DlgTempl =
{
    L"Tahoma",
    DS_CENTER|DS_SETFONT|WS_POPUPWINDOW|WS_CAPTION,
    0,
    __ARRAYSIZE(l_DlgItems),
    9,
    0,
    0,
    190,
    82,
    l_DlgItems,
};

typedef enum MISCINFOOPTION
{
    MISCINFO_OPT_MACADDRESS = 0x1,
}
MISCINFOOPTION;
static const UINT l_uMiscInfoOptName[] =
{
    IDS_MISCINFO_OPT_MACADDRESS,
};

// DefDlgEx: DialogProc message cracker system (WindowsX).
static BOOL l_bDefDlgEx = FALSE;
static LRESULT CALLBACK DefWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

int __stdcall MsgBox(HWND hWnd, LPSTR lpszText, DWORD dwFlags)
{
    char szTitle[64];
    char szMessage[4096];
    HINSTANCE hInstance = hWnd ? GetWindowInstance(hWnd) : GetModuleHandle(NULL);

    LoadStringA(hInstance, IDS_TITLE, szTitle, __ARRAYSIZE(szTitle));

    if((size_t)lpszText<=(WORD)-1)
    {// resource string, see MAKEINTRESOURCE on how this works
        LoadStringA(hInstance, (size_t)lpszText, szMessage, __ARRAYSIZE(szMessage));
        lpszText = szMessage;
    }

    return MessageBoxA(hWnd, lpszText, szTitle, dwFlags);
}

bool __stdcall GetFileClassFromExtension(const char* lpszExtension, char* lpszBuffer, size_t uBufferSize)
{
    bool bSuccess = false;

    if(lpszExtension[0]=='.')
    {// it's a file extension
        HKEY hKey;

        if(ERROR_SUCCESS==RegOpenKeyExA(HKEY_CLASSES_ROOT, lpszExtension, 0, KEY_QUERY_VALUE, &hKey))
        {
            if(ERROR_SUCCESS==RegQueryValueExA(hKey, NULL, NULL, NULL, lpszBuffer, &uBufferSize))
            {
                bSuccess = true;
            }

            RegCloseKey(hKey);
        }
    }

    return bSuccess;
}

static bool __stdcall MiscInfoAgreePrompt(HWND hWnd)
{
    char szBuffer[256*3+128];
    char szPrefix[256], szSuffix[256], szInfo[256] = { 0 };
    int nMiscInfo, nMiscInfoAgree;
    unsigned long luIdx;
    HINSTANCE hInstance = GetWindowInstance(hWnd);

    nMiscInfo = ConfigGetInt("MiscInfo");
    nMiscInfoAgree = ConfigGetInt("_MiscInfoLastAgreed");

    if(nMiscInfo==nMiscInfoAgree)
    {
        return true;
    }

    LoadStringA(hInstance, IDS_MISCINFO_PROMPT_PREFIX, szPrefix, __ARRAYSIZE(szPrefix));
    LoadStringA(hInstance, IDS_MISCINFO_PROMPT_SUFFIX, szSuffix, __ARRAYSIZE(szSuffix));

    for(luIdx = 0; luIdx<__ARRAYSIZE(l_uMiscInfoOptName); luIdx++)
    {
        if(nMiscInfo&(1<<luIdx))
        {
            lstrcatA(szInfo, "\r\n\t- ");
            LoadStringA(hInstance, l_uMiscInfoOptName[luIdx], szInfo+lstrlenA(szInfo), __ARRAYSIZE(szInfo)-lstrlenA(szInfo));
        }
    }

    wsprintfA(szBuffer, "%s%s\r\n\r\n%s", szPrefix, szInfo, szSuffix);

    if(MsgBox(hWnd, szBuffer, MB_YESNO|MB_ICONQUESTION|MB_DEFBUTTON2)==IDYES)
    {
        ConfigSetInt("_MiscInfoLastAgreed", nMiscInfo);
        return true;
    }

    return false;
}

static void __stdcall CombineExePathName(char* lpszExePath, unsigned long luExePathSize, const char* lpszExeName)
{
    char* lpszSlash = lpszExePath+GetModuleFileNameA(NULL, lpszExePath, luExePathSize);

    for(; lpszSlash[-1]!='\\'; lpszSlash--);

    lstrcpyA(lpszSlash, lpszExeName);
}

// Waits for an process to exit, while keeping the application idle,
// responsive and hidden.
static void __stdcall IdleWaitProcess(HWND hWnd, HANDLE hProcess)
{
    bool bTrayIcon = !ConfigGetInt("PolicyNoTrayIcon");
    HINSTANCE hInstance = GetWindowInstance(hWnd);
    NOTIFYICONDATA Nid = { sizeof(Nid), hWnd, 1, NIF_ICON|NIF_TIP };

    ShowWindow(hWnd, SW_MINIMIZE);
    Sleep(200);  // animation
    ShowWindow(hWnd, SW_HIDE);

    if(bTrayIcon)
    {
        // set up notification icon (no 'auto restore' or 'load later')
        Nid.hIcon = LoadSmallIcon(hInstance, MAKEINTRESOURCE(IDI_MAINICON));
        LoadStringA(hInstance, IDS_TITLE, Nid.szTip, __ARRAYSIZE(Nid.szTip));
        Shell_NotifyIcon(NIM_ADD, &Nid);
    }

    // go idle
    SetPriorityClass(GetCurrentProcess(), IDLE_PRIORITY_CLASS);

    // stay aware and wait for process to exit
    while(MsgWaitForMultipleObjects(1, &hProcess, FALSE, INFINITE, QS_ALLINPUT)!=WAIT_OBJECT_0)
    {
        MSG Msg;

        while(PeekMessage(&Msg, NULL, 0, 0, PM_REMOVE))
        {
            if(!IsDialogMessage(hWnd, &Msg))
            {
                TranslateMessage(&Msg);
                DispatchMessage(&Msg);
            }
        }
    }

    // return to normal
    SetPriorityClass(GetCurrentProcess(), NORMAL_PRIORITY_CLASS);

    if(bTrayIcon)
    {
        Shell_NotifyIcon(NIM_DELETE, &Nid);
    }

    ShowWindow(hWnd, SW_SHOWMINIMIZED);
    ShowWindow(hWnd, SW_RESTORE);
}

// Run and execute process.
static bool __cdecl InvokeProcess(HWND hWnd, const char* lpszApplication, const char* lpszParamFmt, ...)
{
    bool bSuccess = false;
    char szBuffer[1024], szFileClass[MAX_REGISTRY_KEY_SIZE+1];
    va_list lpVl;
    SHELLEXECUTEINFO Sei = { sizeof(Sei) };

    va_start(lpVl, lpszParamFmt);
    wvsprintfA(szBuffer, lpszParamFmt, lpVl);
    va_end(lpVl);

    Sei.fMask = SEE_MASK_NOCLOSEPROCESS|SEE_MASK_FLAG_NO_UI|(ISUNCPATH(lpszApplication) ? SEE_MASK_CONNECTNETDRV : 0)|SEE_MASK_CLASSNAME;
    Sei.hwnd = hWnd;
    Sei.lpVerb = NULL;
    Sei.lpFile = lpszApplication;
    Sei.lpParameters = szBuffer;
    Sei.nShow = SW_SHOWNORMAL;

    // consider the file an executable, whether or not it looks like it
    if(GetFileClassFromExtension(".exe", szFileClass, __ARRAYSIZE(szFileClass)))
    {
        Sei.lpClass = szFileClass;
    }
    else
    {// should not happen, fallback to default
        Sei.lpClass = "exefile";
    }

    if(ShellExecuteEx(&Sei))
    {
        if(Sei.hProcess)
        {
            IdleWaitProcess(hWnd, Sei.hProcess);
            CloseHandle(Sei.hProcess);

            bSuccess = true;
        }
    }
    else if(GetLastError()==ERROR_CANCELLED)
    {
        // Purpose: The user explicitely canceled a shell UI, such
        // as "Find application to run this file" or UAC prompt,
        // preceding the actual process invocation. As per UX
        // guidelines, such action should not result in a follow up
        // error message.
        ;
    }
    else
    {
        char szMessage[4096];
        char szFormat[256];
        char szError[1024];

        FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM, 0, GetLastError(), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), szError, __ARRAYSIZE(szError), NULL);
        LoadStringA(GetWindowInstance(hWnd), IDS_EXE_ERROR, szFormat, __ARRAYSIZE(szFormat));
        wsprintfA(szMessage, szFormat, lpszApplication, szBuffer, szError);
        MsgBox(hWnd, szMessage, MB_ICONSTOP|MB_OK);

        ZeroMemory((void*)(volatile void*)szMessage, sizeof(szMessage));
    }

    ZeroMemory((void*)(volatile void*)szBuffer, sizeof(szBuffer));

    return bSuccess;
}

static bool __stdcall AppMutexAcquire(HANDLE* lphMutex)
{
    char szMutexName[MAX_PATH];

    // care about secondary instances?
    if(ConfigGetInt("SecondInstance"))
    {
        return true;
    }

    // fetch our name (block double execution of this very same exe only)
    GetModuleFileNameA(NULL, szMutexName, __ARRAYSIZE(szMutexName));

    // replace backslashes with a valid character
    BvChrReplaceA(szMutexName, '\\', '!');

    if((lphMutex[0] = CreateMutex(NULL, FALSE, szMutexName))==NULL)
    {
        return false;
    }
    else if(GetLastError()==ERROR_ALREADY_EXISTS || WaitForSingleObject(lphMutex[0], 0)!=WAIT_OBJECT_0)
    {
        CloseHandle(lphMutex[0]);
        return false;
    }

    return true;
}

static void __stdcall AppMutexRelease(HANDLE* lphMutex)
{
    if(lphMutex[0])
    {
        CloseHandle(lphMutex[0]);
        lphMutex[0] = NULL;
    }
}

static bool __stdcall CreateCustomButton(const char* lpszSection, void* lpContext)
{
    const char* const lpszName = lpszSection+15;  // skip "ROCred.Buttons."
    HWND hWnd = (HWND)lpContext;

    if(ButtonCheckName(lpszName))
    {
        char szBufferDisplayName[4096], szBufferActionData[4096];  // STRINGTABLE limit, which is probably more than you will ever need
        const char* lpszDisplayName;
        const char* lpszActionData;
        const char* lpszActionHandler;
        int nActionType;
        RECT rcBtn;

        rcBtn.left   = ConfigGetIntFromSection(lpszSection, "X");
        rcBtn.top    = ConfigGetIntFromSection(lpszSection, "Y");
        rcBtn.right  = ConfigGetIntFromSection(lpszSection, "W");
        rcBtn.bottom = ConfigGetIntFromSection(lpszSection, "H");
        nActionType  = ConfigGetIntFromSection(lpszSection, "ActionType");
        lpszDisplayName   = ConfigGetStrFromSection(lpszSection, "DisplayName");
        lpszActionData    = ConfigGetStrFromSection(lpszSection, "ActionData");
        lpszActionHandler = ConfigGetStrFromSection(lpszSection, "ActionHandler");

        if(lpszDisplayName[0]=='#')
        {
            LoadStringA(GetWindowInstance(hWnd), BvStrToULongA(lpszDisplayName+1, NULL, 10), szBufferDisplayName, __ARRAYSIZE(szBufferDisplayName));
            lpszDisplayName = szBufferDisplayName;
        }

        if(lpszActionData[0]=='#')
        {
            LoadStringA(GetWindowInstance(hWnd), BvStrToULongA(lpszActionData+1, NULL, 10), szBufferActionData, __ARRAYSIZE(szBufferActionData));
            lpszActionData = szBufferActionData;
        }

        MapDialogRect(hWnd, &rcBtn);

        if(ButtonCreate(hWnd, rcBtn.left, rcBtn.top, rcBtn.right /* width */, rcBtn.bottom /* height */, lpszDisplayName, lpszName, nActionType, lpszActionData, lpszActionHandler))
        {
            SetWindowFont(GetDlgItem(hWnd, ButtonGetId(lpszName)), GetWindowFont(hWnd), TRUE);
        }
        else
        {
            MessageBoxA(hWnd, "Failed to create button.", lpszName, MB_ICONSTOP|MB_OK);
        }
    }
    else
    {
        MessageBoxA(hWnd, "Invalid button identifier.", lpszSection, MB_ICONSTOP|MB_OK);
    }

    return true;  // next
}

bool __stdcall StartClient(HWND hWnd, const char* const lpszExecutable, const char* const lpszParameters)
{
    bool bSuccess = false;
    char szUserName[24];
    char szPassWord[24];
    char szExePath[MAX_PATH];
    char szMiscInfo[128] = { 0 };
    char szTargetName[128];
    const char* lpszExeName;
    const char* lpszExeType;
    BOOL bCheckSave;
    HINSTANCE hInstance = GetWindowInstance(hWnd);

    GetWindowTextA(GetDlgItem(hWnd, IDC_USERNAME), szUserName, __ARRAYSIZE(szUserName));

    if(lstrlenA(szUserName)<4)
    {
        MsgBox(hWnd, szUserName[0] ? MAKEINTRESOURCE(IDS_USER_SHRT) : MAKEINTRESOURCE(IDS_USER_NONE), MB_OK|MB_ICONINFORMATION);
        return false;
    }

    GetWindowTextA(GetDlgItem(hWnd, IDC_PASSWORD), szPassWord, __ARRAYSIZE(szPassWord));

    if(lstrlenA(szPassWord)<4)
    {
        MsgBox(hWnd, szPassWord[0] ? MAKEINTRESOURCE(IDS_PASS_SHRT) : MAKEINTRESOURCE(IDS_PASS_NONE), MB_OK|MB_ICONINFORMATION);
        return false;
    }

    if(ConfigGetInt("PolicyNoSessionPassword"))
    {
        SetWindowTextA(GetDlgItem(hWnd, IDC_PASSWORD), "");
    }

    bCheckSave = (BOOL)(SendMessage(GetDlgItem(hWnd, IDC_CHECKSAVE), BM_GETCHECK, 0, 0)==BST_CHECKED);
    ConfigSetStr("CheckSave", bCheckSave ? "1" : "0");

    szTargetName[0] = 0;
    if(ConfigGetInt("CheckSavePassword"))
    {
        const char* lpszConfigID = ConfigGetStr("ConfigID");

        if(lpszConfigID[0])
        {
            snprintf(szTargetName, __ARRAYSIZE(szTargetName), "%s%s", ROCRED_TARGET_NAME, lpszConfigID);
        }
    }

    if(bCheckSave)
    {// save
        ConfigSetStr("UserName", szUserName);

        if(szTargetName[0])
        {
            WinCredSaveA(szTargetName, szUserName, szPassWord, CRED_PERSIST_LOCAL_MACHINE);
        }
    }
    else
    {// delete
        ConfigSetStr("UserName", NULL);

        if(szTargetName[0])
        {
            WinCredDeleteA(szTargetName, CRED_TYPE_GENERIC, 0);
        }
    }

    lpszExeName = lpszExecutable;
    lpszExeType = lpszParameters;
    CombineExePathName(szExePath, __ARRAYSIZE(szExePath), lpszExeName);

    // miscellaneous information for the server
    if(ConfigGetInt("MiscInfo"))
    {
        if(MiscInfoAgreePrompt(hWnd))
        {// agreed
            int nInfo = ConfigGetInt("MiscInfo");

            if(nInfo&MISCINFO_OPT_MACADDRESS)
            {
                MACADDRESS Mac;
                MacAddressGet(&Mac, MACADDR_OPT_DEFAULT_ZERO);

                wsprintfA(szMiscInfo+lstrlenA(szMiscInfo), "mac=%02x%02x%02x%02x%02x%02x&", Mac.Address[0], Mac.Address[1], Mac.Address[2], Mac.Address[3], Mac.Address[4], Mac.Address[5]);
            }

            lstrcatA(szMiscInfo, "key=");
        }
        else
        {// did not agree
            return false;
        }
    }

    if(ConfigGetInt("HashMD5"))
    {// MD5
        MD5HASH Hash;
        char szHexHash[sizeof(Hash)*2+1];

        MD5_String(szPassWord, &Hash);
        XF_BinHex(szHexHash, __ARRAYSIZE(szHexHash), Hash.ucData, sizeof(Hash.ucData));

        bSuccess = InvokeProcess(hWnd, szExePath, "-t:%s%s %s %s", szMiscInfo, szHexHash, szUserName, lpszExeType);
    }
    else
    {// Plaintext
        bSuccess = InvokeProcess(hWnd, szExePath, "-t:%s%s %s %s", szMiscInfo, szPassWord, szUserName, lpszExeType);
    }

    // get rid of the password after it has been used
    ZeroMemory((void*)(volatile void*)szPassWord, sizeof(szPassWord));

    return bSuccess;
}

static BOOL CALLBACK WndProcOnInitDialog(HWND hWnd, HWND hWndFocus, LPARAM lParam)
{
    char szBuffer[4096];
    BOOL bCheckSave, bSetFocus = TRUE;
    HINSTANCE hInstance = GetWindowInstance(hWnd);

    SetWindowLargeIcon(hWnd, LoadLargeIcon(hInstance, MAKEINTRESOURCE(IDI_MAINICON)));
    SetWindowSmallIcon(hWnd, LoadSmallIcon(hInstance, MAKEINTRESOURCE(IDI_MAINICON)));

    LoadStringA(hInstance, IDS_TITLE, szBuffer, __ARRAYSIZE(szBuffer));
    SetWindowTextA(hWnd, szBuffer);

    LoadStringA(hInstance, IDS_USERNAME, szBuffer, __ARRAYSIZE(szBuffer));
    SetWindowTextA(GetDlgItem(hWnd, IDS_USERNAME), szBuffer);

    LoadStringA(hInstance, IDS_PASSWORD, szBuffer, __ARRAYSIZE(szBuffer));
    SetWindowTextA(GetDlgItem(hWnd, IDS_PASSWORD), szBuffer);

    if(ConfigGetInt("PolicyNoCheckSave"))
    {
        ShowWindow(GetDlgItem(hWnd, IDC_CHECKSAVE), SW_HIDE);
    }
    else
    {
        LoadStringA(hInstance, IDS_CHECKSAVE, szBuffer, __ARRAYSIZE(szBuffer));
        SetWindowTextA(GetDlgItem(hWnd, IDC_CHECKSAVE), szBuffer);

        bCheckSave = ConfigGetInt("CheckSave");
        SendMessage(GetDlgItem(hWnd, IDC_CHECKSAVE), BM_SETCHECK, (WPARAM)bCheckSave, 0);

        if(bCheckSave)
        {
            const char* lpszUserName = ConfigGetStr("UserName");

            if(lpszUserName[0])
            {
                SetWindowTextA(GetDlgItem(hWnd, IDC_USERNAME), lpszUserName);
                SetDialogFocus(hWnd, GetDlgItem(hWnd, IDC_PASSWORD));  // move focus to password
                bSetFocus = FALSE;
            }

            if(ConfigGetInt("CheckSavePassword"))
            {// check credential manager
                const char* lpszConfigID = ConfigGetStr("ConfigID");

                if(lpszConfigID[0])
                {
                    char szUserName[24];
                    char szPassWord[24];
                    char szTargetName[128];

                    snprintf(szTargetName, __ARRAYSIZE(szTargetName), "%s%s", ROCRED_TARGET_NAME, lpszConfigID);

                    if(WinCredLoadA(szTargetName, szUserName, __ARRAYSIZE(szUserName), szPassWord, __ARRAYSIZE(szPassWord)))
                    {
                        SetWindowTextA(GetDlgItem(hWnd, IDC_PASSWORD), szPassWord);
                        ZeroMemory((void*)(volatile void*)szPassWord, sizeof(szPassWord));

                        SetWindowTextA(GetDlgItem(hWnd, IDC_USERNAME), szUserName);
                        SetDialogFocus(hWnd, GetDlgItem(hWnd, IDOK));
                        bSetFocus = FALSE;
                    }
                }
            }
        }
    }

    LoadStringA(hInstance, IDS_OK, szBuffer, __ARRAYSIZE(szBuffer));
    SetWindowTextA(GetDlgItem(hWnd, IDOK), szBuffer);

    LoadStringA(hInstance, IDS_CLOSE, szBuffer, __ARRAYSIZE(szBuffer));
    SetWindowTextA(GetDlgItem(hWnd, IDCANCEL), szBuffer);

    // load custom buttons if any
    ConfigForEachSectionMatch("ROCred.Buttons.", &CreateCustomButton, hWnd);

    // apply available skins
    BgSkinInit(hWnd);

    return bSetFocus;
}

static void CALLBACK WndProcOnCommand(HWND hWnd, int nId, HWND hWndCtl, UINT uCodeNotify)
{
    if(uCodeNotify==0U || uCodeNotify==1U)
    {
        switch(nId)
        {
            case IDOK:
                StartClient(hWnd, ConfigGetStr("ExeName"), ConfigGetStr("ExeType"));
                break;
            case IDCANCEL:
                EndDialog(hWnd, 0);
                break;
            default:
                ButtonAction(hWnd, nId);
                break;
        }
    }
}

static BOOL CALLBACK WndProcOnEraseBkgnd(HWND hWnd, HDC hDC)
{
    if(!BgSkinOnEraseBkGnd(hWnd, hDC))
    {
        return FORWARD_WM_ERASEBKGND(hWnd, hDC, &DefWndProc);  // default background
    }

    return TRUE;
}

static void CALLBACK WndProcOnLButtonDown(HWND hWnd, BOOL bDoubleClick, int nX, int nY, UINT uKeyFlags)
{
    if(!BgSkinOnLButtonDown(hWnd))
    {
        FORWARD_WM_LBUTTONDOWN(hWnd, bDoubleClick, nX, nY, uKeyFlags, &DefWndProc);
    }
}

static HBRUSH CALLBACK WndProcOnCtlColor(HWND hWnd, HDC hDC, HWND hWndChild, int nType)
{
    HBRUSH hbrBackground = NULL;

    switch(nType)
    {
        case CTLCOLOR_STATIC:
            hbrBackground = BgSkinOnCtlColorStatic(hDC, hWndChild);

            if(hbrBackground==NULL)
            {
                hbrBackground = FORWARD_WM_CTLCOLORSTATIC(hWnd, hDC, hWndChild, &DefWndProc);
            }

            break;
        case CTLCOLOR_EDIT:
            hbrBackground = BgSkinOnCtlColorEdit(hDC, hWndChild);

            if(hbrBackground==NULL)
            {
                hbrBackground = FORWARD_WM_CTLCOLOREDIT(hWnd, hDC, hWndChild, &DefWndProc);
            }

            break;
    }

    return hbrBackground;
}

static void CALLBACK WndProcOnDrawItem(HWND hWnd, const DRAWITEMSTRUCT* lpDrawItem)
{
    if(!BgSkinOnDrawItem(lpDrawItem->CtlID, lpDrawItem))
    {
        FORWARD_WM_DRAWITEM(hWnd, lpDrawItem, &DefWndProc);
    }
}

static void CALLBACK WndProcOnHelp(HWND hWnd, LPHELPINFO lphi)
{
    DLGABOUTINFO Dai =
    {
        hWnd,
        MAKELONG(2012,2018),
        "About ROCred...",
        "ROCred",
        APP_VERSION,
        "Ai4rei/AN",
        "\r\nThis software is FREEWARE and is provided AS IS, without warranty of ANY KIND, either expressed or implied, including but not limited to the implied warranties of merchantability and/or fitness for a particular purpose. If your country's law does not allow complete exclusion of liability, you may not use this software. The author SHALL NOT be held liable for ANY damage to you, your hardware, your software, your pets, your dear other, or to anyone or anything else, that may or may not result from the use or misuse of this software. Basically, you use it at YOUR OWN RISK.",
        "http://ai4rei.net/p/rocredweb",
    };

    DlgAbout(&Dai);
}

static void CALLBACK WndProcOnDestroy(HWND hWnd)
{
    BgSkinFree();
}

static LRESULT CALLBACK DefWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    return DefDlgProcEx(hWnd, uMsg, wParam, lParam, &l_bDefDlgEx);
}

static LRESULT CALLBACK WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch(uMsg)
    {
        HANDLE_MSG(hWnd, WM_INITDIALOG,     &WndProcOnInitDialog);
        HANDLE_MSG(hWnd, WM_COMMAND,        &WndProcOnCommand);
        HANDLE_MSG(hWnd, WM_ERASEBKGND,     &WndProcOnEraseBkgnd);
        HANDLE_MSG(hWnd, WM_LBUTTONDOWN,    &WndProcOnLButtonDown);
        HANDLE_MSG(hWnd, WM_CTLCOLORSTATIC, &WndProcOnCtlColor);
        HANDLE_MSG(hWnd, WM_CTLCOLOREDIT,   &WndProcOnCtlColor);
        HANDLE_MSG(hWnd, WM_DRAWITEM,       &WndProcOnDrawItem);
        HANDLE_MSG(hWnd, WM_HELP,           &WndProcOnHelp);
        HANDLE_MSG(hWnd, WM_DESTROY,        &WndProcOnDestroy);
    }

    return DefWndProc(hWnd, uMsg, wParam, lParam);
}

static BOOL CALLBACK DlgProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    CheckDefDlgRecursion(&l_bDefDlgEx);

    return SetDlgMsgResult(hWnd, uMsg, WndProc(hWnd, uMsg, wParam, lParam));
}

static MEM_OUTOFMEMORY_ACTION __WDECL OnOOM(LPCMEMOUTOFMEMORYINFO const lpInfo, LPCMEMSTATISTICS const lpStats, void* lpContext)
{// consider the following: we have no memory
    char szMessage[128];

    snprintf(szMessage, __ARRAYSIZE(szMessage), "Failed to allocate %u+%u bytes of memory.", lpInfo->uWantAvail, lpInfo->uWantBytes-lpInfo->uWantAvail);

    if(MsgBox(NULL, szMessage, MB_RETRYCANCEL|MB_ICONSTOP|MB_SYSTEMMODAL)==IDRETRY)
    {
        return MEM_OOM_RETRY;
    }

    return MEM_OOM_DEFAULT;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpszCmdLine, int nShowCmd)
{
    DLGTEMPLATEINFO DlgTi;
    HANDLE hMutex = NULL;

    // memory
    MemSetOptions(MEM_OPT_EXCEPTIO|MEM_OPT_STOPBAAD|MEM_OPT_STOPNULL|MEM_OPT_STOPZERO);
    MemSetHandler(&OnOOM, NULL);

    // start up
    if(!FAILED(CoInitialize(NULL)))
    {
        if(ConfigInit())
        {
            if(AppMutexAcquire(&hMutex))
            {
#ifdef WITH_RESOURCE_EMBEDDING
                if(lpszCmdLine[0]=='/')
                {
                    if(!lstrcmpiA(&lpszCmdLine[1], "embed"))
                    {
                        if(ConfigSave())
                        {
                            MsgBox(NULL, "Configuration was successfully embedded.", MB_OK|MB_ICONINFORMATION);
                        }
                        else
                        {
                            MsgBox(NULL, "Embedding configuration failed. Make sure you have a configuration set up and do this on Windows NT or later.", MB_OK|MB_ICONSTOP);
                        }
                    }
                }
                else
#endif  /* WITH_RESOURCE_EMBEDDING */
                {
                    CopyMemory(&DlgTi, &l_DlgTempl, sizeof(DlgTi));
                    DlgTi.wFontSize = ConfigGetInt("FontSize");

                    InitCommonControls();

                    DlgTemplateExBoxParam(hInstance, &DlgTi, NULL, &DlgProc, 0);
                }

                AppMutexRelease(&hMutex);
            }

            ConfigQuit();
        }
        else
        {
            MsgBox(NULL, MAKEINTRESOURCE(IDS_CONFIG_ERROR), MB_OK|MB_ICONSTOP);
        }

        CoUninitialize();
    }
    else
    {
        MsgBox(NULL, MAKEINTRESOURCE(IDS_COINIT_ERROR), MB_OK|MB_ICONSTOP);
    }

    // memory again
    AssertHere(!MemIsLeaked());

    return EXIT_SUCCESS;
}
