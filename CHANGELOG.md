# Changelog

All notable changes to EXTIRPA. Dates are in the author's time zone.

## [Unreleased]

## [1.0.1] — 2026-10-21

Leftover search for Windows Installer (MSI) programs that don't record their install folder, found testing GridCraft 0.9.0: its uninstall entry has no `InstallLocation`, no icon and `MsiExec.exe` as the uninstaller, so EXTIRPA didn't know where the program lived and only found its AppData folder.

### Fixed
- **Install folder of MSI programs** is now read from the installer's own component records (`Installer\UserData\<user>\Components`): the deepest folder shared by the program's files under Program Files (or `%LOCALAPPDATA%\Programs`). Components shared with another product are ignored. When there are no component records, the folder comes from App Paths and from Start menu shortcuts with the program's exact name. This also applies to forced uninstall, tracing and "Open folder".
- With the folder known, App Paths, file types (ProgIDs), "Open with" lists, Start menu shortcuts (including Windows Installer "advertised" shortcuts, which have only a working folder), compatibility settings, MuiCache and UserAssist entries of such programs are found and come checked.
- Other installed MSI programs are now known by their folders too, so their files are never taken for the program being removed.

### Added
- The Start menu's list of used shortcuts (`UFH\SHC`) is searched; only the program's values are removed, the key itself is protected.
- When nothing says where the program was (forced uninstall of an orphan entry), usage traces, App Paths keys and file types that point to an executable or shortcut with the program's exact name that no longer exists are found by name. A file type with the program's name whose executable still exists comes unchecked, and so do the "Open with" entries that point to it.
- RegisteredApplications values with the program's exact name that point to a key that no longer exists.
- UserAssist and Jump List entries written with the Start menu or desktop folders in front (`{0139D44E-...}\GridCraft.lnk`).


## [1.0.0] — 2026-10-20

First release.

### Added
- **Uninstall** with the program's own uninstaller, waiting for it and every process it starts (job object), then a search for leftovers in three modes: Safe, Moderate (recommended) and Advanced.
- **Leftover search** in the install folder (and the publisher's folder around it, if left empty); Program Files, ProgramData, AppData (Roaming, Local, LocalLow) and the publisher's folders, including loose files with the program's exact name (like `winscp.rnd`); registry keys under SOFTWARE (64 and 32 bits, machine and user); Run, RunOnce and the Task Manager's startup switches (also the ones left behind after the uninstaller removed the entry); services; scheduled tasks; Windows Firewall rules; the PATH variable (system and user); App Paths and SharedDLLs; file types (ProgIDs), "Open with" lists and default associations; COM components and type libraries; context menu entries and Explorer extensions (handlers, icon overlays, approved extensions, browser helper objects); AutoPlay handlers; Windows packages the program registered (such as the package that adds it to the Windows 11 context menu), removed with `Remove-AppxPackage`; Software\Clients and RegisteredApplications; shortcuts on the desktop, Start menu, Startup folder and taskbar pins; usage traces (MuiCache, compatibility settings, UserAssist, Jump Lists, Explorer's "Open with" lists). Paths in short 8.3 form are recognized. The Advanced mode adds similar names and a deep registry scan for any value that points into the program's folders.
- Every leftover shows its type, a confidence level (high items come checked; medium and low come unchecked) and why it was found.
- **Protection rules:** a protection list for system folders and Windows registry areas, checked twice; the most specific folder decides who owns a path; names shared with another installed program come unchecked and are narrowed down to the part of the program being removed; folders that contain another installed program are removed item by item; Windows' own scheduled tasks are never touched.
- **Backups:** a `.reg` file before every registry deletion, the Recycle Bin for files, `.xml` for removed scheduled tasks and an optional Windows restore point before each uninstall. The Backups tab restores or deletes them.
- **Quick and batch uninstall**, silent when the uninstaller allows it: Windows Installer (MSI), Inno Setup, NSIS (both recognized from the uninstaller itself, so Inno Setup gets `/VERYSILENT` instead of a `/SILENT` that still asks questions) and `QuietUninstallString`.
- **Forced uninstall** for programs that are already gone, broken or have no uninstaller, by name, folder or both.
- **Install tracing:** snapshots of the registry and folders before and after an installer runs; "Uninstall using trace" removes exactly what it created.
- **Hunter mode:** a crosshair that can be dropped on any window to uninstall that program, open its folder, end it or remove it from startup.
- **Windows apps:** lists and removes Microsoft Store and built-in apps, for the current user or all users.
- **Startup manager:** enable, disable or delete what starts with Windows.
- Before deleting leftovers, lists the programs still running from the folders about to be deleted and offers to close them.
- Log of every deletion, failure and backup in `%LOCALAPPDATA%\EXTIRPA\extirpa.log`.
- Interface in Portuguese (Brazil) and English, per-monitor DPI.
- A single native executable of about 240 KB, written in C without a C library, built with LLVM only.
