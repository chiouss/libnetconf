set(SRCS_TRANSPORT ssh.c)
if(ENABLE_TLS)
	list(APPEND SRCS_TRANSPORT tls.c)
endif()

set(SRCS
	callbacks.c
	error.c
	internal.c
	compat.c
	messages.c
	session.c
	transport.c
	${SRCS_TRANSPORT}
	notifications.c
	with_defaults.c
	url.c
	nacm.c
	datastore.c
	datastore/edit_config.c
	datastore/empty/datastore_empty.c
	datastore/file/datastore_file.c
	datastore/custom/datastore_custom.c
	transapi/transapi.c
	transapi/yinparser.c
	transapi/xmldiff.c
)

set(SRC_HDRS
	netconf.h
	callbacks.h
	callbacks_ssh.h
	error.h
	session.h
	messages.h
	messages_xml.h
	transport.h
	callhome.h
	notifications.h
	notifications_xml.h
	with_defaults.h
	url.h
	datastore.h
	datastore_xml.h
	datastore/custom/datastore_custom.h
	transapi.h
)

add_library(netconf SHARED ${SRCS})
set_target_properties(
	netconf PROPERTIES
	VERSION ${LT_VERSION}
	SOVERSION ${LT_SOVERSION}
	C_VISIBILITY_PRESET hidden
	POSITION_INDEPENDENT_CODE ON
)
target_include_directories(netconf PUBLIC "${CMAKE_SOURCE_DIR}" "${CMAKE_SOURCE_DIR}/src")
install(TARGETS netconf LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}")
install(FILES ${SRC_HDRS} DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/libnetconf")
install(
	DIRECTORY DESTINATION "${NC_WORKINGDIR_PATH}"
	DIRECTORY_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE
			      GROUP_READ GROUP_WRITE GROUP_EXECUTE
			      WORLD_READ WORLD_WRITE WORLD_EXECUTE
)
