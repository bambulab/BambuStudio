#/bin/sh

./configure --system-name=GCC
mv Makefile script/Makefile.gcc
./configure --system-name=GCC --shared
mv Makefile script/Makefile.gcc_so
./configure --system-name=CYGWIN
mv Makefile script/Makefile.cygwin
./configure --system-name=CYGWIN --shared
mv Makefile script/Makefile.cygwin_dll
./configure --system-name=MSVC
mv Makefile script/Makefile.msvc
./configure --system-name=MSVC --shared
mv Makefile script/Makefile.msvc_dll
./configure --system-name=BCC
mv Makefile script/Makefile.bcc32
./configure --system-name=BCC --shared
mv Makefile script/Makefile.bcc32_dll
./configure --system-name=MINGW
mv Makefile script/Makefile.mingw
./configure --system-name=MINGW --shared
mv Makefile script/Makefile.mingw_dll

exit 0

