# NetSurf - the web browser (layer 3 application) and its libraries, built from the sources in
# third_party/netsurf (the browser) and third_party/netsurf-libs (libparserutils, libcss,
# libdom, curl, ...): "crtos setup --netsurf" fetches them (the group "netsurf" of
# third_party/sources.txt, with CRTOS' changes from patches/). Without them the browser is
# skipped. libjpeg and Mbed TLS come from the MCUXpresso SDK (third_party/nxp-sdk/middleware,
# the same group).
# NetSurf's framebuffer front end draws through a libnsfb surface on a CRTOS window
# (nsfb_crtos.c). tools/netsurf_prepare.py does what NetSurf's make rules would: it
# generates tables, images and the font, and puts the resources in /sd/crtos/share/netsurf.

set(NS ${CRTOS_TOP}/third_party/netsurf)
set(NSL ${CRTOS_TOP}/third_party/netsurf-libs)
set(MW ${CRTOS_TOP}/third_party/nxp-sdk/middleware)
if(NOT CRTOS_BUILD_NETSURF OR NOT EXISTS ${NS}/desktop/netsurf.c OR NOT EXISTS ${NSL}/libcss/src
   OR NOT EXISTS ${MW}/mbedtls/library)
  message(STATUS "NetSurf: not built (sources not found - 'crtos setup --netsurf' fetches them)")
  return()
endif()

set(NSGEN ${CMAKE_BINARY_DIR}/netsurf-gen)
execute_process(
  COMMAND ${Python3_EXECUTABLE} ${CRTOS_TOP}/tools/netsurf_prepare.py ${NSGEN} ${CRTOS_SDCARD}/share/netsurf
  RESULT_VARIABLE prep_result)
# a change of the generator or of what it reads for JavaScript (the bindings, the Web IDL, the
# scripts every page gets: NetSurf's, the polyfill library, ours in js/) configures again, and
# so runs it
file(GLOB ns_js_inputs ${NS}/content/handlers/javascript/duktape/*.bnd ${NS}/content/handlers/javascript/duktape/*.js
  ${NS}/content/handlers/javascript/WebIDL/*.idl ${NSL}/nsgenbind/src/*.c ${NSL}/polyfill/polyfill.js
  ${CMAKE_CURRENT_SOURCE_DIR}/js/*.js)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CRTOS_TOP}/tools/netsurf_prepare.py ${ns_js_inputs})
if(prep_result)
  message(FATAL_ERROR "NetSurf: tools/netsurf_prepare.py failed")
endif()

# Third-party code: its own warnings are not ours to fix; GCC 14 errors on old-style code
set(NS_CFLAGS ${CRTOS_APP_CFLAGS} -std=gnu99 -w
  -Wno-error=implicit-function-declaration -Wno-error=incompatible-pointer-types -Wno-error=int-conversion)
set(NS_DEFINES ${CRTOS_APP_DEFINES} __CRTOS__ _GNU_SOURCE _DEFAULT_SOURCE _BSD_SOURCE
  _POSIX_TIMERS=1 _POSIX_MONOTONIC_CLOCK=1)

# the sources a library's own Makefiles list
function(ns_sources var lib)
  execute_process(COMMAND ${Python3_EXECUTABLE} ${CRTOS_TOP}/tools/nsmake.py ${NSL}/${lib} ${ARGN}
    OUTPUT_VARIABLE out RESULT_VARIABLE r)
  if(r)
    message(FATAL_ERROR "NetSurf: cannot list the sources of ${lib}")
  endif()
  string(REPLACE "\n" ";" out "${out}")
  set(files "")
  foreach(f ${out})
    if(f)
      list(APPEND files ${NSL}/${lib}/${f})
    endif()
  endforeach()
  set(${var} ${files} PARENT_SCOPE)
endfunction()

# the libraries are built as their release variant (NDEBUG: no asserts, and no debug output
# such as libhubbub's tree builder states on stdout); the browser keeps its asserts
function(ns_lib name)
  cmake_parse_arguments(L "" "" "SOURCES;INCLUDES;DEFINES" ${ARGN})
  add_library(${name} STATIC ${L_SOURCES})
  target_compile_options(${name} PRIVATE ${NS_CFLAGS})
  target_include_directories(${name} PRIVATE ${CRTOS_APP_INCLUDES} ${L_INCLUDES})
  target_compile_definitions(${name} PRIVATE ${NS_DEFINES} ${L_DEFINES})
  if(NOT name STREQUAL "ns_core")
    target_compile_definitions(${name} PRIVATE NDEBUG)
  endif()
endfunction()

set(INC_PARSERUTILS ${NSL}/libparserutils/include)
set(INC_WAPCAPLET ${NSL}/libwapcaplet/include)
set(INC_HUBBUB ${NSL}/libhubbub/include)
set(INC_DOM ${NSL}/libdom/include)
set(INC_CSS ${NSL}/libcss/include)
set(INC_NSUTILS ${NSL}/libnsutils/include)
set(INC_NSPSL ${NSL}/libnspsl/include)
set(INC_NSBMP ${NSL}/libnsbmp/include)
set(INC_NSGIF ${NSL}/libnsgif/include)
set(INC_NSFB ${NSL}/libnsfb/include)
set(INC_UTF8PROC ${NSL}/libutf8proc)

# ---- libraries --------------------------------------------------------------------------------

ns_sources(src libparserutils)
ns_lib(ns_parserutils SOURCES ${src} INCLUDES ${INC_PARSERUTILS} ${NSL}/libparserutils/src
  DEFINES WITHOUT_ICONV_FILTER)

ns_sources(src libwapcaplet)
ns_lib(ns_wapcaplet SOURCES ${src} INCLUDES ${INC_WAPCAPLET} ${NSL}/libwapcaplet/src)

ns_sources(src libhubbub)
ns_lib(ns_hubbub SOURCES ${src} INCLUDES ${INC_HUBBUB} ${NSL}/libhubbub/src ${INC_PARSERUTILS})

# XML: expat (its lib/ directory; expat_config.h here), for libdom's XML parser binding
set(INC_EXPAT ${NSL}/libexpat/expat/lib)
ns_lib(ns_expat SOURCES ${INC_EXPAT}/xmlparse.c ${INC_EXPAT}/xmlrole.c ${INC_EXPAT}/xmltok.c
  INCLUDES ${CMAKE_CURRENT_SOURCE_DIR} ${INC_EXPAT} DEFINES HAVE_EXPAT_CONFIG_H XML_STATIC)

ns_sources(src libdom src bindings/hubbub)
list(APPEND src ${NSL}/libdom/bindings/xml/expat_xmlparser.c)
ns_lib(ns_dom SOURCES ${src} INCLUDES ${INC_DOM} ${NSL}/libdom/src
  ${INC_HUBBUB} ${INC_PARSERUTILS} ${INC_WAPCAPLET} ${INC_EXPAT} DEFINES XML_STATIC)

ns_sources(src libcss)
file(GLOB css_autogen ${NSL}/libcss/src/parse/properties/autogenerated_*.c)   # from properties.gen
list(APPEND src ${css_autogen})
ns_lib(ns_css SOURCES ${src} INCLUDES ${INC_CSS} ${NSL}/libcss/src ${INC_PARSERUTILS} ${INC_WAPCAPLET})
# libcss casts int expressions to a union with an int32_t member: newlib's int32_t is long
target_compile_options(ns_css PRIVATE -U__UINT32_TYPE__ -D__INT32_TYPE__=int
  "-D_ALIGNED=__attribute__((aligned))")   # (a macro nothing defines for this platform)

ns_lib(ns_nsutils SOURCES ${NSL}/libnsutils/src/base64.c ${NSL}/libnsutils/src/time.c
  INCLUDES ${INC_NSUTILS} ${NSL}/libnsutils/src)

ns_sources(src libnspsl)
ns_lib(ns_nspsl SOURCES ${src} INCLUDES ${INC_NSPSL} ${NSL}/libnspsl/src)

ns_sources(src libnsbmp)
ns_lib(ns_nsbmp SOURCES ${src} INCLUDES ${INC_NSBMP} ${NSL}/libnsbmp/src)

ns_sources(src libnsgif)
ns_lib(ns_nsgif SOURCES ${src} INCLUDES ${INC_NSGIF} ${NSL}/libnsgif/src
  DEFINES NSGIF_NAME=nsgif NSGIF_VERSION=1.0.0)

ns_sources(src libnsfb)
ns_lib(ns_nsfb SOURCES ${src} ${NSL}/libnsfb/src/surface/surface.c
  INCLUDES ${INC_NSFB} ${NSL}/libnsfb/src)

ns_lib(ns_utf8proc SOURCES ${NSL}/libutf8proc/utf8proc.c INCLUDES ${INC_UTF8PROC} DEFINES UTF8PROC_STATIC)

set(INC_ZLIB ${NSL}/zlib)
set(ZLIB_SOURCES adler32.c compress.c crc32.c deflate.c gzclose.c gzlib.c gzread.c gzwrite.c infback.c
  inffast.c inflate.c inftrees.c trees.c uncompr.c zutil.c)
list(TRANSFORM ZLIB_SOURCES PREPEND ${NSL}/zlib/)
ns_lib(ns_zlib SOURCES ${ZLIB_SOURCES} INCLUDES ${INC_ZLIB} DEFINES HAVE_UNISTD_H)

# images: PNG (libpng 1.6, its prebuilt default configuration), JPEG (IJG libjpeg 9 from the
# MCUXpresso SDK), WebP (libwebp's decoder, plain C: no SIMD or threads on the M7) and SVG
# (libsvgtiny, parsing through libdom's XML binding)
set(INC_PNG ${NSL}/libpng ${NSGEN}/png)
set(PNG_SOURCES png.c pngerror.c pngget.c pngmem.c pngpread.c pngread.c pngrio.c pngrtran.c pngrutil.c
  pngset.c pngtrans.c pngwio.c pngwrite.c pngwtran.c pngwutil.c)
list(TRANSFORM PNG_SOURCES PREPEND ${NSL}/libpng/)
ns_lib(ns_png SOURCES ${PNG_SOURCES} INCLUDES ${INC_PNG} ${INC_ZLIB} DEFINES PNG_ARM_NEON_OPT=0)

set(INC_JPEG ${MW}/libjpeg/inc ${MW}/libjpeg/template)
# (the library alone: src/ also has IJG's programs and other memory managers)
set(JPEG_SOURCES jaricom.c jcapimin.c jcapistd.c jcarith.c jccoefct.c jccolor.c jcdctmgr.c jchuff.c
  jcinit.c jcmainct.c jcmarker.c jcmaster.c jcomapi.c jcparam.c jcprepct.c jcsample.c jctrans.c
  jdapimin.c jdapistd.c jdarith.c jdatadst.c jdatasrc.c jdcoefct.c jdcolor.c jddctmgr.c jdhuff.c
  jdinput.c jdmainct.c jdmarker.c jdmaster.c jdmerge.c jdpostct.c jdsample.c jdtrans.c jerror.c
  jfdctflt.c jfdctfst.c jfdctint.c jidctflt.c jidctfst.c jidctint.c jmemmgr.c jmemnobs.c jquant1.c
  jquant2.c jutils.c)
list(TRANSFORM JPEG_SOURCES PREPEND ${MW}/libjpeg/src/)
ns_lib(ns_jpeg SOURCES ${JPEG_SOURCES} INCLUDES ${INC_JPEG})

set(INC_WEBP ${NSL}/libwebp/src)
set(WEBP_SOURCES
  dec/alpha_dec.c dec/buffer_dec.c dec/frame_dec.c dec/idec_dec.c dec/io_dec.c dec/quant_dec.c dec/tree_dec.c
  dec/vp8_dec.c dec/vp8l_dec.c dec/webp_dec.c
  dsp/alpha_processing.c dsp/cpu.c dsp/dec.c dsp/dec_clip_tables.c dsp/filters.c dsp/lossless.c dsp/rescaler.c
  dsp/upsampling.c dsp/yuv.c
  utils/bit_reader_utils.c utils/color_cache_utils.c utils/filters_utils.c utils/huffman_utils.c utils/palette.c
  utils/quant_levels_dec_utils.c utils/rescaler_utils.c utils/random_utils.c utils/thread_utils.c utils/utils.c)
list(TRANSFORM WEBP_SOURCES PREPEND ${NSL}/libwebp/src/)
ns_lib(ns_webp SOURCES ${WEBP_SOURCES} INCLUDES ${NSL}/libwebp ${INC_WEBP})

set(INC_SVGTINY ${NSL}/libsvgtiny/include)
ns_sources(src libsvgtiny)
ns_lib(ns_svgtiny SOURCES ${src} INCLUDES ${INC_SVGTINY} ${NSL}/libsvgtiny/src ${NSGEN}/include
  ${INC_DOM} ${INC_WAPCAPLET} ${INC_PARSERUTILS} ${INC_HUBBUB})

# TLS: Mbed TLS 3.6 (from the MCUXpresso SDK) as crtos_mbedtls_config.h sets it up
set(INC_MBEDTLS ${MW}/mbedtls/include)
set(MBEDTLS_CONFIG MBEDTLS_CONFIG_FILE="crtos_mbedtls_config.h")
file(GLOB MBEDTLS_SOURCES ${MW}/mbedtls/library/*.c)
ns_lib(ns_mbedtls SOURCES ${MBEDTLS_SOURCES} ${CMAKE_CURRENT_SOURCE_DIR}/tls_crtos.c
  INCLUDES ${CMAKE_CURRENT_SOURCE_DIR} ${INC_MBEDTLS} ${MW}/mbedtls/library DEFINES ${MBEDTLS_CONFIG})

# HTTP and HTTPS: libcurl 8.11 as curl_config.h sets it up
file(GLOB CURL_SOURCES ${NSL}/curl/lib/*.c ${NSL}/curl/lib/vauth/*.c ${NSL}/curl/lib/vtls/*.c
  ${NSL}/curl/lib/vquic/*.c)
ns_lib(ns_curl SOURCES ${CURL_SOURCES}
  INCLUDES ${CMAKE_CURRENT_SOURCE_DIR} ${NSL}/curl/include ${NSL}/curl/lib ${INC_MBEDTLS} ${INC_ZLIB}
  DEFINES HAVE_CONFIG_H BUILDING_LIBCURL CURL_STATICLIB ${MBEDTLS_CONFIG})

# iconv on libparserutils' charset tables
ns_lib(ns_port SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/iconv.c
  INCLUDES ${INC_PARSERUTILS} ${NSL}/libparserutils/src/charset/codecs)

# ---- the browser ---------------------------------------------------------------------------------

set(NS_CORE
  content/content.c content/content_factory.c content/fetch.c content/hlcache.c content/llcache.c
  content/mimesniff.c content/textsearch.c content/urldb.c content/no_backing_store.c
  content/fetchers/data.c content/fetchers/resource.c content/fetchers/curl.c
  content/fetchers/about/about.c content/fetchers/about/blank.c content/fetchers/about/certificate.c
  content/fetchers/about/chart.c content/fetchers/about/choices.c content/fetchers/about/config.c
  content/fetchers/about/imagecache.c content/fetchers/about/nscolours.c content/fetchers/about/query.c
  content/fetchers/about/query_auth.c content/fetchers/about/query_fetcherror.c
  content/fetchers/about/query_privacy.c content/fetchers/about/query_timeout.c
  content/fetchers/about/testament.c content/fetchers/about/websearch.c
  content/fetchers/file/dirlist.c content/fetchers/file/file.c
  content/handlers/css/css.c content/handlers/css/dump.c content/handlers/css/internal.c
  content/handlers/css/hints.c content/handlers/css/select.c
  content/handlers/html/box_construct.c content/handlers/html/box_inspect.c
  content/handlers/html/box_manipulate.c content/handlers/html/box_normalise.c
  content/handlers/html/box_special.c content/handlers/html/box_textarea.c content/handlers/html/css.c
  content/handlers/html/css_fetcher.c content/handlers/html/dom_event.c content/handlers/html/font.c
  content/handlers/html/form.c content/handlers/html/forms.c content/handlers/html/html.c
  content/handlers/html/imagemap.c content/handlers/html/interaction.c content/handlers/html/layout.c
  content/handlers/html/layout_flex.c content/handlers/html/object.c content/handlers/html/redraw.c
  content/handlers/html/redraw_border.c content/handlers/html/script.c content/handlers/html/table.c
  content/handlers/html/textselection.c
  content/handlers/image/image.c content/handlers/image/image_cache.c content/handlers/image/bmp.c
  content/handlers/image/gif.c content/handlers/image/ico.c content/handlers/image/png.c
  content/handlers/image/jpeg.c content/handlers/image/svg.c content/handlers/image/webp.c
  content/handlers/text/textplain.c
  content/handlers/javascript/fetcher.c content/handlers/javascript/content.c
  content/handlers/javascript/duktape/dukky.c
  utils/bloom.c utils/corestrings.c utils/file.c utils/filepath.c utils/hashmap.c utils/hashtable.c
  utils/idna.c utils/libdom.c utils/log.c utils/messages.c utils/nscolour.c utils/nsoption.c
  utils/punycode.c utils/ssl_certs.c utils/talloc.c utils/time.c utils/url.c utils/useragent.c
  utils/utf8.c utils/utils.c
  utils/http/challenge.c utils/http/generics.c utils/http/primitives.c utils/http/parameter.c
  utils/http/cache-control.c utils/http/content-disposition.c utils/http/content-type.c
  utils/http/strict-transport-security.c utils/http/www-authenticate.c
  utils/nsurl/nsurl.c utils/nsurl/parse.c
  desktop/cookie_manager.c desktop/knockout.c desktop/hotlist.c desktop/mouse.c desktop/plot_style.c
  desktop/print.c desktop/search.c desktop/searchweb.c desktop/scrollbar.c desktop/textarea.c
  desktop/version.c desktop/system_colour.c desktop/local_history.c desktop/global_history.c
  desktop/treeview.c desktop/page-info.c
  desktop/bitmap.c desktop/browser.c desktop/browser_window.c desktop/browser_history.c
  desktop/download.c desktop/frames.c desktop/netsurf.c desktop/cw_helper.c desktop/save_complete.c
  desktop/save_text.c desktop/selection.c desktop/textinput.c desktop/gui_factory.c desktop/save_pdf.c
  desktop/font_haru.c
  frontends/framebuffer/gui.c frontends/framebuffer/framebuffer.c frontends/framebuffer/schedule.c
  frontends/framebuffer/bitmap.c frontends/framebuffer/fetch.c frontends/framebuffer/findfile.c
  frontends/framebuffer/corewindow.c frontends/framebuffer/local_history.c
  frontends/framebuffer/clipboard.c frontends/framebuffer/font_internal.c
  frontends/framebuffer/fbtk/fbtk.c frontends/framebuffer/fbtk/event.c frontends/framebuffer/fbtk/fill.c
  frontends/framebuffer/fbtk/bitmap.c frontends/framebuffer/fbtk/user.c frontends/framebuffer/fbtk/window.c
  frontends/framebuffer/fbtk/text.c frontends/framebuffer/fbtk/scroll.c frontends/framebuffer/fbtk/osk.c)
list(TRANSFORM NS_CORE PREPEND ${NS}/)

file(STRINGS ${NSGEN}/images.txt ns_images)
set(NS_GENERATED ${NSGEN}/font-ns-sans.c)
foreach(i ${ns_images})
  list(APPEND NS_GENERATED ${NSGEN}/image-${i}.c)
endforeach()

# string settings NetSurf's make rules pass on the command line
file(WRITE ${NSGEN}/crtos_config.h.tmp
  "/* NetSurf build settings for CRTOS (apps/netsurf/CMakeLists.txt) */\n"
  "#define NETSURF_FB_RESPATH \"/sd/crtos/share/netsurf\"\n"
  "#define NETSURF_FB_FONTPATH \"/sd/crtos/share/fonts\"\n"
  "#define NETSURF_UA_FORMAT_STRING \"Mozilla/5.0 (%s) NetSurf/%d.%d\"\n"
  "#define NETSURF_HOMEPAGE \"about:welcome\"\n"
  "#define NETSURF_BUILTIN_LOG_FILTER \"(level:WARNING || cat:jserrors)\"\n"
  "#define NETSURF_BUILTIN_VERBOSE_FILTER \"(level:VERBOSE || cat:jserrors)\"\n"
  "/* bigger images are decoded at 1/2, 1/4 or 1/8 of their size (image_decode_scale()) */\n"
  "#define NETSURF_IMAGE_MAX_BYTES (4 * 1024 * 1024)\n"
  "/* pages are rendered this wide for their history thumbnails, twice the thumbnail's 116 px\n"
  " * (not 1024: 3.6 MB at once; 480 still took 800 KB in one piece of a full heap) */\n"
  "#define FB_THUMBNAIL_RENDER_WIDTH 232\n")
configure_file(${NSGEN}/crtos_config.h.tmp ${NSGEN}/crtos_config.h COPYONLY)

# its main() becomes nsfb_main(), started by main.c
set_source_files_properties(${NS}/frontends/framebuffer/gui.c PROPERTIES COMPILE_DEFINITIONS main=nsfb_main)

set(NS_CORE_INCLUDES ${NS} ${NS}/include ${NS}/frontends ${NS}/content/handlers ${NSGEN} ${NSGEN}/include
  ${NS}/content/handlers/javascript/duktape ${NSL}/curl/include
  ${INC_PARSERUTILS} ${INC_WAPCAPLET} ${INC_HUBBUB} ${INC_DOM} ${INC_CSS} ${INC_NSUTILS} ${INC_NSPSL}
  ${INC_NSBMP} ${INC_NSGIF} ${INC_NSFB} ${INC_UTF8PROC} ${INC_ZLIB} ${INC_PNG} ${INC_JPEG} ${INC_SVGTINY} ${INC_WEBP}
  ${CRTOS_TOP}/system/lib/libgfx/include)
set(NS_CORE_DEFINES nsframebuffer small NETSURF_LOG_LEVEL=INFO STMTEXPR=1 CURL_STATICLIB
  WITH_CURL WITH_BMP WITH_GIF WITH_PNG WITH_JPEG WITH_WEBP WITH_NS_SVG WITH_UTF8PROC WITH_NSPSL UTF8PROC_STATIC)
ns_lib(ns_core SOURCES ${NS_CORE} ${NS_GENERATED} INCLUDES ${NS_CORE_INCLUDES} DEFINES ${NS_CORE_DEFINES})
target_compile_options(ns_core PRIVATE -include ${NSGEN}/crtos_config.h)

# JavaScript: the Duktape engine (ECMAScript 5.1 and parts of 2015) and the DOM bindings that
# nsgenbind made from the .bnd files (netsurf_prepare.py; one source per Web IDL interface),
# optimised for size - most of them are small functions that pages seldom call
file(STRINGS ${NSGEN}/duktape/sources.txt ns_js_gen)
list(TRANSFORM ns_js_gen PREPEND ${NSGEN}/duktape/)
ns_lib(ns_js SOURCES ${NS}/content/handlers/javascript/duktape/duktape.c ${ns_js_gen}
  INCLUDES ${NS_CORE_INCLUDES} DEFINES ${NS_CORE_DEFINES})
target_compile_options(ns_js PRIVATE -Os -include ${NSGEN}/crtos_config.h)

# the browser's settings for this board (the certificates HTTPS trusts are in
# rootfs/etc/ssl/cacert.pem; curl_config.h names the path)
crtos_file(${CMAKE_CURRENT_SOURCE_DIR}/Choices share/netsurf/Choices)

# The heap: 8.25 MB, so that the arena (program image 3.8 MB with JavaScript and the scripts every
# page runs first, heap, stack) stays at 12 MB - six 2 MB subregions of a 16 MB MPU region; a
# larger one needed a 16 MB arena in 4 MB subregions, which a fragmented SDRAM often cannot give. The malloc is libcrtosheap's (a TLSF,
# kept in the arena: main.c), not newlib's first-fit list, which scripts spent most time in.
# libnsfb's surfaces register themselves from constructors: nothing refers to them, so they are
# linked as objects of the program rather than taken from a library - ours for the window, and
# the RAM surface that holds every bitmap (decoded images)
crtos_app(netsurf SOURCES main.c nsfb_crtos.c ${NSL}/libnsfb/src/surface/ram.c DEST apps STACK 65536 HEAP 8650752 ICON icon.png
  INCLUDES ${CRTOS_TOP}/system/lib/libgfx/include ${INC_NSFB} ${NSL}/libnsfb/src
  LIBS ns_core ns_js ns_nsfb ns_css ns_dom ns_hubbub ns_parserutils ns_wapcaplet ns_nsutils ns_nspsl ns_nsbmp
    ns_nsgif ns_svgtiny ns_png ns_jpeg ns_webp ns_expat ns_utf8proc ns_port ns_curl ns_mbedtls ns_zlib gfx crtosheap)
