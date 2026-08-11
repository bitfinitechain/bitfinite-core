packages:=boost openssl libevent

protobuf_native_packages = native_protobuf
protobuf_packages = protobuf

qt_packages = qrencode zlib

qt_linux_packages:=qt expat libxcb xcb_proto libXau xproto freetype fontconfig libxkbcommon libxcb_util libxcb_util_render libxcb_util_keysyms libxcb_util_image libxcb_util_wm

rapidcheck_packages = rapidcheck

qt_darwin_packages=qt
qt_mingw32_packages=qt

wallet_packages=bdb

zmq_packages=zeromq

upnp_packages=miniupnpc

darwin_native_packages = native_biplist native_ds_store native_mac_alias

# Boost 1.77 builds with its own b2 rather than the in-tree bootstrap. Needed
# because C++20 removed std::allocator<T>::pointer, which Boost 1.70's
# signals2 still used.
$(host_arch)_$(host_os)_native_packages += native_b2

ifneq ($(build_os),darwin)
darwin_native_packages += native_cctools native_cdrkit native_libdmg-hfsplus
endif

jemalloc_packages = jemalloc
