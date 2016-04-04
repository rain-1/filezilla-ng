!ifndef EXECUTABLE_RUNNING_INCLUDED
!define EXECUTABLE_RUNNING_INCLUDED

!include "LogicLib.nsh"

; Returns number of processes * 4 on top of stack,
; array with all processes right below it.
; Array should be cleared using System::Free

Function EnumProcesses

  ; Is this really necesssary for $Rx?
  Push $R1
  Push $R0
  Push $R2
  Push $R3

  ; Double size of array each time EnumProcesses fills it completely so that
  ; we do get all processes

  StrCpy $R1 1024

 enum_processes_loop:

  System::Alloc $R1

  Pop $R0

  System::Call "psapi::EnumProcesses(i R0, i R1, *i .R2) i .R3"

  ${If} $R3 == 0
    ; EnumProcesses failed, how can that be? :P
    goto enum_processes_fail
  ${EndIf}

  ${If} $R1 == $R2

    ; Too small buffer. Retry with twice the size

    Intop $R1 $R1 * 2
    System::Free $R0

    goto enum_processes_loop

  ${EndIf}

  StrCpy $R1 $R2

  ; Restore registers
  ; and put results on stack
  Pop $R3
  Pop $R2
  Exch $R0
  Exch
  Exch $R1
  return

 enum_processes_fail:

  Pop $R3
  Pop $R2
  Pop $R0
  Pop $R1

  Push 0
  Push 0

FunctionEnd

; Expects process ID on top of stack, returns
; filename (in device syntax) on top of stack
Function GetFilenameFromProcessId

  Exch $R0
  Push $R1
  Push $R2
  Push $R3

  !define PROCESS_QUERY_INFORMATION 0x0400
  System::Call "kernel32::OpenProcess(i ${PROCESS_QUERY_INFORMATION}, i 0, i $R0) i .R0"

  ${If} $R0 == 0

    Pop $R3
    Pop $R2
    Pop $R1
    Pop $R0
    Push ''
    return

  ${EndIf}

  StrCpy $R3 ${NSIS_MAX_STRLEN}
  System::Call "kernel32::QueryFullProcessImageName(i R0, i 0, t .R1, *i R3) i .R2"

  ${If} $R2 == 0
    ; Fallback
    System::Call "psapi::GetProcessImageFileName(i R0, t .R1, i ${NSIS_MAX_STRLEN}) i .R2"
  ${EndIf}

  ${If} $R2 == 0

    System::Call "kernel32::CloseHandle(i R0)"
    Pop $R3
    Pop $R2
    Pop $R1
    Pop $R0
    Push ''

    return

  ${EndIf}

  System::Call "kernel32::CloseHandle(i R0)"

  Pop $R3
  Pop $R2
  StrCpy $R0 $R1
  Pop $R1
  Exch $R0

FunctionEnd

; Expects process name on top of stack
; Afterwards, top of stack contains path to the process if it's running or an empty string if it is not.
Function IsProcessRunning

  Exch $R0 ; Name

  Push $R1 ; Bytes
  Push $R2 ; Array
  Push $R3 ; Counter
  Push $R4 ; Strlen
  Push $R5 ; Current process ID and image filename
  Push $R6 ; Last part of path

  StrCpy $R0 "\$R0"

  StrLen $R4 $R0
  IntOp $R4 0 - $R4

  Call EnumProcesses

  Pop $R1
  Pop $R2

  StrCpy $R3 0

  ${While} $R3 < $R1

    IntOp $R5 $R2 + $R3

    System::Call "*$R5(i .R5)"
    Push $R5

    Call GetFilenameFromProcessId

    Pop $R5

    ; Get last part of filename
    StrCpy $R6 $R5 '' $R4

    ${If} $R6 == $R0

      ; Program is running
      StrCpy $R0 $R5
      Pop $R6
      Pop $R5
      Pop $R4
      Pop $R3
      Pop $R2
      Pop $R1
      Exch $R0
      return

    ${EndIf}

    IntOp $R3 $R3 + 4

  ${EndWhile}

  Pop $R5
  Pop $R4
  Pop $R3
  Pop $R2
  Pop $R1
  Pop $R0
  Push ''

FunctionEnd

!endif ;EXECUTABLE_RUNNING_INCLUDED
