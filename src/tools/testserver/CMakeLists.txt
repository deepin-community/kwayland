add_executable(org-kde-kf5-kwayland-testserver main.cpp testserver.cpp)
target_link_libraries(org-kde-kf5-kwayland-testserver Qt${QT_MAJOR_VERSION}::Core KF5::WaylandServer)
install(TARGETS org-kde-kf5-kwayland-testserver DESTINATION ${KDE_INSTALL_LIBEXECDIR} )
