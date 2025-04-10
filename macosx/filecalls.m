/* filecalls.c posix implimentatation of the abstracted interface to accesing host
   OS file and Directory functions.
   Copyright (c) 2005 Peter Howkins, covered under the GNU GPL see file COPYING for more
   details */

#include <Foundation/Foundation.h>

/* application includes */
#define FileInfo ArcEm_FileInfo
#include "filecalls.h"
#undef FileInfo

/**
 * Open the specified file in the application data directory
 *
 * @param sName Name of file to open
 * @param sMode Mode to open the file with
 * @returns File handle or `NULL` on failure
 */
FILE *File_OpenAppData(const char *sName, const char *sMode)
{
    @autoreleasepool {
        NSString *nsName = [NSString stringWithUTF8String:sName];
        NSArray *array;
        FILE *f;

        array = NSSearchPathForDirectoriesInDomains(NSApplicationSupportDirectory, NSUserDomainMask, YES);
        if ([array count] == 0)
            return NULL;

        for (id object in array) {
            NSString *str = object;
            NSString *dataPath = [str stringByAppendingPathComponent:@"/arcem"];
            NSString *filePath = [dataPath stringByAppendingPathComponent:nsName];

            [[NSFileManager defaultManager] createDirectoryAtPath:dataPath
                                    withIntermediateDirectories:YES
                                    attributes:nil
                                    error:nil];

            f = fopen([filePath fileSystemRepresentation], sMode);
            if (f)
                return f;
        }

        return NULL;
    }
}

/**
 * Directory_OpenAppDir
 *
 * Open the specified directory in the application directory
 *
 * @param sName of directory to scan
 * @param hDir Pointer to a Directory struct to fill in
 * @returns true on success false on failure
 */
bool Directory_OpenAppDir(const char *sName, Directory *hDirectory)
{
    @autoreleasepool {
        NSString *dirName = [NSString stringWithUTF8String:sName];
        NSString *resourcePath = [[NSBundle mainBundle] resourcePath];
        NSString *dirPath = [resourcePath stringByAppendingPathComponent:dirName];

        return Directory_Open([dirPath fileSystemRepresentation], hDirectory);
    }
}
