# Legt das fertige Plugin in den Ausgabeordner und entfernt dort aeltere Staende.
# Aufruf aus CMakeLists.txt nach dem Bauen:
#   cmake -DOC_MODULE=<Datei> -DOC_DEST_DIR=<Ordner> -DOC_PREFIX=opencirt -DOC_SUFFIX=<.brx|.lrx> -P copy_plugin.cmake
if(NOT OC_MODULE OR NOT OC_DEST_DIR OR NOT OC_PREFIX OR NOT OC_SUFFIX)
    message(FATAL_ERROR "copy_plugin.cmake: OC_MODULE, OC_DEST_DIR, OC_PREFIX und OC_SUFFIX angeben")
endif()
if(NOT EXISTS "${OC_MODULE}")
    message(FATAL_ERROR "Plugin nicht gefunden: ${OC_MODULE}")
endif()
file(MAKE_DIRECTORY "${OC_DEST_DIR}")
get_filename_component(_name "${OC_MODULE}" NAME)
# Vorhandene Dateien werden geloescht, nicht ueberschrieben: eine Datei, die
# ein laufendes BricsCAD geladen hat, darf nicht an Ort und Stelle veraendert
# werden. Nach dem Loeschen behaelt BricsCAD seinen alten Stand.
file(GLOB _old "${OC_DEST_DIR}/${OC_PREFIX}*${OC_SUFFIX}")
foreach(_f IN LISTS _old)
    get_filename_component(_n "${_f}" NAME)
    file(REMOVE "${_f}")
    if(NOT _n STREQUAL _name)
        message(STATUS "Aelterer Stand entfernt: ${_n}")
    endif()
endforeach()
file(COPY "${OC_MODULE}" DESTINATION "${OC_DEST_DIR}")
message(STATUS "Abgelegt: ${OC_DEST_DIR}/${_name}")
