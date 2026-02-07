--set_toolchains("clang-cl")
add_rules("mode.debug", "mode.release")

add_requires(
	"stb 2024.06.01",
	"spdlog",
	"vulkan-headers v1.4.325",
	"vk-bootstrap v1.3.292",
	"vulkan-memory-allocator v3.1.0",
	"glm 1.0.1"--,
    --"entt"
)

add_requires("slang v2025.19.0", {
})

add_requires("fmt 11.0.1", {
	configs = {
		shared = true
	}
})


add_requires("libsdl3", {
	configs = {
		shared = true
	}
})

set_defaultmode("debug")

rule("utils.slang")
    set_extensions(".slang")
    before_buildcmd_file(function(target, batch, file, opt)
        import("lib.detect.find_tool")

        local slangc = find_tool("slangc", {check = function (tool) return tool end, program="slangc", paths = {"$(env PATH)"}})
        local bin2c = find_tool("bin2c", {check = function (tool) return tool end, program="bin2c", paths = {"$(env PATH)"}})

        local outputdir = target:extraconf("rules", "utils.slang", "outputdir") or path.join(target:autogendir(), "rules", "utils", "slang")
        local compile_target = target:extraconf("rules", "utils.slang", "target")
        local extension = target:extraconf("rules", "utils.slang", "extension")
        local profile = target:extraconf("rules", "utils.slang", "profile") or "glsl_460"
        local flags = target:extraconf("rules", "utils.slang", "flags") or {}
        local use_bin2c = target:extraconf("rules", "utils.slang", "use_bin2c") or false

        assert(slangc, "slangc was not found!")
        if use_bin2c then
            assert(bin2c, "bin2c was not found! Please install it to use the bin2c feature.")
        end
        assert(slangc, "bin2c was not found!")
        assert(target, "target is not defined")
        assert(extension, "extension is not defined")

        local filepath = path.join(outputdir, path.filename(file) .. extension)

        if slangc then
            batch:show_progress(opt.progress, "${color.build.object}generating.slang %s", file)
            batch:mkdir(outputdir)

            batch:vrunv(slangc.program, {
                path(file),
                "-profile", profile,
                "-target", compile_target,
                "-o", path(filepath),
                table.unpack(flags)
            })

            if use_bin2c then
                batch:vrunv(bin2c.program, {
                    "-n", "__" .. path.filename(filepath):gsub("%.", "_"),
                    "-o", path.join(outputdir, path.filename(file) .. ".h"),
                    path(filepath)
                })
            end
        end
    end)

package("slang")
    set_homepage("https://github.com/shader-slang/slang")
    set_description("Making it easier to work with shaders")
    set_license("MIT")

    add_urls("https://github.com/shader-slang/slang.git")

    add_versions("v2025.19.0", "5978f934ee9a8a3e710dc743a4af92191639b718")
    add_versions("v2025.11.0", "ee51fe592747fc66bd0b5757207583198068b5bd")
    add_versions("v2025.6.3", "b9300bae08a77df6ef2efe2b62de14a13b10b9a4")
    add_versions("v2024.1.18", "efdbb954c57b89362e390f955d45f90e59d66878")
    add_versions("v2024.1.17", "62b7219e715bd4c0f984bcd98c9767fb6422c78f")

    add_configs("shared", { description = "Build shared library", default = true, type = "boolean", readonly = true })
    add_configs("embed_stdlib_source", { description = "Embed stdlib source in the binary", default = true, type = "boolean" })
    add_configs("embed_stdlib", { description = "Build slang with an embedded version of the stdlib", default = false, type = "boolean" })
    add_configs("full_ir_validation", { description = "Enable full IR validation (SLOW!)", default = false, type = "boolean" })
    add_configs("gfx", { description = "Enable gfx targets", default = false, type = "boolean" })
    add_configs("slangd", { description = "Enable language server target", default = false, type = "boolean" })
    add_configs("slangc", { description = "Enable standalone compiler target", default = true, type = "boolean" })
    add_configs("slangrt", { description = "Enable runtime target", default = false, type = "boolean" })
    add_configs("slang_glslang", { description = "Enable glslang dependency and slang-glslang wrapper target", default = false, type = "boolean" })
    add_configs("slang_llvm_flavor", { description = "How to get or build slang-llvm (available options: FETCH_BINARY, USE_SYSTEM_LLVM, DISABLE)", default = "DISABLE", type = "string" })

    add_deps("cmake")
    add_deps("miniz")

    on_install("windows|x64", "macosx", "linux|x86_64", function (package)
        io.replace("cmake/SlangTarget.cmake", [[set_property(TARGET ${target} PROPERTY SUFFIX ".dylib")]], "", {plain = true})
        local configs = {"-DSLANG_ENABLE_TESTS=OFF", "-DSLANG_ENABLE_EXAMPLES=OFF", "-DSLANG_USE_SYSTEM_MINIZ=ON"}
        table.insert(configs, "-DCMAKE_BUILD_TYPE=" .. (package:is_debug() and "Debug" or "Release"))
        table.insert(configs, "-DSLANG_LIB_TYPE=" .. (package:config("shared") and "SHARED" or "STATIC"))
        table.insert(configs, "-DSLANG_EMBED_STDLIB_SOURCE=" .. (package:config("embed_stdlib_source") and "ON" or "OFF"))
        table.insert(configs, "-DSLANG_EMBED_STDLIB=" .. (package:config("embed_stdlib") and "ON" or "OFF"))
        table.insert(configs, "-DSLANG_ENABLE_FULL_IR_VALIDATION=" .. (package:config("full_ir_validation") and "ON" or "OFF"))
        table.insert(configs, "-DSLANG_ENABLE_ASAN=" .. (package:config("asan") and "ON" or "OFF"))
        table.insert(configs, "-DSLANG_ENABLE_GFX=" .. (package:config("gfx") and "ON" or "OFF"))
        table.insert(configs, "-DSLANG_ENABLE_SLANGD=" .. (package:config("slangd") and "ON" or "OFF"))
        table.insert(configs, "-DSLANG_ENABLE_SLANGC=" .. (package:config("slangc") and "ON" or "OFF"))
        table.insert(configs, "-DSLANG_ENABLE_SLANGRT=" .. (package:config("slangrt") and "ON" or "OFF"))
        table.insert(configs, "-DSLANG_ENABLE_SLANG_GLSLANG=" .. (package:config("slang_glslang") and "ON" or "OFF"))
        table.insert(configs, "-DSLANG_SLANG_LLVM_FLAVOR=" .. package:config("slang_llvm_flavor"))

        io.replace("CMakeLists.txt", [[find_package(Threads REQUIRED)]], [[find_package(Threads REQUIRED)]], {plain = true})

        import("package.tools.cmake").install(package, configs)
        package:addenv("PATH", "bin")
    end)

    on_test(function (package)
        assert(package:check_cxxsnippets({ test = [[
            #include <slang-com-ptr.h>
            #include <slang.h>

            void test() {
                Slang::ComPtr<slang::IGlobalSession> global_session;
                slang::createGlobalSession(global_session.writeRef());
            }
        ]] }, {configs = {languages = "c++17"}}))
    end)

rule("defaults_rule")
 	on_load(function(target)
		import("core.base.task")
		import("core.project.project")

		local precompiled_header = target:extraconf("rules", "defaults_rule", "precompiled_header") or nil
		if precompiled_header then
			target:add("forceincludes", precompiled_header)
			target:set("pcxxheader", precompiled_header)
		end

		target:set("targetdir", "$(projectdir)/output/$(os)_$(arch)/")
	end)

	on_install(function(target)
		import("core.base.task")
		import("core.project.project")
		import("target.action.install")

		install(target, {
			bindir = "./$(host)_$(arch)/",
			libdir = "./$(host)_$(arch)/",
			includedir = "./$(host)_$(arch)/"
		})
	end)

	before_build(function(target)
		import("core.base.task")
		import("core.project.project")

		for name, package in pairs(target:pkgs()) do
			for _, file in ipairs(table.wrap(package:get("libfiles"))) do
				if file:endswith(".dll") or file:endswith(".so") or file:endswith(".dylib") then
					os.vcp(file, "$(projectdir)/output/$(os)_$(arch)/")
				end
			end
		end
	end)

includes("bindless", "shader-compiler")