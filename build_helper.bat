@echo off

rem Script to build a WDK7 project from within a VS2012 environment
rem
rem Copyright (c) 2014 Battelle Memorial Institute
rem Licensed under a modification of the 3-clause BSD license
rem See License.txt for the full text of the license and additional disclaimers
rem
rem %1 Path to project to build
rem %2 Build type (chk or fre)
rem %3 Architecture (x86 or x64)

call C:\WinDDK\7600.16385.1\bin\setenv.bat C:\WinDDK\7600.16385.1\ %2 %3 WIN7
cd /d %1
build.exe /cefgw
