set_project("doca-stdexec")
set_version("0.1.0")
set_languages("c++23")
add_defines("DOCA_ALLOW_EXPERIMENTAL_API")
set_policy("build.sanitizer.address", true)
add_requires("pkgconfig::doca-argp", { alias = "doca-argp" })
add_requires("pkgconfig::doca-aes-gcm", { alias = "doca-aes-gcm" })
add_requires("pkgconfig::doca-comch", { alias = "doca-comch" })
add_requires("pkgconfig::doca-common", { alias = "doca-common" })
add_requires("pkgconfig::doca-dma", { alias = "doca-dma" })
add_requires("pkgconfig::doca-rdma", { alias = "doca-rdma" })
add_requires("pkgconfig::doca-sha", { alias = "doca-sha" })

add_requires("stdexec main")

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
add_defines("DOCA_ALLOW_EXPERIMENTAL_API") -- Ensure define is applied to compilation


-- If there are headers to install, add them here e.g. add_headerfiles("include/(**.h)")

target("rdma_loopback")
    set_kind("binary")
    add_files("test/rdma_loopback.cpp")
    add_deps("doca-stdexec")
    add_packages("stdexec")

target("doca-stdexec")
    set_kind("headeronly")
    add_headerfiles("include/**.hpp")
    add_headerfiles("include/**.h")
    add_includedirs("include", { public = true })
	add_packages("stdexec")