// SPDX-License-Identifier: MIT
__declspec(dllimport) void __stdcall ExitProcess(unsigned);
void WinMainCRTStartup(void) { ExitProcess(42); }
