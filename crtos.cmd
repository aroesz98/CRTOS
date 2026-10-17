@echo off
rem crtos: build CRTOS and work with the board ("crtos --help", README.md)
where py >nul 2>nul
if %errorlevel%==0 (
  py -3 "%~dp0tools\crtos.py" %*
) else (
  python "%~dp0tools\crtos.py" %*
)
exit /b %errorlevel%
