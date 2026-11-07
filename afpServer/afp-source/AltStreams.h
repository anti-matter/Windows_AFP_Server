//
// This header contains definitions from DDK
//
// ADS-related definitions
//


typedef INT NTSTATUS;

// The only return code we check for
#define STATUS_BUFFER_OVERFLOW        ((NTSTATUS)0x80000005L)


// Must match the NT kernel layout exactly: the kernel always writes a
// union NTSTATUS/PVOID followed by a pointer-sized Information value -
// 16 bytes on x64. The old 8-byte definition let NtQueryInformationFile
// write past the end of the stack variable on every call (caught in
// debug builds as Run-Time Check Failure #2 around 'ioStatus').
typedef struct _IO_STATUS_BLOCK {
  union {
    NTSTATUS Status;
    PVOID Pointer;
  };
  ULONG_PTR Information;
} IO_STATUS_BLOCK, *PIO_STATUS_BLOCK;


typedef enum _FILE_INFORMATION_CLASS {
  FileDirectoryInformation = 1,     // 1
  FileFullDirectoryInformation,     // 2
  FileBothDirectoryInformation,     // 3
  FileBasicInformation,             // 4
  FileStandardInformation,          // 5
  FileInternalInformation,          // 6
  FileEaInformation,                // 7
  FileAccessInformation,            // 8
  FileNameInformation,              // 9
  FileRenameInformation,            // 10
  FileLinkInformation,              // 11
  FileNamesInformation,             // 12
  FileDispositionInformation,       // 13
  FilePositionInformation,          // 14
  FileModeInformation = 16,         // 16
  FileAlignmentInformation,         // 17
  FileAllInformation,               // 18
  FileAllocationInformation,        // 19
  FileEndOfFileInformation,         // 20
  FileAlternateNameInformation,     // 21
  FileStreamInformation,            // 22
  FilePipeInformation,              // 23
  FilePipeLocalInformation,         // 24
  FilePipeRemoteInformation,        // 25
  FileMailslotQueryInformation,     // 26
  FileMailslotSetInformation,       // 27
  FileCompressionInformation,       // 28
  FileObjectIdInformation,          // 29
  FileCompletionInformation,        // 30
  FileMoveClusterInformation,       // 31
  FileQuotaInformation,             // 32
  FileReparsePointInformation,      // 33
  FileNetworkOpenInformation,       // 34
  FileAttributeTagInformation,      // 35
  FileTrackingInformation           // 36
} FILE_INFORMATION_CLASS, *PFILE_INFORMATION_CLASS;


#pragma pack(push, 4)

typedef struct _FILE_STREAM_INFORMATION { // Information Class 22
  ULONG NextEntryOffset;
  ULONG StreamNameLength;
  LARGE_INTEGER EndOfStream;
  LARGE_INTEGER AllocationSize;
  WCHAR StreamName[1];
} FILE_STREAM_INFORMATION, *PFILE_STREAM_INFORMATION;

#pragma pack(pop)


// Define pointer to function type for dynamic loading
typedef NTSTATUS (NTAPI *NTQUERYINFORMATIONFILE)( 
  IN HANDLE FileHandle,
  OUT PIO_STATUS_BLOCK IoStatusBlock,
  OUT PVOID FileInformation,
  IN ULONG Length,
  IN FILE_INFORMATION_CLASS FileInformationClass);
