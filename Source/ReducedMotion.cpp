// Reduced-motion preference on platforms other than macOS (see ReducedMotion.mm for macOS).

#if defined (_WIN32)
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
#endif

bool semitoneShouldReduceMotion();

bool semitoneShouldReduceMotion()
{
   #if defined (_WIN32)
    // "Show animations in Windows" switched off means the user wants less motion.
    BOOL animationsEnabled = TRUE;
    if (SystemParametersInfo (SPI_GETCLIENTAREAANIMATION, 0, &animationsEnabled, 0))
        return animationsEnabled == FALSE;
   #endif

    return false;
}
