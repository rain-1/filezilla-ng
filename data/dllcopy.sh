#! /bin/sh

if [ $# != 5 ]; then
  echo Wrong number of arguments
  exit 1
fi

exepath="$1"
exename="$2"
objdump="$3"
cxx="$4"
searchpath="$5"

searchpath="$searchpath:`\"$cxx\" -print-search-dirs | grep libraries | sed 's/libraries: =//'`"

#echo $searchpath

touch dll_install.nsh
touch dll_uninstall.nsh

process_dll()
{
  if [ ! -f "dlls/$1" ] && [ ! -f "dlls/${1}.processed" ]; then
    echo "Looking for dependency $1"
    (
      IFS=':'
      for path in $searchpath; do
        if [ -f "$path/$1" ]; then
          unset IFS
          echo "Found $1"
          cp "$path/$1" "dlls/$1"
          process_file "dlls/$1"

          echo "File dlls\\$1" >> dll_install.nsh
          echo "Delete \$INSTDIR\\$1" >> dll_uninstall.nsh
          break
        fi
      done
      unset IFS
      echo processed > "dlls/${1}.processed"
    )
  fi
}

process_dlls()
{
  while [ ! -z "$1" ]; do
    process_dll "$1"
    shift
  done
}

process_file()
{
  process_dlls `"$objdump" -j .idata -x "$1" | grep 'DLL Name:' | sed 's/ *DLL Name: *//'`
}

if [ -f "$exepath/.libs/$exename" ]; then
  process_file "$exepath/.libs/$exename"
else
  process_file "$exepath/$exename"
fi
