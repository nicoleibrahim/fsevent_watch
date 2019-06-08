#include "common.h"
#include "cli.h"
#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

// TODO: set on fire. cli.{h,c} handle both parsing and defaults, so there's
//       no need to set those here. also, in order to scope metadata by path,
//       each stream will need its own configuration... so this won't work as
//       a global any more. In the end the goal is to make the output format
//       able to declare not just that something happened and what flags were
//       attached, but what path it was watching that caused those events (so
//       that the path itself can be used for routing that information to the
//       relevant callback).
//
// Structure for storing metadata parsed from the commandline
static struct {
  FSEventStreamEventId     sinceWhen;
  CFTimeInterval           latency;
  FSEventStreamCreateFlags flags;
  CFMutableArrayRef        paths;
  long                     format;
} config = {
  (UInt64) kFSEventStreamEventIdSinceNow,
  (double) 0.3,
  (CFOptionFlags) kFSEventStreamCreateFlagNone,
  NULL,
  0.0
};

// Prototypes
static void         append_path(const char* path);
static inline void  parse_cli_settings(int argc, const char* argv[]);
static void         callback(FSEventStreamRef streamRef,
                             void* clientCallBackInfo,
                             size_t numEvents,
                             void* eventPaths,
                             const FSEventStreamEventFlags eventFlags[],
                             const FSEventStreamEventId eventIds[]);


static void append_path(const char* path)
{
  CFStringRef pathRef = CFStringCreateWithCString(kCFAllocatorDefault,
                                                  path,
                                                  kCFStringEncodingUTF8);
  CFArrayAppendValue(config.paths, pathRef);
  CFRelease(pathRef);
}

// Parse commandline settings
static inline void parse_cli_settings(int argc, const char* argv[])
{
  // runtime os version detection
  SInt32 osMajorVersion, osMinorVersion;
  if (!(Gestalt(gestaltSystemVersionMajor, &osMajorVersion) == noErr)) {
    osMajorVersion = 0;
  }
  if (!(Gestalt(gestaltSystemVersionMinor, &osMinorVersion) == noErr)) {
    osMinorVersion = 0;
  }

  if ((osMajorVersion == 10) & (osMinorVersion < 5)) {
    fprintf(stderr, "The FSEvents API is unavailable on this version of macos!\n");
    exit(EXIT_FAILURE);
  }

  struct cli_info args_info;
  cli_parser_init(&args_info);

  if (cli_parser(argc, argv, &args_info) != 0) {
    exit(EXIT_FAILURE);
  }

  config.paths = CFArrayCreateMutable(NULL,
                                      (CFIndex)0,
                                      &kCFTypeArrayCallBacks);

  config.sinceWhen = args_info.since_when_arg;
  config.latency = args_info.latency_arg;
  config.format = args_info.format_arg;

  if (args_info.no_defer_flag) {
    config.flags |= kFSEventStreamCreateFlagNoDefer;
  }
  if (args_info.watch_root_flag) {
    config.flags |= kFSEventStreamCreateFlagWatchRoot;
  }

  if (args_info.ignore_self_flag) {
    if ((osMajorVersion == 10) & (osMinorVersion >= 6)) {
      config.flags |= kFSEventStreamCreateFlagIgnoreSelf;
    } else {
      fprintf(stderr, "MacOSX 10.6 or later is required for --ignore-self\n");
      exit(EXIT_FAILURE);
    }
  }

  if (args_info.file_events_flag) {
    if ((osMajorVersion == 10) & (osMinorVersion >= 7)) {
      config.flags |= kFSEventStreamCreateFlagFileEvents;
    } else {
      fprintf(stderr, "MacOSX 10.7 or later required for --file-events\n");
      exit(EXIT_FAILURE);
    }
  }

  if (args_info.mark_self_flag) {
    if ((osMajorVersion == 10) & (osMinorVersion >= 9)) {
      config.flags |= kFSEventStreamCreateFlagMarkSelf;
    } else {
      fprintf(stderr, "MacOSX 10.9 or later required for --mark-self\n");
      exit(EXIT_FAILURE);
    }
  }

  if (args_info.inputs_num == 0) {
    append_path(".");
  } else {
    for (unsigned int i=0; i < args_info.inputs_num; ++i) {
      append_path(args_info.inputs[i]);
    }
  }

  cli_parser_free(&args_info);

#ifdef DEBUG
  fprintf(stderr, "config.sinceWhen    %llu\n", config.sinceWhen);
  fprintf(stderr, "config.latency      %f\n", config.latency);
  fprintf(stderr, "config.flags        %#.8x\n", config.flags);

  FLAG_CHECK_STDERR(config.flags, kFSEventStreamCreateFlagUseCFTypes,
                    "  Using CF instead of C types");
  FLAG_CHECK_STDERR(config.flags, kFSEventStreamCreateFlagNoDefer,
                    "  NoDefer latency modifier enabled");
  FLAG_CHECK_STDERR(config.flags, kFSEventStreamCreateFlagWatchRoot,
                    "  WatchRoot notifications enabled");
  FLAG_CHECK_STDERR(config.flags, kFSEventStreamCreateFlagIgnoreSelf,
                    "  IgnoreSelf enabled");
  FLAG_CHECK_STDERR(config.flags, kFSEventStreamCreateFlagFileEvents,
                    "  FileEvents enabled");

  fprintf(stderr, "config.paths\n");

  long numpaths = CFArrayGetCount(config.paths);

  for (long i = 0; i < numpaths; i++) {
    char path[PATH_MAX];
    CFStringGetCString(CFArrayGetValueAtIndex(config.paths, i),
                       path,
                       PATH_MAX,
                       kCFStringEncodingUTF8);
    fprintf(stderr, "  %s\n", path);
  }

  fprintf(stderr, "\n");
#endif
}

static void callback(__attribute__((unused)) FSEventStreamRef streamRef,
                     __attribute__((unused)) void* clientCallBackInfo,
                     size_t numEvents,
                     void* eventPaths,
                     const FSEventStreamEventFlags eventFlags[],
                     const FSEventStreamEventId eventIds[])
{
  char** paths = eventPaths;
  char *buf = calloc(sizeof(FSEVENTSBITS), sizeof(char));
  
  for (size_t i = 0; i < numEvents; i++) {
	time_t curtime;
    struct tm *loc_time;
	char buffer[32];
    long fd;
    long inode;  
    fd = open(paths[i], O_RDONLY);
    inode = 1;
    
    if (fd < 0) {  
        // some error occurred while opening the file  
        fd = 0;
        inode = 0;
    }  

    struct stat file_stat;
    struct stat file_stat_l;  
    long ret;  
    ret = fstat (fd, &file_stat);  
    if (ret < 0) {  
       //printf("print here");
    } 
    if (inode == 1) {
        lstat(paths[i], &file_stat_l);
        inode = file_stat_l.st_ino;  
    }

    //Getting current time of system
    curtime = time (NULL);
    
    // Converting current time to local time
    loc_time = localtime (&curtime);
    strftime(buffer, 32, "%Y-%m-%d %H:%M:%S", loc_time);

    sprintb(buf, eventFlags[i], FSEVENTSBITS);
    
    // fs_node not converting properly. 
    printf("%s\t%llu\t%ld\t%#.8x\t%s\t%s\n", buffer, eventIds[i], inode, eventFlags[i], buf, paths[i]);
  }
  fflush(stdout);
  free(buf);

  if (fcntl(STDIN_FILENO, F_GETFD) == -1) {
    CFRunLoopStop(CFRunLoopGetCurrent());
  }
}

static void stdin_callback(CFFileDescriptorRef fdref, CFOptionFlags callBackTypes, void *info)
{
  char buf[1024];
  long nread;

  do {
    nread = read(STDIN_FILENO, buf, sizeof(buf));
    if (nread == -1 && errno == EAGAIN) {
      CFFileDescriptorEnableCallBacks(fdref, kCFFileDescriptorReadCallBack);
      return;
    } else if (nread == 0) {
      exit(1);
      return;
    }
  } while (nread > 0);
}

int main(int argc, const char* argv[])
{
  parse_cli_settings(argc, argv);
  printf("Current_Timestamp\tEvent_ID\tInode\tFlags_Hex\tFlags_String\tPath\n");
  FSEventStreamContext context = {0, NULL, NULL, NULL, NULL};
  FSEventStreamRef stream;
  stream = FSEventStreamCreate(kCFAllocatorDefault,
                               (FSEventStreamCallback)&callback,
                               &context,
                               config.paths,
                               config.sinceWhen,
                               config.latency,
                               config.flags);

#ifdef DEBUG
  FSEventStreamShow(stream);
  fprintf(stderr, "\n");
#endif

  fcntl(STDIN_FILENO, F_SETFL, O_NONBLOCK);

  CFFileDescriptorRef fdref = CFFileDescriptorCreate(kCFAllocatorDefault, STDIN_FILENO, false, stdin_callback, NULL);
  CFFileDescriptorEnableCallBacks(fdref, kCFFileDescriptorReadCallBack);
  CFRunLoopSourceRef source = CFFileDescriptorCreateRunLoopSource(kCFAllocatorDefault, fdref, 0);
  CFRunLoopAddSource(CFRunLoopGetCurrent(), source, kCFRunLoopDefaultMode);
  CFRelease(source);

  FSEventStreamScheduleWithRunLoop(stream,
                                   CFRunLoopGetCurrent(),
                                   kCFRunLoopDefaultMode);
  FSEventStreamStart(stream);
  CFRunLoopRun();
  FSEventStreamFlushSync(stream);
  FSEventStreamStop(stream);

  return 0;
}

// vim: ts=2 sts=2 et sw=2
