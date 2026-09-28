target("compute_test")
   set_kind("binary")
    set_languages("cxx23")
    set_encodings("utf-8")

    add_defines("GLM_ENABLE_EXPERIMENTAL")
    add_deps(
        "gfx",
        "gfx_slang"
    )

    add_files("src/*.cpp")
    add_includedirs("src")

    add_rules("defaults_rule")
