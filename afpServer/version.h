// ReSharper disable IdentifierTypo
// ReSharper disable CppClangTidyClangDiagnosticUnusedMacros
// ReSharper disable CppClangTidyModernizeMacroToEnum
// ReSharper disable CppClangTidyClangDiagnosticMacroRedefined
#pragma once

#define STRINGIZE2(s) #s
#define STRINGIZE(s) STRINGIZE2(s)

#define VERSION_MAJOR               2
#define VERSION_MINOR               1
#define VERSION_REVISION            0
#define VERSION_BUILD               0

#define VER_FILE_DESCRIPTION_STR    "Apple Filing Protocol Server for Windows."
#define VER_FILE_VERSION            VERSION_MAJOR, VERSION_MINOR, VERSION_REVISION, VERSION_BUILD
#define VER_FILE_VERSION_STR        STRINGIZE(VERSION_MAJOR)        \
                                    "." STRINGIZE(VERSION_MINOR)    \
                                    "." STRINGIZE(VERSION_REVISION) \
                                    "." STRINGIZE(VERSION_BUILD)    \

#define VER_FILE_SHORT_VERSION      VERSION_MAJOR, VERSION_MINOR, VERSION_REVISION
#define VER_FILE_SHORT_VERSION_STR  STRINGIZE(VERSION_MAJOR)        \
                                    "." STRINGIZE(VERSION_MINOR)    \
                                    "." STRINGIZE(VERSION_REVISION) \

#define VER_PRODUCTNAME_STR         "AfpServer"
#define VER_PRODUCT_VERSION         VER_FILE_VERSION
#define VER_PRODUCT_VERSION_STR     VER_FILE_VERSION_STR
#define VER_ORIGINAL_FILENAME_STR   VER_PRODUCTNAME_STR ".exe"
#define VER_INTERNAL_NAME_STR       VER_ORIGINAL_FILENAME_STR
#define VER_COPYRIGHT_STR           "(C) 2023, Michael J. Conrad."

#define VER_FILEOS                  VOS_NT_WINDOWS32
#define VER_FILEFLAGS               VER_VER_DEBUG
#define VER_FILETYPE                VFT_APP
