#pragma once

//
//File flags.
//
#define kShortPathType		0
#define kLongPathType		1
#define kPathDelimeter		':'
#define kShortNames			1
#define kLongNames			2
#define kUnicodeNames		3
#define kRootDirID			2
#define kParentOfRoot		1
#define kSoftCreate			0x1
#define kHardCreate			0x2
#define kReadMode			0x0001
#define kWriteMode			0x0002
#define kDenyRead			0x10
#define kDenyWrite			0x20
#define kReadWriteMode		0x03
#define kResourceForkBit	0x80
#define kDataFork			0x00
#define kRsrcFork			0x01
#define kAttrFork			0xFF	//Special for NTFS attributes access
#define kWriteStartEndFlag	0x01
#define kFromStart			0
#define kFromEnd			1


// ReSharper disable CppInconsistentNaming
enum AfpCommand
{
    afpByteRangeLock = 1,
    afpVolClose = 2,
    afpDirClose = 3,
    afpForkClose = 4,
    afpCopyFile = 5,
    afpDirCreate = 6,
    afpFileCreate = 7,
    afpDelete = 8,
    afpEnumerate = 9,
    afpFlush = 10,
    afpForkFlush = 11,
    afpGetDirParms = 12,
    afpGetFileParms = 13,
    afpGetForkParms = 14,
    afpGetSInfo = 15,
    afpGetSParms = 16,
    afpGetVolParms = 17,
    afpLogin = 18,
    afpContLogin = 19,
    afpLogout = 20,
    afpMapID = 21,
    afpMapName = 22,
    afpMove = 23,
    afpOpenVol = 24,
    afpOpenDir = 25,
    afpOpenFork = 26,
    afpRead = 27,
    afpRename = 28,
    afpSetDirParms = 29,
    afpSetFileParms = 30,
    afpSetForkParms = 31,
    afpSetVolParms = 32,
    afpWrite = 33,
    afpGetFlDrParms = 34,
    afpSetFlDrParms = 35,
    afpChangePwd = 36,
    afpGetUserInfo = 37,
    afpGetSrvrMsg = 38,
    afpCreateID = 39,
    afpDeleteID = 40,
    afpResolveID = 41,
    afpExchangeFiles = 42,
    afpDTOpen = 48,
    afpDTClose = 49,
    afpGetIcon = 51,
    afpGtIcnInfo = 52,
    afpAddAPPL = 53,
    afpRmvAPPL = 54,
    afpGetAPPL = 55,
    afpAddCmt = 56,
    afpRmvCmt = 57,
    afpGetCmt = 58,
    afpAddIcon = 192,

    //AFP3.0
    afpByteRangeLockExt = 59,
    afpReadExt = 60,
    afpWriteExt = 61,
    afpGetAuthMethods = 62,
    afpLoginExt = 63,
    afpGetSessionToken = 64,
    afpDisconnectOldSession = 65,
    afpEnumerateExt = 66,
    afpCatSearchExt = 67,
    afpEnumerateExt2 = 68,

    //AFP3.2
    afpGetExtAttr = 69,
    afpSetExtAttr = 70,
    afpRemoveExtAttr = 71,
    afpListExtAttr = 72,

    afpZzzz = 122
};

enum AfpErrorCode
{
    afpAccessDenied = -5000,
    afpAuthContinue = -5001,
    afpBadUAM = -5002,
    afpBadVersNum = -5003,
    afpBitmapErr = -5004,
    afpCantMove = -5005,
    afpDenyConflict = -5006,
    afpDirNotEmpty = -5007,
    afpDiskFull = -5008,
    afpEofError = -5009,
    afpFileBusy = -5010,
    afpFlatVol = -5011,
    afpItemNotFound = -5012,
    afpLockErr = -5013,
    afpMiscErr = -5014,
    afpNoMoreLocks = -5015,
    afpNoServer = -5016,
    afpObjectExists = -5017,
    afpObjectNotFound = -5018,
    afpParmErr = -5019,
    afpRangeNotLocked = -5020,
    afpRangeOverlap = -5021,
    afpSessClosed = -5022,
    afpUserNotAuth = -5023,
    afpCallNotSupported = -5024,
    afpObjectTypeErr = -5025,
    afpTooManyFilesOpen = -5026,
    afpServerGoingDown = -5027,
    afpCantRename = -5028,
    afpDirNotFound = -5029,
    afpIconTypeError = -5030,
    afpVolLocked = -5031,
    afpObjectLocked = -5032,
    afpContainsSharedErr = -5033,
    afpIDNotFound = -5034,
    afpIDExists = -5035,
    afpDiffVolErr = -5036,
    afpCatalogChanged = -5037,
    afpSameObjectErr = -5038,
    afpBadIDErr = -5039,
    afpPwdSameErr = -5040,
    afpPwdTooShortErr = -5041,
    afpPwdExpiredErr = -5042,
    afpInsideSharedErr = -5043,
    afpInsideTrashErr = -5044,
    afpBadDirIDType = -5060,
    afpCantMountMoreSrvre = -5061,
    afpAlreadyMounted = -5062,
    afpSameNodeErr = -5063
};
// ReSharper restore CppInconsistentNaming

#define AFP_VERSION_30

using AFPERROR = LONG;

#define AFP_RESOURCE_FORK_NAME			TEXT(":Afp_AfpResource")
#define AFP_RESOURCE_FORK_NAME_PLAIN	TEXT("Afp_AfpResource")

#ifdef AFP_VERSION_30
#define AFP_VERSION_COUNT	4
#else
#define AFP_VERSION_COUNT	1
#endif

#define AFP_22_VERSION_STR	TEXT("AFP2.2")
#define AFP_30_VERSION_STR	TEXT("AFPX03")
#define AFP_31_VERSION_STR	TEXT("AFP3.1")
#define AFP_32_VERSION_STR	TEXT("AFP3.2")

#define AFP_MAX_DWORD			MAXDWORD
#define AFP_MAX_LONG			MAXLONG
#define AFP_MAX_WORD			MAXWORD

//AFP version constants
enum
{
    afpVersion22 = 1,
    afpVersion30,
    afpVersion31,
    afpVersion32
};

enum
{
    afpUAMNone = 0,
    afpUAMCleartext = 1,
    afpUAMDHCAST128 = 2
};

#define UAM_NONE_STR			TEXT("No User Authent")
#define UAM_CLEAR_TEXT			TEXT("Cleartxt passwrd")
#define UAM_DHCAST128			TEXT("DHCAST128")
#define UAM_COUNT				3
#define AFP_MACHINE_TYPE		TEXT("Windows")
#define AFP_COMMENT_MAX			199
#define MAX_UAM_STRING_LEN		25
#define MAX_AFP_VERSION_LEN		15

//
//Attention codes
//
#define ATTN_SERVER_SHUTDOWN        0x8000
#define ATTN_USER_DISCONNECT        0x9000
#define ATTN_SERVER_MESSAGE         0x2000
#define ATTN_SERVER_NOTIFY          0x3001
#define ATTN_TIME_MASK              0x0FFF

//
//Enumerate offsets
//
#define ENUM_FILE_BITMAP_OFFSET	0
#define ENUM_DIR_BITMAP_OFFSET	2
#define ENUM_NUM_ENTRIES		4
#define ENUM_BEGIN_LIST			6

#define MAX_AFP20_NAME							32
#define MAX_AFP20_PATH							256
#define MAX_AFP30_NAME							256
#define MAX_AFP30_PATH							MAX_PATH
#define MAX_AFP_SHORT_NAME						12

#ifdef AFP_VERSION_30
#define MAX_AFP_NAME							MAX_AFP30_NAME
#define MAX_DIR_NAME							MAX_AFP30_NAME
#define MAX_FILE_NAME							MAX_AFP30_NAME
#define MAX_AFP_PATH							MAX_AFP30_PATH
#else
#define MAX_AFP_NAME							MAX_AFP20_NAME
#define MAX_DIR_NAME							MAX_AFP20_NAME
#define MAX_FILE_NAME							MAX_AFP20_NAME
#define MAX_AFP_PATH							MAX_AFP20_PATH
#endif //AFP_VERSION_30

#define UAM_USERNAMELEN							33
#define UAM_CLRTXTPWDLEN						8
#define MAX_DHXPASSWORDLEN						64
#define AFP_ICONSIZE							128
#define MAX_VOLNAME_LEN							27
#define FINDERINFO_LEN							32
#define PRODOS_LEN								6
#define APPLETALK_NAME_LEN						31
#define AFP_DIR_NEVER_BACKEDUP					0x80000000
#define AFP_GUEST_USER_ID						0
#define AFP_OWNER_USER_ID						1
#define MAX_DOMAIN_NAME_LEN						32


//
//Mac's time is based from January 1, 1904 while a server's is based on the
//year 2000. Below is the difference between the two for conversion and a
//macro to make life easier.
//
#define NUM_SECS_FROM_1904_TO_2000 	3029529600
#define LOCAL_TIME(serverTime)		(NUM_SECS_FROM_1904_TO_2000 + serverTime)

#define FIRST_VALID_ANSI_CHAR	32
#define LAST_VALID_ANSI_CHAR	255

//
//Yes, we succeeded and are OK.
//
#define AFP_OK					0
#define AFP_SUCCESS(e)			((e) == AFP_OK)
#define AFP_FAILED(e)           ((e) != AFP_OK)

//
//This user sturcture is kept by the DSI session. AFP
//uses it to keep track of open volumes and files on
//a per-session basis.
//
#define MAX_AFP_FILES_OPEN		24
#define MAX_AFP_OPEN_DIRS		24
#define MAX_AFP_OPEN_VOLUMES	16


//
//This structure keeps track of file locks. Locks are tracked on a per
//session basis and a session who has a fork open more than once
//can conflict with its own locks as per spec.
//
//typedef struct
//{
//    DWORD			dwLockStart;
//    DWORD			dwLockLength;
//
//    LPSESSION		pSession;			//Session that locked the range
//    PVOID			nextLockEntry;		//Ptr to next lock entry in list
//}BRL_LOCK_ENTRY, *LPBRL_LOCK_ENTRY;

//
//AFP worker thread parameter structure and enums
//
typedef struct
{
    PVOID pParam1;
    PVOID pParam2;
    DWORD dwParam;
    DWORD dwWorkerFunction;
    PVOID next;
} AFP_WORKER_ELEMENT, *PAFP_WORKER_ELEMENT;

enum
{
    afp_worker_delete_index_entry = 1,
    afp_worker_index_directory = 2,
    afp_worker_index_complete = 3
};

using afp_clear_text_password = unsigned char[8];

// ReSharper disable CppInconsistentNaming
enum
{
    KFPMsgSrvrBit = 0x1
};

enum afp_server_messaging
{
    message_max_length = 199,
    message_login_type = 0,
    message_server_type = 1
};

enum afp_get_server_message_flags
{
    kSendMessageAsUnicode = 0x2
};

enum afp_volume_bitmap_flags
{
    kFPVolAttributeBit = 0x1,
    kFPVolSignatureBit = 0x2,
    kFPVolCreateDateBit = 0x4,
    kFPVolModDateBit = 0x8,
    kFPVolBackupDateBit = 0x10,
    kFPVolIDBit = 0x20,
    kFPVolBytesFreeBit = 0x40,
    kFPVolBytesTotalBit = 0x80,
    kFPVolNameBit = 0x100,
    kFPVolExtBytesFree = 0x200,
    kFPVolExtBytesTotal = 0x400,
    kFPVolBlockSize = 0x800
};

enum afp_volume_types
{
    kFlatVolumeType = 1,
    kFixedDirectoryID = 2,
    kVariableDirectoryID = 3 //Deprecated
};

enum afp_volume_attribute_flags
{
    kFPVolReadOnly = 0x01,
    kFPVolHasVolumePassword = 0x02,
    kFPVolSupportsFileIDs = 0x04,
    kFPVolSupportsCatSearch = 0x08,
    kFPVolSupportsBlankAccessPrivileges = 0x10,
    kFPVolSupportsUnixPrivs = 0x20,
    kFPVolSupportsUnicodeNames = 0x40,
    kFPVolNoNetworkUserIDs = 0x80,
    kDefaultPrivsFromParent = 0x100,
    kNoExchangeFiles = 0x200,
    kSupportsExtAttrs = 0x400,
    kSupportsACLs = 0x800
};

enum
{
    FileDirParmsStart = 6
};

#define FILEPARMS_ALL		(kFPFileAttributes | kFPParentID | kFPCreateDate |		\
                            kFPModDate | kFPBackupDate | kFPFinderInfo | 			\
                            kFPLongName | kFPShortName | kFPFileNum | kFPDFLen |	\
                            kFPRFLen | kFPProDos)

#define DIRPARMS_ALL		(kFPDirAttribute | kFPDirParentID | kFPDirCreateDate |	\
                            kFPDirModDate | kFPDirBackupDate | kFPDirFinderInfo	|	\
                            kFPDirLongName | kFPDirShortName | kFPDirID	|			\
                            kFPDirOffCount | kFPDirOwnerID | kFPDirGroupID |		\
                            kFPDirAccess | kFPDirProDOS)

#define kAccess_OALL		(kAccess_OS  | kAccess_OR  | kAccess_OW )
#define kAccess_GALL		(kAccess_GS  | kAccess_GR  | kAccess_GW )
#define kAccess_WALL		(kAccess_WS  | kAccess_WR  | kAccess_WW )
#define kAccess_UALL		(kAccess_UAS | kAccess_UAR | kAccess_UAW)

enum afp_access_rights : unsigned int
{
    kAccess_OS = 0x00000001,
    kAccess_OR = 0x00000002,
    kAccess_OW = 0x00000004,
    kAccess_GS = 0x00000100,
    kAccess_GR = 0x00000200,
    kAccess_GW = 0x00000400,
    kAccess_WS = 0x00010000,
    kAccess_WR = 0x00020000,
    kAccess_WW = 0x00040000,
    kAccess_UAS = 0x01000000,
    kAccess_UAR = 0x02000000,
    kAccess_UAW = 0x04000000,
    kAccess_Owner = 0x80000000
};

enum afp_file_bitmaps
{
    kFPFileNone = 0x0000,
    kFPFileAttributes = 0x0001,
    kFPParentID = 0x0002,
    kFPCreateDate = 0x0004,
    kFPModDate = 0x0008,
    kFPBackupDate = 0x0010,
    kFPFinderInfo = 0x0020,
    kFPLongName = 0x0040,
    kFPShortName = 0x0080,
    kFPFileNum = 0x0100,
    kFPDFLen = 0x0200,
    kFPRFLen = 0x0400,
    kFPProDos = 0x2000,

    //AFP3.0
    kFPExtDataForkLen = 0x800,
    kFPLaunchLimit = 0x1000,
    kFPUnicodeName = 0x2000,
    kFPExtRsrcForkLen = 0x4000,
    kFPUnixPrivs = 0x8000
};

enum afp_directory_bitmaps
{
    kFPDirNone = 0x0000,
    kFPDirAttribute = 0x0001,
    kFPDirParentID = 0x0002,
    kFPDirCreateDate = 0x0004,
    kFPDirModDate = 0x0008,
    kFPDirBackupDate = 0x0010,
    kFPDirFinderInfo = 0x0020,
    kFPDirLongName = 0x0040,
    kFPDirShortName = 0x0080,
    kFPDirID = 0x0100,
    kFPDirOffCount = 0x0200,
    kFPDirOwnerID = 0x0400,
    kFPDirGroupID = 0x0800,
    kFPDirAccess = 0x1000,
    kFPDirProDOS = 0x2000,

    //AFP3.0
    kFPDirUnicodeName = 0x2000,
    kFPDirUnixPrivs = 0x8000
};

enum afp_file_attribute_flags
{
    kFileInvisible = 0x00001,
    kFileMultiUser = 0x00002,
    kFileSystem = 0x00004,
    kFileDataForkOpen = 0x00008,
    kFileRsrcForkOpen = 0x00010,
    kFileReadOnly = 0x00020,
    kFileNeedsBackup = 0x00040,
    kFileRenameInhibit = 0x00080,
    kFileDeleteInhibit = 0x00100,
    kFileCopyProtected = 0x00200,

    kFileSetClearFlag = 0x08000
};

enum afp_directory_attribute_flags
{
    kDirInvisible = 0x00001,
    kDirIsExpFolder = 0x00002,
    kDirSystem = 0x00004,
    kDirMounted = 0x00008,
    kDirInExpFolder = 0x00010,
    kDirBackupNeeded = 0x00040,
    kDirRenameInhibit = 0x00080,
    kDirDeleteInhibit = 0x00100,
    kDirSetClearFlag = 0x04000,

    kFileDirIsDir = 0x80
};

#pragma pack(push,1)

// How many replacement chars are saved when file names have
// Mac specific extended chars in them.
#define MAX_REPLACED_CHARACTERS		MAX_DIR_NAME

// This is a table that will map invalid Mac characters to a known
// good windows character. This will then be placed back when
// data is sent back to the Mac.
using AFPCHARMAP = struct _AFPCHARMAP
{
    WORD wKeyCode; //Keycode of offending character
    WORD wOffset; //Offset in string where char existed
};

using AFPCHARMAPARRAY = AFPCHARMAP[MAX_REPLACED_CHARACTERS];

struct mac_point
{
    SHORT x;
    SHORT y;
};

struct afp_finder_info
{
    afp_finder_info()
        : fd_type{}
          , fd_creator{}
          , fd_flags(0)
          , fd_location{}
          , fd_fldr(0)
          , fd_finder_data{}
    {
    }

    CHAR fd_type[4];
    CHAR fd_creator[4];
    WORD fd_flags;
    mac_point fd_location;
    SHORT fd_fldr;
    CHAR fd_finder_data[16];
};

constexpr BYTE extended_finder_info_version = 1;

// This is the extended FinderInfo that we use to store
// data per file/dir specific to this file server.
struct afp_extended_finder_info
{
    afp_extended_finder_info()
        : version(extended_finder_info_version)
          , char_map{}
          , reserved(0)
          , reserved2(0)
    {
    }

    afp_finder_info finder_info;

    BYTE version;
    AFPCHARMAPARRAY char_map;

    DWORD reserved;
    DWORD reserved2;
};

#pragma pack(pop)

// ReSharper disable CppInconsistentNaming
enum afp_finder_flags
{
    kFDIsShared = 0x0040,
    kFDHasNoInits = 0x0080,
    kFDHasBeenInited = 0x0100,
    kFDReserved = 0x0200,
    kFDHasCustomIcon = 0x0400,
    kFDIsStationary = 0x0800,
    kFDNameLocked = 0x1000,
    kFDHasBundle = 0x2000,
    kFDInvisible = 0x4000,
    kFDIsAlias = 0x8000
};

enum afp_get_server_info_flags
{
    kSupportsCopyFile = 0x1,
    kSupportsChngPswd = 0x2,
    kDontAllowSavePwd = 0x4,
    kSupportsSrvrMsgs = 0x8,
    kSupportsSrvrSig = 0x10,
    kSupportsTCPIP = 0x20,
    kSupportsSrvrNotification = 0x40,
    kSupportsReconnect = 0x80,
    kSupportsDirServices = 0x100,
    kSupportsUTF8Name = 0x200,
    kSupportsSuperClient = 0x8000
};

enum afp_get_server_info_offsets
{
    offset_machine_type = 0,
    offset_afp_version_count = 2,
    offset_uam_count = 4,
    offset_volume_icon = 6,
    offset_flags = 8,
    offset_server_name = 10
};

enum afp_byte_range_lock_flags
{
    kUnlockFlag = 0x01,
    kStartEndFlag = 0x80,

    kLock = TRUE,
    kUnlock = FALSE,
    kSelFromStart = TRUE,
    kSelFromEnd = FALSE
};

// For reconnecting sessions that have been killed.
#define AFP_SESSION_TOKEN_SIZE sizeof(DWORD)

enum afp_session_token_types
{
    kLoginWithoutID = 0,
    kLoginWithID = 1,
    kReconnWithID = 2,
    kLoginWithTimeAndID = 3,
    kReconnWithTimeAndID = 4,
    kRecon1Login = 5,
    kRecon1ReconnectLogin = 6,
    kRecon1Refresh = 7,
    kGetKerberosSessionKey = 8
};

// ReSharper restore CppInconsistentNaming
