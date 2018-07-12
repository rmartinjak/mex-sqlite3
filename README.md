mex-sqlite3
===========

An extension for MATLAB® or GNU/octave to access sqlite3 databases

Compilation for octave :
mkoctfile --mex -lsqlite3 -o sqlite3.mex sqlite3.c structlist.c

Then add resulting sqlite3.mex file to octave search path, for example :
%cd to your .mex file
p=genpath(pwd)
addpath(p)
