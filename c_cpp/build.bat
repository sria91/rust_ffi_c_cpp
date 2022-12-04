@echo off

cmake.exe -S . -B build
msbuild build\c_cpp.sln
