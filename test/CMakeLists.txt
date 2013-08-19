INCLUDE_DIRECTORIES("../src")

MACRO(AddTest Name)
	ADD_EXECUTABLE("${Name}" "${Name}.cpp")
	TARGET_LINK_LIBRARIES("${Name}" "vman")
ENDMACRO()

AddTest("test")
AddTest("path")
AddTest("volume")
AddTest("chunk")
AddTest("access")

ADD_EXECUTABLE("benchmark" "benchmark.cpp" "${InihSource}/ini.c")
TARGET_LINK_LIBRARIES("benchmark" "vman")
