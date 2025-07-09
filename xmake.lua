add_rules("mode.debug", "mode.release")
set_optimize("fastest")
set_languages("c99")
set_warnings("all", "extra")
set_policy("run.autobuild", true)

target("pleditor")
    set_kind("binary")
    add_files("src/*.c")
    
    -- 根据操作系统选择平台实现文件
    if is_plat("windows") then
        add_files("src/platform/windows.c")
    else
        add_files("src/platform/linux.c")
    end

    on_run(function(target)
        import("core.base.option")
        local args = option.get("arguments")
        os.execv(target:targetfile(), args)
    end)