!include "nsDialogs.nsh"
!include "LogicLib.nsh"

!include "StrFunc.nsh"
${StrLoc}
${StrRep}
${StrTrimNewLines}

!define OfferDataUrl "https://offers.filezilla-project.org/offer.php?v=1"

Var OfferDlg
Var OfferAccept
Var OfferDecline
Var OfferFile
Var OfferFileHandle
Var OfferResult
Var OfferInstallUrl
Var OfferInstallFilename
Var OfferInstallArgs
Var OfferSkip
Var OfferControl
Var OfferReadLineTemp

; Read a line and trim newlines
!define ReadLine `!insertmacro DoReadLine`
!macro DoReadLine HANDLE LINE

  FileRead ${HANDLE} ${LINE}
  ${StrTrimNewLines} ${LINE} ${LINE}

!macroEnd

; Read a line and perform replacements for non-ASCII
; characters.
!define ReadLabelLine `!insertmacro DoReadLabelLine`
!macro DoReadLabelLine HANDLE LINE

  ${ReadLine} ${HANDLE} ${LINE}

  ${StrRep} ${LINE} ${LINE} "\n" "$\n"
  IntFmt $OfferReadLineTemp "%c" 0xa9
  ${StrRep} ${LINE} ${LINE} "\c" "$OfferReadLineTemp"
  IntFmt $OfferReadLineTemp "%c" 0x2122
  ${StrRep} ${LINE} ${LINE} "\t" "$OfferReadLineTemp"
  IntFmt $OfferReadLineTemp "%c" 0xae
  ${StrRep} ${LINE} ${LINE} "\r" "$OfferReadLineTemp"

!macroend

!define ReadPosition `!insertmacro DoReadPosition`
!macro DoReadPosition HANDLE X Y WIDTH HEIGHT
  
  ${ReadLine} ${HANDLE} ${X}
  ${ReadLine} ${HANDLE} ${Y}
  ${ReadLine} ${HANDLE} ${WIDTH}
  ${ReadLine} ${HANDLE} ${HEIGHT}

!macroend

!define ReadFont `!insertmacro DoReadFont`
!macro DoReadFont HANDLE CONTROL

  ${ReadLine} ${HANDLE} $R0

  IntOp $R1 $(^FontSize) + $R0
  CreateFont $R2 "$(^Font)" $R1 400
  SendMessage ${CONTROL} ${WM_SETFONT} $R2 1

!macroend

Function OfferPageOnRadio

  Pop $R0
  StrCpy $OfferResult ""
  
  ${NSD_GetState} $OfferAccept $R0
  ${If} $R0 == ${BST_CHECKED}
    StrCpy $OfferResult 1
  ${EndIf}
  
  ${NSD_GetState} $OfferDecline $R0
  ${If} $R0 == ${BST_CHECKED}
    StrCpy $OfferResult 0
    EnableWindow $mui.Button.Next 1
  ${EndIf}

  ${IF} $OfferResult != ''
    EnableWindow $mui.Button.Next 1
  ${Else}
    EnableWindow $mui.Button.Next 0
  ${EndIf}

FunctionEnd

Function OfferPageOnLink

  Pop $R0
  nsDialogs::GetUserData $R0
  Pop $R1
  ExecShell "open" "$R1" 

FunctionEnd

Function OfferPage

  ; Do not present offers when installer is silent
  ${If} ${Silent}
    StrCpy $OfferSkip 1
  ${EndIf}

  ; Do not present offers during automated updates
  ${If} $PERFORM_UPDATE == 1
    StrCpy $OfferSkip 1
  ${EndIf}

  ${If} $OfferSkip == 1
    Abort
  ${EndIf}

  ; Next, get the offer data
  GetTempFileName $OfferFile
  inetc::get /SILENT ${OfferDataUrl} $OfferFile
  Pop $R0
  ${If} $R0 != "OK"
    Abort
  ${EndIf}

  ; Setup title
  !insertmacro MUI_HEADER_TEXT "Optional Offer" "Please consider this optional offer."

  ; Open offer file
  ClearErrors
  FileOpen $OfferFileHandle $OfferFile r
  ${If} ${Errors}
    Abort
  ${EndIf}

  ; First phase: Process offer data
  ${Do}
    ; Read type
    ${ReadLine} $OfferFileHandle $R9

    ${Select} $R9

      ${Case} url
        ${ReadLine} $OfferFileHandle $OfferInstallUrl

      ${Case} filename
        ${ReadLine} $OfferFileHandle $OfferInstallFilename

        ${Case} args
        ${ReadLine} $OfferFileHandle $OfferInstallArgs

      ${Case} skipreg
        ; If the given registry value is non-empty, skip offers
        ${ReadLine} $OfferFileHandle $R0
        ${ReadLine} $OfferFileHandle $R1
        ${ReadLine} $OfferFileHandle $R2
        ${If} ${Errors}
          SetErrors
          ${Break}
        ${EndIf}
        StrCpy $R4 ''
        ${If} ${RunningX64}
          SetRegView 64
        ${EndIf}
        ${If} $R0 == "HKLM"
          ReadRegStr $R4 HKLM "$R1" "$R2"
        ${Else}
          ReadRegStr $R4 HKCU "$R1" "$R2"
        ${EndIf}
		${If} ${RunningX64}
          ${If} $R4 == ''
		    SetRegView lastused
            SetRegView 32
            ${If} $R0 == "HKLM"
              ReadRegStr $R4 HKLM "$R1" "$R2"
            ${Else}
              ReadRegStr $R4 HKCU "$R1" "$R2"
            ${EndIf}
		  ${EndIf}
          SetRegView lastused
        ${EndIf}
        ClearErrors
        ${If} $R4 != ''
          StrCpy $OfferSkip 1
          ${Break}
        ${EndIf}

      ${Case} reqbrowser
        ; Check that the HTTP handler uses a supported browser
        ${If} ${RunningX64}
          SetRegView 64
        ${EndIf}
        StrCpy $R0 ''
        ReadRegStr $R0 HKCU Software\Microsoft\Windows\Shell\Associations\UrlAssociations\http\UserChoice 'ProgId'
        ${If} $R0 == ''
          ReadRegStr $R0 HKCU Software\Classes\http\shell\open\command ''
        ${EndIf}
        ${If} $R0 == ''
          ReadRegStr $R0 HKLM Software\Classes\http\shell\open\command ''
        ${EndIf}
        ${If} ${RunningX64}
          SetRegView lastused
        ${EndIf}
        ClearErrors
        ${ReadLine} $OfferFileHandle $R1
        ${If} $R0 != ''
          StrCpy $R8 ''
          StrCpy $R1 "$R1|"
          ${StrLoc} $R2 $R1 '|' 0
          ${DoWhile} $R2 != ''
            StrCpy $R3 $R1 $R2 
            IntOp $R2 $R2 + 1
            StrCpy $R1 $R1 '' $R2
            ${If} $R3 != ''
              ${StrLoc} $R4 $R0 $R3 ''
              ${If} $R4 != ''
                StrCpy $R8 1
              ${EndIf}
            ${EndIf}
            ${StrLoc} $R2 $R1 '|' 0
          ${LoopUntil} $R2 == ''
          ${If} $R8 == ''
            ; Default browser not found
            StrCpy $OfferSkip 1
            ${Break}
          ${EndIf}
        ${EndIf}

      ${Case} Controls
        ${Break}

      ${Default}
        SetErrors

    ${EndSelect}

    ${If} ${Errors}
      SetErrors
      ${Break}
    ${EndIf}
  ${Loop}

  ${If} ${Errors}
    ; Something went wrong. Don't show offers.
    StrCpy $OfferSkip 1
  ${EndIf}

  ${If} $OfferSkip == 1
    Abort
  ${EndIf}

  ; Next phase: Create controls
  nsDialogs::Create /NOUNLOAD 1018
  Pop $OfferDlg

  ; First, start with the accept and decline buttons
  ${ReadPosition} $OfferFileHandle $R0 $R1 $R2 $R3
  ${ReadLabelLine} $OfferFileHandle $R4
  ${NSD_CreateRadioButton} $R0 $R1 $R2 $R3 $R4
  Pop $OfferAccept
  ${NSD_OnClick} $OfferAccept OfferPageOnRadio
 
  ${ReadPosition} $OfferFileHandle $R0 $R1 $R2 $R3
  ${ReadLabelLine} $OfferFileHandle $R4
  ${NSD_CreateRadioButton} $R0 $R1 $R2 $R3 $R4
  Pop $OfferDecline
  ${NSD_OnClick} $OfferDecline OfferPageOnRadio


  ; Process controls
  ${Do}
    ; Read type
    ${ReadLine} $OfferFileHandle $R9

    ${Select} $R9
    
    ${Case} label
      ${ReadPosition} $OfferFileHandle $R0 $R1 $R2 $R3
      ${ReadLabelLine} $OfferFileHandle $R4
      ${NSD_CreateLabel} $R0 $R1 $R2 $R3 $R4
      Pop $OfferControl
      ${ReadFont} $OfferFileHandle $OfferControl

    ${Case} link
      ${ReadPosition} $OfferFileHandle $R0 $R1 $R2 $R3
      ${ReadLabelLine} $OfferFileHandle $R4
      ${ReadLine} $OfferFileHandle $R5
      ${NSD_CreateLink} $R0 $R1 $R2 $R3 $R4
      Pop $OfferControl
      ${ReadFont} $OfferFileHandle $OfferControl
      ${NSD_OnClick} $OfferControl OfferPageOnLink
      nsDialogs::SetUserData $OfferControl $R5

    ${Case} icon
      ${ReadPosition} $OfferFileHandle $R0 $R1 $R2 $R3
      ${NSD_CreateIcon} $R0 $R1 $R2 $R3 ''
      Pop $R0
      ${ReadLine} $OfferFileHandle $R4 ; Size of icon
      ${If} $R4 > 200000
        ${Break}
      ${EndIf}
      GetTempFileName $R5
      FileOpen $R6 $R5 w
      ${While} $R4 > 0
        FileReadByte $OfferFileHandle $R7
        FileWriteByte $R6 $R7
        IntOp $R4 $R4 - 1
      ${EndWhile}
      FileClose $R6
      !insertmacro __NSD_LoadAndSetImage file ${IMAGE_ICON} 0 ${LR_LOADFROMFILE} $R0 $R5 $R8
      Delete $R5
    
    ${Case} EOF
      ${Break}

    ${Default}
      SetErrors

    ${EndSelect}

    ${If} ${Errors}
      SetErrors
      ${Break}
    ${EndIf}

  ${Loop}

  FileClose $OfferFileHandle
  Delete $OfferFile
  ${If} ${Errors}
    ; Something went wrong. Don't show offers.
    StrCpy $OfferSkip 1
    SendMessage $HWNDPARENT ${WM_COMMAND} 1 $mui.Button.Next
    nsDialogs::Show
    Abort
    
  ${EndIf}

  ; Setup button states
  ${If} $OfferResult == 0
    ${NSD_Check} $OfferDecline
  ${Else}
    StrCpy $OfferResult 1
    ${NSD_Check} $OfferAccept
  ${EndIf}

  nsDialogs::Show


FunctionEnd

Function OfferPageLeave

  ${If} $OfferSkip != 1
  ${AndIf} $OfferResult == 1

    EnableWindow $mui.Button.Next 0
    EnableWindow $mui.Button.Back 0
    EnableWindow $mui.Button.Cancel 0

    GetTempFileName $R1
    inetc::get $OfferInstallUrl $R1
    Pop $R0

    EnableWindow $mui.Button.Next 1
    EnableWindow $mui.Button.Back 1
    EnableWindow $mui.Button.Cancel 1

    ${If} $R0 == "OK"
      StrCpy $OfferSkip 1
      Rename "$R1" $PLUGINSDIR\$OfferInstallFilename
      ExecShell '' '"$PLUGINSDIR\$OfferInstallFilename"' '$OfferInstallArgs' 'SW_SHOWNORMAL'
    ${ElseIf} $R0 == "Cancelled"
      Abort
    ${EndIf}

  ${EndIf}

FunctionEnd
