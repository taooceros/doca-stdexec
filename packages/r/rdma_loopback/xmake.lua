package("rdma_loopback")
    set_kind("binary")
    set_description("The rdma_loopback package")
    add_deps("doca-stdexec")
    add_deps("stdexec main")

    add_urls("https://github.com/myrepo/foo.git")
    add_versions("0.1.0", "<shasum256 or gitcommit>")

    on_install(function (package)
        local configs = {}
        if package:config("shared") then
            configs.kind = "shared"
        end
        import("package.tools.xmake").install(package, configs)
    end)

    on_test(function (package)
        -- TODO check includes and interfaces
        -- assert(package:has_cfuncs("foo", {includes = "foo.h"})
    end)
