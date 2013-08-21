#include "common.h"
#include "cli.h"

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
  int                      format;
} config = {
  (UInt64) kFSEventStreamEventIdSinceNow,
  (double) 0.3,
  (CFOptionFlags) kFSEventStreamCreateFlagNone,
  NULL,
  0
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


// Resolve a path and append it to the CLI settings structure
// The FSEvents API will, internally, resolve paths using a similar scheme.
// Performing this ahead of time makes things less confusing, IMHO.
static void append_path(const char* path)
{
#ifdef DEBUG
  fprintf(stderr, "\n");
  fprintf(stderr, "append_path called for: %s\n", path);
#endif

#if MAC_OS_X_VERSION_MIN_REQUIRED >= 1060

#ifdef DEBUG
  fprintf(stderr, "compiled against 10.6+, using CFURLCreateFileReferenceURL\n");
#endif

  CFURLRef url = CFURLCreateFromFileSystemRepresentation(NULL, (const UInt8*)path, (CFIndex)strlen(path), false);
  CFURLRef placeholder = CFURLCopyAbsoluteURL(url);
  CFRelease(url);

  CFMutableArrayRef imaginary = NULL;

  // if we don't have an existing url, spin until we get to a parent that
  // does exist, saving any imaginary components for appending back later
  while(!CFURLResourceIsReachable(placeholder, NULL)) {
#ifdef DEBUG
    fprintf(stderr, "path does not exist\n");
#endif

    CFStringRef child;

    if (imaginary == NULL) {
      imaginary = CFArrayCreateMutable(NULL, 0, &kCFTypeArrayCallBacks);
    }

    child = CFURLCopyLastPathComponent(placeholder);
    CFArrayInsertValueAtIndex(imaginary, 0, child);
    CFRelease(child);

    url = CFURLCreateCopyDeletingLastPathComponent(NULL, placeholder);
    CFRelease(placeholder);
    placeholder = url;

#ifdef DEBUG
    fprintf(stderr, "parent: ");
    CFShow(placeholder);
#endif
  }

#ifdef DEBUG
  fprintf(stderr, "path exists\n");
#endif

  // realpath() doesn't always return the correct case for a path, so this
  // is a funky workaround that converts a path into a (volId/inodeId) pair
  // and asks what the path should be for that. since it looks at the actual
  // inode instead of returning the same case passed in like realpath()
  // appears to do for HFS+, it should always be correct.
  url = CFURLCreateFileReferenceURL(NULL, placeholder, NULL);
  CFRelease(placeholder);
  placeholder = CFURLCreateFilePathURL(NULL, url, NULL);
  CFRelease(url);

#ifdef DEBUG
  fprintf(stderr, "path resolved to: ");
  CFShow(placeholder);
#endif

  // if we stripped off any imaginary path components, append them back on
  if (imaginary != NULL) {
    CFIndex count = CFArrayGetCount(imaginary);
    for (CFIndex i = 0; i<count; i++) {
      CFStringRef component = CFArrayGetValueAtIndex(imaginary, i);
#ifdef DEBUG
      fprintf(stderr, "appending component: ");
      CFShow(component);
#endif
      url = CFURLCreateCopyAppendingPathComponent(NULL, placeholder, component, false);
      CFRelease(placeholder);
      placeholder = url;
    }
    CFRelease(imaginary);
  }

#ifdef DEBUG
  fprintf(stderr, "result: ");
  CFShow(placeholder);
#endif

  CFStringRef cfPath = CFURLCopyFileSystemPath(placeholder, kCFURLPOSIXPathStyle);
  CFArrayAppendValue(config.paths, cfPath);
  CFRelease(cfPath);
  CFRelease(placeholder);

#else

#ifdef DEBUG
  fprintf(stderr, "compiled against 10.5, using realpath()\n");
#endif

  char fullPath[PATH_MAX + 1];

  if (realpath(path, fullPath) == NULL) {
#ifdef DEBUG
    fprintf(stderr, "  realpath not directly resolvable from path\n");
#endif

    if (path[0] != '/') {
#ifdef DEBUG
      fprintf(stderr, "  passed path is not absolute\n");
#endif
      size_t len;
      getcwd(fullPath, sizeof(fullPath));
#ifdef DEBUG
      fprintf(stderr, "  result of getcwd: %s\n", fullPath);
#endif
      len = strlen(fullPath);
      fullPath[len] = '/';
      strlcpy(&fullPath[len + 1], path, sizeof(fullPath) - (len + 1));
    } else {
#ifdef DEBUG
      fprintf(stderr, "  assuming path does not YET exist\n");
#endif
      strlcpy(fullPath, path, sizeof(fullPath));
    }
  }

#ifdef DEBUG
  fprintf(stderr, "  resolved path to: %s\n", fullPath);
  fprintf(stderr, "\n");
#endif

  CFStringRef pathRef = CFStringCreateWithCString(kCFAllocatorDefault,
                                                  fullPath,
                                                  kCFStringEncodingUTF8);
  CFArrayAppendValue(config.paths, pathRef);
  CFRelease(pathRef);

#endif
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
    sprintb(buf, eventFlags[i], FSEVENTSBITS);
    printf("%llu\t%#.8x=[%s]\t%s\n", eventIds[i], eventFlags[i], buf, paths[i]);
  }
  free(buf);
}

int main(int argc, const char* argv[])
{
  parse_cli_settings(argc, argv);

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