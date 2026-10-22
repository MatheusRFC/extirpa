# Changelog

All notable changes to EXTIRPA. Dates are in the author's time zone.

## [Unreleased]

## [1.1.0] — 2026-10-22

The leftover search now covers what Windows itself keeps about a program, and the results come in two steps: registry, then files and folders.

### Added
- **Two-step leftover window:** first the registry and system items (keys, values, services, scheduled tasks, firewall rules, PATH, packages), with counts of keys and values; then the files and folders, with counts and total size. Each step has its own **Delete checked**; after the registry is cleaned without failures, the window moves on to the files.
- **Analysis before uninstalling:** right before the program's uninstaller runs, EXTIRPA searches with the program still installed. Whatever that search found and still exists after the uninstaller is added to the leftovers.
- **Windows Installer leftovers** of MSI programs, by product code: the product's registration (`Installer\Products`, `Features`, `UpgradeCodes`, `UserData\<user>\Products`, the components only it used, and only its value in shared ones), the `Installer\Folders` entries for its folders and for its cache folder, the cached package (`C:\Windows\Installer\<name>.msi`, only the one registered as its `LocalPackage`), `C:\Windows\Installer\{code}` and `SourceHash{code}`. After a normal `msiexec /x` only the cache folder and its `Folders` entry usually remain; in a forced uninstall the whole registration is offered, so Windows no longer thinks the program is installed.
- **Traces kept under the executable's name:** Prefetch files (`C:\Windows\Prefetch\NAME.EXE-XXXXXXXX.pf`), crash dumps (`%LOCALAPPDATA%\CrashDumps`), Windows Error Reporting folders (`AppCrash_name.exe_...`), network tracing keys (`Microsoft\Tracing\name_RASAPI32`/`_RASMANCS`), heap-leak diagnosis and DirectInput keys. High confidence when the executable has the program's exact name; medium for other executables that were in the program's folder; low for generic names (`unins000`, `setup`, `update`...) or names another installed program also uses. The executables in the program's folders are listed before uninstalling.
- Explorer's FeatureUsage (taskbar usage per program), the "new app for this file type" notices (`ApplicationAssociationToasts`), `Compatibility Assistant\Persisted`, `Applications\<program>.exe` keys with no command left, and dot folders in the user profile (`%USERPROFILE%\.<program>`).

### Fixed
- A file type class that belongs to Windows or to another program (`AudioCD`, `DVD`...) where the program only added a verb (VLC's "Play with VLC") is no longer offered as a whole key: only the program's own verb (`shell\<verb>`) is. Found while testing this version: VLC adds a verb to `AudioCD` and `DVD`, and a first build offered (and on the test machine deleted) the whole keys. Versions 1.0.x had the same flaw in forced uninstalls of a program still installed.
- The analysis before uninstalling only brings back items that are still the program's: items found by name or path, and values whose content is still the same. A file type association that the uninstaller handed back to another program (`.mp3` pointing to another player again) is not offered.

### Changed
- **Protection list:** narrow exceptions for the items above. Only keys named by the program's exact packed product code under the Windows Installer areas, values of `Installer\Folders`, and, inside the Windows folder, only direct children of `C:\Windows\Installer` named `{code}`, `SourceHash{code}` or `<name>.msi`/`.msp`, and `Prefetch\NAME.EXE-XXXXXXXX.pf` files. The keys that group all products and the rest of the Windows folder stay protected. New keys protected as a whole: `FeatureUsage` and its subkeys, `ApplicationAssociationToasts`, `Microsoft\Tracing`, `RADAR\HeapLeakDetection\DiagnosedApplications`, `DirectInput`.


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
