# Security

EXTIRPA runs as administrator and deletes registry keys, files and system objects. Deleting the wrong thing is the main risk, so most of the design is about not doing that.

## What could go wrong and how EXTIRPA prevents it

| Risk | Defense |
|---|---|
| Deleting a part of Windows | A protection list covers system folders (Windows, Program Files and Program Files (x86) themselves, ProgramData, the user profile folders, AppData itself, the Start menu, Startup, Temp...) and the registry areas that belong to Windows (`HKLM\SYSTEM` and `SOFTWARE\Microsoft\Windows NT` as whole trees, Windows Installer data, policies, the `SOFTWARE` and `SOFTWARE\Microsoft` keys themselves, the roots of file types, COM and TypeLib, `CurrentVersion\Run` itself...). A protected item, or anything that contains one, is never deleted. Protection is checked when an item enters the list and again right before it is deleted. |
| Deleting another installed program | Before scanning, EXTIRPA reads every other installed program (name, publisher, folders). The most specific folder decides who owns a path. A name shared with another installed program comes unchecked, and EXTIRPA looks inside it for the part that belongs only to the program being removed. A folder that contains another installed program is removed item by item, keeping the other program. A registry key that refers to another installed program comes unchecked. |
| Windows' own scheduled tasks | Tasks under `\Microsoft\` are never touched. |
| Usage records Windows keeps for all programs | In MuiCache and the compatibility settings, only the program's own values are deleted; the keys stay. |
| A false match | Every item shows its type, a confidence level and why it was found. Only high-confidence items (exact name, the program's own folder, something that points inside it) come checked. The Advanced mode's deep registry scan only produces low-confidence, unchecked items. |
| Losing something you wanted to keep | Before any registry change, a `.reg` backup of everything that will be deleted is written to `%LOCALAPPDATA%\EXTIRPA\Backups`; double-click it (or use the Backups tab) to restore. Files go to the Recycle Bin. Removed scheduled tasks are saved as `.xml`. A Windows restore point can be created before each uninstall. Items that could not go to the Recycle Bin (very long paths) are reported as deleted permanently. |
| Editing PATH wrongly | Only the entries that point inside the program's folders are removed; the variable keeps its type (`REG_EXPAND_SZ`) and every other entry, and the change is announced to running programs. |
| Running something it shouldn't | EXTIRPA only runs the uninstaller the program registered, the installer you pick for tracing, `msiexec` (always from the System32 folder, never the first one on PATH) for Windows Installer products, `schtasks`, and PowerShell's `Remove-NetFirewallRule`/`Remove-AppxPackage`, with the script passed as `-EncodedCommand` and package names validated before use. |

Everything EXTIRPA deletes, and every failure, is written to `%LOCALAPPDATA%\EXTIRPA\extirpa.log`.

## Known limitations

- The executable is not code-signed yet, so Windows SmartScreen warns on first run.
- It needs administrator rights, because uninstalling programs installed for all users requires them.

## Reporting a vulnerability

Please open a private security advisory in this repository (**Security → Report a vulnerability**) instead of a public issue.
