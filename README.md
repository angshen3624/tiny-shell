# tiny-shell

AUTHOR
Ang Shen<asi031>  Jixiao Ma<jmq856>


For man page: groff -man -Tascii tsh.1
The tiny shell is a simple command line interpreter which have the basic operations from kernel and system. It can control process, handle signals, execute commands, and redirect I/O. It is written in C for Unix system.  

Run the tiny shell:   ./tsh

Exit the tiny shell:  exit	

Add a job to background:    &

Tell a job to continue and bring the backgrounded job to the foreground:  fg [ID] 

Send SIGCONT to a backgrounded job:  bg [ID]

Print out a table of all currently Running or Stopped jobs: jobs 

# tiny-shell
# tiny-shell
