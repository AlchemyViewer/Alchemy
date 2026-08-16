@echo off
python "%~dp0scripts\bench.py" %*
exit /b %ERRORLEVEL%
