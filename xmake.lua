add_rules("mode.debug", "mode.release")
set_optimize("fastest")
set_languages("c99")
set_warnings("all", "extra")

target("pleditor")
    set_kind("binary")
    add_files("src/*.c")
    add_files("src/platform/*.c")
