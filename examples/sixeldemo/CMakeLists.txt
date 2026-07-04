add_executable(sixeldemo sixeldemo.cpp formulaplot.cpp moonphase.cpp)
target_link_libraries(sixeldemo PUBLIC ${PROJECT_NAME})
tv_app_common(sixeldemo)
