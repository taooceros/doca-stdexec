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
-- If there are headers to install, add them here e.g. add_headerfiles("include/(**.h)")

target("rdma_loopback")
    set_kind("binary")
    add_files("test/rdma_loopback.cpp")
    add_deps("doca-stdexec")
    add_defines("DOCA_ALLOW_EXPERIMENTAL_API") -- Ensure define is applied to compilation
    add_packages("stdexec")

package("doca-stdexec")
    set_description("The doca-stdexec package")
    set_license("Apache-2.0")
    add_deps("doca-stdexec") -- optional: add other package dependencies

    set_urls("https://github.com/taooceros/doca-stdexec.git")
    add_versions("0.1.0", "auto") -- specify version
package_end()