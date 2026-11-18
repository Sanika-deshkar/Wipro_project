READY-TO-RUN PROGRAMS (no compiler, no install needed)
=======================================================

svsm.exe          Windows 10/11, 64-bit        (statically linked, no DLLs)
svsm-linux-x64    Linux x86-64                 (statically linked)

Run them from the project folder (the one that contains "web"):

  Windows :  double-click ..\start.bat            or   dist\svsm.exe --serve 8080 --open
  Linux   :  sh start.sh                          or   dist/svsm-linux-x64 --serve 8080 --open

Other modes:   svsm --help   |   svsm (terminal dashboard)   |   svsm --demo 40

Windows SmartScreen may say "Windows protected your PC" because the program is
not code-signed. Click "More info" -> "Run anyway".

Rebuild both from source (on Linux / WSL with gcc and mingw-w64):   make dist
