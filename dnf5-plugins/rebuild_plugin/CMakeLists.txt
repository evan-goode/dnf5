# set gettext domain for translations
add_definitions(-DGETTEXT_DOMAIN=\"dnf5_cmd_rebuild\")

file(GLOB REBUILD_SOURCES *.cpp)
add_library(rebuild_cmd_plugin MODULE ${REBUILD_SOURCES})

set_target_properties(rebuild_cmd_plugin PROPERTIES PREFIX "")

pkg_check_modules(LIBPKGMANIFEST REQUIRED libpkgmanifest)
target_link_directories(rebuild_cmd_plugin PRIVATE ${LIBPKGMANIFEST_LIBRARY_DIRS})
target_link_libraries(rebuild_cmd_plugin PRIVATE pkgmanifest)

pkg_check_modules(JSONC REQUIRED json-c)
include_directories(${JSONC_INCLUDE_DIRS})
target_link_libraries(rebuild_cmd_plugin PRIVATE ${JSONC_LIBRARIES})

target_link_libraries(rebuild_cmd_plugin PRIVATE dnf5 libdnf5 libdnf5-cli)

install(TARGETS rebuild_cmd_plugin LIBRARY DESTINATION ${CMAKE_INSTALL_FULL_LIBDIR}/dnf5/plugins/)
