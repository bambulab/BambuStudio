# Haru Free PDF Library [![Build Status](https://github.com/libharu/libharu/actions/workflows/cmake.yml/badge.svg)](https://github.com/libharu/libharu/actions/workflows/cmake.yml)

  **URL http://libharu.org/**

  **Copyright 2000-2006 (c) Takeshi Kanno**

  **Copyright 2007-2009 (c) Antony Dovgal et al.**

See INSTALL for instructions on how to install libHaru.


# What is Haru Free PDF Library?

Haru is a free, cross platform, open-sourced software library for generating 
PDF. It supports the following features.

   1. Generating PDF files with lines, text, images.
   2. Outline, text annotation, link annotation.
   3. Compressing document with deflate-decode.
   4. Embedding PNG, Jpeg images.
   5. Embedding Type1 font and TrueType font.
   6. Creating encrypted PDF files.
   7. Using various character set (ISO8859-1\~16, MSCP1250\~8, KOI8-R).
   8. Supporting CJK fonts and encodings.

You can add the feature of PDF creation by using Haru without understanding 
complicated internal structure of PDF.


# The differences from the previous version 


The biggest differences are that all code is written in C. 
To our regret, because internal structures changed greatly, The API is not 
compatible with previous version.

However, the new-version has the following advantages compared with the 
previous version.

 1. Supported shared-library build.
    A new-version library is able to be built as shared-library.
 2. The performance has been greatly improved.
    A new-version library is about 4-15 times faster than a previous version. 
 3. Supported more features.
    TrueType font, 128 bit encryption, arc function, and so on.


# Supporting platforms

Haru is written in ANSI-C and should compile easily with any compliant C 
compiler.
I inspected Haru in the following environment and make files for these 
environments are included in a package.

   1. Cygwin + GCC (Microsoft Windows)
   2. Cygwin + MinGW (Microsoft Windows)
   3. MSYS + MinGW (Microsoft Windows)
   3. Microsoft VC++ (Microsoft Windows)
   4. Borland C++ (Microsoft Windows)
   5. GCC (Linux, FreeBSD, NetBSD, Solaris...)

Also on platforms except the above, it is easy to build HARU. If you success to build HARU on other platforms, please send makefile to me.
In addition, ZLIB and PNGLIB are required when you want to use the features of 
compression and embedding PNG images. (In the case of Windows, static library 
files for several compilers are included in the package for WIndows.  In the 
case of  most of UNIX, these libraries are usually installed.)


# Available development environment

Haru can work as both static-library (.a, .lib) and shared-library (.so, .dll).
When you use it as static-library, It can be used by C and a C++.
But when you use it as shared-library, it can be used by many development 
languages which support shared library.
So far, Haru provides bindings for Ruby, Delphi/Free Pascal and C#.

If you write bindings for other programming languages, please notice me!


# Runtime environment of programs using Haru

1. static-library
   No runtime files are required.

2. shared-library
   In Windows, you have to distribute libhpdf.dll with a program. In UNIX you 
   have to distribute libhpdf.so* with a program.



NOTE:
In the UNIX environment, there are the cases that libz.so, libpng.so.x are 
necessary. About this, please refer to the documentation of PNGLIB and ZLIB.


# License

Haru is distributed under the ZLIB/LIBPNG License. Because ZLIB/LIBPNG License 
is one of the freest licenses, You can use Haru for various purposes.

The license of Haru is as follows.

Copyright (C) 1999-2006 Takeshi Kanno
Copyright (C) 2007-2009 Antony Dovgal

This software is provided 'as-is', without any express or implied warranty.

In no event will the authors be held liable for any damages arising from the 
use of this software.

Permission is granted to anyone to use this software for any purpose,including 
commercial applications, and to alter it and redistribute it freely, subject 
to the following restrictions:

 1. The origin of this software must not be misrepresented; you must not claim 
    that you wrote the original software. If you use this software in a 
    product, an acknowledgment in the product documentation would be 
    appreciated but is not required.
 2. Altered source versions must be plainly marked as such, and must not be 
    misrepresented as being the original software.
 3. This notice may not be removed or altered from any source distribution.



# Acknowledgment

1. Information-technology Promotion Agency(IPA)
   The development of Haru has been supported by Exploratory Software Project 
   of Information-technology Promotion Agency(IPA), Japan. 

2. All users of libHaru.
   We wish to thank all users of Haru.
   In particular, we thank Thomas Nimstad, LeslieM, Par Hogberg, adenelson, 
   Riccardo Cohen, sea_sbs, Andrew. 
   They gave me very useful advice.

3. Adobe Systems Inc.
   We thank Adobe Systems Inc. for publishing PDF specification.
