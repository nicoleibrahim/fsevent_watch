# fsevent_watch

The tool allows you to monitor filesystem events or FSEvents on a Mac.

This is a clone from http://github.com/proger/fsevent_watch

Updated to include more flags, inode numbers and timestamps

## Usage
            fsevent_watch v0.2

            A flexible command-line interface for the FSEvents API

            Usage: fsevent_watch [OPTIONS]... [PATHS]...

              -h, --help                you're looking at it
              -V, --version             print version number and exit
              -p, --show-plist          display the embedded Info.plist values
              -s, --since-when=EventID  fire historical events since ID
              -l, --latency=seconds     latency period (default='0.5')
              -n, --no-defer            enable no-defer latency modifier
              -F, --file-events         provide file level event data
  
# Sample Output
```
% ./fsevent_watch -F /Users/SomeUser/Desktop
Current_Timestamp   Event_ID    Inode         Event_Flags                           Path
2019-06-07 15:38:43	24880655	8599274640    0x00010100=[created,isfile]	        /Users/SomeUser/Desktop/My_Test_File.txt
2019-06-07 15:39:58	24881111	8599274640    0x00011000=[modified,isfile]			/Users/SomeUser/Desktop/My_Test_File.txt
2019-06-07 15:40:18	24881273	8599274640    0x00011000=[modified,isfile]			/Users/SomeUser/Desktop/My_Test_File.txt
2019-06-07 15:40:40	24881611	0             0x00010200=[removed,isfile]	        /Users/SomeUser/Desktop/My_Test_File.txt
2019-06-07 15:40:54	24881804	8599274718    0x00010100=[created,isfile]	        /Users/SomeUser/Desktop/My_Test_File.txt
```



## Building

* just run `make install` (and make sure you have a compiler)

## Caveats

* fsevents API does not follow symlinks
