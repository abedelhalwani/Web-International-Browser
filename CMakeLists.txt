cmake_minimum_required(VERSION 3.20)
project(WebInternationalBrowser CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# The WebView2 loader below is linked from the x64 folder, so only 64-bit builds are supported.
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "64-bit build required. Configure with: cmake -S . -B build -A x64")
endif()

# Provide the WebView2 SDK, e.g. extract the NuGet package
# Microsoft.Web.WebView2 and pass -DWEBVIEW2_SDK=<path>.
set(WEBVIEW2_SDK "" CACHE PATH "Path to the extracted Microsoft.Web.WebView2 NuGet package")
if(NOT WEBVIEW2_SDK)
  message(FATAL_ERROR "Set -DWEBVIEW2_SDK=<path to Microsoft.Web.WebView2 package>")
endif()

add_executable(WebInternationalBrowser WIN32 main.cpp TabManager.cpp)
target_compile_definitions(WebInternationalBrowser PRIVATE UNICODE _UNICODE NOMINMAX)
target_include_directories(WebInternationalBrowser PRIVATE "${WEBVIEW2_SDK}/build/native/include")
target_link_directories(WebInternationalBrowser PRIVATE "${WEBVIEW2_SDK}/build/native/x64")
target_link_libraries(WebInternationalBrowser PRIVATE WebView2LoaderStatic.lib shlwapi.lib ole32.lib)
target_compile_options(WebInternationalBrowser PRIVATE /utf-8 /W4)
