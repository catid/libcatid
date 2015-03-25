This section describes how to download and build LibCat for use in another application.


# How to get a copy #

Releases are available here: http://code.google.com/p/libcatid/downloads/list

To download the latest code from SVN: http://code.google.com/p/libcatid/source/checkout


# Microsoft Windows #

LibCat requires Windows XP SP1 or newer.


# Microsoft Visual Studio 2005/2008 #

Solution files have been provided for MSVC, so re-building is easy.

To statically link with some part of LibCat:
  * I recommend copying the LibCat code into your development folder, to make updating it easy.
  * Specify the path to /lib/cat/ in "Additional Library Directories" in your project linker settings.
  * Specify the name of the LibCat static library to link to in "Additional Dependencies."
  * Under C/C++ "General", specify the path to LibCat's /include/ folder in "Additional Include Directories."
  * In your source, #include <cat/AllTunnel.hpp> or whatever part you want to include.
  * You may want to examine some of the test projects to get ideas for how to use the code.

See "Build MSVC.txt" for more information.


# Other Compilers (Eclipse, etc) #
  * Download and install CMAKE version 2.6 or newer.
  * Run CMAKE on the /build/ directory to create a makefile for the compiler of your choice.
  * Run the makefile in your target compiler to create static libraries that your project can link to.
  * If it doesn't compile please Email me (mrcatid@gmail.com) and I will fix it for you.

# Qt Creator in particular #
  * Download and install CMAKE version 2.6 or newer.
  * You may need to specify the location of CMake before doing this, in the Qt Creator options.
  * Open the /build/CMakeLists.txt as a project file in Qt Creator.
  * During the import, it will prompt you for parameters to pass to CMake.  In Windows I have found it necessary sometimes to pass in this line:

-D CMAKE\_MAKE\_PROGRAM="C:/Qt/2009.04/mingw/bin/mingw32-make.exe"


# Linux #

LibCat uses the 'vmstat -s' command to collect entropy for its random number generator.

You can use CMAKE to generate a Makefile for your platform.  Simply invoke "cmake" from within the ./build directory.

After building, the LibCat static libraries should be present in the ./lib/cat/ directory.

You can now link to these static libraries in your own application.