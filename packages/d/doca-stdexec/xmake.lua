
target("doca-stdexec")
    set_kind("headeronly")
    add_includedirs("include", { public = true })

add_packages(
	"doca-argp",
	"doca-aes-gcm",
	"doca-comch",
	"doca-common",
	"doca-dma",
	"doca-rdma",
	"doca-sha",
	{ public = true }
)
-- If there are headers to install, add them here e.g. add_headerfiles("include/(**.h)")

package("doca-stdexec")
    set_kind("library", {headeronly = true})
    set_description("The doca-stdexec package")

    add_deps("pkgconfig::doca-argp")
    add_deps("pkgconfig::doca-aes-gcm")
    add_deps("pkgconfig::doca-comch")
    add_deps("pkgconfig::doca-common")
    add_deps("pkgconfig::doca-dma")
    add_deps("pkgconfig::doca-rdma")
    add_deps("pkgconfig::doca-sha")

    on_load(function (package)
        package:set("installdir", path.join(os.scriptdir(), package:plat(), package:arch(), package:mode()))
    end)

    on_fetch(function (package)
        local result = {}
        result.includedirs = package:installdir("include")
        return result
    end)
