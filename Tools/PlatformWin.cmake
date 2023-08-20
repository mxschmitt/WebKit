if (ENABLE_MINIBROWSER)
    add_subdirectory(MiniBrowser/win)
endif ()

if (ENABLE_WEBKIT)
    add_subdirectory(Playwright/win)
endif ()
