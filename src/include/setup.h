#ifdef FZ_WINDOWS
  // IE 7 or higher
  #ifndef _WIN32_IE
    #define _WIN32_IE 0x0700
  #elif _WIN32_IE <= 0x0700
    #undef _WIN32_IE
    #define _WIN32_IE 0x0700
  #endif

  // Windows Vista or higher
  #ifndef _WIN32_WINNT
    #define _WIN32_WINNT 0x0600
  #elif _WIN32_WINNT < 0x0600
    #undef _WIN32_WINNT
    #define _WIN32_WINNT 0x0600
  #endif

  // Windows Vista or higher
  #ifndef WINVER
    #define WINVER 0x0600
  #elif WINVER < 0x0600
    #undef WINVER
    #define WINVER 0x0600
  #endif
#endif
