target("bindless_test")
    set_kind("binary")
    set_languages("cxx26")
    set_encodings("utf-8")

    add_defines("GLM_ENABLE_EXPERIMENTAL")
    add_deps(
        "gfx",
        "gfx_image_io",
        "gfx_present",
        "gfx_sdl",
        "gfx_slang",
        "gfx_framegraph",
        "gfx_imgui"
    )

    add_files("src/*.cpp")
    add_includedirs("src")

    add_packages(
        "spdlog",
        "glm",
        "fastgltf",
        "tomlcpp"
    )
    add_rules("defaults_rule")
