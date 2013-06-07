# STRICT_CXX_FLAGS
# LENIENT_CXX_FLAGS

SET(CommonFlags ${CMAKE_CXX_FLAGS})

IF(MSVC)
	# Strict flags use W4
	IF(CommonFlags MATCHES "/W[0-4]")
		STRING(REGEX REPLACE "/W[0-4]" "/W4" STRICT_CXX_FLAGS "${CommonFlags}")
	ELSE()
		SET(STRICT_CXX_FLAGS "${CommonFlags} /W4")
	ENDIF()

	# Lenient flags use W0
	IF(CommonFlags MATCHES "/W[0-4]")
		STRING(REGEX REPLACE "/W[0-4]" "/W0" LENIENT_CXX_FLAGS "${CommonFlags}")
	ELSE()
		SET(STRICT_CXX_FLAGS "${CommonFlags} /W0")
	ENDIF()
ELSE()
#	---- -Wall ----
# 	-Waddress
# 	-Wc++11-compat
# 	-Wchar-subscripts
# 	-Wcomment
# 	-Wformat
# 	-Wmaybe-uninitialized
# 	-Wnonnull
# 	-Wparentheses
# 	-Wpointer-sign
# 	-Wreorder
# 	-Wreturn-type
# 	-Wsequence-point
# 	-Wsign-compare
# 	-Wstrict-aliasing
# 	-Wstrict-overflow=1
# 	-Wswitch
# 	-Wtrigraphs
# 	-Wuninitialized
# 	-Wunknown-pragmas
# 	-Wunused-function
# 	-Wunused-label
# 	-Wunused-value
# 	-Wunused-variable
# 	-Wvolatile-register-var

#	---- -Wextra ----
# 	-Wclobbered
# 	-Wempty-body
# 	-Wignored-qualifiers
# 	-Wmissing-field-initializers
# 	-Wmissing-parameter-type (C only)
# 	-Wold-style-declaration (C only)
# 	-Woverride-init
# 	-Wsign-compare
# 	-Wtype-limits
# 	-Wuninitialized
# 	-Wunused-parameter (only with -Wunused or -Wall)
# 	-Wunused-but-set-parameter (only with -Wunused or -Wall)

	SET(SharedFlags "-Wno-unknown-pragmas")
	
	SET(STRICT_CXX_FLAGS "${CommonFlags} -Werror -Wall -Wextra -pedantic ${SharedFlags}")
	SET(STRICT_CXX_FLAGS "${STRICT_CXX_FLAGS} -Wmissing-include-dirs -Wpointer-arith -Winit-self")
	SET(STRICT_CXX_FLAGS "${STRICT_CXX_FLAGS} -Wno-variadic-macros -Wno-long-long -Wno-sign-compare -Wno-unused-parameter -Wno-unused-variable")

	SET(LENIENT_CXX_FLAGS "${CommonFlags} ${SharedFlags}")
ENDIF()
