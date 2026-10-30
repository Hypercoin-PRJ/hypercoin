# Copyright (c) 2023-present The Hypercoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit/.

include_guard(GLOBAL)

function(setup_split_debug_script)
  if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
    set(OBJCOPY ${CMAKE_OBJCOPY})
    set(STRIP ${CMAKE_STRIP})
    configure_file(
      contrib/devtools/split-debug.sh.in split-debug.sh
      FILE_PERMISSIONS OWNER_READ OWNER_EXECUTE
                       GROUP_READ GROUP_EXECUTE
                       WORLD_READ
      @ONLY
    )
  endif()
endfunction()

function(add_windows_deploy_target)
  configure_file(${PROJECT_SOURCE_DIR}/cmake/script/GenerateWindowsInstaller.cmake.in ${PROJECT_BINARY_DIR}/GenerateWindowsInstaller.cmake USE_SOURCE_PERMISSIONS @ONLY)
  if(MINGW AND TARGET hypercoin AND TARGET hypercoin-qt AND TARGET hypercoind AND TARGET hypercoin-cli AND TARGET hypercoin-tx AND TARGET hypercoin-wallet AND TARGET hypercoin-util AND TARGET test_hypercoin)
    add_custom_command(
      OUTPUT ${PROJECT_BINARY_DIR}/hypercoin-win64-setup.exe
      WORKING_DIRECTORY ${PROJECT_BINARY_DIR}
      COMMAND ${CMAKE_COMMAND} -E make_directory release
      COMMAND ${CMAKE_STRIP} $<TARGET_FILE:hypercoin> -o release/$<TARGET_FILE_NAME:hypercoin>
      COMMAND ${CMAKE_STRIP} $<TARGET_FILE:hypercoin-qt> -o release/$<TARGET_FILE_NAME:hypercoin-qt>
      COMMAND ${CMAKE_STRIP} $<TARGET_FILE:hypercoind> -o release/$<TARGET_FILE_NAME:hypercoind>
      COMMAND ${CMAKE_STRIP} $<TARGET_FILE:hypercoin-cli> -o release/$<TARGET_FILE_NAME:hypercoin-cli>
      COMMAND ${CMAKE_STRIP} $<TARGET_FILE:hypercoin-tx> -o release/$<TARGET_FILE_NAME:hypercoin-tx>
      COMMAND ${CMAKE_STRIP} $<TARGET_FILE:hypercoin-wallet> -o release/$<TARGET_FILE_NAME:hypercoin-wallet>
      COMMAND ${CMAKE_STRIP} $<TARGET_FILE:hypercoin-util> -o release/$<TARGET_FILE_NAME:hypercoin-util>
      COMMAND ${CMAKE_STRIP} $<TARGET_FILE:test_hypercoin> -o release/$<TARGET_FILE_NAME:test_hypercoin>
      COMMAND ${CMAKE_COMMAND} -D BIN_DIR=release -D LIBEXEC_DIR=release -P GenerateWindowsInstaller.cmake
    )
    add_custom_target(deploy DEPENDS ${PROJECT_BINARY_DIR}/hypercoin-win64-setup.exe)
  endif()
endfunction()

function(add_macos_deploy_target)
  if(CMAKE_SYSTEM_NAME STREQUAL "Darwin" AND TARGET hypercoin-qt)
    set(macos_app "Hypercoin-Qt.app")
    # Populate Contents subdirectory.
    configure_file(${PROJECT_SOURCE_DIR}/share/qt/Info.plist.in ${macos_app}/Contents/Info.plist NO_SOURCE_PERMISSIONS)
    file(CONFIGURE OUTPUT ${macos_app}/Contents/PkgInfo CONTENT "APPL????")
    # Populate Contents/Resources subdirectory.
    file(CONFIGURE OUTPUT ${macos_app}/Contents/Resources/empty.lproj CONTENT "")
    configure_file(${PROJECT_SOURCE_DIR}/src/qt/res/icons/hypercoin.icns ${macos_app}/Contents/Resources/hypercoin.icns NO_SOURCE_PERMISSIONS COPYONLY)
    file(CONFIGURE OUTPUT ${macos_app}/Contents/Resources/Base.lproj/InfoPlist.strings
      CONTENT "{ CFBundleDisplayName = \"@CLIENT_NAME@\"; CFBundleName = \"@CLIENT_NAME@\"; }"
    )

    add_custom_command(
      OUTPUT ${PROJECT_BINARY_DIR}/${macos_app}/Contents/MacOS/Hypercoin-Qt
      COMMAND ${CMAKE_COMMAND} --install ${PROJECT_BINARY_DIR} --config $<CONFIG> --component hypercoin-qt --prefix ${macos_app}/Contents/MacOS --strip
      COMMAND ${CMAKE_COMMAND} -E rename ${macos_app}/Contents/MacOS/bin/$<TARGET_FILE_NAME:hypercoin-qt> ${macos_app}/Contents/MacOS/Hypercoin-Qt
      COMMAND ${CMAKE_COMMAND} -E rm -rf ${macos_app}/Contents/MacOS/bin
      COMMAND ${CMAKE_COMMAND} -E rm -rf ${macos_app}/Contents/MacOS/share
      VERBATIM
    )

    set(macos_zip "hypercoin-macos-app")
    if(CMAKE_HOST_APPLE)
      add_custom_command(
        OUTPUT ${PROJECT_BINARY_DIR}/${macos_zip}.zip
        COMMAND Python3::Interpreter ${PROJECT_SOURCE_DIR}/contrib/macdeploy/macdeployqtplus ${macos_app} -translations-dir=${QT_TRANSLATIONS_DIR} -zip=${macos_zip}
        DEPENDS ${PROJECT_BINARY_DIR}/${macos_app}/Contents/MacOS/Hypercoin-Qt
        VERBATIM
      )
      add_custom_target(deploydir
        DEPENDS ${PROJECT_BINARY_DIR}/${macos_zip}.zip
      )
      add_custom_target(deploy
        DEPENDS ${PROJECT_BINARY_DIR}/${macos_zip}.zip
      )
    else()
      add_custom_command(
        OUTPUT ${PROJECT_BINARY_DIR}/dist/${macos_app}/Contents/MacOS/Hypercoin-Qt
        COMMAND ${CMAKE_COMMAND} -E env OBJDUMP=${CMAKE_OBJDUMP} $<TARGET_FILE:Python3::Interpreter> ${PROJECT_SOURCE_DIR}/contrib/macdeploy/macdeployqtplus ${macos_app} -translations-dir=${QT_TRANSLATIONS_DIR}
        DEPENDS ${PROJECT_BINARY_DIR}/${macos_app}/Contents/MacOS/Hypercoin-Qt
        VERBATIM
      )
      add_custom_target(deploydir
        DEPENDS ${PROJECT_BINARY_DIR}/dist/${macos_app}/Contents/MacOS/Hypercoin-Qt
      )

      find_program(ZIP_EXECUTABLE zip)
      if(NOT ZIP_EXECUTABLE)
        add_custom_target(deploy
          COMMAND ${CMAKE_COMMAND} -E echo "Error: ZIP not found"
        )
      else()
        add_custom_command(
          OUTPUT ${PROJECT_BINARY_DIR}/dist/${macos_zip}.zip
          WORKING_DIRECTORY dist
          COMMAND ${PROJECT_SOURCE_DIR}/cmake/script/macos_zip.sh ${ZIP_EXECUTABLE} ${macos_zip}.zip
          VERBATIM
        )
        add_custom_target(deploy
          DEPENDS ${PROJECT_BINARY_DIR}/dist/${macos_zip}.zip
        )
      endif()
    endif()
    add_dependencies(deploydir hypercoin-qt)
    add_dependencies(deploy deploydir)
  endif()
endfunction()
