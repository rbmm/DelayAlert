# DelayAlert

ksupnd.dll is DbgEng Extension DLL
put it in Debuggers\x64\winext\ 

use .load ksupnd and .unload ksupnd inside windbg

ksupnd.dll is tiny kernel driver (signed with selfsigned cerificatat, so require testsign mode)

can be loaded and unloaded (if no thread suspended inside it) (use install.bat for install driver, that start/stop and deinstall, if need)

ksupnd support 2 command

!kdelay [dwMilliseconds]

and

!kalert <thread>
-----------------------------------------------

dwMilliseconds is optional parameter - The time interval for which execution is to be suspended, in milliseconds
for instance 
!kdelay 8000
will suspended execution of current thread on 8 seconds

!kdelay
indicates that the suspension should not time out. (until !kalert <thread> call)

command print thread ( PKTHREAD ) of current (suspended) thread. it value need later user in !kalert call

command not check that IRQL <= APC_LEVEL or thread in some system wide critical section.
------------------------------------------------------

!kalert <thread>
<thread> calue is mandatory and must be from previous !kalert output

----------------------------------------------------------------

internally !kdelay save context of current execution in stack (PC, registers) and call KeDelayExecutionThread, after resore registers and ret back
!kalert call KeAlertThread (of course you must pass CORRECT value of thread)
 
