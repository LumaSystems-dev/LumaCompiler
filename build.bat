@echo off
rem Build Luma compiler and tests. Run from project root or by full path.
setlocal
cd /d "%~dp0"

set CXX=g++
rem Static executables do not depend on MinGW DLLs.
set CXXFLAGS=-std=c++17 -Wall -Wextra -static

rem Shared compiler sources; main.cpp belongs only to luma.exe.
set LIB_SRC=src\lexer.cpp src\parser.cpp src\value.cpp src\interpreter.cpp src\analyzer.cpp src\chunk.cpp src\compiler.cpp src\vm.cpp src\imports.cpp

echo [1/2] Building luma.exe...
%CXX% %CXXFLAGS% src\main.cpp %LIB_SRC% -o luma.exe
if errorlevel 1 exit /b 1

echo [2/2] Building tests...
%CXX% %CXXFLAGS% tests\test_main.cpp tests\test_lexer.cpp tests\test_parser.cpp tests\test_interpreter.cpp tests\test_statements.cpp tests\test_analyzer.cpp tests\test_bytecode.cpp tests\test_vm.cpp tests\test_serialize.cpp tests\test_import.cpp tests\test_input.cpp tests\test_stdlib.cpp %LIB_SRC% -o tests\run_tests.exe
if errorlevel 1 exit /b 1

echo Done.
