A hack for loading system libraries in Android Nougat where dlopen() has been disabled by some wise guys.<br>
A sample app (written entirely in C) using the famous Freetype2 library to render character strings is included
as well as a trivial example explaining how to deal with Android C++ classes.<br>
The main file "jni/fake_dlfcn.c" is intended for arm and aarch64 only but should be easily portable to other architectures as well.

