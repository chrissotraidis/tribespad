/* Microsoft WDK field declarations, independently compiled for the guest's
   i686 MSVC ABI. No host Apple packing assumptions or Windows SDK dependency.
   Sources: FILE_ALL_INFORMATION and FILE_FS_VOLUME_INFORMATION on learn.microsoft.com. */
typedef unsigned long ULONG;
typedef unsigned char BOOLEAN;
typedef unsigned short WCHAR;
typedef long long LARGE_INTEGER;
typedef struct {LARGE_INTEGER CreationTime,LastAccessTime,LastWriteTime,ChangeTime;ULONG FileAttributes;} FILE_BASIC_INFORMATION;
typedef struct {LARGE_INTEGER AllocationSize,EndOfFile;ULONG NumberOfLinks;BOOLEAN DeletePending,Directory;} FILE_STANDARD_INFORMATION;
typedef struct {LARGE_INTEGER IndexNumber;} FILE_INTERNAL_INFORMATION;
typedef struct {ULONG EaSize;} FILE_EA_INFORMATION;
typedef struct {ULONG AccessFlags;} FILE_ACCESS_INFORMATION;
typedef struct {LARGE_INTEGER CurrentByteOffset;} FILE_POSITION_INFORMATION;
typedef struct {ULONG Mode;} FILE_MODE_INFORMATION;
typedef struct {ULONG AlignmentRequirement;} FILE_ALIGNMENT_INFORMATION;
typedef struct {ULONG FileNameLength;WCHAR FileName[1];} FILE_NAME_INFORMATION;
typedef struct {
 FILE_BASIC_INFORMATION BasicInformation;
 FILE_STANDARD_INFORMATION StandardInformation;
 FILE_INTERNAL_INFORMATION InternalInformation;
 FILE_EA_INFORMATION EaInformation;
 FILE_ACCESS_INFORMATION AccessInformation;
 FILE_POSITION_INFORMATION PositionInformation;
 FILE_MODE_INFORMATION ModeInformation;
 FILE_ALIGNMENT_INFORMATION AlignmentInformation;
 FILE_NAME_INFORMATION NameInformation;
} FILE_ALL_INFORMATION;
typedef struct {LARGE_INTEGER VolumeCreationTime;ULONG VolumeSerialNumber,VolumeLabelLength;BOOLEAN SupportsObjects;WCHAR VolumeLabel[1];} FILE_FS_VOLUME_INFORMATION;
#define CHECK(t,m,n) _Static_assert(__builtin_offsetof(t,m)==n,#t "." #m)
_Static_assert(sizeof(void *)==4,"guest pointer width");
_Static_assert(sizeof(FILE_ALL_INFORMATION)==104,"all size");
_Static_assert(sizeof(FILE_FS_VOLUME_INFORMATION)==24,"volume size");
CHECK(FILE_ALL_INFORMATION,StandardInformation,40);
CHECK(FILE_ALL_INFORMATION,InternalInformation,64);
CHECK(FILE_ALL_INFORMATION,EaInformation,72);
CHECK(FILE_ALL_INFORMATION,AccessInformation,76);
CHECK(FILE_ALL_INFORMATION,PositionInformation,80);
CHECK(FILE_ALL_INFORMATION,ModeInformation,88);
CHECK(FILE_ALL_INFORMATION,AlignmentInformation,92);
CHECK(FILE_ALL_INFORMATION,NameInformation.FileNameLength,96);
CHECK(FILE_ALL_INFORMATION,NameInformation.FileName,100);
CHECK(FILE_FS_VOLUME_INFORMATION,VolumeSerialNumber,8);
CHECK(FILE_FS_VOLUME_INFORMATION,VolumeLabelLength,12);
CHECK(FILE_FS_VOLUME_INFORMATION,SupportsObjects,16);
CHECK(FILE_FS_VOLUME_INFORMATION,VolumeLabel,18);
