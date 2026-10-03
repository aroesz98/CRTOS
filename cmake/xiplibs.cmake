# The runtime libraries of programs that run in place (crtos_app XIP, gcc -mxip): lib/xip/libc.a,
# libm.a, libgcc.a, libstdc++.a and libsupc++.a (and the _nano names of the C++ ones, which
# crtos.specs links), position independent with the data addressed through r9. The build
# compiles them with the Arm GNU Toolchain it uses: each object from the newlib and GCC sources of
# third_party/ with the options toolchain/xiplibs/recipe.txt gives it (tools/xiplibs_recipe.py
# took them from newlib's and GCC's own build) - about 3000 objects, the first time.
#
#   crtos_xiplibs(<lib/xip directory>)
#       the rules; the libraries go to the global property CRTOS_XIP_FILES (what an XIP
#       program's link waits for), and CRTOS_XIP_OK is OFF when they cannot be made with this
#       compiler (another GCC version than the recipe's: XIP programs are left out then)

function(crtos_xiplibs xipdir)
  set(recipe ${CRTOS_TOP}/toolchain/xiplibs/recipe.txt)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${recipe})
  file(STRINGS ${recipe} lines)
  set(B ${CMAKE_BINARY_DIR}/xiplibs)
  get_filename_component(T "${CRTOS_GCC_BIN}" DIRECTORY)
  set(from @N @G @X @T @B)
  set(to "${CRTOS_TOP}/third_party/newlib" "${CRTOS_TOP}/third_party/gcc" "${CRTOS_TOP}/toolchain/xiplibs/gen"
         "${T}" "${B}")

  # the sources: entries newlib and gcc of third_party/sources.txt (thirdparty.py fetched them
  # at the start of the configuration)
  foreach(f third_party/newlib/newlib/libc/include/stdio.h third_party/gcc/libgcc/libgcc2.c)
    if(NOT EXISTS ${CRTOS_TOP}/${f})
      message(FATAL_ERROR "XIP libraries: ${f} is missing - third_party/sources.txt must have the "
                          "entries newlib and gcc ('python tools/thirdparty.py --status' shows them)")
    endif()
  endforeach()

  # the recipe is for one GCC version (its sources, its generated headers in plugin/include)
  list(FIND lines "version ${CMAKE_C_COMPILER_VERSION}" at)
  if(at EQUAL -1)
    list(FILTER lines INCLUDE REGEX "^version ")
    message(WARNING "XIP programs are left out: the recipe of their libraries (toolchain/xiplibs) is "
                    "for GCC ${lines}, the compiler is ${CMAKE_C_COMPILER_VERSION}")
    set_property(GLOBAL PROPERTY CRTOS_XIP_OK OFF)
    return()
  endif()

  # __FILE__ (assert messages) relative to the repository: the same libraries wherever it is;
  # no warnings (other projects' code, as their own builds have it)
  set(prefix_map -fmacro-prefix-map=${CRTOS_TOP}/= -w)
  set(gens "")
  set(libs "")
  set(dir "")
  set(lib "")
  foreach(line IN LISTS lines)
    if(line STREQUAL "" OR line MATCHES "^#")
      continue()
    endif()
    # the words first, then the paths (which may have spaces)
    string(REPLACE " " ";" w "${line}")
    foreach(f t IN ZIP_LISTS from to)
      string(REPLACE "${f}" "${t}" w "${w}")
    endforeach()
    list(POP_FRONT w kind)

    if(kind STREQUAL "copy")
      # copy <file> <file>, or copy <dir>/ <dir>/ <name...>
      list(POP_FRONT w dest src)
      if(w)
        foreach(n IN LISTS w)
          configure_file("${src}${n}" "${dest}${n}" COPYONLY)
        endforeach()
      else()
        configure_file("${src}" "${dest}" COPYONLY)
      endif()

    elseif(kind STREQUAL "gen")
      list(POP_FRONT w out step src)
      get_filename_component(d "${out}" DIRECTORY)
      file(MAKE_DIRECTORY "${d}")
      if(step STREQUAL "tzdata")
        add_custom_command(OUTPUT ${out}
          COMMAND ${Python3_EXECUTABLE} ${CRTOS_TOOLS}/xiplibs.py tzdata ${src} ${out}
          DEPENDS ${src} ${CRTOS_TOOLS}/xiplibs.py
          VERBATIM)
      else()
        add_custom_command(OUTPUT ${out}
          COMMAND ${CMAKE_C_COMPILER} ${w} ${prefix_map} -MD -MF ${out}.d -S ${src} -o ${out}.tmp
          COMMAND ${Python3_EXECUTABLE} ${CRTOS_TOOLS}/xiplibs.py ${step} ${out}.tmp ${out}
          DEPENDS ${src} ${CRTOS_TOOLS}/xiplibs.py
          DEPFILE ${out}.d
          VERBATIM)
      endif()
      list(APPEND gens ${out})

    elseif(kind STREQUAL "lib")
      list(GET w 0 lib)
      list(APPEND libs ${lib})
      set(objs_${lib} "")
      file(MAKE_DIRECTORY ${B}/obj/${lib})

    elseif(kind STREQUAL "set")
      list(POP_FRONT w name)
      set(set_${name} "${w}")

    elseif(kind STREQUAL "dir")
      list(GET w 0 dir)

    elseif(kind STREQUAL "cc")
      # cc <object> <source> [$set] [own option...]: the set's options with the own ones at %
      list(POP_FRONT w obj src)
      if(NOT IS_ABSOLUTE "${src}")
        set(src "${dir}/${src}")
      endif()
      set(opts "${w}")
      list(LENGTH w n)
      if(n GREATER 0)
        list(GET w 0 first)
        if(first MATCHES "^\\$(.*)")
          list(POP_FRONT w)
          set(opts "${set_${CMAKE_MATCH_1}}")
          list(FIND opts "%" at)
          list(REMOVE_AT opts ${at})
          if(w)
            list(INSERT opts ${at} ${w})
          endif()
        endif()
      endif()
      set(o ${B}/obj/${lib}/${obj})
      add_custom_command(OUTPUT ${o}
        COMMAND ${CMAKE_C_COMPILER} ${opts} ${prefix_map} -MD -MF ${o}.d -c ${src} -o ${o}
        DEPENDS ${src} ${gens}
        DEPFILE ${o}.d
        VERBATIM)
      list(APPEND objs_${lib} ${o})

    elseif(kind STREQUAL "alias")
      list(POP_FRONT w name of)
      set(alias_${name} ${of})
      list(APPEND aliases ${name})
    endif()
  endforeach()

  # the archives (deterministic: no time stamps, no owners), the objects listed in a file
  set(files "")
  foreach(lib IN LISTS libs)
    set(a ${xipdir}/${lib}.a)
    list(JOIN objs_${lib} "\"\n\"" list)
    file(WRITE ${B}/${lib}.objects.new "\"${list}\"\n")
    configure_file(${B}/${lib}.objects.new ${B}/${lib}.objects COPYONLY)
    add_custom_command(OUTPUT ${a}
      COMMAND ${CMAKE_COMMAND} -E make_directory ${xipdir}
      COMMAND ${CMAKE_COMMAND} -E rm -f ${a}
      COMMAND ${CMAKE_AR} rcsD ${a} @${B}/${lib}.objects
      DEPENDS ${objs_${lib}} ${B}/${lib}.objects
      COMMENT "XIP library lib/xip/${lib}.a"
      VERBATIM)
    list(APPEND files ${a})
  endforeach()
  foreach(name IN LISTS aliases)
    add_custom_command(OUTPUT ${xipdir}/${name}.a
      COMMAND ${CMAKE_COMMAND} -E copy ${xipdir}/${alias_${name}}.a ${xipdir}/${name}.a
      DEPENDS ${xipdir}/${alias_${name}}.a
      VERBATIM)
    list(APPEND files ${xipdir}/${name}.a)
  endforeach()
  set_property(GLOBAL PROPERTY CRTOS_XIP_FILES ${files})
  set_property(GLOBAL PROPERTY CRTOS_XIP_OK ON)
endfunction()
