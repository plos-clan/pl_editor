add_rules("mode.debug", "mode.release")

target("pleditor")
    set_kind("binary")
    add_files("src/*.c")
    add_files("src/platform/*.c")
    if is_plat("linux") then
        add_syslinks("m")
    end
