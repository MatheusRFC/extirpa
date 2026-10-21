// SPDX-License-Identifier: MIT
// Declarações da API do Windows usadas pelo EXTIRPA, escritas à mão: o projeto compila
// só com o compilador (clang) e o linker (lld-link), sem SDK do Windows nem MinGW.
//
// Cada tipo, constante e estrutura daqui é conferida contra os cabeçalhos oficiais do
// mingw-w64 por tools/check_win32.py (tamanhos, deslocamentos dos campos, valores das
// constantes e assinaturas das funções). Ao acrescentar algo, rode a conferência.
//
// Com W32_CHECK_FUNCS_ONLY definido, só as funções são declaradas: é assim que a
// conferência compara as assinaturas com as do mingw-w64.
#ifndef EXTIRPA_WIN32_H
#define EXTIRPA_WIN32_H

#include <stdarg.h>

#ifdef EXTIRPA_FAKE_WIN
// testes no Linux: as funções vêm de tests/fakewin.c (Registro e disco simulados)
#define WINAPI
#define CALLBACK
#define W32IMP
#else
#define WINAPI __stdcall
#define CALLBACK __stdcall
#define W32IMP __declspec(dllimport)
#endif

#ifndef W32_CHECK_FUNCS_ONLY

#include "../core/base.h"

// ---------------------------------------------------------------- tipos básicos
typedef unsigned char BYTE, *LPBYTE;
typedef unsigned short WORD, LANGID;
typedef unsigned long DWORD, *LPDWORD, *PDWORD;
typedef long LONG, LSTATUS, HRESULT;
typedef int BOOL, INT;
typedef unsigned int UINT, *PUINT;
typedef short SHORT;
typedef long long LONGLONG, INT64;
typedef unsigned long long ULONGLONG;
typedef long long LONG_PTR, INT_PTR;
typedef unsigned long long ULONG_PTR, UINT_PTR, SIZE_T, DWORD_PTR, *PULONG_PTR, *PDWORD_PTR;
typedef UINT_PTR WPARAM;
typedef LONG_PTR LPARAM, LRESULT;
typedef DWORD COLORREF, ACCESS_MASK, REGSAM;
typedef WORD ATOM, FILEOP_FLAGS;
typedef void VOID, *PVOID, *LPVOID;
typedef const void *LPCVOID;
typedef char CHAR;
typedef const CHAR *LPCSTR;
typedef WCHAR *LPWSTR, *PWSTR;
typedef const WCHAR *LPCWSTR, *PCWSTR, *PCZZWSTR;

typedef void *HANDLE, **PHANDLE, *HWND, *HINSTANCE, *HMODULE, *HICON, *HCURSOR, *HBRUSH, *HFONT, *HMENU, *HDC, *HGDIOBJ, *HKEY, **PHKEY, *HMONITOR,
    *HPEN, *HBITMAP, *SC_HANDLE, *HLOCAL;
struct _IMAGELIST;
typedef struct _IMAGELIST *HIMAGELIST;
struct _ITEMIDLIST;
typedef struct _ITEMIDLIST ITEMIDLIST;
typedef ITEMIDLIST *PIDLIST_ABSOLUTE;
typedef const ITEMIDLIST *PCIDLIST_ABSOLUTE;
struct _OVERLAPPED;
typedef struct _OVERLAPPED OVERLAPPED, *LPOVERLAPPED;

typedef LRESULT(CALLBACK *WNDPROC)(HWND, UINT, WPARAM, LPARAM);
typedef DWORD(WINAPI *LPTHREAD_START_ROUTINE)(LPVOID);
typedef int(CALLBACK *BFFCALLBACK)(HWND, UINT, LPARAM, LPARAM);
typedef UINT_PTR(CALLBACK *LPOFNHOOKPROC)(HWND, UINT, WPARAM, LPARAM);
typedef void(CALLBACK *TIMERPROC)(HWND, UINT, UINT_PTR, DWORD);
typedef INT_PTR(WINAPI *FARPROC)();

// ---------------------------------------------------------------- estruturas
typedef struct tagRECT {
    LONG left, top, right, bottom;
} RECT, *LPRECT;

typedef struct tagPOINT {
    LONG x, y;
} POINT, *LPPOINT;

typedef struct tagMSG {
    HWND hwnd;
    UINT message;
    WPARAM wParam;
    LPARAM lParam;
    DWORD time;
    POINT pt;
} MSG, *LPMSG;

typedef struct tagWNDCLASSEXW {
    UINT cbSize;
    UINT style;
    WNDPROC lpfnWndProc;
    int cbClsExtra;
    int cbWndExtra;
    HINSTANCE hInstance;
    HICON hIcon;
    HCURSOR hCursor;
    HBRUSH hbrBackground;
    LPCWSTR lpszMenuName;
    LPCWSTR lpszClassName;
    HICON hIconSm;
} WNDCLASSEXW;

typedef struct tagCREATESTRUCTW {
    LPVOID lpCreateParams;
    HINSTANCE hInstance;
    HMENU hMenu;
    HWND hwndParent;
    int cy, cx, y, x;
    LONG style;
    LPCWSTR lpszName;
    LPCWSTR lpszClass;
    DWORD dwExStyle;
} CREATESTRUCTW;

typedef struct tagLOGFONTW {
    LONG lfHeight, lfWidth, lfEscapement, lfOrientation, lfWeight;
    BYTE lfItalic, lfUnderline, lfStrikeOut, lfCharSet, lfOutPrecision, lfClipPrecision, lfQuality, lfPitchAndFamily;
    WCHAR lfFaceName[32];
} LOGFONTW;

typedef struct tagNONCLIENTMETRICSW {
    UINT cbSize;
    int iBorderWidth, iScrollWidth, iScrollHeight, iCaptionWidth, iCaptionHeight;
    LOGFONTW lfCaptionFont;
    int iSmCaptionWidth, iSmCaptionHeight;
    LOGFONTW lfSmCaptionFont;
    int iMenuWidth, iMenuHeight;
    LOGFONTW lfMenuFont, lfStatusFont, lfMessageFont;
    int iPaddedBorderWidth;
} NONCLIENTMETRICSW;

typedef struct tagPAINTSTRUCT {
    HDC hdc;
    BOOL fErase;
    RECT rcPaint;
    BOOL fRestore;
    BOOL fIncUpdate;
    BYTE rgbReserved[32];
} PAINTSTRUCT, *LPPAINTSTRUCT;

typedef struct tagMINMAXINFO {
    POINT ptReserved, ptMaxSize, ptMaxPosition, ptMinTrackSize, ptMaxTrackSize;
} MINMAXINFO;

typedef struct tagMONITORINFO {
    DWORD cbSize;
    RECT rcMonitor;
    RECT rcWork;
    DWORD dwFlags;
} MONITORINFO, *LPMONITORINFO;

typedef struct tagNMHDR {
    HWND hwndFrom;
    UINT_PTR idFrom;
    UINT code;
} NMHDR;

typedef struct tagLVITEMW {
    UINT mask;
    int iItem;
    int iSubItem;
    UINT state;
    UINT stateMask;
    LPWSTR pszText;
    int cchTextMax;
    int iImage;
    LPARAM lParam;
    int iIndent;
    int iGroupId;
    UINT cColumns;
    PUINT puColumns;
    int *piColFmt;
    int iGroup;
} LVITEMW;

typedef struct tagLVCOLUMNW {
    UINT mask;
    int fmt;
    int cx;
    LPWSTR pszText;
    int cchTextMax;
    int iSubItem;
    int iImage;
    int iOrder;
    int cxMin;
    int cxDefault;
    int cxIdeal;
} LVCOLUMNW;

typedef struct tagNMLISTVIEW {
    NMHDR hdr;
    int iItem;
    int iSubItem;
    UINT uNewState;
    UINT uOldState;
    UINT uChanged;
    POINT ptAction;
    LPARAM lParam;
} NMLISTVIEW;

typedef struct tagNMITEMACTIVATE {
    NMHDR hdr;
    int iItem;
    int iSubItem;
    UINT uNewState;
    UINT uOldState;
    UINT uChanged;
    POINT ptAction;
    LPARAM lParam;
    UINT uKeyFlags;
} NMITEMACTIVATE;

typedef struct tagLVDISPINFOW {
    NMHDR hdr;
    LVITEMW item;
} NMLVDISPINFOW;

#pragma pack(push, 1)
typedef struct tagLVKEYDOWN {
    NMHDR hdr;
    WORD wVKey;
    UINT flags;
} NMLVKEYDOWN;
#pragma pack(pop)

typedef struct tagLVHITTESTINFO {
    POINT pt;
    UINT flags;
    int iItem;
    int iSubItem;
    int iGroup;
} LVHITTESTINFO;

typedef struct _HD_ITEMW {
    UINT mask;
    int cxy;
    LPWSTR pszText;
    HBITMAP hbm;
    int cchTextMax;
    int fmt;
    LPARAM lParam;
    int iImage;
    int iOrder;
    UINT type;
    void *pvFilter;
    UINT state;
} HDITEMW;

typedef struct tagTCITEMW {
    UINT mask;
    DWORD dwState;
    DWORD dwStateMask;
    LPWSTR pszText;
    int cchTextMax;
    int iImage;
    LPARAM lParam;
} TCITEMW;

typedef struct tagINITCOMMONCONTROLSEX {
    DWORD dwSize;
    DWORD dwICC;
} INITCOMMONCONTROLSEX;

typedef struct _SHFILEOPSTRUCTW {
    HWND hwnd;
    UINT wFunc;
    LPCWSTR pFrom;
    LPCWSTR pTo;
    FILEOP_FLAGS fFlags;
    BOOL fAnyOperationsAborted;
    LPVOID hNameMappings;
    PCWSTR lpszProgressTitle;
} SHFILEOPSTRUCTW, *LPSHFILEOPSTRUCTW;

typedef struct _browseinfoW {
    HWND hwndOwner;
    PCIDLIST_ABSOLUTE pidlRoot;
    LPWSTR pszDisplayName;
    LPCWSTR lpszTitle;
    UINT ulFlags;
    BFFCALLBACK lpfn;
    LPARAM lParam;
    int iImage;
} BROWSEINFOW, *LPBROWSEINFOW;

typedef struct tagOFNW {
    DWORD lStructSize;
    HWND hwndOwner;
    HINSTANCE hInstance;
    LPCWSTR lpstrFilter;
    LPWSTR lpstrCustomFilter;
    DWORD nMaxCustFilter;
    DWORD nFilterIndex;
    LPWSTR lpstrFile;
    DWORD nMaxFile;
    LPWSTR lpstrFileTitle;
    DWORD nMaxFileTitle;
    LPCWSTR lpstrInitialDir;
    LPCWSTR lpstrTitle;
    DWORD Flags;
    WORD nFileOffset;
    WORD nFileExtension;
    LPCWSTR lpstrDefExt;
    LPARAM lCustData;
    LPOFNHOOKPROC lpfnHook;
    LPCWSTR lpTemplateName;
    void *pvReserved;
    DWORD dwReserved;
    DWORD FlagsEx;
} OPENFILENAMEW, *LPOPENFILENAMEW;

typedef struct _SECURITY_ATTRIBUTES {
    DWORD nLength;
    LPVOID lpSecurityDescriptor;
    BOOL bInheritHandle;
} SECURITY_ATTRIBUTES, *LPSECURITY_ATTRIBUTES;

typedef struct _STARTUPINFOW {
    DWORD cb;
    LPWSTR lpReserved;
    LPWSTR lpDesktop;
    LPWSTR lpTitle;
    DWORD dwX, dwY, dwXSize, dwYSize, dwXCountChars, dwYCountChars, dwFillAttribute, dwFlags;
    WORD wShowWindow;
    WORD cbReserved2;
    LPBYTE lpReserved2;
    HANDLE hStdInput, hStdOutput, hStdError;
} STARTUPINFOW, *LPSTARTUPINFOW;

typedef struct _PROCESS_INFORMATION {
    HANDLE hProcess, hThread;
    DWORD dwProcessId, dwThreadId;
} PROCESS_INFORMATION, *LPPROCESS_INFORMATION;

typedef struct _JOBOBJECT_ASSOCIATE_COMPLETION_PORT {
    PVOID CompletionKey;
    HANDLE CompletionPort;
} JOBOBJECT_ASSOCIATE_COMPLETION_PORT;

typedef struct _FILETIME {
    DWORD dwLowDateTime, dwHighDateTime;
} FILETIME, *PFILETIME, *LPFILETIME;

typedef struct _SYSTEMTIME {
    WORD wYear, wMonth, wDayOfWeek, wDay, wHour, wMinute, wSecond, wMilliseconds;
} SYSTEMTIME, *LPSYSTEMTIME;

typedef struct _WIN32_FIND_DATAW {
    DWORD dwFileAttributes;
    FILETIME ftCreationTime, ftLastAccessTime, ftLastWriteTime;
    DWORD nFileSizeHigh, nFileSizeLow, dwReserved0, dwReserved1;
    WCHAR cFileName[260];
    WCHAR cAlternateFileName[14];
} WIN32_FIND_DATAW, *LPWIN32_FIND_DATAW;

typedef struct _WIN32_FILE_ATTRIBUTE_DATA {
    DWORD dwFileAttributes;
    FILETIME ftCreationTime, ftLastAccessTime, ftLastWriteTime;
    DWORD nFileSizeHigh, nFileSizeLow;
} WIN32_FILE_ATTRIBUTE_DATA;

typedef struct _SERVICE_STATUS {
    DWORD dwServiceType, dwCurrentState, dwControlsAccepted, dwWin32ExitCode, dwServiceSpecificExitCode, dwCheckPoint, dwWaitHint;
} SERVICE_STATUS, *LPSERVICE_STATUS;

typedef struct _GUID {
    DWORD Data1;
    WORD Data2;
    WORD Data3;
    BYTE Data4[8];
} GUID, KNOWNFOLDERID;
typedef const GUID *REFKNOWNFOLDERID;

typedef struct tagBITMAPINFOHEADER {
    DWORD biSize;
    LONG biWidth;
    LONG biHeight;
    WORD biPlanes;
    WORD biBitCount;
    DWORD biCompression;
    DWORD biSizeImage;
    LONG biXPelsPerMeter;
    LONG biYPelsPerMeter;
    DWORD biClrUsed;
    DWORD biClrImportant;
} BITMAPINFOHEADER;

typedef struct tagRGBQUAD {
    BYTE rgbBlue, rgbGreen, rgbRed, rgbReserved;
} RGBQUAD;

typedef struct tagBITMAPINFO {
    BITMAPINFOHEADER bmiHeader;
    RGBQUAD bmiColors[1];
} BITMAPINFO;

typedef struct _RTL_SRWLOCK {
    PVOID Ptr;
} SRWLOCK, *PSRWLOCK;

// Ponto de restauração (srrestoreptapi.h: estas duas estruturas são empacotadas em 1 byte)
#pragma pack(push, 1)
typedef struct _RESTOREPTINFOW {
    DWORD dwEventType;
    DWORD dwRestorePtType;
    INT64 llSequenceNumber;
    WCHAR szDescription[256];
} RESTOREPOINTINFOW, *PRESTOREPOINTINFOW;

typedef struct _SMGRSTATUS {
    DWORD nStatus;
    INT64 llSequenceNumber;
} STATEMGRSTATUS, *PSTATEMGRSTATUS;
#pragma pack(pop)

typedef enum _GET_FILEEX_INFO_LEVELS { GetFileExInfoStandard, GetFileExMaxInfoLevel } GET_FILEEX_INFO_LEVELS;
typedef enum _FINDEX_INFO_LEVELS { FindExInfoStandard, FindExInfoBasic, FindExInfoMaxInfoLevel } FINDEX_INFO_LEVELS;
typedef enum _FINDEX_SEARCH_OPS { FindExSearchNameMatch, FindExSearchLimitToDirectories, FindExSearchLimitToDevices, FindExSearchMaxSearchOp } FINDEX_SEARCH_OPS;
typedef enum _JOBOBJECTINFOCLASS { JobObjectAssociateCompletionPortInformation = 7 } JOBOBJECTINFOCLASS;

// ---------------------------------------------------------------- constantes
#define TRUE 1
#define FALSE 0
#define MAX_PATH 260
#define INFINITE 0xFFFFFFFF
#define INVALID_HANDLE_VALUE ((HANDLE)(LONG_PTR)-1)
#define INVALID_FILE_ATTRIBUTES ((DWORD)-1)
#define MAKEINTRESOURCEW(i) ((LPWSTR)((ULONG_PTR)((WORD)(i))))
#define LOWORD(l) ((WORD)(((DWORD_PTR)(l)) & 0xffff))
#define HIWORD(l) ((WORD)((((DWORD_PTR)(l)) >> 16) & 0xffff))
#define GET_X_LPARAM(lp) ((int)(short)LOWORD(lp))
#define GET_Y_LPARAM(lp) ((int)(short)HIWORD(lp))
#define RGB(r, g, b) ((COLORREF)(((BYTE)(r) | ((WORD)((BYTE)(g)) << 8)) | (((DWORD)(BYTE)(b)) << 16)))

// erros
#define ERROR_SUCCESS 0
#define ERROR_FILE_NOT_FOUND 2
#define ERROR_PATH_NOT_FOUND 3
#define ERROR_ACCESS_DENIED 5
#define ERROR_INVALID_HANDLE 6
#define ERROR_NO_MORE_FILES 18
#define ERROR_SHARING_VIOLATION 32
#define ERROR_MORE_DATA 234
#define ERROR_NO_MORE_ITEMS 259
#define ERROR_ALREADY_EXISTS 183
#define ERROR_DIR_NOT_EMPTY 145
#define ERROR_SERVICE_DOES_NOT_EXIST 1060
#define ERROR_SERVICE_NOT_ACTIVE 1062
#define ERROR_SERVICE_MARKED_FOR_DELETE 1072
#define ERROR_SERVICE_DISABLED 1058
#define ERROR_CANCELLED 1223
#define WAIT_OBJECT_0 0
#define WAIT_TIMEOUT 258

// memória
#define HEAP_ZERO_MEMORY 0x00000008

// arquivos
#define GENERIC_READ 0x80000000
#define GENERIC_WRITE 0x40000000
#define FILE_SHARE_READ 0x00000001
#define FILE_SHARE_WRITE 0x00000002
#define FILE_SHARE_DELETE 0x00000004
#define CREATE_ALWAYS 2
#define OPEN_EXISTING 3
#define OPEN_ALWAYS 4
#define FILE_ATTRIBUTE_READONLY 0x00000001
#define FILE_ATTRIBUTE_HIDDEN 0x00000002
#define FILE_ATTRIBUTE_SYSTEM 0x00000004
#define FILE_ATTRIBUTE_DIRECTORY 0x00000010
#define FILE_ATTRIBUTE_NORMAL 0x00000080
#define FILE_ATTRIBUTE_REPARSE_POINT 0x00000400
#define FILE_APPEND_DATA 0x0004
#define FIND_FIRST_EX_LARGE_FETCH 0x00000002
#define MOVEFILE_DELAY_UNTIL_REBOOT 0x00000004
#define LOAD_LIBRARY_SEARCH_SYSTEM32 0x00000800

// processos e jobs
#define CREATE_SUSPENDED 0x00000004
#define CREATE_NO_WINDOW 0x08000000
#define CREATE_UNICODE_ENVIRONMENT 0x00000400
#define STARTF_USESHOWWINDOW 0x00000001
#define STARTF_USESTDHANDLES 0x00000100
#define HANDLE_FLAG_INHERIT 0x00000001
#define PROCESS_QUERY_LIMITED_INFORMATION 0x1000
#define PROCESS_TERMINATE 0x0001
#define SYNCHRONIZE 0x00100000
#define JOB_OBJECT_MSG_ACTIVE_PROCESS_ZERO 4
#define STILL_ACTIVE 259

// Registro
#define HKEY_CLASSES_ROOT ((HKEY)(ULONG_PTR)((LONG)0x80000000))
#define HKEY_CURRENT_USER ((HKEY)(ULONG_PTR)((LONG)0x80000001))
#define HKEY_LOCAL_MACHINE ((HKEY)(ULONG_PTR)((LONG)0x80000002))
#define HKEY_USERS ((HKEY)(ULONG_PTR)((LONG)0x80000003))
#define KEY_QUERY_VALUE 0x0001
#define KEY_SET_VALUE 0x0002
#define KEY_ENUMERATE_SUB_KEYS 0x0008
#define KEY_READ 0x20019
#define KEY_WRITE 0x20006
#define KEY_ALL_ACCESS 0xF003F
#define KEY_WOW64_64KEY 0x0100
#define KEY_WOW64_32KEY 0x0200
#define DELETE 0x00010000
#define REG_NONE 0
#define REG_SZ 1
#define REG_EXPAND_SZ 2
#define REG_BINARY 3
#define REG_DWORD 4
#define REG_MULTI_SZ 7
#define REG_QWORD 11
#define REG_OPTION_NON_VOLATILE 0x00000000

// serviços
#define SC_MANAGER_CONNECT 0x0001
#define SC_MANAGER_ENUMERATE_SERVICE 0x0004
#define SERVICE_QUERY_STATUS 0x0004
#define SERVICE_STOP 0x0020
#define SERVICE_CONTROL_STOP 0x00000001
#define SERVICE_STOPPED 0x00000001
#define SERVICE_STOP_PENDING 0x00000003

// janelas
#define CW_USEDEFAULT ((int)0x80000000)
#define WS_OVERLAPPED 0x00000000
#define WS_POPUP 0x80000000
#define WS_CHILD 0x40000000
#define WS_VISIBLE 0x10000000
#define WS_DISABLED 0x08000000
#define WS_CLIPSIBLINGS 0x04000000
#define WS_CLIPCHILDREN 0x02000000
#define WS_CAPTION 0x00C00000
#define WS_BORDER 0x00800000
#define WS_VSCROLL 0x00200000
#define WS_SYSMENU 0x00080000
#define WS_THICKFRAME 0x00040000
#define WS_GROUP 0x00020000
#define WS_TABSTOP 0x00010000
#define WS_MINIMIZEBOX 0x00020000
#define WS_MAXIMIZEBOX 0x00010000
#define WS_OVERLAPPEDWINDOW 0x00CF0000
#define WS_EX_TOPMOST 0x00000008
#define WS_EX_TOOLWINDOW 0x00000080
#define WS_EX_CLIENTEDGE 0x00000200
#define WS_EX_CONTROLPARENT 0x00010000
#define WS_EX_DLGMODALFRAME 0x00000001
#define WS_EX_LAYERED 0x00080000
#define WS_EX_NOACTIVATE 0x08000000
#define CS_VREDRAW 0x0001
#define CS_HREDRAW 0x0002
#define CS_DBLCLKS 0x0008
#define GWLP_USERDATA (-21)
#define GA_ROOT 2
#define SW_HIDE 0
#define SW_SHOWNORMAL 1
#define SW_SHOW 5
#define SW_RESTORE 9
#define SW_SHOWDEFAULT 10
#define SWP_NOSIZE 0x0001
#define SWP_NOMOVE 0x0002
#define SWP_NOZORDER 0x0004
#define SWP_NOACTIVATE 0x0010
#define SWP_SHOWWINDOW 0x0040
#define HWND_TOPMOST ((HWND)-1)
#define HWND_BROADCAST ((HWND)0xffff)
#define SMTO_ABORTIFHUNG 0x0002
#define LWA_ALPHA 0x00000002
#define MONITOR_DEFAULTTONEAREST 0x00000002
#define SM_CXSCREEN 0
#define SM_CYSCREEN 1
#define SM_CXSMICON 49
#define SM_CYSMICON 50
#define SPI_GETNONCLIENTMETRICS 0x0029
#define USER_DEFAULT_SCREEN_DPI 96
#define COLOR_WINDOW 5
#define COLOR_WINDOWTEXT 8
#define COLOR_BTNFACE 15
#define COLOR_GRAYTEXT 17
#define COLOR_HIGHLIGHT 13
#define IDC_ARROW MAKEINTRESOURCEW(32512)
#define IDC_CROSS MAKEINTRESOURCEW(32515)
#define IDC_WAIT MAKEINTRESOURCEW(32514)
#define IDI_APPLICATION MAKEINTRESOURCEW(32512)
#define IMAGE_ICON 1
#define LR_DEFAULTCOLOR 0x00000000
#define LR_SHARED 0x00008000
#define DI_NORMAL 0x0003
#define VK_RETURN 0x0D
#define VK_ESCAPE 0x1B
#define VK_DELETE 0x2E
#define VK_F5 0x74
#define VK_CONTROL 0x11
#define VK_APPS 0x5D

// mensagens
#define WM_CREATE 0x0001
#define WM_DESTROY 0x0002
#define WM_SIZE 0x0005
#define WM_ACTIVATE 0x0006
#define WM_SETFOCUS 0x0007
#define WM_ENABLE 0x000A
#define WM_SETTEXT 0x000C
#define WM_PAINT 0x000F
#define WM_CLOSE 0x0010
#define WM_ERASEBKGND 0x0014
#define WM_SETCURSOR 0x0020
#define WM_GETMINMAXINFO 0x0024
#define WM_SETFONT 0x0030
#define WM_NOTIFY 0x004E
#define WM_CONTEXTMENU 0x007B
#define WM_SETICON 0x0080
#define WM_NCCREATE 0x0081
#define WM_NCDESTROY 0x0082
#define WM_GETFONT 0x0031
#define WM_SYSCOLORCHANGE 0x0015
#define WM_SETTINGCHANGE 0x001A
#define WM_MOUSEWHEEL 0x020A
#define WM_SHOWWINDOW 0x0018
#define WM_ACTIVATEAPP 0x001C
#define WM_KEYDOWN 0x0100
#define WM_COMMAND 0x0111
#define WM_TIMER 0x0113
#define WM_CTLCOLORSTATIC 0x0138
#define WM_MOUSEMOVE 0x0200
#define WM_LBUTTONDOWN 0x0201
#define WM_LBUTTONUP 0x0202
#define WM_RBUTTONUP 0x0205
#define WM_CAPTURECHANGED 0x0215
#define WM_DPICHANGED 0x02E0
#define WM_USER 0x0400
#define WM_APP 0x8000
#define ICON_SMALL 0
#define ICON_BIG 1

// controles padrão
#define BS_PUSHBUTTON 0x00000000
#define BS_DEFPUSHBUTTON 0x00000001
#define BS_AUTOCHECKBOX 0x00000003
#define BS_AUTORADIOBUTTON 0x00000009
#define BS_GROUPBOX 0x00000007
#define BM_GETCHECK 0x00F0
#define BM_SETCHECK 0x00F1
#define BST_UNCHECKED 0x0000
#define BST_CHECKED 0x0001
#define BN_CLICKED 0
#define ES_LEFT 0x0000
#define ES_AUTOHSCROLL 0x0080
#define ES_MULTILINE 0x0004
#define ES_READONLY 0x0800
#define ES_AUTOVSCROLL 0x0040
#define EN_CHANGE 0x0300
#define EM_SETSEL 0x00B1
#define SS_LEFT 0x00000000
#define SS_NOPREFIX 0x00000080
#define SS_ENDELLIPSIS 0x00004000
#define SS_PATHELLIPSIS 0x00008000

// menus
#define MF_STRING 0x00000000
#define MF_GRAYED 0x00000001
#define MF_SEPARATOR 0x00000800
#define MF_DEFAULT 0x00001000
#define TPM_RIGHTBUTTON 0x0002
#define TPM_RETURNCMD 0x0100
#define TPM_NONOTIFY 0x0080

// caixas de mensagem
#define MB_OK 0x00000000
#define MB_OKCANCEL 0x00000001
#define MB_YESNOCANCEL 0x00000003
#define MB_YESNO 0x00000004
#define MB_ICONERROR 0x00000010
#define MB_ICONQUESTION 0x00000020
#define MB_ICONWARNING 0x00000030
#define MB_ICONINFORMATION 0x00000040
#define MB_DEFBUTTON2 0x00000100
#define IDOK 1
#define IDCANCEL 2
#define IDYES 6
#define IDNO 7

// GDI
#define TRANSPARENT 1
#define OPAQUE 2
#define PS_SOLID 0
#define NULL_BRUSH 5
#define DEFAULT_GUI_FONT 17
#define DT_LEFT 0x00000000
#define DT_CENTER 0x00000001
#define DT_VCENTER 0x00000004
#define DT_SINGLELINE 0x00000020
#define DT_NOPREFIX 0x00000800
#define DT_END_ELLIPSIS 0x00008000
#define DT_WORDBREAK 0x00000010
#define DT_CALCRECT 0x00000400
#define FW_BOLD 700
#define BI_RGB 0
#define DIB_RGB_COLORS 0
#define PW_CLIENTONLY 0x00000001
#define PW_RENDERFULLCONTENT 0x00000002

// controles comuns
#define ICC_LISTVIEW_CLASSES 0x00000001
#define ICC_BAR_CLASSES 0x00000004
#define ICC_TAB_CLASSES 0x00000008
#define ICC_PROGRESS_CLASS 0x00000020
#define ICC_STANDARD_CLASSES 0x00004000
#define ILC_MASK 0x00000001
#define ILC_COLOR32 0x00000020
#define LVS_REPORT 0x0001
#define LVS_SINGLESEL 0x0004
#define LVS_SHOWSELALWAYS 0x0008
#define LVS_SHAREIMAGELISTS 0x0040
#define LVS_OWNERDATA 0x1000
#define LVS_EX_CHECKBOXES 0x00000004
#define LVS_EX_FULLROWSELECT 0x00000020
#define LVS_EX_INFOTIP 0x00000400
#define LVS_EX_LABELTIP 0x00004000
#define LVS_EX_DOUBLEBUFFER 0x00010000
#define LVM_FIRST 0x1000
#define LVM_SETIMAGELIST (LVM_FIRST + 3)
#define LVM_GETITEMCOUNT (LVM_FIRST + 4)
#define LVM_DELETEITEM (LVM_FIRST + 8)
#define LVM_DELETEALLITEMS (LVM_FIRST + 9)
#define LVM_GETNEXTITEM (LVM_FIRST + 12)
#define LVM_HITTEST (LVM_FIRST + 18)
#define LVM_ENSUREVISIBLE (LVM_FIRST + 19)
#define LVM_REDRAWITEMS (LVM_FIRST + 21)
#define LVM_GETCOLUMNWIDTH (LVM_FIRST + 29)
#define LVM_SETCOLUMNWIDTH (LVM_FIRST + 30)
#define LVM_GETHEADER (LVM_FIRST + 31)
#define LVM_SETITEMSTATE (LVM_FIRST + 43)
#define LVM_GETITEMSTATE (LVM_FIRST + 44)
#define LVM_SETITEMCOUNT (LVM_FIRST + 47)
#define LVM_GETSELECTEDCOUNT (LVM_FIRST + 50)
#define LVM_SETEXTENDEDLISTVIEWSTYLE (LVM_FIRST + 54)
#define LVM_GETITEMW (LVM_FIRST + 75)
#define LVM_SETITEMW (LVM_FIRST + 76)
#define LVM_INSERTITEMW (LVM_FIRST + 77)
#define LVM_SETCOLUMNW (LVM_FIRST + 96)
#define LVM_INSERTCOLUMNW (LVM_FIRST + 97)
#define LVM_GETITEMTEXTW (LVM_FIRST + 115)
#define LVM_SETITEMTEXTW (LVM_FIRST + 116)
#define LVN_FIRST (0U - 100U)
#define LVN_ITEMCHANGED (LVN_FIRST - 1)
#define LVN_COLUMNCLICK (LVN_FIRST - 8)
#define LVN_KEYDOWN (LVN_FIRST - 55)
#define LVN_GETDISPINFOW (LVN_FIRST - 77)
#define NM_FIRST (0U - 0U)
#define NM_CLICK (NM_FIRST - 2)
#define NM_DBLCLK (NM_FIRST - 3)
#define NM_RETURN (NM_FIRST - 4)
#define NM_RCLICK (NM_FIRST - 5)
#define LVIF_TEXT 0x00000001
#define LVIF_IMAGE 0x00000002
#define LVIF_PARAM 0x00000004
#define LVIF_STATE 0x00000008
#define LVIS_FOCUSED 0x0001
#define LVIS_SELECTED 0x0002
#define LVIS_STATEIMAGEMASK 0xF000
#define LVNI_ALL 0x0000
#define LVNI_FOCUSED 0x0001
#define LVNI_SELECTED 0x0002
#define LVCF_FMT 0x0001
#define LVCF_WIDTH 0x0002
#define LVCF_TEXT 0x0004
#define LVCF_SUBITEM 0x0008
#define LVCFMT_LEFT 0x0000
#define LVCFMT_RIGHT 0x0001
#define LVSIL_SMALL 1
#define LVHT_ONITEMSTATEICON 0x00000008
#define I_IMAGECALLBACK (-1)
#define HDM_FIRST 0x1200
#define HDM_GETITEMW (HDM_FIRST + 11)
#define HDM_SETITEMW (HDM_FIRST + 12)
#define HDI_FORMAT 0x0004
#define HDF_SORTUP 0x0400
#define HDF_SORTDOWN 0x0200
#define TCM_FIRST 0x1300
#define TCM_GETCURSEL (TCM_FIRST + 11)
#define TCM_SETCURSEL (TCM_FIRST + 12)
#define TCM_ADJUSTRECT (TCM_FIRST + 40)
#define TCM_INSERTITEMW (TCM_FIRST + 62)
#define TCN_FIRST (0U - 550U)
#define TCN_SELCHANGE (TCN_FIRST - 1)
#define TCIF_TEXT 0x0001
#define SB_SETPARTS (WM_USER + 4)
#define SB_SETTEXTW (WM_USER + 11)
#define SBARS_SIZEGRIP 0x0100
#define PBS_MARQUEE 0x08
#define PBM_SETPOS (WM_USER + 2)
#define PBM_SETRANGE32 (WM_USER + 6)
#define PBM_SETMARQUEE (WM_USER + 10)
#define ECM_FIRST 0x1500
#define EM_SETCUEBANNER (ECM_FIRST + 1)

// shell
#define FO_DELETE 0x0003
#define FOF_SILENT 0x0004
#define FOF_NOCONFIRMATION 0x0010
#define FOF_ALLOWUNDO 0x0040
#define FOF_NOERRORUI 0x0400
#define BIF_RETURNONLYFSDIRS 0x00000001
#define BIF_NEWDIALOGSTYLE 0x00000040
#define OFN_FILEMUSTEXIST 0x00001000
#define OFN_PATHMUSTEXIST 0x00000800
#define OFN_HIDEREADONLY 0x00000004
#define OFN_DONTADDTORECENT 0x02000000
#define KF_FLAG_DONT_VERIFY 0x00004000
#define COINIT_APARTMENTTHREADED 0x2
#define COINIT_DISABLE_OLE1DDE 0x4

// ponto de restauração
#define BEGIN_SYSTEM_CHANGE 100
#define END_SYSTEM_CHANGE 101
#define APPLICATION_UNINSTALL 1
#define CANCELLED_OPERATION 13

// idiomas e mensagens de erro
#define LANG_PORTUGUESE 0x16
#define FORMAT_MESSAGE_IGNORE_INSERTS 0x00000200
#define FORMAT_MESSAGE_FROM_SYSTEM 0x00001000

// Pastas conhecidas (knownfolders.h)
#define W32_GUID(name, l, w1, w2, b1, b2, b3, b4, b5, b6, b7, b8) static const GUID name = {l, w1, w2, {b1, b2, b3, b4, b5, b6, b7, b8}}
W32_GUID(FOLDERID_ProgramFiles, 0x905e63b6, 0xc1bf, 0x494e, 0xb2, 0x9c, 0x65, 0xb7, 0x32, 0xd3, 0xd2, 0x1a);
W32_GUID(FOLDERID_ProgramFilesX86, 0x7c5a40ef, 0xa0fb, 0x4bfc, 0x87, 0x4a, 0xc0, 0xf2, 0xe0, 0xb9, 0xfa, 0x8e);
W32_GUID(FOLDERID_ProgramFilesCommon, 0xf7f1ed05, 0x9f6d, 0x47a2, 0xaa, 0xae, 0x29, 0xd3, 0x17, 0xc6, 0xf0, 0x66);
W32_GUID(FOLDERID_ProgramFilesCommonX86, 0xde974d24, 0xd9c6, 0x4d3e, 0xbf, 0x91, 0xf4, 0x45, 0x51, 0x20, 0xb9, 0x17);
W32_GUID(FOLDERID_ProgramData, 0x62ab5d82, 0xfdc1, 0x4dc3, 0xa9, 0xdd, 0x07, 0x0d, 0x1d, 0x49, 0x5d, 0x97);
W32_GUID(FOLDERID_RoamingAppData, 0x3eb685db, 0x65f9, 0x4cf6, 0xa0, 0x3a, 0xe3, 0xef, 0x65, 0x72, 0x9f, 0x3d);
W32_GUID(FOLDERID_LocalAppData, 0xf1b32785, 0x6fba, 0x4fcf, 0x9d, 0x55, 0x7b, 0x8e, 0x7f, 0x15, 0x70, 0x91);
W32_GUID(FOLDERID_LocalAppDataLow, 0xa520a1a4, 0x1780, 0x4ff6, 0xbd, 0x18, 0x16, 0x73, 0x43, 0xc5, 0xaf, 0x16);
W32_GUID(FOLDERID_Programs, 0xa77f5d77, 0x2e2b, 0x44c3, 0xa6, 0xa2, 0xab, 0xa6, 0x01, 0x05, 0x4a, 0x51);
W32_GUID(FOLDERID_CommonPrograms, 0x0139d44e, 0x6afe, 0x49f2, 0x86, 0x90, 0x3d, 0xaf, 0xca, 0xe6, 0xff, 0xb8);
W32_GUID(FOLDERID_StartMenu, 0x625b53c3, 0xab48, 0x4ec1, 0xba, 0x1f, 0xa1, 0xef, 0x41, 0x46, 0xfc, 0x19);
W32_GUID(FOLDERID_CommonStartMenu, 0xa4115719, 0xd62e, 0x491d, 0xaa, 0x7c, 0xe7, 0x4b, 0x8b, 0xe3, 0xb0, 0x67);
W32_GUID(FOLDERID_Startup, 0xb97d20bb, 0xf46a, 0x4c97, 0xba, 0x10, 0x5e, 0x36, 0x08, 0x43, 0x08, 0x54);
W32_GUID(FOLDERID_CommonStartup, 0x82a5ea35, 0xd9cd, 0x47c5, 0x96, 0x29, 0xe1, 0x5d, 0x2f, 0x71, 0x4e, 0x6e);
W32_GUID(FOLDERID_Desktop, 0xb4bfcc3a, 0xdb2c, 0x424c, 0xb0, 0x29, 0x7f, 0xe9, 0x9a, 0x87, 0xc6, 0x41);
W32_GUID(FOLDERID_PublicDesktop, 0xc4aa340d, 0xf20f, 0x4863, 0xaf, 0xef, 0xf8, 0x7e, 0xf2, 0xe6, 0xba, 0x25);
W32_GUID(FOLDERID_Profile, 0x5e6c858f, 0x0e22, 0x4760, 0x9a, 0xfe, 0xea, 0x33, 0x17, 0xb6, 0x71, 0x73);
W32_GUID(FOLDERID_UserProfiles, 0x0762d272, 0xc50a, 0x4bb0, 0xa3, 0x82, 0x69, 0x7d, 0xcd, 0x72, 0x9b, 0x80);
W32_GUID(FOLDERID_Public, 0xdfdf76a2, 0xc82a, 0x4d63, 0x90, 0x6a, 0x56, 0x44, 0xac, 0x45, 0x73, 0x85);
W32_GUID(FOLDERID_PublicDocuments, 0xed4824af, 0xdce4, 0x45a8, 0x81, 0xe2, 0xfc, 0x79, 0x65, 0x08, 0x36, 0x34);
W32_GUID(FOLDERID_Documents, 0xfdd39ad0, 0x238f, 0x46af, 0xad, 0xb4, 0x6c, 0x85, 0x48, 0x03, 0x69, 0xc7);
W32_GUID(FOLDERID_Downloads, 0x374de290, 0x123f, 0x4565, 0x91, 0x64, 0x39, 0xc4, 0x92, 0x5e, 0x46, 0x7b);
W32_GUID(FOLDERID_Music, 0x4bd8d571, 0x6d19, 0x48d3, 0xbe, 0x97, 0x42, 0x22, 0x20, 0x08, 0x0e, 0x43);
W32_GUID(FOLDERID_Pictures, 0x33e28130, 0x4e1e, 0x4676, 0x83, 0x5a, 0x98, 0x39, 0x5c, 0x3b, 0xc3, 0xbb);
W32_GUID(FOLDERID_Videos, 0x18989b1d, 0x99b5, 0x455b, 0x84, 0x1c, 0xab, 0x7c, 0x74, 0xe4, 0xdd, 0xfc);
W32_GUID(FOLDERID_SavedGames, 0x4c5c32ff, 0xbb9d, 0x43b0, 0xb5, 0xb4, 0x2d, 0x72, 0xe5, 0x4e, 0xaa, 0xa4);
W32_GUID(FOLDERID_Windows, 0xf38bf404, 0x1d43, 0x42f2, 0x93, 0x05, 0x67, 0xde, 0x0b, 0x28, 0xfc, 0x23);
W32_GUID(FOLDERID_System, 0x1ac14e77, 0x02e7, 0x4e5d, 0xb7, 0x44, 0x2e, 0xb1, 0xae, 0x51, 0x98, 0xb7);

#endif  // W32_CHECK_FUNCS_ONLY

// ---------------------------------------------------------------- funções

// kernel32
W32IMP VOID WINAPI ExitProcess(UINT uExitCode);
W32IMP HANDLE WINAPI GetProcessHeap(void);
W32IMP LPVOID WINAPI HeapAlloc(HANDLE hHeap, DWORD dwFlags, SIZE_T dwBytes);
W32IMP LPVOID WINAPI HeapReAlloc(HANDLE hHeap, DWORD dwFlags, LPVOID lpMem, SIZE_T dwBytes);
W32IMP BOOL WINAPI HeapFree(HANDLE hHeap, DWORD dwFlags, LPVOID lpMem);
W32IMP DWORD WINAPI GetLastError(void);
W32IMP VOID WINAPI SetLastError(DWORD dwErrCode);
W32IMP HANDLE WINAPI CreateFileW(LPCWSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode, LPSECURITY_ATTRIBUTES lpSecurityAttributes,
                                 DWORD dwCreationDisposition, DWORD dwFlagsAndAttributes, HANDLE hTemplateFile);
W32IMP BOOL WINAPI ReadFile(HANDLE hFile, LPVOID lpBuffer, DWORD nNumberOfBytesToRead, LPDWORD lpNumberOfBytesRead, LPOVERLAPPED lpOverlapped);
W32IMP BOOL WINAPI WriteFile(HANDLE hFile, LPCVOID lpBuffer, DWORD nNumberOfBytesToWrite, LPDWORD lpNumberOfBytesWritten, LPOVERLAPPED lpOverlapped);
W32IMP BOOL WINAPI CloseHandle(HANDLE hObject);
W32IMP BOOL WINAPI CreateDirectoryW(LPCWSTR lpPathName, LPSECURITY_ATTRIBUTES lpSecurityAttributes);
W32IMP DWORD WINAPI GetFileAttributesW(LPCWSTR lpFileName);
W32IMP DWORD WINAPI GetShortPathNameW(LPCWSTR lpszLongPath, LPWSTR lpszShortPath, DWORD cchBuffer);
W32IMP BOOL WINAPI K32EnumProcesses(DWORD *lpidProcess, DWORD cb, LPDWORD lpcbNeeded);
W32IMP BOOL WINAPI GetFileAttributesExW(LPCWSTR lpFileName, GET_FILEEX_INFO_LEVELS fInfoLevelId, LPVOID lpFileInformation);
W32IMP BOOL WINAPI SetFileAttributesW(LPCWSTR lpFileName, DWORD dwFileAttributes);
W32IMP HANDLE WINAPI FindFirstFileExW(LPCWSTR lpFileName, FINDEX_INFO_LEVELS fInfoLevelId, LPVOID lpFindFileData, FINDEX_SEARCH_OPS fSearchOp,
                                      LPVOID lpSearchFilter, DWORD dwAdditionalFlags);
W32IMP BOOL WINAPI FindNextFileW(HANDLE hFindFile, LPWIN32_FIND_DATAW lpFindFileData);
W32IMP BOOL WINAPI FindClose(HANDLE hFindFile);
W32IMP BOOL WINAPI MoveFileExW(LPCWSTR lpExistingFileName, LPCWSTR lpNewFileName, DWORD dwFlags);
W32IMP BOOL WINAPI DeleteFileW(LPCWSTR lpFileName);
W32IMP BOOL WINAPI RemoveDirectoryW(LPCWSTR lpPathName);
W32IMP DWORD WINAPI ExpandEnvironmentStringsW(LPCWSTR lpSrc, LPWSTR lpDst, DWORD nSize);
W32IMP DWORD WINAPI GetEnvironmentVariableW(LPCWSTR lpName, LPWSTR lpBuffer, DWORD nSize);
W32IMP DWORD WINAPI GetModuleFileNameW(HMODULE hModule, LPWSTR lpFilename, DWORD nSize);
W32IMP HMODULE WINAPI GetModuleHandleW(LPCWSTR lpModuleName);
W32IMP HMODULE WINAPI LoadLibraryExW(LPCWSTR lpLibFileName, HANDLE hFile, DWORD dwFlags);
W32IMP FARPROC WINAPI GetProcAddress(HMODULE hModule, LPCSTR lpProcName);
W32IMP BOOL WINAPI FreeLibrary(HMODULE hLibModule);
W32IMP BOOL WINAPI CreateProcessW(LPCWSTR lpApplicationName, LPWSTR lpCommandLine, LPSECURITY_ATTRIBUTES lpProcessAttributes,
                                  LPSECURITY_ATTRIBUTES lpThreadAttributes, BOOL bInheritHandles, DWORD dwCreationFlags, LPVOID lpEnvironment,
                                  LPCWSTR lpCurrentDirectory, LPSTARTUPINFOW lpStartupInfo, LPPROCESS_INFORMATION lpProcessInformation);
W32IMP DWORD WINAPI WaitForSingleObject(HANDLE hHandle, DWORD dwMilliseconds);
W32IMP BOOL WINAPI GetExitCodeProcess(HANDLE hProcess, LPDWORD lpExitCode);
W32IMP HANDLE WINAPI CreateJobObjectW(LPSECURITY_ATTRIBUTES lpJobAttributes, LPCWSTR lpName);
W32IMP BOOL WINAPI AssignProcessToJobObject(HANDLE hJob, HANDLE hProcess);
W32IMP BOOL WINAPI SetInformationJobObject(HANDLE hJob, JOBOBJECTINFOCLASS JobObjectInformationClass, LPVOID lpJobObjectInformation,
                                           DWORD cbJobObjectInformationLength);
W32IMP HANDLE WINAPI CreateIoCompletionPort(HANDLE FileHandle, HANDLE ExistingCompletionPort, ULONG_PTR CompletionKey, DWORD NumberOfConcurrentThreads);
W32IMP BOOL WINAPI GetQueuedCompletionStatus(HANDLE CompletionPort, LPDWORD lpNumberOfBytesTransferred, PULONG_PTR lpCompletionKey,
                                             LPOVERLAPPED *lpOverlapped, DWORD dwMilliseconds);
W32IMP DWORD WINAPI ResumeThread(HANDLE hThread);
W32IMP BOOL WINAPI TerminateProcess(HANDLE hProcess, UINT uExitCode);
W32IMP HANDLE WINAPI OpenProcess(DWORD dwDesiredAccess, BOOL bInheritHandle, DWORD dwProcessId);
W32IMP BOOL WINAPI QueryFullProcessImageNameW(HANDLE hProcess, DWORD dwFlags, LPWSTR lpExeName, PDWORD lpdwSize);
W32IMP HANDLE WINAPI CreateThread(LPSECURITY_ATTRIBUTES lpThreadAttributes, SIZE_T dwStackSize, LPTHREAD_START_ROUTINE lpStartAddress, LPVOID lpParameter,
                                  DWORD dwCreationFlags, LPDWORD lpThreadId);
W32IMP HANDLE WINAPI CreateEventW(LPSECURITY_ATTRIBUTES lpEventAttributes, BOOL bManualReset, BOOL bInitialState, LPCWSTR lpName);
W32IMP BOOL WINAPI SetEvent(HANDLE hEvent);
W32IMP BOOL WINAPI ResetEvent(HANDLE hEvent);
W32IMP VOID WINAPI Sleep(DWORD dwMilliseconds);
W32IMP ULONGLONG WINAPI GetTickCount64(void);
W32IMP VOID WINAPI GetLocalTime(LPSYSTEMTIME lpSystemTime);
W32IMP BOOL WINAPI FileTimeToSystemTime(const FILETIME *lpFileTime, LPSYSTEMTIME lpSystemTime);
W32IMP BOOL WINAPI FileTimeToLocalFileTime(const FILETIME *lpFileTime, LPFILETIME lpLocalFileTime);
W32IMP HANDLE WINAPI CreateMutexW(LPSECURITY_ATTRIBUTES lpMutexAttributes, BOOL bInitialOwner, LPCWSTR lpName);
W32IMP LANGID WINAPI GetUserDefaultUILanguage(void);
W32IMP BOOL WINAPI CreatePipe(PHANDLE hReadPipe, PHANDLE hWritePipe, LPSECURITY_ATTRIBUTES lpPipeAttributes, DWORD nSize);
W32IMP BOOL WINAPI SetHandleInformation(HANDLE hObject, DWORD dwMask, DWORD dwFlags);
W32IMP UINT WINAPI GetSystemDirectoryW(LPWSTR lpBuffer, UINT uSize);
W32IMP UINT WINAPI GetWindowsDirectoryW(LPWSTR lpBuffer, UINT uSize);
W32IMP VOID WINAPI InitializeSRWLock(PSRWLOCK SRWLock);
W32IMP VOID WINAPI AcquireSRWLockExclusive(PSRWLOCK SRWLock);
W32IMP VOID WINAPI ReleaseSRWLockExclusive(PSRWLOCK SRWLock);
W32IMP HLOCAL WINAPI LocalFree(HLOCAL hMem);
W32IMP LPWSTR WINAPI GetCommandLineW(void);
W32IMP DWORD WINAPI GetCurrentProcessId(void);
W32IMP DWORD WINAPI FormatMessageW(DWORD dwFlags, LPCVOID lpSource, DWORD dwMessageId, DWORD dwLanguageId, LPWSTR lpBuffer, DWORD nSize, va_list *Arguments);


// user32
W32IMP ATOM WINAPI RegisterClassExW(const WNDCLASSEXW *lpwcx);
W32IMP HWND WINAPI CreateWindowExW(DWORD dwExStyle, LPCWSTR lpClassName, LPCWSTR lpWindowName, DWORD dwStyle, int X, int Y, int nWidth, int nHeight,
                                   HWND hWndParent, HMENU hMenu, HINSTANCE hInstance, LPVOID lpParam);
W32IMP LRESULT WINAPI DefWindowProcW(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam);
W32IMP BOOL WINAPI ShowWindow(HWND hWnd, int nCmdShow);
W32IMP BOOL WINAPI UpdateWindow(HWND hWnd);
W32IMP BOOL WINAPI GetMessageW(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax);
W32IMP BOOL WINAPI TranslateMessage(const MSG *lpMsg);
W32IMP LRESULT WINAPI DispatchMessageW(const MSG *lpMsg);
W32IMP BOOL WINAPI IsDialogMessageW(HWND hDlg, LPMSG lpMsg);
W32IMP VOID WINAPI PostQuitMessage(int nExitCode);
W32IMP BOOL WINAPI PostMessageW(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam);
W32IMP LRESULT WINAPI SendMessageW(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam);
W32IMP LRESULT WINAPI SendMessageTimeoutW(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam, UINT fuFlags, UINT uTimeout, PDWORD_PTR lpdwResult);
W32IMP int WINAPI MessageBoxW(HWND hWnd, LPCWSTR lpText, LPCWSTR lpCaption, UINT uType);
W32IMP HCURSOR WINAPI LoadCursorW(HINSTANCE hInstance, LPCWSTR lpCursorName);
W32IMP HICON WINAPI LoadIconW(HINSTANCE hInstance, LPCWSTR lpIconName);
W32IMP HANDLE WINAPI LoadImageW(HINSTANCE hInst, LPCWSTR name, UINT type, int cx, int cy, UINT fuLoad);
W32IMP BOOL WINAPI SetWindowTextW(HWND hWnd, LPCWSTR lpString);
W32IMP int WINAPI GetWindowTextW(HWND hWnd, LPWSTR lpString, int nMaxCount);
W32IMP int WINAPI GetWindowTextLengthW(HWND hWnd);
W32IMP BOOL WINAPI MoveWindow(HWND hWnd, int X, int Y, int nWidth, int nHeight, BOOL bRepaint);
W32IMP BOOL WINAPI SetWindowPos(HWND hWnd, HWND hWndInsertAfter, int X, int Y, int cx, int cy, UINT uFlags);
W32IMP BOOL WINAPI GetClientRect(HWND hWnd, LPRECT lpRect);
W32IMP BOOL WINAPI GetWindowRect(HWND hWnd, LPRECT lpRect);
W32IMP BOOL WINAPI DestroyWindow(HWND hWnd);
W32IMP BOOL WINAPI EnableWindow(HWND hWnd, BOOL bEnable);
W32IMP HWND WINAPI SetFocus(HWND hWnd);
W32IMP LONG_PTR WINAPI SetWindowLongPtrW(HWND hWnd, int nIndex, LONG_PTR dwNewLong);
W32IMP LONG_PTR WINAPI GetWindowLongPtrW(HWND hWnd, int nIndex);
W32IMP HMENU WINAPI CreatePopupMenu(void);
W32IMP BOOL WINAPI AppendMenuW(HMENU hMenu, UINT uFlags, UINT_PTR uIDNewItem, LPCWSTR lpNewItem);
W32IMP BOOL WINAPI DestroyMenu(HMENU hMenu);
W32IMP BOOL WINAPI TrackPopupMenu(HMENU hMenu, UINT uFlags, int x, int y, int nReserved, HWND hWnd, const RECT *prcRect);
W32IMP BOOL WINAPI GetCursorPos(LPPOINT lpPoint);
W32IMP HWND WINAPI SetCapture(HWND hWnd);
W32IMP BOOL WINAPI ReleaseCapture(void);
W32IMP HWND WINAPI WindowFromPoint(POINT Point);
W32IMP HWND WINAPI GetAncestor(HWND hwnd, UINT gaFlags);
W32IMP DWORD WINAPI GetWindowThreadProcessId(HWND hWnd, LPDWORD lpdwProcessId);
W32IMP BOOL WINAPI SystemParametersInfoW(UINT uiAction, UINT uiParam, PVOID pvParam, UINT fWinIni);
W32IMP UINT WINAPI GetDpiForWindow(HWND hwnd);
W32IMP int WINAPI GetSystemMetrics(int nIndex);
W32IMP HCURSOR WINAPI SetCursor(HCURSOR hCursor);
W32IMP BOOL WINAPI InvalidateRect(HWND hWnd, const RECT *lpRect, BOOL bErase);
W32IMP HDC WINAPI BeginPaint(HWND hWnd, LPPAINTSTRUCT lpPaint);
W32IMP BOOL WINAPI EndPaint(HWND hWnd, const PAINTSTRUCT *lpPaint);
W32IMP int WINAPI FillRect(HDC hDC, const RECT *lprc, HBRUSH hbr);
W32IMP BOOL WINAPI DrawIconEx(HDC hdc, int xLeft, int yTop, HICON hIcon, int cxWidth, int cyWidth, UINT istepIfAniCur, HBRUSH hbrFlickerFreeDraw,
                              UINT diFlags);
W32IMP BOOL WINAPI SetForegroundWindow(HWND hWnd);
W32IMP HWND WINAPI GetForegroundWindow(void);
W32IMP HWND WINAPI FindWindowW(LPCWSTR lpClassName, LPCWSTR lpWindowName);
W32IMP BOOL WINAPI IsWindowVisible(HWND hWnd);
W32IMP BOOL WINAPI IsIconic(HWND hWnd);
W32IMP BOOL WINAPI IsWindow(HWND hWnd);
W32IMP BOOL WINAPI IsWindowEnabled(HWND hWnd);
W32IMP UINT_PTR WINAPI SetTimer(HWND hWnd, UINT_PTR nIDEvent, UINT uElapse, TIMERPROC lpTimerFunc);
W32IMP BOOL WINAPI KillTimer(HWND hWnd, UINT_PTR uIDEvent);
W32IMP HBRUSH WINAPI GetSysColorBrush(int nIndex);
W32IMP DWORD WINAPI GetSysColor(int nIndex);
W32IMP BOOL WINAPI ScreenToClient(HWND hWnd, LPPOINT lpPoint);
W32IMP BOOL WINAPI ClientToScreen(HWND hWnd, LPPOINT lpPoint);
W32IMP HMONITOR WINAPI MonitorFromWindow(HWND hwnd, DWORD dwFlags);
W32IMP BOOL WINAPI GetMonitorInfoW(HMONITOR hMonitor, LPMONITORINFO lpmi);
W32IMP BOOL WINAPI DestroyIcon(HICON hIcon);
W32IMP BOOL WINAPI SetLayeredWindowAttributes(HWND hwnd, COLORREF crKey, BYTE bAlpha, DWORD dwFlags);
W32IMP SHORT WINAPI GetKeyState(int nVirtKey);
W32IMP HWND WINAPI GetParent(HWND hWnd);
W32IMP HWND WINAPI GetShellWindow(void);
W32IMP int WINAPI DrawTextW(HDC hdc, LPCWSTR lpchText, int cchText, LPRECT lprc, UINT format);
W32IMP BOOL WINAPI AdjustWindowRectEx(LPRECT lpRect, DWORD dwStyle, BOOL bMenu, DWORD dwExStyle);
W32IMP int WINAPI GetDlgCtrlID(HWND hWnd);
W32IMP HDC WINAPI GetDC(HWND hWnd);
W32IMP int WINAPI ReleaseDC(HWND hWnd, HDC hDC);
W32IMP BOOL WINAPI PrintWindow(HWND hwnd, HDC hdcBlt, UINT nFlags);

// gdi32
W32IMP HFONT WINAPI CreateFontIndirectW(const LOGFONTW *lplf);
W32IMP HGDIOBJ WINAPI SelectObject(HDC hdc, HGDIOBJ h);
W32IMP BOOL WINAPI DeleteObject(HGDIOBJ ho);
W32IMP HGDIOBJ WINAPI GetStockObject(int i);
W32IMP int WINAPI SetBkMode(HDC hdc, int mode);
W32IMP COLORREF WINAPI SetTextColor(HDC hdc, COLORREF color);
W32IMP COLORREF WINAPI SetBkColor(HDC hdc, COLORREF color);
W32IMP HBRUSH WINAPI CreateSolidBrush(COLORREF color);
W32IMP HPEN WINAPI CreatePen(int iStyle, int cWidth, COLORREF color);
W32IMP BOOL WINAPI Ellipse(HDC hdc, int left, int top, int right, int bottom);
W32IMP BOOL WINAPI MoveToEx(HDC hdc, int x, int y, LPPOINT lppt);
W32IMP BOOL WINAPI LineTo(HDC hdc, int x, int y);
W32IMP HDC WINAPI CreateCompatibleDC(HDC hdc);
W32IMP BOOL WINAPI DeleteDC(HDC hdc);
W32IMP HBITMAP WINAPI CreateDIBSection(HDC hdc, const BITMAPINFO *pbmi, UINT usage, VOID **ppvBits, HANDLE hSection, DWORD offset);

// comctl32
W32IMP BOOL WINAPI InitCommonControlsEx(const INITCOMMONCONTROLSEX *picce);
W32IMP HIMAGELIST WINAPI ImageList_Create(int cx, int cy, UINT flags, int cInitial, int cGrow);
W32IMP int WINAPI ImageList_ReplaceIcon(HIMAGELIST himl, int i, HICON hicon);
W32IMP BOOL WINAPI ImageList_Destroy(HIMAGELIST himl);

// shell32
W32IMP int WINAPI SHFileOperationW(LPSHFILEOPSTRUCTW lpFileOp);
W32IMP HINSTANCE WINAPI ShellExecuteW(HWND hwnd, LPCWSTR lpOperation, LPCWSTR lpFile, LPCWSTR lpParameters, LPCWSTR lpDirectory, INT nShowCmd);
W32IMP HRESULT WINAPI SHGetKnownFolderPath(REFKNOWNFOLDERID rfid, DWORD dwFlags, HANDLE hToken, PWSTR *ppszPath);
W32IMP UINT WINAPI ExtractIconExW(LPCWSTR lpszFile, int nIconIndex, HICON *phiconLarge, HICON *phiconSmall, UINT nIcons);
W32IMP PIDLIST_ABSOLUTE WINAPI SHBrowseForFolderW(LPBROWSEINFOW lpbi);
W32IMP BOOL WINAPI SHGetPathFromIDListW(PCIDLIST_ABSOLUTE pidl, LPWSTR pszPath);
W32IMP BOOL WINAPI IsUserAnAdmin(void);
W32IMP LPWSTR *WINAPI CommandLineToArgvW(LPCWSTR lpCmdLine, int *pNumArgs);

// ole32
W32IMP HRESULT WINAPI CoInitializeEx(LPVOID pvReserved, DWORD dwCoInit);
W32IMP void WINAPI CoUninitialize(void);
W32IMP void WINAPI CoTaskMemFree(LPVOID pv);

// advapi32
W32IMP LSTATUS WINAPI RegOpenKeyExW(HKEY hKey, LPCWSTR lpSubKey, DWORD ulOptions, REGSAM samDesired, PHKEY phkResult);
W32IMP LSTATUS WINAPI RegCloseKey(HKEY hKey);
W32IMP LSTATUS WINAPI RegEnumKeyExW(HKEY hKey, DWORD dwIndex, LPWSTR lpName, LPDWORD lpcchName, LPDWORD lpReserved, LPWSTR lpClass, LPDWORD lpcchClass,
                                    PFILETIME lpftLastWriteTime);
W32IMP LSTATUS WINAPI RegEnumValueW(HKEY hKey, DWORD dwIndex, LPWSTR lpValueName, LPDWORD lpcchValueName, LPDWORD lpReserved, LPDWORD lpType, LPBYTE lpData,
                                    LPDWORD lpcbData);
W32IMP LSTATUS WINAPI RegQueryValueExW(HKEY hKey, LPCWSTR lpValueName, LPDWORD lpReserved, LPDWORD lpType, LPBYTE lpData, LPDWORD lpcbData);
W32IMP LSTATUS WINAPI RegQueryInfoKeyW(HKEY hKey, LPWSTR lpClass, LPDWORD lpcchClass, LPDWORD lpReserved, LPDWORD lpcSubKeys, LPDWORD lpcbMaxSubKeyLen,
                                       LPDWORD lpcbMaxClassLen, LPDWORD lpcValues, LPDWORD lpcbMaxValueNameLen, LPDWORD lpcbMaxValueLen,
                                       LPDWORD lpcbSecurityDescriptor, PFILETIME lpftLastWriteTime);
W32IMP LSTATUS WINAPI RegSetValueExW(HKEY hKey, LPCWSTR lpValueName, DWORD Reserved, DWORD dwType, const BYTE *lpData, DWORD cbData);
W32IMP LSTATUS WINAPI RegDeleteValueW(HKEY hKey, LPCWSTR lpValueName);
W32IMP LSTATUS WINAPI RegDeleteTreeW(HKEY hKey, LPCWSTR lpSubKey);
W32IMP LSTATUS WINAPI RegDeleteKeyExW(HKEY hKey, LPCWSTR lpSubKey, REGSAM samDesired, DWORD Reserved);
W32IMP LSTATUS WINAPI RegCreateKeyExW(HKEY hKey, LPCWSTR lpSubKey, DWORD Reserved, LPWSTR lpClass, DWORD dwOptions, REGSAM samDesired,
                                      LPSECURITY_ATTRIBUTES lpSecurityAttributes, PHKEY phkResult, LPDWORD lpdwDisposition);
W32IMP SC_HANDLE WINAPI OpenSCManagerW(LPCWSTR lpMachineName, LPCWSTR lpDatabaseName, DWORD dwDesiredAccess);
W32IMP SC_HANDLE WINAPI OpenServiceW(SC_HANDLE hSCManager, LPCWSTR lpServiceName, DWORD dwDesiredAccess);
W32IMP BOOL WINAPI ControlService(SC_HANDLE hService, DWORD dwControl, LPSERVICE_STATUS lpServiceStatus);
W32IMP BOOL WINAPI QueryServiceStatus(SC_HANDLE hService, LPSERVICE_STATUS lpServiceStatus);
W32IMP BOOL WINAPI DeleteService(SC_HANDLE hService);
W32IMP BOOL WINAPI CloseServiceHandle(SC_HANDLE hSCObject);

// comdlg32
W32IMP BOOL WINAPI GetOpenFileNameW(LPOPENFILENAMEW lpofn);

// uxtheme
W32IMP HRESULT WINAPI SetWindowTheme(HWND hwnd, LPCWSTR pszSubAppName, LPCWSTR pszSubIdList);

#endif
