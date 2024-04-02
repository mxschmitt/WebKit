set(WebKit_OUTPUT_NAME WebKit2)
set(WebProcess_OUTPUT_NAME WebKitWebProcess)
set(NetworkProcess_OUTPUT_NAME WebKitNetworkProcess)
set(GPUProcess_OUTPUT_NAME WebKitGPUProcess)
set(PluginProcess_OUTPUT_NAME WebKitPluginProcess)

include(Headers.cmake)
include(Platform/Curl.cmake)
include(Platform/WC.cmake)

list(APPEND WebKit_SOURCES
    GPUProcess/media/win/RemoteMediaPlayerProxyWin.cpp

    GPUProcess/win/GPUProcessMainWin.cpp
    GPUProcess/win/GPUProcessWin.cpp

    NetworkProcess/NetworkDataTaskDataURL.cpp

    NetworkProcess/Classifier/WebResourceLoadStatisticsStore.cpp

    Platform/IPC/win/ArgumentCodersWin.cpp
    Platform/IPC/win/ConnectionWin.cpp
    Platform/IPC/win/IPCSemaphoreWin.cpp

    Platform/classifier/ResourceLoadStatisticsClassifier.cpp

    Platform/win/LoggingWin.cpp
    Platform/win/ModuleWin.cpp

    Shared/win/AuxiliaryProcessMainWin.cpp
    Shared/win/NativeWebKeyboardEventWin.cpp
    Shared/win/NativeWebMouseEventWin.cpp
    Shared/win/NativeWebTouchEventWin.cpp
    Shared/win/NativeWebWheelEventWin.cpp
    Shared/win/WebCoreArgumentCodersWin.cpp
    Shared/win/WebEventFactory.cpp

    UIProcess/API/C/WKViewportAttributes.cpp

    UIProcess/API/C/win/WKView.cpp

    UIProcess/Automation/win/WebAutomationSessionWin.cpp

    UIProcess/DefaultUndoController.cpp
    UIProcess/LegacySessionStateCodingNone.cpp
    UIProcess/WebGrammarDetail.cpp
    UIProcess/WebViewportAttributes.cpp

    UIProcess/CoordinatedGraphics/DrawingAreaProxyCoordinatedGraphics.cpp

    UIProcess/Inspector/win/InspectorResourceURLSchemeHandler.cpp
    UIProcess/Inspector/win/WebInspectorUIProxyWin.cpp

    UIProcess/Launcher/win/ProcessLauncherWin.cpp

    UIProcess/WebsiteData/win/WebsiteDataStoreWin.cpp

    UIProcess/win/AutomationClientWin.cpp

    UIProcess/win/InspectorTargetProxyWin.cpp
    UIProcess/win/InspectorPlaywrightAgentClientWin.cpp
    UIProcess/win/PageClientImpl.cpp
    UIProcess/win/WebContextMenuProxyWin.cpp
    UIProcess/win/WebPageInspectorEmulationAgentWin.cpp
    UIProcess/win/WebPageInspectorInputAgentWin.cpp
    UIProcess/win/WebPageProxyWin.cpp
    UIProcess/win/WebPopupMenuProxyWin.cpp
    UIProcess/win/WebProcessPoolWin.cpp
    UIProcess/win/WebView.cpp

    WebProcess/GPU/media/win/VideoLayerRemoteWin.cpp

    WebProcess/InjectedBundle/win/InjectedBundleWin.cpp

    WebProcess/Inspector/win/RemoteWebInspectorUIWin.cpp
    WebProcess/Inspector/win/WebInspectorUIWin.cpp

    WebProcess/MediaCache/WebMediaKeyStorageManager.cpp

    WebProcess/WebCoreSupport/win/WebPopupMenuWin.cpp
    WebProcess/WebCoreSupport/win/WebDragClientWin.cpp

    WebProcess/WebPage/AcceleratedSurface.cpp

    WebProcess/WebPage/CoordinatedGraphics/CompositingCoordinator.cpp
    WebProcess/WebPage/CoordinatedGraphics/DrawingAreaCoordinatedGraphics.cpp

    WebProcess/WebPage/win/WebPageWin.cpp

    WebProcess/win/WebProcessMainWin.cpp
    WebProcess/win/WebProcessWin.cpp

    win/WebKitDLL.cpp
)

list(APPEND WebKit_PRIVATE_INCLUDE_DIRECTORIES
    "${WEBKIT_DIR}/Platform/IPC/win"
    "${WEBKIT_DIR}/Platform/classifier"
    "${WEBKIT_DIR}/Platform/generic"
    "${WEBKIT_DIR}/PluginProcess/win"
    "${WEBKIT_DIR}/Shared/API/c/win"
    "${WEBKIT_DIR}/Shared/CoordinatedGraphics"
    "${WEBKIT_DIR}/Shared/CoordinatedGraphics/threadedcompositor"
    "${WEBKIT_DIR}/Shared/win"
    "${WEBKIT_DIR}/UIProcess/API/C/win"
    "${WEBKIT_DIR}/UIProcess/API/cpp/win"
    "${WEBKIT_DIR}/UIProcess/API/win"
    "${WEBKIT_DIR}/UIProcess/CoordinatedGraphics"
    "${WEBKIT_DIR}/UIProcess/Inspector/socket"
    "${WEBKIT_DIR}/UIProcess/Inspector/win"
    "${WEBKIT_DIR}/UIProcess/Plugins/win"
    "${WEBKIT_DIR}/UIProcess/win"
    "${WEBKIT_DIR}/WebProcess/InjectedBundle/API/win"
    "${WEBKIT_DIR}/WebProcess/InjectedBundle/API/win/DOM"
    "${WEBKIT_DIR}/WebProcess/Inspector/win"
    "${WEBKIT_DIR}/WebProcess/WebCoreSupport/win"
    "${WEBKIT_DIR}/WebProcess/WebPage/CoordinatedGraphics"
    "${WEBKIT_DIR}/WebProcess/WebPage/win"
    "${WEBKIT_DIR}/win"
)

list(APPEND WebKit_PUBLIC_FRAMEWORK_HEADERS
    Shared/API/c/win/WKBaseWin.h

    UIProcess/API/C/win/WKView.h
)

list(APPEND WebKit_PRIVATE_LIBRARIES
    comctl32
    ${LIBVPX_CUSTOM_LIBRARY}
)

# Playwright begin
list(APPEND WebKit_SYSTEM_INCLUDE_DIRECTORIES
    "${THIRDPARTY_DIR}/libwebrtc/Source/third_party/libyuv/include"
    "${LIBVPX_CUSTOM_INCLUDE_DIR}"
)

list(APPEND WebKit_PRIVATE_INCLUDE_DIRECTORIES
    "${THIRDPARTY_DIR}/libwebrtc/Source/third_party/libwebm"
)

set(vpxutils_SOURCES
    "${THIRDPARTY_DIR}/libwebrtc/Source/third_party/libwebm/mkvmuxer/mkvmuxer.cc"
    "${THIRDPARTY_DIR}/libwebrtc/Source/third_party/libwebm/mkvmuxer/mkvmuxerutil.cc"
    "${THIRDPARTY_DIR}/libwebrtc/Source/third_party/libwebm/mkvmuxer/mkvwriter.cc"
    "${THIRDPARTY_DIR}/libwebrtc/Source/third_party/libyuv/source/compare.cc"
    "${THIRDPARTY_DIR}/libwebrtc/Source/third_party/libyuv/source/compare_common.cc"
    "${THIRDPARTY_DIR}/libwebrtc/Source/third_party/libyuv/source/compare_gcc.cc"
    "${THIRDPARTY_DIR}/libwebrtc/Source/third_party/libyuv/source/compare_msa.cc"
    "${THIRDPARTY_DIR}/libwebrtc/Source/third_party/libyuv/source/compare_neon64.cc"
    "${THIRDPARTY_DIR}/libwebrtc/Source/third_party/libyuv/source/compare_neon.cc"
    "${THIRDPARTY_DIR}/libwebrtc/Source/third_party/libyuv/source/compare_win.cc"
    "${THIRDPARTY_DIR}/libwebrtc/Source/third_party/libyuv/source/convert_argb.cc"
    "${THIRDPARTY_DIR}/libwebrtc/Source/third_party/libyuv/source/convert.cc"
    "${THIRDPARTY_DIR}/libwebrtc/Source/third_party/libyuv/source/convert_from_argb.cc"
    "${THIRDPARTY_DIR}/libwebrtc/Source/third_party/libyuv/source/convert_from.cc"
    "${THIRDPARTY_DIR}/libwebrtc/Source/third_party/libyuv/source/convert_jpeg.cc"
    "${THIRDPARTY_DIR}/libwebrtc/Source/third_party/libyuv/source/convert_to_argb.cc"
    "${THIRDPARTY_DIR}/libwebrtc/Source/third_party/libyuv/source/convert_to_i420.cc"
    "${THIRDPARTY_DIR}/libwebrtc/Source/third_party/libyuv/source/cpu_id.cc"
    "${THIRDPARTY_DIR}/libwebrtc/Source/third_party/libyuv/source/mjpeg_decoder.cc"
    "${THIRDPARTY_DIR}/libwebrtc/Source/third_party/libyuv/source/mjpeg_validate.cc"
    "${THIRDPARTY_DIR}/libwebrtc/Source/third_party/libyuv/source/planar_functions.cc"
    "${THIRDPARTY_DIR}/libwebrtc/Source/third_party/libyuv/source/rotate_any.cc"
    "${THIRDPARTY_DIR}/libwebrtc/Source/third_party/libyuv/source/rotate_argb.cc"
    "${THIRDPARTY_DIR}/libwebrtc/Source/third_party/libyuv/source/rotate.cc"
    "${THIRDPARTY_DIR}/libwebrtc/Source/third_party/libyuv/source/rotate_common.cc"
    "${THIRDPARTY_DIR}/libwebrtc/Source/third_party/libyuv/source/rotate_gcc.cc"
    "${THIRDPARTY_DIR}/libwebrtc/Source/third_party/libyuv/source/rotate_msa.cc"
    "${THIRDPARTY_DIR}/libwebrtc/Source/third_party/libyuv/source/rotate_neon64.cc"
    "${THIRDPARTY_DIR}/libwebrtc/Source/third_party/libyuv/source/rotate_neon.cc"
    "${THIRDPARTY_DIR}/libwebrtc/Source/third_party/libyuv/source/rotate_win.cc"
    "${THIRDPARTY_DIR}/libwebrtc/Source/third_party/libyuv/source/row_any.cc"
    "${THIRDPARTY_DIR}/libwebrtc/Source/third_party/libyuv/source/row_common.cc"
    "${THIRDPARTY_DIR}/libwebrtc/Source/third_party/libyuv/source/row_gcc.cc"
    "${THIRDPARTY_DIR}/libwebrtc/Source/third_party/libyuv/source/row_msa.cc"
    "${THIRDPARTY_DIR}/libwebrtc/Source/third_party/libyuv/source/row_neon64.cc"
    "${THIRDPARTY_DIR}/libwebrtc/Source/third_party/libyuv/source/row_neon.cc"
    "${THIRDPARTY_DIR}/libwebrtc/Source/third_party/libyuv/source/row_win.cc"
    "${THIRDPARTY_DIR}/libwebrtc/Source/third_party/libyuv/source/scale_any.cc"
    "${THIRDPARTY_DIR}/libwebrtc/Source/third_party/libyuv/source/scale_argb.cc"
    "${THIRDPARTY_DIR}/libwebrtc/Source/third_party/libyuv/source/scale.cc"
    "${THIRDPARTY_DIR}/libwebrtc/Source/third_party/libyuv/source/scale_common.cc"
    "${THIRDPARTY_DIR}/libwebrtc/Source/third_party/libyuv/source/scale_gcc.cc"
    "${THIRDPARTY_DIR}/libwebrtc/Source/third_party/libyuv/source/scale_msa.cc"
    "${THIRDPARTY_DIR}/libwebrtc/Source/third_party/libyuv/source/scale_neon64.cc"
    "${THIRDPARTY_DIR}/libwebrtc/Source/third_party/libyuv/source/scale_neon.cc"
    "${THIRDPARTY_DIR}/libwebrtc/Source/third_party/libyuv/source/scale_rvv.cc"
    "${THIRDPARTY_DIR}/libwebrtc/Source/third_party/libyuv/source/scale_uv.cc"
    "${THIRDPARTY_DIR}/libwebrtc/Source/third_party/libyuv/source/scale_win.cc"
    "${THIRDPARTY_DIR}/libwebrtc/Source/third_party/libyuv/source/video_common.cc"
)

add_library(vpxutils STATIC ${vpxutils_SOURCES})

target_include_directories(vpxutils PRIVATE
    "${THIRDPARTY_DIR}/libwebrtc/Source/third_party/libyuv/include"
    "${THIRDPARTY_DIR}/libwebrtc/Source/third_party/libwebm"
)

target_link_libraries(WebKit PRIVATE vpxutils)
# Playwright end

list(APPEND WebProcess_SOURCES
    WebProcess/EntryPoint/win/WebProcessMain.cpp
)

list(APPEND NetworkProcess_SOURCES
    NetworkProcess/EntryPoint/win/NetworkProcessMain.cpp
)

list(APPEND GPUProcess_SOURCES
    GPUProcess/EntryPoint/win/GPUProcessMain.cpp
)

if (ENABLE_REMOTE_INSPECTOR)
    list(APPEND WebKit_SOURCES
        UIProcess/Inspector/socket/RemoteInspectorClient.cpp
        UIProcess/Inspector/socket/RemoteInspectorProtocolHandler.cpp

        UIProcess/Inspector/win/RemoteWebInspectorUIProxyWin.cpp
    )

    list(APPEND WebKit_PRIVATE_INCLUDE_DIRECTORIES
        "${WEBKIT_DIR}/UIProcess/socket"
    )
endif ()

if (USE_CAIRO)
    list(APPEND WebKit_SOURCES
        Shared/API/c/cairo/WKImageCairo.cpp

        UIProcess/Automation/cairo/WebAutomationSessionCairo.cpp
    )
    list(APPEND WebKit_PRIVATE_INCLUDE_DIRECTORIES
        "${WEBKIT_DIR}/UIProcess/API/C/cairo"
    )
    list(APPEND WebKit_PUBLIC_FRAMEWORK_HEADERS
        Shared/API/c/cairo/WKImageCairo.h
    )
endif ()
