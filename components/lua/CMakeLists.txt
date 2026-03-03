idf_component_register(
  SRCS
    "src/lapi.c"
    "src/lcode.c"
    "src/lctype.c"
    "src/ldebug.c"
    "src/ldo.c"
    "src/ldump.c"
    "src/lfunc.c"
    "src/lgc.c"
    "src/llex.c"
    "src/lmem.c"
    "src/lobject.c"
    "src/lopcodes.c"
    "src/lparser.c"
    "src/lstate.c"
    "src/lstring.c"
    "src/ltable.c"
    "src/ltm.c"
    "src/lundump.c"
    "src/lvm.c"
    "src/lzio.c"
    "src/lauxlib.c"
    "src/lbaselib.c"
    "src/lcorolib.c"
    "src/ldblib.c"
    "src/lmathlib.c"
    "src/lstrlib.c"
    "src/ltablib.c"
    "src/lutf8lib.c"
  INCLUDE_DIRS
    "src"
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-extra-semi)
target_compile_definitions(${COMPONENT_LIB} PUBLIC LUA_USE_C89)
