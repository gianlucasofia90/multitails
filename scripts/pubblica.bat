@echo off
rem MULTITAILS - doppio clic: push delle modifiche, merge su main e tag v0.1.0.
rem Per un altro tag:  pubblica.bat -Version 0.1.1
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0pubblica.ps1" %*
