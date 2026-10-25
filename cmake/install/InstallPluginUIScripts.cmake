# Install-time step for the packaged QGroundControl.PluginUI module, run by install(SCRIPT) in
# InstallPluginSDK.cmake with QGC_PLUGIN_UI_QML_BODIES set.
#
# A control body can import a JavaScript file by relative path (QGCGroupBox.qml imports
# QGCGroupBox.js). The roster lists only the .qml bodies, so without this step the SDK would ship
# a body whose import cannot resolve, and the type would fail to load for anything that compiles
# it against the SDK. Runs at install time because the bodies exist only once the build has
# materialised the QML modules.

set(_qgc_plugin_ui_dest "${CMAKE_INSTALL_PREFIX}/qml/QGroundControl/PluginUI")
foreach(_qgc_body IN LISTS QGC_PLUGIN_UI_QML_BODIES)
    file(STRINGS "${_qgc_body}" _qgc_imports REGEX "^import \"[^\"]+\\.js\"")
    foreach(_qgc_import IN LISTS _qgc_imports)
        string(REGEX REPLACE "^import \"([^\"]+\\.js)\".*" "\\1" _qgc_script "${_qgc_import}")
        get_filename_component(_qgc_body_dir "${_qgc_body}" DIRECTORY)
        set(_qgc_script_path "${_qgc_body_dir}/${_qgc_script}")
        if(NOT EXISTS "${_qgc_script_path}")
            message(FATAL_ERROR "QGC: ${_qgc_body} imports ${_qgc_script}, which does not exist")
        endif()
        file(INSTALL "${_qgc_script_path}" DESTINATION "${_qgc_plugin_ui_dest}")
    endforeach()
endforeach()
