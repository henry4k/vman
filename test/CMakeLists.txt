INCLUDE_DIRECTORIES("../src")

MACRO(AddTest Name)
	ADD_EXECUTABLE("${Name}" "${Name}.cpp")
	TARGET_LINK_LIBRARIES("${Name}" "vman")
ENDMACRO()

AddTest("test")
AddTest("world")
AddTest("chunk")
AddTest("access")
AddTest("benchmark")
