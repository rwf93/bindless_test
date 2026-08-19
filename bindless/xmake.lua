--set_toolchains("clang-cl")

--target("shaders-slang-spirv")
--	set_kind("object")
--	add_rules("utils.slang", {
--		outputdir = "$(projectdir)/bindless/src",
--		target = "spirv",
--		extension = ".spv",
--        use_bin2c = true,
--		flags = { "-profile", "spirv_1_6", "-emit-spirv-directly", "-fvk-use-entrypoint-name" }
--	})
--	add_files("src/*.slang")
--	add_packages("slang")

target("bindless_test")
    set_kind("binary")

    add_defines("VK_NO_PROTOTYPES", "GLM_ENABLE_EXPERIMENTAL")

    add_files("src/*.cpp")
    add_files("src/imgui/imgui.cpp")
    add_files("src/imgui/imgui_*.cpp")
    add_files("src/imgui/backends/imgui_impl_vulkan.cpp")
    add_files("src/imgui/backends/imgui_impl_sdl3.cpp")
    add_files("src/imgui/imguizmo/*.cpp")


    add_includedirs("src/imgui")
    add_includedirs("src/imgui/backends")
    add_includedirs("src/imgui/imguizmo")

    set_languages("cxx23")
    add_packages(
        "stb",
        "spdlog",
        "vulkan-headers",
        "vk-bootstrap",
        "vulkan-memory-allocator",
        "glm",
        "libsdl3",
        "slang",
        "fastgltf",
        "tomlcpp"
        --"entt"
    )
    add_rules("defaults_rule")
