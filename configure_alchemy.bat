@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
call ".\.venv\Scripts\activate.bat"
"C:\Program Files\CMake\bin\cmake.exe" -S indra --preset vs2022-os -DINSTALL_PROPRIETARY=ON -DUSE_KDU=OFF
