target("shader-compiler")
    set_kind("binary")
    add_files("src/*.cpp")
    add_packages(
        "slang"
    )
    set_languages("cxx23")
    add_rules("defaults_rule")